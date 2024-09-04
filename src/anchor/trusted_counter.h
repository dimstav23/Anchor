#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <libpmem.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Trusted counter emulation header file */

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_COUNTERS \
  300 + 1 +          \
      2  // 300 counters for 50 lanes + 1 for manifest + 2 for metadata log
#define METADATA_COUNTERS 4  // number of metadata counters

#define DELIMITER ','      // file delimiter : .csv
#define NEXT_AVAIL_IDX -1  // flag to get the next available counter idx

#ifndef COUNTER
#define COUNTER
struct Counter {
  uint64_t _counter;
  int _index;
};
#endif

extern struct Counter* counters_pool[MAX_COUNTERS];
extern int _index;
extern int unstable_period;
extern uint64_t hw_index;

/*
 * delay: adds mocked delay for emulating writing into a file - unstable period
 * interval
 */
void* delay();

/*
 * inc: increments the counter value by one and returns the new value
 */
uint64_t inc(struct Counter* counter);

/*
 * create_counter: creates counter with the initial value
 */
struct Counter* create_counter(int initial_value);

/*
 * create_counter_idx: creates counter with the initial value at position
 * indicated by index
 */
struct Counter* create_counter_idx(int initial_value, int index);

/*
 * delete_counter: frees the allocated space of a counter
 */
void delete_counter(struct Counter** _c);

/*
 * get_counter: get counter object from the counters_pool based on the given
 * index
 */
struct Counter* get_counter(int index);

/*
 * set_counter: updates counter value
 */
void set_counter(struct Counter* counter, uint64_t value);

/*
 * load_counters: loads the counters from the specified file
 */
void load_counters(const char* file_path);

/*
 * load_counters: loads the counters from the specified file after mmaping this
 * file
 */
void load_counters_mmap(const char* file_path);

/*
 * write_counters: stores the current counter to the specified file
 */
void write_counters(const char* file_path);

/*
 * write_counters: stores the current counter to the specified file after
 * mmaping this file
 */
void write_counters_mmap(const char* file_path);

/*
 * delete_all_counters: deallocates all the memory dedicated to the counters
 */
void delete_all_counters();

/*
 * delete_all_counters_mmap: deallocates all the memory dedicated to the
 * counters and unmaps the respective file
 */
void delete_all_counters_mmap();

/*
 * counters_cleanup: calls delete_all_counter and close_counters (if applicable)
 */
void counters_cleanup();

/*
 * close_counters: destroys pthread_rwlock and closes timer thread
 */
void close_counters();

/*
 * create_counter_thread: creates the responsible thread for the saving of the
 * counters into the file and the mocked delay for the stabilization
 */
void create_counter_thread();

int query_counter(int index, uint64_t value);
int query_curr_delay();

#ifdef __cplusplus
}
#endif