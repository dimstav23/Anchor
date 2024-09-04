#ifndef METADATA_OPS
#define METADATA_OPS 1
#include <inttypes.h>
#include <libpmemobj.h>
#include "hashmap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_LENGTH 8

#define COMPACTION_MASK(comp_run) \
  (((uint64_t)(comp_run)) << 58)  // to get the compaction number of an object
#define COMPACTION_MASK_OFF \
  ~(((uint64_t)(0xF)) << 58)  // to remove the compaction mask of the size in
                               // an object epc entry

struct obj_ptr {
  uint64_t epoch;
  uint8_t* obj_data;
};

struct epc_entry {
  uint64_t obj_sign[2];
  uint64_t obj_size;
  struct obj_ptr obj_ptr;
};

void epc_metadata_init();
struct epc_entry* epc_lookup(uint8_t* key_string);
struct epc_entry* epc_lookup_lock(uint8_t* key_string);
void epc_metadata_free();
void* epc_metadata_set(uint8_t* key_string, void* new_entry, int update_cache);
int epc_metadata_remove(uint8_t* key_string);
int epc_number_of_objects();
void epc_cache_flush();
void epc_force_cache_flush();
void epc_forEach(dicFunc f);
// int epc_foreach_object(PFany apply_function);

void* spool_header_read(void* addr, uint64_t offset, int decrypt);
void* spool_metadata_read(uint64_t pool_uuid_lo, void* addr, uint64_t offset,
                          uint64_t* obj_size, int decrypt);
void* spool_metadata_read_cached(uint64_t pool_uuid_lo, void* addr,
                                 uint64_t offset, uint64_t* obj_size,
                                 int decrypt);
int spool_metadata_set_cached(uint64_t pool_uuid_lo, uint64_t offset,
                              void* data);
int spool_metadata_write(uint64_t pool_uuid_lo, uint64_t offset, void* addr,
                         size_t data_size, void* data, int copy, int persist);
int spool_metadata_write_part(uint64_t pool_uuid_lo, uint64_t offset,
                              void* addr, size_t data_size, void* data,
                              int copy, uint64_t copy_offset, int copy_size,
                              int persist);
int spool_metadata_write_atomic(uint64_t pool_uuid_lo, uint64_t offset,
                                void* addr, size_t data_size, void* data,
                                int copy, int persist /*, lock */);
int spool_metadata_write_part_atomic(uint64_t pool_uuid_lo, uint64_t offset,
                                     void* addr, size_t data_size, void* data,
                                     int copy, uint64_t copy_offset,
                                     int copy_size, int persist /*, lock */);
int append_metadata_manifest_entry(uint64_t pool_uuid_lo, uint64_t offset,
                                   void* addr, size_t data_size, void* data,
                                   uint64_t tx_lane_id);
int write_metadata_entry(uint64_t pool_uuid_lo, uint64_t offset, void* addr,
                         size_t data_size, void* data);

struct epc_entry* epc_lookup_lock_inc(uint8_t* key_string);

#ifdef __cplusplus
}
#endif
#endif