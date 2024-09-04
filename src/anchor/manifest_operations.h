/*
 * Manifest Operations header file
 */
#ifndef MANIFEST_OPS  // #include guards
#define MANIFEST_OPS 1

#include <inttypes.h>
#include <libpmem.h>
#include <libpmemobj.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUGEPAGE_2MB 2097152
#define HUGEPAGE_1GB 1073741824

#define ENTRY_DATA_SIZE 40  // 40 bytes of data per entry without tcv and HMAC
#define ENTRY_ENCRYPTED_DATA_SIZE \
  48                            // 48 metadata bytes per entry without
                                // HMAC
#define MANIFEST_ENTRY_SIZE 64  // 64 bytes per entry
#define MANIFEST_DEFAULT_SIZE 2147483648 // 1024 * 2MB hugepages
#define COMPACTION_THRESHOLD \
  0.7  // manifest percentage size after which compaction should start
#define MANIFEST_MAX_SIZE 1024 * 1024 * 1024 * 3  // 3GB Max manifest size
#define KEY_LENGTH 8                              // 128 bits key = 16 bytes
#define VALUE_LENGTH 24                           // 8 + 8 + 8 bytes

#define MANIFEST_START_COUNTER_IDX \
  0  // manifest's counters are the very first in the secured file
#define MANIFEST_END_COUNTER_IDX \
  1  // manifest's counters are the very first in the secured file

#define ENTRY_TYPE_MASK ((uint64_t)(0b111ULL << 61ULL))
#define ENTRY_TYPE_MASK_OFF ~((uint64_t)(0b111ULL << 61ULL))

#define OBJ_NLANES_MANIFEST 32ULL  // MAX TX_LANE_ID
#define ENTRY_TX_LANE_ID_MASK ((uint64_t)(0x3FULL << 52ULL))
#define ENTRY_TX_LANE_ID(arg)                     \
  ((((uint64_t)(arg)) & ENTRY_TX_LANE_ID_MASK) >> \
   52)  // to get the tx_lane_id of an entry
#define ENTRY_INVALID_MASK ((uint64_t)(0b1ULL << 63ULL))
#define ENTRY_INVALID(arg) \
  (((uint64_t)(arg)) & ENTRY_INVALID_MASK)  // to get the validity of an entry
#define ENTRY_OBJ_SIZE_MASK_OFF ~((uint64_t)(0xFFFLL << 52ULL))

enum tx_entry_type {
  TX_START,
  TX_COMMIT,
  TX_ABORT,
  TX_REC_REDO,
  TX_REC_UNDO,
  TX_FINISH,
  ULOG_HDR_UPDATE,

  TX_MAX_TYPE
};

#define TX_IDLE 0
#define TX_STARTED 1
#define TX_COMMITED 2

#define ENTRY_TYPE_POSITION \
  3  // depicts in which data field of the entry, the type of the entry is held
     // (data[3])
#define VALIDATION_POSITION \
  4  // depicts in which data field of the entry, the type of the entry is held
     // (data[4])

// obj_offset containts the type of the entry (see ENTRY_TYPE_MASK)
// obj_size contains the lane id (bits 52-57) (see ENTRY_TX_LANE_ID) +
// compaction number (58-60) (see COMPACTION_MASK) (which is removed before
// writing)

struct manifest_entry {
  uint64_t
      data[5];  // data[3] corresponds to obj_offset of
                // manifest_object_entry and entry_type of manifest_tx_entry
                // which maintains the entry_type in the upper bits
  uint64_t tcv;
  uint64_t entry_sign[2];
};

struct manifest_object_entry {
  uint64_t obj_sign[2];
  uint64_t obj_poolid;
  uint64_t obj_offset; /* offset with entry type flag */
  uint64_t obj_size; /* with validation & compaction & lane_idx flag as max obj
                        size is limited to 34 bits 1 MSB validation + 11 MSB
                        tx_lane_id  + 3 Compaction Number*/
  uint64_t tcv;      /* fur use in the append_entry function */
};

struct manifest_tx_info_entry {
  uint64_t poolid;
  uint64_t tx_lane_id;
  uint64_t tx_stage;
  uint64_t entry_type;
  uint64_t unused;
  uint64_t tcv; /* fur use in the append_entry function */
};

/*
 * ANCHOR: 8byte snapshot for atomic operation / works fine for single-threaded
 * app otherwise it has to be combined with a lock
 */
struct manifest_atomic_snapshot_entry {
  uint64_t obj_poolid;
  uint64_t obj_offset;
  uint64_t old_data;
  uint64_t internal_offset; /* offset of the 8byte value inside of the object
                               with entry type flag */
  uint64_t invalid;         /* validation flag 1 bit / 63 bits unused */
  uint64_t tcv;             /* fur use in the append_entry function */
};

// Initialisation of global variables for new instance of pool_opening/creation
void global_init(void);

// Clears out global variables etc.
void global_fini(void);

// Open/Create Manifest - Bootstrap
char* load_manifest(const char* path, size_t manifest_size);
// Unmaps manifest file
void unload_manifest(void* manifest_addr, size_t manifest_size);

// Scan Manifest - Bootstrap
int scan_manifest(size_t manifest_size);

// Verify Entry - Bootstrap
struct manifest_entry* verify_entry(struct manifest_entry* entry,
                                    uint64_t expected_tcv);

// Store Manifest Entries in Memory - Bootstrap
int store_epc_entry(uint64_t pool_id, uint64_t obj_offset, uint8_t* obj_sign,
                    uint64_t obj_size, int update_cache);

// Scan & Check Manifest on boot
int manifest_boot(const char* path);
// Set pool uuid for compaction
void manifest_set_pool_id(uint64_t pool_id);

int* get_unfinished_tx();

void free_unfinished_tx();

// Compact Manifest - Runtime
// check for compaction time in a separate thread
void* compaction_check(void* args);
void close_compaction_thread(void);

// Append Object Entry to the Manifest
uint64_t append_undo_object_entry(PMEMoid oid, uint8_t* obj_tag,
                                  uint64_t tx_lane_id, size_t size, int invalid,
                                  int add_to_epc);
uint64_t append_redo_object_entry(PMEMoid oid, uint8_t* obj_tag,
                                  uint64_t tx_lane_id, size_t size,
                                  int invalid);
uint64_t append_ulog_object_entry(PMEMoid oid, uint8_t* obj_tag,
                                  uint64_t tx_lane_id, size_t size,
                                  int invalid);
uint64_t append_atomic_object_entry(PMEMoid oid, uint8_t* obj_tag, size_t size,
                                    int invalid);

// Append transaction related entry in the manifest
uint64_t append_tx_info_entry(uint64_t pool_id, uint64_t tx_lane_id,
                              uint64_t tx_stage);

// Append entry in the manifest regardless of the type
uint64_t append_entry(uint8_t* new_entry_data);

// get initial manifest size
size_t get_manifest_size(const char* path);

struct temp_list_entry {
  struct manifest_object_entry* data;
  struct temp_list_entry* next;
};

struct temp_list_head {
  struct temp_list_entry* head;
};

// Functions to enter entries in the temp list till their respective transaction
// finish entry is found
typedef int (*temp_list_entry_cb)(uint64_t pool_id, uint64_t offset,
                                  uint8_t* tag, uint64_t size,
                                  uintptr_t base_addr);
int foreach_temp_list_entry(struct temp_list_head* temp_list,
                            temp_list_entry_cb cb, uintptr_t base_addr);
int add_to_temp_list(struct temp_list_head* temp_list, uint64_t poolid,
                     uint64_t obj_offset, uint8_t* obj_sign, uint64_t obj_size);
int add_with_replace_to_temp_list(struct temp_list_head* temp_list,
                                  uint64_t poolid, uint64_t obj_offset,
                                  uint8_t* obj_sign, uint64_t obj_size);
int discard_from_temp_list(struct temp_list_head* temp_list, uint64_t poolid);
int add_temp_list_to_epc(struct temp_list_head* temp_list, uint64_t poolid);
uint8_t* scan_temp_list(struct temp_list_head* temp_list, uint64_t poolid,
                        uint64_t obj_offset, uint64_t* obj_size);
int free_temp_list(struct temp_list_head* temp_list);
int free_temp_list_entry(struct temp_list_head* temp_list, uint64_t poolid,
                         uint64_t obj_offset, int epc_update);

struct atomic_snapshot {
  struct manifest_atomic_snapshot_entry* snapshot;
  struct atomic_snapshot* next;
};

struct atomic_snapshot_list_head {
  struct atomic_snapshot* head;
};

// Functions to manipulate snapshots of atomic operations
int add_with_replace_to_atomic_snapshot_list(
    struct atomic_snapshot_list_head* snapshot_list, uint64_t poolid,
    uint64_t obj_offset, uint64_t old_data, uint64_t internal_offset,
    uint64_t invalid);
int free_atomic_snapshot_list(struct atomic_snapshot_list_head* snapshot_list);
int free_atomic_snapshot_list_entry(
    struct atomic_snapshot_list_head* snapshot_list, uint64_t poolid,
    uint64_t obj_offset, uint64_t internal_offset);
int apply_atomic_snapshots(uint64_t pool_id, uintptr_t base_addr);

#ifdef DEBUG
void print_manifest_stats();
#endif

#ifdef __cplusplus
}
#endif

#endif  // MANIFEST_OPS