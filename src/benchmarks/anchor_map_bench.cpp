/*
 * Copyright 2015-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * anchor_map_bench.cpp -- benchmarks for: ctree, btree, rtree, rbtree,
 * hashmap_tx and skiplist from examples.
 */
#include <cassert>

#include "benchmark.hpp"
#include "file.h"
#include "libpmemobj.h"
#include "os.h"
#include "os_thread.h"
#include "poolset_util.hpp"

#include "map.h"
#include "map_btree.h"
#include "map_btree_opt.h"
#include "map_ctree.h"
#include "map_ctree_opt.h"
#include "map_hashmap_tx.h"
#include "map_hashmap_tx_opt.h"
#include "map_rbtree.h"
#include "map_rbtree_opt.h"
#include "map_rtree.h"
#include "map_rtree_opt.h"
#include "map_skiplist.h"
#include "map_skiplist_opt.h"
#include "mmap.h"

#include "./workload/generate_keys.h"
#include "./workload/generate_traces.h"
#include "timers/crystal_clock.h"
#include "timers/rdtsc.h"

#include <unistd.h>
#include <iostream>
using namespace anchor_tr_;
#ifdef STATISTICS
extern "C" {
#include "internal_statistics.h"
}
extern const char *cat_str[];
extern const char *cycles_str[];
extern int sampling_rate;
extern uint64_t sampling_delay;
#endif

#include "trusted_counter.h"
#define LOOP_DELAY 40000000

/* Values less than 3 is not suitable for current rtree implementation */
#define FACTOR 3
#define ALLOC_OVERHEAD 64

#define WORKLOAD_PATH_SIZE 1024
int tx_count = 0;
double time_thres = 0.7;
int tx_batch_size = 15000;

TOID_DECLARE_ROOT(struct root);

struct root {
  PMEMoid map;
};

#define OBJ_TYPE_NUM 1

#define swap(a, b)            \
  do {                        \
    __typeof__(a) _tmp = (a); \
    (a) = (b);                \
    (b) = _tmp;               \
  } while (0)

/* Values less than 2048 is not suitable for current rtree implementation */
#define SIZE_PER_KEY 2048

static const struct {
  const char *str;
  const struct map_ops *ops;
} map_types[] = {
    {"ctree_non_opt", MAP_CTREE},           {"ctree", MAP_CTREE_OPT},
    {"btree_non_opt", MAP_BTREE},           {"btree", MAP_BTREE_OPT},
    {"rtree_non_opt", MAP_RTREE},           {"rtree", MAP_RTREE_OPT},
    {"rbtree_non_opt", MAP_RBTREE},         {"rbtree", MAP_RBTREE_OPT},
    {"skiplist_non_opt", MAP_SKIPLIST},     {"skiplist", MAP_SKIPLIST_OPT},
    {"hashmap_tx_non_opt", MAP_HASHMAP_TX}, {"hashmap_tx", MAP_HASHMAP_TX_OPT}};

#define MAP_TYPES_NUM (sizeof(map_types) / sizeof(map_types[0]))

struct map_bench_args {
  unsigned seed;
  uint64_t max_key;
  char *type;
  bool ext_tx;
  bool alloc;
  uint64_t value_size; /* size of the value */
  uint64_t key_number; /* number of keys */
  int read_ratio;      /* read ration of the workload */
  int zipf_exp;        /* zipf exponential of the workload */
};

struct map_bench_worker {
  uint64_t *keys;
  size_t nkeys; /* for custom wl nkeys = number of ops */
  Trace_cmd::operation *op;
};

struct map_bench {
  struct map_ctx *mapc;
  os_mutex_t lock;

  PMEMobjpool *pop;
  size_t pool_size;

  size_t nkeys;
  size_t init_nkeys;
  uint64_t *keys;
  struct benchmark_args *args;
  struct map_bench_args *margs;

  PMEMoid root;
  PMEMoid root_oid;
  PMEMoid map;

  int (*insert)(struct map_bench *, uint64_t);
  int (*remove)(struct map_bench *, uint64_t);
  int (*get)(struct map_bench *, uint64_t);
};

/*
 * mutex_lock_nofail -- locks mutex and aborts if locking failed
 */

static void mutex_lock_nofail(os_mutex_t *lock) {
  errno = os_mutex_lock(lock);
  if (errno) {
    perror("os_mutex_lock");
    abort();
  }
}

/*
 * mutex_unlock_nofail -- unlocks mutex and aborts if unlocking failed
 */

static void mutex_unlock_nofail(os_mutex_t *lock) {
  errno = os_mutex_unlock(lock);
  if (errno) {
    perror("os_mutex_unlock");
    abort();
  }
}

static void bench_tx_batch_start(PMEMobjpool *pop, size_t index) {
  if (tx_count == 0) {
    pmemobj_tx_begin(pop, NULL, TX_PARAM_NONE);
    tx_count++;
  } else {
    tx_count++;
  }
}

static void bench_tx_batch_end(size_t index, size_t n_ops) {
  if (tx_count == tx_batch_size || index == (n_ops - 1)) {
    pmemobj_tx_commit();
    pmemobj_tx_end();
    tx_count = 0;
  }
}

/*
 * get_key -- return 64-bit random key
 */
static uint64_t get_key(unsigned *seed, uint64_t max_key) {
  unsigned key_lo = os_rand_r(seed);
  unsigned key_hi = os_rand_r(seed);
  uint64_t key = (((uint64_t)key_hi) << 32) | ((uint64_t)key_lo);

  if (max_key) key = key % max_key;

  return key;
}

/*
 * parse_map_type -- parse type of map
 */
static const struct map_ops *parse_map_type(const char *str) {
  /* specifically for rtree
   * now it's overwritten
   */
  if (strcmp(str, "rtree") == 0) {
    time_thres = 0.6;
    tx_batch_size = 1000;
  }

  for (unsigned i = 0; i < MAP_TYPES_NUM; i++) {
    if (strcmp(str, map_types[i].str) == 0) return map_types[i].ops;
  }

  return nullptr;
}

/*
 * map_remove_free_op -- remove and free object from map
 */
static int map_remove_free_op(struct map_bench *map_bench, uint64_t key) {
  volatile int ret = 0;
  TX_BEGIN(map_bench->pop) {
    PMEMoid val = map_remove(map_bench->mapc, map_bench->map, key);
    if (OID_IS_NULL(val))
      ret = -1;
    else
      sobj_tx_free(val);
  }
  TX_ONABORT { ret = -1; }
  TX_END

  return ret;
}

/*
 * map_remove_custom_free_op -- remove and free object from map
 */
static int map_remove_custom_free_op(struct map_bench *map_bench,
                                     uint64_t key) {
  volatile int ret = 0;
  TX_BEGIN(map_bench->pop) {
    PMEMoid val = map_remove(map_bench->mapc, map_bench->map, key);
    if (OID_IS_NULL(val))
      ret = -1;
    else
      sobj_tx_free(val);
  }
  TX_ONABORT { ret = -1; }
  TX_END

  return ret;
}

/*
 * map_remove_root_op -- remove root object from map
 */
static int map_remove_root_op(struct map_bench *map_bench, uint64_t key) {
  PMEMoid val = map_remove(map_bench->mapc, map_bench->map, key);

  return !OID_EQUALS(val, map_bench->root_oid);
}

/*
 * map_remove_custom_root_op -- remove root object from map
 */
static int map_remove_custom_root_op(struct map_bench *map_bench,
                                     uint64_t key) {
  PMEMoid val = map_remove(map_bench->mapc, map_bench->map, key);

  if (OID_IS_NULL(val)) {  // case it's not found is OK
    return 0;
  } else {
    return !OID_EQUALS(val, map_bench->root_oid);
  }
}

/*
 * map_remove_op -- main operation for map_remove benchmark
 */
static int map_remove_op(struct benchmark *bench, struct operation_info *info) {
  auto *map_bench = (struct map_bench *)pmembench_get_priv(bench);
  auto *tworker = (struct map_bench_worker *)info->worker->priv;

  uint64_t key = tworker->keys[info->index];

  mutex_lock_nofail(&map_bench->lock);

  bench_tx_batch_start(map_bench->pop, info->index);

  int ret = map_bench->remove(map_bench, key);

  bench_tx_batch_end(info->index, info->args->n_ops_per_thread);

  mutex_unlock_nofail(&map_bench->lock);

  return ret;
}

/*
 * map_insert_alloc_op -- allocate an object and insert to map
 */
static int map_insert_alloc_op(struct map_bench *map_bench, uint64_t key) {
  int ret = 0;
  uint8_t *buffer = (uint8_t *)calloc(map_bench->args->dsize, sizeof(uint8_t));

  TX_BEGIN(map_bench->pop) {
    PMEMoid oid = sobj_tx_alloc(map_bench->args->dsize, OBJ_TYPE_NUM);
    sobj_tx_write(map_bench->pop, oid, buffer);

    ret = map_insert(map_bench->mapc, map_bench->map, key, oid);
  }
  TX_ONABORT { ret = -1; }
  TX_END

  free(buffer);
  return ret;
}

/*
 * map_insert_custom_alloc_op -- allocate an object and insert to map
 */
static int map_insert_custom_alloc_op(struct map_bench *map_bench,
                                      uint64_t key) {
  int ret = 0;
  PMEMoid oid = sobj_tx_alloc(map_bench->args->dsize, OBJ_TYPE_NUM);
  uint8_t *data = (uint8_t *)calloc(map_bench->args->dsize, sizeof(uint8_t));

  sobj_tx_write(map_bench->mapc->pop, oid, data);
  free(data);

  ret = map_insert(map_bench->mapc, map_bench->map, key, oid);

  return ret;
}

/*
 * map_insert_root_op -- insert root object to map
 */
static int map_insert_root_op(struct map_bench *map_bench, uint64_t key) {
  return map_insert(map_bench->mapc, map_bench->map, key, map_bench->root_oid);
}

/*
 * map_insert_custom_root_op -- insert root object to map
 */
static int map_insert_custom_root_op(struct map_bench *map_bench,
                                     uint64_t key) {
  return map_insert(map_bench->mapc, map_bench->map, key, map_bench->root_oid);
}

/*
 * map_insert_op -- main operation for map_insert benchmark
 */
static int map_insert_op(struct benchmark *bench, struct operation_info *info) {
  auto *map_bench = (struct map_bench *)pmembench_get_priv(bench);
  auto *tworker = (struct map_bench_worker *)info->worker->priv;
  uint64_t key = tworker->keys[info->index];

  mutex_lock_nofail(&map_bench->lock);

  bench_tx_batch_start(map_bench->pop, info->index);

  int ret = map_bench->insert(map_bench, key);

  bench_tx_batch_end(info->index, info->args->n_ops_per_thread);

  mutex_unlock_nofail(&map_bench->lock);

  return ret;
}

/*
 * map_get_obj_op -- get object from map at specified key
 */
static int map_get_obj_op(struct map_bench *map_bench, uint64_t key) {
  PMEMoid val = map_get(map_bench->mapc, map_bench->map, key);
  if (!OID_IS_NULL(val)) {
    uint8_t *buffer = (uint8_t *)sobj_tx_read(map_bench->pop, val);
    free(buffer);
  }
  return OID_IS_NULL(val);
}

/*
 * map_get_custom_op -- get object from map at specified key
 */
static int map_get_custom_obj_op(struct map_bench *map_bench, uint64_t key) {
  PMEMoid val = map_get(map_bench->mapc, map_bench->map, key);
  if (!OID_IS_NULL(val)) {
    void *value = sobj_tx_read(map_bench->mapc->pop, val);
    free(value);
  }

  return 0;
}

/*
 * map_get_root_op -- get root object from map at specified key
 */
static int map_get_root_op(struct map_bench *map_bench, uint64_t key) {
  PMEMoid val = map_get(map_bench->mapc, map_bench->map, key);

  return !OID_EQUALS(val, map_bench->root_oid);
}

/*
 * map_get_custom_root_op -- get root object from map at specified key
 */
static int map_get_custom_root_op(struct map_bench *map_bench, uint64_t key) {
  PMEMoid val = map_get(map_bench->mapc, map_bench->map, key);

  if (OID_IS_NULL(val)) {  // case it's not found is OK
    return 0;
  } else {
    return !OID_EQUALS(val, map_bench->root_oid);
  }
}

/*
 * map_get_op -- main operation for map_get benchmark
 */
static int map_get_op(struct benchmark *bench, struct operation_info *info) {
  auto *map_bench = (struct map_bench *)pmembench_get_priv(bench);
  auto *tworker = (struct map_bench_worker *)info->worker->priv;
  int ret = -1;
  uint64_t key = tworker->keys[info->index];

  mutex_lock_nofail(&map_bench->lock);

  ret = map_bench->get(map_bench, key);

  mutex_unlock_nofail(&map_bench->lock);

  return ret;
}

int op_cnt = 0;
uint64_t tx_start = 0;
/*
 * map_custom_op -- main operation for map_custom benchmark
 */
static int map_custom_op(struct benchmark *bench, struct operation_info *info) {
  auto *map_bench = (struct map_bench *)pmembench_get_priv(bench);
  auto *tworker = (struct map_bench_worker *)info->worker->priv;

  uint64_t key = tworker->keys[info->index];
  Trace_cmd::operation op = tworker->op[info->index];
  mutex_lock_nofail(&map_bench->lock);

  int ret = 1;

  if (tx_start == 0) {
    tx_start = 1;  // get_tsc();
    pmemobj_tx_begin(map_bench->pop, NULL, TX_PARAM_NONE);
  } else if (info->index % 500 == 0) {
    if (query_curr_delay() > time_thres * LOOP_DELAY) {
      pmemobj_tx_commit();
      pmemobj_tx_end();
      pmemobj_tx_begin(map_bench->pop, NULL, TX_PARAM_NONE);
    }
  }

  switch (op) {
    case Trace_cmd::Get:
      ret = map_bench->get(map_bench, key);
      break;
    case Trace_cmd::Put:
      ret = map_bench->insert(map_bench, key);
      break;
    default:
      break;
  }

  if (info->index == info->args->n_ops_per_thread - 1 && tx_start == 1) {
    pmemobj_tx_commit();
    pmemobj_tx_end();
    tx_start = 0;
  }

  mutex_unlock_nofail(&map_bench->lock);

  return ret;
}

/*
 * map_common_init_worker -- common init worker function for map_* benchmarks
 */
static int map_common_init_worker(struct benchmark *bench,
                                  struct benchmark_args *args,
                                  struct worker_info *worker) {
  struct map_bench_worker *tworker =
      (struct map_bench_worker *)calloc(1, sizeof(*tworker));
  struct map_bench *tree;
  struct map_bench_args *targs;

  if (!tworker) {
    perror("calloc");
    return -1;
  }

  tx_count = 0;
  tworker->nkeys = args->n_ops_per_thread;  // * args->n_threads;
  tworker->keys =
      (uint64_t *)mmap_helper(tworker->nkeys * sizeof(*tworker->keys));
  if (!tworker->keys) {
    perror("malloc");
    goto err_free_worker;
  }

  tree = (struct map_bench *)pmembench_get_priv(bench);
  targs = (struct map_bench_args *)args->opts;
  if (targs->ext_tx) {
    int ret = pmemobj_tx_begin(tree->pop, nullptr);
    if (ret) {
      (void)pmemobj_tx_end();
      goto err_free_keys;
    }
  }

  worker->priv = tworker;

  return 0;
err_free_keys:
  munmap_helper(tworker->keys, tworker->nkeys * sizeof(*tworker->keys));
err_free_worker:
  free(tworker);
  return -1;
}

/*
 * map_custom_init_worker -- custom init worker function for map_custom
 * benchmarks
 */
static int map_custom_init_worker(struct benchmark *bench,
                                  struct benchmark_args *args,
                                  struct worker_info *worker) {
  struct map_bench_worker *tworker =
      (struct map_bench_worker *)calloc(1, sizeof(*tworker));
  struct map_bench *tree;
  struct map_bench_args *targs;

  if (!tworker) {
    perror("calloc");
    return -1;
  }

  tworker->nkeys = args->n_ops_per_thread;
  tworker->keys =
      (uint64_t *)mmap_helper(tworker->nkeys * sizeof(*tworker->keys));
  if (!tworker->keys) {
    perror("malloc");
    goto err_free_worker;
  }
  tworker->op = (Trace_cmd::operation *)mmap_helper(tworker->nkeys *
                                                    sizeof(*tworker->op));
  if (!tworker->op) {
    perror("malloc");
    goto err_free_keys;
  }

  tree = (struct map_bench *)pmembench_get_priv(bench);
  targs = (struct map_bench_args *)args->opts;
  if (targs->ext_tx) {
    int ret = pmemobj_tx_begin(tree->pop, nullptr);
    if (ret) {
      (void)pmemobj_tx_end();
      goto err_free_ops;
    }
  }

  worker->priv = tworker;

  return 0;

err_free_ops:
  munmap_helper(tworker->op, tworker->nkeys * sizeof(*tworker->op));
err_free_keys:
  munmap_helper(tworker->keys, tworker->nkeys * sizeof(*tworker->keys));
err_free_worker:
  free(tworker);
  return -1;
}

/*
 * map_common_free_worker -- common cleanup worker function for map_*
 * benchmarks
 */
static void map_common_free_worker(struct benchmark *bench,
                                   struct benchmark_args *args,
                                   struct worker_info *worker) {
  auto *tworker = (struct map_bench_worker *)worker->priv;
  auto *targs = (struct map_bench_args *)args->opts;

  if (targs->ext_tx) {
    pmemobj_tx_commit();
    (void)pmemobj_tx_end();
  }

  munmap_helper(tworker->keys, tworker->nkeys * sizeof(*tworker->keys));
  free(tworker);
}

/*
 * map_custom_free_worker -- custom cleanup worker function for map_*
 * benchmarks
 */
static void map_custom_free_worker(struct benchmark *bench,
                                   struct benchmark_args *args,
                                   struct worker_info *worker) {
  auto *tworker = (struct map_bench_worker *)worker->priv;
  auto *targs = (struct map_bench_args *)args->opts;

  if (targs->ext_tx) {
    pmemobj_tx_commit();
    (void)pmemobj_tx_end();
  }
  munmap_helper(tworker->keys, tworker->nkeys * sizeof(*tworker->keys));
  munmap_helper(tworker->op, tworker->nkeys * sizeof(*tworker->op));
  free(tworker);
}

/*
 * map_insert_init_worker -- init worker function for map_insert benchmark
 */
static int map_insert_init_worker(struct benchmark *bench,
                                  struct benchmark_args *args,
                                  struct worker_info *worker) {
  int ret = map_common_init_worker(bench, args, worker);
  if (ret) return ret;

  auto *targs = (struct map_bench_args *)args->opts;
  assert(targs);
  auto *tworker = (struct map_bench_worker *)worker->priv;

  assert(tworker);

  for (size_t i = 0; i < tworker->nkeys; i++)
    tworker->keys[i] = get_key(&targs->seed, targs->max_key);

  return 0;
}

/*
 * map_global_rand_keys_init -- assign random keys from global keys array
 */
static int map_global_rand_keys_init(struct benchmark *bench,
                                     struct benchmark_args *args,
                                     struct worker_info *worker) {
  auto *tree = (struct map_bench *)pmembench_get_priv(bench);
  assert(tree);
  auto *targs = (struct map_bench_args *)args->opts;
  assert(targs);
  auto *tworker = (struct map_bench_worker *)worker->priv;

  assert(tworker);
  assert(tree->init_nkeys);

  /*
   * Assign random keys from global tree->keys array without repetitions.
   */
  for (size_t i = 0; i < tworker->nkeys; i++) {
    uint64_t index = get_key(&targs->seed, tree->init_nkeys);
    tworker->keys[i] = tree->keys[index];
    swap(tree->keys[index], tree->keys[tree->init_nkeys - 1]);
    tree->init_nkeys--;
  }

  return 0;
}

/*
 * map_custom_wl_trace_init -- custom workload init
 */
static std::vector<Trace_cmd> map_custom_wl_trace_init(char *wl_file) {
  std::string tr(wl_file);
  std::vector<Trace_cmd> trace = trace_init(tr);
  return trace;
}

/*
 * workload_file_construct -- construct the file name
 */
static char *workload_file_construct(const char *dir_path, uint64_t wl_size,
                                     struct map_bench_args *targs) {
  char cwd[WORKLOAD_PATH_SIZE / 2];
  if (getcwd(cwd, sizeof(cwd)) == NULL)
    std::cout << "Unable to get current directory to locate workload files\n";

  char *ret = (char *)malloc(WORKLOAD_PATH_SIZE * sizeof(char));
  snprintf(ret, WORKLOAD_PATH_SIZE,
           "%s/%s/simple_trace_w_%ld_k_%ld_a_%.2f_r_%.2f.txt", cwd, dir_path,
           wl_size, targs->key_number, float(targs->zipf_exp) / 100,
           float(targs->read_ratio) / 100);

  return ret;
}

/*
 * keys_file_construct -- construct the file name
 */
static char *keys_file_construct(const char *dir_path, uint64_t wl_size,
                                 struct map_bench_args *targs) {
  char cwd[WORKLOAD_PATH_SIZE / 2];
  if (getcwd(cwd, sizeof(cwd)) == NULL)
    std::cout
        << "Unable to get current directory to locate exported key files\n";

  char *ret = (char *)malloc(WORKLOAD_PATH_SIZE * sizeof(char));

  snprintf(ret, WORKLOAD_PATH_SIZE,
           "%s/%s/simple_trace_w_%ld_k_%ld_a_%.2f_r_%.2f_keys.txt", cwd,
           dir_path, wl_size, targs->key_number, float(targs->zipf_exp) / 100,
           float(targs->read_ratio) / 100);

  return ret;
}

/*
 * map_custom_wl_init -- assign workload keys from trace
 */
static int map_custom_wl_init(struct benchmark *bench,
                              struct benchmark_args *args,
                              struct worker_info *worker) {
  auto *tree = (struct map_bench *)pmembench_get_priv(bench);
  assert(tree);
  auto *targs = (struct map_bench_args *)args->opts;
  assert(targs);
  auto *tworker = (struct map_bench_worker *)worker->priv;

  assert(tworker);
  assert(tree->init_nkeys);

  uint64_t wl_size = args->n_ops_per_thread * args->n_threads;
  char *wl_file = workload_file_construct(args->wl_file_dir, wl_size, targs);
  std::vector<Trace_cmd> trace = map_custom_wl_trace_init(wl_file);
  free(wl_file);
  assert(args->n_ops_per_thread * args->n_threads == trace.size());

  /*
   * Assign random keys from global tree->keys array without repetitions.
   */
  size_t idx_off = (tworker->nkeys * args->n_threads) - tree->init_nkeys;
  for (size_t i = 0; i < tworker->nkeys; i++) {
    tworker->keys[i] = trace.at(i + idx_off).key_hash;
    tworker->op[i] = trace.at(i + idx_off).op;
    tree->init_nkeys--;
  }
  trace.clear();
  trace.shrink_to_fit();

  munmap_helper(tree->keys, tree->nkeys * sizeof(*tree->keys));

  return 0;
}

/*
 * map_remove_init_worker -- init worker function for map_remove benchmark
 */
static int map_remove_init_worker(struct benchmark *bench,
                                  struct benchmark_args *args,
                                  struct worker_info *worker) {
  int ret = map_common_init_worker(bench, args, worker);
  if (ret) return ret;

  ret = map_global_rand_keys_init(bench, args, worker);
  if (ret) goto err_common_free_worker;
  return 0;
err_common_free_worker:
  map_common_free_worker(bench, args, worker);
  return -1;
}

/*
 * map_bench_get_init_worker -- init worker function for map_get benchmark
 */
static int map_bench_get_init_worker(struct benchmark *bench,
                                     struct benchmark_args *args,
                                     struct worker_info *worker) {
  int ret = map_common_init_worker(bench, args, worker);
  if (ret) return ret;

  ret = map_global_rand_keys_init(bench, args, worker);
  if (ret) goto err_common_free_worker;
  return 0;
err_common_free_worker:
  map_common_free_worker(bench, args, worker);
  return -1;
}

/*
 * map_bench_custom_init_worker -- init worker function for map_custom benchmark
 */
static int map_bench_custom_init_worker(struct benchmark *bench,
                                        struct benchmark_args *args,
                                        struct worker_info *worker) {
  int ret = map_custom_init_worker(bench, args, worker);
  if (ret) return ret;

  ret = map_custom_wl_init(bench, args, worker);
  if (ret) goto err_custom_free_worker;
  return 0;
err_custom_free_worker:
  map_custom_free_worker(bench, args, worker);
  return -1;
}

/*
 * map_common_init -- common init function for map_* benchmarks
 */
static int map_common_init(struct benchmark *bench,
                           struct benchmark_args *args) {
  assert(bench);
  assert(args);
  assert(args->opts);

  tx_count = 0;

  char path[PATH_MAX];
  if (util_safe_strcpy(path, args->fname, sizeof(path)) != 0) return -1;

  /* A 128 bit key */
  uint8_t *key = (uint8_t *)"012345678901234";
  size_t key_len = 16;
  /* A 128 bit IV */
  uint8_t *iv = (uint8_t *)"012345678901234";
  size_t iv_len = 16;

  struct root *temp_root = NULL;

  char manifest_path[PATH_MAX];
  if (util_safe_strcpy(manifest_path, args->manifest_fname,
                       sizeof(manifest_path)) != 0)
    return -1;
  char counters_path[PATH_MAX];
  if (util_safe_strcpy(counters_path, args->counters_fname,
                       sizeof(counters_path)) != 0)
    return -1;
  char metadata_log_path[PATH_MAX];
  if (util_safe_strcpy(metadata_log_path, args->metadata_log_fname,
                       sizeof(metadata_log_path)) != 0)
    return -1;

  enum file_type type = util_file_get_type(args->fname);
  if (type == OTHER_ERROR) {
    fprintf(stderr, "could not check type of file %s\n", args->fname);
    return -1;
  }

  size_t size_per_key;
  struct map_bench *map_bench =
      (struct map_bench *)calloc(1, sizeof(*map_bench));

  if (!map_bench) {
    perror("calloc");
    return -1;
  }

  map_bench->args = args;
  map_bench->margs = (struct map_bench_args *)args->opts;

  const struct map_ops *ops = parse_map_type(map_bench->margs->type);
  if (!ops) {
    fprintf(stderr, "invalid map type value specified -- '%s'\n",
            map_bench->margs->type);
    goto err_free_bench;
  }

  if (map_bench->margs->ext_tx && args->n_threads > 1) {
    fprintf(stderr, "external transaction requires single thread\n");
    goto err_free_bench;
  }

  if (map_bench->margs->alloc) {
    map_bench->insert = map_insert_alloc_op;
    map_bench->remove = map_remove_free_op;
    map_bench->get = map_get_obj_op;
  } else {
    map_bench->insert = map_insert_root_op;
    map_bench->remove = map_remove_root_op;
    map_bench->get = map_get_root_op;
  }

  map_bench->nkeys = args->n_threads * args->n_ops_per_thread;
  map_bench->init_nkeys = map_bench->nkeys;
  size_per_key = map_bench->margs->alloc
                     ? SIZE_PER_KEY + map_bench->args->dsize + ALLOC_OVERHEAD
                     : SIZE_PER_KEY;

  map_bench->pool_size = map_bench->nkeys * size_per_key * FACTOR * 20;

  if (args->is_poolset || type == TYPE_DEVDAX) {
    if (args->fsize < map_bench->pool_size) {
      fprintf(stderr, "file size too large\n");
      goto err_free_bench;
    }

    map_bench->pool_size = 0;
  } else if (map_bench->pool_size < 2 * PMEMOBJ_MIN_POOL) {
    map_bench->pool_size = 2 * PMEMOBJ_MIN_POOL;
  }

  if (args->is_dynamic_poolset) {
    int ret = dynamic_poolset_create(args->fname, map_bench->pool_size);
    if (ret == -1) goto err_free_bench;

    if (util_safe_strcpy(path, POOLSET_PATH, sizeof(path)) != 0)
      goto err_free_bench;

    map_bench->pool_size = 0;
  }

  map_bench->pop = spool_create(path, "map_bench", map_bench->pool_size,
                                args->fmode, manifest_path, counters_path,
                                metadata_log_path, key, key_len, iv, iv_len);

  if (!map_bench->pop) {
    fprintf(stderr, "spool_create: %s\n", pmemobj_errormsg());
    goto err_free_bench;
  }

  errno = os_mutex_init(&map_bench->lock);
  if (errno) {
    perror("os_mutex_init");
    goto err_close;
  }

  map_bench->mapc = map_ctx_init(ops, map_bench->pop);
  if (!map_bench->mapc) {
    perror("map_ctx_init");
    goto err_destroy_lock;
  }

  map_bench->root = sobj_root_get(map_bench->pop, sizeof(struct root));

  if (OID_IS_NULL(map_bench->root)) {
    fprintf(stderr, "sobj_root_get: %s\n", pmemobj_errormsg());
    goto err_free_map;
  }

  map_bench->root_oid = map_bench->root;

  temp_root =
      (struct root *)sobj_read(map_bench->pop, map_bench->root, 1, NULL);
  if (map_create(map_bench->mapc, &temp_root->map, nullptr)) {
    perror("map_new");
    free(temp_root);
    goto err_free_map;
  }
  map_bench->map = temp_root->map;

  pmembench_set_priv(bench, map_bench);
  free(temp_root);

  return 0;
err_free_map:
  map_ctx_free(map_bench->mapc);
err_destroy_lock:
  os_mutex_destroy(&map_bench->lock);
err_close:
  spool_close(map_bench->pop);
err_free_bench:
  free(map_bench);
  return -1;
}

/*
 * map_custom_init -- init function for custom map_* benchmarks
 */
static int map_custom_init(struct benchmark *bench,
                           struct benchmark_args *args) {
  assert(bench);
  assert(args);
  assert(args->opts);

  char path[PATH_MAX];
  if (util_safe_strcpy(path, args->fname, sizeof(path)) != 0) return -1;

  enum file_type type = util_file_get_type(args->fname);
  if (type == OTHER_ERROR) {
    fprintf(stderr, "could not check type of file %s\n", args->fname);
    return -1;
  }

  /* A 128 bit key */
  uint8_t *key = (uint8_t *)"012345678901234";
  size_t key_len = 16;
  /* A 128 bit IV */
  uint8_t *iv = (uint8_t *)"012345678901234";
  size_t iv_len = 16;

  struct root *temp_root = NULL;

  char manifest_path[PATH_MAX];
  if (util_safe_strcpy(manifest_path, args->manifest_fname,
                       sizeof(manifest_path)) != 0)
    return -1;
  char counters_path[PATH_MAX];
  if (util_safe_strcpy(counters_path, args->counters_fname,
                       sizeof(counters_path)) != 0)
    return -1;
  char metadata_log_path[PATH_MAX];
  if (util_safe_strcpy(metadata_log_path, args->metadata_log_fname,
                       sizeof(metadata_log_path)) != 0)
    return -1;

  size_t size_per_key;
  struct map_bench *map_bench =
      (struct map_bench *)calloc(1, sizeof(*map_bench));

  if (!map_bench) {
    perror("calloc");
    return -1;
  }

  map_bench->args = args;
  map_bench->margs = (struct map_bench_args *)args->opts;

  const struct map_ops *ops = parse_map_type(map_bench->margs->type);
  if (!ops) {
    fprintf(stderr, "invalid map type value specified -- '%s'\n",
            map_bench->margs->type);
    goto err_free_bench;
  }

  if (map_bench->margs->ext_tx && args->n_threads > 1) {
    fprintf(stderr, "external transaction requires single thread\n");
    goto err_free_bench;
  }

  if (map_bench->margs->alloc) {
    map_bench->insert = map_insert_custom_alloc_op;
    map_bench->remove = map_remove_custom_free_op;
    map_bench->get = map_get_custom_obj_op;
  } else {
    map_bench->insert = map_insert_custom_root_op;
    map_bench->remove = map_remove_custom_root_op;
    map_bench->get = map_get_custom_root_op;
  }

  map_bench->nkeys = map_bench->margs->key_number;
  map_bench->init_nkeys = args->n_threads * args->n_ops_per_thread;

  size_per_key = map_bench->margs->alloc
                     ? SIZE_PER_KEY + map_bench->args->dsize + ALLOC_OVERHEAD
                     : SIZE_PER_KEY;

  map_bench->pool_size = map_bench->nkeys * size_per_key * FACTOR * 20;

  if (args->is_poolset || type == TYPE_DEVDAX) {
    if (args->fsize < map_bench->pool_size) {
      fprintf(stderr, "file size too large\n");
      goto err_free_bench;
    }

    map_bench->pool_size = 0;
  } else if (map_bench->pool_size < 2 * PMEMOBJ_MIN_POOL) {
    map_bench->pool_size = 2 * PMEMOBJ_MIN_POOL;
  }

  if (args->is_dynamic_poolset) {
    int ret = dynamic_poolset_create(args->fname, map_bench->pool_size);
    if (ret == -1) goto err_free_bench;

    if (util_safe_strcpy(path, POOLSET_PATH, sizeof(path)) != 0)
      goto err_free_bench;

    map_bench->pool_size = 0;
  }

  map_bench->pop = spool_create(path, "map_bench", map_bench->pool_size,
                                args->fmode, manifest_path, counters_path,
                                metadata_log_path, key, key_len, iv, iv_len);

  if (!map_bench->pop) {
    fprintf(stderr, "spool_create: %s\n", pmemobj_errormsg());
    goto err_free_bench;
  }

  errno = os_mutex_init(&map_bench->lock);
  if (errno) {
    perror("os_mutex_init");
    goto err_close;
  }

  map_bench->mapc = map_ctx_init(ops, map_bench->pop);
  if (!map_bench->mapc) {
    perror("map_ctx_init");
    goto err_destroy_lock;
  }

  map_bench->root = sobj_root_get(map_bench->pop, sizeof(struct root));

  if (OID_IS_NULL(map_bench->root)) {
    fprintf(stderr, "sobj_root_get: %s\n", pmemobj_errormsg());
    goto err_free_map;
  }

  map_bench->root_oid = map_bench->root;
  temp_root =
      (struct root *)sobj_read(map_bench->pop, map_bench->root, 1, NULL);
  if (map_create(map_bench->mapc, &temp_root->map, nullptr)) {
    perror("map_new");
    free(temp_root);
    goto err_free_map;
  }
  map_bench->map = temp_root->map;

  pmembench_set_priv(bench, map_bench);
  free(temp_root);

  return 0;
err_free_map:
  map_ctx_free(map_bench->mapc);
err_destroy_lock:
  os_mutex_destroy(&map_bench->lock);
err_close:
  spool_close(map_bench->pop);
err_free_bench:
  free(map_bench);
  return -1;
}

/*
 * map_common_exit -- common cleanup function for map_* benchmarks
 */
static int map_common_exit(struct benchmark *bench,
                           struct benchmark_args *args) {
  auto *tree = (struct map_bench *)pmembench_get_priv(bench);

  os_mutex_destroy(&tree->lock);

  map_ctx_free(tree->mapc);
  spool_close(tree->pop);
  free(tree);
  return 0;
}

/*
 * map_custom_keys_exit -- cleanup of keys array
 */
static int map_custom_keys_exit(struct benchmark *bench,
                                struct benchmark_args *args) {
  auto *tree = (struct map_bench *)pmembench_get_priv(bench);
  munmap_helper(tree->keys, tree->nkeys * sizeof(*tree->keys));
  return 0;
}

/*
 * map_custom_exit -- exit function for map_custom benchmark
 */
static int map_custom_exit(struct benchmark *bench,
                           struct benchmark_args *args) {
  map_custom_keys_exit(bench, args);
  return map_common_exit(bench, args);
}

/*
 * map_keys_init -- initialize array with keys
 */
static int map_keys_init(struct benchmark *bench, struct benchmark_args *args) {
  auto *map_bench = (struct map_bench *)pmembench_get_priv(bench);
  assert(map_bench);
  auto *targs = (struct map_bench_args *)args->opts;
  assert(targs);

  assert(map_bench->nkeys != 0);
  map_bench->keys =
      (uint64_t *)mmap_helper(map_bench->nkeys * sizeof(*map_bench->keys));

  if (!map_bench->keys) {
    perror("malloc");
    return -1;
  }

  int ret = 0;

  mutex_lock_nofail(&map_bench->lock);

  for (size_t i = 0; i < map_bench->nkeys; i++) {
    bench_tx_batch_start(map_bench->pop, i);

    uint64_t key;
    PMEMoid oid;
    do {
      key = get_key(&targs->seed, targs->max_key);
      oid = map_get(map_bench->mapc, map_bench->map, key);
    } while (!OID_IS_NULL(oid));

    if (targs->alloc)
      oid = sobj_tx_alloc(args->dsize, OBJ_TYPE_NUM);
    else
      oid = map_bench->root_oid;

    ret = map_insert(map_bench->mapc, map_bench->map, key, oid);

    if (ret) break;
    map_bench->keys[i] = key;

    bench_tx_batch_end(i, map_bench->nkeys);
  }

  epc_force_cache_flush();
  tx_count = 0;

  mutex_unlock_nofail(&map_bench->lock);

  if (!ret) return 0;

  munmap_helper(map_bench->keys, map_bench->nkeys * sizeof(*map_bench->keys));

  return ret;
}

/*
 * map_custom_keys_init -- initialize KV with keys of custom workload
 */
static int map_custom_keys_init(struct benchmark *bench,
                                struct benchmark_args *args) {
  auto *map_bench = (struct map_bench *)pmembench_get_priv(bench);
  assert(map_bench);
  auto *targs = (struct map_bench_args *)args->opts;
  assert(targs);

  assert(map_bench->nkeys != 0);

  int ret = 0;

  uint64_t wl_size = args->n_ops_per_thread * args->n_threads;
  char *keys_file = keys_file_construct(args->wl_file_dir, wl_size, targs);

  std::string tr(keys_file);
  std::vector<Keys_cmd> keys = keys_init(tr);

  free(keys_file);
  assert(targs->key_number >= keys.size());

  mutex_lock_nofail(&map_bench->lock);
  for (size_t i = 0; i < keys.size(); i++) {
    bench_tx_batch_start(map_bench->pop, i);

    PMEMoid oid;
    if (targs->alloc)
      oid = sobj_tx_alloc(args->dsize, OBJ_TYPE_NUM);
    else
      oid = map_bench->root_oid;

    ret = map_insert(map_bench->mapc, map_bench->map, keys.at(i).key_hash, oid);

    bench_tx_batch_end(i, keys.size());

    if (ret) break;
  }

  keys.clear();
  keys.shrink_to_fit();

  epc_force_cache_flush();
  tx_count = 0;

  mutex_unlock_nofail(&map_bench->lock);

  if (!ret) return 0;

  return ret;
}

/*
 * map_keys_exit -- cleanup of keys array
 */
static int map_keys_exit(struct benchmark *bench, struct benchmark_args *args) {
  auto *tree = (struct map_bench *)pmembench_get_priv(bench);
  munmap_helper(tree->keys, tree->nkeys * sizeof(*tree->keys));
  return 0;
}

/*
 * map_remove_init -- init function for map_remove benchmark
 */
static int map_remove_init(struct benchmark *bench,
                           struct benchmark_args *args) {
  int ret = map_common_init(bench, args);
  if (ret) return ret;
  ret = map_keys_init(bench, args);
  if (ret) goto err_exit_common;

  return 0;
err_exit_common:
  map_common_exit(bench, args);
  return -1;
}

/*
 * map_remove_exit -- cleanup function for map_remove benchmark
 */
static int map_remove_exit(struct benchmark *bench,
                           struct benchmark_args *args) {
  map_keys_exit(bench, args);
  return map_common_exit(bench, args);
}

/*
 * map_bench_get_init -- init function for map_get benchmark
 */
static int map_bench_get_init(struct benchmark *bench,
                              struct benchmark_args *args) {
  int ret = map_common_init(bench, args);
  if (ret) return ret;
  ret = map_keys_init(bench, args);
  if (ret) goto err_exit_common;

  return 0;
err_exit_common:
  map_common_exit(bench, args);
  return -1;
}

/*
 * map_get_exit -- exit function for map_get benchmark
 */
static int map_get_exit(struct benchmark *bench, struct benchmark_args *args) {
  map_keys_exit(bench, args);
  return map_common_exit(bench, args);
}

/*
 * map_bench_custom_init -- init function for map_custom benchmark
 */
static int map_bench_custom_init(struct benchmark *bench,
                                 struct benchmark_args *args) {
  int ret = map_custom_init(bench, args);
  if (ret) return ret;

  ret = map_custom_keys_init(bench, args);
  if (ret) goto err_exit_common;

  return 0;

err_exit_common:
  map_custom_exit(bench, args);
  return -1;
}

static struct benchmark_clo map_bench_clos[9];

static struct benchmark_info map_insert_info;
static struct benchmark_info map_remove_info;
static struct benchmark_info map_get_info;
static struct benchmark_info map_custom_info;

CONSTRUCTOR(map_bench_constructor)
void map_bench_constructor(void) {
  map_bench_clos[0].opt_short = 'T';
  map_bench_clos[0].opt_long = "type";
  map_bench_clos[0].descr =
      "Type of container "
      "[ctree|ctree_opt|btree|rtree|rbtree|hashmap_tx|skiplist]";

  map_bench_clos[0].off = clo_field_offset(struct map_bench_args, type);
  map_bench_clos[0].type = CLO_TYPE_STR;
  map_bench_clos[0].def = "ctree";

  map_bench_clos[1].opt_short = 's';
  map_bench_clos[1].opt_long = "seed";
  map_bench_clos[1].descr = "PRNG seed";
  map_bench_clos[1].off = clo_field_offset(struct map_bench_args, seed);
  map_bench_clos[1].type = CLO_TYPE_UINT;
  map_bench_clos[1].def = "1";
  map_bench_clos[1].type_uint.size =
      clo_field_size(struct map_bench_args, seed);
  map_bench_clos[1].type_uint.base = CLO_INT_BASE_DEC;
  map_bench_clos[1].type_uint.min = 1;
  map_bench_clos[1].type_uint.max = UINT_MAX;

  map_bench_clos[2].opt_short = 'M';
  map_bench_clos[2].opt_long = "max-key";
  map_bench_clos[2].descr = "maximum key (0 means no limit)";
  map_bench_clos[2].off = clo_field_offset(struct map_bench_args, max_key);
  map_bench_clos[2].type = CLO_TYPE_UINT;
  map_bench_clos[2].def = "0";
  map_bench_clos[2].type_uint.size =
      clo_field_size(struct map_bench_args, max_key);
  map_bench_clos[2].type_uint.base = CLO_INT_BASE_DEC;
  map_bench_clos[2].type_uint.min = 0;
  map_bench_clos[2].type_uint.max = UINT64_MAX;

  map_bench_clos[3].opt_short = 'x';
  map_bench_clos[3].opt_long = "external-tx";
  map_bench_clos[3].descr =
      "Use external transaction for all "
      "operations (works with single "
      "thread only)";
  map_bench_clos[3].off = clo_field_offset(struct map_bench_args, ext_tx);
  map_bench_clos[3].type = CLO_TYPE_FLAG;

  map_bench_clos[4].opt_short = 'A';
  map_bench_clos[4].opt_long = "alloc";
  map_bench_clos[4].descr =
      "Allocate object of specified size "
      "when inserting";
  map_bench_clos[4].off = clo_field_offset(struct map_bench_args, alloc);
  map_bench_clos[4].type = CLO_TYPE_FLAG;

  map_bench_clos[5].opt_short = 'K';
  map_bench_clos[5].opt_long = "keys";
  map_bench_clos[5].descr = "Number of keys";
  map_bench_clos[5].off = clo_field_offset(struct map_bench_args, key_number);
  map_bench_clos[5].type = CLO_TYPE_UINT;
  map_bench_clos[5].def = "0";
  map_bench_clos[5].type_uint.size =
      clo_field_size(struct map_bench_args, key_number);
  map_bench_clos[5].type_uint.base = CLO_INT_BASE_DEC;
  map_bench_clos[5].type_uint.min = 0;
  map_bench_clos[5].type_uint.max = UINT64_MAX;

  map_bench_clos[6].opt_short = 'V';
  map_bench_clos[6].opt_long = "value-size";
  map_bench_clos[6].descr = "Value size";
  map_bench_clos[6].off = clo_field_offset(struct map_bench_args, value_size);
  map_bench_clos[6].type = CLO_TYPE_UINT;
  map_bench_clos[6].def = "0";
  map_bench_clos[6].type_uint.size =
      clo_field_size(struct map_bench_args, value_size);
  map_bench_clos[6].type_uint.base = CLO_INT_BASE_DEC;
  map_bench_clos[6].type_uint.min = 0;
  map_bench_clos[6].type_uint.max = UINT64_MAX;

  map_bench_clos[7].opt_short = 'R';
  map_bench_clos[7].opt_long = "read-ratio";
  map_bench_clos[7].type = CLO_TYPE_UINT;
  map_bench_clos[7].descr = "Read ratio";
  map_bench_clos[7].off = clo_field_offset(struct map_bench_args, read_ratio);
  map_bench_clos[7].def = "0";
  map_bench_clos[7].type_uint.size =
      clo_field_size(struct map_bench_args, read_ratio);
  map_bench_clos[7].type_uint.base = CLO_INT_BASE_DEC | CLO_INT_BASE_HEX;
  map_bench_clos[7].type_uint.min = 0;
  map_bench_clos[7].type_uint.max = 100;

  map_bench_clos[8].opt_short = 'z';
  map_bench_clos[8].opt_long = "zipf-exp";
  map_bench_clos[8].type = CLO_TYPE_UINT;
  map_bench_clos[8].descr = "Zipf exponent";
  map_bench_clos[8].off = clo_field_offset(struct map_bench_args, zipf_exp);
  map_bench_clos[8].def = "0";
  map_bench_clos[8].type_uint.size =
      clo_field_size(struct map_bench_args, zipf_exp);
  map_bench_clos[8].type_uint.base = CLO_INT_BASE_DEC | CLO_INT_BASE_HEX;
  map_bench_clos[8].type_uint.min = 0;
  map_bench_clos[8].type_uint.max = 100;

  map_insert_info.name = "map_insert";
  map_insert_info.brief = "Inserting to tree map";
  map_insert_info.init = map_common_init;
  map_insert_info.exit = map_common_exit;
  map_insert_info.multithread = true;
  map_insert_info.multiops = true;
  map_insert_info.init_worker = map_insert_init_worker;
  map_insert_info.free_worker = map_common_free_worker;
  map_insert_info.operation = map_insert_op;
  map_insert_info.measure_time = true;
  map_insert_info.clos = map_bench_clos;
  map_insert_info.nclos = ARRAY_SIZE(map_bench_clos);
  map_insert_info.opts_size = sizeof(struct map_bench_args);
  map_insert_info.rm_file = true;
  map_insert_info.allow_poolset = true;
  REGISTER_BENCHMARK(map_insert_info);

  map_remove_info.name = "map_remove";
  map_remove_info.brief = "Removing from tree map";
  map_remove_info.init = map_remove_init;
  map_remove_info.exit = map_remove_exit;
  map_remove_info.multithread = true;
  map_remove_info.multiops = true;
  map_remove_info.init_worker = map_remove_init_worker;
  map_remove_info.free_worker = map_common_free_worker;
  map_remove_info.operation = map_remove_op;
  map_remove_info.measure_time = true;
  map_remove_info.clos = map_bench_clos;
  map_remove_info.nclos = ARRAY_SIZE(map_bench_clos);
  map_remove_info.opts_size = sizeof(struct map_bench_args);
  map_remove_info.rm_file = true;
  map_remove_info.allow_poolset = true;
  REGISTER_BENCHMARK(map_remove_info);

  map_get_info.name = "map_get";
  map_get_info.brief = "Tree lookup";
  map_get_info.init = map_bench_get_init;
  map_get_info.exit = map_get_exit;
  map_get_info.multithread = true;
  map_get_info.multiops = true;
  map_get_info.init_worker = map_bench_get_init_worker;
  map_get_info.free_worker = map_common_free_worker;
  map_get_info.operation = map_get_op;
  map_get_info.measure_time = true;
  map_get_info.clos = map_bench_clos;
  map_get_info.nclos = ARRAY_SIZE(map_bench_clos);
  map_get_info.opts_size = sizeof(struct map_bench_args);
  map_get_info.rm_file = true;
  map_get_info.allow_poolset = true;
  REGISTER_BENCHMARK(map_get_info);

  map_custom_info.name = "map_custom";
  map_custom_info.brief = "Custom ops";
  map_custom_info.init = map_bench_custom_init;
  map_custom_info.exit = map_custom_exit;
  map_custom_info.multithread = true;
  map_custom_info.multiops = true;
  map_custom_info.init_worker = map_bench_custom_init_worker;
  map_custom_info.free_worker = map_custom_free_worker;
  map_custom_info.operation = map_custom_op;
  map_custom_info.measure_time = true;
  map_custom_info.clos = map_bench_clos;
  map_custom_info.nclos = ARRAY_SIZE(map_bench_clos);
  map_custom_info.opts_size = sizeof(struct map_bench_args);
  map_custom_info.rm_file = true;
  map_custom_info.allow_poolset = true;
  REGISTER_BENCHMARK(map_custom_info);
}
