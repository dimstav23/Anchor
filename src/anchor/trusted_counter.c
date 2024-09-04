#include "trusted_counter.h"
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* Trusted counter emulation */

struct Counter* counters_pool[MAX_COUNTERS];
uint64_t stable_counters[MAX_COUNTERS];
int _index = 0;
int _max_index = 0;
pthread_rwlock_t _rw_lock = PTHREAD_RWLOCK_INITIALIZER;
uint64_t hw_index = 0;
char* counter_file_path = NULL;
char* counters_map_addr = NULL;
size_t counter_file_size =
    (MAX_COUNTERS + 1) * sizeof(uint64_t);  // counters + EOF

int keep_working = 0;
pthread_t timer_thread;
// The measurements below were calculated by running 
// a sample application that only contains a loop
// with a for loop with LOOP_DELAY iterations over a `volatile int` variable
// in each environment (i.e., native and scone)
// To achieve similar results with the paper, adjust accordingly!
#ifdef SCONE
#define LOOP_DELAY 40000000  // scone 60-80ms loop
#else
#define LOOP_DELAY 40000000  // native average ~60-80ms loop per tx
#endif

/*
 * create_counter_thread: creates the responsible thread for the saving of the
 * counters into the file and the mocked delay for the stabilization
 */
void create_counter_thread() {
  keep_working = 1;
  pthread_create(&timer_thread, NULL, &delay, NULL);
}

/*
 * create_counter: creates counter with the initial value
 */
struct Counter* create_counter(int initial_value) {
  assert(_index < MAX_COUNTERS);
  struct Counter* _new = (struct Counter*)malloc(sizeof(struct Counter));
  _new->_counter = initial_value;
  _new->_index = _index;
  counters_pool[_index] = _new;
  printf("%s _index : %d\n", __func__, _index);
  __sync_add_and_fetch(&_index, 1);
  __sync_add_and_fetch(&_max_index, 1);
  return _new;
}

/*
 * create_counter_idx: creates counter with the initial value at position
 * indicated by index
 */
struct Counter* create_counter_idx(int initial_value, int index) {
  assert(index < MAX_COUNTERS);
  struct Counter* _new = (struct Counter*)malloc(sizeof(struct Counter));
  if (!~index) {
    index = _max_index;
  }

  _new->_counter = initial_value;
  _new->_index = index;
  counters_pool[index] = _new;

  if (index >= _max_index) {
    __atomic_store_n(&_max_index, index + 1, __ATOMIC_SEQ_CST);
  }

  return _new;
}

/*
 * get_counter: get counter object from the counters_pool based on the given
 * index
 */
struct Counter* get_counter(int index) {
  if (index < MAX_COUNTERS)
    return counters_pool[index];
  else
    return NULL;
}

/*
 * set_counter: updates counter value
 */
void set_counter(struct Counter* counter, uint64_t value) {
  pthread_rwlock_rdlock(&_rw_lock);
  __atomic_store_n(&counter->_counter, value, __ATOMIC_SEQ_CST);
  pthread_rwlock_unlock(&_rw_lock);
}

/*
 * load_counters: loads the counters from the specified file
 */
void load_counters(const char* file_path) {
  uint64_t temp;
  char token;
  int ret;
  // if no path is set or new path is given
  if (counter_file_path ==
      NULL /* || strcmp(file_path, counter_file_path) != 0*/) {
    counter_file_path = (char*)malloc((strlen(file_path) + 1) * sizeof(char));
    if (counter_file_path == NULL) {
      fprintf(stderr, "malloc failed\n");
      exit(1);
    }
    strcpy(counter_file_path, file_path);
  }

  FILE* fp;
  fp = fopen(file_path, "a+");
  if (fp == NULL) {
    perror("Error : ");
  }

  while ((ret = fscanf(fp, "%" SCNu64 "%c", &temp, &token)) != EOF) {
    if (ret & 0x2 && token == ',')  // if ret == 2 we have read value + comma
      create_counter_idx(temp, NEXT_AVAIL_IDX);
    else
      hw_index = temp;  // if ret == 1 we reached hw_index
  }

  if (fclose(fp) != 0) {
    perror("Error : ");
    exit(-1);
  }

  // counters initialised in the volatile memory - startup of the thread
  create_counter_thread();
}

/*
 * load_counters_mmap: loads the counters from the specified file after mmaping
 * this file
 */
void load_counters_mmap(const char* file_path) {
  // if no path is set or new path is given
  if (counter_file_path ==
      NULL /* || strcmp(file_path, counter_file_path) != 0*/) {
    counter_file_path = (char*)malloc((strlen(file_path) + 1) * sizeof(char));
    if (counter_file_path == NULL) {
      fprintf(stderr, "malloc failed\n");
      exit(1);
    }
    strcpy(counter_file_path, file_path);
  }

  // check if file already exists to determine whether to read from it or not
  int scan_file = access(counter_file_path, F_OK);

  size_t mapped_len;
  int is_pmem;
  char* counters_read_addr;
  if ((counters_read_addr =
           pmem_map_file(counter_file_path, counter_file_size, PMEM_FILE_CREATE,
                         0666, &mapped_len, &is_pmem)) == NULL) {
    perror("counters' pmem_map_file");
    exit(1);
  }
  counters_map_addr = counters_read_addr;
  if (scan_file == 0) {
    uint64_t* temp = (uint64_t*)counters_read_addr;
    while (1) {
      if (*(temp + 1) != EOF) {
        create_counter_idx(*temp, NEXT_AVAIL_IDX);
        temp++;
      } else {
        hw_index = *temp;
        break;
      }
    }
  }

  // counters initialised in the volatile memory - startup of the thread
  create_counter_thread();
}

/*
 * write_counters: stores the current counter to the specified file after
 * mmaping this file
 */
void write_counters(const char* file_path) {
  FILE* fp;
  fp = fopen(file_path, "w");
  if (fp == NULL) {
    perror("Error : counters file");
    exit(-1);
  }
  for (int i = 0; i < _max_index; i++) {
    fprintf(fp, "%" PRIu64 ",",
            counters_pool[i] != NULL ? counters_pool[i]->_counter : 0);
  }
  __sync_add_and_fetch(&hw_index, 1);
  fprintf(fp, "%ld", hw_index);
  if (fclose(fp) != 0) {
    perror("Error : ");
    exit(-1);
  }
}

/*
 * write_counters_mmap: stores the current counter to the specified file
 */
void write_counters_mmap(const char* file_path) {
  uint64_t* counter_write_addr = (uint64_t*)counters_map_addr;
  for (int i = 0; i < _max_index; i++) {
    *counter_write_addr =
        counters_pool[i] != NULL ? counters_pool[i]->_counter : 0;
    stable_counters[i] = *counter_write_addr;
    counter_write_addr++;
  }
  __sync_add_and_fetch(&hw_index, 1);
  *counter_write_addr = hw_index;
  *(counter_write_addr + 1) = EOF;  // to know how many counters I already have
}

static volatile int delay_var = 0;
/*
 * delay: adds mocked delay for emulating writing into a file - unstable period
 * interval
 */
void* delay() {
  while (keep_working) {
    pthread_rwlock_wrlock(&_rw_lock);
    // printf("locked to write\n");
    write_counters_mmap(counter_file_path);
    pthread_rwlock_unlock(&_rw_lock);
    // printf("unlocked to write\n");
    // this is our mocked delay for the unstable period
    // struct timespec start, end;
    // clock_gettime(CLOCK_MONOTONIC, &start);
    // volatile int delay;
    for (delay_var = 0; delay_var < LOOP_DELAY; delay_var++) {
      // time(NULL);
      // asm("");
    }
    // delay_var = 0;
    // clock_gettime(CLOCK_MONOTONIC, &end);
    // double time_taken;
    // time_taken = (end.tv_sec - start.tv_sec) * 1e9;
    // time_taken = (time_taken + (end.tv_nsec - start.tv_nsec)) * 1e-9;
    // printf("time for the mocked loop %f\n", time_taken);
  }
  return NULL;
}

int query_curr_delay() { return delay_var; }

/*
 * inc: increments the counter value by one
 */
uint64_t inc(struct Counter* counter) {
  // clock_t begin = clock();
  pthread_rwlock_rdlock(&_rw_lock);
  // pthread_rwlock_wrlock(&_rw_lock);
  // clock_t end = clock();
  // double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
  // if (time_spent > 0.000001)
  //    printf("waiting for counter lock : %f\n", time_spent);
  uint64_t ret;
  ret = __sync_fetch_and_add(&counter->_counter, 1);
  pthread_rwlock_unlock(&_rw_lock);

  return ret;
}

/*
 * delete_counter: frees the allocated space of a counter
 */
void delete_counter(struct Counter** _c) {
  if (*_c != NULL) {
    free(*_c);
    *_c = NULL;
  }
}

/*
 * delete_all_counters: deallocates all the memory dedicated to the counters
 */
void delete_all_counters() {
  pthread_rwlock_wrlock(&_rw_lock);
  write_counters(counter_file_path);
  pthread_rwlock_unlock(&_rw_lock);
  for (int i = 0; i < _max_index; i++)
    if (counters_pool[i] != NULL) delete_counter(&counters_pool[i]);

  _max_index = 0;
  _index = 0;
  hw_index = 0;
}

/*
 * delete_all_counters_mmap: deallocates all the memory dedicated to the
 * counters and unmaps the respective file
 */
void delete_all_counters_mmap() {
  pthread_rwlock_wrlock(&_rw_lock);
  write_counters_mmap(
      counter_file_path);  // to save the latest counter before closing the pool
  pthread_rwlock_unlock(&_rw_lock);
  for (int i = 0; i < _max_index; i++)
    if (counters_pool[i] != NULL) {
#ifdef DEBUG
      printf("del cnt %d\n", i);
      fflush(stdout);
#endif
      delete_counter(&counters_pool[i]);
    }
  _max_index = 0;
  _index = 0;
  hw_index = 0;
  if (counters_map_addr != NULL)
    pmem_unmap(counters_map_addr, counter_file_size);
  else
    perror("counters' file unmapping:");
}

/*
 * close_counters: closes timer thread and the counters file
 */
void close_counters() {
  keep_working = 0;
  pthread_join(timer_thread, NULL);
}

/*
 * counters_cleanup: calls delete_all_counters_mmap and close_counters (if
 * applicable) destroys pthread_rwlock frees the allocated memory for the
 * counters' file path
 */
void counters_cleanup() {
  close_counters();
  delete_all_counters_mmap();
  assert(pthread_rwlock_destroy(&_rw_lock) == 0);

  free(counter_file_path);
  counter_file_path = NULL;
}

int query_counter(int index, uint64_t value) {
  return (stable_counters[index] >= value);
}