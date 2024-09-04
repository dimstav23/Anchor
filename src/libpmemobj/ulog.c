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
 * ulog.c -- unified log implementation
 */
#ifdef ANCHOR_FUNCS
#include <openssl/conf.h>         //ANCHOR
#include <openssl/err.h>          //ANCHOR
#include <openssl/evp.h>          //ANCHOR
#include <stdio.h>                //ANCHOR
#include "manifest_operations.h"  //ANCHOR
#include "metadata_log.h"         //ANCHOR
#include "metadata_operations.h"  //ANCHOR
#include "openssl_gcm_encrypt.h"  //ANCHOR
#include "user_operations.h"      //ANCHOR
extern struct temp_list_head temp_redo_list[OBJ_NLANES_MANIFEST];
extern struct temp_list_head temp_ulog_list[OBJ_NLANES_MANIFEST];
#ifdef STATISTICS
#include "internal_statistics.h"
#endif
#endif

#include <inttypes.h>
#include <string.h>

#include "libpmemobj.h"
#include "obj.h"
#include "out.h"
#include "tx.h"
#include "ulog.h"
#include "util.h"
#include "valgrind_internal.h"

/*
 * Operation flag at the three most significant bits
 */
#define ULOG_OPERATION(op) ((uint64_t)(op))
#define ULOG_OPERATION_MASK ((uint64_t)(0b111ULL << 61ULL))
#define ULOG_OPERATION_FROM_OFFSET(off) \
  (ulog_operation_type)((off)&ULOG_OPERATION_MASK)
#define ULOG_OFFSET_MASK (~(ULOG_OPERATION_MASK))

#define CACHELINE_ALIGN(size) ALIGN_UP(size, CACHELINE_SIZE)
#define IS_CACHELINE_ALIGNED(ptr) \
  (((uintptr_t)(ptr) & (CACHELINE_SIZE - 1)) == 0)

/*
 * ulog_by_offset -- calculates the ulog pointer
 */
struct ulog *ulog_by_offset(size_t offset, const struct pmem_ops *p_ops) {
  if (offset == 0) return NULL;

  size_t aligned_offset = CACHELINE_ALIGN(offset);

  return (struct ulog *)((char *)p_ops->base + aligned_offset);
}

/*
 * ulog_operation -- returns the type of entry operation
 */
ulog_operation_type ulog_entry_type(const struct ulog_entry_base *entry) {
  return ULOG_OPERATION_FROM_OFFSET(entry->offset);
}

/*
 * ulog_offset -- returns offset
 */
uint64_t ulog_entry_offset(const struct ulog_entry_base *entry) {
  return entry->offset & ULOG_OFFSET_MASK;
}

/*
 * ulog_next -- retrieves the pointer to the next ulog
 */
struct ulog *ulog_next(struct ulog *ulog, const struct pmem_ops *p_ops) {
  return ulog_by_offset(ulog->next, p_ops);
}

/*
 * ulog_entry_size -- returns the size of a ulog entry
 */
size_t ulog_entry_size(const struct ulog_entry_base *entry) {
  LOG(11, "%p", entry);
  struct ulog_entry_buf *eb;

  switch (ulog_entry_type(entry)) {
    case ULOG_OPERATION_AND:
    case ULOG_OPERATION_OR:
    case ULOG_OPERATION_SET:
      // printf("Redo entry\n");
      return sizeof(struct ulog_entry_val);
    case ULOG_OPERATION_BUF_SET:
    case ULOG_OPERATION_BUF_CPY: {
      // printf("Undo entry\n");
      eb = (struct ulog_entry_buf *)entry;
      LOG(11, "%ld", eb->size);
      return CACHELINE_ALIGN(sizeof(struct ulog_entry_buf) + eb->size);
    }
    default:
      ASSERT(0);
  }
  return 0;
}

/*
 * ulog_entry_valid -- (internal) checks if a ulog entry is valid
 * Returns 1 if the range is valid, otherwise 0 is returned.
 */
int ulog_entry_valid(struct ulog *ulog, const struct ulog_entry_base *entry) {
  LOG(16, "%p", entry);

  if (entry->offset == 0) return 0;

  size_t size;
  struct ulog_entry_buf *b;  // = (struct ulog_entry_buf *)entry;
  switch (ulog_entry_type(entry)) {
    case ULOG_OPERATION_BUF_CPY:
    case ULOG_OPERATION_BUF_SET:
      LOG(15, "Undo Entry");
      // printf("Undo entry\n");
      size = ulog_entry_size(entry);  // entry_size does the verification again
      b = (struct ulog_entry_buf *)entry;
      // printf("size valid: %ld\n",size);
      b->entry_sign[0] = 0;
      b->entry_sign[1] = 0;
      uint64_t csum =
          util_checksum_compute(b, ULOG_BUF_HDR_SIZE, &b->checksum, 0);
      csum = util_checksum_seq(((struct ulog_entry_buf *)b)->data,
                               size - ULOG_BUF_HDR_SIZE, csum);
      csum = util_checksum_seq(&ulog->gen_num, sizeof(ulog->gen_num), csum);
      LOG(11, "csum check : %ld %ld gen num: %ld", b->checksum, csum,
          ulog->gen_num);

      if (b->checksum != csum) {
        return 0;
      }
      break;
    default:
      break;
  }
  return 1;
}

/*
 * ulog_entry_apply -- applies modifications of a single ulog entry
 */
void ulog_entry_apply(const struct ulog_entry_base *e, int persist,
                      const struct pmem_ops *p_ops) {
  LOG(10, "entry offset : %p", e);
  ulog_operation_type t = ulog_entry_type(e);
  uint64_t offset = ulog_entry_offset(e);
  LOG(10, "offset : %ld type : %ld", offset, t);
  size_t dst_size = sizeof(uint64_t);
  uint64_t *dst = (uint64_t *)((uintptr_t)p_ops->base + offset);

  struct ulog_entry_val *ev;
  struct ulog_entry_buf *eb;

  flush_fn f = persist ? p_ops->persist : p_ops->flush;

  switch (t) {
    case ULOG_OPERATION_AND:
      LOG(11, "REDO AND entry");
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      *dst &= ev->value;
      f(p_ops->base, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_OR:
      LOG(11, "REDO OR entry");
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      *dst |= ev->value;
      f(p_ops->base, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_SET:
      LOG(11, "REDO SET entry");
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      *dst = ev->value;
      f(p_ops->base, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_BUF_SET:
      eb = (struct ulog_entry_buf *)e;
      LOG(10, "APPLY BUF SET");
      dst_size = eb->size;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      pmemops_memcpy(p_ops, dst, eb->data, eb->size,
                     PMEMOBJ_F_RELAXED | PMEMOBJ_F_MEM_NODRAIN);
      break;
    case ULOG_OPERATION_BUF_CPY:
      eb = (struct ulog_entry_buf *)e;
      LOG(10, "APPLY BUF CPY");
      dst_size = eb->size;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      pmemops_memcpy(p_ops, dst, eb->data, eb->size,
                     PMEMOBJ_F_RELAXED | PMEMOBJ_F_MEM_NODRAIN);
      break;
    default:
      ASSERT(0);
  }
  VALGRIND_REMOVE_FROM_TX(dst, dst_size);
}

/*
 * ulog_foreach_entry -- iterates over every existing entry in the ulog
 */
int ulog_foreach_entry(struct ulog *ulog, ulog_entry_cb cb, void *arg,
                       const struct pmem_ops *ops) {
  LOG(11, "Ulog address : %p", ulog);
  struct ulog_entry_base *e;
  int ret = 0;
  for (struct ulog *r = ulog; r != NULL; r = ulog_next(r, ops)) {
    for (size_t offset = 0; offset < r->capacity;) {
      e = (struct ulog_entry_base *)(r->data + offset);

      if (!ulog_entry_valid(ulog, e)) return ret;

      if ((ret = cb(e, arg, ops)) != 0) return ret;

      offset += ulog_entry_size(e);
    }
  }

  return ret;
}

/*
 * ulog_check_entry --
 *	(internal) checks consistency of a single ulog entry
 */
static int ulog_check_entry(struct ulog_entry_base *e, void *arg,
                            const struct pmem_ops *p_ops) {
  LOG(10, "entry offset : %p", e);
  uint64_t offset = ulog_entry_offset(e);
  ulog_check_offset_fn check = arg;

  if (!check(p_ops->base, offset)) {
    LOG(15, "ulog %p invalid offset %" PRIu64, e, offset);
    return -1;
  }
  return offset == 0 ? -1 : 0;
}

/*
 * ulog_base_nbytes -- (internal) counts the actual of number of bytes
 *	occupied by the ulog
 */
size_t ulog_base_nbytes(struct ulog *ulog) {
  size_t offset = 0;
  struct ulog_entry_base *e;
  for (offset = 0; offset < ulog->capacity;) {
    e = (struct ulog_entry_base *)(ulog->data + offset);
    if (!ulog_entry_valid(ulog, e)) break;
    LOG(11, "entry : %p size : %ld", e, ulog_entry_size(e));
    offset += ulog_entry_size(e);
  }

  return offset;
}

/*
 * ulog_entry_val_create -- creates a new log value entry in the ulog
 *
 * This function requires at least a cacheline of space to be available in the
 * ulog.
 */
struct ulog_entry_val *ulog_entry_val_create(struct ulog *ulog, size_t offset,
                                             uint64_t *dest, uint64_t value,
                                             ulog_operation_type type,
                                             const struct pmem_ops *p_ops) {
  LOG(15, NULL);
  struct ulog_entry_val *e = (struct ulog_entry_val *)(ulog->data + offset);

  struct {
    struct ulog_entry_val v;
    struct ulog_entry_base zeroes;
  } data;
  COMPILE_ERROR_ON(sizeof(data) != sizeof(data.v) + sizeof(data.zeroes));

  /*
   * Write a little bit more to the buffer so that the next entry that
   * resides in the log is erased. This will prevent leftovers from
   * a previous, clobbered, log from being incorrectly applied.
   */
  data.zeroes.offset = 0;
  data.v.base.offset = (uint64_t)(dest) - (uint64_t)p_ops->base;
  data.v.base.offset |= ULOG_OPERATION(type);
  data.v.value = value;
  data.v.tcv = 0;
  data.v.bitmap_offset = 0;

  pmemops_memcpy(p_ops, e, &data, sizeof(data),
                 PMEMOBJ_F_MEM_NOFLUSH | PMEMOBJ_F_RELAXED);
  return e;
}

/*
 * ulog_capacity -- (internal) returns the total capacity of the ulog
 */
size_t ulog_capacity(struct ulog *ulog, size_t ulog_base_bytes,
                     const struct pmem_ops *p_ops) {
  size_t capacity = ulog_base_bytes;
  // LOG(11,NULL);
  /* skip the first one, we count it in 'ulog_base_bytes' */
  while ((ulog = ulog_next(ulog, p_ops)) != NULL) {
    capacity += ulog->capacity;
  }

  return capacity;
}

/*
 * ulog_rebuild_next_vec -- rebuilds the vector of next entries
 */
void ulog_rebuild_next_vec(struct ulog *ulog, struct ulog_next *next,
                           const struct pmem_ops *p_ops) {
  do {
    if (ulog->next != 0) VEC_PUSH_BACK(next, ulog->next);
  } while ((ulog = ulog_next(ulog, p_ops)) != NULL);
}

/*
 * ulog_reserve -- reserves new capacity in the ulog
 */
int ulog_reserve(struct ulog *ulog, size_t ulog_base_nbytes, size_t gen_num,
                 int auto_reserve, size_t *new_capacity, ulog_extend_fn extend,
                 struct ulog_next *next, const struct pmem_ops *p_ops) {
  if (!auto_reserve) {
    LOG(1, "cannot auto reserve next ulog");
    return -1;
  }

  int is_first_log = 0;
  size_t capacity = ulog_base_nbytes;

  uint64_t offset;
  VEC_FOREACH(offset, next) {
    ulog = ulog_by_offset(offset, p_ops);
    ASSERTne(ulog, NULL);
    capacity += ulog->capacity;
  }

  while (capacity < *new_capacity) {
#ifdef ANCHOR_FUNCS
    if (extend(p_ops->base, &ulog->next, gen_num, NULL, is_first_log, 0) != 0)
#else
    if (extend(p_ops->base, &ulog->next, gen_num) != 0)
#endif
      return -1;
    VEC_PUSH_BACK(next, ulog->next);
    ulog = ulog_next(ulog, p_ops);
    ASSERTne(ulog, NULL);

    capacity += ulog->capacity;
  }
  *new_capacity = capacity;

  return 0;
}

#ifdef ANCHOR_FUNCS
struct ulog *ulog_hdr_decrypt(struct ulog *ulog, const struct pmem_ops *p_ops) {
  uint64_t pool_uuid_lo =
      ((PMEMobjpool *)(p_ops->base_v))->uuid_lo == 0
          ? pmemobj_get_uuid_lo((PMEMobjpool *)(p_ops->base_v))
          : ((PMEMobjpool *)(p_ops->base_v))->uuid_lo;

  return (struct ulog *)spool_metadata_read_cached(
      pool_uuid_lo, ulog, (uintptr_t)ulog - (uintptr_t)p_ops->base, NULL, 1);
}

struct ulog *ulog_hdr_encrypt(struct ulog *ulog, uint8_t *iv, uint8_t *tag) {
  return (struct ulog *)encrypt_final(
      (uint8_t *)ulog, sizeof(struct ulog), tag, NULL, 0,
      iv);  // for the header of the entry to be encrypted
}

/*
 * sec_ulog_next -- retrieves the pointer to the next ulog
 */
struct ulog *sec_ulog_next(struct ulog *ulog, const struct pmem_ops *p_ops) {
  struct ulog *ulog_v = ulog_hdr_decrypt(ulog, p_ops);
  if (ulog_v == NULL) return NULL;
  struct ulog *ret = ulog_by_offset(ulog_v->next, p_ops);
  return ret;
}

int sec_ulog_foreach_entry_shadow_log(struct ulog *ulog,
                                      ulog_entry_cb_shadow_log cb, void *arg,
                                      const struct pmem_ops *ops) {
  LOG(11, "Ulog address : %p", ulog);
  struct ulog_entry_base *e;
  int ret = 0;
  for (struct ulog *r = ulog; r != NULL;) {
    for (size_t offset = 0; offset < r->capacity;) {
      e = (struct ulog_entry_base *)(r->data + offset);

      if (!ulog_entry_valid(ulog, e)) return ret;

      if ((ret = cb(e, arg, ops, r->tx_lane_id)) != 0) return ret;

      offset += ulog_entry_size(e);
    }
    if (r->next != 0) {
      r = ulog_next(r, ops);
      struct ulog *ulog_v = ulog_hdr_decrypt(r, ops);
      ASSERT(ulog_v != NULL);
    } else {
      break;
    }
  }
  return ret;
}

/*
 * sec_ulog_entry_size -- returns the size of a ulog entry
 */
size_t sec_ulog_entry_size(const struct ulog_entry_base *entry,
                           const struct pmem_ops *p_ops) {
  LOG(11, "%p", entry);
  struct ulog_entry_val *eb = (struct ulog_entry_val *)entry;

  if (eb->size == UINT64_MAX)
    return sizeof(struct ulog_entry_val);
  else
    return CACHELINE_ALIGN(sizeof(struct ulog_entry_buf) + eb->size);
}

/*
 * sec_ulog_entry_valid -- (internal) checks if a ulog entry is valid
 * Returns 1 if the range is valid, otherwise 0 is returned.
 */
static int sec_ulog_entry_valid(struct ulog *ulog,
                                const struct ulog_entry_base *entry,
                                const struct pmem_ops *p_ops) {
  LOG(16, "%p", entry);

  if (entry->offset == 0) return 0;

  struct ulog_entry_base *decr_entry =
      (struct ulog_entry_base *)sec_ulog_entry_verify(entry, p_ops);

  size_t size;
  struct ulog_entry_buf *b;
  switch (ulog_entry_type(decr_entry)) {
    case ULOG_OPERATION_BUF_CPY:
    case ULOG_OPERATION_BUF_SET:
      LOG(15, "Undo Entry");
      size = sec_ulog_entry_size(entry, p_ops);
      b = (struct ulog_entry_buf *)decr_entry;
      b->entry_sign[0] = 0;
      b->entry_sign[1] = 0;
      uint64_t csum =
          util_checksum_compute(b, ULOG_BUF_HDR_SIZE, &b->checksum, 0);
      csum = util_checksum_seq(((struct ulog_entry_buf *)b)->data,
                               size - ULOG_BUF_HDR_SIZE, csum);
      // ANCHOR : decrypt ulog structure!
      struct ulog *ulog_v = ulog_hdr_decrypt(ulog, p_ops);
      csum = util_checksum_seq(&ulog_v->gen_num, sizeof(ulog->gen_num), csum);
      if (b->checksum != csum) {
        free(decr_entry);
        return 0;
      }
      break;
    default:
      break;
  }
  free(decr_entry);
  return 1;
}

/*
 * sec_ulog_foreach_entry -- iterates over every existing entry in the ulog
 */
int sec_ulog_foreach_entry(struct ulog *ulog, ulog_entry_cb cb, void *arg,
                           const struct pmem_ops *ops) {
  LOG(11, "Ulog address : %p", ulog);
  struct ulog_entry_base *e;
  int ret = 0;
  size_t capacity = 0;
  for (struct ulog *r = ulog; r != NULL; r = sec_ulog_next(r, ops)) {
    // ANCHOR : decrypt ulog structure! r should maintain the decrypted values!
    // The volatile logs are unencrypted so they do not need decryption!
    if (SEC_OBJ_PTR_FROM_POOL(ops->base, ops->base_v, r)) {
      struct ulog *ulog_v = ulog_hdr_decrypt(r, ops);
      capacity = ulog_v->capacity;
    } else {
      capacity = r->capacity;
    }

    for (size_t offset = 0; offset < /*r->*/ capacity;) {
      e = (struct ulog_entry_base *)(r->data + offset);

      if (!sec_ulog_entry_valid(ulog, e, ops)) return ret;

      if ((ret = cb(e, arg, ops)) != 0) return ret;

      offset += sec_ulog_entry_size(e, ops);
    }
  }

  return ret;
}

/*
 * ulog_fetch_each_entry -- iterates over every existing entry in the ulog
 */
struct ulog_vol_entries *ulog_fetch_each_entry(struct ulog *ulog,
                                               sec_ulog_counter_cb cb,
                                               void *arg,
                                               const struct pmem_ops *ops,
                                               struct Counter *tcv,
                                               uint64_t starting_counter) {
  LOG(11, "Ulog address : %p", ulog);
  struct ulog_entry_base *e;
  int ret = 0;
  size_t capacity = 0;
  uint64_t expected_counter =
      starting_counter;  // ANCHOR: UPDATE LOG COUNTER
                         // initial expected counter value, found in files,
                         // updated when log is deleted!
  struct ulog_vol_entries *head = NULL;
  struct ulog_vol_entries *curr = NULL;
  struct ulog *ulog_v = NULL;
  for (struct ulog *r = ulog; r != NULL; /* r = sec_ulog_next(r, ops)*/) {
    // ANCHOR : decrypt ulog structure! r should maintain the decrypted values!
    // The volatile logs are unencrypted so they do not need decryption!
    ulog_v = SEC_OBJ_PTR_FROM_POOL(ops->base, ops->base_v, r)
                 ? ulog_hdr_decrypt(r, ops)
                 : r;
    capacity = ulog_v->capacity;

    for (size_t offset = 0; offset < /*r->*/ capacity;) {
      e = (struct ulog_entry_base *)(r->data + offset);

      if (e->offset == 0) {  // termination condition
        goto end;
      }

      e = (struct ulog_entry_base *)sec_ulog_entry_verify(e, ops);

      if (!ulog_entry_valid(ulog_v, e)) {
        sec_ulog_free_entries(head);
        return NULL;
      }

      if (!(ret = cb(e, arg, expected_counter))) {
        sec_ulog_free_entries(head);
        return NULL;
      }

      offset += ulog_entry_size(e);

      if (head == NULL) {
        head =
            (struct ulog_vol_entries *)malloc(sizeof(struct ulog_vol_entries));
        head->ulog_entry = e;
        head->next = NULL;
        curr = head;
      } else {
        curr->next =
            (struct ulog_vol_entries *)malloc(sizeof(struct ulog_vol_entries));
        curr->next->ulog_entry = e;
        curr->next->next = NULL;
        curr = curr->next;
      }

      expected_counter++;
    }

    if (ulog_v != r) {
      r = ulog_next(ulog_v, ops);
    } else {
      r = ulog_next(ulog_v, ops);
    }
  }

end:
  if (expected_counter != tcv->_counter) {
    sec_ulog_free_entries(head);
    return NULL;
  }
  return head;
}

/*
 * ulog_fetch_each_entry_redo -- iterates over every existing entry in the redo
 * ulog
 */
struct ulog_vol_entries *ulog_fetch_each_entry_redo(struct ulog *ulog,
                                                    sec_ulog_counter_cb cb,
                                                    void *arg,
                                                    const struct pmem_ops *ops,
                                                    struct Counter *tcv,
                                                    uint64_t starting_counter) {
  LOG(11, "Ulog address : %p", ulog);
  struct ulog_entry_base *e;
  int ret = 0;
  size_t capacity = 0;
  uint64_t expected_counter =
      starting_counter;  // ANCHOR: UPDATE LOG COUNTER
                         // initial expected counter value, found in files,
                         // updated when log is deleted!
  struct ulog_vol_entries *head = NULL;
  struct ulog_vol_entries *curr = NULL;
  struct ulog *ulog_v = NULL;
  for (struct ulog *r = ulog; r != NULL; /* r = sec_ulog_next(r, ops)*/) {
    // ANCHOR : decrypt ulog structure! r should maintain the decrypted values!
    // The volatile logs are unencrypted so they do not need decryption!
    ulog_v = SEC_OBJ_PTR_FROM_POOL(ops->base, ops->base_v, r)
                 ? ulog_hdr_decrypt(r, ops)
                 : r;
    capacity = ulog_v->capacity;
    uint64_t iv[IV_SIZE_UINT64];
    iv[0] = ((PMEMobjpool *)(ops->base_v))->uuid_lo;
    iv[1] = (uintptr_t)r - (uintptr_t)(ops->base);
    uint8_t *ulog_data =
        decrypt_final(r->data, ulog_v->data_size, (uint8_t *)ulog_v->data_tag,
                      NULL, 0, (uint8_t *)iv);

    for (size_t offset = 0; offset < /*r->*/ capacity;) {
      e = (struct ulog_entry_base *)(ulog_data + offset);

      if (e->offset == 0) {  // termination condition
        break;
      }

      if (!ulog_entry_valid(ulog_v, e)) {
        sec_ulog_free_entries(head);
        return NULL;
      }

      if (!(ret = cb(e, arg, expected_counter))) {
        sec_ulog_free_entries(head);
        return NULL;
      }

      offset += ulog_entry_size(e);

      expected_counter++;
    }

    if (head == NULL) {
      head = (struct ulog_vol_entries *)malloc(sizeof(struct ulog_vol_entries));
      head->ulog_entry = ulog_data;
      head->next = NULL;
      curr = head;
    } else {
      curr->next =
          (struct ulog_vol_entries *)malloc(sizeof(struct ulog_vol_entries));
      curr->next->ulog_entry = ulog_data;
      curr->next->next = NULL;
      curr = curr->next;
    }

    if (ulog_v->next != 0) {
      r = ulog_next(ulog_v, ops);
      free(ulog_v);
    } else {
      free(ulog_v);
      break;
    }
  }

  if (expected_counter != tcv->_counter) {
    sec_ulog_free_entries(head);
    return NULL;
  }
  return head;
}

/*
 * sec_ulog_capacity -- (internal) returns the total capacity of the ulog
 */
size_t sec_ulog_capacity(struct ulog *ulog, size_t ulog_base_bytes,
                         const struct pmem_ops *p_ops) {
  size_t capacity = ulog_base_bytes;
  struct ulog *ulog_v;
  // LOG(11,NULL);
  /* skip the first one, we count it in 'ulog_base_bytes' */
  while ((ulog = sec_ulog_next(ulog, p_ops)) != NULL) {
    ulog_v = ulog_hdr_decrypt(ulog, p_ops);
    capacity += ulog_v->capacity;
  }

  return capacity;
}

/*
 * sec_ulog_rebuild_next_vec -- rebuilds the vector of next entries
 */
void sec_ulog_rebuild_next_vec(struct ulog *ulog, struct ulog_next *next,
                               const struct pmem_ops *p_ops) {
  struct ulog *ulog_v;
  do {
    ulog_v = ulog_hdr_decrypt(ulog, p_ops);
    if (ulog_v != NULL && ulog_v->next != 0) VEC_PUSH_BACK(next, ulog_v->next);
  } while ((ulog = sec_ulog_next(ulog, p_ops)) != NULL);
}

/*
 * sec_ulog_entry_val_create -- creates a new log value entry in the ulog
 *
 * This function requires at least a cacheline of space to be available in the
 * ulog.
 */
struct ulog_entry_val *sec_ulog_entry_val_create(
    struct ulog *ulog, size_t offset, uint64_t *dest, uint64_t value,
    ulog_operation_type type, const struct pmem_ops *p_ops, ...) {
  LOG(15, NULL);
  struct ulog_entry_val *e = (struct ulog_entry_val *)(ulog->data + offset);

  struct {
    struct ulog_entry_val v;
    struct ulog_entry_base zeroes;
  } data;
  COMPILE_ERROR_ON(sizeof(data) != sizeof(data.v) + sizeof(data.zeroes));

  /*
   * Write a little bit more to the buffer so that the next entry that
   * resides in the log is erased. This will prevent leftovers from
   * a previous, clobbered, log from being incorrectly applied.
   */
  // ANCHOR : type of redo log might have the bitmap position integrated from
  // sec_run_preparation_hdr function
  ulog_operation_type pure_type = type & ULOG_OPERATION_MASK;
  data.zeroes.offset =
      0;  // for marking the end of a log! if this is the final entry

  data.v.base.offset = (uint64_t)(dest) - (uint64_t)p_ops->base;
  data.v.base.offset |= ULOG_OPERATION(pure_type);
  data.v.value = value;

  va_list args;
  va_start(args, p_ops);
  struct Counter *_tcv = va_arg(args, struct Counter *);
  va_end(args);
  data.v.tcv = _tcv->_counter;
  // ANCHOR : if we have AND/OR operations referring to bitmaps, find the
  // appropriate offset to the start of the bitmap to decrypt/encrypt when
  // applying the logs
  data.v.bitmap_offset =
      (pure_type == ULOG_OPERATION_AND || pure_type == ULOG_OPERATION_OR)
          ? (uintptr_t)dest - (uintptr_t)p_ops->base -
                (type & ~ULOG_OPERATION_MASK) * sizeof(uint64_t)
          : 0;
  data.v.size = UINT64_MAX;

  LOG(13, "Pointer to write the data : %p", e);
  pmemops_memcpy(p_ops, e, &data, sizeof(data),
                 PMEMOBJ_F_MEM_NOFLUSH | PMEMOBJ_F_RELAXED);

  inc(_tcv);

#ifdef WRITE_AMPL
  bytes_written_inc(REDO_LOG_DATA_PMDK, sizeof(struct ulog_entry_val_pmdk) +
                                            sizeof(struct ulog_entry_base));
#endif

  return e;
}

/*
 * ulog_inc_gen_num -- (internal) increments gen num in the ulog
 */
static void ulog_inc_gen_num(struct ulog *ulog, const struct pmem_ops *p_ops,
                             uint64_t tx_lane_id) {
  LOG(10, NULL);
  size_t gns = sizeof(ulog->gen_num);
  VALGRIND_ADD_TO_TX(&ulog->gen_num, gns);

  PMEMoid log_oid = {((PMEMobjpool *)(p_ops->base_v))->uuid_lo,
                     (uintptr_t)ulog - (uintptr_t)(p_ops->base)};
  // ANCHOR : decrypt ulog structure!
  struct ulog *ulog_v = ulog_hdr_decrypt(ulog, p_ops);
  ulog_v->gen_num++;

  uint8_t tag[HMAC_SIZE];
  struct ulog *ulog_encr = ulog_hdr_encrypt(ulog_v, (uint8_t *)&log_oid, tag);

  // ANCHOR: LOG HEADER FIX
  append_undo_object_entry(log_oid, tag, OBJ_NLANES, sizeof(struct ulog), 0, 1);

  // 8 byte atomic update
  pmemops_memcpy(p_ops, &ulog->gen_num, &ulog_encr->gen_num, gns, 0);

  if (p_ops) {
    pmemops_persist(p_ops, &ulog->gen_num, gns);
  } else {
    VALGRIND_SET_CLEAN(&ulog->gen_num, gns);
  }
  free(ulog_encr);
  VALGRIND_REMOVE_FROM_TX(&ulog->gen_num, gns);
}

/*
 * ulog_free_next -- free all ulogs starting from the indicated one.
 * Function returns 1 if any ulog have been freed or unpinned, 0 otherwise.
 */
int ulog_free_next(struct ulog *u, const struct pmem_ops *p_ops,
                   ulog_free_fn ulog_free,
                   ulog_rm_user_buffer_fn user_buff_remove, uint64_t flags) {
  int ret = 0;
  LOG(11, NULL);
  if (u == NULL) return ret;

  VEC(, uint64_t *) ulogs_internal_except_first;
  VEC_INIT(&ulogs_internal_except_first);

  /*
   * last_internal - pointer to a last found ulog allocated
   * internally by the libpmemobj
   */
  // ANCHOR : decrypt ulog structure!
  struct ulog *last_internal_decr = ulog_hdr_decrypt(u, p_ops);
  struct ulog *last_internal = u;
  struct ulog *current;

  // ANCHOR : we don't have ulog user buffers for now, so this loop is not
  // relevant for us
  /* iterate all linked logs and unpin user defined */
  struct ulog *current_decr = NULL;
  while ((flags & ULOG_ANY_USER_BUFFER) && last_internal != NULL &&
         last_internal_decr->next != 0) {
    current = ulog_by_offset(last_internal_decr->next, p_ops);
    /*
     * handle case with user logs one after the other
     * or mixed user and internal logs
     */
    current_decr = ulog_hdr_decrypt(current, p_ops);
    while (current != NULL && (current_decr->flags & ULOG_USER_OWNED)) {
      last_internal_decr->next = current_decr->next;

      uint8_t *ciphertext;
      uint8_t tag[HMAC_SIZE];
      PMEMoid log_entry_oid = {
          ((struct pmemobjpool *)(p_ops->base_v))->uuid_lo,
          (uintptr_t)last_internal - (uintptr_t)(p_ops->base)};
      // for the header of the entry to be encrypted
      ciphertext =
          encrypt_final((uint8_t *)last_internal_decr, sizeof(struct ulog), tag,
                        NULL, 0, (uint8_t *)&log_entry_oid);
      append_undo_object_entry(log_entry_oid, tag, OBJ_NLANES,
                               sizeof(struct ulog), 0, 1);

      pmemops_memcpy(p_ops, &last_internal->next,
                     &((struct ulog *)ciphertext)->next,
                     sizeof(last_internal->next), 0);

      pmemops_persist(p_ops, &last_internal->next, sizeof(last_internal->next));

      user_buff_remove(p_ops->base, current);

      current = ulog_by_offset(last_internal_decr->next, p_ops);

      current_decr = ulog_hdr_decrypt(current, p_ops);

      /* any ulog has been unpinned - set return value to 1 */
      ret = 1;
    }
    last_internal = ulog_by_offset(last_internal_decr->next, p_ops);
    last_internal_decr = ulog_hdr_decrypt(last_internal, p_ops);
  }

  current_decr = ulog_hdr_decrypt(u, p_ops);
  while (current_decr->next != 0) {
    if (VEC_PUSH_BACK(&ulogs_internal_except_first, &u->next) != 0) {
      /* this is fine, it will just use more pmem */
      LOG(1, "unable to free transaction logs memory");
      goto out;
    }
    u = ulog_by_offset(current_decr->next, p_ops);
    current_decr = ulog_hdr_decrypt(u, p_ops);
  }

  /* free non-user defined logs */
  uint64_t *ulog_ptr;
  VEC_FOREACH_REVERSE(ulog_ptr, &ulogs_internal_except_first) {
    ulog_free(p_ops->base_v == NULL ? p_ops->base : p_ops->base_v, ulog_ptr);
    ret = 1;
  }

out:
  VEC_DELETE(&ulogs_internal_except_first);
  return ret;
}

/*
 * ulog_clobber -- zeroes the metadata of the ulog
 */
void ulog_clobber(struct ulog *dest, struct ulog_next *next,
                  const struct pmem_ops *p_ops) {
  LOG(11, "ulog : %p", dest);
  struct ulog empty;
  memset(&empty, 0, sizeof(empty));
  struct ulog *ulog_v = ulog_hdr_decrypt(dest, p_ops);
  if (next != NULL)
    empty.next = VEC_SIZE(next) == 0 ? 0 : VEC_FRONT(next);
  else  // called with next = NULL only on recovery - start-up
    empty.next = ulog_v->next;

  PMEMoid log_oid = {((PMEMobjpool *)(p_ops->base_v))->uuid_lo,
                     (uintptr_t)dest - (uintptr_t)(p_ops->base)};

  *ulog_v = empty;
  uint8_t tag[HMAC_SIZE];
  struct ulog *ulog_encr = ulog_hdr_encrypt(ulog_v, (uint8_t *)&log_oid, tag);

  append_undo_object_entry(log_oid, tag, OBJ_NLANES, sizeof(struct ulog), 0, 1);

  pmemops_memcpy(p_ops, dest, ulog_encr, sizeof(struct ulog), PMEMOBJ_F_MEM_WC);

  free(ulog_encr);
}

/*
 * ulog_clobber_data -- zeroes out 'nbytes' of data in the logs
 */
int ulog_clobber_data(struct ulog *ulog_first, size_t nbytes,
                      size_t ulog_base_nbytes, struct ulog_next *next,
                      ulog_free_fn ulog_free,
                      ulog_rm_user_buffer_fn user_buff_remove,
                      const struct pmem_ops *p_ops, unsigned flags,
                      uint64_t tx_lane_id) {
  ASSERTne(ulog_first, NULL);
  LOG(11, "first : %p next : %p nbytes : %p", ulog_first, next, &nbytes);
  /* In case of abort we need to increment counter in the first ulog. */
  if (flags & ULOG_INC_FIRST_GEN_NUM)
    ulog_inc_gen_num(ulog_first, p_ops, tx_lane_id);

  /*
   * In the case of abort or commit, we are not going to free all ulogs,
   * but rather increment the generation number to be consistent in the
   * first two ulogs.
   */
  size_t second_offset = VEC_SIZE(next) == 0 ? 0 : *VEC_GET(next, 0);
  struct ulog *ulog_second = ulog_by_offset(second_offset, p_ops);

  if (ulog_second && !(flags & ULOG_FREE_AFTER_FIRST))
    /*
     * We want to keep gen_nums consistent between ulogs.
     * If the transaction will commit successfully we'll reuse the
     * second buffer (third and next ones will be freed anyway).
     * If the application will crash we'll free 2nd ulog on
     * recovery, which means we'll never read gen_num of the
     * second ulog in case of an ungraceful shutdown.
     */
    ulog_inc_gen_num(ulog_second, p_ops, tx_lane_id);

  /* The ULOG_ANY_USER_BUFFER flag indicates more than one ulog exist */
  if (flags & ULOG_ANY_USER_BUFFER) ASSERTne(ulog_second, NULL);

  struct ulog *u;

  /*
   * only if there was any user buffer it make sense to check
   * if the second ulog is allocated by user
   */
  if ((flags & ULOG_ANY_USER_BUFFER) &&
      (ulog_second->flags & ULOG_USER_OWNED)) {
    /*
     * function ulog_free_next() starts from 'next' ulog,
     * so to start from the second ulog we need to
     * pass the first one
     */
    u = ulog_first;
  } else {
    /*
     * To make sure that transaction logs do not occupy too
     * much of space, all of them, except for the first one,
     * are freed at the end of the operation. The reasoning for
     * this is that pmalloc() is a relatively cheap operation for
     * transactions where many hundreds of kilobytes are being
     * snapshot, and so, allocating and freeing the buffer for
     * each transaction is an acceptable overhead for the average
     * case.
     */
    if (flags & ULOG_FREE_AFTER_FIRST)
      u = ulog_first;
    else
      u = ulog_second;
  }

  if (u == NULL) {
    return 0;
  }

  return ulog_free_next(u, p_ops, ulog_free, user_buff_remove, flags);
}

static int validate_ulog_epc_entry(uint64_t pool_id, uint64_t offset,
                                   uint8_t *tag, uint64_t size,
                                   uintptr_t base_addr) {
  // get the log address
  struct ulog *ulog = (struct ulog *)(base_addr + offset);
  struct epc_entry *obj_record = NULL;
  obj_record = epc_lookup_lock((uint8_t *)&offset);

  uint8_t *decrypted_text;
  // if successful we are fine else check with the temporary one
  if ((decrypted_text = (uint8_t *)decrypt_final(
           (uint8_t *)ulog, size, (uint8_t *)obj_record->obj_sign, NULL, 0,
           (uint8_t *)&(PMEMoid){pool_id, offset})) == NULL) {
    if ((decrypted_text = (uint8_t *)decrypt_final(
             (uint8_t *)ulog, size, tag, NULL, 0,
             (uint8_t *)&(PMEMoid){pool_id, offset})) == NULL) {
      printf("inconsistent log header\n");
      exit(1);
    } else {
      free(decrypted_text);
      store_epc_entry(pool_id, offset, tag, size, 1);
      return 1;
    }
  }
  free(decrypted_text);
  return 0;
}
/*
 * ulog_header_validate -- (internal)
 * in case of interrupted ulog_header gen_num update,
 * fix the ulog header signature in the epc and state it as updated in the
 * manifest
 */
void ulog_header_validate(const struct pmem_ops *p_ops, uint64_t tx_lane_id) {
  int update =
      foreach_temp_list_entry(&temp_ulog_list[tx_lane_id],
                              validate_ulog_epc_entry, (uintptr_t)p_ops->base);
  free_temp_list(&temp_ulog_list[tx_lane_id]);
  return;
}

/*
 * ulog_base_nbytes_redo -- (internal) counts the actual of number of bytes
 *	occupied by the redo ulog
 */
size_t sec_ulog_base_nbytes_redo(struct ulog *ulog,
                                 const struct pmem_ops *p_ops) {
  size_t offset = 0;
  struct ulog_entry_base *e;
  // ANCHOR : decrypt ulog structure!
  struct ulog *ulog_v = ulog_hdr_decrypt(ulog, p_ops);
  if (ulog_v->data_size == 0) {
    return 0;
  }

  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = ((PMEMobjpool *)(p_ops->base_v))->uuid_lo;
  iv[1] = (uintptr_t)ulog - (uintptr_t)(p_ops->base);
  uint8_t *ulog_data =
      decrypt_final(ulog->data, ulog_v->data_size, (uint8_t *)ulog_v->data_tag,
                    NULL, 0, (uint8_t *)iv);
  ASSERT(ulog_data != NULL);

  for (offset = 0; offset < ulog_v->capacity;) {
    e = (struct ulog_entry_base *)(ulog_data + offset);
    if (!ulog_entry_valid(ulog, e)) break;
    LOG(11, "entry : %p size : %ld", e, ulog_entry_size(e));
    offset += ulog_entry_size(e);
  }

  free(ulog_data);
  return offset;
}

/*
 * ulog_base_nbytes_undo -- (internal) counts the actual of number of bytes
 *	occupied by the undo ulog
 */
size_t sec_ulog_base_nbytes_undo(struct ulog *ulog,
                                 const struct pmem_ops *p_ops) {
  size_t offset = 0;
  struct ulog_entry_base *e;
  // ANCHOR : decrypt ulog structure!
  struct ulog *ulog_v = ulog_hdr_decrypt(ulog, p_ops);
  for (offset = 0; offset < ulog_v->capacity;) {
    e = (struct ulog_entry_base *)(ulog->data + offset);
    if (!sec_ulog_entry_valid(ulog, e, p_ops)) break;
    LOG(11, "entry : %p size : %ld", e, sec_ulog_entry_size(e, p_ops));
    offset += sec_ulog_entry_size(e, p_ops);
  }
  return offset;
}

/*
 * sec_ulog_check_entry --
 *	(internal) checks consistency of a single ulog entry
 */
static int sec_ulog_check_entry(struct ulog_entry_base *e, void *arg,
                                const struct pmem_ops *p_ops) {
  LOG(10, "entry offset : %p", e);
  struct ulog_entry_base *decr_entry =
      (struct ulog_entry_base *)sec_ulog_entry_verify(e, p_ops);
  uint64_t offset = ulog_entry_offset(decr_entry);
  free(decr_entry);
  ulog_check_offset_fn check = arg;

  if (!check(p_ops->base, offset)) {
    LOG(15, "ulog %p invalid offset %" PRIu64, e, offset);
    return -1;
  }
  return offset == 0 ? -1 : 0;
}

#else
/*
 * ulog_inc_gen_num -- (internal) increments gen num in the ulog
 */
static void ulog_inc_gen_num(struct ulog *ulog, const struct pmem_ops *p_ops) {
  LOG(10, NULL);
  size_t gns = sizeof(ulog->gen_num);
  VALGRIND_ADD_TO_TX(&ulog->gen_num, gns);
  ulog->gen_num++;
  if (p_ops)
    pmemops_persist(p_ops, &ulog->gen_num, gns);
  else
    VALGRIND_SET_CLEAN(&ulog->gen_num, gns);
  VALGRIND_REMOVE_FROM_TX(&ulog->gen_num, gns);
}

/*
 * ulog_free_next -- free all ulogs starting from the indicated one.
 * Function returns 1 if any ulog have been freed or unpinned, 0 otherwise.
 */
int ulog_free_next(struct ulog *u, const struct pmem_ops *p_ops,
                   ulog_free_fn ulog_free,
                   ulog_rm_user_buffer_fn user_buff_remove, uint64_t flags) {
  int ret = 0;
  LOG(11, NULL);
  if (u == NULL) return ret;

  VEC(, uint64_t *) ulogs_internal_except_first;
  VEC_INIT(&ulogs_internal_except_first);

  /*
   * last_internal - pointer to a last found ulog allocated
   * internally by the libpmemobj
   */
  struct ulog *last_internal = u;
  struct ulog *current;

  /* iterate all linked logs and unpin user defined */
  while ((flags & ULOG_ANY_USER_BUFFER) && last_internal != NULL &&
         last_internal->next != 0) {
    current = ulog_by_offset(last_internal->next, p_ops);
    /*
     * handle case with user logs one after the other
     * or mixed user and internal logs
     */
    while (current != NULL && (current->flags & ULOG_USER_OWNED)) {
      last_internal->next = current->next;
      pmemops_persist(p_ops, &last_internal->next, sizeof(last_internal->next));

      user_buff_remove(p_ops->base, current);

      current = ulog_by_offset(last_internal->next, p_ops);
      /* any ulog has been unpinned - set return value to 1 */
      ret = 1;
    }
    last_internal = ulog_by_offset(last_internal->next, p_ops);
  }

  while (u->next != 0) {
    if (VEC_PUSH_BACK(&ulogs_internal_except_first, &u->next) != 0) {
      /* this is fine, it will just use more pmem */
      LOG(1, "unable to free transaction logs memory");
      goto out;
    }
    u = ulog_by_offset(u->next, p_ops);
  }
  /* free non-user defined logs */
  uint64_t *ulog_ptr;
  VEC_FOREACH_REVERSE(ulog_ptr, &ulogs_internal_except_first) {
    ulog_free(p_ops->base_v == NULL ? p_ops->base : p_ops->base_v,
              ulog_ptr);  // ANCHOR: Hack for correct pop address
    ret = 1;
  }

out:
  VEC_DELETE(&ulogs_internal_except_first);
  return ret;
}

/*
 * ulog_clobber -- zeroes the metadata of the ulog
 */
void ulog_clobber(struct ulog *dest, struct ulog_next *next,
                  const struct pmem_ops *p_ops) {
  LOG(11, "ulog : %p", dest);
  struct ulog empty;
  memset(&empty, 0, sizeof(empty));

  if (next != NULL)
    empty.next = VEC_SIZE(next) == 0 ? 0 : VEC_FRONT(next);
  else
    empty.next = dest->next;

  pmemops_memcpy(p_ops, dest, &empty, sizeof(empty), PMEMOBJ_F_MEM_WC);
}

/*
 * ulog_clobber_data -- zeroes out 'nbytes' of data in the logs
 */
int ulog_clobber_data(struct ulog *ulog_first, size_t nbytes,
                      size_t ulog_base_nbytes, struct ulog_next *next,
                      ulog_free_fn ulog_free,
                      ulog_rm_user_buffer_fn user_buff_remove,
                      const struct pmem_ops *p_ops, unsigned flags) {
  ASSERTne(ulog_first, NULL);
  LOG(11, "first : %p next : %p nbytes : %p", ulog_first, next, &nbytes);
  /* In case of abort we need to increment counter in the first ulog. */
  if (flags & ULOG_INC_FIRST_GEN_NUM) ulog_inc_gen_num(ulog_first, p_ops);

  /*
   * In the case of abort or commit, we are not going to free all ulogs,
   * but rather increment the generation number to be consistent in the
   * first two ulogs.
   */
  size_t second_offset = VEC_SIZE(next) == 0 ? 0 : *VEC_GET(next, 0);
  struct ulog *ulog_second = ulog_by_offset(second_offset, p_ops);

  if (ulog_second && !(flags & ULOG_FREE_AFTER_FIRST))
    /*
     * We want to keep gen_nums consistent between ulogs.
     * If the transaction will commit successfully we'll reuse the
     * second buffer (third and next ones will be freed anyway).
     * If the application will crash we'll free 2nd ulog on
     * recovery, which means we'll never read gen_num of the
     * second ulog in case of an ungraceful shutdown.
     */
    ulog_inc_gen_num(ulog_second, NULL);

  /* The ULOG_ANY_USER_BUFFER flag indicates more than one ulog exist */
  if (flags & ULOG_ANY_USER_BUFFER) ASSERTne(ulog_second, NULL);

  struct ulog *u;

  /*
   * only if there was any user buffer it make sense to check
   * if the second ulog is allocated by user
   */
  if ((flags & ULOG_ANY_USER_BUFFER) &&
      (ulog_second->flags & ULOG_USER_OWNED)) {
    /*
     * function ulog_free_next() starts from 'next' ulog,
     * so to start from the second ulog we need to
     * pass the first one
     */
    u = ulog_first;
  } else {
    /*
     * To make sure that transaction logs do not occupy too
     * much of space, all of them, except for the first one,
     * are freed at the end of the operation. The reasoning for
     * this is that pmalloc() is a relatively cheap operation for
     * transactions where many hundreds of kilobytes are being
     * snapshot, and so, allocating and freeing the buffer for
     * each transaction is an acceptable overhead for the average
     * case.
     */
    if (flags & ULOG_FREE_AFTER_FIRST)
      u = ulog_first;
    else
      u = ulog_second;
  }

  if (u == NULL) {
    return 0;
  }

  return ulog_free_next(u, p_ops, ulog_free, user_buff_remove, flags);
}

#endif

/*
 * ulog_construct -- initializes the ulog structure
 */
void ulog_construct(uint64_t offset, size_t capacity, uint64_t gen_num,
                    int flush, uint64_t flags, const struct pmem_ops *p_ops) {
  LOG(16, NULL);
  struct ulog *ulog = ulog_by_offset(offset, p_ops);
  ASSERTne(ulog, NULL);

  size_t diff = OBJ_PTR_TO_OFF(p_ops->base, ulog) - offset;
  if (diff > 0) capacity = ALIGN_DOWN(capacity - diff, CACHELINE_SIZE);

  VALGRIND_ADD_TO_TX(ulog, SIZEOF_ULOG(capacity));

  ulog->capacity = capacity;
  ulog->checksum = 0;
  ulog->next = 0;
  ulog->gen_num = gen_num;
  ulog->tcv = 0;
  ulog->flags = flags;
  memset(ulog->unused, 0, sizeof(ulog->unused));

  /* we only need to zero out the header of ulog's first entry */
  size_t zeroed_data = CACHELINE_ALIGN(sizeof(struct ulog_entry_base));

  if (flush) {
    pmemops_xflush(p_ops, ulog, sizeof(*ulog), PMEMOBJ_F_RELAXED);
    pmemops_memset(
        p_ops, ulog->data, 0, zeroed_data,
        PMEMOBJ_F_MEM_NONTEMPORAL | PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_RELAXED);
  } else {
    /*
     * We want to avoid replicating zeroes for every ulog of every
     * lane, to do that, we need to use plain old memset.
     */
    memset(ulog->data, 0, zeroed_data);
  }

  VALGRIND_REMOVE_FROM_TX(ulog, SIZEOF_ULOG(capacity));
}

/*
 * ulog_checksum -- (internal) calculates ulog checksum
 */
static int ulog_checksum(struct ulog *ulog, size_t ulog_base_bytes,
                         int insert) {
  return util_checksum(ulog, SIZEOF_ULOG(ulog_base_bytes), &ulog->checksum,
                       insert, 0);
}

/*
 * ulog_store -- stores the transient src ulog in the
 *	persistent dest ulog
 *
 * The source and destination ulogs must be cacheline aligned.
 */
void ulog_store(struct ulog *dest, struct ulog *src, size_t nbytes,
                size_t ulog_base_nbytes, size_t ulog_total_capacity,
                struct ulog_next *next, const struct pmem_ops *p_ops) {
  LOG(11, NULL);
  /*
   * First, store all entries over the base capacity of the ulog in
   * the next logs.
   * Because the checksum is only in the first part, we don't have to
   * worry about failsafety here.
   */
  struct ulog *ulog = dest;
  size_t offset = ulog_base_nbytes;

  /*
   * Copy at least 8 bytes more than needed. If the user always
   * properly uses entry creation functions, this will zero-out the
   * potential leftovers of the previous log. Since all we really need
   * to zero is the offset, sizeof(struct redo_log_entry_base) is enough.
   * If the nbytes is aligned, an entire cacheline needs to be
   * additionally zeroed.
   * But the checksum must be calculated based solely on actual data.
   * If the ulog total capacity is equal to the size of the
   * ulog being stored (nbytes == ulog_total_capacity), then there's
   * nothing to invalidate because the entire log data will
   * be overwritten.
   */
  size_t checksum_nbytes = MIN(ulog_base_nbytes, nbytes);
  if (nbytes != ulog_total_capacity)
    nbytes = CACHELINE_ALIGN(nbytes + sizeof(struct ulog_entry_base));
  ASSERT(nbytes <= ulog_total_capacity);

  size_t base_nbytes = MIN(ulog_base_nbytes, nbytes);
  size_t next_nbytes = nbytes - base_nbytes;

  size_t nlog = 0;

  while (next_nbytes > 0) {
    ulog = ulog_by_offset(VEC_ARR(next)[nlog++], p_ops);
    ASSERTne(ulog, NULL);

    size_t copy_nbytes = MIN(next_nbytes, ulog->capacity);
    next_nbytes -= copy_nbytes;

    ASSERT(IS_CACHELINE_ALIGNED(ulog->data));

    VALGRIND_ADD_TO_TX(ulog->data, copy_nbytes);
    pmemops_memcpy(
        p_ops, ulog->data, src->data + offset, copy_nbytes,
        PMEMOBJ_F_MEM_WC | PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_RELAXED);
    VALGRIND_REMOVE_FROM_TX(ulog->data, copy_nbytes);
    offset += copy_nbytes;
  }

  if (nlog != 0) pmemops_drain(p_ops);

  /*
   * Then, calculate the checksum and store the first part of the
   * ulog.
   */
  size_t old_capacity = src->capacity;
  src->capacity = base_nbytes;
  src->next = VEC_SIZE(next) == 0 ? 0 : VEC_FRONT(next);
  ulog_checksum(src, checksum_nbytes, 1);

  pmemops_memcpy(p_ops, dest, src, SIZEOF_ULOG(base_nbytes), PMEMOBJ_F_MEM_WC);

  src->capacity = old_capacity;
}

/*
 * ulog_clobber_entry -- zeroes out a single log entry header
 */
void ulog_clobber_entry(const struct ulog_entry_base *e,
                        const struct pmem_ops *p_ops) {
  static const size_t aligned_entry_size =
      CACHELINE_ALIGN(sizeof(struct ulog_entry_base));

  VALGRIND_ADD_TO_TX(e, aligned_entry_size);
  pmemops_memset(p_ops, (char *)e, 0, aligned_entry_size,
                 PMEMOBJ_F_MEM_NONTEMPORAL);
  VALGRIND_REMOVE_FROM_TX(e, aligned_entry_size);
}

/*
 * ulog_entry_buf_create -- atomically creates a buffer entry in the log
 */
struct ulog_entry_buf *ulog_entry_buf_create(struct ulog *ulog, size_t offset,
                                             uint64_t gen_num, uint64_t *dest,
                                             const void *src, uint64_t size,
                                             ulog_operation_type type,
                                             const struct pmem_ops *p_ops) {
  LOG(15, NULL);
  struct ulog_entry_buf *e = (struct ulog_entry_buf *)(ulog->data + offset);

  /*
   * Depending on the size of the source buffer, we might need to perform
   * up to three separate copies:
   *	1. The first cacheline, 24b of metadata and 40b of data
   * If there's still data to be logged:
   *	2. The entire remainder of data data aligned down to cacheline,
   *	for example, if there's 150b left, this step will copy only
   *	128b.
   * Now, we are left with between 0 to 63 bytes. If nonzero:
   *	3. Create a stack allocated cacheline-sized buffer, fill in the
   *	remainder of the data, and copy the entire cacheline.
   *
   * This is done so that we avoid a cache-miss on misaligned writes.
   */

  struct ulog_entry_buf *b = alloca(CACHELINE_SIZE);
  b->base.offset = (uint64_t)(dest) - (uint64_t)p_ops->base;
  b->base.offset |= ULOG_OPERATION(type);
  b->size = size;
  b->checksum = 0;

  LOG(10, "src : %p  dest : %p size : %ld volatile_entry : %p", src, e, size,
      b);

  size_t bdatasize = CACHELINE_SIZE - sizeof(struct ulog_entry_buf);
  size_t ncopy = MIN(size, bdatasize);
  memcpy(b->data, src, ncopy);  // copy of the data from the pmem
  memset(b->data + ncopy, 0, bdatasize - ncopy);

  size_t remaining_size = ncopy > size ? 0 : size - ncopy;

  char *srcof = (char *)src + ncopy;
  size_t rcopy = ALIGN_DOWN(remaining_size, CACHELINE_SIZE);
  size_t lcopy = remaining_size - rcopy;

  uint8_t last_cacheline[CACHELINE_SIZE];
  LOG(10,
      "srcof : %p  ncopy : %ld rcopy : %ld lcopy : %ld remaining_size : %ld",
      srcof, ncopy, rcopy, lcopy, remaining_size);
  if (lcopy != 0) {
    memcpy(last_cacheline, srcof + rcopy, lcopy);
    memset(last_cacheline + lcopy, 0, CACHELINE_SIZE - lcopy);
  }

  // LOG(10, "Before the copies");
  if (rcopy != 0) {
    void *dest = e->data + ncopy;
    ASSERT(IS_CACHELINE_ALIGNED(dest));
    VALGRIND_ADD_TO_TX(dest, rcopy);
    pmemops_memcpy(p_ops, dest, srcof, rcopy,
                   PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_MEM_NONTEMPORAL);
    VALGRIND_REMOVE_FROM_TX(dest, rcopy);
  }

  if (lcopy != 0) {
    void *dest = e->data + ncopy + rcopy;
    ASSERT(IS_CACHELINE_ALIGNED(dest));
    VALGRIND_ADD_TO_TX(dest, CACHELINE_SIZE);
    pmemops_memcpy(p_ops, dest, last_cacheline, CACHELINE_SIZE,
                   PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_MEM_NONTEMPORAL);
    VALGRIND_REMOVE_FROM_TX(dest, CACHELINE_SIZE);
  }
  // LOG(10, "After the main copies");
  b->tcv = 0;
  b->entry_sign[0] = 0;
  b->entry_sign[1] = 0;
  b->checksum = util_checksum_seq(b, CACHELINE_SIZE, 0);
  if (rcopy != 0) b->checksum = util_checksum_seq(srcof, rcopy, b->checksum);
  if (lcopy != 0)
    b->checksum =
        util_checksum_seq(last_cacheline, CACHELINE_SIZE, b->checksum);

  b->checksum = util_checksum_seq(&gen_num, sizeof(gen_num), b->checksum);

  ASSERT(IS_CACHELINE_ALIGNED(e));
  VALGRIND_ADD_TO_TX(e, CACHELINE_SIZE);
  pmemops_memcpy(p_ops, e, b, CACHELINE_SIZE,
                 PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_MEM_NONTEMPORAL);
  VALGRIND_REMOVE_FROM_TX(e, CACHELINE_SIZE);
  // LOG(10, "After the checksum copies");
  pmemops_drain(p_ops);

  /*
   * Allow having uninitialized data in the buffer - this requires marking
   * data as defined so that comparing checksums is not reported as an
   * error by memcheck.
   */
#if VG_MEMCHECK_ENABLED
  if (On_valgrind) {
    VALGRIND_MAKE_MEM_DEFINED(e->data, ncopy + rcopy + lcopy);
    VALGRIND_MAKE_MEM_DEFINED(&e->checksum, sizeof(e->checksum));
  }
#endif
  ASSERT(ulog_entry_valid(
      ulog, &e->base));  // to temporary check that the persistent entry if fine
  return e;
}

/*
 * ulog_process_entry -- (internal) processes a single ulog entry
 */
static int ulog_process_entry(struct ulog_entry_base *e, void *arg,
                              const struct pmem_ops *p_ops) {
  ulog_entry_apply(e, 0, p_ops);

  return 0;
}

/*
 * ulog_check -- (internal) check consistency of ulog entries
 */
int ulog_check(struct ulog *ulog, ulog_check_offset_fn check,
               const struct pmem_ops *p_ops) {
  LOG(15, "ulog %p", ulog);

  return ulog_foreach_entry(ulog, ulog_check_entry, check, p_ops);
}

/*
 * ulog_process -- process ulog entries
 */
void ulog_process(struct ulog *ulog, ulog_check_offset_fn check,
                  const struct pmem_ops *p_ops) {
  LOG(15, "ulog %p", ulog);

#ifdef DEBUG
  if (check) ulog_check(ulog, check, p_ops);
#endif

  ulog_foreach_entry(ulog, ulog_process_entry, NULL, p_ops);
}

/*
 * ulog_recovery_needed -- checks if the logs needs recovery
 */
int ulog_recovery_needed(struct ulog *ulog, int verify_checksum) {
  LOG(16, NULL);
#ifdef ANCHOR_FUNCS
  size_t nbytes =
      MIN(sec_ulog_base_nbytes_redo(ulog, NULL), ulog->capacity);  // never used
#else
  size_t nbytes = MIN(ulog_base_nbytes(ulog), ulog->capacity);
#endif
  if (nbytes == 0) return 0;

  if (verify_checksum && !ulog_checksum(ulog, nbytes, 0)) return 0;

  return 1;
}

/*
 * ulog_recover -- recovery of ulog
 *
 * The ulog_recover shall be preceded by ulog_check call.
 */
void ulog_recover(struct ulog *ulog, ulog_check_offset_fn check,
                  const struct pmem_ops *p_ops) {
  LOG(15, "ulog %p", ulog);
  if (ulog_recovery_needed(ulog, 1)) {
    ulog_process(ulog, check, p_ops);
    ulog_clobber(ulog, NULL,
                 p_ops);  // here i first have to update the starting counter!
  }
}

#ifdef ANCHOR_FUNCS
/*
 * ANCHOR Functions
 */

/*
 * sec_ulog_check -- (internal) check consistency of ulog entries
 */
int sec_ulog_check(struct ulog *ulog, ulog_check_offset_fn check,
                   const struct pmem_ops *p_ops) {
  LOG(15, "ulog %p", ulog);

  return sec_ulog_foreach_entry(ulog, sec_ulog_check_entry, check, p_ops);
}

/*
 *	sec_ulog_update_hdr: update the persistent log header with the new
 *metadata ulog_p 	: pointer to the persistent ulog
 *  ulog_v 	: pointer to the volatile updated ulog
 *	offset	: offset inside the structure that needs to be persisted
 * 	size	: size of the metadata to be persisted
 */
static __attribute__((unused)) void sec_ulog_update_hdr(
    const struct pmem_ops *p_ops, struct ulog *ulog_p, struct ulog *ulog_v,
    uint8_t offset, uint8_t size) {
  uint8_t *ciphertext;
  uint8_t tag[HMAC_SIZE];
  PMEMoid log_oid = (PMEMoid){((PMEMobjpool *)(p_ops->base_v))->uuid_lo,
                              (uintptr_t)ulog_p - (uintptr_t)(p_ops->base)};
  ciphertext = encrypt_final(
      (uint8_t *)ulog_v, sizeof(struct ulog), tag, NULL, 0,
      (uint8_t *)&log_oid);  // for the header of the entry to be encrypted

  append_undo_object_entry(log_oid, tag, OBJ_NLANES, sizeof(struct ulog), 0, 1);

  pmemops_memcpy(p_ops, (uint8_t *)ulog_p + offset, ciphertext + offset, size,
                 0);
  pmemops_persist(p_ops, ulog_p, sizeof(struct ulog));

  free(ciphertext);
}

/*
 * sec_ulog_checksum -- (internal) calculates ulog checksum
 */
static int sec_ulog_checksum(struct ulog *ulog, uint8_t *data,
                             size_t ulog_base_bytes, int insert) {
  LOG(11, NULL);

  if (!insert) {
    uint64_t csum;
    csum = util_checksum_compute(ulog, SIZEOF_ULOG(0), &ulog->checksum, 0);
    csum = util_checksum_seq(data, ulog_base_bytes, csum);
    int ret = (ulog->checksum == csum) ? 1 : 0;
    return ret;
  } else {
    return util_checksum(ulog, SIZEOF_ULOG(ulog_base_bytes), &ulog->checksum,
                         insert, 0);
  }
}
/*
 * sec_ulog_store -- stores the transient src ulog in the
 *	persistent dest ulog
 *
 * The source and destination ulogs must be cacheline aligned.
 */
void sec_ulog_store(struct ulog *dest, struct ulog *src, size_t nbytes,
                    size_t ulog_base_nbytes, size_t ulog_total_capacity,
                    struct ulog_next *next, const struct pmem_ops *p_ops,
                    uint64_t tx_lane_id) {
#ifdef STATISTICS
  stats_measure_start(REDO_LOG);
#endif
  LOG(11, NULL);
  /*
   * First, store all entries over the base capacity of the ulog in
   * the next logs.
   * Because the checksum is only in the first part, we don't have to
   * worry about failsafety here.
   */
  struct ulog *ulog = dest;
  size_t offset = ulog_base_nbytes;

  /*
   * Copy at least 8 bytes more than needed. If the user always
   * properly uses entry creation functions, this will zero-out the
   * potential leftovers of the previous log. Since all we really need
   * to zero is the offset, sizeof(struct redo_log_entry_base) is enough.
   * If the nbytes is aligned, an entire cacheline needs to be
   * additionally zeroed.
   * But the checksum must be calculated based solely on actual data.
   * If the ulog total capacity is equal to the size of the
   * ulog being stored (nbytes == ulog_total_capacity), then there's
   * nothing to invalidate because the entire log data will
   * be overwritten.
   */
  size_t checksum_nbytes = MIN(ulog_base_nbytes, nbytes);
  if (nbytes != ulog_total_capacity)
    nbytes = CACHELINE_ALIGN(nbytes + sizeof(struct ulog_entry_base));
  ASSERT(nbytes <= ulog_total_capacity);

  size_t base_nbytes = MIN(ulog_base_nbytes, nbytes);
  size_t next_nbytes = nbytes - base_nbytes;

  size_t nlog = 0;
  struct ulog *ulog_v;
  while (next_nbytes > 0) {
    ulog = ulog_by_offset(VEC_ARR(next)[nlog++], p_ops);
    ASSERTne(ulog, NULL);

    ulog_v = ulog_hdr_decrypt(ulog, p_ops);
    size_t copy_nbytes = MIN(next_nbytes, ulog_v->capacity);

    next_nbytes -= copy_nbytes;

    ASSERT(IS_CACHELINE_ALIGNED(ulog->data));

    VALGRIND_ADD_TO_TX(ulog->data, copy_nbytes);
    // copy of encrypted bytes created in ulog_entry_val in the next ulog
    PMEMoid log_oid = {((PMEMobjpool *)(p_ops->base_v))->uuid_lo,
                       (uintptr_t)ulog - (uintptr_t)(p_ops->base)};
    uint8_t tag[HMAC_SIZE];
    encrypt_final_direct(src->data + offset, copy_nbytes,
                         (uint8_t *)ulog_v->data_tag, NULL, 0,
                         (uint8_t *)&log_oid, (uint8_t *)ulog->data);
    ulog_v->data_size = copy_nbytes;

    encrypt_final_direct((uint8_t *)ulog_v, sizeof(struct ulog), tag, NULL, 0,
                         (uint8_t *)&log_oid, (uint8_t *)ulog);
    /* LOG */
    append_undo_object_entry(log_oid, tag, OBJ_NLANES, sizeof(struct ulog), 0,
                             1);

#ifdef WRITE_AMPL
    bytes_written_inc(REDO_LOG_DATA, copy_nbytes + sizeof(struct ulog));
    bytes_written_inc(REDO_LOG_DATA_PMDK,
                      sizeof(struct ulog_pmdk));  // rest payload is added in
                                                  // sec_ulog_entry_val_create
#endif
    VALGRIND_REMOVE_FROM_TX(ulog->data, copy_nbytes);
    offset += copy_nbytes;
  }

  if (nlog != 0) pmemops_drain(p_ops);

  /*
   * Then, calculate the checksum and store the first part of the
   * ulog.
   * Checksum of decrypted header + encrypted data
   */
  size_t old_capacity = src->capacity;
  src->capacity = base_nbytes;
  src->next = VEC_SIZE(next) == 0 ? 0 : VEC_FRONT(next);
  src->data_size = base_nbytes;

  // uint8_t* ciphertext;
  uint8_t tag[HMAC_SIZE];
  PMEMoid log_oid = {((PMEMobjpool *)(p_ops->base_v))->uuid_lo,
                     (uintptr_t)dest - (uintptr_t)(p_ops->base)};

  encrypt_final_direct(src->data, base_nbytes, (uint8_t *)src->data_tag, NULL,
                       0, (uint8_t *)&log_oid, (uint8_t *)dest->data);

  sec_ulog_checksum(src, src->data, checksum_nbytes, 1);

  struct ulog *dest_v;
  if ((dest_v = ulog_hdr_decrypt(dest, p_ops)) != NULL) {
    *dest_v = *src;
  }

  // ensure first ulog header is stored safely!
  encrypt_final_direct((uint8_t *)src, sizeof(struct ulog), tag, NULL, 0,
                       (uint8_t *)&log_oid, (uint8_t *)dest);
  /* LOG */
  append_undo_object_entry(log_oid, tag, OBJ_NLANES, sizeof(struct ulog), 0, 1);

#ifdef WRITE_AMPL
  bytes_written_inc(REDO_LOG_DATA, base_nbytes + sizeof(struct ulog));
  bytes_written_inc(REDO_LOG_DATA_PMDK,
                    sizeof(struct ulog_pmdk));  // rest payload is added in
                                                // sec_ulog_entry_val_create
#endif
  src->capacity = old_capacity;
#ifdef STATISTICS
  stats_measure_end(REDO_LOG);
#endif
}
/*
 * sec_ulog_reserve -- reserves new capacity in the ulog
 */
int sec_ulog_reserve(struct ulog *ulog, size_t ulog_base_nbytes, size_t gen_num,
                     int auto_reserve, size_t *new_capacity,
                     ulog_extend_fn extend, struct ulog_next *next,
                     const struct pmem_ops *p_ops, uint64_t tx_lane_id) {
  if (!auto_reserve) {
    LOG(1, "cannot auto reserve next ulog");
    return -1;
  }

  int is_first_log = 0;
  if (VEC_SIZE(next) == 0) {
    is_first_log = 1;
  }

  size_t capacity = ulog_base_nbytes;

  uint64_t offset;
  struct ulog *ulog_v;
  VEC_FOREACH(offset, next) {
    ulog = ulog_by_offset(offset, p_ops);
    ASSERTne(ulog, NULL);
    ulog_v = ulog_hdr_decrypt(ulog, p_ops);
    capacity += ulog_v->capacity;
  }

  while (capacity < *new_capacity) {
    ulog_v = ulog_hdr_decrypt(ulog, p_ops);
    if (extend(p_ops->base_v, &ulog->next, gen_num, ulog_v, is_first_log,
               tx_lane_id) != 0)
      return -1;
    VEC_PUSH_BACK(next, ulog_v->next);

    ulog = sec_ulog_next(ulog, p_ops);
    ASSERTne(ulog, NULL);

    ulog_v = ulog_hdr_decrypt(ulog, p_ops);
    capacity += ulog_v->capacity;
  }
  *new_capacity = capacity;
  return 0;
}

/*
 * sec_ulog_redo_bitmap_action -- applies REDO AND/OR operations on bitmaps
 * correctly!
 */
void sec_ulog_redo_bitmap_action(struct ulog_entry_val *ev, uint64_t *dst,
                                 uint64_t tx_lane_id,
                                 const struct pmem_ops *p_ops,
                                 ulog_operation_type t) {
  long unsigned bpos;
  uint64_t *temp_bitmap;
  uint64_t obj_size;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  uint8_t *ciphertext;

  temp_bitmap = (uint64_t *)spool_metadata_read_cached(
      ((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
      (uint8_t *)((uintptr_t)p_ops->base + (uintptr_t)ev->bitmap_offset),
      ev->bitmap_offset, &obj_size, 1);

  // check for the backup signature that exists in temp list and decrypt
  int to_be_freed = 0;
  if (temp_bitmap == NULL) {
    uint8_t *temp_tag;
    temp_tag = scan_temp_list(&temp_redo_list[tx_lane_id],
                              ((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
                              ev->bitmap_offset, &obj_size);
    if (temp_tag != NULL) {
      temp_bitmap = (uint64_t *)decrypt_final(
          (uint8_t *)((uintptr_t)p_ops->base + (uintptr_t)ev->bitmap_offset),
          obj_size, temp_tag, NULL, 0,
          (uint8_t *)&(PMEMoid){((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
                                ev->bitmap_offset});

      memcpy(tag, temp_tag, HMAC_SIZE);
      if (temp_bitmap != NULL) {
        to_be_freed = 1;
        free_temp_list_entry(&temp_redo_list[tx_lane_id],
                             ((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
                             ev->bitmap_offset, 0);
      }
    } else {
      printf("heap metadata corruption\n");
      exit(1);
    }
  }
  bpos = ((uintptr_t)dst - (uintptr_t)p_ops->base - ev->bitmap_offset) /
         sizeof(uint64_t);

  // apply the action in the volatile bitmap state
  // the action is applied when the entry was appended in
  // append_bitmap_manifest_entry
  if (to_be_freed) {  // I have read the old signature after a recovery
    if (t == ULOG_OPERATION_AND)
      temp_bitmap[bpos] &= ev->value;
    else if (t == ULOG_OPERATION_OR)
      temp_bitmap[bpos] |= ev->value;
  }

  // encrypt and save the modified object
  iv[0] = ((struct pmemobjpool *)p_ops->base_v)->uuid_lo;
  iv[1] = ev->bitmap_offset;
  ciphertext = encrypt_final((uint8_t *)temp_bitmap, obj_size, tag, NULL, 0,
                             (uint8_t *)iv);

  *dst = ((uint64_t *)ciphertext)[bpos];

  if (to_be_freed) free(temp_bitmap);

  free(ciphertext);
}

/*
 * sec_ulog_redo_bitmap_action_recovery -- applies REDO AND/OR operations on
 * bitmaps correctly on recovery
 */
void sec_ulog_redo_bitmap_action_recovery(struct ulog_entry_val *ev,
                                          uint64_t *dst, uint64_t tx_lane_id,
                                          const struct pmem_ops *p_ops,
                                          ulog_operation_type t) {
  long unsigned bpos;
  uint64_t *temp_bitmap;
  uint64_t obj_size;
  uint8_t tag[HMAC_SIZE];
  uint64_t iv[IV_SIZE_UINT64];
  uint8_t *ciphertext;

  temp_bitmap = (uint64_t *)spool_metadata_read_cached(
      ((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
      (uint8_t *)((uintptr_t)p_ops->base + (uintptr_t)ev->bitmap_offset),
      ev->bitmap_offset, &obj_size, 1);

  // check for the backup signature that exists in temp list and decrypt
  int to_be_freed = 0;
  if (temp_bitmap == NULL) {
    uint8_t *temp_tag;
    temp_tag = scan_temp_list(&temp_redo_list[tx_lane_id],
                              ((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
                              ev->bitmap_offset, &obj_size);
    if (temp_tag != NULL) {
      temp_bitmap = (uint64_t *)decrypt_final(
          (uint8_t *)((uintptr_t)p_ops->base + (uintptr_t)ev->bitmap_offset),
          obj_size, temp_tag, NULL, 0,
          (uint8_t *)&(PMEMoid){((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
                                ev->bitmap_offset});

      memcpy(tag, temp_tag, HMAC_SIZE);
      if (temp_bitmap != NULL) {
        to_be_freed = 1;
        free_temp_list_entry(&temp_redo_list[tx_lane_id],
                             ((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
                             ev->bitmap_offset, 0);
      }
    } else {
      printf("heap metadata corruption\n");
      exit(1);
    }
  }
  bpos = ((uintptr_t)dst - (uintptr_t)p_ops->base - ev->bitmap_offset) /
         sizeof(uint64_t);

  // apply the action in the volatile bitmap state
  // the action is applied when the entry was appended in
  // append_bitmap_manifest_entry
  if (!to_be_freed) {  // I have read the old signature after a recovery
    if (t == ULOG_OPERATION_AND)
      temp_bitmap[bpos] &= ev->value;
    else if (t == ULOG_OPERATION_OR)
      temp_bitmap[bpos] |= ev->value;
  }

  // encrypt and save the modified object
  iv[0] = ((struct pmemobjpool *)p_ops->base_v)->uuid_lo;
  iv[1] = ev->bitmap_offset;
  ciphertext = encrypt_final((uint8_t *)temp_bitmap, obj_size, tag, NULL, 0,
                             (uint8_t *)iv);

  *dst = ((uint64_t *)ciphertext)[bpos];

  if (to_be_freed) free(temp_bitmap);

  free(ciphertext);
}

/*
 * sec_ulog_redo_bitmap_action_check -- checks if REDO AND/OR operation is
 * already applied
 */
int sec_ulog_redo_bitmap_action_check(struct ulog_entry_val *ev, uint64_t *dst,
                                      uint64_t tx_lane_id,
                                      const struct pmem_ops *p_ops,
                                      ulog_operation_type t) {
  int ret = 0;
  uint64_t *temp_bitmap;
  uint64_t obj_size;

  temp_bitmap = (uint64_t *)spool_metadata_read_cached(
      ((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
      (uint8_t *)((uintptr_t)p_ops->base + (uintptr_t)ev->bitmap_offset),
      ev->bitmap_offset, &obj_size, 1);

  // check for the backup signature that exists in temp list and decrypt
  if (temp_bitmap == NULL) {
    uint8_t *temp_tag;
    temp_tag = scan_temp_list(&temp_redo_list[tx_lane_id],
                              ((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
                              ev->bitmap_offset, &obj_size);
    if (temp_tag != NULL) {
      temp_bitmap = (uint64_t *)decrypt_final(
          (uint8_t *)((uintptr_t)p_ops->base + (uintptr_t)ev->bitmap_offset),
          obj_size, temp_tag, NULL, 0,
          (uint8_t *)&(PMEMoid){((struct pmemobjpool *)p_ops->base_v)->uuid_lo,
                                ev->bitmap_offset});
      if (temp_bitmap != NULL) {
        // redo log entry has been applied -- go on and apply the rest on
        // recovery
        free(temp_bitmap);
        ret = 1;
      }
    }
  }
  return ret;
}

/*
 * sec_ulog_entry_apply -- applies modifications of a single ulog entry
 */
void sec_ulog_entry_apply(const struct ulog_entry_base *e, int persist,
                          const struct pmem_ops *p_ops, uint64_t tx_lane_id) {
  ulog_operation_type t = ulog_entry_type(e);
  uint64_t offset = ulog_entry_offset(e);
  size_t dst_size = sizeof(uint64_t);
  uint64_t *dst = (uint64_t *)((uintptr_t)p_ops->base + offset);

  struct ulog_entry_val *ev;
  struct ulog_entry_buf *eb;

  flush_fn f = persist ? p_ops->persist : p_ops->flush;

  /*
  ANCHOR : ULOG_OPERATION_AND and ULOG_OPERATION_OR have only to do with bitmaps
                   therefore before applied they should update bitmap signature
  */
  switch (t) {
    case ULOG_OPERATION_AND:
    case ULOG_OPERATION_OR:
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      LOG(10,
          "REDO AND/OR tcv: %ld offset: %ld prev value: %lx value applied: %lx "
          "final value: %lx",
          ev->tcv, offset, *dst, ev->value, *dst | ev->value);
      sec_ulog_redo_bitmap_action(ev, dst, tx_lane_id, p_ops, t);
      f(p_ops->base_v, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_SET:
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      LOG(10,
          "REDO SET tcv: %ld offset: %ld prev value: %lx value applied: %lx "
          "final value: %lx",
          ev->tcv, offset, *dst, ev->value, ev->value);
      *dst = ev->value;
      f(p_ops->base_v, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_BUF_SET:
      eb = (struct ulog_entry_buf *)e;
      LOG(10, "APPLY BUF SET tcv : %ld", eb->tcv);
      dst_size = ((struct ulog_entry_buf *)eb)->size;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      pmemops_memset(p_ops, dst, *eb->data, dst_size,
                     PMEMOBJ_F_RELAXED | PMEMOBJ_F_MEM_NODRAIN);
      break;
    case ULOG_OPERATION_BUF_CPY:
      eb = (struct ulog_entry_buf *)e;
      LOG(10, "APPLY BUF CPY tcv : %ld", eb->tcv);
      dst_size = ((struct ulog_entry_buf *)eb)->size;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      pmemops_memcpy(p_ops, dst, eb->data, dst_size,
                     PMEMOBJ_F_RELAXED | PMEMOBJ_F_MEM_NODRAIN);
      break;
    default:
      ASSERT(0);
  }
  VALGRIND_REMOVE_FROM_TX(dst, dst_size);
}

/*
 * sec_ulog_entry_apply_decrypted -- applies modifications of a single ulog
 * entry
 */
void sec_ulog_entry_apply_decrypted(const struct ulog_entry_base *e,
                                    int persist, const struct pmem_ops *p_ops,
                                    uint64_t tx_lane_id) {
  LOG(10, "entry offset : %p", e);

  ulog_operation_type t = ulog_entry_type(e);
  uint64_t offset = ulog_entry_offset(e);
  LOG(10, "base : %p offset : %ld type : %ld", p_ops->base, offset, t);
  size_t dst_size = sizeof(uint64_t);
  uint64_t *dst = (uint64_t *)((uintptr_t)p_ops->base + offset);

  struct ulog_entry_val *ev;
  struct ulog_entry_buf *eb;

  flush_fn f = persist ? p_ops->persist : p_ops->flush;

  /*
  ANCHOR : ULOG_OPERATION_AND and ULOG_OPERATION_OR have only to do with bitmaps
                   therefore before applied they should update bitmap signature
  */
  switch (t) {
    case ULOG_OPERATION_AND:
    case ULOG_OPERATION_OR:
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      LOG(10,
          "REDO AND/OR tcv: %ld offset: %ld prev value: %lx value applied: %lx "
          "final value: %lx",
          ev->tcv, offset, *dst, ev->value, *dst | ev->value);
      sec_ulog_redo_bitmap_action(ev, dst, tx_lane_id, p_ops, t);
      f(p_ops->base_v, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_SET:
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      LOG(10,
          "REDO SET tcv: %ld offset: %ld prev value: %lx value applied: %lx "
          "final value: %lx",
          ev->tcv, offset, *dst, ev->value, ev->value);
      *dst = ev->value;
      f(p_ops->base_v, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_BUF_SET:
      eb = (struct ulog_entry_buf *)e;
      LOG(10, "APPLY BUF SET tcv : %ld", eb->tcv);
      dst_size = ((struct ulog_entry_buf *)eb)->size;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      pmemops_memset(p_ops, dst, *eb->data, dst_size,
                     PMEMOBJ_F_RELAXED | PMEMOBJ_F_MEM_NODRAIN);
      break;
    case ULOG_OPERATION_BUF_CPY:
      eb = (struct ulog_entry_buf *)e;
      LOG(10, "APPLY BUF CPY tcv : %ld", eb->tcv);
      dst_size = ((struct ulog_entry_buf *)eb)->size;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      pmemops_memcpy(p_ops, dst, eb->data, dst_size,
                     PMEMOBJ_F_RELAXED | PMEMOBJ_F_MEM_NODRAIN);
      break;
    default:
      ASSERT(0);
  }
  VALGRIND_REMOVE_FROM_TX(dst, dst_size);
}

/*
 * sec_ulog_entry_apply_decrypted_recovery -- applies modifications of a single
 * ulog entry on recovery
 */
void sec_ulog_entry_apply_decrypted_recovery(const struct ulog_entry_base *e,
                                             int persist,
                                             const struct pmem_ops *p_ops,
                                             uint64_t tx_lane_id) {
  LOG(10, "entry offset : %p", e);

  ulog_operation_type t = ulog_entry_type(e);
  uint64_t offset = ulog_entry_offset(e);
  LOG(10, "base : %p offset : %ld type : %ld", p_ops->base, offset, t);
  size_t dst_size = sizeof(uint64_t);
  uint64_t *dst = (uint64_t *)((uintptr_t)p_ops->base + offset);

  struct ulog_entry_val *ev;
  struct ulog_entry_buf *eb;

  flush_fn f = persist ? p_ops->persist : p_ops->flush;

  /*
  ANCHOR : ULOG_OPERATION_AND and ULOG_OPERATION_OR have only to do with bitmaps
                   therefore before applied they should update bitmap signature
  */
  switch (t) {
    case ULOG_OPERATION_AND:
    case ULOG_OPERATION_OR:
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      LOG(10,
          "REDO AND/OR tcv: %ld offset: %ld prev value: %lx value applied: %lx "
          "final value: %lx",
          ev->tcv, offset, *dst, ev->value, *dst | ev->value);
      sec_ulog_redo_bitmap_action_recovery(ev, dst, tx_lane_id, p_ops, t);
      f(p_ops->base_v, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_SET:
      ev = (struct ulog_entry_val *)e;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      LOG(10,
          "REDO SET tcv: %ld offset: %ld prev value: %lx value applied: %lx "
          "final value: %lx",
          ev->tcv, offset, *dst, ev->value, ev->value);
      *dst = ev->value;
      f(p_ops->base_v, dst, sizeof(uint64_t), PMEMOBJ_F_RELAXED);
      break;
    case ULOG_OPERATION_BUF_SET:
      eb = (struct ulog_entry_buf *)e;
      LOG(10, "APPLY BUF SET tcv : %ld", eb->tcv);
      dst_size = ((struct ulog_entry_buf *)eb)->size;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      pmemops_memset(p_ops, dst, *eb->data, dst_size,
                     PMEMOBJ_F_RELAXED | PMEMOBJ_F_MEM_NODRAIN);
      break;
    case ULOG_OPERATION_BUF_CPY:
      eb = (struct ulog_entry_buf *)e;
      LOG(10, "APPLY BUF CPY tcv : %ld", eb->tcv);
      dst_size = ((struct ulog_entry_buf *)eb)->size;
      VALGRIND_ADD_TO_TX(dst, dst_size);
      pmemops_memcpy(p_ops, dst, eb->data, dst_size,
                     PMEMOBJ_F_RELAXED | PMEMOBJ_F_MEM_NODRAIN);
      break;
    default:
      ASSERT(0);
  }
  VALGRIND_REMOVE_FROM_TX(dst, dst_size);
}

/*
 * sec_ulog_entry_apply_decrypted_check -- checks for an applied modification
 */
int sec_ulog_entry_apply_decrypted_check(const struct ulog_entry_base *e,
                                         int persist,
                                         const struct pmem_ops *p_ops,
                                         uint64_t tx_lane_id) {
  int ret = 0;
  LOG(10, "entry offset : %p", e);

  ulog_operation_type t = ulog_entry_type(e);
  uint64_t offset = ulog_entry_offset(e);
  LOG(10, "base : %p offset : %ld type : %ld", p_ops->base, offset, t);
  uint64_t *dst = (uint64_t *)((uintptr_t)p_ops->base + offset);

  struct ulog_entry_val *ev;

  switch (t) {
    case ULOG_OPERATION_AND:
    case ULOG_OPERATION_OR:
      ev = (struct ulog_entry_val *)e;
      ret = sec_ulog_redo_bitmap_action_check(ev, dst, tx_lane_id, p_ops, t);
      break;
    case ULOG_OPERATION_SET:
      ev = (struct ulog_entry_val *)e;
      if (*dst == ev->value) ret = 1;
      break;
    case ULOG_OPERATION_BUF_SET:
    case ULOG_OPERATION_BUF_CPY:
      break;
    default:
      ASSERT(0);
  }
  return ret;
}

/*
 * sec_ulog_process_entry -- (internal) processes a single ulog entry
 */
static int __attribute__((unused))
sec_ulog_process_entry(struct ulog_entry_base *e, void *arg,
                       const struct pmem_ops *p_ops, uint64_t tx_lane_id) {
  sec_ulog_entry_apply(e, 0, p_ops, tx_lane_id);

  return 0;
}

/*
 * sec_ulog_counter_check -- (internal) processes a single ulog entry
 */
int sec_ulog_counter_check(struct ulog_entry_base *e, void *arg,
                           const uint64_t expected_counter) {
  switch (ulog_entry_type(e)) {
    case ULOG_OPERATION_AND:
    case ULOG_OPERATION_OR:
    case ULOG_OPERATION_SET:
      return (((struct ulog_entry_val *)e)->tcv == expected_counter);
    case ULOG_OPERATION_BUF_SET:
    case ULOG_OPERATION_BUF_CPY: {
      return (((struct ulog_entry_buf *)e)->tcv == expected_counter);
    }
    default:
      ASSERT(0);
  }

  return 0;
}

/*
 * Apply the volatile decrypted log entries!
 */
void sec_ulog_apply_entries(struct ulog_vol_entries *decr_entries, int persist,
                            const struct pmem_ops *p_ops, uint64_t tx_lane_id) {
  struct ulog_vol_entries *curr = decr_entries;
  while (curr != NULL) {
    sec_ulog_entry_apply_decrypted((struct ulog_entry_base *)curr->ulog_entry,
                                   persist, p_ops, tx_lane_id);
    curr = curr->next;
  }
}

/*
 * Check if first entry is applied to move on with the rest
 */
int first_entry_applied(struct ulog_vol_entries *decr_entries, int persist,
                        const struct pmem_ops *p_ops, uint64_t tx_lane_id) {
  int ret = 0;
  struct ulog_vol_entries *curr = decr_entries;
  struct ulog_entry_base *e;

  if (curr != NULL) {
    e = (struct ulog_entry_base *)((uintptr_t)curr->ulog_entry);
    if (e->offset != 0)
      ret = sec_ulog_entry_apply_decrypted_check(e, persist, p_ops, tx_lane_id);
  }
  return ret;
}

/*
 * Apply the volatile decrypted redo log entries!
 */
void sec_ulog_apply_entries_redo(struct ulog_vol_entries *decr_entries,
                                 int persist, const struct pmem_ops *p_ops,
                                 uint64_t tx_lane_id) {
  struct ulog_vol_entries *curr = decr_entries;
  struct ulog_entry_base *e;

  while (curr != NULL) {
    for (size_t offset = 0;; offset += sizeof(struct ulog_entry_val)) {
      e = (struct ulog_entry_base *)((uintptr_t)curr->ulog_entry + offset);
      if (e->offset != 0)
        sec_ulog_entry_apply_decrypted_recovery(e, persist, p_ops, tx_lane_id);
      else
        break;
    }
    curr = curr->next;
  }
}

/*
 * After applied, free the volatile decrypted log entries!
 */
void sec_ulog_free_entries(struct ulog_vol_entries *decr_entries) {
  struct ulog_vol_entries *curr = decr_entries;
  struct ulog_vol_entries *prev;
  // ANCHOR: TODO
  while (curr != NULL) {
    prev = curr;
    curr = curr->next;
    free(prev->ulog_entry);
    free(prev);
  }
}

/*
 * sec_ulog_process_persistent_redo -- process ulog entries
 */
int sec_ulog_process_persistent_redo(
    struct ulog *ulog, ulog_check_offset_fn check, const struct pmem_ops *p_ops,
    struct Counter *tcv, uint64_t starting_counter, uint64_t tx_lane_id) {
  LOG(15, "ulog %p", ulog);
  int ret = 0;
  // case of persistent redo ulog - consulted only on recovery or abort
  // ANCHOR : COUNTERS : verify counter sequentiality
  // 1. get entries in volatile memory
  // 2. check their integrity and sequentiality
  // 3. if ok, process the entries

  struct ulog_vol_entries *decr_entries;
  decr_entries = ulog_fetch_each_entry_redo(ulog, sec_ulog_counter_check, NULL,
                                            p_ops, tcv, starting_counter);

  if (first_entry_applied(decr_entries, 0, p_ops, tx_lane_id)) {
    sec_ulog_apply_entries_redo(decr_entries, 0, p_ops, tx_lane_id);
    ret = 1;
  }
  // free the decr entries!
  sec_ulog_free_entries(decr_entries);
  return ret;
}

/*
 * sec_ulog_process -- process ulog entries
 */
void sec_ulog_process(struct ulog *ulog, ulog_check_offset_fn check,
                      const struct pmem_ops *p_ops, struct Counter *tcv,
                      uint64_t starting_counter, uint64_t tx_lane_id) {
  LOG(15, "ulog %p", ulog);

  if (!SEC_OBJ_PTR_FROM_POOL(p_ops->base, p_ops->base_v, ulog)) {
    // case of volatile redo log that can be applied directly w/o decryption
    sec_ulog_foreach_entry_shadow_log(ulog, sec_ulog_process_entry, NULL,
                                      p_ops);
  } else {
    // case of persistent ulog - consulted only on recovery or abort
    // ANCHOR : COUNTERS : verify counter sequentiality
    // 1. get entries in volatile memory
    // 2. check their integrity and sequentiality
    // 3. if ok, process the entries

    struct ulog_vol_entries *decr_entries;

    decr_entries = ulog_fetch_each_entry(ulog, sec_ulog_counter_check, NULL,
                                         p_ops, tcv, starting_counter);

    sec_ulog_apply_entries(decr_entries, 0, p_ops, tx_lane_id);

    // free the decr entries!
    sec_ulog_free_entries(decr_entries);
  }
}

/*
 * sec_ulog_recovery_needed -- checks if the logs needs recovery
 */
int sec_ulog_recovery_needed(struct ulog *ulog, int verify_checksum,
                             const struct pmem_ops *p_ops) {
  LOG(16, NULL);
  struct ulog *ulog_v = ulog_hdr_decrypt(ulog, p_ops);
  size_t nbytes = 0;
  if (ulog_v != NULL) {
    nbytes = MIN(sec_ulog_base_nbytes_redo(ulog, p_ops), ulog_v->capacity);
  } else {
    // only if epc_lookup did not find the log should return 0
    // as in this case it was not properly written
    // in any other case -> inconsistent data!
    uint64_t off = (uintptr_t)ulog - (uintptr_t)p_ops->base;
    if (!epc_lookup((uint8_t *)&off)) {
      return 0;
    } else {
      printf("inconsistent log header data\n");
      exit(1);
    }
  }

  if (nbytes == 0) {
    return 0;
  }

  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = ((PMEMobjpool *)(p_ops->base_v))->uuid_lo;
  iv[1] = (uintptr_t)ulog - (uintptr_t)(p_ops->base);
  uint8_t *ulog_data =
      decrypt_final(ulog->data, ulog_v->data_size, (uint8_t *)ulog_v->data_tag,
                    NULL, 0, (uint8_t *)iv);
  ASSERT(ulog_data != NULL);

  if (verify_checksum && !sec_ulog_checksum(ulog_v, ulog_data, nbytes, 0)) {
    return 0;
  }
  return 1;
}

/*
 * sec_ulog_recover -- recovery of ulog
 *
 * The sec_ulog_recover shall be preceded by sec_ulog_check call.
 */
int sec_ulog_recover(struct ulog *ulog, ulog_check_offset_fn check,
                     const struct pmem_ops *p_ops, uint64_t tx_lane_id,
                     int log_idx, uint64_t tx_stage) {
  int ret = 0;
  if (sec_ulog_recovery_needed(ulog, 1, p_ops)) {
    if (sec_ulog_process_persistent_redo(
            ulog, check, p_ops,
            get_counter(
                ((int)tx_lane_id * LANE_TOTAL_LOGS * LOG_COUNTERS + log_idx) +
                METADATA_COUNTERS),
            get_counter(
                (((int)tx_lane_id * LANE_TOTAL_LOGS * LOG_COUNTERS + log_idx) +
                 1) +
                METADATA_COUNTERS)
                ->_counter,
            tx_lane_id)) {
      ret = append_tx_info_entry(
          pmemobj_get_uuid_lo(p_ops->base_v), tx_lane_id,
          tx_stage);  // end of lane recovery as redo-log is applied
    }

    write_counters_mmap(
        "/dev/shm/amcs");  // manual writing -- need to wait for stability
  }
  return ret;
}

/*
 * sec_ulog_construct -- initializes the ulog structure
 */
void sec_ulog_construct(uint64_t offset, size_t capacity, uint64_t gen_num,
                        int flush, uint64_t flags, const struct pmem_ops *p_ops,
                        uint64_t tx_lane_id) {
  struct ulog *ulog = ulog_by_offset(offset, p_ops);
  ASSERTne(ulog, NULL);

  int to_be_freed = 0;
  struct ulog *ulog_v = ulog_hdr_decrypt(ulog, p_ops);
  if (ulog_v == NULL) {
    // if ulog not already used, create a new volatile representation
    to_be_freed = 1;
    ulog_v = (struct ulog *)malloc(sizeof(struct ulog));
    ASSERTne(ulog_v, NULL);
  }

  size_t diff = OBJ_PTR_TO_OFF(p_ops->base, ulog) - offset;
  if (diff > 0) capacity = ALIGN_DOWN(capacity - diff, CACHELINE_SIZE);

  VALGRIND_ADD_TO_TX(ulog, SIZEOF_ULOG(capacity));

  ulog_v->capacity = capacity;
  ulog_v->checksum = 0;
  ulog_v->next = 0;
  ulog_v->gen_num = gen_num;
  ulog_v->tx_lane_id = tx_lane_id;  // ANCHOR : initial value on the log
                                    // creation / refers to no transaction
  ulog_v->tcv =
      0;  // ANCHOR : initial value to avoid create unnecessary counters
  ulog_v->flags = flags;
  ulog_v->data_size = 0;
  memset(ulog_v->data_tag, 0, sizeof(ulog_v->data_tag));
  memset(ulog_v->unused, 0, sizeof(ulog_v->unused));

  PMEMoid log_oid = {pmemobj_get_uuid_lo((PMEMobjpool *)(p_ops->base_v)),
                     (uintptr_t)ulog - (uintptr_t)(p_ops->base)};
  uint8_t tag[HMAC_SIZE];
  struct ulog *ulog_encr = ulog_hdr_encrypt(ulog_v, (uint8_t *)&log_oid, tag);

  append_undo_object_entry(log_oid, tag, OBJ_NLANES, sizeof(struct ulog), 0, 1);

  /* we only need to zero out the header of ulog's first entry */
  size_t zeroed_data = CACHELINE_ALIGN(sizeof(struct ulog_entry_base));

  // here we do not have to care about transactions and atomicity!
  // as if this is incomplete, the ulog->next field that points to this log will
  // not become persisent, so this data part will not be accessed before actually
  // be reallocated
  pmemops_memcpy(p_ops, ulog, ulog_encr, sizeof(struct ulog), 0);

  if (to_be_freed) free(ulog_v);  // it's created by malloc
  free(ulog_encr);

  if (flush) {
    pmemops_xflush(p_ops, ulog, sizeof(*ulog), PMEMOBJ_F_RELAXED);
    pmemops_memset(
        p_ops, ulog->data, 0, zeroed_data,
        PMEMOBJ_F_MEM_NONTEMPORAL | PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_RELAXED);
  } else {
    /*
     * We want to avoid replicating zeroes for every ulog of every
     * lane, to do that, we need to use plain old memset.
     */
    memset(ulog->data, 0, zeroed_data);
  }
}

/*
 * sec_ulog_entry_buf_create -- atomically creates a buffer entry in the secured
 * log
 */
struct ulog_entry_buf *sec_ulog_entry_buf_create(
    struct ulog *ulog, size_t offset, uint64_t gen_num, uint64_t *dest,
    const void *src, uint64_t size, ulog_operation_type type,
    const struct pmem_ops *p_ops, const uint64_t obj_off, ...) {
  LOG(11, NULL);

  struct ulog_entry_buf *e = (struct ulog_entry_buf *)(ulog->data + offset);

  /*
   * Depending on the size of the source buffer, we might need to perform
   * up to three separate copies:
   *	1. The first cacheline, 48b of metadata and 16 of data
   * If there's still data to be logged:
   *	2. The entire remainder of data data aligned down to cacheline,
   *	for example, if there's 150b left, this step will copy only
   *	128b.
   * Now, we are left with between 0 to 63 bytes. If nonzero:
   *	3. Create a stack allocated cacheline-sized buffer, fill in the
   *	remainder of the data, and copy the entire cacheline.
   *
   * This is done so that we avoid a cache-miss on misaligned writes.
   */

  struct ulog_entry_buf *b = alloca(CACHELINE_SIZE);

  b->obj_off = obj_off;
  b->base.offset = (uint64_t)(dest);
  b->base.offset |= ULOG_OPERATION(type);
  b->size = size;
  b->checksum = 0;

  LOG(10, "src : %p  dest : %p size : %ld volatile_entry : %p", src, e, size,
      b);
  size_t bdatasize = CACHELINE_SIZE - sizeof(struct ulog_entry_buf);
  size_t ncopy = MIN(size, bdatasize);
  memcpy(b->data, src, ncopy);  // copy of the data from the pmem
  memset(b->data + ncopy, 0, bdatasize - ncopy);

  size_t remaining_size = ncopy > size ? 0 : size - ncopy;

  char *srcof = (char *)src + ncopy;
  size_t rcopy = ALIGN_DOWN(remaining_size, CACHELINE_SIZE);
  size_t lcopy = remaining_size - rcopy;

  uint8_t last_cacheline[CACHELINE_SIZE];
  LOG(10,
      "srcof : %p  ncopy : %ld rcopy : %ld lcopy : %ld remaining_size : %ld",
      srcof, ncopy, rcopy, lcopy, remaining_size);
  if (lcopy != 0) {
    memcpy(last_cacheline, srcof + rcopy, lcopy);
    memset(last_cacheline + lcopy, 0, CACHELINE_SIZE - lcopy);
  }

  struct ulog_entry_buf *volatile_entry = NULL;
  if (posix_memalign((void **)&volatile_entry, (size_t)CACHELINE_SIZE,
                     CACHELINE_ALIGN(sizeof(struct ulog_entry_buf) + size))) {
    perror("posix_memalign failed");
    exit(EXIT_FAILURE);
  }

  if (rcopy != 0) {
    void *dest = volatile_entry->data + ncopy;
    ASSERT(IS_CACHELINE_ALIGNED(dest));
    memcpy(dest, srcof, rcopy);
  }

  if (lcopy != 0) {
    void *dest = volatile_entry->data + ncopy + rcopy;
    ASSERT(IS_CACHELINE_ALIGNED(dest));
    memcpy(dest, last_cacheline, CACHELINE_SIZE);
  }

  /*
   * Allow having uninitialized data in the buffer - this requires marking
   * data as defined so that comparing checksums is not reported as an
   * error by memcheck.
   */
#if VG_MEMCHECK_ENABLED
  if (On_valgrind) {
    VALGRIND_MAKE_MEM_DEFINED(e->data, ncopy + rcopy + lcopy);
    VALGRIND_MAKE_MEM_DEFINED(&e->checksum, sizeof(e->checksum));
  }
#endif
  va_list args;
  va_start(args, obj_off);
  struct Counter *_tcv = va_arg(args, struct Counter *);
  va_end(args);
  b->tcv = _tcv->_counter;
  b->entry_sign[0] = 0;  // init for volatile checksum
  b->entry_sign[1] = 0;  // init for volatile checksum

  b->checksum = util_checksum_seq(b, CACHELINE_SIZE, 0);
  if (rcopy != 0) b->checksum = util_checksum_seq(srcof, rcopy, b->checksum);
  if (lcopy != 0)
    b->checksum =
        util_checksum_seq(last_cacheline, CACHELINE_SIZE, b->checksum);

  b->checksum = util_checksum_seq(&gen_num, sizeof(gen_num), b->checksum);

  ASSERT(IS_CACHELINE_ALIGNED(e));
  ASSERT(IS_CACHELINE_ALIGNED(volatile_entry));
  memcpy(volatile_entry, b, CACHELINE_SIZE);

  uint8_t *ciphertext;
  uint8_t tag[HMAC_SIZE];
  PMEMoid log_entry_oid = {((PMEMobjpool *)(p_ops->base_v))->uuid_lo,
                           (uintptr_t)e - (uintptr_t)p_ops->base};

  ciphertext = encrypt_final_two_parts(
      (uint8_t *)volatile_entry, offsetof(struct ulog_entry_buf, size),
      (uint8_t *)volatile_entry->data, volatile_entry->size, tag, NULL, 0,
      (uint8_t *)&log_entry_oid);  // for the header of the entry and the data
                                   // to be encrypted

  // persist the encrypted header
  pmemops_memcpy(p_ops, e, ciphertext, offsetof(struct ulog_entry_buf, size),
                 PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_MEM_NONTEMPORAL);

  // persist the encrypted data
  pmemops_memcpy(p_ops, e->data,
                 ciphertext + offsetof(struct ulog_entry_buf, size), size,
                 PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_MEM_NONTEMPORAL);

  // fill rest entry with zeros
  pmemops_memset(p_ops, e->data + size, 0,
                 CACHELINE_ALIGN(sizeof(struct ulog_entry_buf) + size) -
                     sizeof(struct ulog_entry_buf) - size,
                 0);

  free(ciphertext);

  // persist the size
  pmemops_memcpy(p_ops, &e->size, &volatile_entry->size, sizeof(uint64_t),
                 PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_MEM_NONTEMPORAL);
  // persist the tag
  pmemops_memcpy(p_ops, e->entry_sign, tag, HMAC_SIZE,
                 PMEMOBJ_F_MEM_NODRAIN | PMEMOBJ_F_MEM_NONTEMPORAL);

  pmemops_drain(p_ops);

#ifdef WRITE_AMPL
  bytes_written_inc(UNDO_LOG_DATA, size + sizeof(struct ulog_entry_buf));
  bytes_written_inc(UNDO_LOG_DATA_PMDK,
                    size + sizeof(struct ulog_entry_buf_pmdk));
#endif

#ifdef DEBUG
  ASSERT(sec_ulog_entry_valid(
      ulog, &e->base,
      p_ops));  // to temporary check that the persistent entry is fine
#endif

  inc(_tcv);
  free(volatile_entry);
  return e;
}

void *sec_ulog_entry_verify(const struct ulog_entry_base *entry,
                            const struct pmem_ops *p_ops) {
  uint8_t *decryptedHeader;
  uint64_t entry_size =
      ((struct ulog_entry_buf *)entry)
          ->size;  // entry buf and entry val have the size at the same order
  uint8_t *temp =
      NULL;  // variable to construct the log entry from the decrypted data
  uint64_t iv[IV_SIZE_UINT64];
  iv[0] = ((PMEMobjpool *)p_ops->base_v)->uuid_lo;
  iv[1] = (uintptr_t)entry - (uintptr_t)p_ops->base;

  if (entry_size == UINT64_MAX)  // we have redo log entry
  {
    return NULL;
  } else  // we have undo log entry ,  needs the additional data to be decrypted
  {
    temp = (uint8_t *)malloc(CACHELINE_ALIGN(ULOG_BUF_HDR_SIZE + entry_size) *
                             sizeof(uint8_t));
    ((struct ulog_entry_buf *)temp)->size = entry_size;
    decryptedHeader = decrypt_final_two_parts(
        (uint8_t *)entry, ULOG_BUF_HDR_SIZE - (HMAC_SIZE + sizeof(uint64_t)),
        (uint8_t *)((struct ulog_entry_buf *)entry)->data, entry_size,
        (uint8_t *)((struct ulog_entry_buf *)entry)->entry_sign, NULL, 0,
        (uint8_t *)iv);
    memcpy(((struct ulog_entry_buf *)temp)->data,
           decryptedHeader + ULOG_BUF_HDR_SIZE - (HMAC_SIZE + sizeof(uint64_t)),
           entry_size);
    memset(((struct ulog_entry_buf *)temp)->data + entry_size, 0,
           CACHELINE_ALIGN(sizeof(struct ulog_entry_buf) + entry_size) -
               ULOG_BUF_HDR_SIZE - entry_size);
  }

  if (decryptedHeader != NULL) {
    memcpy(temp, decryptedHeader,
           ULOG_BUF_HDR_SIZE - (HMAC_SIZE + sizeof(uint64_t)));
  } else {
    printf("Error while decrypting ulog entry\n");
    exit(1);
  }

  free(decryptedHeader);
  return temp;
}

#endif