#include "internal_statistics.h"
#include <assert.h>

static __thread uint64_t log_category[MAX_CAT];
static __thread uint64_t cycles_spent[MAX_CAT];
static __thread int measuring = 0;

static __thread uint64_t cycles_start[MAX_CAT] = {0};

#ifdef WRITE_AMPL
static __thread uint64_t bytes_written[MAX_WRITE_CAT] = {0};
#endif

int sampling_rate = 1000;
uint64_t sampling_delay = 0;

const char *cat_str[] = {"READ",
                         "WRITE",
                         "WRITE_PART",
                         "ALLOC",
                         "ZALLOC",
                         "FREE",
                         "UNDO_LOG",
                         "REDO_LOG",
                         "MANIFEST_LOG",
                         "ENCRYPTION_COST",
                         "TX",
                         "LOOKUP",
                         "GET",
                         "PUT",
                         "TX_COMMIT_PHASE",
                         "CACHE_CLEANUP",
                         "MAX_CAT"};
const char *cycles_str[] = {"READ_CYCLES",
                            "WRITE_CYCLES",
                            "WRITE_PART_CYCLES",
                            "ALLOC_CYCLES",
                            "ZALLOC_CYCLES",
                            "FREE_CYCLES",
                            "UNDO_LOG_CYCLES",
                            "REDO_LOG_CYCLES",
                            "MANIFEST_LOG_CYCLES",
                            "ENCRYPTION_COST_CYCLES",
                            "TX_CYCLES",
                            "LOOKUP_CYCLES",
                            "GET_CYCLES",
                            "PUT_CYCLES",
                            "TX_COMMIT_PHASE_CYCLES",
                            "CACHE_CLEANUP_CYCLES",
                            "MAX_CAT"};

#ifdef WRITE_AMPL
const char *data_str[] = {"OBJ",
                          "OBJ_PMDK",
                          "MANIFEST",
                          "UNDO_LOG_DATA",
                          "UNDO_LOG_DATA_PMDK",
                          "REDO_LOG_DATA",
                          "REDO_LOG_DATA_PMDK",
                          "METADATA",
                          "METADATA_PMDK",
                          "MAX_WRITE_CAT"};
#endif

void stats_init() {
  for (int i = 0; i < MAX_CAT; i++) {
    log_category[i] = 0;
    cycles_spent[i] = 0;
  }

  uint64_t delay_s = stats_rdtsc_s();
  for (int i = 0; i < sampling_rate - 1; i++) {
    stats_rdtsc_s();
    stats_rdtsc_e();
  }
  uint64_t delay_e = stats_rdtsc_e();
  sampling_delay = delay_e - delay_s;

#ifdef WRITE_AMPL
  for (int i = 0; i < MAX_WRITE_CAT; i++) {
    bytes_written[i] = 0;
  }
#endif
}

void stats_clear() {
  for (int i = 0; i < MAX_CAT; i++) {
    log_category[i] = 0;
    cycles_spent[i] = 0;
  }

#ifdef WRITE_AMPL
  for (int i = 0; i < MAX_WRITE_CAT; i++) {
    bytes_written[i] = 0;
  }
#endif
}

void stats_print() {
  for (int i = 0; i < MAX_CAT; i++) {
    printf("%s calls: %ld cycles: %ld\n", cat_str[i], log_category[i],
           cycles_spent[i]);
  }
#ifdef WRITE_AMPL
  for (int i = 0; i < MAX_WRITE_CAT; i++) {
    printf("%s logged bytes: %ld\n", data_str[i], bytes_written[i]);
  }
#endif
}

void stats_on() { measuring = 1; }

void stats_off() { measuring = 0; }

void stats_measure_start(enum log_category cat) {
  if (measuring && cat < MAX_CAT) {
    log_category[cat] += 1;
    if (log_category[cat] % sampling_rate == 0 || cat == TX_COMMIT_PHASE ||
        cat == CACHE_CLEANUP) {
      cycles_start[cat] = stats_rdtsc_s();
    }
  }
}

void stats_measure_end(enum log_category cat) {
  if (measuring && cat < MAX_CAT) {
    if (log_category[cat] % sampling_rate == 0 || cat == TX_COMMIT_PHASE ||
        cat == CACHE_CLEANUP) {
      assert(cycles_start[cat] != 0);
      uint64_t end = stats_rdtsc_e();
      cycles_spent[cat] += (end - cycles_start[cat]);
      cycles_start[cat] = 0;
    }
  }
}

#ifdef WRITE_AMPL
void bytes_written_inc(enum write_ampl cat, uint64_t bytes) {
  if (measuring && cat < MAX_WRITE_CAT) {
    bytes_written[cat] += bytes;
  }
}
#endif

/* getters for the stats */
uint64_t *stats_get_total_counters() { return log_category; }

uint64_t *stats_get_total_cycles() { return cycles_spent; }

#ifdef WRITE_AMPL
uint64_t *stats_get_total_bytes_written() { return bytes_written; }
#endif