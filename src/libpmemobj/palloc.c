/*
 * Copyright 2015-2020, Intel Corporation
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
 * palloc.c -- implementation of pmalloc POSIX-like API
 *
 * This is the front-end part of the persistent memory allocator. It uses both
 * transient and persistent representation of the heap to provide memory blocks
 * in a reasonable time and with an acceptable common-case fragmentation.
 *
 * Lock ordering in the entirety of the allocator is simple, but might be hard
 * to follow at times because locks are, by necessity, externalized.
 * There are two sets of locks that need to be taken into account:
 *	- runtime state locks, represented by buckets.
 *	- persistent state locks, represented by memory block mutexes.
 *
 * To properly use them, follow these rules:
 *	- When nesting, always lock runtime state first.
 *	Doing the reverse might cause deadlocks in other parts of the code.
 *
 *	- When introducing functions that would require runtime state locks,
 *	always try to move the lock acquiring to the upper most layer. This
 *	usually means that the functions will simply take "struct bucket" as
 *	their argument. By doing so most of the locking can happen in
 *	the frontend part of the allocator and it's easier to follow the first
 *	rule because all functions in the backend can safely use the persistent
 *	state locks - the runtime lock, if it is needed, will be already taken
 *	by the upper layer.
 *
 * General lock ordering:
 *	1. arenas.lock
 *	2. buckets (sorted by ID)
 *	3. memory blocks (sorted by lock address)
 */

#ifdef ANCHOR_FUNCS
#include "manifest_operations.h"
#include "obj.h"
#include "openssl_gcm_encrypt.h"
#include "user_operations.h"
#endif

#include "alloc_class.h"
#include "heap.h"
#include "heap_layout.h"
#include "out.h"
#include "palloc.h"
#include "ravl.h"
#include "sys_util.h"
#include "valgrind_internal.h"
#include "vec.h"

struct pobj_action_internal {
  /* type of operation (alloc/free vs set) */
  enum pobj_action_type type;

  /* not used */
  uint32_t padding;

  /*
   * Action-specific lock that needs to be taken for the duration of
   * an action.
   */
  os_mutex_t *lock;

  /* action-specific data */
  union {
    /* valid only when type == POBJ_ACTION_TYPE_HEAP */
    struct {
      uint64_t offset;
      enum memblock_state new_state;
      struct memory_block m;
      struct memory_block_reserved *mresv;
    };

    /* valid only when type == POBJ_ACTION_TYPE_MEM or
     * POBJ_ACTION_TYPE_ULOG_GEN_NUM*/
    struct {
      uint64_t *ptr;
      uint64_t value;
    };

    /* padding, not used */
    uint64_t data2[14];
  };
};

/*
 * palloc_set_value -- creates a new set memory action
 */
void palloc_set_value(struct palloc_heap *heap, struct pobj_action *act,
                      uint64_t *ptr, uint64_t value) {
  act->type = POBJ_ACTION_TYPE_MEM;

  struct pobj_action_internal *actp = (struct pobj_action_internal *)act;
  actp->ptr = ptr;
  actp->value = value;
  actp->lock = NULL;
}

/*
 * alloc_prep_block -- (internal) prepares a memory block for allocation
 *
 * Once the block is fully reserved and it's guaranteed that no one else will
 * be able to write to this memory region it is safe to write the allocation
 * header and call the object construction function.
 *
 * Because the memory block at this stage is only reserved in transient state
 * there's no need to worry about fail-safety of this method because in case
 * of a crash the memory will be back in the free blocks collection.
 */
static int alloc_prep_block(struct palloc_heap *heap,
                            const struct memory_block *m,
                            palloc_constr constructor, void *arg,
                            uint64_t extra_field, uint16_t object_flags,
                            uint64_t *offset_value) {
  void *uptr = m->m_ops->get_user_data(m);
  size_t usize = m->m_ops->get_user_size(m);

  VALGRIND_DO_MEMPOOL_ALLOC(heap->layout, uptr, usize);
  VALGRIND_DO_MAKE_MEM_UNDEFINED(uptr, usize);
  VALGRIND_ANNOTATE_NEW_MEMORY(uptr, usize);

  m->m_ops->write_header(m, extra_field, object_flags);

  /*
   * Set allocated memory with pattern, if debug.heap.alloc_pattern CTL
   * parameter had been set.
   */
  if (unlikely(heap->alloc_pattern > PALLOC_CTL_DEBUG_NO_PATTERN)) {
    pmemops_memset(&heap->p_ops, uptr, heap->alloc_pattern, usize, 0);
    VALGRIND_DO_MAKE_MEM_UNDEFINED(uptr, usize);
  }

  int ret;
  if (constructor != NULL &&
      (ret = constructor(heap->base, uptr, usize, arg)) != 0) {
    /*
     * If canceled, revert the block back to the free state in vg
     * machinery.
     */
    VALGRIND_DO_MEMPOOL_FREE(heap->layout, uptr);

    return ret;
  }

  /*
   * To avoid determining the user data pointer twice this method is also
   * responsible for calculating the offset of the object in the pool that
   * will be used to set the offset destination pointer provided by the
   * caller.
   */
  *offset_value = HEAP_PTR_TO_OFF(heap, uptr);

  return 0;
}

/*
 * palloc_reservation_create -- creates a volatile reservation of a
 *	memory block.
 *
 * The first step in the allocation of a new block is reserving it in
 * the transient heap - which is represented by the bucket abstraction.
 *
 * To provide optimal scaling for multi-threaded applications and reduce
 * fragmentation the appropriate bucket is chosen depending on the
 * current thread context and to which allocation class the requested
 * size falls into.
 *
 * Once the bucket is selected, just enough memory is reserved for the
 * requested size. The underlying block allocation algorithm
 * (best-fit, next-fit, ...) varies depending on the bucket container.
 */
static int palloc_reservation_create(struct palloc_heap *heap, size_t size,
                                     palloc_constr constructor, void *arg,
                                     uint64_t extra_field,
                                     uint16_t object_flags, uint16_t class_id,
                                     uint16_t arena_id,
                                     struct pobj_action_internal *out) {
  int err = 0;

  struct memory_block *new_block = &out->m;
  out->type = POBJ_ACTION_TYPE_HEAP;

  ASSERT(class_id < UINT8_MAX);
  struct alloc_class *c =
      class_id == 0
          ? heap_get_best_class(heap, size)
          : alloc_class_by_id(heap_alloc_classes(heap), (uint8_t)class_id);

  if (c == NULL) {
    ERR("no allocation class for size %lu bytes", size);
    errno = EINVAL;
    return -1;
  }

  /*
   * The caller provided size in bytes, but buckets operate in
   * 'size indexes' which are multiples of the block size in the
   * bucket.
   *
   * For example, to allocate 500 bytes from a bucket that
   * provides 256 byte blocks two memory 'units' are required.
   */
  ssize_t size_idx = alloc_class_calc_size_idx(c, size);
  if (size_idx < 0) {
    ERR("allocation class not suitable for size %lu bytes", size);
    errno = EINVAL;
    return -1;
  }
  ASSERT(size_idx <= UINT32_MAX);
  *new_block = MEMORY_BLOCK_NONE;
  new_block->size_idx = (uint32_t)size_idx;

  struct bucket *b = heap_bucket_acquire(heap, c->id, arena_id);

  err = heap_get_bestfit_block(heap, b, new_block);
  if (err != 0) goto out;

  if (alloc_prep_block(heap, new_block, constructor, arg, extra_field,
                       object_flags, &out->offset) != 0) {
    /*
     * Constructor returned non-zero value which means
     * the memory block reservation has to be rolled back.
     */
    LOG(11, NULL);
    if (new_block->type == MEMORY_BLOCK_HUGE) {
      bucket_insert_block(b, new_block);
    }
    err = ECANCELED;
    goto out;
  }

  /*
   * Each as of yet unfulfilled reservation needs to be tracked in the
   * runtime state.
   * The memory block cannot be put back into the global state unless
   * there are no active reservations.
   */
  if ((out->mresv = b->active_memory_block) != NULL)
    util_fetch_and_add64(&out->mresv->nresv, 1);

  out->lock = new_block->m_ops->get_lock(new_block);
  out->new_state = MEMBLOCK_ALLOCATED;

out:
  heap_bucket_release(heap, b);

  if (err == 0) return 0;

  errno = err;
  return -1;
}

/*
 * palloc_heap_action_exec -- executes a single heap action (alloc, free)
 */
static void palloc_heap_action_exec(struct palloc_heap *heap,
                                    const struct pobj_action_internal *act,
                                    struct operation_context *ctx) {
  /*
   * The actual required metadata modifications are chunk-type
   * dependent, but it always is a modification of a single 8 byte
   * value - either modification of few bits in a bitmap or
   * changing a chunk type from free to used or vice versa.
   */
  act->m.m_ops->prep_hdr(&act->m, act->new_state, ctx);
}

/*
 * palloc_restore_free_chunk_state -- updates the runtime state of a free chunk.
 *
 * This function also takes care of coalescing of huge chunks.
 */
static void palloc_restore_free_chunk_state(struct palloc_heap *heap,
                                            struct memory_block *m) {
  if (m->type == MEMORY_BLOCK_HUGE) {
    struct bucket *b = heap_bucket_acquire(heap, DEFAULT_ALLOC_CLASS_ID,
                                           HEAP_ARENA_PER_THREAD);
    if (heap_free_chunk_reuse(heap, b, m) != 0) {
      if (errno == EEXIST) {
        FATAL("duplicate runtime chunk state, possible double free");
      } else {
        LOG(2, "unable to track runtime chunk state");
      }
    }
    heap_bucket_release(heap, b);
  }
}

/*
 * palloc_mem_action_noop -- empty handler for unused memory action funcs
 */
static void palloc_mem_action_noop(struct palloc_heap *heap,
                                   struct pobj_action_internal *act) {}

/*
 * palloc_reservation_clear -- clears the reservation state of the block,
 *	discards the associated memory block if possible
 */
static void palloc_reservation_clear(struct palloc_heap *heap,
                                     struct pobj_action_internal *act,
                                     int publish) {
  if (act->mresv == NULL) return;

  struct memory_block_reserved *mresv = act->mresv;
  struct bucket *b = mresv->bucket;

  if (!publish) {
    util_mutex_lock(&b->lock);
    struct memory_block *am = &b->active_memory_block->m;

    /*
     * If a memory block used for the action is the currently active
     * memory block of the bucket it can be inserted back to the
     * bucket. This way it will be available for future allocation
     * requests, improving performance.
     */
    if (b->is_active && am->chunk_id == act->m.chunk_id &&
        am->zone_id == act->m.zone_id) {
      ASSERTeq(b->active_memory_block, mresv);
      LOG(11, NULL);
      bucket_insert_block(b, &act->m);
    }

    util_mutex_unlock(&b->lock);
  }

  if (util_fetch_and_sub64(&mresv->nresv, 1) == 1) {
    VALGRIND_ANNOTATE_HAPPENS_AFTER(&mresv->nresv);
    /*
     * If the memory block used for the action is not currently used
     * in any bucket nor action it can be discarded (given back to
     * the heap).
     */
    heap_discard_run(heap, &mresv->m);
    Free(mresv);
  } else {
    VALGRIND_ANNOTATE_HAPPENS_BEFORE(&mresv->nresv);
  }
}

/*
 * palloc_heap_action_on_cancel -- restores the state of the heap
 */
static void __attribute__((unused))
palloc_heap_action_on_cancel(struct palloc_heap *heap,
                             struct pobj_action_internal *act) {
  if (act->new_state == MEMBLOCK_FREE) return;

  VALGRIND_DO_MEMPOOL_FREE(heap->layout, act->m.m_ops->get_user_data(&act->m));

  act->m.m_ops->invalidate(&act->m);
  palloc_restore_free_chunk_state(heap, &act->m);

  palloc_reservation_clear(heap, act, 0 /* publish */);
}

/*
 * palloc_heap_action_on_process -- performs finalization steps under a lock
 *	on the persistent state
 */
static void palloc_heap_action_on_process(struct palloc_heap *heap,
                                          struct pobj_action_internal *act) {
  if (act->new_state == MEMBLOCK_ALLOCATED) {
    STATS_INC(heap->stats, persistent, heap_curr_allocated,
              act->m.m_ops->get_real_size(&act->m));
    if (act->m.type == MEMORY_BLOCK_RUN) {
      STATS_INC(heap->stats, transient, heap_run_allocated,
                act->m.m_ops->get_real_size(&act->m));
    }
  } else if (act->new_state == MEMBLOCK_FREE) {
    if (On_valgrind) {
      void *ptr = act->m.m_ops->get_user_data(&act->m);
      size_t size = act->m.m_ops->get_real_size(&act->m);

      VALGRIND_DO_MEMPOOL_FREE(heap->layout, ptr);

      /*
       * The sync module, responsible for implementations of
       * persistent memory resident volatile variables,
       * de-registers the pmemcheck pmem mapping at the time
       * of initialization. This is done so that usage of
       * pmem locks is not reported as an error due to
       * missing flushes/stores outside of transaction. But,
       * after we freed an object, we need to reestablish
       * the pmem mapping, otherwise pmemchek might miss bugs
       * that occur in newly allocated memory locations, that
       * once were occupied by a lock/volatile variable.
       */
      VALGRIND_REGISTER_PMEM_MAPPING(ptr, size);
    }

    STATS_SUB(heap->stats, persistent, heap_curr_allocated,
              act->m.m_ops->get_real_size(&act->m));
    if (act->m.type == MEMORY_BLOCK_RUN) {
      STATS_SUB(heap->stats, transient, heap_run_allocated,
                act->m.m_ops->get_real_size(&act->m));
    }
    heap_memblock_on_free(heap, &act->m);
  }
}

/*
 * palloc_heap_action_on_unlock -- performs finalization steps that need to be
 *	performed without a lock on persistent state
 */
static void __attribute__((unused))
palloc_heap_action_on_unlock(struct palloc_heap *heap,
                             struct pobj_action_internal *act) {
  if (act->new_state == MEMBLOCK_ALLOCATED) {
    palloc_reservation_clear(heap, act, 1 /* publish */);
  } else if (act->new_state == MEMBLOCK_FREE) {
    palloc_restore_free_chunk_state(heap, &act->m);
  }
}

/*
 * palloc_mem_action_exec -- executes a single memory action (set, and, or)
 */
static void __attribute__((unused))
palloc_mem_action_exec(struct palloc_heap *heap,
                       const struct pobj_action_internal *act,
                       struct operation_context *ctx) {
  operation_add_entry(ctx, act->ptr, act->value, ULOG_OPERATION_SET);
}

#ifdef ANCHOR_FUNCS
// delare the prototypes as these functions are in the last section of the code
// file
static void sec_palloc_heap_action_on_cancel(struct palloc_heap *heap,
                                             struct pobj_action_internal *act);
static void sec_palloc_heap_action_on_unlock(struct palloc_heap *heap,
                                             struct pobj_action_internal *act);
static void sec_palloc_heap_action_on_unlock(struct palloc_heap *heap,
                                             struct pobj_action_internal *act);
static void sec_palloc_mem_action_exec(struct palloc_heap *heap,
                                       const struct pobj_action_internal *act,
                                       struct operation_context *ctx);
static void sec_palloc_ulog_gen_num_action_exec(
    struct palloc_heap *heap, const struct pobj_action_internal *act,
    struct operation_context *ctx);
#endif
static const struct {
  /*
   * Translate action into some number of operation_entry'ies.
   */
  void (*exec)(struct palloc_heap *heap, const struct pobj_action_internal *act,
               struct operation_context *ctx);

  /*
   * Cancel any runtime state changes. Can be called only when action has
   * not been translated to persistent operation yet.
   */
  void (*on_cancel)(struct palloc_heap *heap, struct pobj_action_internal *act);

  /*
   * Final steps after persistent state has been modified. Performed
   * under action-specific lock.
   */
  void (*on_process)(struct palloc_heap *heap,
                     struct pobj_action_internal *act);

  /*
   * Final steps after persistent state has been modified. Performed
   * after action-specific lock has been dropped.
   */
  void (*on_unlock)(struct palloc_heap *heap, struct pobj_action_internal *act);
} action_funcs[POBJ_MAX_ACTION_TYPE] = {
#ifdef ANCHOR_FUNCS
    [POBJ_ACTION_TYPE_HEAP] =
        {
            .exec = palloc_heap_action_exec,
            .on_cancel = sec_palloc_heap_action_on_cancel,
            .on_process = palloc_heap_action_on_process,
            .on_unlock = sec_palloc_heap_action_on_unlock,
        },
    [POBJ_ACTION_TYPE_MEM] =
        {
            .exec = sec_palloc_mem_action_exec,
            .on_cancel = palloc_mem_action_noop,
            .on_process = palloc_mem_action_noop,
            .on_unlock = palloc_mem_action_noop,
        },
    [POBJ_ACTION_TYPE_ULOG_GEN_NUM] =
        {
            .exec = sec_palloc_ulog_gen_num_action_exec,
            .on_cancel = palloc_mem_action_noop,
            .on_process = palloc_mem_action_noop,
            .on_unlock = palloc_mem_action_noop,
        }
#else
    [POBJ_ACTION_TYPE_HEAP] =
        {
            .exec = palloc_heap_action_exec,
            .on_cancel = palloc_heap_action_on_cancel,
            .on_process = palloc_heap_action_on_process,
            .on_unlock = palloc_heap_action_on_unlock,
        },
    [POBJ_ACTION_TYPE_MEM] =
        {
            .exec = palloc_mem_action_exec,
            .on_cancel = palloc_mem_action_noop,
            .on_process = palloc_mem_action_noop,
            .on_unlock = palloc_mem_action_noop,
        }
#endif
};

/*
 * palloc_action_compare -- compares two actions based on lock address
 */
static int palloc_action_compare(const void *lhs, const void *rhs) {
  const struct pobj_action_internal *mlhs = lhs;
  const struct pobj_action_internal *mrhs = rhs;
  uintptr_t vlhs = (uintptr_t)(mlhs->lock);
  uintptr_t vrhs = (uintptr_t)(mrhs->lock);

  if (vlhs < vrhs) return -1;
  if (vlhs > vrhs) return 1;

  return 0;
}

/*
 * palloc_exec_actions -- perform the provided free/alloc operations
 */
static void palloc_exec_actions(struct palloc_heap *heap,
                                struct operation_context *ctx,
                                struct pobj_action_internal *actv,
                                size_t actvcnt) {
  /*
   * The operations array is sorted so that proper lock ordering is
   * ensured.
   */
  qsort(actv, actvcnt, sizeof(struct pobj_action_internal),
        palloc_action_compare);

  struct pobj_action_internal *act;
  for (size_t i = 0; i < actvcnt; ++i) {
    act = &actv[i];

    /*
     * This lock must be held for the duration between the creation
     * of the allocation metadata updates in the operation context
     * and the operation processing. This is because a different
     * thread might operate on the same 8-byte value of the run
     * bitmap and override allocation performed by this thread.
     */
    if (i == 0 || act->lock != actv[i - 1].lock) {
      if (act->lock) util_mutex_lock(act->lock);
    }

    /* translate action to some number of operation_entry'ies */
    action_funcs[act->type].exec(heap, act, ctx);
  }

  /* wait for all allocated object headers to be persistent */
  pmemops_drain(&heap->p_ops);

  /* perform all persistent memory operations */
  operation_process(ctx);

  for (size_t i = 0; i < actvcnt; ++i) {
    act = &actv[i];

    action_funcs[act->type].on_process(heap, act);

    if (i == actvcnt - 1 || act->lock != actv[i + 1].lock) {
      if (act->lock) util_mutex_unlock(act->lock);
    }
  }

  for (size_t i = 0; i < actvcnt; ++i) {
    act = &actv[i];

    action_funcs[act->type].on_unlock(heap, act);
  }

  operation_finish(ctx, 0);
}

/*
 * palloc_reserve -- creates a single reservation
 */
int palloc_reserve(struct palloc_heap *heap, size_t size,
                   palloc_constr constructor, void *arg, uint64_t extra_field,
                   uint16_t object_flags, uint16_t class_id, uint16_t arena_id,
                   struct pobj_action *act) {
  COMPILE_ERROR_ON(sizeof(struct pobj_action) !=
                   sizeof(struct pobj_action_internal));

  return palloc_reservation_create(heap, size, constructor, arg, extra_field,
                                   object_flags, class_id, arena_id,
                                   (struct pobj_action_internal *)act);
}

/*
 * palloc_defer_free_create -- creates an internal deferred free action
 */
static void palloc_defer_free_create(struct palloc_heap *heap, uint64_t off,
                                     struct pobj_action_internal *out) {
  COMPILE_ERROR_ON(sizeof(struct pobj_action) !=
                   sizeof(struct pobj_action_internal));

  out->type = POBJ_ACTION_TYPE_HEAP;
  out->offset = off;
  out->m = memblock_from_offset(heap, off);

  /*
   * For the duration of free we may need to protect surrounding
   * metadata from being modified.
   */
  out->lock = out->m.m_ops->get_lock(&out->m);
  out->mresv = NULL;
  out->new_state = MEMBLOCK_FREE;
}

/*
 * palloc_defer_free -- creates a deferred free action
 */
void palloc_defer_free(struct palloc_heap *heap, uint64_t off,
                       struct pobj_action *act) {
  COMPILE_ERROR_ON(sizeof(struct pobj_action) !=
                   sizeof(struct pobj_action_internal));

  palloc_defer_free_create(heap, off, (struct pobj_action_internal *)act);
}

/*
 * palloc_cancel -- cancels all reservations in the array
 */
void palloc_cancel(struct palloc_heap *heap, struct pobj_action *actv,
                   size_t actvcnt) {
  struct pobj_action_internal *act;
  for (size_t i = 0; i < actvcnt; ++i) {
    act = (struct pobj_action_internal *)&actv[i];
    action_funcs[act->type].on_cancel(heap, act);
  }
}

#ifdef ANCHOR_FUNCS
/*
 * sec_palloc_exec_actions -- perform the provided free/alloc operations
 */
static void sec_palloc_exec_actions(struct palloc_heap *heap,
                                    struct operation_context *ctx,
                                    struct pobj_action_internal *actv,
                                    size_t actvcnt) {
  LOG(11, NULL);
  /*
   * The operations array is sorted so that proper lock ordering is
   * ensured.
   */
  qsort(actv, actvcnt, sizeof(struct pobj_action_internal),
        palloc_action_compare);

  struct pobj_action_internal *act;
  for (size_t i = 0; i < actvcnt; ++i) {
    act = &actv[i];

    /*
     * This lock must be held for the duration between the creation
     * of the allocation metadata updates in the operation context
     * and the operation processing. This is because a different
     * thread might operate on the same 8-byte value of the run
     * bitmap and override allocation performed by this thread.
     */
    if (i == 0 || act->lock != actv[i - 1].lock) {
      if (act->lock) util_mutex_lock(act->lock);
    }

    /* translate action to some number of operation_entry'ies */
    action_funcs[act->type].exec(heap, act, ctx);
  }

  /* wait for all allocated object headers to be persistent */
  pmemops_drain(&heap->p_ops);

  /* perform all persistent memory operations */
  sec_operation_process(ctx);

  for (size_t i = 0; i < actvcnt; ++i) {
    act = &actv[i];

    action_funcs[act->type].on_process(heap, act);

    if (i == actvcnt - 1 || act->lock != actv[i + 1].lock) {
      if (act->lock) util_mutex_unlock(act->lock);
    }
  }

  for (size_t i = 0; i < actvcnt; ++i) {
    act = &actv[i];

    action_funcs[act->type].on_unlock(heap, act);
  }

  operation_finish(ctx, 0);
}
/*
 * palloc_publish -- publishes all reservations in the array
 */
void palloc_publish(struct palloc_heap *heap, struct pobj_action *actv,
                    size_t actvcnt, struct operation_context *ctx) {
  LOG(15, NULL);
  if (heap->base_v != NULL)
    sec_palloc_exec_actions(heap, ctx, (struct pobj_action_internal *)actv,
                            actvcnt);
  else
    palloc_exec_actions(heap, ctx, (struct pobj_action_internal *)actv,
                        actvcnt);
}
#else
/*
 * palloc_publish -- publishes all reservations in the array
 */
void palloc_publish(struct palloc_heap *heap, struct pobj_action *actv,
                    size_t actvcnt, struct operation_context *ctx) {
  LOG(15, NULL);
  palloc_exec_actions(heap, ctx, (struct pobj_action_internal *)actv, actvcnt);
}
#endif

/*
 * palloc_operation -- persistent memory operation. Takes a NULL pointer
 *	or an existing memory block and modifies it to occupy, at least, 'size'
 *	number of bytes.
 *
 * The malloc, free and realloc routines are implemented in the context of this
 * common operation which encompasses all of the functionality usually done
 * separately in those methods.
 *
 * The first thing that needs to be done is determining which memory blocks
 * will be affected by the operation - this varies depending on the whether the
 * operation will need to modify or free an existing block and/or allocate
 * a new one.
 *
 * Simplified allocation process flow is as follows:
 *	- reserve a new block in the transient heap
 *	- prepare the new block
 *	- create redo log of required modifications
 *		- chunk metadata
 *		- offset of the new object
 *	- commit and process the redo log
 *
 * And similarly, the deallocation process:
 *	- create redo log of required modifications
 *		- reverse the chunk metadata back to the 'free' state
 *		- set the destination of the object offset to zero
 *	- commit and process the redo log
 * There's an important distinction in the deallocation process - it does not
 * return the memory block to the transient container. That is done once no more
 * memory is available.
 *
 * Reallocation is a combination of the above, with one additional step
 * of copying the old content.
 */
int palloc_operation(struct palloc_heap *heap, uint64_t off, uint64_t *dest_off,
                     size_t size, palloc_constr constructor, void *arg,
                     uint64_t extra_field, uint16_t object_flags,
                     uint16_t class_id, uint16_t arena_id,
                     struct operation_context *ctx) {
  size_t user_size = 0;

  size_t nops = 0;
  struct pobj_action_internal ops[2];
  struct pobj_action_internal *alloc = NULL;
  struct pobj_action_internal *dealloc = NULL;

  /*
   * The offset of an existing block can be nonzero which means this
   * operation is either free or a realloc - either way the offset of the
   * object needs to be translated into memory block, which is a structure
   * that all of the heap methods expect.
   */
  if (off != 0) {
    dealloc = &ops[nops++];
    palloc_defer_free_create(heap, off, dealloc);
    user_size = dealloc->m.m_ops->get_user_size(&dealloc->m);
    if (user_size == size) {
      operation_cancel(ctx);
      return 0;
    }
  }

  /* alloc or realloc */
  if (size != 0) {
    alloc = &ops[nops++];
    if (palloc_reservation_create(heap, size, constructor, arg, extra_field,
                                  object_flags, class_id, arena_id,
                                  alloc) != 0) {
      operation_cancel(ctx);
      return -1;
    }
  }

  /* realloc */
  if (alloc != NULL && dealloc != NULL) {
    /* copy data to newly allocated memory */
    size_t old_size = user_size;
    size_t to_cpy = old_size > size ? size : old_size;
    VALGRIND_ADD_TO_TX(HEAP_OFF_TO_PTR(heap, alloc->offset), to_cpy);
    pmemops_memcpy(&heap->p_ops, HEAP_OFF_TO_PTR(heap, alloc->offset),
                   HEAP_OFF_TO_PTR(heap, off), to_cpy, 0);
    VALGRIND_REMOVE_FROM_TX(HEAP_OFF_TO_PTR(heap, alloc->offset), to_cpy);
  }

  /*
   * If the caller provided a destination value to update, it needs to be
   * modified atomically alongside the heap metadata, and so the operation
   * context must be used.
   */
  if (dest_off) {
    operation_add_entry(ctx, dest_off, alloc ? alloc->offset : 0,
                        ULOG_OPERATION_SET);
  }

  /* and now actually perform the requested operation! */
  palloc_exec_actions(heap, ctx, ops, nops);

  return 0;
}

/*
 * palloc_offset_compare -- (internal) comparator for sorting by the offset of
 *	an object.
 */
static int palloc_offset_compare(const void *lhs, const void *rhs) {
  const uint64_t *const *mlhs = lhs;
  const uint64_t *const *mrhs = rhs;
  uintptr_t vlhs = **mlhs;
  uintptr_t vrhs = **mrhs;

  if (vlhs < vrhs) return 1;
  if (vlhs > vrhs) return -1;

  return 0;
}

struct palloc_defrag_entry {
  uint64_t **offsetp;
};

/*
 * palloc_pointer_compare -- (internal) comparator for sorting by the
 *	pointer of an offset in the tree.
 */
static int palloc_pointer_compare(const void *lhs, const void *rhs) {
  const struct palloc_defrag_entry *mlhs = lhs;
  const struct palloc_defrag_entry *mrhs = rhs;
  uintptr_t vlhs = (uintptr_t)*mlhs->offsetp;
  uintptr_t vrhs = (uintptr_t)*mrhs->offsetp;

  if (vlhs > vrhs) return 1;
  if (vlhs < vrhs) return -1;

  return 0;
}

VEC(pobj_actions, struct pobj_action);

/*
 * pobj_actions_add -- add a new action to the end of the vector and return
 *      its slot. Vector must be able to hold the new value. Reallocation is
 *      forbidden.
 */
static struct pobj_action *pobj_actions_add(struct pobj_actions *actv) {
  /*
   * This shouldn't happen unless there's a bug in the calculation
   * of the maximum number of actions.
   */
  if (VEC_SIZE(actv) == VEC_CAPACITY(actv)) abort();

  actv->size++;

  return &VEC_BACK(actv);
}

/*
 * palloc_defrag -- forces recycling of all available memory, and reallocates
 *	provided objects so that they have the lowest possible address.
 */
int palloc_defrag(struct palloc_heap *heap, uint64_t **objv, size_t objcnt,
                  struct operation_context *ctx,
                  struct pobj_defrag_result *result) {
  int ret = -1;
  /*
   * Offsets pointers need to be sorted by the offset of the object in
   * descending order. This gives us two things, a) the defragmentation
   * process is more likely to move objects to a lower offset, improving
   * locality and tentatively enabling the heap to shrink, and b) pointers
   * to the same object are next to each other in the array, so it's easy
   * to reallocate the object once and simply update all remaining
   * pointers.
   */
  qsort(objv, objcnt, sizeof(uint64_t *), palloc_offset_compare);

  /*
   * We also need to store pointers to objects in a tree, so that it's
   * possible to update pointers to other objects on the provided list
   * that reside in the objects that were already reallocated or
   * will be reallocated later on in the process.
   */
  struct ravl *objvp = ravl_new_sized(palloc_pointer_compare,
                                      sizeof(struct palloc_defrag_entry));
  if (objvp == NULL) goto err_ravl;

  /*
   * We need to calculate how many pointers to the same object we will
   * need to update during defrag. This will be used to calculate capacity
   * for the action vector and the redo log.
   */
  size_t longest_object_sequence = 1;
  size_t current_object_sequence = 1;
  for (size_t i = 0; i < objcnt; ++i) {
    if (i != 0 && *objv[i - 1] == *objv[i]) {
      current_object_sequence += 1;
    } else {
      if (current_object_sequence > longest_object_sequence)
        longest_object_sequence = current_object_sequence;
      current_object_sequence = 1;
    }

    struct palloc_defrag_entry e = {&objv[i]};
    if (ravl_emplace_copy(objvp, &e) != 0) goto err_objvp;
  }

  if (current_object_sequence > longest_object_sequence)
    longest_object_sequence = current_object_sequence;

  heap_force_recycle(heap);

  /*
   * The number of actions at which the action vector will be processed.
   */
  const size_t actions_per_realloc = 3; /* alloc + free + set */
  const size_t max_actions =
      LANE_REDO_EXTERNAL_SIZE / sizeof(struct ulog_entry_val) -
      actions_per_realloc;

  struct pobj_actions actv;
  VEC_INIT(&actv);

  /*
   * Vector needs enough capacity to handle the largest
   * possible sequence of actions. Given that the actions are published
   * once the max_actions threshold is crossed AND the sequence for the
   * current object is finished, worst-case capacity is a sum of
   * max_actions and the largest object sequence - because that sequence
   * might happen to begin when current object number i == max_action.
   */
  size_t actv_required_capacity =
      max_actions + longest_object_sequence + actions_per_realloc;

  if (VEC_RESERVE(&actv, actv_required_capacity) != 0) goto err;

  /*
   * Do NOT reallocate action vector after this line, because
   * prev_reserve can point to the slot in the original vector.
   */

  struct pobj_action *prev_reserve = NULL;
  uint64_t prev_offset = 0;
  for (size_t i = 0; i < objcnt; ++i) {
    uint64_t *offsetp = objv[i];
    uint64_t offset = *offsetp;

    /*
     * We want to keep our redo logs relatively small, and so
     * actions vector is processed on a regular basis.
     */
    if (prev_offset != offset && VEC_SIZE(&actv) >= max_actions) {
      /*
       * If there are any pointers on the tree to the
       * memory actions that are being applied, they need to
       * be removed. Future reallocations will already have
       * these modifications applied.
       */
      struct pobj_action *iter;
      VEC_FOREACH_BY_PTR(iter, &actv) {
        if (iter->type != POBJ_ACTION_TYPE_MEM) continue;
        struct pobj_action_internal *iteri =
            (struct pobj_action_internal *)iter;
        struct palloc_defrag_entry e = {&iteri->ptr};
        struct ravl_node *n = ravl_find(objvp, &e, RAVL_PREDICATE_EQUAL);
        if (n != NULL) ravl_remove(objvp, n);
      }

      size_t entries_size = VEC_SIZE(&actv) * sizeof(struct ulog_entry_val);
#ifdef ANCHOR_FUNCS
      if (sec_operation_reserve(ctx, entries_size) != 0) goto err;
#else
      if (operation_reserve(ctx, entries_size) != 0) goto err;
#endif
      palloc_publish(heap, VEC_ARR(&actv), VEC_SIZE(&actv), ctx);

      operation_start(ctx);
      VEC_CLEAR(&actv);
    }

    /*
     * If the previous pointer of this offset was skipped,
     * skip all pointers for that object.
     */
    if (prev_reserve == NULL && prev_offset == offset) continue;

    /*
     * If this is an offset to an object that was already
     * reallocated in the previous iteration, we need to only update
     * the pointer to the new offset.
     */
    if (prev_reserve && prev_offset == offset) {
      struct pobj_action *set = pobj_actions_add(&actv);

      palloc_set_value(heap, set, offsetp, prev_reserve->heap.offset);
      struct pobj_action_internal *seti = (struct pobj_action_internal *)set;

      /*
       * Since this pointer can reside in an object that will
       * be reallocated later on we need to be able to
       * find and update it when that happens.
       */
      struct palloc_defrag_entry e = {&seti->ptr};
      struct ravl_node *n = ravl_find(objvp, &e, RAVL_PREDICATE_EQUAL);
      if (n != NULL) ravl_remove(objvp, n);
      /*
       * Notice that the tree is ordered by the content of the
       * pointer, not the pointer itself. This might look odd,
       * but we are inserting a *different* pointer to the
       * same pointer to an offset.
       */
      if (ravl_emplace_copy(objvp, &e) != 0) goto err;

      continue;
    }

    if (result) result->total++;

    prev_reserve = NULL;
    prev_offset = offset;

    struct memory_block m = memblock_from_offset(heap, offset);

    if (m.type == MEMORY_BLOCK_HUGE) continue;

    os_mutex_t *mlock = m.m_ops->get_lock(&m);
    os_mutex_lock(mlock);
    unsigned original_fillpct = m.m_ops->fill_pct(&m);
    os_mutex_unlock(mlock);

    /*
     * Empirically, 50% fill rate is the sweetspot for moving
     * objects between runs. Other values tend to produce worse
     * results.
     */
    if (original_fillpct > 50) continue;

    size_t user_size = m.m_ops->get_user_size(&m);

    struct pobj_action *reserve = pobj_actions_add(&actv);

    if (palloc_reservation_create(
            heap, user_size, NULL, NULL, m.m_ops->get_extra(&m),
            m.m_ops->get_flags(&m), 0, HEAP_ARENA_PER_THREAD,
            (struct pobj_action_internal *)reserve) != 0) {
      VEC_POP_BACK(&actv);
      continue;
    }

    uint64_t new_offset = reserve->heap.offset;

    VALGRIND_ADD_TO_TX(HEAP_OFF_TO_PTR(heap, new_offset), user_size);
    pmemops_memcpy(&heap->p_ops, HEAP_OFF_TO_PTR(heap, new_offset),
                   HEAP_OFF_TO_PTR(heap, *offsetp), user_size, 0);
    VALGRIND_REMOVE_FROM_TX(HEAP_OFF_TO_PTR(heap, new_offset), user_size);

    /*
     * If there is a pointer provided by the user inside of the
     * object we are in the process of reallocating, we need to
     * find that pointer and update it to reflect the new location
     * of PMEMoid.
     */
    ptrdiff_t diff = (ptrdiff_t)(new_offset - offset);
    uint64_t *objptr = (uint64_t *)((uint64_t)heap->base + offset);
    uint64_t objend = ((uint64_t)objptr + user_size);
    struct ravl_node *nptr = NULL;
    enum ravl_predicate p = RAVL_PREDICATE_GREATER_EQUAL;
    struct palloc_defrag_entry search_entry = {&objptr};

    while ((nptr = ravl_find(objvp, &search_entry, p)) != NULL) {
      p = RAVL_PREDICATE_GREATER;
      struct palloc_defrag_entry *e = ravl_data(nptr);
      uint64_t poffset = (uint64_t)(*e->offsetp);

      if (poffset >= objend) break;

      struct palloc_defrag_entry ne = *e;
      ravl_remove(objvp, nptr);

      objptr = (uint64_t *)poffset;

      poffset = (uint64_t)((ptrdiff_t)poffset + diff);

      *ne.offsetp = (uint64_t *)poffset;
    }
    offsetp = objv[i];

    struct pobj_action *set = pobj_actions_add(&actv);

    /*
     * We need to change the pointer in the tree to the pointer
     * of this new unpublished action, so that it can be updated
     * later on if needed.
     */
    palloc_set_value(heap, set, offsetp, new_offset);
    struct pobj_action_internal *seti = (struct pobj_action_internal *)set;
    struct palloc_defrag_entry e = {&seti->ptr};
    struct ravl_node *n = ravl_find(objvp, &e, RAVL_PREDICATE_EQUAL);
    if (n != NULL) ravl_remove(objvp, n);

    /* same as above, this is a different pointer to same content */
    if (ravl_emplace_copy(objvp, &e) != 0) goto err;

    struct pobj_action *dfree = pobj_actions_add(&actv);

    palloc_defer_free(heap, offset, dfree);

    if (result) result->relocated++;

    prev_reserve = reserve;
    prev_offset = offset;
  }

  if (VEC_SIZE(&actv) != 0) {
    size_t entries_size = VEC_SIZE(&actv) * sizeof(struct ulog_entry_val);
#ifdef ANCHOR_FUNCS
    if (sec_operation_reserve(ctx, entries_size) != 0) goto err;
#else
    if (operation_reserve(ctx, entries_size) != 0) goto err;
#endif
    palloc_publish(heap, VEC_ARR(&actv), VEC_SIZE(&actv), ctx);
  } else {
    operation_cancel(ctx);
  }

  ret = 0;

err:
  if (ret != 0) palloc_cancel(heap, VEC_ARR(&actv), VEC_SIZE(&actv));
  VEC_DELETE(&actv);
err_objvp:
  ravl_delete(objvp);
err_ravl:
  if (ret != 0) operation_cancel(ctx);

  return ret;
}

/*
 * palloc_usable_size -- returns the number of bytes in the memory block
 */
size_t palloc_usable_size(struct palloc_heap *heap, uint64_t off) {
  struct memory_block m = memblock_from_offset(heap, off);

  return m.m_ops->get_user_size(&m);
}

/*
 * palloc_extra -- returns allocation extra field
 */
uint64_t palloc_extra(struct palloc_heap *heap, uint64_t off) {
  struct memory_block m = memblock_from_offset(heap, off);

  return m.m_ops->get_extra(&m);
}

/*
 * palloc_flags -- returns allocation flags
 */
uint16_t palloc_flags(struct palloc_heap *heap, uint64_t off) {
  struct memory_block m = memblock_from_offset(heap, off);

  return m.m_ops->get_flags(&m);
}

/*
 * pmalloc_search_cb -- (internal) foreach callback.
 */
static int pmalloc_search_cb(const struct memory_block *m, void *arg) {
  struct memory_block *out = arg;

  if (MEMORY_BLOCK_EQUALS(*m, *out)) return 0; /* skip the same object */

  *out = *m;

  return 1;
}

/*
 * palloc_first -- returns the first object from the heap.
 */
uint64_t palloc_first(struct palloc_heap *heap) {
  struct memory_block search = MEMORY_BLOCK_NONE;

  heap_foreach_object(heap, pmalloc_search_cb, &search, MEMORY_BLOCK_NONE);

  if (MEMORY_BLOCK_IS_NONE(search)) return 0;

  void *uptr = search.m_ops->get_user_data(&search);

  return HEAP_PTR_TO_OFF(heap, uptr);
}

/*
 * palloc_next -- returns the next object relative to 'off'.
 */
uint64_t palloc_next(struct palloc_heap *heap, uint64_t off) {
  struct memory_block m = memblock_from_offset(heap, off);
  struct memory_block search = m;

  heap_foreach_object(heap, pmalloc_search_cb, &search, m);

  if (MEMORY_BLOCK_IS_NONE(search) || MEMORY_BLOCK_EQUALS(search, m)) return 0;

  void *uptr = search.m_ops->get_user_data(&search);

  return HEAP_PTR_TO_OFF(heap, uptr);
}

/*
 * palloc_boot -- initializes allocator section
 */
int palloc_boot(struct palloc_heap *heap, void *heap_start, uint64_t heap_size,
                uint64_t *sizep, void *base, struct pmem_ops *p_ops,
                struct stats *stats, struct pool_set *set) {
  return heap_boot(heap, heap_start, heap_size, sizep, base, p_ops, stats, set);
}

/*
 * palloc_buckets_init -- initialize buckets
 */
int palloc_buckets_init(struct palloc_heap *heap) {
  return heap_buckets_init(heap);
}

/*
 * palloc_init -- initializes palloc heap
 */
int palloc_init(void *heap_start, uint64_t heap_size, uint64_t *sizep,
                struct pmem_ops *p_ops) {
  return heap_init(heap_start, heap_size, sizep, p_ops);
}

/*
 * palloc_heap_end -- returns first address after heap
 */
void *palloc_heap_end(struct palloc_heap *h) { return heap_end(h); }

/*
 * palloc_heap_check -- verifies heap state
 */
int palloc_heap_check(void *heap_start, uint64_t heap_size) {
  return heap_check(heap_start, heap_size);
}

/*
 * palloc_heap_check_remote -- verifies state of remote replica
 */
int palloc_heap_check_remote(void *heap_start, uint64_t heap_size,
                             struct remote_ops *ops) {
  return heap_check_remote(heap_start, heap_size, ops);
}

/*
 * palloc_heap_cleanup -- cleanups the volatile heap state
 */
void palloc_heap_cleanup(struct palloc_heap *heap) { heap_cleanup(heap); }

#if VG_MEMCHECK_ENABLED
/*
 * palloc_vg_register_alloc -- (internal) registers allocation header
 * in Valgrind
 */
static int palloc_vg_register_alloc(const struct memory_block *m, void *arg) {
  struct palloc_heap *heap = arg;

  m->m_ops->reinit_header(m);

  void *uptr = m->m_ops->get_user_data(m);
  size_t usize = m->m_ops->get_user_size(m);
  VALGRIND_DO_MEMPOOL_ALLOC(heap->layout, uptr, usize);
  VALGRIND_DO_MAKE_MEM_DEFINED(uptr, usize);

  return 0;
}

/*
 * palloc_heap_vg_open -- notifies Valgrind about heap layout
 */
void palloc_heap_vg_open(struct palloc_heap *heap, int objects) {
  heap_vg_open(heap, palloc_vg_register_alloc, heap, objects);
}
#endif

#ifdef ANCHOR_FUNCS
/*
 * ANCHOR Functions
 */

/*
 * sec_palloc_heap_end -- returns first address after heap
 */
void *sec_palloc_heap_end(struct palloc_heap *h) { return sec_heap_end(h); }

/*
 * sec_palloc_defer_free_create -- creates an internal deferred free action in
 * ANCHOR
 */
static void sec_palloc_defer_free_create(struct palloc_heap *heap, uint64_t off,
                                         struct pobj_action_internal *out) {
  COMPILE_ERROR_ON(sizeof(struct pobj_action) !=
                   sizeof(struct pobj_action_internal));

  out->type = POBJ_ACTION_TYPE_HEAP;
  out->offset = off;
  out->m = sec_memblock_from_offset(heap, off);

  /*
   * For the duration of free we may need to protect surrounding
   * metadata from being modified.
   */
  out->lock = out->m.m_ops->get_lock(&out->m);
  out->mresv = NULL;
  out->new_state = MEMBLOCK_FREE;
}

/*
 * sec_palloc_defer_free -- creates a deferred free action in ANCHOR
 */
void sec_palloc_defer_free(struct palloc_heap *heap, uint64_t off,
                           struct pobj_action *act) {
  COMPILE_ERROR_ON(sizeof(struct pobj_action) !=
                   sizeof(struct pobj_action_internal));

  sec_palloc_defer_free_create(heap, off, (struct pobj_action_internal *)act);
}
/*
 * sec_palloc_restore_free_chunk_state -- updates the runtime state of a free
 * chunk.
 *
 * This function also takes care of coalescing of huge chunks.
 */
static void sec_palloc_restore_free_chunk_state(struct palloc_heap *heap,
                                                struct memory_block *m) {
  if (m->type == MEMORY_BLOCK_HUGE) {
    struct bucket *b = heap_bucket_acquire(heap, DEFAULT_ALLOC_CLASS_ID,
                                           HEAP_ARENA_PER_THREAD);
    if (sec_heap_free_chunk_reuse(heap, b, m) != 0) {
      if (errno == EEXIST) {
        FATAL("duplicate runtime chunk state, possible double free");
      } else {
        LOG(2, "unable to track runtime chunk state");
      }
    }
    heap_bucket_release(heap, b);
  }
}

/*
 * sec_palloc_reservation_clear -- clears the reservation state of the block,
 *	discards the associated memory block if possible
 */
static void sec_palloc_reservation_clear(struct palloc_heap *heap,
                                         struct pobj_action_internal *act,
                                         int publish) {
  if (act->mresv == NULL) return;

  struct memory_block_reserved *mresv = act->mresv;
  struct bucket *b = mresv->bucket;

  if (!publish) {
    util_mutex_lock(&b->lock);
    struct memory_block *am = &b->active_memory_block->m;

    /*
     * If a memory block used for the action is the currently active
     * memory block of the bucket it can be inserted back to the
     * bucket. This way it will be available for future allocation
     * requests, improving performance.
     */
    if (b->is_active && am->chunk_id == act->m.chunk_id &&
        am->zone_id == act->m.zone_id) {
      ASSERTeq(b->active_memory_block, mresv);
      LOG(11, NULL);
      bucket_insert_block(b, &act->m);
    }

    util_mutex_unlock(&b->lock);
  }

  if (util_fetch_and_sub64(&mresv->nresv, 1) == 1) {
    VALGRIND_ANNOTATE_HAPPENS_AFTER(&mresv->nresv);
    /*
     * If the memory block used for the action is not currently used
     * in any bucket nor action it can be discarded (given back to
     * the heap).
     */
    sec_heap_discard_run(heap, &mresv->m);
    Free(mresv);
  } else {
    VALGRIND_ANNOTATE_HAPPENS_BEFORE(&mresv->nresv);
  }
}

/*
 * palloc_heap_action_on_cancel -- restores the state of the heap
 */
static void sec_palloc_heap_action_on_cancel(struct palloc_heap *heap,
                                             struct pobj_action_internal *act) {
  if (act->new_state == MEMBLOCK_FREE) return;

  VALGRIND_DO_MEMPOOL_FREE(heap->layout, act->m.m_ops->get_user_data(&act->m));

  act->m.m_ops->invalidate(&act->m);
  sec_palloc_restore_free_chunk_state(heap, &act->m);

  sec_palloc_reservation_clear(heap, act, 0 /* publish */);
}

/*
 * sec_palloc_heap_action_on_unlock -- performs finalization steps that need to
 *be performed without a lock on persistent state
 */
static void sec_palloc_heap_action_on_unlock(struct palloc_heap *heap,
                                             struct pobj_action_internal *act) {
  if (act->new_state == MEMBLOCK_ALLOCATED) {
    sec_palloc_reservation_clear(heap, act, 1 /* publish */);
  } else if (act->new_state == MEMBLOCK_FREE) {
    sec_palloc_restore_free_chunk_state(heap, &act->m);
  }
}

/*
 * sec_palloc_mem_action_exec -- executes a single memory action (set, and, or)
 */
static void sec_palloc_mem_action_exec(struct palloc_heap *heap,
                                       const struct pobj_action_internal *act,
                                       struct operation_context *ctx) {
  sec_operation_add_entry(ctx, act->ptr, act->value, ULOG_OPERATION_SET);
}

/*
 * sec_palloc_ulog_gen_num_action_exec
 * Adds 3 actions : 1 for encrypted gen_num, 2 for the tag update
 */
static void sec_palloc_ulog_gen_num_action_exec(
    struct palloc_heap *heap, const struct pobj_action_internal *act,
    struct operation_context *ctx) {
  PMEMoid log_oid = {((struct pmemobjpool *)(heap->base_v))->uuid_lo,
                     (uintptr_t)act->ptr - (uintptr_t)(heap->base)};
  struct ulog *ulog_v = ulog_hdr_decrypt(
      (struct ulog *)act->ptr, (const struct pmem_ops *)&(heap->p_ops));
  ulog_v->gen_num = act->value;

  uint8_t tag[HMAC_SIZE];
  struct ulog *ulog_encr = ulog_hdr_encrypt(ulog_v, (uint8_t *)&log_oid, tag);

  append_undo_object_entry(log_oid, tag, OBJ_NLANES, sizeof(struct ulog), 0, 1);

  sec_operation_add_entry(ctx, &((struct ulog *)act->ptr)->gen_num,
                          ulog_encr->gen_num, ULOG_OPERATION_SET);

  free(ulog_encr);
}

/*
 * palloc_set_gen_num -- creates a new set memory action
 */
void palloc_set_gen_num(struct palloc_heap *heap, struct pobj_action *act,
                        uint64_t *ulog_ptr, uint64_t gen_num_value) {
  act->type = POBJ_ACTION_TYPE_ULOG_GEN_NUM;

  struct pobj_action_internal *actp = (struct pobj_action_internal *)act;
  actp->ptr = ulog_ptr;
  actp->value = gen_num_value;
  actp->lock = NULL;
}

/*
 * sec_palloc_usable_size -- returns the number of bytes in the memory block
 */
size_t sec_palloc_usable_size(struct palloc_heap *heap, uint64_t off) {
  struct memory_block m = sec_memblock_from_offset(heap, off);

  return m.m_ops->get_user_size(&m);
}

/*
 * sec_pmemobj_alloc_usable_size -- returns usable size of secure object
 */
size_t sec_pmemobj_alloc_usable_size(PMEMoid oid) {
  LOG(3, "oid.off 0x%016" PRIx64, oid.off);

  if (oid.off == 0) return 0;

  PMEMobjpool *pop = pmemobj_pool_by_oid(oid);

  ASSERTne(pop, NULL);
  ASSERT(OBJ_OID_IS_VALID(pop, oid));

  return (sec_palloc_usable_size(&pop->heap, oid.off));
}

/*
 * sec_alloc_prep_block -- (internal) prepares a memory block for allocation
 *
 * Once the block is fully reserved and it's guaranteed that no one else will
 * be able to write to this memory region it is safe to write the allocation
 * header and call the object construction function.
 *
 * Because the memory block at this stage is only reserved in transient state
 * there's no need to worry about fail-safety of this method because in case
 * of a crash the memory will be back in the free blocks collection.
 */
static int sec_alloc_prep_block(struct palloc_heap *heap,
                                const struct memory_block *m,
                                palloc_constr constructor, void *arg,
                                uint64_t extra_field, uint16_t object_flags,
                                uint64_t *offset_value) {
  void *uptr = m->m_ops->get_user_data(m);
  size_t usize = m->m_ops->get_user_size(m);

  VALGRIND_DO_MEMPOOL_ALLOC(heap->layout, uptr, usize);
  VALGRIND_DO_MAKE_MEM_UNDEFINED(uptr, usize);
  VALGRIND_ANNOTATE_NEW_MEMORY(uptr, usize);

  m->m_ops->write_header(m, extra_field, object_flags);

  /*
   * Set allocated memory with pattern, if debug.heap.alloc_pattern CTL
   * parameter had been set.
   */
  if (unlikely(heap->alloc_pattern > PALLOC_CTL_DEBUG_NO_PATTERN)) {
    pmemops_memset(&heap->p_ops, uptr, heap->alloc_pattern, usize, 0);
    VALGRIND_DO_MAKE_MEM_UNDEFINED(uptr, usize);
  }

  int ret;
  if (constructor != NULL &&
      (ret = constructor(heap->base_v, uptr, usize, arg)) !=
          0) {  // ANCHOR: Pass the volatile pop address

    /*
     * If canceled, revert the block back to the free state in vg
     * machinery.
     */
    VALGRIND_DO_MEMPOOL_FREE(heap->layout, uptr);

    return ret;
  }

  /*
   * To avoid determining the user data pointer twice this method is also
   * responsible for calculating the offset of the object in the pool that
   * will be used to set the offset destination pointer provided by the
   * caller.
   */
  *offset_value = HEAP_PTR_TO_OFF(heap, uptr);

  return 0;
}

/*
 * sec_palloc_reservation_create -- creates a volatile reservation of a
 *	memory block.
 *
 * The first step in the allocation of a new block is reserving it in
 * the transient heap - which is represented by the bucket abstraction.
 *
 * To provide optimal scaling for multi-threaded applications and reduce
 * fragmentation the appropriate bucket is chosen depending on the
 * current thread context and to which allocation class the requested
 * size falls into.
 *
 * Once the bucket is selected, just enough memory is reserved for the
 * requested size. The underlying block allocation algorithm
 * (best-fit, next-fit, ...) varies depending on the bucket container.
 */
static int sec_palloc_reservation_create(struct palloc_heap *heap, size_t size,
                                         palloc_constr constructor, void *arg,
                                         uint64_t extra_field,
                                         uint16_t object_flags,
                                         uint16_t class_id, uint16_t arena_id,
                                         struct pobj_action_internal *out) {
  int err = 0;

  struct memory_block *new_block = &out->m;
  out->type = POBJ_ACTION_TYPE_HEAP;

  ASSERT(class_id < UINT8_MAX);
  struct alloc_class *c =
      class_id == 0
          ? heap_get_best_class(heap, size)
          : alloc_class_by_id(heap_alloc_classes(heap), (uint8_t)class_id);

  if (c == NULL) {
    ERR("no allocation class for size %lu bytes", size);
    errno = EINVAL;
    return -1;
  }

  /*
   * The caller provided size in bytes, but buckets operate in
   * 'size indexes' which are multiples of the block size in the
   * bucket.
   *
   * For example, to allocate 500 bytes from a bucket that
   * provides 256 byte blocks two memory 'units' are required.
   */
  ssize_t size_idx = alloc_class_calc_size_idx(c, size);
  if (size_idx < 0) {
    ERR("allocation class not suitable for size %lu bytes", size);
    errno = EINVAL;
    return -1;
  }
  ASSERT(size_idx <= UINT32_MAX);
  *new_block = MEMORY_BLOCK_NONE;
  new_block->size_idx = (uint32_t)size_idx;

  struct bucket *b = heap_bucket_acquire(heap, c->id, arena_id);

  err = sec_heap_get_bestfit_block(heap, b, new_block);
  if (err != 0) goto out;

  if (sec_alloc_prep_block(heap, new_block, constructor, arg, extra_field,
                           object_flags, &out->offset) != 0) {
    /*
     * Constructor returned non-zero value which means
     * the memory block reservation has to be rolled back.
     */
    if (new_block->type == MEMORY_BLOCK_HUGE) {
      LOG(11, NULL);
      bucket_insert_block(b, new_block);
    }
    err = ECANCELED;
    goto out;
  }

  /*
   * Each as of yet unfulfilled reservation needs to be tracked in the
   * runtime state.
   * The memory block cannot be put back into the global state unless
   * there are no active reservations.
   */
  if ((out->mresv = b->active_memory_block) != NULL)
    util_fetch_and_add64(&out->mresv->nresv, 1);

  out->lock = new_block->m_ops->get_lock(new_block);
  out->new_state = MEMBLOCK_ALLOCATED;

out:
  heap_bucket_release(heap, b);

  if (err == 0) return 0;

  errno = err;
  return -1;
}

/*
 * sec_palloc_reserve -- creates a single reservation
 */
int sec_palloc_reserve(struct palloc_heap *heap, size_t size,
                       palloc_constr constructor, void *arg,
                       uint64_t extra_field, uint16_t object_flags,
                       uint16_t class_id, uint16_t arena_id,
                       struct pobj_action *act) {
  LOG(11, NULL);
  COMPILE_ERROR_ON(sizeof(struct pobj_action) !=
                   sizeof(struct pobj_action_internal));

  return sec_palloc_reservation_create(
      heap, size, constructor, arg, extra_field, object_flags, class_id,
      arena_id, (struct pobj_action_internal *)act);
}

/*
 * sec_palloc_operation -- persistent memory operation. Takes a NULL pointer
 *	or an existing memory block and modifies it to occupy, at least, 'size'
 *	number of bytes.
 *
 * The malloc, free and realloc routines are implemented in the context of this
 * common operation which encompasses all of the functionality usually done
 * separately in those methods.
 *
 * The first thing that needs to be done is determining which memory blocks
 * will be affected by the operation - this varies depending on the whether the
 * operation will need to modify or free an existing block and/or allocate
 * a new one.
 *
 * Simplified allocation process flow is as follows:
 *	- reserve a new block in the transient heap
 *	- prepare the new block
 *	- create redo log of required modifications
 *		- chunk metadata
 *		- offset of the new object
 *	- commit and process the redo log
 *
 * And similarly, the deallocation process:
 *	- create redo log of required modifications
 *		- reverse the chunk metadata back to the 'free' state
 *		- set the destination of the object offset to zero
 *	- commit and process the redo log
 * There's an important distinction in the deallocation process - it does not
 * return the memory block to the transient container. That is done once no more
 * memory is available.
 *
 * Reallocation is a combination of the above, with one additional step
 * of copying the old content.
 */
int sec_palloc_operation(struct palloc_heap *heap, uint64_t off,
                         uint64_t *dest_off, uint64_t *dest_off_v, size_t size,
                         palloc_constr constructor, void *arg,
                         uint64_t extra_field, uint16_t object_flags,
                         uint16_t class_id, uint16_t arena_id,
                         struct operation_context *ctx,
                         uint64_t value_to_update) {
  LOG(10, NULL);
  size_t user_size = 0;

  size_t nops = 0;
  struct pobj_action_internal ops[2];
  struct pobj_action_internal *alloc = NULL;
  struct pobj_action_internal *dealloc = NULL;

  /*
   * The offset of an existing block can be nonzero which means this
   * operation is either free or a realloc - either way the offset of the
   * object needs to be translated into memory block, which is a structure
   * that all of the heap methods expect.
   */
  if (off != 0) {
    dealloc = &ops[nops++];
    sec_palloc_defer_free_create(heap, off, dealloc);
    user_size = dealloc->m.m_ops->get_user_size(&dealloc->m);
    if (user_size == size) {
      operation_cancel(ctx);
      return 0;
    }
  }

  /* alloc or realloc */
  if (size != 0) {
    alloc = &ops[nops++];
    if (sec_palloc_reservation_create(heap, size, constructor, arg, extra_field,
                                      object_flags, class_id, arena_id,
                                      alloc) != 0) {
      operation_cancel(ctx);
      return -1;
    }
  }

  /* realloc */
  if (alloc != NULL && dealloc != NULL) {
    /* copy data to newly allocated memory */
    size_t old_size = user_size;
    size_t to_cpy = old_size > size ? size : old_size;
    VALGRIND_ADD_TO_TX(HEAP_OFF_TO_PTR(heap, alloc->offset), to_cpy);
    pmemops_memcpy(&heap->p_ops, HEAP_OFF_TO_PTR(heap, alloc->offset),
                   HEAP_OFF_TO_PTR(heap, off), to_cpy, 0);
    VALGRIND_REMOVE_FROM_TX(HEAP_OFF_TO_PTR(heap, alloc->offset), to_cpy);
  }

  /*
   * If the caller provided a destination value to update, it needs to be
   * modified atomically alongside the heap metadata, and so the operation
   * context must be used.
   */
  if (dest_off) {
    if (value_to_update != 0) {
      sec_operation_add_entry(ctx, dest_off, value_to_update,
                              ULOG_OPERATION_SET);
    } else {
      sec_operation_add_entry(ctx, dest_off, alloc ? alloc->offset : 0,
                              ULOG_OPERATION_SET);
    }
  }
  if (dest_off_v) {
    *dest_off_v = alloc ? alloc->offset : 0;
  }

  /* and now actually perform the requested operation! */
  /* one shot operation */
  sec_palloc_exec_actions(heap, ctx, ops, nops);

  return 0;
}

/*
 * Prepares the object that is going to be modified with the PMEMoid of the
 * newly allocated object The steps are: 1.Decrypt the current object in
 * volatile memory 2.Modify its value in the place where PMEMoid will be stored
 * 3.Encrypt and get the new tag
 * 4.Add operation entries to perform the update with the allocation atomically
 */
uint8_t *sec_alloc_prepare_object_id_update(PMEMobjpool *pop,
                                            uint64_t new_value,
                                            PMEMoid *object_id,
                                            uint64_t internal_obj_off,
                                            struct operation_context *ctx) {
  // Read & modify the object
  uint64_t object_size;
  void *modified_object = sobj_read(pop, *object_id, 1, &object_size);
  ((PMEMoid *)((uintptr_t)modified_object + internal_obj_off))->pool_uuid_lo =
      pop->uuid_lo;
  ((PMEMoid *)((uintptr_t)modified_object + internal_obj_off))->off = new_value;

  // encrypt the object
  uint8_t *ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = object_id->pool_uuid_lo;
  iv[1] = object_id->off;
  ciphertext = encrypt_final(modified_object, (size_t)object_size, tag, NULL, 0,
                             (uint8_t *)iv);

  // append its entry into the redo list
  add_redo_object_to_modification_list(ctx, *object_id, tag, object_size);

  free(modified_object);
  return ciphertext;
}

/*
 * Prepares the object that is going to be modified with the zeroed PMEMoid of
 * the freed object The steps are: 1.Decrypt the current object in volatile
 * memory 2.Modify its value in the place where zeroed PMEMoid will be stored
 * 3.Encrypt and get the new tag
 * 4.Add operation entries to perform the update with the allocation atomically
 */
uint8_t *sec_free_prepare_object_id_update(PMEMobjpool *pop, uint64_t new_value,
                                           PMEMoid *object_id,
                                           uint64_t internal_obj_off,
                                           struct operation_context *ctx) {
  // Read & modify the object
  uint64_t object_size;
  void *modified_object = sobj_read(pop, *object_id, 1, &object_size);
  ((PMEMoid *)((uintptr_t)modified_object + internal_obj_off))->pool_uuid_lo =
      0;  // zero out
  ((PMEMoid *)((uintptr_t)modified_object + internal_obj_off))->off =
      new_value;  // will always be zero
  ASSERT(new_value == 0);

  // encrypt the object
  uint8_t *ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = object_id->pool_uuid_lo;
  iv[1] = object_id->off;
  ciphertext = encrypt_final(modified_object, (size_t)object_size, tag, NULL, 0,
                             (uint8_t *)iv);

  // append its entry into the redo list
  add_redo_object_to_modification_list(ctx, *object_id, tag, object_size);

  free(modified_object);
  return ciphertext;
}

/*
 * Prepares the pool header that is going to be modified with the size & offset
 * of the root The steps are: 1.Modify the volatile state of the pool header in
 * order to have the correct values 2.Encrypt and get the new tag 3.Add
 * operation entries to perform the update with the allocation atomically
 */
uint8_t *sec_alloc_root_prepare_pool_header(PMEMobjpool *pop,
                                            uint64_t new_value, uint64_t size,
                                            struct operation_context *ctx) {
  // Modify the volatile state of the pool header
  uint64_t restore_root_offset = pop->root_offset;
  uint64_t restore_root_size = pop->root_size;

  pop->root_offset = new_value;
  pop->root_size = size;

  // encrypt the object
  uint8_t *ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  memset(iv, 0, IV_SIZE);
  ciphertext = encrypt_final((uint8_t *)pop, PMEM_OBJ_POOL_NON_RUN_TIME_SIZE,
                             tag, NULL, 0, (uint8_t *)iv);

  // append its entry into the redo list
  add_redo_object_to_modification_list(ctx, (PMEMoid){0, 0}, tag,
                                       PMEM_OBJ_POOL_NON_RUN_TIME_SIZE);

  pop->root_offset = restore_root_offset;
  pop->root_size = restore_root_size;

  return ciphertext;
}

/*
 * sec_alloc_prepare_new_object: appends object signature for non-transactional
 * allocations in the redo modification list. It will be applied along with the
 * redo bitmap operations referring to the memory block allocation dedicated to
 * this object.
 */
int sec_alloc_prepare_new_object(uint64_t pool_id, uint64_t obj_off,
                                 uint64_t size, struct operation_context *ctx,
                                 uint64_t flags) {
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pool_id;
  iv[1] = obj_off;
  void *pmem_pointer = sec_pmemobj_direct((PMEMoid){pool_id, obj_off});

  encrypt_final_direct(pmem_pointer, (size_t)size, tag, NULL, 0, (uint8_t *)iv,
                       pmem_pointer);

  add_redo_object_to_modification_list(ctx, (PMEMoid){pool_id, obj_off}, tag,
                                       size);

  return 1;
}

/*
 * sec_realloc_prepare_new_object: calculates and appends reallocated object
 * signature for non-transactional allocations in the redo modification list. It
 * will be applied along with the redo bitmap operations referring to the new
 * memory block allocation dedicated to this object. Returns the new ciphertext
 * in order be copied to the new object position.
 */
int sec_realloc_prepare_new_object(PMEMobjpool *pop, uint64_t new_obj_off,
                                   uint64_t new_size, PMEMoid old_obj_id,
                                   uint64_t old_size,
                                   struct operation_context *ctx,
                                   uint64_t flags) {
  // read the actual object data
  uint8_t *old_data = sobj_read(pop, old_obj_id, 1, NULL);

  uint8_t *ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pop->uuid_lo;
  iv[1] = new_obj_off;
  void *new_pmem_pointer =
      sec_pmemobj_direct((PMEMoid){pop->uuid_lo, new_obj_off});

  old_data = realloc(old_data, new_size);
  // pad the ciphertext with the content of the PM after this point
  if (new_size > old_size) {
    memcpy((void *)((uintptr_t)old_data + old_size),
           (void *)((uintptr_t)new_pmem_pointer + old_size),
           new_size - old_size);
  }
  ciphertext = encrypt_final(
      old_data, (size_t)new_size, tag, NULL, 0,
      (uint8_t *)iv);  // encrypt the old data with the new allocated space

  add_redo_object_to_modification_list(ctx,
                                       (PMEMoid){pop->uuid_lo, new_obj_off},
                                       tag, new_size);  // add the new entry

  memset(tag, 0, HMAC_SIZE);
  add_redo_object_to_modification_list(ctx, old_obj_id, tag,
                                       0);  // remove the previous one

  VALGRIND_ADD_TO_TX(new_pmem_pointer, new_size);
  pmem_memcpy(new_pmem_pointer, ciphertext, new_size, 0);
  VALGRIND_REMOVE_FROM_TX(new_pmem_pointer, new_size);

  free(old_data);
  free(ciphertext);

  return 1;
}

/*
 * sec_free_remove_object: appends object signature for non-transactional
 * deallocations in the redo modification list. It will be applied along with
 * the redo bitmap operations referring to the memory block deallocation
 * dedicated to this object.
 */
int sec_free_remove_object(uint64_t pool_id, uint64_t obj_off,
                           struct operation_context *ctx) {
  add_redo_object_to_modification_list(ctx, (PMEMoid){pool_id, obj_off}, NULL,
                                       0);
  return 1;
}

/*
 * Prepares the pool header that is going to be modified with the size & offset
 * of the root The steps are: 1.Modify the volatile state of the pool header in
 * order to have the correct values 2.Encrypt and get the new tag 3.Add
 * operation entries to perform the update with the allocation atomically
 */
uint8_t *sec_extend_prepare_ulog_header(PMEMobjpool *pop, uint64_t new_value,
                                        struct ulog *ulog_v,
                                        uint64_t ulog_offset,
                                        struct operation_context *ctx) {
  // Modify the volatile state of the pool header
  ulog_v->next = new_value;

  // encrypt the object
  uint8_t *ciphertext;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = pop->uuid_lo;
  iv[1] = ulog_offset;
  ciphertext = encrypt_final((uint8_t *)ulog_v, sizeof(struct ulog), tag, NULL,
                             0, (uint8_t *)iv);

  // append its entry into the redo list
  append_undo_object_entry((PMEMoid){pop->uuid_lo, ulog_offset}, tag,
                           OBJ_NLANES, sizeof(struct ulog), 0, 1);

  return ciphertext;
}

int sec_palloc_operation_init(struct palloc_heap *heap, uint64_t off,
                              size_t size, palloc_constr constructor, void *arg,
                              uint64_t extra_field, uint16_t object_flags,
                              uint16_t class_id, uint16_t arena_id,
                              struct operation_context *ctx, size_t *nops,
                              uint64_t *offset, void **ops) {
  LOG(10, NULL);
  size_t user_size = 0;

  size_t nops_internal = 0;
  struct pobj_action_internal *internal_ops =
      (struct pobj_action_internal *)malloc(
          2 * sizeof(struct pobj_action_internal));
  struct pobj_action_internal *alloc = NULL;
  struct pobj_action_internal *dealloc = NULL;

  /*
   * The offset of an existing block can be nonzero which means this
   * operation is either free or a realloc - either way the offset of the
   * object needs to be translated into memory block, which is a structure
   * that all of the heap methods expect.
   */
  if (off != 0) {
    dealloc = &internal_ops[nops_internal++];
    sec_palloc_defer_free_create(heap, off, dealloc);
    user_size = dealloc->m.m_ops->get_user_size(&dealloc->m);
    if (user_size == size) {  // if size == user_size -> no modification needed
      *nops = 0;
      free(internal_ops);
      operation_cancel(ctx);
      return 0;
    }
  }

  /* alloc or realloc */
  if (size != 0) {
    alloc = &internal_ops[nops_internal++];
    if (sec_palloc_reservation_create(heap, size, constructor, arg, extra_field,
                                      object_flags, class_id, arena_id,
                                      alloc) != 0) {
      *nops = 0;
      free(internal_ops);
      operation_cancel(ctx);
      return -1;
    }
  }

  *offset = alloc ? alloc->offset : 0;
  *nops = nops_internal;
  *ops = (void *)internal_ops;

  return 0;
}

int sec_palloc_operation_exec(struct palloc_heap *heap, uint64_t *dest_off,
                              size_t nops, void *ops,
                              struct operation_context *ctx,
                              uint64_t value_to_update) {
  LOG(10, NULL);
  struct pobj_action_internal *internal_ops =
      (struct pobj_action_internal *)ops;
  /*
   * If the caller provided a destination value to update, it needs to be
   * modified atomically alongside the heap metadata, and so the operation
   * context must be used.
   */
  if (dest_off) {
    sec_operation_add_entry(ctx, dest_off, value_to_update, ULOG_OPERATION_SET);
  }

  /* and now actually perform the requested operation! */
  /* one shot operation */

  sec_palloc_exec_actions(heap, ctx, internal_ops, nops);
  free(internal_ops);

  return 0;
}

/*
 * sec_palloc_heap_check -- verifies secure heap state
 */
int sec_palloc_heap_check(void *heap_start, uint64_t heap_size,
                          uint64_t heap_off) {
  return sec_heap_check(heap_start, heap_size, heap_off);
}

/*
 * sec_palloc_init -- initializes palloc heap
 */
int sec_palloc_init(void *heap_start, uint64_t heap_size, uint64_t *sizep,
                    struct pmem_ops *p_ops, uint64_t heap_off) {
  return sec_heap_init(heap_start, heap_size, sizep, p_ops, heap_off);
}
#endif