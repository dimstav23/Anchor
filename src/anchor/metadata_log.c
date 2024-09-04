/*
 * metadata_log.c -- metadata log handling
 */
#include "metadata_log.h"
#include "metadata_operations.h"
#include "openssl_gcm_encrypt.h"
#ifdef STATISTICS
#include "internal_statistics.h"
#endif

struct metadata_log_rt log_info;

/*
 * metadata_log_load -- opens or creates the metadata log and initialises rt
 * infos
 */
int metadata_log_load(const char* metadata_log_path, uint64_t pool_id,
                      void* pop_mapped_addr) {
  log_info.pool_uuid_lo = pool_id;
  log_info.pop_mapped_addr = pop_mapped_addr;

  log_info._tcv = metadata_log_tcv_load(METADATA_LOG_END_COUNTER_IDX);
  log_info._start_tcv = metadata_log_tcv_load(METADATA_LOG_START_COUNTER_IDX);

  size_t metadata_log_file_size = get_metadata_log_size(metadata_log_path);

  log_info.metadata_log_file_address =
      metadata_log_file_map(metadata_log_path, metadata_log_file_size);

  log_info.metadata_log_vol_address = metadata_log_vol_malloc();

  log_info.vol_log_start_off = 0;
  log_info.vol_log_end_off = 0;
  log_info.persist_point = 0;
  log_info.PM_log_end_off = 0;

  assert(pthread_rwlock_init(&log_info._append_rw_lock, NULL) == 0);
  assert(pthread_mutex_init(&log_info._persist_mutex, NULL) == 0);
  assert(pthread_mutex_init(&log_info._apply_mutex, NULL) == 0);

  assert(pthread_mutex_init(&log_info._reset_mutex, NULL) == 0);
  assert(pthread_cond_init(&log_info._reset_cond, NULL) == 0);
  log_info.reset_in_progress = 0;

  return 0;
}

/*
 * metadata_log_close -- unmaps the metadata log
 */
void metadata_log_close() {
  set_counter(log_info._start_tcv, log_info._tcv->_counter);
  metadata_log_unmap(log_info.metadata_log_file_address,
                     log_info.metadata_log_file_mapped_size);
  free(log_info.metadata_log_vol_address);

  pthread_rwlock_destroy(&log_info._append_rw_lock);
  pthread_mutex_destroy(&log_info._persist_mutex);
  pthread_mutex_destroy(&log_info._apply_mutex);

  pthread_mutex_destroy(&log_info._reset_mutex);
  pthread_cond_destroy(&log_info._reset_cond);

  return;
}

/*
 * metadata_log_append -- append entries in the volatile state of the log
 */
void metadata_log_append(uint64_t offset, uint64_t data_size, uint8_t* data) {
  pthread_rwlock_rdlock(&log_info._append_rw_lock);

  uint64_t added_off = sizeof(struct log_entry) + data_size;
  uint64_t curr_off =
      __sync_fetch_and_add(&log_info.vol_log_end_off, added_off);

  switch (metadata_log_vol_full(log_info, curr_off, data_size)) {
    case METADATA_LOG_FULL_RESET:
      pthread_rwlock_unlock(&log_info._append_rw_lock);
      /* wait till every on-going append process and then proceed with the
       * clearance */
      __sync_bool_compare_and_swap(&log_info.reset_in_progress, 0,
                                   1);  // indicate that a reset is in progress
      metadata_log_persist(curr_off);   // pass curr_off to indicate the final
                                        // offset of the vol log to write to PM
      metadata_log_apply_rt();
      metadata_log_vol_truncate_and_reset(&log_info);
      metadata_log_append(offset, data_size,
                          data); /* after trucate and reset, re-attempt */
      return;
      break;
    case METADATA_LOG_FULL_WAIT:
      pthread_rwlock_unlock(&log_info._append_rw_lock);
      /* wait for truncate to finish */
      pthread_mutex_lock(&log_info._reset_mutex);
      while (log_info.reset_in_progress) {
        pthread_cond_wait(&log_info._reset_cond, &log_info._reset_mutex);
      }
      pthread_mutex_unlock(&log_info._reset_mutex);

      metadata_log_append(offset, data_size,
                          data); /* after trucate and reset, re-attempt */
      return;
      break;
    default:
      break;
  }

  struct log_entry* entry =
      (struct log_entry*)(log_info.metadata_log_vol_address + curr_off);
  entry->offset = offset;
  entry->data_size = data_size;
  memcpy(entry->data, data, data_size);

  pthread_rwlock_unlock(&log_info._append_rw_lock);

  return;
}

/*
 * metadata_log_persist -- encrypts and writes current metadata log in PM
 */
void metadata_log_persist(int vol_end_point) {
  if (log_info.persist_point == log_info.vol_log_end_off ||
      log_info.persist_point == vol_end_point)
    return;

  int reset_flag = 0;
  /* lock for PM writing and persist point updates */
  pthread_mutex_lock(&log_info._persist_mutex);

  int start_off = log_info.persist_point; /* current start off in volatile log
                                             where this persist should start*/

  /* wait for all the appends to finish and then proceed with the checks */
  pthread_rwlock_wrlock(&log_info._append_rw_lock);

  /* safely get current end off in volatile log to make sure last entry is
   * written*/
  int end_off = vol_end_point == METADATA_LOG_PM_PERSIST_ALL
                    ? log_info.vol_log_end_off
                    : vol_end_point;

  /* check if I have data to persist */
  if (start_off >= end_off) {
    pthread_mutex_unlock(&log_info._persist_mutex);
    pthread_rwlock_unlock(&log_info._append_rw_lock);
    return;
  }

  if ((metadata_log_PM_full(log_info, end_off - start_off)) ==
      METADATA_LOG_FULL_RESET) {
    reset_flag = 1;
    metadata_log_apply_rt();
    metadata_log_PM_truncate_and_reset(&log_info);
  } else {
    /* let the append continue */
    pthread_rwlock_unlock(&log_info._append_rw_lock);
  }

  struct log_entry_hdr hdr;
  hdr.size = end_off - start_off;      /* bytes to be written */
  hdr.pool_id = log_info.pool_uuid_lo; /* pool id */

  set_counter(log_info._start_tcv, log_info._tcv->_counter);
  hdr.tcv = inc(log_info._tcv); /* Trusted counter */

  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = 0;
  iv[1] = hdr.tcv;
  size_t hdr_unencrypted_size = offsetof(struct log_entry_hdr, pool_id);

  uint8_t* ciphertext = encrypt_final_two_parts(
      (uint8_t*)&hdr.pool_id,
      sizeof(struct log_entry_hdr) - hdr_unencrypted_size,
      (uint8_t*)log_info.metadata_log_vol_address + start_off, hdr.size,
      (uint8_t*)&(hdr.tag), NULL, 0, (uint8_t*)iv);
  pmem_memcpy(log_info.metadata_log_file_address + log_info.PM_log_end_off,
              &hdr, hdr_unencrypted_size, PMEM_F_MEM_NONTEMPORAL);
  pmem_memcpy(log_info.metadata_log_file_address + log_info.PM_log_end_off +
                  hdr_unencrypted_size,
              ciphertext,
              hdr.size + sizeof(struct log_entry_hdr) - hdr_unencrypted_size,
              PMEM_F_MEM_NONTEMPORAL);
  pmem_drain();
  free(ciphertext);

#ifdef WRITE_AMPL
  bytes_written_inc(METADATA, hdr.size + sizeof(struct log_entry_hdr));
#endif
  /* update PM log info */
  log_info.PM_log_end_off =
      log_info.PM_log_end_off + hdr.size + sizeof(struct log_entry_hdr);

  /* update the persist point till which the log can be applied */
  log_info.persist_point = end_off;

  if (reset_flag) {
    /* if a reset is triggered append is blocked till this process ends */
    pthread_rwlock_unlock(&log_info._append_rw_lock);
  }

  pthread_mutex_unlock(&log_info._persist_mutex);

  return;
}

/**
 * metadata_log_entry_apply_rec -- applies an entry to PM on recovery with epc
 * signature and manifest entry
 */
void metadata_log_entry_apply_rec(struct metadata_log_rt log_info,
                                  struct log_entry* entry) {
  spool_metadata_write(log_info.pool_uuid_lo, entry->offset,
                       log_info.pop_mapped_addr + entry->offset,
                       entry->data_size, entry->data, 1, 1);
}

/**
 * metadata_log_entry_apply -- applies an entry to PM
 */
void metadata_log_entry_apply(struct metadata_log_rt log_info,
                              struct log_entry* entry) {
  write_metadata_entry(log_info.pool_uuid_lo, entry->offset,
                       log_info.pop_mapped_addr + entry->offset,
                       entry->data_size, entry->data);
}

/*
 * metadata_log_apply_rt -- applies current metadata log actions to PM pool
 * during runtime till the point that has already been persisted
 */
void metadata_log_apply_rt() {
  if (log_info.vol_log_start_off == log_info.persist_point) return;
  /* lock for PM apply */
  pthread_mutex_lock(&log_info._apply_mutex);

  /* get current start & end for volatile log to apply */
  int start_off = log_info.vol_log_start_off;

  /* safely get the persist point */
  int end_off = log_info.persist_point;

  /* update volatile log start offset for future applies */
  log_info.vol_log_start_off = end_off;

  struct log_entry* entry;
  while (start_off < end_off) {
    entry = (struct log_entry*)(log_info.metadata_log_vol_address + start_off);
    metadata_log_entry_apply(log_info, entry);
    start_off += sizeof(struct log_entry) + entry->data_size;
  }

  pthread_mutex_unlock(&log_info._apply_mutex);
  return;
}

/**
 * metadata_log_entry_apppend -- appends a manifest entry to PM
 */
static void metadata_log_entry_append_manifest(struct metadata_log_rt log_info,
                                               struct log_entry* entry,
                                               uint64_t tx_lane_id) {
  append_metadata_manifest_entry(log_info.pool_uuid_lo, entry->offset,
                                 log_info.pop_mapped_addr + entry->offset,
                                 entry->data_size, entry->data, tx_lane_id);
}
/*
 * metadata_log_append_manifest_rt -- appens current metadata log actions to PM
 * manifest during runtime till the point that has already been persisted
 */
void metadata_log_append_manifest_rt(uint64_t tx_lane_id) {
  if (log_info.vol_log_start_off == log_info.persist_point) return;
  /* lock for PM apply */
  pthread_mutex_lock(&log_info._apply_mutex);

  /* get current start & end for volatile log to apply */
  int start_off = log_info.vol_log_start_off;

  /* safely get the persist point */
  int end_off = log_info.persist_point;

  struct log_entry* entry;
  while (start_off < end_off) {
    entry = (struct log_entry*)(log_info.metadata_log_vol_address + start_off);
    metadata_log_entry_append_manifest(log_info, entry, tx_lane_id);
    start_off += sizeof(struct log_entry) + entry->data_size;
  }

  pthread_mutex_unlock(&log_info._apply_mutex);
  return;
}

/*
 * metadata_log_apply_rec -- applies current metadata log actions to PM pool
 * during recovery should start where it left off
 */
int metadata_log_apply_rec() {
  if (log_info._tcv->_counter > log_info._start_tcv->_counter) {
    void* temp_vol_log;
    uint64_t temp_vol_off = 0;
    if (posix_memalign(&temp_vol_log, sysconf(_SC_PAGESIZE),
                       METADATA_LOG_VOL_DEFAULT_SIZE)) {
      fprintf(stderr, "metadata log posix_memalign failed\n");
      exit(1);
    }

    void* scan_address = log_info.metadata_log_file_address;
    uint64_t initial_counter = log_info._start_tcv->_counter;
    uint64_t current_counter = 0;

    struct log_entry_hdr* hdr = NULL;
    size_t hdr_unencrypted_size = offsetof(struct log_entry_hdr, pool_id);
    uint64_t iv[IV_SIZE_UINT64];
    iv[0] = 0;
    uint8_t* decryptedtext;
    while (current_counter < log_info._tcv->_counter) {
      hdr = (struct log_entry_hdr*)scan_address;
      iv[1] = current_counter;
      decryptedtext = decrypt_final(
          (uint8_t*)&hdr->pool_id,
          hdr->size + sizeof(struct log_entry_hdr) - hdr_unencrypted_size,
          (uint8_t*)hdr->tag, NULL, 0, (uint8_t*)iv);
      if (decryptedtext == NULL) {
        printf("metadata_log_apply_rec : decryption failure\n");
        exit(1);
      } else {
#ifdef DEBUG
        uint64_t log_tcv =
            (*(uint64_t*)(decryptedtext + offsetof(struct log_entry_hdr, tcv) -
                          hdr_unencrypted_size));
        assert(log_tcv == current_counter);
#endif
        if (current_counter >= initial_counter) {
          memcpy(temp_vol_log + temp_vol_off,
                 decryptedtext + sizeof(struct log_entry_hdr) -
                     hdr_unencrypted_size,
                 hdr->size);
          temp_vol_off += hdr->size;
        }
      }

      current_counter++;
      scan_address += (hdr->size + sizeof(struct log_entry_hdr));
    }

    /* TCV check here */
    assert(current_counter == log_info._tcv->_counter);

    /* apply the modifications here */
    uint64_t scan_off = 0;
    while (scan_off < temp_vol_off) {
      struct log_entry* entry = (struct log_entry*)(temp_vol_log + scan_off);
      metadata_log_entry_apply_rec(log_info, entry);

      scan_off += entry->data_size + sizeof(struct log_entry);
    }

    /* invalidate the log after the applied actions become stable */
    metadata_log_PM_truncate_and_reset(&log_info);

    free(temp_vol_log);
  }
  return 0;
}

/*
 * metadata_log_extend -- extends the capacity of metadata log
 */
void metadata_log_extend() { return; }

/*
 * metadata_log_PM_truncate_and_reset -- truncates the log as it's no longer
 * needed and resets it
 */
void metadata_log_PM_truncate_and_reset(struct metadata_log_rt* log_info) {
  /* increase the starting counter to invalidate the entries */
  set_counter(log_info->_start_tcv, log_info->_tcv->_counter);
  log_info->PM_log_end_off = 0;

  return;
}

/*
 * metadata_log_vol_truncate_and_reset -- truncates the log as it's no longer
 * needed and resets it
 */
void metadata_log_vol_truncate_and_reset(struct metadata_log_rt* log_info) {
  log_info->vol_log_start_off = 0;
  log_info->vol_log_end_off = 0;
  log_info->persist_point = 0;

  pthread_mutex_lock(&log_info->_reset_mutex);
  __sync_bool_compare_and_swap(&log_info->reset_in_progress, 1, 0);
  pthread_cond_broadcast(&log_info->_reset_cond);
  pthread_mutex_unlock(&log_info->_reset_mutex);

  return;
}

/*
 * metadata_log_vol_full -- Checks if the volatile metadata log is going to be
 * filled with the following entry
 */
int metadata_log_vol_full(struct metadata_log_rt log_info, uint64_t append_off,
                          uint64_t data_size) {
  if (append_off + sizeof(struct log_entry) + data_size +
          sizeof(struct log_entry_hdr) >
      log_info.metadata_log_vol_size) {
    if (append_off < log_info.metadata_log_vol_size) {
      return METADATA_LOG_FULL_RESET;  // perform the volatile log reset
    } else {
      return METADATA_LOG_FULL_WAIT;  // wait to finish the reset
    }
  } else {
    return METADATA_LOG_AVAIL;
  }
}

/*
 * metadata_log_PM_full -- Checks if the PM metadata log is going to be filled
 * with the following entry
 */
int metadata_log_PM_full(struct metadata_log_rt log_info, uint64_t data_size) {
  if (log_info.PM_log_end_off + sizeof(struct log_entry_hdr) + data_size >
      log_info.metadata_log_file_mapped_size) {
    return METADATA_LOG_FULL_RESET;
  } else {
    return METADATA_LOG_AVAIL;
  }
}

/*
 * get_metadata_log_size -- Get Metadata log file size
 */
size_t get_metadata_log_size(const char* path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return st.st_size;
  } else {
    return (size_t)0;
  }
}

/**
 * metadata_log_tcv_load -- Create/Load the metadata log counter into the
 * runtime structure
 */
struct Counter* metadata_log_tcv_load(int index) {
  struct Counter* ret;
  if (!(ret = get_counter(index))) {
    ret = create_counter_idx(0, index);
  }
  return ret;
}

/*
 * metadata_log_file_map -- Open/Create Metadata log file
 * Returns pointer to the mapped Metadata log File
 */
void* metadata_log_file_map(const char* path, size_t metadata_log_file_size) {
  if (metadata_log_file_size == 0) {
    // if metadata log does not exist
    metadata_log_file_size = METADATA_LOG_FILE_DEFAULT_SIZE;
    log_info.vol_log_start_off = 0;
    log_info.vol_log_end_off = 0;
    log_info.PM_log_end_off = 0;
  }

  size_t mapped_len;
  int is_pmem;
  char* pmemaddr;
  if ((pmemaddr = pmem_map_file(path, metadata_log_file_size, PMEM_FILE_CREATE,
                                0666, &mapped_len, &is_pmem)) == NULL) {
    perror("metadata log pmem_map_file");
    exit(1);
  }

  log_info.metadata_log_file_mapped_size = metadata_log_file_size;

  return pmemaddr;
}

/*
 * metadata_log_vol_malloc -- Creates volatile log mapping
 * Returns pointer to the mapped Metadata log region in volatile memory
 */
void* metadata_log_vol_malloc() {
  void* addr;

  if (posix_memalign(&addr, sysconf(_SC_PAGESIZE),
                     METADATA_LOG_VOL_DEFAULT_SIZE)) {
    fprintf(stderr, "metadata log posix_memalign failed\n");
    exit(1);
  }

  log_info.metadata_log_vol_size = METADATA_LOG_VOL_DEFAULT_SIZE;

  memset(addr, 0, METADATA_LOG_VOL_DEFAULT_SIZE);
  return addr;
}

/*
 * metadata_log_unmap -- Unmaps Metadata log File & volatile mapping
 */
void metadata_log_unmap(void* metadata_log_address, size_t metadata_log_size) {
  if (pmem_unmap(metadata_log_address, metadata_log_size) != 0) {
    perror("error in metadata log file unmap");
    exit(1);
  }
  return;
}