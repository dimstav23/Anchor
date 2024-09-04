/*
 * Transactional User Operations source code
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
#endif

#include "manifest_operations.h"
#include "metadata_operations.h"
#include "openssl_gcm_encrypt.h"
#include "trusted_counter.h"
#include "tx_user_operations.h"
#include "user_operations.h"

#include "../libpmemobj/tx.h"

PMEMoid sobj_tx_alloc(size_t size, uint64_t type_num) {
  return sec_pmemobj_tx_alloc(size, type_num);
}

PMEMoid sobj_tx_zalloc(size_t size, uint64_t type_num) {
  return sec_pmemobj_tx_zalloc(size, type_num);
}

PMEMoid sobj_tx_realloc(PMEMoid oid, size_t size, uint64_t type_num) {
  return sec_pmemobj_tx_realloc(oid, size, type_num);
}

PMEMoid sobj_tx_zrealloc(PMEMoid oid, size_t size, uint64_t type_num) {
  return sec_pmemobj_tx_zrealloc(oid, size, type_num);
}

int sobj_tx_free(PMEMoid oid) {
  /*
   * Remove the object from epc immediately
   */
  int ret = sec_pmemobj_tx_free(oid);

  store_epc_entry(oid.pool_uuid_lo, oid.off, NULL, 0, 1);
  return ret;
}

void* sobj_tx_read(PMEMobjpool* pop, PMEMoid oid) {
  return sobj_read(pop, oid, 1, NULL);  // same as simple read
}

int sobj_tx_write(PMEMobjpool* pop, PMEMoid oid, void* data) {
#ifdef STATISTICS
  stats_measure_start(WRITE);
#endif
  // Fetch the entry from the epc hashmap
  uint64_t actual_size;
  struct epc_entry* obj_record = NULL;

  obj_record = epc_lookup_lock((uint8_t*)&oid.off);

  // Fetch the object from PM in order to do the verification before writing
  void* pmem_pointer = sec_pmemobj_direct(oid);
  actual_size = obj_record->obj_size & COMPACTION_MASK_OFF;  // & DIRTY_BIT_OFF;

  // perform the encryption
  uint8_t tag[HMAC_SIZE];

  encrypt_final_direct(data, actual_size, tag, NULL, 0, (uint8_t*)&oid,
                       pmem_pointer);
  // append entry in the epc
  store_epc_entry(oid.pool_uuid_lo, oid.off, tag, actual_size, 1);

  // store the modified object
  // pmemobj_memcpy_persist(pop, pmem_pointer, ciphertext, actual_size);
  pmem_persist(pmem_pointer, actual_size);

#ifdef STATISTICS
  stats_measure_end(WRITE);
#endif

  return 1;
}

/*
 * Writes part of the secure object in the secure pool
 * Args :   1)pointer to the pool,
 *          2)object Id struct,
 *          3)data offset inside the object,
 *          4)size of the data to be updated,
 *          5)pointer to the new data
 */
int sobj_tx_write_part(PMEMobjpool* pop, PMEMoid oid, uint64_t offset,
                       size_t data_size, void* data) {
#ifdef STATISTICS
  stats_measure_start(WRITE_PART);
#endif
  // Fetch the entry from the epc hashmap
  uint64_t actual_size = 0;
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
  uint8_t* ciphertext = NULL;
  uint8_t tag[HMAC_SIZE];
  ciphertext =
      encrypt_final(decryptedtext, actual_size, tag, NULL, 0, (uint8_t*)&oid);

  // append entry in the epc
  store_epc_entry(oid.pool_uuid_lo, oid.off, tag, actual_size, 1);

  // store the modified object
  pmem_memcpy(pmem_pointer + offset, ciphertext + offset, data_size, 0);

  free(decryptedtext);
  free(ciphertext);

#ifdef STATISTICS
  stats_measure_end(WRITE_PART);
#endif

  return 1;
}

int sobj_tx_add_range(PMEMoid oid, uint64_t offset, size_t size) {
  return sec_pmemobj_tx_add_range(oid, offset, size);
}
