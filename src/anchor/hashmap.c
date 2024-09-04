#include "hashmap.h"
#include "metadata_operations.h"
#ifdef STATISTICS
#include "internal_statistics.h"
#endif
#include <pthread.h>
#include <stdio.h>
#define hash_func meiyan

#define MEGABYTE 1048576
#define FLUSH_THRES 30 * MEGABYTE
int64_t acc_bytes = 0;
#define FLUSH 1
#define IDLE 0
int flushing = 0;
int process_ended = 0;
extern int compaction_number;

pthread_t flush_tid;
// Declaration of condition variables
pthread_cond_t epc_flush_start;
// Declaration of mutexes
pthread_mutex_t flush_lock;

#define MAX_THREADS 64  // indexes 0-63 for the lanes!
uint64_t curr_epoch;
uint64_t min_epoch;
uint64_t epoch[MAX_THREADS];

void *dic_cache_flush_thr(void *arg);

uint64_t get_curr_epoch() { return curr_epoch; }
void set_min_epoch() {
  // no need for synchronisation here
  // worst case we will free less objects than we can
  uint64_t min = epoch[0];
  for (int i = 1; i < MAX_THREADS; i++) {
    if (min > epoch[i]) {
      min = epoch[i];
    }
  }
  __atomic_store_n(&min_epoch, min, __ATOMIC_SEQ_CST);
}
void update_tx_epoch(uint64_t tx_lane_id, uint64_t new_epoch) {
  __atomic_store_n(&epoch[tx_lane_id], new_epoch, __ATOMIC_SEQ_CST);
}
uint64_t get_min_epoch() { return min_epoch; }


static inline uint32_t meiyan(const char *key) {
  typedef uint32_t *P;
  int count = KEY_SIZE;
  uint32_t h = 0x811c9dc5;
  while (count >= 8) {
    h = (h ^ ((((*(P)key) << 5) | ((*(P)key) >> 27)) ^ *(P)(key + 4))) *
        0xad3e7;
    count -= 8;
    key += 8;
  }
#define tmp                             \
  h = (h ^ *(uint16_t *)key) * 0xad3e7; \
  key += 2;
  if (count & 4) {
    tmp tmp
  }
  if (count & 2) {
    tmp
  }
  if (count & 1) {
    h = (h ^ *key) * 0xad3e7;
  }
#undef tmp
  return h ^ (h >> 16);
}

struct keynode *keynode_new(char *k) {
  struct keynode *node = malloc(sizeof(struct keynode));
  memcpy(node->key, k, KEY_SIZE);
  node->next = NULL;
  node->value = NULL;
  return node;
}

void keynode_delete_chain(struct keynode *node) {
  struct epc_entry *typed_node = (struct epc_entry *)(node->value);
  if (typed_node->obj_ptr.obj_data != NULL) {
    free(typed_node->obj_ptr.obj_data);
    typed_node->obj_ptr.obj_data = NULL;
  }

  free(node->value);

  if (node->next) keynode_delete_chain(node->next);

  free(node);
}

void keynode_delete(struct dictionary *dic, struct keynode *node,
                    struct keynode *prev) {
  struct epc_entry *typed_node = (struct epc_entry *)(node->value);
  if (typed_node->obj_ptr.obj_data != NULL) {
    free(typed_node->obj_ptr.obj_data);
    typed_node->obj_ptr.obj_data = NULL;
    __sync_fetch_and_sub(&acc_bytes,
                         typed_node->obj_size & COMPACTION_MASK_OFF);
  }

  free(node->value);
  if (node->next != NULL) {
    prev->next = node->next;
  } else {
    prev->next = NULL;
  }

  free(node);
}

struct dictionary *dic_new(int initial_size) {
  ;
  struct dictionary *dic = malloc(sizeof(struct dictionary));
  if (initial_size == 0) initial_size = 204800;  // 409600;
  dic->length = initial_size;
  dic->count = 0;
  dic->table = calloc(sizeof(struct keynode *), initial_size);
  dic->growth_treshold = 2.0;
  dic->growth_factor = 10;
  dic->pool_id = 0;  // set it to 0 for now

  curr_epoch = 0;
  min_epoch = 0;
  for (int i = 0; i < MAX_THREADS; i++) {
    epoch[i] = UINT64_MAX;
  }

  dic->_bucket_rw_lock = (int64_t *)calloc(initial_size, sizeof(int64_t));

  int ret;
  ret = pthread_cond_init(&epc_flush_start, NULL);
  assert(ret == 0);
  ret = pthread_mutex_init(&flush_lock, NULL);
  assert(ret == 0);

  __atomic_store_n(&acc_bytes, 0, __ATOMIC_SEQ_CST);
  __atomic_store_n(&flushing, IDLE, __ATOMIC_SEQ_CST);

  pthread_rwlock_init(&dic->_global_rw_lock, NULL);

  if (pthread_create(&flush_tid, NULL, dic_cache_flush_thr, dic) != 0) {
    perror("pthread_create() error");
    exit(1);
  }

  return dic;
}

/*
 * Manifest compaction thread stopper
 */
void close_flush_thread(void) {
  process_ended = 1;

  while (flushing == FLUSH)
    ;
  pthread_cond_signal(&epc_flush_start);
  pthread_join(flush_tid, NULL);
}

void dic_delete(struct dictionary *dic) {
  for (int i = 0; i < dic->length; i++) {
    if (dic->table[i]) {
      while (!__sync_bool_compare_and_swap(&dic->_bucket_rw_lock[i], 0,
                                           INT64_MIN)) {
        for (volatile int i = 0; i < 1000; i++)
          ;
      }
      keynode_delete_chain(dic->table[i]);
      __atomic_store_n(&dic->_bucket_rw_lock[i], 0, __ATOMIC_SEQ_CST);
    }
  }
  free(dic->table);
  dic->table = 0;

  close_flush_thread();
  pthread_cond_destroy(&epc_flush_start);
  pthread_mutex_destroy(&flush_lock);
  pthread_rwlock_destroy(&dic->_global_rw_lock);

  free(dic->_bucket_rw_lock);

  free(dic);
}

void cache_flush_chain(struct keynode *node) {
  if (node != NULL) {
    struct epc_entry *typed_node = (struct epc_entry *)(node->value);
    if (typed_node->obj_ptr.obj_data != NULL) {
      if (typed_node->obj_ptr.epoch < min_epoch) {
        free(typed_node->obj_ptr.obj_data);
        typed_node->obj_ptr.obj_data = NULL;
        __sync_fetch_and_sub(&acc_bytes,
                             typed_node->obj_size & COMPACTION_MASK_OFF);
      }
    }
    if (node->next != NULL) cache_flush_chain(node->next);
  }
}

void *dic_cache_flush_thr(void *arg) {
  pthread_mutex_lock(&flush_lock);
  while (1) {
    pthread_cond_wait(&epc_flush_start, &flush_lock);
    if (process_ended) {
#ifdef DEBUG
      printf("Process ended - epc flush thread ended \n");
#endif
      return NULL;
    }
#ifdef STATISTICS
    stats_measure_start(CACHE_CLEANUP);
#endif
    struct dictionary *dic = (struct dictionary *)arg;
    for (int i = 0; i < dic->length; i++) {
      if (dic->table[i]) {
        while (!__sync_bool_compare_and_swap(&dic->_bucket_rw_lock[i], 0,
                                             INT64_MIN)) {
          for (volatile int i = 0; i < 1000; i++)
            ;
        }
        cache_flush_chain(dic->table[i]);
        __atomic_store_n(&dic->_bucket_rw_lock[i], 0, __ATOMIC_SEQ_CST);
      }
    }
#ifdef STATISTICS
    stats_measure_end(CACHE_CLEANUP);
#endif
    __atomic_store_n(&flushing, IDLE, __ATOMIC_SEQ_CST);
  }
  // we still own the mutex. remember to release it on exit
  pthread_mutex_unlock(&flush_lock);

  return NULL;
}

void dic_cache_flush(struct dictionary *dic) {
#ifdef STATISTICS
  stats_measure_start(CACHE_CLEANUP);
#endif
  if (acc_bytes > FLUSH_THRES) {
    if (!__sync_bool_compare_and_swap(&flushing, IDLE, FLUSH)) {
      return;
    }
    pthread_cond_signal(&epc_flush_start);
  }
#ifdef STATISTICS
  stats_measure_end(CACHE_CLEANUP);
#endif
}

void dic_force_cache_flush(struct dictionary *dic) {
  for (int i = 0; i < dic->length; i++) {
    if (dic->table[i]) {
      while (!__sync_bool_compare_and_swap(&dic->_bucket_rw_lock[i], 0,
                                           INT64_MIN)) {
        for (volatile int i = 0; i < 1000; i++)
          ;
      }
      cache_flush_chain(dic->table[i]);
      __atomic_store_n(&dic->_bucket_rw_lock[i], 0, __ATOMIC_SEQ_CST);
    }
  }
  __atomic_store_n(&acc_bytes, 0, __ATOMIC_SEQ_CST);
}

void dic_reinsert_when_resizing(struct dictionary *dic, struct keynode *k2) {
  int n = hash_func((const char *)k2->key) % dic->length;
  if (dic->table[n] == 0) {
    dic->table[n] = k2;
    return;
  }
  struct keynode *k = dic->table[n];
  k2->next = k;
  dic->table[n] = k2;
}

void dic_resize(struct dictionary *dic, int newsize) {
#ifdef DEBUG
  printf("hashmap resize triggered\n");
#endif
  pthread_rwlock_wrlock(&dic->_global_rw_lock);

  int o = dic->length;
  struct keynode **old = dic->table;
  dic->table = calloc(sizeof(struct keynode *), newsize);

  // destroy old locks and init the new for the extra buckets
  free(dic->_bucket_rw_lock);

  dic->_bucket_rw_lock = (int64_t *)calloc(newsize, sizeof(int64_t));

  dic->length = newsize;
  for (int i = 0; i < o; i++) {
    struct keynode *k = old[i];
    while (k) {
      struct keynode *next = k->next;
      k->next = 0;
      dic_reinsert_when_resizing(dic, k);
      k = next;
    }
  }
  free(old);

  pthread_rwlock_unlock(&dic->_global_rw_lock);
}

void *dic_add(struct dictionary *dic, void *key, void *value) {
  int n = hash_func((const char *)key) % dic->length;
  while (
      !__sync_bool_compare_and_swap(&dic->_bucket_rw_lock[n], 0, INT64_MIN)) {
    for (volatile int i = 0; i < 1000; i++)
      ;
  }

  if (dic->table[n] == 0) {
    dic->table[n] = keynode_new((char *)key);
    dic->count++;
    dic->table[n]->value = value;

    __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
    return NULL;
  }
  struct keynode *k = dic->table[n];
  while (k) {
    if (memcmp(k->key, key, KEY_SIZE) == 0) {
      /* do not invalidate last data if not explicitly stated */
      struct epc_entry *typed_old_node = (struct epc_entry *)(k->value);
      struct epc_entry *typed_new_node = (struct epc_entry *)(value);
      if ((typed_old_node->obj_size & COMPACTION_MASK_OFF) !=
          (typed_new_node->obj_size & COMPACTION_MASK_OFF)) {
        typed_old_node->obj_size = typed_new_node->obj_size;
        free(typed_old_node->obj_ptr.obj_data);
        typed_old_node->obj_ptr.obj_data = NULL;
      }
      memcpy(typed_old_node->obj_sign, typed_new_node->obj_sign,
             sizeof(typed_new_node->obj_sign));
      free(typed_new_node);
      __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
      return NULL;
    }
    k = k->next;
  }
  dic->count++;
  struct keynode *k2 = keynode_new((char *)key);
  k2->next = dic->table[n];
  dic->table[n] = k2;
  k2->value = value;

  __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
  return k2->next->value;
}

void *dic_find(struct dictionary *dic, void *key, void **value) {
  int n = hash_func((const char *)key) % dic->length;
  while (__sync_fetch_and_add(&dic->_bucket_rw_lock[n], 1) < 0) {
    for (volatile int i = 0; i < 1000; i++)
      ;
  };
  __builtin_prefetch(dic->table[n]);
  struct keynode *k = dic->table[n];

  if (!k) {
    __sync_fetch_and_sub(&dic->_bucket_rw_lock[n], 1);
    return NULL;
  }

  while (k) {
    if (!memcmp(k->key, key, KEY_SIZE)) {
      *value = k->value;
      struct epc_entry *typed_node = (struct epc_entry *)k->value;
      typed_node->obj_ptr.epoch = __sync_fetch_and_add(&curr_epoch, 1);
      __sync_fetch_and_sub(&dic->_bucket_rw_lock[n], 1);
      return k->key;
    }
    k = k->next;
  }
  __sync_fetch_and_sub(&dic->_bucket_rw_lock[n], 1);

  return NULL;
}

void *dic_find_lock_inc(struct dictionary *dic, void *key, void **value) {
  int n = hash_func((const char *)key) % dic->length;
  while (__sync_fetch_and_add(&dic->_bucket_rw_lock[n], 1) < 0) {
    for (volatile int i = 0; i < 1000; i++)
      ;
  };
  __builtin_prefetch(dic->table[n]);
  struct keynode *k = dic->table[n];

  if (!k) {
    __sync_fetch_and_sub(&dic->_bucket_rw_lock[n], 1);
    return NULL;
  }
  while (k) {
    if (!memcmp(k->key, key, KEY_SIZE)) {
      *value = k->value;
      __sync_fetch_and_sub(&dic->_bucket_rw_lock[n], 1);
      return k->key;
    }
    k = k->next;
  }
  __sync_fetch_and_sub(&dic->_bucket_rw_lock[n], 1);

  return NULL;
}

void *dic_find_with_position_update(struct dictionary *dic, void *key,
                                    void **value) {
  int n = hash_func((const char *)key) % dic->length;

  while (
      !__sync_bool_compare_and_swap(&dic->_bucket_rw_lock[n], 0, INT64_MIN)) {
    for (volatile int i = 0; i < 1000; i++)
      ;
  }

  __builtin_prefetch(dic->table[n]);
  struct keynode *k = dic->table[n];
  struct keynode *prev = dic->table[n];

  if (!k) {
    __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
    return 0;
  }
  while (k) {
    if (!memcmp(k->key, key, KEY_SIZE)) {
      *value = k->value;
      if (k != dic->table[n]) {
        prev->next = k->next;
        k->next = dic->table[n];
        dic->table[n] = k;
      }
      __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
      return k->key;
    }
    prev = k;
    k = k->next;
  }
  __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
  return NULL;
}

void *dic_find_fetch_front(struct dictionary *dic, void *key, void **value) {
  int n = hash_func((const char *)key) % dic->length;

  while (
      !__sync_bool_compare_and_swap(&dic->_bucket_rw_lock[n], 0, INT64_MIN)) {
    for (volatile int i = 0; i < 1000; i++)
      ;
  }

  __builtin_prefetch(dic->table[n]);
  struct keynode *k = dic->table[n];
  struct keynode *prev __attribute__((unused)) = dic->table[n];
  void *ret = NULL;

  if (!k) {
    __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
    return ret;
  }
  while (k) {
    if (!memcmp(k->key, key, KEY_SIZE)) {
      *value = k->value;

      if (k != dic->table[n]) {  // fetch front
        prev->next = k->next;
        k->next = dic->table[n];
        dic->table[n] = k;
        ret = k->next->value;
      }

      __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
      return ret;
    }
    prev = k;
    k = k->next;
  }
  __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
  return ret;
}

int dic_delete_node(struct dictionary *dic, void *key) {
  int n = hash_func((const char *)key) % dic->length;

  while (
      !__sync_bool_compare_and_swap(&dic->_bucket_rw_lock[n], 0, INT64_MIN)) {
    for (volatile int i = 0; i < 1000; i++)
      ;
  }

  __builtin_prefetch(dic->table[n]);
  struct keynode *k = dic->table[n];
  struct keynode *k_prev = dic->table[n];

  if (!k) {
    __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
    return 0;
  }

  while (k) {
    if (!memcmp(k->key, key, KEY_SIZE)) {
      if (k == dic->table[n]) {
        dic->table[n] = k->next != NULL ? k->next : NULL;
      }
      keynode_delete(dic, k, k_prev);
      dic->count--;
      __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
      return 1;
    }
    k_prev = k;
    k = k->next;
  }

  __atomic_store_n(&dic->_bucket_rw_lock[n], 0, __ATOMIC_SEQ_CST);
  return 0;
}

void keynode_forEach(struct keynode *node, dicFunc f) {
  struct epc_entry *typed_node = (struct epc_entry *)(node->value);
  if (typed_node != NULL) {
    f(node->key, typed_node);
  }
  if (node->next) keynode_forEach(node->next, f);
}

void dic_forEach(struct dictionary *dic, dicFunc f) {
  for (int i = 0; i < dic->length; i++) {
    if (dic->table[i]) {
      while (!__sync_bool_compare_and_swap(&dic->_bucket_rw_lock[i], 0,
                                           INT64_MIN)) {
        for (volatile int i = 0; i < 1000; i++)
          ;
      }
      keynode_forEach(dic->table[i], f);
      __atomic_store_n(&dic->_bucket_rw_lock[i], 0, __ATOMIC_SEQ_CST);
    }
  }
}

#undef hash_func
