#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <sys/mman.h>

#ifdef RDMA
#define ERPC_INFINIBAND true
#else
#define ERPC_DPDK true
#endif

#include "generate_traces.h"
#include "rdtsc.h"

#include "Client.h"
#include "test_common.h"

#define INPUT_BUF_LEN 1000
static const size_t key_size = sizeof(uint64_t);
static int value_size = 512;

uint16_t port = 31850;

thread_local unsigned char *value_buf;
thread_local unsigned char *key_buf;

struct workload_params {
	uint64_t *keys;
	anchor_tr_::Trace_cmd::operation *op;
};

struct test_params {
	uint16_t port;
	uint8_t id;
	size_t put_requests;
	size_t get_requests;
	size_t del_requests;
};

struct test_results {
	size_t successful_puts;
	size_t failed_puts;
	uint64_t put_time;
	size_t successful_gets;
	size_t failed_gets;
	uint64_t get_time;
	size_t successful_deletes;
	size_t failed_deletes;
	uint64_t delete_time;
	size_t timeouts;
	size_t invalid_responses;
	uint64_t time_all;
};

uint64_t start_timestamp = 0;
uint64_t end_timestamp = 0;

thread_local struct test_params local_params = {0};
thread_local struct test_results local_results = {0};
struct workload_params wl_params = {0};

#define WORKLOAD_PATH_SIZE 1024
static const char *wl_dir_path = "../src/benchmarks/ycsb_traces/";
static int zipf_exp = 99;
static int key_number = 100000;
static uint64_t wl_size = 10000000;
static uint64_t actual_workload = wl_size/2;

#ifdef SCONE
#define SYS_untrusted_mmap 1025
void *
scone_kernel_mmap(void *addr, size_t length, int prot, int flags, int fd,
		  off_t offset)
{
	return (void *)syscall(SYS_untrusted_mmap, addr, length, prot, flags,
			       fd, offset);
	// printf("scone mmap syscall number : %d\n", SYS_untrusted_mmap);
}
void *
mmap_helper(size_t size)
{
	return scone_kernel_mmap(NULL, size, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANON, -1, 0);
}
#else
void *
mmap_helper(size_t size)
{
	return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
		    -1, 0);
}
#endif

void
munmap_helper(void *addr, size_t size)
{
	munmap(addr, size);
}

void
print_results(const char* data_structure, int read_ratio) {
	printf("type;keys;ops;read_ratio;key_size;value_size;time;throughput");
	printf("\n");
	long double total_time = get_time_in_s(end_timestamp - start_timestamp);
	long double throughput = (double)actual_workload / total_time;
	printf("%s;%d;%ld;%d;%ld;%ld;%Lf;%Lf",
			data_structure,
			key_number,
			actual_workload,
			read_ratio,
			KEY_SIZE_NETWORK,
			VAL_SIZE,
			total_time,
			throughput);
}

/*
 * keys_file_construct -- construct the file name
 */
static char *
wl_file_construct(int read_ratio)
{
	char *ret = (char *)malloc(WORKLOAD_PATH_SIZE * sizeof(char));
	snprintf(ret, WORKLOAD_PATH_SIZE,
		 "%s/simple_trace_w_%ld_k_%d_a_%.2f_r_%.2f.txt", wl_dir_path,
		 wl_size, key_number, float(zipf_exp) / 100,
		 float(read_ratio) / 100);
	return ret;
}

/*
 * map_custom_wl_trace_init -- custom workload init
 */
static std::vector<anchor_tr_::Trace_cmd>
workload_trace_init(char *wl_file)
{
	std::string tr(wl_file);
	std::vector<anchor_tr_::Trace_cmd> trace = anchor_tr_::trace_init(tr);

	return trace;
}

void
release_workload()
{
	munmap_helper(wl_params.keys, wl_size * sizeof(*(wl_params.keys)));
	munmap_helper(wl_params.op, wl_size * sizeof(*(wl_params.op)));
}

/*
 * data_structure_warmup -- initialize KV with keys of custom workload
 */
static int
workload_scan(int read_ratio)
{
	int ret = 0;

	char *wl_file = wl_file_construct(read_ratio);
	std::vector<anchor_tr_::Trace_cmd> trace = workload_trace_init(wl_file);
	free(wl_file);

	assert(wl_size == trace.size());

	wl_params.keys =
		(uint64_t *)mmap_helper(wl_size * sizeof(*(wl_params.keys)));
	if (!wl_params.keys) {
		perror("malloc keys");
		goto err;
	}
	wl_params.op = (anchor_tr_::Trace_cmd::operation *)mmap_helper(
		wl_size * sizeof(*(wl_params.op)));
	if (!wl_params.op) {
		perror("malloc op");
		goto err_free_ops;
	}

	for (uint64_t i = 0; i < wl_size; i++) {
		wl_params.keys[i] = trace.at(i).key_hash;
		wl_params.op[i] = trace.at(i).op;
	}

	trace.clear();
	trace.shrink_to_fit();

	return ret;

err_free_ops:
	munmap_helper(wl_params.keys, wl_size * sizeof(*(wl_params.keys)));
err:
	return -1;
}

void
evaluate_failed_op(ret_val status)
{
	if (likely(status == TIMEOUT))
		local_results.timeouts++;
	else
		local_results.invalid_responses++;
}

void
put_callback(ret_val status, const void *tag)
{
	if (status == OP_SUCCESS || status == OP_FAILED) {
		if (status == OP_SUCCESS)
			local_results.successful_puts++;
		else
			local_results.failed_puts++;
	} else {
		evaluate_failed_op(status);
	}
}

void
get_callback(ret_val status, const void *tag)
{
	if (status == OP_SUCCESS || status == OP_FAILED) {
		if (OP_SUCCESS == status)
			local_results.successful_gets++;
		else
			local_results.failed_gets++;
	} else {
		evaluate_failed_op(status);
	}
}

void
del_callback(ret_val status, const void *tag)
{
	if (status == OP_SUCCESS || status == OP_FAILED) {
		if (status == OP_SUCCESS)
			local_results.successful_deletes++;
		else
			local_results.failed_deletes++;
	} else {
		evaluate_failed_op(status);
	}
}

static void
unknown_command(const char *str)
{
	fprintf(stderr, "unknown command '%c'\n", str[0]);
}

void
queue_check(Client *client)
{
	if (client->queue_full()) {
		std::this_thread::sleep_for(
			chrono::microseconds(CLIENT_TIMEOUT));
		client->run_event_loop_n_times(LOOP_ITERATIONS);
	}
}

int
issue_requests(Client *client, int read_ratio)
{
	/* We have all start times in an array and pass the pointers
	 * to the put/get functions as a tag that is returned in the callback
	 */
	size_t gets_performed = 0,
	       puts_performed = 0; //, deletes_performed = 0;
	int ret = 0;

	if (workload_scan(read_ratio) != 0) {
		perror("workload scan failed");
		return -1;
	}
	struct timespec *time_now = nullptr;
	//take starting timestamp
	start_timestamp = get_tsc();
	
	for (uint64_t i = 0; i < actual_workload; i++) {
		/*
		if (i % 500000 == 0) {
			printf("%ld\n", i);
			fflush(stdout);
		}
		*/
		uint64_t key = wl_params.keys[i];
		anchor_tr_::Trace_cmd::operation op = wl_params.op[i];
		switch (op) {
			case anchor_tr_::Trace_cmd::Get:
				queue_check(client);
				if (0 > client->get((void *)&key,
						    KEY_SIZE_NETWORK,
						    (void *)value_buf, nullptr,
						    get_callback, time_now,
						    LOOP_ITERATIONS)) {
					cerr << "get() failed" << endl;
					return -1;
				} else {
					gets_performed++;
				}
				break;
			case anchor_tr_::Trace_cmd::Put:
				queue_check(client);
				if (0 > client->put((void *)&key,
						    KEY_SIZE_NETWORK,
						    (void *)value_buf, VAL_SIZE,
						    put_callback, time_now,
						    LOOP_ITERATIONS)) {
					cerr << "put() failed" << endl;
					return -1;
				} else {
					puts_performed++;
				}
				break;
			default:
				break;
		}
	}

	size_t total_ops = local_params.put_requests +
		local_params.get_requests + local_params.del_requests;

	// Run event loop if there are requests without responses
	for (size_t i = 0; i < 128 &&
	     (local_results.failed_puts + local_results.successful_puts +
		      local_results.failed_gets +
		      local_results.successful_gets +
		      local_results.failed_deletes +
		      local_results.successful_deletes +
		      local_results.timeouts + local_results.invalid_responses <
	      total_ops);
	     i++) {
		client->run_event_loop_n_times(LOOP_ITERATIONS);
	}
	
	//take end timestamp
	end_timestamp = get_tsc();
	this_thread::sleep_for(3s);
	(void)client->disconnect();

	return ret;
}

void
test_thread(std::string *server_hostname, int read_ratio)
{

	Client client{0, KEY_SIZE_NETWORK, VAL_SIZE};
	key_buf = static_cast<unsigned char *>(calloc(1, KEY_SIZE_NETWORK));
	value_buf = static_cast<unsigned char *>(malloc(VAL_SIZE));
	if (!(key_buf && value_buf))
		goto end_test_thread;

	if (0 > client.connect(*server_hostname, port, key_do_not_use)) {
		cerr << "Client Thread : Failed to connect to server" << endl;
		return;
	}

	if (issue_requests(&client, read_ratio) != 0)
		perror("issue requrests failure\n");

end_test_thread:
	free(key_buf);
	free(value_buf);
}

void
run_client(string &server_hostname, int read_ratio)
{

	test_thread(&server_hostname, read_ratio);

	std::cout << "Ended experiment" << std::endl;
}

void
print_usage(const char *arg0)
{
	cout << "Usage: " << arg0
	     << " <client ip-address> <server ip-address> <data-structure> <read-ratio> <value-size>" << endl;
	// global_test_params::print_options();
}

int
main(int argc, const char *argv[])
{
	if (argc < 6) {
		print_usage(argv[0]);
		return 1;
	}
	value_size = atoi(argv[5]);
	global_params.key_size = sizeof(uint64_t);
	global_params.val_size = value_size;
	global_params.event_loop_iterations = 16;//1000;

	string client_hostname(argv[1]);
	string server_hostname(argv[2]);
	Client::init(client_hostname, port);

	const char* data_structure = argv[3];
	int read_ratio = atoi(argv[4]);
	
	// global_params.parse_args(argc - 3, argv + 3);
	start_timestamp = 0;
	end_timestamp = 0;

	run_client(server_hostname, read_ratio);

	// Wait for the server to prepare:
	this_thread::sleep_for(3s);
	Client::terminate();

	release_workload();

	print_results(data_structure, read_ratio);
	return 0;
}
