/*
 * User Operations header file
 */

#include <inttypes.h>
#include <libpmemobj.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_LENGTH 8

/*
 *  Pool Operations
 */
PMEMobjpool* spool_create(const char* path, const char* layout_name,
                          size_t pool_size, mode_t mode,
                          const char* manifest_path, const char* counters_path,
                          const char* metadata_log_path, uint8_t* key_setup,
                          size_t key_len_setup, uint8_t* iv_setup,
                          size_t iv_len_setup);
PMEMobjpool* spool_open(const char* path, const char* layout_name,
                        const char* manifest_path, const char* counters_path,
                        const char* metadata_log_path, uint8_t* key_setup,
                        size_t key_len_setup, uint8_t* iv_setup,
                        size_t iv_len_setup);
int spool_close(PMEMobjpool* pop);

/*
 *  Root Operations
 */
PMEMoid sobj_root_get(PMEMobjpool* pop, size_t size);

/*
 *  Allocator Operations
 */

/*
 * sobj_alloc : creates a zerored persistent memory object
 * oidp : the location where the new oid of the allocated object should be
 * stored (PM or volatile pointer) update_object : points to the oid of the
 * object where the oidp should be stored in order to update its signature
 * (volatile memory pointer)
 */
int sobj_alloc(PMEMobjpool* pop, PMEMoid* oidp, size_t size, uint64_t type_num,
               pmemobj_constr constructor, void* arg, PMEMoid* updated_object);
int sobj_zalloc(PMEMobjpool* pop, PMEMoid* oidp, size_t size, uint64_t type_num,
                pmemobj_constr constructor, void* arg, PMEMoid* updated_object);
/*
 * sobj_realloc : resizes an existing secure object.
 * oid : the object id that is going to be resized (volatile memory pointer)
 * oidp : the location where the new oid of the reallocated object should be
 * stored (PM pointer) / if not specified, update the oid pointer as it will
 * refer to volatile memory update_object : points to the oid of the object
 * where the oidp should be stored in order to update its signature (volatile
 * memory pointer)
 */
int sobj_realloc(PMEMobjpool* pop, PMEMoid* oid, PMEMoid* oidp, size_t size,
                 uint64_t type_num, PMEMoid* updated_object);
int sobj_zrealloc(PMEMobjpool* pop, PMEMoid* oid, PMEMoid* oidp, size_t size,
                  uint64_t type_num, PMEMoid* updated_object);
/*
 * sobj_free : Frees an existing object.
 * oid : the object id that is going to be freed (volatile memory pointer)
 * oidp : the location where the zeroed PMEMoid of the freed object should be
 * stored (PM pointer) update_object : points to the oid of the object where the
 * zeroed oidp should be stored in order to update its signature (volatile
 * memory pointer)
 */
void sobj_free(PMEMoid* oid, PMEMoid* oidp, PMEMoid* updated_object);

/*
 *  Object Operations
 */
void* sobj_read(PMEMobjpool* pop, PMEMoid oid, int decrypt,
                uint64_t* object_size);
int sobj_write(PMEMobjpool* pop, PMEMoid oid, void* data);
int sobj_write_part(PMEMobjpool* pop, PMEMoid oid, uint64_t offset,
                    size_t data_size, void* data);
int sobj_restore_part(void* addr, size_t data_size, void* data);
int sobj_resize(PMEMobjpool* pop, PMEMoid oid, size_t data_size);

void* sobj_direct(PMEMobjpool* pop, PMEMoid oid);
void* sobj_direct_size(PMEMobjpool* pop, PMEMoid oid, uint64_t* object_size);

#ifdef STATISTICS
/*
 * Wrappers for statistics' functions
 */
void sobj_stats_init();
void sobj_stats_clear();
void sobj_stats_print();
void sobj_stats_on();
void sobj_stats_off();
uint64_t* sobj_get_counters();
uint64_t* sobj_get_cycles();
#ifdef WRITE_AMPL
uint64_t* sobj_get_bytes_written();
#endif
#endif

#ifdef __cplusplus
}
#endif