#include <cstdlib>
#include <cstring>
#include <iostream>

#include "Server.h"
#include "test_common.h"

#include <ex_common.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "hashmap_anchor/hashmap.h"
#include "map.h"
#include "map_btree.h"
#include "map_btree_opt.h"
#include "map_ctree.h"
#include "map_ctree_opt.h"
#include "map_hashmap_tx.h"
#include "map_hashmap_tx_opt.h"
#include "map_rbtree.h"
#include "map_rbtree_opt.h"
#include "map_rtree.h"
#include "map_rtree_opt.h"
#include "map_skiplist.h"
#include "map_skiplist_opt.h"

#include "generate_keys.h"

#include "trusted_counter.h"
#define LOOP_DELAY 40000000

#include <libanchor.h>
#include <libpmemobj.h>

#define MAX_PAIRS 500000
#define ALLOC_OVERHEAD 64
/* Values less than 2048 is not suitable for current rtree implementation */
#define SIZE_PER_KEY 2048
/* Values less than 3 is not suitable for current rtree implementation */
#define FACTOR 3

#define POOL_SIZE                                                              \
	MAX_PAIRS *(SIZE_PER_KEY + DATA_SIZE + ALLOC_OVERHEAD) * FACTOR

POBJ_LAYOUT_BEGIN(map);
POBJ_LAYOUT_ROOT(map, struct root);
POBJ_LAYOUT_END(map);

struct root {
	PMEMoid map;
};

static PMEMobjpool *pop;
static struct map_ctx *mapc;
static PMEMoid root;
static PMEMoid map;

static int alloc = 1;
static const size_t key_size = sizeof(uint64_t);
static int value_size = 512;

#define OBJ_TYPE_NUM 1
#define WORKLOAD_PATH_SIZE 1024
#define TX_BATCH_SIZE 10000 // 10000
int tx_count = 0;
static const char *wl_dir_path = "../src/benchmarks/ycsb_traces/";
static int zipf_exp = 99;
static int key_number = 100000;
static uint64_t wl_size = 10000000;
static uint64_t actual_workload = wl_size/2;

static void
bench_tx_batch_start(PMEMobjpool *pop, size_t index)
{
	if (tx_count == 0) {
		pmemobj_tx_begin(pop, NULL, TX_PARAM_NONE);
		tx_count++;
	} else {
		tx_count++;
	}
}

static void
bench_tx_batch_end(size_t index, size_t n_ops)
{
	if (tx_count == TX_BATCH_SIZE || index == (n_ops - 1)) {
		pmemobj_tx_commit();
		pmemobj_tx_end();
		tx_count = 0;
	}
}

/*
 * keys_file_construct -- construct the file name
 */
static char *
keys_file_construct(int read_ratio)
{
	char *ret = (char *)malloc(WORKLOAD_PATH_SIZE * sizeof(char));
	snprintf(ret, WORKLOAD_PATH_SIZE,
		 "%s/simple_trace_w_%ld_k_%d_a_%.2f_r_%.2f_keys.txt",
		 wl_dir_path, wl_size, key_number, float(zipf_exp) / 100,
		 float(read_ratio) / 100);
	return ret;
}

/*
 * data_structure_warmup -- initialize KV with keys of custom workload
 */
static int
data_structure_warmup(PMEMobjpool *pop, PMEMoid map, PMEMoid root,
		      struct map_ctx *mapc, int read_ratio)
{
	int ret = 0;

	char *keys_file = keys_file_construct(read_ratio);

	std::string tr(keys_file);
	std::vector<anchor_tr_::Keys_cmd> keys = anchor_tr_::keys_init(tr);
	free(keys_file);

	assert((size_t)key_number >= keys.size());

	for (size_t i = 0; i < keys.size(); i++) {
		bench_tx_batch_start(pop, i);
		PMEMoid oid;
		if (alloc)
			oid = sobj_tx_alloc(value_size, OBJ_TYPE_NUM);
		else
			oid = root;

		ret = map_insert(mapc, map, keys.at(i).key_hash, oid);

		bench_tx_batch_end(i, keys.size());

		if (ret)
			break;
	}
	keys.clear();
	keys.shrink_to_fit();
	epc_force_cache_flush();
	return ret;
}

int tx_start = 0;
int op_cnt = 0;
/*
 * kv_put
 */
static int
kv_put(const void *key, size_t key_len, void *value, size_t val_size)
{
	uint64_t *u_key = (uint64_t *)key;
	PMEMoid oid = root; // dummy value for the cases when we do not
			    // explicitly allocate objects
	
	if (tx_start == 0) {
		pmemobj_tx_begin(mapc->pop, NULL, TX_PARAM_NONE);
		tx_start = 1;
	}
	else if (op_cnt % 500 == 0) 
	{
		//uint64_t curr;
		//curr = get_tsc();
		//printf("curr delay : %d\n",query_curr_delay());
		//fflush(stdout);
		if (query_curr_delay() > 0.7 * LOOP_DELAY) 
		{
			//printf("commited at %d\n", op_cnt);
			fflush(stdout);
			pmemobj_tx_commit();
			pmemobj_tx_end();
			pmemobj_tx_begin(mapc->pop, NULL, TX_PARAM_NONE);
			//tx_start = 0;
		}
	}

	//TX_BEGIN(mapc->pop)
	//{	
	if (op_cnt % 1000000 == 0){
                printf("%d\n",op_cnt);
                fflush(stdout);
        }
	//bench_tx_batch_start(mapc->pop, op_cnt);
		if (alloc) {
			oid = sobj_tx_alloc(val_size, 0);
			if (OID_IS_NULL(oid)) {
				fprintf(stderr,
					"object allocation for key %ld failed\n",
					*u_key);
			}
		}
		if (map_insert(mapc, map, *u_key, oid) != 0) {
			fprintf(stderr, "insertion of key %ld failed\n",
				*u_key);
			return -1;
		}
	//bench_tx_batch_end(op_cnt, actual_workload);
	//}
	//TX_ONABORT
	//{
	//	printf("allocation transaction aborted\n");
	//}
	//TX_END
	
	
	if (op_cnt == (actual_workload - 1) && tx_start == 1) 
	{
		//printf("finalised at %ld \n", info->index);
		pmemobj_tx_commit();
		pmemobj_tx_end();
		tx_start=0;
	}
	

	op_cnt++;
	// return next stab point
	return 0;
}

/*
 * kv_delete
 */
static int
kv_delete(const void *key, size_t key_len)
{
	uint64_t *u_key = (uint64_t *)key;

	if (OID_IS_NULL(map_remove(mapc, map, *u_key))) {
		fprintf(stderr, "deletion of key %ld failed\n", *u_key);
		return -1;
	}
	// return next stab point
	return 0;
}

/*
 * kv_get
 */
static const void *
kv_get(const void *key, size_t key_len, size_t *data_len)
{
	uint64_t *u_key = (uint64_t *)key;
	PMEMoid value_obj;

	if (op_cnt % 1000000 == 0){
		printf("%d\n",op_cnt);
		fflush(stdout);
	}
	//bench_tx_batch_start(mapc->pop, op_cnt);
	//printf("get %d\n",op_cnt);
	//fflush(stdout);

	if (tx_start == 0) {
		tx_start = 1;
		pmemobj_tx_begin(mapc->pop, NULL, TX_PARAM_NONE);
	}
	else if (op_cnt % 100 == 0) 
	{
		//uint64_t curr;
		//curr = get_tsc();
		//printf("curr delay : %d\n",query_curr_delay());
		//fflush(stdout);
		if (query_curr_delay() > 0.7 * LOOP_DELAY) 
		{
			//printf("commited at %d\n", op_cnt);
			//fflush(stdout);
			pmemobj_tx_commit();
			pmemobj_tx_end();
			pmemobj_tx_begin(mapc->pop, NULL, TX_PARAM_NONE);
			//tx_start = 0;
		}
	}
	

	value_obj = map_get(mapc, map, *u_key);

	if (OID_IS_NULL(value_obj)) {
		printf("key %ld not found\n", *u_key);
		return NULL;
	}

	uint64_t size;
	void *ret = sobj_direct_size(mapc->pop, value_obj, &size);
	*data_len = size;
	
	//bench_tx_batch_end(op_cnt, actual_workload);
	
	if (op_cnt == (actual_workload - 1) && tx_start == 1) 
	{
		//printf("finalised at %ld \n", info->index);
		pmemobj_tx_commit();
		pmemobj_tx_end();
		tx_start=0;
	}
	

	op_cnt++;
	return ret;
	// return sobj_direct(mapc->pop, value_obj);
}

int
main(int argc, char *argv[])
{
	if (argc < 6 || argc > 8) {
		printf("usage: %s "
		       "hashmap_tx|ctree|btree|"
		       "rtree|rbtree|skiplist"
		       " file-name <ip-address> read-ratio value-size [<seed>]\n",
		       argv[0]);
		return 1;
	}

	const struct map_ops *ops = NULL;
	const char *type = argv[1];
	const char *path = argv[2];
	value_size = atoi(argv[5]);
	if (strcmp(type, "hashmap_tx") == 0) {
		ops = MAP_HASHMAP_TX_OPT;
	} else if (strcmp(type, "ctree") == 0) {
		ops = MAP_CTREE_OPT;
	} else if (strcmp(type, "btree") == 0) {
		ops = MAP_BTREE_OPT;
	} else if (strcmp(type, "rtree") == 0) {
		ops = MAP_RTREE_OPT;
	} else if (strcmp(type, "rbtree") == 0) {
		ops = MAP_RBTREE_OPT;
	} else if (strcmp(type, "skiplist") == 0) {
		ops = MAP_SKIPLIST_OPT;
	} else {
		fprintf(stderr, "invalid container type -- '%s'\n", type);
		return 1;
	}

	/* SERVER : key exchange */
	/* A 128 bit key */
	uint8_t *enc_key = (uint8_t *)"012345678901234";
	size_t enc_key_len = 16;
	/* A 128 bit IV */
	uint8_t *iv = (uint8_t *)"012345678901234";
	size_t iv_len = 16;

	if (file_exists(path) == 0) {
        unlink(path);
		unlink("/dev/shm/Manifest");
		unlink("/dev/shm/amcs");
		unlink("/dev/shm/Metadata_log");
    } 
	/* SERVER : pool creation  & map init*/
	if (file_exists(path) != 0) {
		size_t size_per_key = alloc
			? SIZE_PER_KEY + value_size + ALLOC_OVERHEAD
			: SIZE_PER_KEY;
		size_t pool_size = MAX_PAIRS * size_per_key * FACTOR;

		pop = spool_create(path, POBJ_LAYOUT_NAME(map), pool_size,
				   CREATE_MODE_RW, "/dev/shm/Manifest",
				   "/dev/shm/amcs", "/dev/shm/Metadata_log",
				   enc_key, enc_key_len, iv, iv_len);

		if (pop == NULL) {
			fprintf(stderr, "failed to create pool: %s\n",
				pmemobj_errormsg());
			return 1;
		}

		struct hashmap_args args;

		if (argc > 6)
			args.seed = atoi(argv[6]);
		else
			args.seed = (uint32_t)time(NULL);
		srand(args.seed);

		mapc = map_ctx_init(ops, pop);
		if (!mapc) {
			spool_close(pop);
			perror("map_ctx_init");
			return 1;
		}

		root = sobj_root_get(pop, sizeof(struct root));
		struct root *temp_root =
			(struct root *)sobj_read(pop, root, 1, NULL);

		map_create(mapc, &temp_root->map, &args);
		sobj_write(pop, root, temp_root);
		map = temp_root->map;
		free(temp_root);

		const int read_ratio = atoi(argv[4]);
		data_structure_warmup(pop, map, root, mapc, read_ratio);

	} else {
		pop = spool_open(path, POBJ_LAYOUT_NAME(map),
				 "/dev/shm/Manifest", "/dev/shm/amcs",
				 "/dev/shm/Metadata_log", enc_key, enc_key_len,
				 iv, iv_len);
		if (pop == NULL) {
			fprintf(stderr, "failed to open pool: %s\n",
				pmemobj_errormsg());
			return 1;
		}
		mapc = map_ctx_init(ops, pop);

		if (!mapc) {
			spool_close(pop);
			perror("map_ctx_init");
			return 1;
		}

		root = sobj_root_get(pop, sizeof(struct root));
		struct root *temp_root =
			(struct root *)sobj_read(pop, root, 1, NULL);
		map = temp_root->map;
		free(temp_root);
	}

	std::string ip(argv[3]);
	const uint16_t standard_udp_port = 31850;
	if (0 != anchor_server::init(ip, standard_udp_port))
		return 1;
	global_params.key_size = sizeof(uint64_t);
	global_params.val_size = value_size;
	global_params.event_loop_iterations = 16;//128;//600;
	/* SERVER : client requests */

	const uint8_t num_clients = 1;
	int ret = -1;
	if (anchor_server::host_server(key_do_not_use, num_clients,
				       KEY_SIZE_NETWORK + VAL_SIZE, true,
				       kv_get, kv_put, kv_delete)) {
		std::cerr << "Failed to host server" << std::endl;
		return ret;
	}

	/* SERVER : termination */

	anchor_server::close_connection(false);
	std::cout << "Shut down server" << std::endl;

	anchor_server::terminate();

	map_ctx_free(mapc);

	spool_close(pop);

	return 0;
}
