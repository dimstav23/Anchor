#ifndef HASHDICTC
#define HASHDICTC
#include <pthread.h> /* lock */
#include <stdint.h>  /* uint32_t */
#include <stdlib.h>  /* malloc/calloc */
#include <string.h>  /* memcpy/memcmp */
#include <assert.h>

struct epc_entry;
typedef int (*enumFunc)(void *key, int count, uint8_t *value, void *user);
typedef void (*dicFunc)(char *key, struct epc_entry *entry);

#define HASHDICT_VALUE_TYPE uint8_t *
#define KEY_LENGTH_TYPE uint8_t
#define KEY_SIZE 8  // 16

struct keynode {
  struct keynode *next;
  char key[KEY_SIZE];
  // KEY_LENGTH_TYPE len;
  HASHDICT_VALUE_TYPE value;
};

struct dictionary {
  struct keynode **table;
  pthread_rwlock_t _global_rw_lock;
  // pthread_rwlock_t *_bucket_rw_lock;
  int64_t *_bucket_rw_lock;
  int length, count;
  uint64_t pool_id;  // pool id that this hashmap refers to
  double growth_treshold;
  double growth_factor;
  HASHDICT_VALUE_TYPE *value; /* redundant after concurrency introduced */
};

struct dictionary *dic_new(int initial_size);
void dic_delete(struct dictionary *dic);
void *dic_add(struct dictionary *dic, void *key, void *data);
int dic_delete_node(struct dictionary *dic, void *key);
void *dic_find(struct dictionary *dic, void *key, void **value);
void *dic_find_lock_inc(struct dictionary *dic, void *key, void **value);
void *dic_find_with_position_update(struct dictionary *dic, void *key,
                                    void **value);
// void dic_forEach(struct dictionary *dic, enumFunc f, void *user);
void dic_forEach(struct dictionary *dic, dicFunc f);
void dic_cache_flush(struct dictionary *dic);
void dic_force_cache_flush(struct dictionary *dic);

uint64_t get_curr_epoch();
void set_min_epoch();
void update_tx_epoch(uint64_t tx_lane_id, uint64_t new_epoch);
uint64_t get_min_epoch();

void *dic_find_fetch_front(struct dictionary *dic, void *key, void **value);
#endif