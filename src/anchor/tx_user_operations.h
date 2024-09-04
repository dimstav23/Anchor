/*
 * Transactional User Operations header file
 */

#include <inttypes.h>
#include <libpmemobj.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Transactional Allocator Operations
 */
PMEMoid sobj_tx_alloc(size_t size, uint64_t type_num);
PMEMoid sobj_tx_zalloc(size_t size, uint64_t type_num);
PMEMoid sobj_tx_realloc(PMEMoid oid, size_t size, uint64_t type_num);
PMEMoid sobj_tx_zrealloc(PMEMoid oid, size_t size, uint64_t type_num);
int sobj_tx_free(PMEMoid oid);

/*
 *  Transactional Object Operations
 */
void* sobj_tx_read(PMEMobjpool* pop, PMEMoid oid);
int sobj_tx_write(PMEMobjpool* pop, PMEMoid oid, void* data);
int sobj_tx_write_part(PMEMobjpool* pop, PMEMoid oid, uint64_t offset,
                       size_t size, void* data);
int sobj_tx_resize(PMEMobjpool* pop, PMEMoid oid, size_t size);

int sobj_tx_add_range(PMEMoid oid, uint64_t offset, size_t size);

#ifdef __cplusplus
}
#endif