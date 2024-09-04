/*
 * Metadata Operations source code
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef STATISTICS
#include "internal_statistics.h"
extern uint64_t total_reads;
extern uint64_t shortcuts;
#endif
#include "manifest_operations.h"
#include "metadata_operations.h"
#include "openssl_gcm_encrypt.h"

extern uint64_t acc_bytes;

#ifdef DEBUG
int total_search = 0;
int cache_hits = 0;
#endif

struct dictionary* dic;
extern struct temp_list_head temp_redo_list[OBJ_NLANES_MANIFEST];

void epc_metadata_init() {
  dic = dic_new(0);
}

void epc_metadata_free() {
  dic_delete(dic);
#ifdef DEBUG
  printf("cache hit rate : %.2f%%\n", (float)cache_hits / total_search * 100);
#endif
}

void* epc_metadata_set(uint8_t* key_string, void* new_entry, int update_cache) {
  void* ret = dic_add(dic, key_string, new_entry);

  return ret;
}

int epc_metadata_remove(uint8_t* key_string) {
  dic_delete_node(dic, key_string);
  return 0;
}

int epc_number_of_objects() { return dic->count; }

/*
 * Integrity signature lookup in the EPC - self lock release
 */
struct epc_entry* epc_lookup(uint8_t* key_string) {
#ifdef DEBUG
  total_search++;
#endif
  struct epc_entry* obj_record = NULL;
  if (obj_record == NULL) {
    uint64_t key_location __attribute__((unused)) = (uintptr_t)dic_find(
        dic, key_string,
        (void**)&obj_record);  // node is locked for read only if found
    if (obj_record == NULL) {
      return NULL;  // if entry is not found for that object
    }
  }
  return obj_record;
}

/*
 * Integrity signature lookup in the EPC with lock acquisition
 */
struct epc_entry* epc_lookup_lock(uint8_t* key_string) {
#ifdef DEBUG
  total_search++;
#endif
  struct epc_entry* obj_record = NULL;
  dic_find(dic, key_string,
           (void**)&obj_record);  // node is locked for read only if found

  return obj_record;
}
/*
 * Integrity signature lookup in the EPC with lock acquisition
 */
struct epc_entry* epc_lookup_lock_inc(uint8_t* key_string) {
#ifdef DEBUG
  total_search++;
#endif
  struct epc_entry* obj_record = NULL;
  dic_find_lock_inc(dic, key_string, (void**)&obj_record);

  return obj_record;
}

/* frees cached content if it's above the defined thres */
void epc_cache_flush() { dic_cache_flush(dic); }

/* frees cached content*/
void epc_force_cache_flush() { dic_force_cache_flush(dic); }

void epc_forEach(dicFunc f) { dic_forEach(dic, f); }

/*
 * reads and verifies the header of the pool
 * addr: starting address of the pool where header is located,
 * offset: offset of the header inside the pool,
 * decrypt: if we want to receive the encrypted or the decrypted object
 */
void* spool_header_read(void* addr, uint64_t offset, int decrypt) {
  // Fetch the entry from the epc hashmap
  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  obj_record = epc_lookup_lock((uint8_t*)&offset);

  // fetch the object inside the volatile memory
  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;

  // perform the decryption
  uint8_t* decryptedtext =
      decrypt_final(addr, actual_size, (uint8_t*)obj_record->obj_sign, NULL, 0,
                    (uint8_t*)&(PMEMoid){0, offset});

  if (decrypt) {
    return decryptedtext;
  } else {
    uint8_t* volatile_pointer = (uint8_t*)malloc(actual_size * sizeof(uint8_t));
    memcpy(volatile_pointer, addr,
           actual_size);  // get object data inside the secure memory
    free(decryptedtext);
    return volatile_pointer;
  }
}

/*
 * reads and verifies the header/metadata of the pool
 * pool_uuid_lo: uuid of the pool
 * addr: starting address of the metadata object
 * offset: offset of the metadata object inside the pool,
 * decrypt: if we want to receive the encrypted or the decrypted object
 */
void* spool_metadata_read(uint64_t pool_uuid_lo, void* addr, uint64_t offset,
                          uint64_t* obj_size, int decrypt) {
  // Fetch the entry from the epc hashmap
  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  obj_record = epc_lookup_lock((uint8_t*)&offset);

  /* metadata entry not found or not stable - return zeroed buffer */
  if (obj_record == NULL) {
    printf("metadata object with offset %lx not found in epc\n", offset);
    return NULL;
  }
  // fetch the object inside the volatile memory
  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;

  if (obj_size != NULL) {
    *obj_size = obj_record->obj_size;
  }

  // perform the decryption
  uint8_t* decryptedtext =
      decrypt_final(addr, actual_size, (uint8_t*)obj_record->obj_sign, NULL, 0,
                    (uint8_t*)&(PMEMoid){pool_uuid_lo, offset});

  if (decrypt) {
    if (decryptedtext == NULL) {
      printf("metadata object with offset %lx not decrypted\n", offset);
      printf("sign : %lX %lX\n", ((uint64_t*)obj_record->obj_sign)[0],
             ((uint64_t*)obj_record->obj_sign)[1]);
      exit(1);
    }
    return decryptedtext;
  } else {
    uint8_t* volatile_pointer = (uint8_t*)malloc(actual_size * sizeof(uint8_t));
    memcpy(volatile_pointer, addr,
           actual_size);  // get object data inside the secure memory
    free(decryptedtext);
    return volatile_pointer;
  }
}

/*
 * reads and verifies the header/metadata of the pool first looking if the
 * result is cached pool_uuid_lo: uuid of the pool addr: starting address of the
 * metadata object offset: offset of the metadata object inside the pool,
 * decrypt: if we want to receive the encrypted or the decrypted object
 */
void* spool_metadata_read_cached(uint64_t pool_uuid_lo, void* addr,
                                 uint64_t offset, uint64_t* obj_size,
                                 int decrypt) {
#ifdef STATISTICS
  stats_measure_start(READ);
  // total_reads++;
#endif

  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  obj_record = epc_lookup_lock((uint8_t*)&offset);

  /* metadata entry not found or not stable - return zeroed buffer */
  if (obj_record == NULL) {
    return NULL;
  }

  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;

  if (obj_size != NULL) *obj_size = actual_size;

  if (obj_record->obj_ptr.obj_data != NULL) {  // if obj data is in the cache
#ifdef STATISTICS
    // shortcuts++;
    stats_measure_end(READ);
#endif
    return obj_record->obj_ptr.obj_data;
  }

  if (decrypt) {
    // perform the decryption
    uint8_t* decryptedtext =
        decrypt_final(addr, actual_size, (uint8_t*)obj_record->obj_sign, NULL,
                      0, (uint8_t*)&(PMEMoid){pool_uuid_lo, offset});
    if (decryptedtext != NULL) {
      obj_record->obj_ptr.obj_data =
          decryptedtext;  // update the entry with the object cached data
      __sync_fetch_and_add(&acc_bytes, actual_size);
    }

#ifdef STATISTICS
    stats_measure_end(READ);
#endif
    return decryptedtext;
  } else {
    uint8_t* volatile_pointer = (uint8_t*)malloc(actual_size * sizeof(uint8_t));
    memcpy(volatile_pointer, addr,
           actual_size);  // get object data inside the secure memory
    return volatile_pointer;
  }

  return NULL;
}

/*
 * updates cached data for immediate use
 * pool_uuid_lo: uuid of the pool
 * offset: offset of the metadata object inside the pool,
 * data: data buffer with the updated content
 */
int spool_metadata_set_cached(uint64_t pool_uuid_lo, uint64_t offset,
                              void* data) {
  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  obj_record = epc_lookup_lock((uint8_t*)&offset);

  /* metadata entry not found or not stable - return NULL */
  if (obj_record == NULL) {
    return 1;
  }

  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;

  // if obj data is in the cache -- update it
  if (obj_record->obj_ptr.obj_data != NULL) {
    memcpy(obj_record->obj_ptr.obj_data, data, actual_size);
  } else {
    obj_record->obj_ptr.obj_data =
        (uint8_t*)malloc(actual_size * sizeof(uint8_t));
    memcpy(obj_record->obj_ptr.obj_data, data, actual_size);
    __sync_fetch_and_add(&acc_bytes, actual_size);
  }
  return 0;
}

/*
 * writes metadata of the pool
 * pool_uuid_lo: uuid of the pool
 * offset: offset of the metadata inside the pool,
 * addr: address of the pool where metadata is located and should be written
 * data_size: size of the metadata
 * data: address of the data to be encrypted and written
 * copy: if the encrypted data should be copied to PM
 * persist: flag for persist or simple copy
 */
int spool_metadata_write(uint64_t pool_uuid_lo, uint64_t offset, void* addr,
                         size_t data_size, void* data, int copy, int persist) {
  // perform the encryption
  uint8_t* ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pool_uuid_lo;
  iv[1] = offset;

  ciphertext = encrypt_final(data, data_size, tag, NULL, 0, (uint8_t*)iv);

  // store (and persist) the modified object if instrcuted so
  if (copy) {
    if (persist) {
      pmem_memcpy_persist(addr, ciphertext, data_size);
      // atomic operation to validate the manifest entry - shows that the
      // content is written
    } else {
      pmem_memcpy(addr, ciphertext, data_size, 0);
    }
    append_undo_object_entry((PMEMoid){pool_uuid_lo, offset}, tag,
                             OBJ_NLANES_MANIFEST, data_size, 0, 1);
  }

  free(ciphertext);
  return 1;
}

/*
 * writes metadata of the pool partially - introduced for run bitmaps
 * pool_uuid_lo: uuid of the pool
 * offset: offset of the metadata inside the pool,
 * addr: address of the pool where metadata is located and should be written
 * data_size: size of the metadata
 * data: address of the data to be encrypted and copied
 * copy: if the encrypted data should be copied to PM
 * copy_offset: offset inside the metadata object to start the copy of copy_size
 * data copy_size: amount of data to be copied persist: flag for persist or
 * simple copy
 */
int spool_metadata_write_part(uint64_t pool_uuid_lo, uint64_t offset,
                              void* addr, size_t data_size, void* data,
                              int copy, uint64_t copy_offset, int copy_size,
                              int persist) {
  // perform the encryption
  uint8_t* ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pool_uuid_lo;
  iv[1] = offset;

  ciphertext = encrypt_final(data, data_size, tag, NULL, 0, (uint8_t*)iv);

  // store (and persist) the modified object if instrcuted so
  if (copy) {
    if (persist) {
      pmem_memcpy_persist(addr, ciphertext + copy_offset, copy_size);
    } else {
      pmem_memcpy(addr, ciphertext + copy_offset, copy_size, 0);
    }
    append_undo_object_entry((PMEMoid){pool_uuid_lo, offset}, tag,
                             OBJ_NLANES_MANIFEST, data_size, 0, 1);
  }

  free(ciphertext);
  return 1;
}

/*
 * writes metadata of the pool
 * pool_uuid_lo: uuid of the pool
 * offset: offset of the metadata inside the pool,
 * addr: address of the pool where metadata is located and should be written
 * data_size: size of the metadata
 * data: address of the data to be encrypted and written
 * copy: if the encrypted data should be copied to PM
 * persist: flag for persist or simple copy
 */
int spool_metadata_write_atomic(uint64_t pool_uuid_lo, uint64_t offset,
                                void* addr, size_t data_size, void* data,
                                int copy, int persist /*, lock */) {
  assert(data_size == sizeof(uint64_t));

  // perform the encryption
  uint8_t* ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pool_uuid_lo;
  iv[1] = offset;

  ciphertext = encrypt_final(data, data_size, tag, NULL, 0, (uint8_t*)iv);

  // store (and persist) the modified object if instrcuted so
  if (copy) {
    if (persist) {
      pmem_memcpy_persist(addr, ciphertext, data_size);
    } else {
      pmem_memcpy(addr, ciphertext, data_size, 0);
    }
    append_redo_object_entry((PMEMoid){pool_uuid_lo, offset}, tag,
                             OBJ_NLANES_MANIFEST, sizeof(uint64_t), 0);
  }

  free(ciphertext);
  return 1;
}

/*
 * writes metadata of the pool partially - introduced for run bitmaps
 * pool_uuid_lo: uuid of the pool
 * offset: offset of the metadata inside the pool,
 * addr: address of the pool where metadata is located and should be written
 * data_size: size of the metadata
 * data: address of the data to be encrypted and copied
 * copy: if the encrypted data should be copied to PM
 * copy_offset: offset inside the metadata object to start the copy of copy_size
 * data copy_size: amount of data to be copied persist: flag for persist or
 * simple copy
 */
int spool_metadata_write_part_atomic(uint64_t pool_uuid_lo, uint64_t offset,
                                     void* addr, size_t data_size, void* data,
                                     int copy, uint64_t copy_offset,
                                     int copy_size, int persist /*, lock */) {
  // perform the encryption
  uint8_t* ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pool_uuid_lo;
  iv[1] = offset;

  ciphertext = encrypt_final(data, data_size, tag, NULL, 0, (uint8_t*)iv);

  // store (and persist) the modified object if instrcuted so
  if (copy) {
    if (persist) {
      pmem_memcpy_persist(addr, ciphertext + copy_offset, copy_size);
    } else {
      pmem_memcpy(addr, ciphertext + copy_offset, copy_size, 0);
    }
    append_undo_object_entry((PMEMoid){pool_uuid_lo, offset}, tag,
                             OBJ_NLANES_MANIFEST, data_size, 0, 1);
  }

  free(ciphertext);
  return 1;
}

/*
 * writes manifest entry of metadata of the pool
 */
int append_metadata_manifest_entry(uint64_t pool_uuid_lo, uint64_t offset,
                                   void* addr, size_t data_size, void* data,
                                   uint64_t tx_lane_id) {
  // perform the encryption
  uint8_t* ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pool_uuid_lo;
  iv[1] = offset;

  ciphertext = encrypt_final(data, data_size, tag, NULL, 0, (uint8_t*)iv);

  // append_redo_object_entry((PMEMoid){pool_uuid_lo, offset}, tag,
  // OBJ_NLANES_MANIFEST, data_size, 0);
  append_redo_object_entry((PMEMoid){pool_uuid_lo, offset}, tag, tx_lane_id,
                           data_size, 0);

  free(ciphertext);
  return 1;
}

/*
 * writes metadata of the pool
 */
int write_metadata_entry(uint64_t pool_uuid_lo, uint64_t offset, void* addr,
                         size_t data_size, void* data) {
  // perform the encryption
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pool_uuid_lo;
  iv[1] = offset;

  encrypt_final_direct(data, data_size, tag, NULL, 0, (uint8_t*)iv, addr);
  pmem_persist(addr, data_size);

  return 1;
}