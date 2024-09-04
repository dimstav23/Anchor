/*
 * Copyright 2015-2019, Intel Corporation
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
 * lane.c -- lane implementation
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef ANCHOR_FUNCS
#include "manifest_operations.h"
extern struct temp_list_head
    temp_undo_list[OBJ_NLANES_MANIFEST];  // works for 1 pool, for more pools it
                                          // has to be converted to a hashmap
extern struct temp_list_head
    temp_redo_list[OBJ_NLANES_MANIFEST];  // works for 1 pool, for more pools it
                                          // has to be converted to a hashmap
#endif

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>

#include "critnib.h"
#include "lane.h"
#include "libpmemobj.h"
#include "memops.h"
#include "obj.h"
#include "os_thread.h"
#include "out.h"
#include "palloc.h"
#include "tx.h"
#include "util.h"
#include "valgrind_internal.h"

static os_tls_key_t Lane_info_key;

static __thread struct critnib *Lane_info_ht;
static __thread struct lane_info *Lane_info_records;
static __thread struct lane_info *Lane_info_cache;

/*
 * lane_info_create -- (internal) constructor for thread shared data
 */
static inline void lane_info_create(void) {
  Lane_info_ht = critnib_new();
  if (Lane_info_ht == NULL) FATAL("critnib_new");
}

/*
 * lane_info_delete -- (internal) deletes lane info hash table
 */
static inline void lane_info_delete(void) {
  if (unlikely(Lane_info_ht == NULL)) return;

  critnib_delete(Lane_info_ht);
  struct lane_info *record;
  struct lane_info *head = Lane_info_records;
  while (head != NULL) {
    record = head;
    head = head->next;
    Free(record);
  }

  Lane_info_ht = NULL;
  Lane_info_records = NULL;
  Lane_info_cache = NULL;
}

/*
 * lane_info_ht_boot -- (internal) boot lane info and add it to thread shared
 *	data
 */
static inline void lane_info_ht_boot(void) {
  lane_info_create();
  int result = os_tls_set(Lane_info_key, Lane_info_ht);
  if (result != 0) {
    errno = result;
    FATAL("!os_tls_set");
  }
}

/*
 * lane_info_ht_destroy -- (internal) destructor for thread shared data
 */
static inline void lane_info_ht_destroy(void *ht) { lane_info_delete(); }

/*
 * lane_info_boot -- initialize lane info hash table and lane info key
 */
void lane_info_boot(void) {
  int result = os_tls_key_create(&Lane_info_key, lane_info_ht_destroy);
  if (result != 0) {
    errno = result;
    FATAL("!os_tls_key_create");
  }
}

/*
 * lane_info_destroy -- destroy lane info hash table
 */
void lane_info_destroy(void) {
  lane_info_delete();
  (void)os_tls_key_delete(Lane_info_key);
}

/*
 * lane_info_cleanup -- remove lane info record regarding pool being deleted
 */
static inline void lane_info_cleanup(PMEMobjpool *pop) {
  if (unlikely(Lane_info_ht == NULL)) return;

  struct lane_info *info = critnib_remove(Lane_info_ht, pop->uuid_lo);
  if (likely(info != NULL)) {
    if (info->prev) info->prev->next = info->next;

    if (info->next) info->next->prev = info->prev;

    if (Lane_info_cache == info) Lane_info_cache = NULL;

    if (Lane_info_records == info) Lane_info_records = info->next;

    Free(info);
  }
}

/*
 * lane_get_layout -- (internal) calculates the real pointer of the lane layout
 */
static struct lane_layout *lane_get_layout(PMEMobjpool *pop,
                                           uint64_t lane_idx) {
  return (void *)((char *)pop + pop->lanes_offset +
                  sizeof(struct lane_layout) * lane_idx);
}

/*
 * lane_ulog_constructor -- (internal) constructor of a ulog extension
 */
static int lane_ulog_constructor(void *base, void *ptr, size_t usable_size,
                                 void *arg) {
  PMEMobjpool *pop = base;
  const struct pmem_ops *p_ops = &pop->p_ops;

  size_t capacity =
      ALIGN_DOWN(usable_size - sizeof(struct ulog), CACHELINE_SIZE);

  uint64_t gen_num = *(uint64_t *)arg;
  ulog_construct(OBJ_PTR_TO_OFF(base, ptr), capacity, gen_num, 1, 0, p_ops);

  return 0;
}
#ifdef ANCHOR_FUNCS
/*
 * lane_undo_extend -- allocates a new undo log
 */
static int lane_undo_extend(void *base, uint64_t *redo, uint64_t gen_num,
                            struct ulog *ulog_v, int is_first_log,
                            uint64_t tx_lane_id) {
  PMEMobjpool *pop = base;
  struct tx_parameters *params = pop->tx_params;
  size_t s = SIZEOF_ALIGNED_ULOG(params->cache_size);

  return pmalloc_construct(base, redo, s, lane_ulog_constructor, &gen_num, 0,
                           OBJ_INTERNAL_OBJECT_MASK, 0);
}

/*
 * lane_redo_extend -- allocates a new redo log
 */
static int lane_redo_extend(void *base, uint64_t *redo, uint64_t gen_num,
                            struct ulog *ulog_v, int is_first_log,
                            uint64_t tx_lane_id) {
  size_t s = SIZEOF_ALIGNED_ULOG(LANE_REDO_EXTERNAL_SIZE);

  return pmalloc_construct(base, redo, s, lane_ulog_constructor, &gen_num, 0,
                           OBJ_INTERNAL_OBJECT_MASK, 0);
}
#else
/*
 * lane_undo_extend -- allocates a new undo log
 */
static int lane_undo_extend(void *base, uint64_t *redo, uint64_t gen_num) {
  PMEMobjpool *pop = base;
  struct tx_parameters *params = pop->tx_params;
  size_t s = SIZEOF_ALIGNED_ULOG(params->cache_size);

  return pmalloc_construct(base, redo, s, lane_ulog_constructor, &gen_num, 0,
                           OBJ_INTERNAL_OBJECT_MASK, 0);
}

/*
 * lane_redo_extend -- allocates a new redo log
 */
static int lane_redo_extend(void *base, uint64_t *redo, uint64_t gen_num) {
  size_t s = SIZEOF_ALIGNED_ULOG(LANE_REDO_EXTERNAL_SIZE);

  return pmalloc_construct(base, redo, s, lane_ulog_constructor, &gen_num, 0,
                           OBJ_INTERNAL_OBJECT_MASK, 0);
}
#endif
/*
 * lane_init -- (internal) initializes a single lane runtime variables
 */
static int lane_init(PMEMobjpool *pop, struct lane *lane,
                     struct lane_layout *layout) {
  ASSERTne(lane, NULL);

  lane->layout = layout;

  lane->internal =
      operation_new((struct ulog *)&layout->internal, LANE_REDO_INTERNAL_SIZE,
                    NULL, NULL, &pop->p_ops, LOG_TYPE_REDO);
  if (lane->internal == NULL) goto error_internal_new;

  lane->external = operation_new(
      (struct ulog *)&layout->external, LANE_REDO_EXTERNAL_SIZE,
      lane_redo_extend, (ulog_free_fn)pfree, &pop->p_ops, LOG_TYPE_REDO);
  if (lane->external == NULL) goto error_external_new;

  lane->undo = operation_new((struct ulog *)&layout->undo, LANE_UNDO_SIZE,
                             lane_undo_extend, (ulog_free_fn)pfree, &pop->p_ops,
                             LOG_TYPE_UNDO);
  if (lane->undo == NULL) goto error_undo_new;

  return 0;

error_undo_new:
  operation_delete(lane->external);
error_external_new:
  operation_delete(lane->internal);
error_internal_new:
  return -1;
}

/*
 * lane_destroy -- cleanups a single lane runtime variables
 */
static void lane_destroy(PMEMobjpool *pop, struct lane *lane) {
  operation_delete(lane->undo);
  operation_delete(lane->internal);
  operation_delete(lane->external);
}

/*
 * lane_boot -- initializes all lanes
 */
int lane_boot(PMEMobjpool *pop) {
  int err = 0;

  pop->lanes_desc.lane = Malloc(sizeof(struct lane) * pop->nlanes);
  if (pop->lanes_desc.lane == NULL) {
    err = ENOMEM;
    ERR("!Malloc of volatile lanes");
    goto error_lanes_malloc;
  }

  pop->lanes_desc.next_lane_idx = 0;

  pop->lanes_desc.lane_locks =
      Zalloc(sizeof(*pop->lanes_desc.lane_locks) * pop->nlanes);
  if (pop->lanes_desc.lane_locks == NULL) {
    ERR("!Malloc for lane locks");
    goto error_locks_malloc;
  }

  /* add lanes to pmemcheck ignored list */
  VALGRIND_ADD_TO_GLOBAL_TX_IGNORE((char *)pop + pop->lanes_offset,
                                   (sizeof(struct lane_layout) * pop->nlanes));

  uint64_t i;
  for (i = 0; i < pop->nlanes; ++i) {
    struct lane_layout *layout = lane_get_layout(pop, i);

    if ((err = lane_init(pop, &pop->lanes_desc.lane[i], layout))) {
      ERR("!lane_init");
      goto error_lane_init;
    }
  }

  return 0;

error_lane_init:
  for (; i >= 1; --i) lane_destroy(pop, &pop->lanes_desc.lane[i - 1]);
  Free(pop->lanes_desc.lane_locks);
  pop->lanes_desc.lane_locks = NULL;
error_locks_malloc:
  Free(pop->lanes_desc.lane);
  pop->lanes_desc.lane = NULL;
error_lanes_malloc:
  return err;
}

/*
 * lane_init_data -- initializes ulogs for all the lanes
 */
void lane_init_data(PMEMobjpool *pop) {
  struct lane_layout *layout;

  for (uint64_t i = 0; i < pop->nlanes; ++i) {
    layout = lane_get_layout(pop, i);
    // HERE INJECTION FOR COUNTERS when a pool is created!
    ulog_construct(OBJ_PTR_TO_OFF(pop, &layout->internal),
                   LANE_REDO_INTERNAL_SIZE, 0, 0, 0, &pop->p_ops);
    ulog_construct(OBJ_PTR_TO_OFF(pop, &layout->external),
                   LANE_REDO_EXTERNAL_SIZE, 0, 0, 0, &pop->p_ops);
    ulog_construct(OBJ_PTR_TO_OFF(pop, &layout->undo), LANE_UNDO_SIZE, 0, 0, 0,
                   &pop->p_ops);
  }
  layout = lane_get_layout(pop, 0);
  LOG(15, "Here we can print pop header content");
  pmemops_xpersist(&pop->p_ops, layout,
                   pop->nlanes * sizeof(struct lane_layout), PMEMOBJ_F_RELAXED);
}

/*
 * lane_cleanup -- destroys all lanes
 */
void lane_cleanup(PMEMobjpool *pop) {
  LOG(20, NULL);
  for (uint64_t i = 0; i < pop->nlanes; ++i) {
    lane_destroy(pop, &pop->lanes_desc.lane[i]);
  }
  Free(pop->lanes_desc.lane);
  pop->lanes_desc.lane = NULL;
  Free(pop->lanes_desc.lane_locks);
  pop->lanes_desc.lane_locks = NULL;

  lane_info_cleanup(pop);
}

/*
 * lane_recover_and_section_boot -- performs initialization and recovery of all
 * lanes
 */
int lane_recover_and_section_boot(PMEMobjpool *pop) {
  COMPILE_ERROR_ON(SIZEOF_ULOG(LANE_UNDO_SIZE) +
                       SIZEOF_ULOG(LANE_REDO_EXTERNAL_SIZE) +
                       SIZEOF_ULOG(LANE_REDO_INTERNAL_SIZE) !=
                   LANE_TOTAL_SIZE);

  int err = 0;
  uint64_t i; /* lane index */
  struct lane_layout *layout;

  /*
   * First we need to recover the internal/external redo logs so that the
   * allocator state is consistent before we boot it.
   */
  for (i = 0; i < pop->nlanes; ++i) {
    layout = lane_get_layout(pop, i);

    ulog_recover((struct ulog *)&layout->internal, OBJ_OFF_IS_VALID_FROM_CTX,
                 &pop->p_ops);
    ulog_recover((struct ulog *)&layout->external, OBJ_OFF_IS_VALID_FROM_CTX,
                 &pop->p_ops);
  }

  if ((err = pmalloc_boot(pop)) != 0) return err;

  /*
   * Undo logs must be processed after the heap is initialized since
   * a undo recovery might require deallocation of the next ulogs.
   */
  for (i = 0; i < pop->nlanes; ++i) {
    struct operation_context *ctx = pop->lanes_desc.lane[i].undo;
    operation_resume(ctx);
    operation_process(ctx);
    operation_finish(ctx, ULOG_INC_FIRST_GEN_NUM | ULOG_FREE_AFTER_FIRST);
  }

  return 0;
}

/*
 * lane_section_cleanup -- performs runtime cleanup of all lanes
 */
int lane_section_cleanup(PMEMobjpool *pop) { return pmalloc_cleanup(pop); }

/*
 * lane_check -- performs check of all lanes
 */
int lane_check(PMEMobjpool *pop) {
  int err = 0;
  uint64_t j; /* lane index */
  struct lane_layout *layout;
  LOG(11, NULL);
  for (j = 0; j < pop->nlanes; ++j) {
    layout = lane_get_layout(pop, j);
    if (ulog_check((struct ulog *)&layout->internal, OBJ_OFF_IS_VALID_FROM_CTX,
                   &pop->p_ops) != 0) {
      LOG(2, "lane %" PRIu64 " internal redo failed: %d", j, err);
      return err;
    }
  }

  return 0;
}

/*
 * get_lane -- (internal) get free lane index
 */
static inline void get_lane(uint64_t *locks, struct lane_info *info,
                            uint64_t nlocks) {
  info->lane_idx = info->primary;
  while (1) {
    do {
      info->lane_idx %= nlocks;
      if (likely(util_bool_compare_and_swap64(&locks[info->lane_idx], 0, 1))) {
        if (info->lane_idx == info->primary) {
          info->primary_attempts = LANE_PRIMARY_ATTEMPTS;
        } else if (info->primary_attempts == 0) {
          info->primary = info->lane_idx;
          info->primary_attempts = LANE_PRIMARY_ATTEMPTS;
        }
        return;
      }

      if (info->lane_idx == info->primary && info->primary_attempts > 0) {
        info->primary_attempts--;
      }

      ++info->lane_idx;
    } while (info->lane_idx < nlocks);

    sched_yield();
  }
}

/*
 * get_lane_info_record -- (internal) get lane record attached to memory pool
 *	or first free
 */
static inline struct lane_info *get_lane_info_record(PMEMobjpool *pop) {
  if (likely(Lane_info_cache != NULL &&
             Lane_info_cache->pop_uuid_lo == pop->uuid_lo)) {
    return Lane_info_cache;
  }

  if (unlikely(Lane_info_ht == NULL)) {
    lane_info_ht_boot();
  }

  struct lane_info *info = critnib_get(Lane_info_ht, pop->uuid_lo);

  if (unlikely(info == NULL)) {
    info = Malloc(sizeof(struct lane_info));
    if (unlikely(info == NULL)) {
      FATAL("Malloc");
    }
    info->pop_uuid_lo = pop->uuid_lo;
    info->lane_idx = UINT64_MAX;
    info->nest_count = 0;
    info->next = Lane_info_records;
    info->prev = NULL;
    info->primary = 0;
    info->primary_attempts = LANE_PRIMARY_ATTEMPTS;
    if (Lane_info_records) {
      Lane_info_records->prev = info;
    }
    Lane_info_records = info;

    if (unlikely(critnib_insert(Lane_info_ht, pop->uuid_lo, info) != 0)) {
      FATAL("critnib_insert");
    }
  }

  Lane_info_cache = info;
  return info;
}

#ifdef ANCHOR_FUNCS
/*
 * lane_hold -- grabs a per-thread lane in a round-robin fashion
 */
unsigned lane_hold(PMEMobjpool *pop, struct lane **lanep) {
  /*
   * Before runtime lane initialization all remote operations are
   * executed using RLANE_DEFAULT.
   */
  if (unlikely(!pop->lanes_desc.runtime_nlanes)) {
    ASSERT(pop->has_remote_replicas);
    if (lanep != NULL) FATAL("cannot obtain section before lane's init");
    return RLANE_DEFAULT;
  }

  struct lane_info *lane = get_lane_info_record(pop);
  while (unlikely(lane->lane_idx == UINT64_MAX)) {
    /* initial wrap to next CL */
    lane->primary = lane->lane_idx =
        util_fetch_and_add32(&pop->lanes_desc.next_lane_idx, LANE_JUMP);
  } /* handles wraparound */

  uint64_t *llocks = pop->lanes_desc.lane_locks;
  /* grab next free lane from lanes available at runtime */
  if (!lane->nest_count++) {
    get_lane(llocks, lane, pop->lanes_desc.runtime_nlanes);
  }

  struct lane *l = &pop->lanes_desc.lane[lane->lane_idx];

  append_tx_info_entry(pop->uuid_lo, lane->lane_idx, TX_START);
  /* reinitialize lane's content only if in outermost hold */
  if (lanep && lane->nest_count == 1) {
    VALGRIND_ANNOTATE_NEW_MEMORY(l, sizeof(*l));
    VALGRIND_ANNOTATE_NEW_MEMORY(l->layout, sizeof(*l->layout));

    /* clobber existing logs like in operation finish*/
    clobber_existing_log(l->external);
    operation_finish_free_logs(l->external, 0);
    operation_finish_free_logs(l->undo, 0);

    operation_counter_init(l->internal, lane->lane_idx, INTERNAL_LOG_IDX);
    operation_counter_init(l->external, lane->lane_idx, EXTERNAL_LOG_IDX);
    operation_counter_init(l->undo, lane->lane_idx, UNDO_LOG_IDX);
    operation_tx_lane_id_init(l->internal, lane->lane_idx);
    operation_tx_lane_id_init(l->external, lane->lane_idx);
    operation_tx_lane_id_init(l->undo, lane->lane_idx);
    operation_init(l->external);
    operation_init(l->internal);
    operation_init(l->undo);
  }

  if (lanep) *lanep = l;

  LOG(10, "Lane %ld is held", lane->lane_idx);
  return (unsigned)lane->lane_idx;
}
#else
/*
 * lane_hold -- grabs a per-thread lane in a round-robin fashion
 */
unsigned lane_hold(PMEMobjpool *pop, struct lane **lanep) {
  /*
   * Before runtime lane initialization all remote operations are
   * executed using RLANE_DEFAULT.
   */
  if (unlikely(!pop->lanes_desc.runtime_nlanes)) {
    ASSERT(pop->has_remote_replicas);
    if (lanep != NULL) FATAL("cannot obtain section before lane's init");
    return RLANE_DEFAULT;
  }

  struct lane_info *lane = get_lane_info_record(pop);
  while (unlikely(lane->lane_idx == UINT64_MAX)) {
    /* initial wrap to next CL */
    lane->primary = lane->lane_idx =
        util_fetch_and_add32(&pop->lanes_desc.next_lane_idx, LANE_JUMP);
  } /* handles wraparound */

  uint64_t *llocks = pop->lanes_desc.lane_locks;
  /* grab next free lane from lanes available at runtime */
  if (!lane->nest_count++) {
    get_lane(llocks, lane, pop->lanes_desc.runtime_nlanes);
  }

  struct lane *l = &pop->lanes_desc.lane[lane->lane_idx];

  /* reinitialize lane's content only if in outermost hold */
  if (lanep && lane->nest_count == 1) {
    VALGRIND_ANNOTATE_NEW_MEMORY(l, sizeof(*l));
    VALGRIND_ANNOTATE_NEW_MEMORY(l->layout, sizeof(*l->layout));
    operation_init(l->external);
    operation_init(l->internal);
    operation_init(l->undo);
  }

  if (lanep) *lanep = l;

  LOG(10, "Lane %ld is held", lane->lane_idx);
  return (unsigned)lane->lane_idx;
}
#endif

/*
 * lane_release -- drops the per-thread lane
 */
void lane_release(PMEMobjpool *pop) {
  if (unlikely(!pop->lanes_desc.runtime_nlanes)) {
    ASSERT(pop->has_remote_replicas);
    return;
  }

  struct lane_info *lane = get_lane_info_record(pop);

  ASSERTne(lane, NULL);
  ASSERTne(lane->lane_idx, UINT64_MAX);

  if (unlikely(lane->nest_count == 0)) {
    FATAL("lane_release");
  } else if (--(lane->nest_count) == 0) {
    if (unlikely(!util_bool_compare_and_swap64(
            &pop->lanes_desc.lane_locks[lane->lane_idx], 1, 0))) {
      FATAL("util_bool_compare_and_swap64");
    }
  }
}

#ifdef ANCHOR_FUNCS
/*
 * ANCHOR Functions
 */

/*
 * sec_lane_ulog_constructor -- (internal) constructor of a ulog extension
 */
static int sec_lane_ulog_constructor(void *base, void *ptr, size_t usable_size,
                                     void *arg) {
  PMEMobjpool *pop = base;
  const struct pmem_ops *p_ops = &pop->p_ops;

  size_t capacity =
      ALIGN_DOWN(usable_size - sizeof(struct ulog), CACHELINE_SIZE);

  uint64_t gen_num = (*(struct log_extend_args *)arg).gen_num;
  uint64_t tx_lane_id = (*(struct log_extend_args *)arg).tx_lane_id;

  sec_ulog_construct(OBJ_PTR_TO_OFF(pop->addr, ptr), capacity, gen_num, 1, 0,
                     p_ops, tx_lane_id);

  return 0;
}

/*
 * sec_lane_redo_extend -- allocates a new redo log
 */
static int sec_lane_redo_extend(void *base, uint64_t *redo, uint64_t gen_num,
                                struct ulog *ulog_v, int is_first_log,
                                uint64_t tx_lane_id) {
  size_t s = SIZEOF_ALIGNED_ULOG(LANE_REDO_EXTERNAL_SIZE);

  struct log_extend_args arg = {gen_num, tx_lane_id};

  return sec_pmalloc_construct(base, redo, s, sec_lane_ulog_constructor, &arg,
                               0, OBJ_INTERNAL_OBJECT_MASK, 0, ulog_v,
                               is_first_log);
}

/*
 * sec_lane_undo_extend -- allocates a new undo log
 */
static int sec_lane_undo_extend(void *base, uint64_t *redo, uint64_t gen_num,
                                struct ulog *ulog_v, int is_first_log,
                                uint64_t tx_lane_id) {
  PMEMobjpool *pop = base;
  struct tx_parameters *params = pop->tx_params;
  size_t s = SIZEOF_ALIGNED_ULOG(params->cache_size);

  struct log_extend_args arg = {gen_num, tx_lane_id};

  return sec_pmalloc_construct(base, redo, s, sec_lane_ulog_constructor, &arg,
                               0, OBJ_INTERNAL_OBJECT_MASK, 0, ulog_v,
                               is_first_log);
}

/*
 * sec_lane_init -- (internal) initializes a single lane runtime variables
 */
static int sec_lane_init(PMEMobjpool *pop, struct lane *lane,
                         struct lane_layout *layout, uint64_t tx_lane_id) {
  ASSERTne(lane, NULL);
  lane->layout = layout;

  lane->internal =
      operation_new((struct ulog *)&layout->internal, LANE_REDO_INTERNAL_SIZE,
                    NULL, NULL, &pop->p_ops, LOG_TYPE_INTERNAL);
  if (lane->internal == NULL) goto error_internal_new;

  lane->external =
      operation_new((struct ulog *)&layout->external, LANE_REDO_EXTERNAL_SIZE,
                    sec_lane_redo_extend, (ulog_free_fn)sec_pfree, &pop->p_ops,
                    LOG_TYPE_REDO);
  if (lane->external == NULL) goto error_external_new;

  lane->undo = operation_new((struct ulog *)&layout->undo, LANE_UNDO_SIZE,
                             sec_lane_undo_extend, (ulog_free_fn)sec_pfree,
                             &pop->p_ops, LOG_TYPE_UNDO);
  if (lane->undo == NULL) goto error_undo_new;

  return 0;

error_undo_new:
  operation_delete(lane->external);
error_external_new:
  operation_delete(lane->internal);
error_internal_new:
  return -1;
}

/*
 * sec_lane_get_layout -- (internal) calculates the real pointer of the lane
 * layout
 */
static struct lane_layout *sec_lane_get_layout(PMEMobjpool *pop, void *pop_addr,
                                               uint64_t lane_idx) {
  return (void *)((char *)pop_addr + pop->lanes_offset +
                  sizeof(struct lane_layout) * lane_idx);
}

/*
 * sec_lane_check -- performs check of all lanes
 */
int sec_lane_check(PMEMobjpool *pop) {
  int err = 0;
  uint64_t j; /* lane index */
  struct lane_layout *layout;
  LOG(11, NULL);
  for (j = 0; j < pop->nlanes; ++j) {
    layout = sec_lane_get_layout(pop, pop->addr, j);
    if (sec_ulog_check((struct ulog *)&layout->internal,
                       OBJ_OFF_IS_VALID_FROM_CTX, &pop->p_ops) != 0) {
      LOG(2, "lane %" PRIu64 " internal redo failed: %d", j, err);
      return err;
    }
  }

  return 0;
}

/*
 * sec_apply_atomic_snapshots -- apply snapshots of unfinished operations
 * that PMDK considered atomic but for ANCHOR are not
 */
int sec_apply_atomic_snapshots(PMEMobjpool *pop) {
  LOG(11, NULL);
  apply_atomic_snapshots(pop->uuid_lo, (uintptr_t)pop->addr);
  return 0;
}

/*
 * sec_lane_ulog_header_validate -- performs check of all lanes to validate the
 * correct ulog header entry in case of a crash
 */
int sec_lane_ulog_header_validate(PMEMobjpool *pop) {
  LOG(11, NULL);
  for (uint64_t j = 0; j < pop->nlanes; ++j) {
    ulog_header_validate(&pop->p_ops, j);
  }
  return 0;
}

/*
 * sec_lane_recover_and_section_boot -- performs initialization and recovery of
 * all lanes
 */
int sec_lane_recover_and_section_boot(PMEMobjpool *pop) {
  COMPILE_ERROR_ON(SIZEOF_ULOG(LANE_UNDO_SIZE) +
                       SIZEOF_ULOG(LANE_REDO_EXTERNAL_SIZE) +
                       SIZEOF_ULOG(LANE_REDO_INTERNAL_SIZE) !=
                   LANE_TOTAL_SIZE);
#ifdef DEBUG
  printf("int %lld ext %lld undo %lld\n", LANE_REDO_INTERNAL_SIZE,
         LANE_REDO_EXTERNAL_SIZE, LANE_UNDO_SIZE);
#endif
  int err = 0;
  struct lane_layout *layout;
  int *unfinished_tx = get_unfinished_tx();

  int cnt = 0;

  if (unfinished_tx != NULL) {
    for (uint64_t lane = 0; lane < pop->nlanes; lane++) {
      if (unfinished_tx[lane] == TX_STARTED) {
        discard_from_temp_list(&temp_redo_list[lane], pop->uuid_lo);
        discard_from_temp_list(&temp_undo_list[lane], pop->uuid_lo);
      } else if (unfinished_tx[lane] == TX_COMMITED) {
        // here we have to recover either redo or undo log
        printf("ongoing tx on lane %ld\n", lane);
        layout = sec_lane_get_layout(pop, pop->addr, (uint64_t)lane);

        cnt = sec_ulog_recover((struct ulog *)&layout->external,
                               OBJ_OFF_IS_VALID_FROM_CTX, &pop->p_ops, lane,
                               EXTERNAL_LOG_IDX, TX_FINISH);

        if (cnt) {
          add_temp_list_to_epc(&temp_redo_list[lane], pop->uuid_lo);
          add_temp_list_to_epc(&temp_undo_list[lane], pop->uuid_lo);
          unfinished_tx[lane] = TX_IDLE;
        }
      }
    }
  }

  if ((err = sec_pmalloc_boot(pop)) != 0) return err;

  if (unfinished_tx != NULL) {
    for (uint64_t lane = 0; lane < pop->nlanes; lane++) {
      if (unfinished_tx[lane] == TX_COMMITED) {
        struct operation_context *ctx = pop->lanes_desc.lane[lane].undo;
        if (operation_counter_load(
                ctx, lane, UNDO_LOG_IDX)) {  // if I do not have the respective
                                             // counters -> do not apply undo
          // ANCHOR: recovery starts for tx_lane_id = i -- append entry for
          // recovery start
          operation_resume(ctx);
          // operation_process(ctx);
          sec_operation_process(ctx);
          operation_finish(ctx, ULOG_INC_FIRST_GEN_NUM | ULOG_FREE_AFTER_FIRST);
          // ANCHOR: recovery ends for tx_lane_id = i -- append entry for
          // recovery end
        }
        cnt = append_tx_info_entry(pop->uuid_lo, lane, TX_ABORT);
        discard_from_temp_list(&temp_redo_list[lane], pop->uuid_lo);
        discard_from_temp_list(&temp_undo_list[lane], pop->uuid_lo);
      }
    }
    free_unfinished_tx();
  }

#ifdef DEBUG
  if (cnt != 0)
    write_counters_mmap(
        "/dev/shm/amcs");  // manual writing -- need to wait for stability
#else
  if (cnt != 0) {
    while (query_counter(MANIFEST_END_COUNTER_IDX, (uint64_t)cnt)) {
      for (volatile int i = 0; i < 1000; i++)
        ;
    }
  }
#endif
  return 0;
}
/*
 * sec_lane_boot -- initializes all lanes
 */
int sec_lane_boot(PMEMobjpool *pop) {
  int err = 0;

  pop->lanes_desc.lane = Malloc(sizeof(struct lane) * pop->nlanes);
  if (pop->lanes_desc.lane == NULL) {
    err = ENOMEM;
    ERR("!Malloc of volatile lanes");
    goto error_lanes_malloc;
  }

  pop->lanes_desc.next_lane_idx = 0;

  pop->lanes_desc.lane_locks =
      Zalloc(sizeof(*pop->lanes_desc.lane_locks) * pop->nlanes);
  if (pop->lanes_desc.lane_locks == NULL) {
    ERR("!Malloc for lane locks");
    goto error_locks_malloc;
  }

  /* add lanes to pmemcheck ignored list */
  VALGRIND_ADD_TO_GLOBAL_TX_IGNORE((char *)pop + pop->lanes_offset,
                                   (sizeof(struct lane_layout) * pop->nlanes));

  uint64_t i;
  for (i = 0; i < pop->nlanes; ++i) {
    struct lane_layout *layout = sec_lane_get_layout(pop, pop->addr, i);
    if ((err = sec_lane_init(pop, &pop->lanes_desc.lane[i], layout, i))) {
      ERR("!lane_init");
      goto error_lane_init;
    }
  }

  return 0;

error_lane_init:
  for (; i >= 1; --i) lane_destroy(pop, &pop->lanes_desc.lane[i - 1]);
  Free(pop->lanes_desc.lane_locks);
  pop->lanes_desc.lane_locks = NULL;
error_locks_malloc:
  Free(pop->lanes_desc.lane);
  pop->lanes_desc.lane = NULL;
error_lanes_malloc:
  return err;
}

/*
 *  sec_lane_init_log -- re-initializes specific lane's log
 */
void sec_lane_init_log(PMEMobjpool *pop, uint64_t lane) {
  struct lane_layout *layout = sec_lane_get_layout(pop, pop->addr, lane);

  sec_ulog_construct(OBJ_PTR_TO_OFF(pop->addr, &layout->internal),
                     LANE_REDO_INTERNAL_SIZE, 0, 0, 0, &pop->p_ops, lane);
  sec_ulog_construct(OBJ_PTR_TO_OFF(pop->addr, &layout->external),
                     LANE_REDO_EXTERNAL_SIZE, 0, 0, 0, &pop->p_ops, lane);
  sec_ulog_construct(OBJ_PTR_TO_OFF(pop->addr, &layout->undo), LANE_UNDO_SIZE,
                     0, 0, 0, &pop->p_ops, lane);

  pmemops_xpersist(&pop->p_ops, layout, sizeof(struct lane_layout),
                   PMEMOBJ_F_RELAXED);
}

/*
 * sec_lane_init_data -- initializes ulogs for all the lanes
 */
void sec_lane_init_data(PMEMobjpool *pop) {
  struct lane_layout *layout;
  for (uint64_t i = 0; i < pop->nlanes; ++i) {
    layout = sec_lane_get_layout(pop, pop->addr, i);
    // HERE INJECTION FOR COUNTERS when a pool is created!
    sec_ulog_construct(OBJ_PTR_TO_OFF(pop->addr, &layout->internal),
                       LANE_REDO_INTERNAL_SIZE, 0, 0, 0, &pop->p_ops,
                       OBJ_NLANES);
    sec_ulog_construct(OBJ_PTR_TO_OFF(pop->addr, &layout->external),
                       LANE_REDO_EXTERNAL_SIZE, 0, 0, 0, &pop->p_ops,
                       OBJ_NLANES);
    sec_ulog_construct(OBJ_PTR_TO_OFF(pop->addr, &layout->undo), LANE_UNDO_SIZE,
                       0, 0, 0, &pop->p_ops, OBJ_NLANES);
  }
  layout = sec_lane_get_layout(pop, pop->addr, 0);
  LOG(15, "Here we can print pop header content");

  pmemops_xpersist(&pop->p_ops, layout,
                   pop->nlanes * sizeof(struct lane_layout), PMEMOBJ_F_RELAXED);
}
#endif