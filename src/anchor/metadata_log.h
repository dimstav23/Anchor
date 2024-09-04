/*
 * metadata_log.h -- struct & function declaration for metadata log handling
 */

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libpmem.h>
#include "trusted_counter.h"

#define FILE_SIZE 1048576

#define METADATA_LOG_FILE_DEFAULT_SIZE \
  FILE_SIZE  // 4096 /* reserve 1 page for the metadata log */

#define METADATA_LOG_VOL_DEFAULT_SIZE \
  FILE_SIZE  // 4096 /* reserve 1 page for the metadata log volatile
             // representation */

#define METADATA_LOG_END_COUNTER_IDX \
  2 /* metadata end log counter located right after the manifest's */

#define METADATA_LOG_START_COUNTER_IDX \
  3 /* metadata start log counter located right after the end log counter */

#define METADATA_LOG_PM_PERSIST_ALL -1

enum metadata_log_vol_state {
  METADATA_LOG_AVAIL,
  METADATA_LOG_FULL_RESET,
  METADATA_LOG_FULL_WAIT,
  METADATA_LOG_MAX
};

struct log_hdr {
  uint64_t tag[2];    /* HMAC for authentication */
  uint64_t start_off; /* Offset where the first log is located */
  uint64_t end_off;   /* Offset where the last log ends */
  uint64_t pool_id;   /* id of the pool this log refers to */
  uint64_t tcv;       /* trusted counter value */
  uint64_t unused[2]; /* padding to reach cacheline boundaries */
};

struct log_entry_hdr {
  uint64_t tag[2];  /* HMAC for authentication */
  uint64_t size;    /* size of this log entity */
  uint64_t pool_id; /* id of the pool this log refers to */
  uint64_t tcv;     /* trusted counter value */
};

struct log_entry {
  uint64_t offset;    /* offset to locate where data should be written */
  uint64_t data_size; /* size of the data to be written */
  uint8_t data[];     /* content to fill in */
};

struct metadata_log_rt {
  /* Trusted counter handle */
  struct Counter* _tcv;
  struct Counter* _start_tcv;

  /* PM file related variables */
  void* metadata_log_file_address;
  size_t metadata_log_file_mapped_size;

  /* Volatile state related variables */
  void* metadata_log_vol_address;
  size_t metadata_log_vol_size;
  int vol_log_start_off; /* volatile log starting offset - to be applied */
  int vol_log_end_off;   /* volatile log ending offset - for appends */
  int persist_point; /* offset till which vol log has been persisted and can be
                        applied */

  /* PM Pool related variables */
  uint64_t pool_uuid_lo; /* PM pool id */
  void* pop_mapped_addr; /* PM pool mapped address */

  /* PM log related variables */
  int PM_log_end_off; /* persisted log ending offset */

  /* necessary locks for cases that writing should be stopped or waited till
   * append is done */
  pthread_rwlock_t _append_rw_lock;
  pthread_mutex_t _persist_mutex;
  pthread_mutex_t _apply_mutex;

  /* condition variable for reset of volatile log */
  pthread_cond_t _reset_cond;
  pthread_mutex_t _reset_mutex;
  int reset_in_progress;
};

/**
 * Main log utilities
 */
int metadata_log_load(
    const char* metadata_log_path, uint64_t pool_id,
    void* pop_mapped_addr); /* opens or creates the metadata log */
void metadata_log_close();  /* unmaps the metadata log */
void metadata_log_append(
    uint64_t offset, uint64_t data_size,
    uint8_t* data); /* append entries in the volatile state of the log */
void metadata_log_persist(
    int vol_end_point); /* encrypts and writes current metadata log in PM -
                           optional parameter - default -1 */
void metadata_log_apply_rt(); /* applies current metadata log actions to PM pool
                                 during runtime */
void metadata_log_append_manifest_rt(
    uint64_t tx_lane_id); /* appends current metadata log actions to PM manifest
                             during runtime */
int metadata_log_apply_rec(); /* applies current metadata log actions to PM pool
                                 during recovery */
void metadata_log_entry_apply(
    struct metadata_log_rt log_info,
    struct log_entry* entry); /* applies an entry to PM */
void metadata_log_extend();   /* extends the capacity of metadata log */
void metadata_log_vol_truncate_and_reset(
    struct metadata_log_rt*
        log_info); /* truncates PM log as it's no longer needed */
void metadata_log_PM_truncate_and_reset(
    struct metadata_log_rt*
        log_info); /* truncates PM log as it's no longer needed */

/**
 * Helper functions
 */
int metadata_log_vol_full(
    struct metadata_log_rt log_info, uint64_t append_off,
    uint64_t data_size); /* checks if the volatile log is going to be full */
int metadata_log_PM_full(
    struct metadata_log_rt log_info,
    uint64_t data_size); /* checks if the PM log is going to be full */
size_t get_metadata_log_size(const char* path); /* Get Metadata Log file size */
struct Counter* metadata_log_tcv_load(
    int index); /* Create/Load the metadata log counter into the runtime
                   structure */
void* metadata_log_file_map(
    const char* path,
    size_t metadata_log_size);   /* Open/Create Metadata log file */
void* metadata_log_vol_malloc(); /* Creates volatile log mapping */
void metadata_log_unmap(
    void* metadata_log_address,
    size_t metadata_log_size); /* Unmaps Metadata log File & volatile mapping*/