/*
 * User Operations source code
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef STATISTICS
#include "internal_statistics.h"
uint64_t total_reads = 0;
uint64_t shortcuts = 0;
#endif

#include "manifest_operations.h"
#include "metadata_log.h"
#include "metadata_operations.h"
#include "openssl_gcm_encrypt.h"
#include "trusted_counter.h"
#include "user_operations.h"

#include "../libpmemobj/tx.h"

extern uint8_t* key;
extern uint8_t* iv;
extern size_t iv_len;
extern uint64_t acc_bytes;

extern pthread_t tid;

PMEMobjpool* spool_create(const char* path, const char* layout_name,
                          size_t pool_size, mode_t mode,
                          const char* manifest_path, const char* counters_path,
                          const char* metadata_log_path, uint8_t* key_setup,
                          size_t key_len_setup, uint8_t* iv_setup,
                          size_t iv_len_setup) {
  encryption_setup(key_setup, key_len_setup, iv_setup, iv_len_setup);
  epc_metadata_init();
  load_counters_mmap(counters_path != NULL ? counters_path : "/dev/shm/amcs");

  if (manifest_boot(manifest_path != NULL ? manifest_path
                                          : "/dev/shm/Manifest") == 0)
    return NULL;

  PMEMobjpool* pop = sec_pmemobj_create(
      path, layout_name, pool_size, mode,
      metadata_log_path != NULL ? metadata_log_path : "/dev/shm/Metadata_log");

  manifest_set_pool_id(pop->uuid_lo);
  return pop;
}

PMEMobjpool* spool_open(const char* path, const char* layout_name,
                        const char* manifest_path, const char* counters_path,
                        const char* metadata_log_path, uint8_t* key_setup,
                        size_t key_len_setup, uint8_t* iv_setup,
                        size_t iv_len_setup) {
  encryption_setup(key_setup, key_len_setup, iv_setup, iv_len_setup);
  epc_metadata_init();
  load_counters_mmap(counters_path != NULL ? counters_path : "/dev/shm/amcs");

  if (manifest_boot(manifest_path != NULL ? manifest_path
                                          : "/dev/shm/Manifest") == 0) {
    perror("manifest_boot");
    return NULL;
  }

  PMEMobjpool* pop = sec_pmemobj_open(
      path, layout_name,
      metadata_log_path != NULL ? metadata_log_path : "/dev/shm/Metadata_log");

  manifest_set_pool_id(pop->uuid_lo);
  return pop;
}

int spool_close(PMEMobjpool* pop) {
#ifdef STATISTICS
  printf("shortcut percentage %.2f\n",
         (float)shortcuts / (float)total_reads * 100);
  printf("raw numbers: total %ld shortcuts %ld\n", total_reads, shortcuts);
  shortcuts = 0;
  total_reads = 0;
#endif

  metadata_log_persist(METADATA_LOG_PM_PERSIST_ALL);
  /* wait for counter stability */
  metadata_log_apply_rt();
  metadata_log_close();

  sec_pmemobj_close(pop);

  close_compaction_thread();

  // pthread_join(tid, NULL);

  epc_metadata_free();

  unload_manifest(NULL, 0);

  counters_cleanup();

  delete_encryption_setup();

  free(pop);

#ifdef DEBUG
  print_manifest_stats();
#endif

  return 1;
}

PMEMoid sobj_root_get(PMEMobjpool* pop, size_t size) {
  return sec_pmemobj_root(pop, size);
}

int sobj_alloc(PMEMobjpool* pop, PMEMoid* oidp, size_t size, uint64_t type_num,
               pmemobj_constr constructor, void* arg, PMEMoid* updated_object) {
  // allocate the new object and zero its content
  int error = sec_pmemobj_alloc(pop, oidp, size, type_num, updated_object);
#ifdef DEBUG
  assert(error == 0);
#endif

  metadata_log_persist(METADATA_LOG_PM_PERSIST_ALL);
  /* counter stabilisation here */
  metadata_log_apply_rt();
  return error;
}

int sobj_zalloc(PMEMobjpool* pop, PMEMoid* oidp, size_t size, uint64_t type_num,
                pmemobj_constr constructor, void* arg,
                PMEMoid* updated_object) {
  // allocate the new object and zero its content
  int error = sec_pmemobj_zalloc(pop, oidp, size, type_num, updated_object);
#ifdef DEBUG
  assert(error == 0);
#endif
  metadata_log_persist(METADATA_LOG_PM_PERSIST_ALL);
  /* counter stabilisation here */
  metadata_log_apply_rt();
  return error;
}

int sobj_realloc(PMEMobjpool* pop, PMEMoid* oid, PMEMoid* oidp, size_t size,
                 uint64_t type_num, PMEMoid* updated_object) {
  // allocate the new object and zero its content
  int error =
      sec_pmemobj_realloc(pop, oid, oidp, size, type_num, updated_object);
#ifdef DEBUG
  assert(error == 0);
#endif
  metadata_log_persist(METADATA_LOG_PM_PERSIST_ALL);
  /* counter stabilisation here */
  metadata_log_apply_rt();
  return error;
}

int sobj_zrealloc(PMEMobjpool* pop, PMEMoid* oid, PMEMoid* oidp, size_t size,
                  uint64_t type_num, PMEMoid* updated_object) {
  // allocate the new object and zero its content
  int error =
      sec_pmemobj_zrealloc(pop, oid, oidp, size, type_num, updated_object);
#ifdef DEBUG
  assert(error == 0);
#endif
  metadata_log_persist(METADATA_LOG_PM_PERSIST_ALL);
  /* counter stabilisation here */
  metadata_log_apply_rt();
  return error;
}

void sobj_free(PMEMoid* oid, PMEMoid* oidp, PMEMoid* updated_object) {
  // Free the PM object
  sec_pmemobj_free(oid, oidp, updated_object);
  metadata_log_persist(METADATA_LOG_PM_PERSIST_ALL);
  /* counter stabilisation here */
  metadata_log_apply_rt();
}
/*
 * reads and verifies an object in the pool
 * pop: pointer to the pool
 * oid: object id struct
 * decrypt: if we want to receive the encrypted or the decrypted object
 * object size : pointer to store the object size if we want to retrieve it
 * during the decryption
 */
void* sobj_read(PMEMobjpool* pop, PMEMoid oid, int decrypt,
                uint64_t* object_size) {
#ifdef STATISTICS
  stats_measure_start(READ);
  total_reads++;
#endif
  void* pmem_pointer = sec_pmemobj_direct(oid);

  // uint8_t* key_string = (uint8_t*) malloc (KEY_LENGTH * sizeof(uint8_t));
  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;
  // Fetch the entry from the epc hashmap
  // obj_record = epc_lookup((uint8_t*)&oid);
  obj_record = epc_lookup_lock((uint8_t*)&oid.off);

  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;  // & DIRTY_BIT_OFF;

  if (obj_record->obj_ptr.obj_data !=
      NULL) {  // if obj data is going to be modified fetch the volatile copy
    if (object_size != NULL) *object_size = actual_size;
#ifdef STATISTICS
    stats_measure_end(READ);
#endif
    void* obj_data = malloc(actual_size);
    memcpy(obj_data, obj_record->obj_ptr.obj_data, actual_size);
    return obj_data;
    // return obj_record->obj_data;
  }

  if (decrypt) {
    uint8_t* decryptedtext;
    // perform the decryption
    decryptedtext =
        decrypt_final(pmem_pointer, actual_size, (uint8_t*)obj_record->obj_sign,
                      NULL, 0, (uint8_t*)&oid);
    if (object_size != NULL) *object_size = actual_size;

#ifdef STATISTICS
    stats_measure_end(READ);
#endif

    return decryptedtext;
  } else {
    uint8_t* volatile_pointer = (uint8_t*)malloc(actual_size * sizeof(uint8_t));
    memcpy(volatile_pointer, pmem_pointer,
           actual_size);  // get object data inside the secure memory
    return volatile_pointer;
  }
}

void* sobj_direct(PMEMobjpool* pop, PMEMoid oid) {
#ifdef STATISTICS
  stats_measure_start(READ);
  total_reads++;
#endif
  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  obj_record = epc_lookup_lock((uint8_t*)&oid.off);
  if (obj_record == NULL) {
    return NULL;
  }

  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;  // & DIRTY_BIT_OFF;

  if (obj_record->obj_ptr.obj_data != NULL) {  // if obj data is in the cache
#ifdef STATISTICS
    shortcuts++;
    stats_measure_end(READ);
#endif
    return obj_record->obj_ptr.obj_data;
  }

  void* pmem_pointer = sec_pmemobj_direct(oid);

  uint8_t* decryptedtext;
  // perform the decryption
  decryptedtext =
      decrypt_final(pmem_pointer, actual_size, (uint8_t*)obj_record->obj_sign,
                    NULL, 0, (uint8_t*)&oid);
  if (__sync_bool_compare_and_swap(&obj_record->obj_ptr.obj_data, NULL,
                                   decryptedtext))
    __sync_fetch_and_add(&acc_bytes, actual_size);
  else
    free(decryptedtext);

#ifdef STATISTICS
  stats_measure_end(READ);
#endif

  return obj_record->obj_ptr.obj_data;
}

void* sobj_direct_size(PMEMobjpool* pop, PMEMoid oid, uint64_t* object_size) {
#ifdef STATISTICS
  stats_measure_start(READ);
  total_reads++;
#endif

  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  obj_record = epc_lookup_lock((uint8_t*)&oid.off);
  if (obj_record == NULL) {
    return NULL;
  }

  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;  // & DIRTY_BIT_OFF;

  if (object_size != NULL) *object_size = actual_size;

  if (obj_record->obj_ptr.obj_data != NULL) {  // if obj data is in the cache
#ifdef STATISTICS
    shortcuts++;
    stats_measure_end(READ);
#endif
    return obj_record->obj_ptr.obj_data;
  }

  void* pmem_pointer = sec_pmemobj_direct(oid);

  uint8_t* decryptedtext;
  // perform the decryption
  decryptedtext =
      decrypt_final(pmem_pointer, actual_size, (uint8_t*)obj_record->obj_sign,
                    NULL, 0, (uint8_t*)&oid);
  if (__sync_bool_compare_and_swap(&obj_record->obj_ptr.obj_data, NULL,
                                   decryptedtext))
    __sync_fetch_and_add(&acc_bytes, actual_size);
  else
    free(decryptedtext);

#ifdef STATISTICS
  stats_measure_end(READ);
#endif

  return obj_record->obj_ptr.obj_data;
}

/*
 * NON-TRANSACTIONAL write of an object
 * can lead to inconsistent state of data
 */
int sobj_write(PMEMobjpool* pop, PMEMoid oid, void* data) {
  // Fetch the entry from the epc hashmap
  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  // Fetch the entry from the epc hashmap
  obj_record = epc_lookup_lock((uint8_t*)&oid.off);

  // Fetch the object from PM in order to do the verification before writing
  void* pmem_pointer = sec_pmemobj_direct(oid);
  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;  // & DIRTY_BIT_OFF;

  uint8_t tag[HMAC_SIZE];
  encrypt_final_direct(data, actual_size, tag, NULL, 0, (uint8_t*)&oid,
                       pmem_pointer);
  // append manifest entry
  append_undo_object_entry(oid, tag, OBJ_NLANES, actual_size, 0, 1);

  // store the modified object;
  pmem_persist(pmem_pointer, actual_size);

  return 1;
}

/*
 * NON-TRANSACTIONAL write of an object
 * can lead to inconsistent state of data
 * Writes part of the secure object in the secure pool
 * Args :   1)pointer to the pool,
 *          2)object Id struct,
 *          3)data offset inside the object,
 *          4)size of the data to be updated,
 *          5)pointer to the new data
 */
int sobj_write_part(PMEMobjpool* pop, PMEMoid oid, uint64_t offset,
                    size_t data_size, void* data) {
  // Fetch the entry from the epc hashmap
  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  obj_record = epc_lookup_lock((uint8_t*)&oid.off);

  // Fetch the object from PM in order to do the verification before writing
  void* pmem_pointer = sec_pmemobj_direct(oid);
  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;  // & DIRTY_BIT_OFF;

  // perform the decryption
  uint8_t* decryptedtext =
      decrypt_final(pmem_pointer, actual_size, (uint8_t*)obj_record->obj_sign,
                    NULL, 0, (uint8_t*)&oid);

  // write the new data part
  memcpy(decryptedtext + offset, data, data_size);

  // perform the encryption
  uint8_t* ciphertext;
  uint8_t* tag = (uint8_t*)malloc(HMAC_SIZE * sizeof(uint8_t));
  ciphertext =
      encrypt_final(decryptedtext, actual_size, tag, NULL, 0, (uint8_t*)&oid);

  // append manifest entry
  append_undo_object_entry(oid, tag, OBJ_NLANES, actual_size, 0, 1);

  // store the modified object
  pmem_memcpy(pmem_pointer + offset, ciphertext + offset, data_size, 0);

  free(decryptedtext);
  free(ciphertext);
  free(tag);
  return 1;
}

/*
 * Restores part of the secure object in the secure pool from the undo log
 * Appends the updated manifest entry
 * Args :   1)pointer to the pool,
 *          2)object Id struct,
 *          3)data offset inside the object,
 *          4)size of the data to be updated,
 *          5)pointer to the new data
 */
int sobj_restore_part(void* addr, size_t data_size, void* data) {
  // store the modified object
  // pmem_memcpy_persist(addr, data, data_size);
  pmem_memcpy(addr, data, data_size,
              PMEM_F_MEM_NODRAIN | PMEM_F_MEM_NONTEMPORAL);
  return 1;
}

#ifdef STATISTICS
/*
 * Wrappers for statistics' functions
 */
void sobj_stats_init() { stats_init(); }

void sobj_stats_clear() { stats_clear(); }

void sobj_stats_print() { stats_print(); }

void sobj_stats_on() { stats_on(); }

void sobj_stats_off() { stats_off(); }

uint64_t* sobj_get_counters() { return stats_get_total_counters(); }

uint64_t* sobj_get_cycles() { return stats_get_total_cycles(); }

#ifdef WRITE_AMPL
uint64_t* sobj_get_bytes_written() { return stats_get_total_bytes_written(); }
#endif
#endif