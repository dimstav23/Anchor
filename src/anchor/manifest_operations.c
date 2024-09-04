/*
 * Manifest Operations source code
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "manifest_operations.h"
#include "metadata_operations.h"
#include "openssl_gcm_encrypt.h"
#include "trusted_counter.h"
#include "user_operations.h"

#ifdef STATISTICS
#include "internal_statistics.h"
#endif

#define UNDO_OBJECT_ENTRY (0b001ULL << 61ULL)
#define REDO_OBJECT_ENTRY (0b010ULL << 61ULL)
#define ULOG_OBJECT_ENTRY (0b011ULL << 61ULL)
#define ATOMIC_OBJECT_ENTRY (0b100ULL << 61ULL)
#define TX_INFO_ENTRY (0b101ULL << 61ULL)
#define INVALID_ENTRY (0b1ULL << 63ULL)  // MSB

int* unfinished_tx;  // to track the unfinished transactions while recovering
uint64_t curr_pool_id = 0;

char manifest_path[128];
char new_manifest_path[128];
int64_t manifest_offset_cnt = 0;
int manifest_serial_no = 1;

void* manifest_scan_address;     // manifest append address for the new entries
size_t curr_manifest_size;       // current manifest size
void* manifest_start_address;    // current manifest start address
void* new_manifest_append_addr;  // start address to append old entries in the
                                 // new manifest
uint64_t compaction_starting_cnt = 0;

void* new_manifest_start_addr;
void*
    new_manifest_concur_update_addr;  // address where entries should be written
                                      // in the new manifest during compaction

struct Counter* _tcv;
struct Counter* _start_tcv;
uint64_t _tcv_temp;  // trusted counter for compaction

pthread_t tid;

int compaction_number;  // compaction number
int compaction_active;  // shared variable to check when compaction should start
int compaction_finished;
int process_ended;
int start_entries;  // number of entries at the beginning of a compaction
volatile int active_tx = 0;  // active transactions

extern uint8_t* key;
extern uint8_t* iv_init;
extern size_t iv_len;

// Declaration of condition variables
pthread_cond_t compaction_start;
pthread_cond_t compaction_end;
pthread_cond_t new_manifest_mapped;

// Declaration of mutexes
pthread_mutex_t lock;
pthread_mutex_t end_lock;
pthread_mutex_t new_manifest_lock;
pthread_mutex_t compaction_mutex;

int counter = 0;  // debugging variable

struct temp_list_head
    temp_undo_list[OBJ_NLANES_MANIFEST];  // works for 1 pool, for more pools it
                                          // has to be converted to a hashmap
struct temp_list_head
    temp_redo_list[OBJ_NLANES_MANIFEST];  // works for 1 pool, for more pools it
                                          // has to be converted to a hashmap
struct temp_list_head
    temp_ulog_list[OBJ_NLANES_MANIFEST];  // works for 1 pool, for more pools it
                                          // has to be converted to a hashmap
struct atomic_snapshot_list_head
    atomic_snapshot_list;  // works for 1 pool, for more pools it has to be
                           // converted to a hashmap

#ifdef DEBUG
int tx_info = 0;
int undo_object = 0;
int redo_object = 0;
int ulog_object = 0;
int atomic_entry = 0;
int atomic_snapshot = 0;
int total_entries = 0;
int tx_start = 0;
int tx_commit = 0;
int tx_abort = 0;
int tx_rec_redo = 0;
int tx_rec_undo = 0;
int tx_finish = 0;
int ulog_hdr_update = 0;
#endif

/*
 * global_init -- Initialisation of global variables for new instance of
 * pool_opening/creation
 */
void global_init(void) {
  // Initialisation of compaction related variables
  new_manifest_start_addr = NULL;
  new_manifest_concur_update_addr =
      NULL;  // address where entries should be written in the new manifest
             // during compaction
  compaction_number = 0;  // compaction number
  compaction_active =
      0;  // shared variable to check when compaction should start
  active_tx = 0;
  compaction_finished = 0;
  process_ended = 0;
  start_entries = 0;  // number of entries at the beginning of a compaction

  // Initialisation of condition variables
  int ret;
  ret = pthread_cond_init(&compaction_start, NULL);
  assert(ret == 0);
  ret = pthread_cond_init(&compaction_end, NULL);
  assert(ret == 0);
  ret = pthread_cond_init(&new_manifest_mapped, NULL);
  assert(ret == 0);

  // Initialisation of mutexes
  ret = pthread_mutex_init(&lock, NULL);
  assert(ret == 0);
  ret = pthread_mutex_init(&end_lock, NULL);
  assert(ret == 0);
  ret = pthread_mutex_init(&new_manifest_lock, NULL);
  assert(ret == 0);
  ret = pthread_mutex_init(&compaction_mutex, NULL);
  assert(ret == 0);

#ifdef DEBUG
  tx_info = 0;
  undo_object = 0;
  redo_object = 0;
  ulog_object = 0;
  atomic_entry = 0;
  atomic_snapshot = 0;
  total_entries = 0;
  tx_start = 0;
  tx_commit = 0;
  tx_abort = 0;
  tx_rec_redo = 0;
  tx_rec_undo = 0;
  tx_finish = 0;
  ulog_hdr_update = 0;
#endif
}

/*
 * global_fini -- Clears out global variables etc.
 */
void global_fini(void) {
  // Destroy condition variables
  pthread_cond_destroy(&compaction_start);
  pthread_cond_destroy(&compaction_end);
  pthread_cond_destroy(&new_manifest_mapped);

  // Destroy mutexes
  pthread_mutex_destroy(&lock);
  pthread_mutex_destroy(&end_lock);
  pthread_mutex_destroy(&new_manifest_lock);
  pthread_mutex_destroy(&compaction_mutex);
}

/*
 * Add epc entries to the manifest during compaction
 */
int comp_cnt = 0;
void compaction_append_entry(char* key, struct epc_entry* entry) {
  if ((entry->obj_size & COMPACTION_MASK(0xFF)) <
      COMPACTION_MASK(compaction_number)) {
    struct manifest_entry* new_entry =
        (struct manifest_entry*)malloc(sizeof(struct manifest_entry));
    struct manifest_object_entry* new_object_entry =
        (struct manifest_object_entry*)new_entry;
    memcpy(new_object_entry->obj_sign, entry->obj_sign,
           sizeof(new_object_entry->obj_sign));  // calculated hmac
    new_object_entry->obj_offset =
        *(uint64_t*)key |
        UNDO_OBJECT_ENTRY;  // just to add the entry directly to the epc hashmap
    new_object_entry->obj_poolid =
        curr_pool_id;  // the only place where this variable is used
    new_object_entry->obj_size = entry->obj_size | (OBJ_NLANES_MANIFEST << 52);
    new_entry->tcv = _tcv_temp;
    _tcv_temp++;
    uint8_t tag[HMAC_SIZE];

    encrypt_final_direct((uint8_t*)new_entry, ENTRY_ENCRYPTED_DATA_SIZE, tag,
                         NULL, 0, (uint8_t*)&(new_object_entry->obj_poolid),
                         (uint8_t*)new_manifest_append_addr);
    pmem_memcpy((void*)(new_manifest_append_addr + ENTRY_ENCRYPTED_DATA_SIZE),
                tag, MANIFEST_ENTRY_SIZE - ENTRY_ENCRYPTED_DATA_SIZE,
                PMEM_F_MEM_NONTEMPORAL);
    pmem_drain();
    new_manifest_append_addr += MANIFEST_ENTRY_SIZE;
    free(new_entry);

    comp_cnt++;
  }
}

void compaction_append_dummy_entries(int num_entries) {
  for (int i = 0; i < num_entries; i++) {
    struct manifest_entry* new_entry =
        (struct manifest_entry*)malloc(sizeof(struct manifest_entry));
    struct manifest_object_entry* new_object_entry =
        (struct manifest_object_entry*)new_entry;
    memset(new_object_entry->obj_sign, 0, sizeof(new_object_entry->obj_sign));
    new_object_entry->obj_offset =
        UNDO_OBJECT_ENTRY;  // just to add the entry directly to the epc hashmap
    new_object_entry->obj_poolid = 0;
    new_object_entry->obj_size = (OBJ_NLANES_MANIFEST << 52);
    new_entry->tcv = _tcv_temp;
    _tcv_temp++;
    uint8_t tag[HMAC_SIZE];

    encrypt_final_direct((uint8_t*)new_entry, ENTRY_ENCRYPTED_DATA_SIZE, tag,
                         NULL, 0, (uint8_t*)&(new_object_entry->obj_poolid),
                         (uint8_t*)new_manifest_append_addr);
    pmem_memcpy((void*)(new_manifest_append_addr + ENTRY_ENCRYPTED_DATA_SIZE),
                tag, MANIFEST_ENTRY_SIZE - ENTRY_ENCRYPTED_DATA_SIZE,
                PMEM_F_MEM_NONTEMPORAL);
    pmem_drain();
    new_manifest_append_addr += MANIFEST_ENTRY_SIZE;
    free(new_entry);
  }
}

int add_epc_entries(void* none, void* data, char* key_pmemoid) {
  PMEMoid* temp = (PMEMoid*)key_pmemoid;
  if ((((struct epc_entry*)data)->obj_size & COMPACTION_MASK(0xFF)) <
      COMPACTION_MASK(compaction_number)) {
    struct manifest_entry* new_entry =
        (struct manifest_entry*)malloc(sizeof(struct manifest_entry));
    struct manifest_object_entry* new_object_entry =
        (struct manifest_object_entry*)new_entry;
    memcpy(new_object_entry->obj_sign, ((struct epc_entry*)data)->obj_sign,
           sizeof(new_object_entry->obj_sign));  // calculated hmac
    new_object_entry->obj_offset = temp->off;
    new_object_entry->obj_poolid = temp->pool_uuid_lo;
    new_object_entry->obj_size = ((struct epc_entry*)data)->obj_size;
    new_entry->tcv = _tcv_temp++;
    uint8_t* ciphertext;
    uint8_t* tag = (uint8_t*)malloc(HMAC_SIZE * sizeof(uint8_t));

    ciphertext =
        encrypt_final((uint8_t*)new_entry, ENTRY_ENCRYPTED_DATA_SIZE, tag, NULL,
                      0, (uint8_t*)&(new_object_entry->obj_poolid));
    memcpy(new_entry->entry_sign, tag, HMAC_SIZE);
    memcpy((unsigned char*)new_entry, ciphertext, ENTRY_ENCRYPTED_DATA_SIZE);
    pmem_memcpy_persist(new_manifest_append_addr, new_entry,
                        MANIFEST_ENTRY_SIZE);
    new_manifest_append_addr += MANIFEST_ENTRY_SIZE;
    free(tag);
    free(new_entry);
  }

  return 0;
}

/*
 * Open/Create Manifest File
 * Returns pointer to the mapped Manifest File
 */
char* load_manifest(const char* path, size_t manifest_size) {
  if (manifest_size == 0) {
    // if manifest does not exist
    manifest_size = MANIFEST_DEFAULT_SIZE;
  }
  strcpy(manifest_path, path);
  size_t mapped_len;
  int is_pmem;
  char* pmemaddr;
  if ((pmemaddr = pmem_map_file(path, manifest_size, PMEM_FILE_CREATE, 0666,
                                &mapped_len, &is_pmem)) == NULL) {
    perror("manifest pmem_map_file");
    exit(1);
  }

  manifest_offset_cnt = 0;
  curr_manifest_size = manifest_size;  // redundant if curr_manifest_size is the
                                       // only one used and it's global
  return pmemaddr;
}

/*
 * Unmaps Manifest File
 */
void unload_manifest(void* manifest_addr, size_t manifest_size) {
  if (manifest_addr == NULL)
    pmem_unmap(manifest_start_address, curr_manifest_size);
  else
    pmem_unmap(manifest_addr, manifest_size);

#ifdef DEBUG
  printf("%ld %ld\n", (inc(_tcv) - 1) * MANIFEST_ENTRY_SIZE,
         MANIFEST_DEFAULT_SIZE);
#endif
}

/*
 * Add an entry into temp list for a specific tx_lane_id
 */
int add_to_temp_list(struct temp_list_head* temp_list, uint64_t poolid,
                     uint64_t obj_offset, uint8_t* obj_sign,
                     uint64_t obj_size) {
  struct temp_list_entry* list_entry =
      (struct temp_list_entry*)malloc(sizeof(struct temp_list_entry));
  list_entry->data = (struct manifest_object_entry*)malloc(
      sizeof(struct manifest_object_entry));
  list_entry->data->obj_poolid = poolid;
  list_entry->data->obj_offset = obj_offset;
  list_entry->data->obj_size = obj_size;
  memcpy(list_entry->data->obj_sign, obj_sign, HMAC_SIZE);
  list_entry->next = NULL;

  if (temp_list->head == NULL) {  // insert first element
    temp_list->head = list_entry;
  } else {
    struct temp_list_entry* curr = temp_list->head;
    while (curr->next != NULL) {
      curr = curr->next;
    }
    curr->next = list_entry;
  }

  return 1;
}
/*
 * Add an entry into temp list for a specific tx_lane_id / replaces and flushes
 * to EPC the previous owner of this cell
 */
int add_with_replace_to_temp_list(struct temp_list_head* temp_list,
                                  uint64_t poolid, uint64_t obj_offset,
                                  uint8_t* obj_sign, uint64_t obj_size) {
  if (temp_list->head == NULL) {
    struct temp_list_entry* list_entry =
        (struct temp_list_entry*)malloc(sizeof(struct temp_list_entry));
    list_entry->data = (struct manifest_object_entry*)malloc(
        sizeof(struct manifest_object_entry));
    list_entry->data->obj_poolid = poolid;
    list_entry->data->obj_offset = obj_offset;
    list_entry->data->obj_size = obj_size;
    memcpy(list_entry->data->obj_sign, obj_sign, HMAC_SIZE);
    list_entry->next = NULL;
    temp_list->head = list_entry;
  } else {
    struct temp_list_entry* curr;
    curr = temp_list->head;
    while (1) {
      // if same object signature found
      if (curr->data->obj_offset == obj_offset &&
          curr->data->obj_poolid == poolid &&
          memcmp(curr->data->obj_sign, obj_sign, HMAC_SIZE) !=
              0)  // for the case that a redo log retries to add the same entry
                  // without actually writing the data
      {
        // store_epc_entry(curr->data->obj_poolid, curr->data->obj_offset,
        //            (uint8_t*)curr->data->obj_sign, curr->data->obj_size, 1);
        curr->data->obj_size = obj_size;
        memcpy(curr->data->obj_sign, obj_sign, HMAC_SIZE);
        break;
      } else if (curr->next == NULL) {
        struct temp_list_entry* list_entry =
            (struct temp_list_entry*)malloc(sizeof(struct temp_list_entry));
        list_entry->data = (struct manifest_object_entry*)malloc(
            sizeof(struct manifest_object_entry));
        list_entry->data->obj_poolid = poolid;
        list_entry->data->obj_offset = obj_offset;
        list_entry->data->obj_size = obj_size;
        memcpy(list_entry->data->obj_sign, obj_sign, HMAC_SIZE);
        list_entry->next = NULL;
        curr->next = list_entry;
        break;
      }
      curr = curr->next;
    }
  }
  return 1;
}
/*
 * Free the list refering to this tx_lane_id as these entries should not be
 * taken into consideration
 */
int discard_from_temp_list(struct temp_list_head* temp_list, uint64_t poolid) {
  struct temp_list_entry *curr, *prev;
  curr = temp_list->head;
  prev = curr;

  while (curr != NULL) {
    curr = curr->next;
    free(prev->data);
    free(prev);
    prev = curr;
  }

  temp_list->head = NULL;

  return 1;
}
/*
 * Apply and free a temp list refering to a tx_lane_id
 */
int add_temp_list_to_epc(struct temp_list_head* temp_list, uint64_t poolid) {
  struct temp_list_entry *curr, *prev;
  // apply and free directly to avoid second pass of the list
  curr = temp_list->head;
  prev = curr;

  while (curr != NULL) {
    // clear temp hashmap list cell
    store_epc_entry(curr->data->obj_poolid, curr->data->obj_offset,
                    (uint8_t*)curr->data->obj_sign, curr->data->obj_size, 1);
    curr = curr->next;
    free(prev->data);
    free(prev);
    prev = curr;
  }
  temp_list->head = NULL;
  return 1;
}
/*
 * Scan the (redo) temp lists in order to find the already modified signature
 * from partial (redo) ulog application
 */
uint8_t* scan_temp_list(struct temp_list_head* temp_list, uint64_t poolid,
                        uint64_t obj_offset, uint64_t* obj_size) {
  struct temp_list_entry* curr;
  curr = temp_list->head;

  while (curr != NULL) {
    if (curr->data->obj_offset == obj_offset &&
        curr->data->obj_poolid == poolid) {
      if (obj_size != NULL) *obj_size = curr->data->obj_size;
      return (uint8_t*)curr->data->obj_sign;
    }
    curr = curr->next;
  }

  return NULL;
}
/*
 * Scan the (redo) temp lists in order to find the already modified signature
 * from partial (redo) ulog application
 */
int foreach_temp_list_entry(struct temp_list_head* temp_list,
                            temp_list_entry_cb cb, uintptr_t base_addr) {
  struct temp_list_entry* curr;
  curr = temp_list->head;
  int ret = 0;
  while (curr != NULL) {
    if (cb(curr->data->obj_poolid, curr->data->obj_offset,
           (uint8_t*)curr->data->obj_sign, curr->data->obj_size, base_addr))
      ret = 1;
    curr = curr->next;
  }

  return ret;
}
/*
 * If entries have been checked, free the temp_list
 */
int free_temp_list(struct temp_list_head* temp_list) {
  struct temp_list_entry* current = temp_list->head;
  struct temp_list_entry* next;

  while (current != NULL) {
    next = current->next;
    free(current->data);
    free(current);
    current = next;
  }
  temp_list->head = NULL;

  return 1;
}
/*
 * If an entry is found to be valid, remove it from the list as the object it
 * refers to, will be modified and a new entry will be applied in the epc after
 * this modification Flags epc_update indicates if entry should be added to the
 * epc prior to deleting it.
 */
int free_temp_list_entry(struct temp_list_head* temp_list, uint64_t poolid,
                         uint64_t obj_offset, int epc_update) {
  struct temp_list_entry *temp = temp_list->head, *prev = NULL;

  // If head node itself holds the key to be deleted
  if (temp != NULL && temp->data->obj_offset == obj_offset &&
      temp->data->obj_poolid == poolid) {
    if (epc_update)
      store_epc_entry(temp->data->obj_poolid, temp->data->obj_offset,
                      (uint8_t*)temp->data->obj_sign, temp->data->obj_size, 1);
    temp_list->head = temp->next;  // Changed head
    free(temp->data);              // free old head data
    free(temp);                    // free old head
    return 1;
  }

  // Search for the key to be deleted, keep track of the
  // previous node as we need to change 'prev->next'
  while (temp != NULL && (temp->data->obj_offset != obj_offset ||
                          temp->data->obj_poolid != poolid)) {
    prev = temp;
    temp = temp->next;
  }

  // If key was not present in linked list
  if (temp == NULL) return 1;

  // Unlink the node from linked list
  prev->next = temp->next;

  if (epc_update)
    store_epc_entry(temp->data->obj_poolid, temp->data->obj_offset,
                    (uint8_t*)temp->data->obj_sign, temp->data->obj_size, 1);

  free(temp->data);  // Free memory
  free(temp);        // Free memory

  return 1;
}

/*
 * Scan the (redo) temp lists in order to find the already modified signature
 * from partial (redo) ulog application
 */
int apply_atomic_snapshots(uint64_t pool_id, uintptr_t base_addr) {
  struct atomic_snapshot* current = atomic_snapshot_list.head;

  while (current != NULL) {
    // pmem_memcpy_persist((void*)(base_addr + current->snapshot->obj_offset +
    // current->snapshot->internal_offset),
    //                    &current->snapshot->old_data, sizeof(uint64_t));
    pmem_memcpy((void*)(base_addr + current->snapshot->obj_offset +
                        current->snapshot->internal_offset),
                &current->snapshot->old_data, sizeof(uint64_t),
                PMEM_F_MEM_NODRAIN | PMEM_F_MEM_NONTEMPORAL);
    current = current->next;
  }

  free_atomic_snapshot_list(&atomic_snapshot_list);
  return 1;
}

/*
 * Add an entry into atomic snapshot list updating its preoccupant in case there
 * is one
 */
int add_with_replace_to_atomic_snapshot_list(
    struct atomic_snapshot_list_head* snapshot_list, uint64_t poolid,
    uint64_t obj_offset, uint64_t old_data, uint64_t internal_offset,
    uint64_t invalid) {
  if (snapshot_list->head == NULL) {
    struct atomic_snapshot* list_entry =
        (struct atomic_snapshot*)malloc(sizeof(struct atomic_snapshot));
    list_entry->snapshot = (struct manifest_atomic_snapshot_entry*)malloc(
        sizeof(struct manifest_atomic_snapshot_entry));
    list_entry->snapshot->obj_poolid = poolid;
    list_entry->snapshot->obj_offset = obj_offset;
    list_entry->snapshot->old_data = old_data;
    list_entry->snapshot->internal_offset = internal_offset;
    list_entry->snapshot->invalid = invalid;
    list_entry->next = NULL;
    snapshot_list->head = list_entry;
  } else {
    struct atomic_snapshot* curr;
    curr = snapshot_list->head;
    while (1) {
      // if same object signature found
      if (curr->snapshot->obj_offset == obj_offset &&
          curr->snapshot->obj_poolid == poolid &&
          curr->snapshot->internal_offset ==
              internal_offset)  // for the case that a redo log retries to add
                                // the same entry without actually writing the
                                // data
      {
        curr->snapshot->old_data = old_data;
        break;
      } else if (curr->next == NULL) {
        struct atomic_snapshot* list_entry =
            (struct atomic_snapshot*)malloc(sizeof(struct atomic_snapshot));
        list_entry->snapshot = (struct manifest_atomic_snapshot_entry*)malloc(
            sizeof(struct manifest_atomic_snapshot_entry));
        list_entry->snapshot->obj_poolid = poolid;
        list_entry->snapshot->obj_offset = obj_offset;
        list_entry->snapshot->old_data = old_data;
        list_entry->snapshot->internal_offset = internal_offset;
        list_entry->snapshot->invalid = invalid;
        list_entry->next = NULL;
        curr->next = list_entry;
        break;
      }
      curr = curr->next;
    }
  }
  return 1;
}
/*
 * If entries have been applied, free the snapshot_list
 */
int free_atomic_snapshot_list(struct atomic_snapshot_list_head* snapshot_list) {
  struct atomic_snapshot* current = snapshot_list->head;
  struct atomic_snapshot* next;

  while (current != NULL) {
    next = current->next;
    free(current->snapshot);
    free(current);
    current = next;
  }
  snapshot_list->head = NULL;

  return 1;
}

/*
 * If an entry is found to be valid, remove it from the snapshot list as a later
 * operation has finished and we have the correct signature for the data
 */
int free_atomic_snapshot_list_entry(
    struct atomic_snapshot_list_head* snapshot_list, uint64_t poolid,
    uint64_t obj_offset, uint64_t internal_offset) {
  struct atomic_snapshot *temp = snapshot_list->head, *prev = NULL;

  // If head node itself holds the key to be deleted
  if (temp != NULL && temp->snapshot->obj_offset == obj_offset &&
      temp->snapshot->obj_poolid ==
          poolid /*&& curr->snapshot->internal_offset == internal_offset*/) {
    snapshot_list->head = temp->next;  // Changed head
    free(temp->snapshot);              // free old head
    free(temp);
    return 1;
  }

  // Search for the key to be deleted, keep track of the
  // previous node as we need to change 'prev->next'
  while (
      temp != NULL &&
      (temp->snapshot->obj_offset != obj_offset ||
       temp->snapshot->obj_poolid !=
           poolid /*|| curr->snapshot->internal_offset == internal_offset*/)) {
    prev = temp;
    temp = temp->next;
  }

  // If key was not present in linked list
  if (temp == NULL) return 1;

  // Unlink the node from linked list
  prev->next = temp->next;

  free(temp->snapshot);  // Free memory
  free(temp);            // Free memory

  return 1;
}

int* get_unfinished_tx() { return unfinished_tx; }

void free_unfinished_tx() { free(unfinished_tx); }

/*
 * Scan Manifest File
 */
int scan_manifest(size_t manifest_size) {
  struct manifest_entry* entry;
  struct manifest_entry* decrypted_entry;
  entry = (struct manifest_entry*)manifest_start_address;
  uint64_t pmemaddr_init = (uint64_t)manifest_start_address;
  manifest_scan_address = (void*)pmemaddr_init;

  _start_tcv = get_counter(MANIFEST_START_COUNTER_IDX);
  if (_start_tcv == NULL) {
    _start_tcv = create_counter_idx(
        0,
        MANIFEST_START_COUNTER_IDX);  // first time in the pool, no manifest to
                                      // check
  }

  _tcv = get_counter(MANIFEST_END_COUNTER_IDX);
  if (_tcv == NULL) {
    _tcv = create_counter_idx(
        0,
        MANIFEST_END_COUNTER_IDX);  // first time in the pool, no manifest to
                                    // check
    return 1;  // as trusted counter for manifest is the first to create and now
               // does not exist
  }
  uint64_t expected_tcv = _start_tcv->_counter;  // manifest starts from zero

  unfinished_tx = (int*)malloc(OBJ_NLANES_MANIFEST * sizeof(int));
  for (int i = 0; i < OBJ_NLANES_MANIFEST; i++) unfinished_tx[i] = TX_IDLE;

  // scan over all the entries
  while ((uint64_t)entry < pmemaddr_init + manifest_size &&
         expected_tcv < _tcv->_counter) {
    // if entry cannot be decrypted it's either the end or an entry is
    // compromised and will be revealed in the tcv check
    if ((decrypted_entry = verify_entry(entry, expected_tcv)) == NULL) break;

    if ((decrypted_entry->data[ENTRY_TYPE_POSITION] & ENTRY_TYPE_MASK) ==
        UNDO_OBJECT_ENTRY)  // if it's undo object entry
    {
      struct manifest_object_entry* object_entry =
          (struct manifest_object_entry*)decrypted_entry;
      if (ENTRY_INVALID(object_entry->obj_size)) {
        // add entry temporarily in a hashmap till manifest scan ends and then
        // check which entry is the correct one printf("invalid entry\n");
      }
      if (ENTRY_TX_LANE_ID(object_entry->obj_size) < OBJ_NLANES_MANIFEST) {
        add_with_replace_to_temp_list(
            &temp_undo_list[ENTRY_TX_LANE_ID(object_entry->obj_size)],
            object_entry->obj_poolid,
            object_entry->obj_offset & ENTRY_TYPE_MASK_OFF,
            (uint8_t*)object_entry->obj_sign,
            object_entry->obj_size & ENTRY_OBJ_SIZE_MASK_OFF);
      } else {
        // refer to log entries or metadata
        store_epc_entry(object_entry->obj_poolid,
                        object_entry->obj_offset & ENTRY_TYPE_MASK_OFF,
                        (uint8_t*)object_entry->obj_sign,
                        object_entry->obj_size & ENTRY_OBJ_SIZE_MASK_OFF, 1);
      }
    } else if ((decrypted_entry->data[ENTRY_TYPE_POSITION] & ENTRY_TYPE_MASK) ==
               REDO_OBJECT_ENTRY) {
      struct manifest_object_entry* object_entry =
          (struct manifest_object_entry*)decrypted_entry;
      // if ((object_entry->obj_offset & ENTRY_TYPE_MASK_OFF) == 0x7082448)
      //    printf("entry with sign : %lX
      //    %lX\n",((uint64_t*)object_entry->obj_sign)[0],
      //    ((uint64_t*)object_entry->obj_sign)[1]);

      // printf("redo object entry in the manifest\n");
      if (ENTRY_TX_LANE_ID(object_entry->obj_size) < OBJ_NLANES_MANIFEST) {
        add_with_replace_to_temp_list(
            &temp_redo_list[ENTRY_TX_LANE_ID(object_entry->obj_size)],
            object_entry->obj_poolid,
            object_entry->obj_offset & ENTRY_TYPE_MASK_OFF,
            (uint8_t*)object_entry->obj_sign,
            object_entry->obj_size & ENTRY_OBJ_SIZE_MASK_OFF);
      } else {
        store_epc_entry(object_entry->obj_poolid,
                        object_entry->obj_offset & ENTRY_TYPE_MASK_OFF,
                        (uint8_t*)object_entry->obj_sign,
                        object_entry->obj_size & ENTRY_OBJ_SIZE_MASK_OFF, 1);
      }
    } else if ((decrypted_entry->data[ENTRY_TYPE_POSITION] & ENTRY_TYPE_MASK) ==
               ULOG_OBJECT_ENTRY) {
      struct manifest_object_entry* object_entry =
          (struct manifest_object_entry*)decrypted_entry;
      // printf("ulog object entry in the manifest\n");
      if (ENTRY_TX_LANE_ID(object_entry->obj_size) < OBJ_NLANES_MANIFEST) {
        add_with_replace_to_temp_list(
            &temp_ulog_list[ENTRY_TX_LANE_ID(object_entry->obj_size)],
            object_entry->obj_poolid,
            object_entry->obj_offset & ENTRY_TYPE_MASK_OFF,
            (uint8_t*)object_entry->obj_sign,
            object_entry->obj_size & ENTRY_OBJ_SIZE_MASK_OFF);
      } else {
        store_epc_entry(object_entry->obj_poolid,
                        object_entry->obj_offset & ENTRY_TYPE_MASK_OFF,
                        (uint8_t*)object_entry->obj_sign,
                        object_entry->obj_size & ENTRY_OBJ_SIZE_MASK_OFF, 1);
      }
    } else if ((decrypted_entry->data[ENTRY_TYPE_POSITION] & ENTRY_TYPE_MASK) ==
               ATOMIC_OBJECT_ENTRY) {
      // if not invalid -> append the object and delete its entry from the temp
      // list (validation entry) else if invalid -> we got a snapshot entry to
      // add in the temp list (snapshot entry)

      if (ENTRY_INVALID(decrypted_entry->data[VALIDATION_POSITION])) {
        /*
         * Pending atomic metadata update case / snapshot of old data
         */
        struct manifest_atomic_snapshot_entry* snapshot =
            (struct manifest_atomic_snapshot_entry*)decrypted_entry;
        add_with_replace_to_atomic_snapshot_list(
            &atomic_snapshot_list, snapshot->obj_poolid, snapshot->obj_offset,
            snapshot->old_data, snapshot->internal_offset & ENTRY_TYPE_MASK_OFF,
            snapshot->invalid);
      } else {
        /*
         * Successful atomic metadata update case
         */
        struct manifest_object_entry* object_entry =
            (struct manifest_object_entry*)decrypted_entry;
        free_atomic_snapshot_list_entry(
            &atomic_snapshot_list, object_entry->obj_poolid,
            object_entry->obj_offset & ENTRY_TYPE_MASK_OFF, 0);
        store_epc_entry(object_entry->obj_poolid,
                        object_entry->obj_offset & ENTRY_TYPE_MASK_OFF,
                        (uint8_t*)object_entry->obj_sign,
                        object_entry->obj_size & ENTRY_OBJ_SIZE_MASK_OFF, 1);
      }
    } else if ((decrypted_entry->data[ENTRY_TYPE_POSITION] & ENTRY_TYPE_MASK) ==
               TX_INFO_ENTRY)  // if it's transaction info entry
    {
      struct manifest_tx_info_entry* tx_info_entry =
          (struct manifest_tx_info_entry*)decrypted_entry;
      switch (tx_info_entry->tx_stage) {
        case TX_START:
          unfinished_tx[tx_info_entry->tx_lane_id] = TX_STARTED;
        case TX_ABORT:  // aborted transaction, or new transaction started
                        // before the other finishes
          discard_from_temp_list(&temp_redo_list[tx_info_entry->tx_lane_id],
                                 tx_info_entry->poolid);
          discard_from_temp_list(&temp_undo_list[tx_info_entry->tx_lane_id],
                                 tx_info_entry->poolid);
          break;
        case TX_FINISH:  // transaction finished successfully
          unfinished_tx[tx_info_entry->tx_lane_id] = TX_IDLE;
          add_temp_list_to_epc(&temp_redo_list[tx_info_entry->tx_lane_id],
                               tx_info_entry->poolid);
          add_temp_list_to_epc(&temp_undo_list[tx_info_entry->tx_lane_id],
                               tx_info_entry->poolid);
          add_temp_list_to_epc(&temp_ulog_list[tx_info_entry->tx_lane_id],
                               tx_info_entry->poolid);
          break;
        case TX_COMMIT:  // redo log is either saved or even processed, so save
                         // undo logs for now / redo will be saved when we get a
                         // finished signal
          unfinished_tx[tx_info_entry->tx_lane_id] = TX_COMMITED;
          break;
        default:
          break;
      }
    } else {
      printf("Unknown manifest entry type\n");
    }

    free(decrypted_entry);
    expected_tcv++;
    entry++;  // get to the next entry
  }

  for (int i = 0; i < OBJ_NLANES_MANIFEST; i++) {
    if (unfinished_tx[i] != TX_IDLE) printf("lane to recover: %d\n", i);
  }

  // the counter is incremented after each append
  // so it contains the value to be appended next
  // if expected_tcv is larger than the counter, then we have unstable entries!
#ifdef DEBUG
  printf("expected counter : %ld\n", expected_tcv);
#endif
  if (expected_tcv < _tcv->_counter) {
    printf("Manifest freshness check failed\n");
    return 0;
  }

  return 1;
}

/*
 * Verify Manifest Entry
 */
struct manifest_entry* verify_entry(struct manifest_entry* entry,
                                    uint64_t expected_tcv) {
  if (!(entry->data[0] == 0 &&
        memcmp(entry->data, entry->data + 1,
               (ENTRY_DATA_SIZE - 1 * sizeof(uint64_t))) ==
            0))  // same as util_is_zeroed function
  {
    manifest_scan_address += MANIFEST_ENTRY_SIZE;
  } else {
    return NULL;
  }

  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = 0;
  iv[1] = manifest_scan_address - manifest_start_address - MANIFEST_ENTRY_SIZE;
  struct manifest_entry* decryptedtext = (struct manifest_entry*)decrypt_final(
      (unsigned char*)entry, ENTRY_ENCRYPTED_DATA_SIZE,
      (uint8_t*)entry->entry_sign, NULL, 0, (uint8_t*)iv);

  if (decryptedtext == NULL) {
    printf("Corrupted Manifest Entry\n");
    exit(1);
  }

  if (decryptedtext->tcv != expected_tcv) {
    printf("Corrupted Manifest Counter\n");
    exit(1);
  }
  return decryptedtext;
}

/*
 * Store verified manifest entry in the Volatile Memory Hashmap Structure
 */
int store_epc_entry(uint64_t pool_id, uint64_t obj_offset, uint8_t* obj_sign,
                    uint64_t obj_size, int update_cache) {
  int error __attribute__((unused));
  uint64_t key_string = obj_offset;
  if (obj_sign == NULL) {
    error = epc_metadata_remove((uint8_t*)&key_string);
#ifdef DEBUG
    assert(error == 0);
#endif
  } else if (obj_size == 0 && *(uint64_t*)obj_sign == 0 &&
             *(uint64_t*)(obj_sign + 1) == 0)  // deleted object
  {
    error = epc_metadata_remove((uint8_t*)&key_string);
#ifdef DEBUG
    assert(error == 0);
#endif
  } else {
    struct epc_entry* new_entry =
        (struct epc_entry*)malloc(sizeof(struct epc_entry));

    new_entry->obj_size = obj_size | COMPACTION_MASK(compaction_number);

    memcpy(new_entry->obj_sign, obj_sign, HMAC_SIZE);
    new_entry->obj_ptr.obj_data = NULL;
    new_entry->obj_ptr.epoch = 0;

    epc_metadata_set((uint8_t*)&key_string, (void*)new_entry, update_cache);
  }
  return 1;
}

/*
 * Scan & Check manifest
 * Returns 1 upon successful completion
 */
int manifest_boot(const char* path) {
  // Initialisation of global variables for new instance of
  // pool_opening/creation
  global_init();

  // Manifest boot
  size_t manifest_size = get_manifest_size(path);
  manifest_start_address = load_manifest(path, manifest_size);
  if (!scan_manifest(manifest_size)) {
    return 0;
  }

  // create thread for compaction
  tid = 0;
  pthread_create(&tid, NULL, &compaction_check, NULL);
  return 1;
}
/*
 * Set pool id as a variable for manifest compaction
 */
void manifest_set_pool_id(uint64_t pool_id) { curr_pool_id = pool_id; }

/*
 * Get Manifest file size
 */
size_t get_manifest_size(const char* path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return st.st_size;
  } else {
    return (size_t)0;
  }
}
// Append transaction related entry in the manifest
uint64_t append_tx_info_entry(uint64_t pool_id, uint64_t tx_lane_id,
                              uint64_t tx_stage) {
#ifdef DEBUG

  tx_info++;
  switch (tx_stage) {
    case TX_START:
      // printf("tx_start\n");
      tx_start++;
      break;
    case TX_COMMIT:
      // printf("TX_COMMIT\n");
      tx_commit++;
      break;
    case TX_ABORT:
      // printf("TX_ABORT\n");
      tx_abort++;
      break;
    case TX_REC_REDO:
      // printf("TX_REC_REDO\n");
      tx_rec_redo++;
      break;
    case TX_REC_UNDO:
      // printf("TX_REC_UNDO\n");
      tx_rec_undo++;
      break;
    case TX_FINISH:
      // printf("tx_finish\n");
      tx_finish++;
      break;
    case ULOG_HDR_UPDATE:
      ulog_hdr_update++;
      break;
    default:
      break;
  }
#endif

  switch (tx_stage) {
    case TX_START:
      active_tx++;
      break;
    case TX_FINISH:
      active_tx--;
      break;
    default:
      break;
  }

  struct manifest_tx_info_entry new_entry;
  new_entry.poolid = pool_id;
  new_entry.tx_lane_id = tx_lane_id;
  new_entry.tx_stage = tx_stage;
  new_entry.entry_type = TX_INFO_ENTRY;
  new_entry.unused = 0;

  uint64_t counter = append_entry((uint8_t*)&new_entry);

  return counter;
}

uint64_t append_undo_object_entry(PMEMoid oid, uint8_t* obj_tag,
                                  uint64_t tx_lane_id, size_t size, int invalid,
                                  int add_to_epc) {
#ifdef DEBUG
  undo_object++;
#endif
  struct manifest_object_entry
      new_entry;  // = (struct manifest_object_entry*) malloc (sizeof(struct
                  // manifest_object_entry));

  if (obj_tag == NULL) {
    memset(new_entry.obj_sign, 0, HMAC_SIZE);  // delete case
  } else {
    memcpy(new_entry.obj_sign, obj_tag, HMAC_SIZE);  // calculated hmac
  }

  new_entry.obj_offset = oid.off | UNDO_OBJECT_ENTRY;
  new_entry.obj_poolid = oid.pool_uuid_lo;
  new_entry.obj_size = size | (tx_lane_id << 52) | ((uint64_t)invalid << 63);

  if (add_to_epc) {
    store_epc_entry(new_entry.obj_poolid,
                    new_entry.obj_offset & ENTRY_TYPE_MASK_OFF,
                    (uint8_t*)new_entry.obj_sign,
                    new_entry.obj_size & ENTRY_OBJ_SIZE_MASK_OFF,
                    1);  // store the entry in the hashmap
  }

  uint64_t counter = append_entry((uint8_t*)&new_entry);
  return counter;
}

uint64_t append_redo_object_entry(PMEMoid oid, uint8_t* obj_tag,
                                  uint64_t tx_lane_id, size_t size,
                                  int invalid) {
#ifdef DEBUG
  redo_object++;
#endif
  struct manifest_object_entry new_entry;

  if (obj_tag == NULL) {
    memset(new_entry.obj_sign, 0, HMAC_SIZE);  // delete case
  } else {
    memcpy(new_entry.obj_sign, obj_tag, HMAC_SIZE);  // calculated hmac
  }

  new_entry.obj_offset = oid.off | REDO_OBJECT_ENTRY;
  new_entry.obj_poolid = oid.pool_uuid_lo;
  new_entry.obj_size = size | (tx_lane_id << 52) | ((uint64_t)invalid << 63);

  store_epc_entry(new_entry.obj_poolid,
                  new_entry.obj_offset & ENTRY_TYPE_MASK_OFF,
                  (uint8_t*)new_entry.obj_sign,
                  new_entry.obj_size & ENTRY_OBJ_SIZE_MASK_OFF,
                  1);  // store the entry in the hashmap

  uint64_t counter = append_entry((uint8_t*)&new_entry);
  return counter;
}

uint64_t append_ulog_object_entry(PMEMoid oid, uint8_t* obj_tag,
                                  uint64_t tx_lane_id, size_t size,
                                  int invalid) {
#ifdef DEBUG
  ulog_object++;
#endif
  struct manifest_object_entry
      new_entry;  // = (struct manifest_object_entry*) malloc (sizeof(struct
                  // manifest_object_entry));

  if (obj_tag == NULL) {
    memset(new_entry.obj_sign, 0, HMAC_SIZE);  // delete case
  } else {
    memcpy(new_entry.obj_sign, obj_tag, HMAC_SIZE);  // calculated hmac
  }

  new_entry.obj_offset = oid.off | ULOG_OBJECT_ENTRY;
  new_entry.obj_poolid = oid.pool_uuid_lo;
  new_entry.obj_size = size | (tx_lane_id << 52) | ((uint64_t)invalid << 63);

  store_epc_entry(new_entry.obj_poolid,
                  new_entry.obj_offset & ENTRY_TYPE_MASK_OFF,
                  (uint8_t*)new_entry.obj_sign,
                  new_entry.obj_size & ENTRY_OBJ_SIZE_MASK_OFF,
                  1);  // store the entry in the hashmap

  uint64_t counter = append_entry((uint8_t*)&new_entry);
  return counter;
}

uint64_t append_atomic_object_entry(PMEMoid oid, uint8_t* obj_tag, size_t size,
                                    int invalid) {
#ifdef DEBUG
  // redo_object++;
  atomic_entry++;
#endif
  assert(size == sizeof(uint64_t));
  struct manifest_object_entry
      new_entry;  // = (struct manifest_object_entry*) malloc (sizeof(struct
                  // manifest_object_entry));

  if (obj_tag == NULL) {
    memset(new_entry.obj_sign, 0, HMAC_SIZE);  // delete case
  } else {
    memcpy(new_entry.obj_sign, obj_tag, HMAC_SIZE);  // calculated hmac
  }

  new_entry.obj_offset = oid.off | REDO_OBJECT_ENTRY;
  new_entry.obj_poolid = oid.pool_uuid_lo;
  new_entry.obj_size = size | ((uint64_t)invalid << 63);

  store_epc_entry(new_entry.obj_poolid,
                  new_entry.obj_offset & ENTRY_TYPE_MASK_OFF,
                  (uint8_t*)new_entry.obj_sign,
                  new_entry.obj_size & ENTRY_OBJ_SIZE_MASK_OFF,
                  1);  // store the entry in the hashmap

  uint64_t counter = append_entry((uint8_t*)&new_entry);
  // free(new_entry);
  return counter;
}

/*
 * Append Entry to the Manifest
 * Doubles the size of the manifest when it is full / going to be full
 */
int writers = 0;
int concur_updates = 0;
uint64_t append_entry(uint8_t* new_entry_data) {
#ifdef DEBUG
  total_entries++;
#endif
#ifdef STATISTICS
  stats_measure_start(MANIFEST_LOG);
#ifdef WRITE_AMPL
  bytes_written_inc(MANIFEST, MANIFEST_ENTRY_SIZE);
#endif
#endif

  __sync_fetch_and_add(&writers, 1);

  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = 0;

  if (new_manifest_concur_update_addr != NULL) {
    pthread_mutex_lock(&compaction_mutex);
    if (compaction_finished) {
#ifdef DEBUG
      printf("writers: %d\n", writers);
#endif
      while (writers != 1)
        ;  // wait till I am the only writer
#ifdef DEBUG
      printf("Manifest file updated\n");
#endif
      // manifest_append_address = new_manifest_concur_update_addr;
      manifest_start_address = new_manifest_start_addr;
      new_manifest_start_addr = NULL;
      manifest_offset_cnt = start_entries;
      start_entries = 0;
      set_counter(_tcv,
                  (_tcv->_counter - 1) +
                      manifest_offset_cnt);  // curr + k // -1 because counter
                                             // shows the next value every time
      set_counter(_start_tcv, compaction_starting_cnt);  // n + 1

#ifdef DEBUG
      printf("updated end cnt (n+k+m): %ld\n", _tcv->_counter);
      printf("updated start cnt (n+1): %ld\n", _start_tcv->_counter);
      printf("k = %d\n", manifest_offset_cnt);
      printf("concur_updates m = %d\n", concur_updates);
#endif
      concur_updates = 0;
      int ret = rename(new_manifest_path, manifest_path);
      if (ret == 0) {
#ifdef DEBUG
        printf("File renamed successfully\n");
#endif
      } else {
        printf("Error: unable to rename the file");
      }
      curr_manifest_size *= 2;
      compaction_active = 0;
      __atomic_store_n(&compaction_finished, 0, __ATOMIC_SEQ_CST);
      // wait for the last counter to become stable
      while (query_counter(MANIFEST_END_COUNTER_IDX, _tcv->_counter))
        ;
      new_manifest_concur_update_addr = NULL;
    }
    pthread_mutex_unlock(&compaction_mutex);  // Release the mutex
  }

  // new_entry is 40 bytes in length, so it should be reallocated to get the
  // 8byte counter
  struct manifest_entry* new_entry = (struct manifest_entry*)new_entry_data;

  new_entry->tcv = inc(_tcv);
  void* manifest_append_address =
      manifest_start_address +
      (new_entry->tcv - manifest_offset_cnt) * MANIFEST_ENTRY_SIZE;

  if (!compaction_active && !compaction_finished && active_tx == 0 &&
      ((manifest_append_address - manifest_start_address) >
       COMPACTION_THRESHOLD * curr_manifest_size)) {
    compaction_active = 1;
    compaction_starting_cnt = new_entry->tcv;  // n+1
#ifdef DEBUG
    printf("active tx : %d\n", active_tx);
    printf(" n + 1 : %ld\n", compaction_starting_cnt);
    printf("%d > %f\n", manifest_append_address - manifest_start_address,
           COMPACTION_THRESHOLD * curr_manifest_size);
#endif
    pthread_cond_signal(&compaction_start);
  }

  iv[1] = manifest_append_address - manifest_start_address;

  encrypt_final_direct((uint8_t*)new_entry, ENTRY_ENCRYPTED_DATA_SIZE, tag,
                       NULL, 0, (uint8_t*)iv,
                       (uint8_t*)manifest_append_address);
  pmem_memcpy((void*)(manifest_append_address + ENTRY_ENCRYPTED_DATA_SIZE), tag,
              MANIFEST_ENTRY_SIZE - ENTRY_ENCRYPTED_DATA_SIZE,
              PMEM_F_MEM_NONTEMPORAL);
  pmem_drain();

  // when compaction is active, write to the new manifest too
  if (compaction_active && !compaction_finished) {
    // wait till new manifest is mapped
    if (new_manifest_concur_update_addr == NULL) {
      pthread_mutex_lock(&new_manifest_lock);
      while (new_manifest_concur_update_addr == NULL) {
        pthread_cond_wait(&new_manifest_mapped, &new_manifest_lock);
      }
      pthread_mutex_unlock(&new_manifest_lock);
    }
    concur_updates++;
    void* concur_append_addr = __sync_fetch_and_add(
        &new_manifest_concur_update_addr, MANIFEST_ENTRY_SIZE);
    new_entry->tcv += start_entries;  // n+k+m
    // printf(" n + k + m : %ld | ",new_entry->tcv);
    iv[1] = concur_append_addr - new_manifest_start_addr;
    encrypt_final_direct((uint8_t*)new_entry, ENTRY_ENCRYPTED_DATA_SIZE, tag,
                         NULL, 0, (uint8_t*)iv, (uint8_t*)concur_append_addr);
    pmem_memcpy((void*)(concur_append_addr + ENTRY_ENCRYPTED_DATA_SIZE), tag,
                MANIFEST_ENTRY_SIZE - ENTRY_ENCRYPTED_DATA_SIZE,
                PMEM_F_MEM_NONTEMPORAL);
    pmem_drain();
  }

#ifdef STATISTICS
  stats_measure_end(MANIFEST_LOG);
#endif
  __sync_fetch_and_sub(&writers, 1);
  return _tcv->_counter;
}

/*
 * Check for the compaction time in a separate thread
 */
void* compaction_check(void* args) {
  // currently active waiting!
  pthread_mutex_lock(&lock);
  while (1) {
    pthread_cond_wait(&compaction_start, &lock);
    if (process_ended) {
#ifdef DEBUG
      printf("Process ended - compaction thread ended \n");
#endif
      return NULL;
    }

    // wait for transactions to finish
    compaction_number++;
#ifdef DEBUG
    printf("I am creating a new manifest\n");
#endif
    _tcv_temp = compaction_starting_cnt;  // n + 1

    size_t mapped_len;
    int is_pmem;
    char* pmemaddr;
    size_t new_size = 2 * curr_manifest_size;
    start_entries = epc_number_of_objects();  // k
#ifdef DEBUG
    printf(" k : %ld\n", start_entries);
    printf("hashmap length for new manifest : %d %ld\n", start_entries,
           new_size);
#endif

    // prepare the new manifest file name
    char serial_no[2] = {};
    snprintf(serial_no, 2, "%d", manifest_serial_no);
    strcpy(new_manifest_path, manifest_path);
    strcat(new_manifest_path, serial_no);
    if ((pmemaddr = pmem_map_file(new_manifest_path, new_size, PMEM_FILE_CREATE,
                                  0666, &mapped_len, &is_pmem)) == NULL) {
      perror("pmem_map_file");
      exit(1);
    }
    new_manifest_append_addr = (void*)pmemaddr;
    new_manifest_start_addr = (void*)pmemaddr;

    new_manifest_concur_update_addr =
        (void*)pmemaddr + start_entries * MANIFEST_ENTRY_SIZE;
    pthread_cond_signal(&new_manifest_mapped);

    epc_forEach(compaction_append_entry);
    compaction_append_dummy_entries(start_entries - comp_cnt);
#ifdef DEBUG
    printf("copied entries : %d\n", comp_cnt);
    printf("entries to fill : %d\n", start_entries - comp_cnt);
#endif
    comp_cnt = 0;
    manifest_serial_no++;
    __atomic_store_n(&compaction_finished, 1, __ATOMIC_SEQ_CST);

    pthread_cond_signal(&compaction_end);
  }

  // we still own the mutex. remember to release it on exit
  pthread_mutex_unlock(&lock);

  return NULL;
}

/*
 * Manifest compaction thread stopper
 */
void close_compaction_thread(void) {
  process_ended = 1;

  while (compaction_active)
    ;
  pthread_cond_signal(&compaction_start);
  pthread_join(tid, NULL);
}

#ifdef DEBUG
void print_manifest_stats() {
  printf("Manifest Stats:\n");
  printf("Total Entries:\t\t\t%d\n", total_entries);
  printf("TX Info Entries:\t\t%d\t%.2f%%\n", tx_info,
         (float)tx_info / total_entries * 100);
  printf("Undo Object Entries:\t\t%d\t%.2f%%\n", undo_object,
         (float)undo_object / total_entries * 100);
  printf("Redo Object Entries:\t\t%d\t%.2f%%\n", redo_object,
         (float)redo_object / total_entries * 100);
  printf("Ulog Object Entries:\t\t%d\t%.2f%%\n", ulog_object,
         (float)ulog_object / total_entries * 100);
  printf("Atomic Object Entries:\t\t%d\t%.2f%%\n", atomic_entry,
         (float)atomic_entry / total_entries * 100);
  printf("Atomic Snapshot Entries:\t%d\t%.2f%%\n", atomic_snapshot,
         (float)atomic_snapshot / total_entries * 100);
  printf("\n");
  printf("TX INFO Stats:\n");
  printf("Total TX INFO Entries:\t\t\t%d\n", tx_info);
  printf("tx_start Entries:\t\t%d\t%.2f%%\n", tx_start,
         (float)tx_start / tx_info * 100);
  printf("tx_commit Entries:\t\t%d\t%.2f%%\n", tx_commit,
         (float)tx_commit / tx_info * 100);
  printf("tx_abort Entries:\t\t%d\t%.2f%%\n", tx_abort,
         (float)tx_abort / tx_info * 100);
  printf("tx_rec_redo Entries:\t\t%d\t%.2f%%\n", tx_rec_redo,
         (float)tx_rec_redo / tx_info * 100);
  printf("tx_rec_undo Entries:\t\t%d\t%.2f%%\n", tx_rec_undo,
         (float)tx_rec_undo / tx_info * 100);
  printf("tx_finish Entries:\t\t%d\t%.2f%%\n", tx_finish,
         (float)tx_finish / tx_info * 100);
  printf("ulog_hdr_update Entries:\t%d\t%.2f%%\n", ulog_hdr_update,
         (float)ulog_hdr_update / tx_info * 100);
}
#endif