#ifndef INTERNAL_STATS
#define INTERNAL_STATS 1

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

enum log_category {
  READ,
  WRITE,
  WRITE_PART,
  ALLOC,
  ZALLOC,
  FREE,
  UNDO_LOG,
  REDO_LOG,
  MANIFEST_LOG,
  ENCRYPTION_COST,
  TX,
  LOOKUP,
  GET,
  PUT,
  TX_COMMIT_PHASE,
  CACHE_CLEANUP,
  MAX_CAT
};

extern enum log_category cat;

enum write_ampl {
  OBJ,
  OBJ_PMDK,
  MANIFEST,
  UNDO_LOG_DATA,
  UNDO_LOG_DATA_PMDK,
  REDO_LOG_DATA,
  REDO_LOG_DATA_PMDK,
  METADATA,
  METADATA_PMDK,
  MAX_WRITE_CAT
};

extern enum write_ampl write_cat;

extern const char *cat_str[];
extern const char *cycles_str[];
#ifdef WRITE_AMPL
extern const char *data_str[];
#endif

void stats_init();
void stats_clear();
void stats_print();
void stats_on();
void stats_off();

void stats_measure_start(enum log_category cat);
void stats_measure_end(enum log_category cat);
#ifdef WRITE_AMPL
void bytes_written_inc(enum write_ampl cat, uint64_t bytes);
#endif

/* getters for the stats */
uint64_t *stats_get_total_counters();
uint64_t *stats_get_total_cycles();
#ifdef WRITE_AMPL
uint64_t *stats_get_total_bytes_written();
#endif
/*
 * Time sampling functions
 */
static __inline__ uint64_t stats_rdtsc_s(void) {
  unsigned a, d;
  asm volatile("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
  asm volatile("rdtsc" : "=a"(a), "=d"(d));
  return ((unsigned long)a) | (((unsigned long)d) << 32);
}

static __inline__ uint64_t stats_rdtsc_e(void) {
  unsigned a, d;
  asm volatile("rdtscp" : "=a"(a), "=d"(d));
  asm volatile("cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx");
  return ((unsigned long)a) | (((unsigned long)d) << 32);
}
#endif