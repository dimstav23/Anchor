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
 * ulog.h -- unified log public interface
 */

#ifndef LIBPMEMOBJ_ULOG_H
#define LIBPMEMOBJ_ULOG_H 1

#include <stddef.h>
#include <stdint.h>
#include "vec.h"
#include "pmemops.h"

#ifdef ANCHOR_FUNCS
#include "trusted_counter.h"
#endif

struct ulog_entry_base {
	uint64_t offset; /* offset with operation type flag */
};

#define ULOG_BUF_HDR_SIZE 56//56
/*
 * ulog_entry_val -- log entry
 */
struct ulog_entry_val {
	struct ulog_entry_base base;
	uint64_t value; /* value to be applied */
	uint64_t tcv; /*trusted counter value */
	uint64_t bitmap_offset; /*in case of OR/AND operation for bitmap set : assign bitmap_offset for decryption*/
	uint64_t size; /*in case of redo log : assign size = 0 to differentiate for decryption*/
	//uint64_t entry_sign[2]; /*entry signature HMAC */
};

/*
 * ulog_entry_buf - ulog buffer entry
 */
struct ulog_entry_buf {
	struct ulog_entry_base base; /* offset with operation type flag */
	uint64_t obj_off; /* object offset that entry refers to */
	uint64_t checksum; /* checksum of the entire log entry */	
	uint64_t tcv; /*trusted counter value */
	uint64_t size; /* size of the buffer to be modified - stored unencrypted*/
	uint64_t entry_sign[2]; /*entry signature HMAC */
	uint8_t data[]; /* content to fill in */
};

//#define ULOG_UNUSED ((CACHELINE_SIZE - 56) / 8)
#define ULOG_UNUSED ((CACHELINE_SIZE - 16) / 8)
/*
 * This structure *must* be located at a cacheline boundary. To achieve this,
 * the next field is always allocated with extra padding, and then the offset
 * is additionally aligned.
 */
#define ULOG(capacity_bytes) {\
	/* 64 bytes of metadata */\
	uint64_t checksum; /* checksum of ulog header and its entries */\
	uint64_t next; /* offset of ulog extension */\
	uint64_t capacity; /* capacity of this ulog in bytes */\
	uint64_t gen_num; /* generation counter */\
	uint64_t flags; /* ulog flags */\
	uint64_t tcv; /*trusted counter value */\
	uint64_t tx_lane_id; /* lane - tx_id this log refers to */\
	uint64_t data_tag[2]; /* for redo log data */\
	uint64_t data_size; /* payload for ulog entries - for redo log*/\
	uint64_t unused[ULOG_UNUSED]; /* must be 0 */\
	uint8_t data[capacity_bytes]; /* N bytes of data */\
}\

#define SEC_ULOG(capacity_bytes, tcv) {\
	/* 64 bytes of metadata */\
	uint64_t checksum; /* checksum of ulog header and its entries */\
	uint64_t next; /* offset of ulog extension */\
	uint64_t capacity; /* capacity of this ulog in bytes */\
	uint64_t gen_num; /* generation counter */\
	uint64_t flags; /* ulog flags */\
	uint64_t tcv = tcv; /*trusted counter value */\
	uint64_t hdr_sign[2]; /*ulog header signature*/\
	uint8_t data[capacity_bytes]; /* N bytes of data */\
}\

#define SIZEOF_ULOG(base_capacity)\
(sizeof(struct ulog) + base_capacity)

/*
 * Ulog buffer allocated by the user must be marked by this flag.
 * It is important to not free it at the end:
 * what user has allocated - user should free himself.
 */
#define ULOG_USER_OWNED (1U << 0)

/* use this for allocations of aligned ulog extensions */
#define SIZEOF_ALIGNED_ULOG(base_capacity)\
ALIGN_UP(SIZEOF_ULOG(base_capacity + (2 * CACHELINE_SIZE)), CACHELINE_SIZE)

struct ulog ULOG(0);

#ifdef WRITE_AMPL
struct ulog_entry_val_pmdk {
	struct ulog_entry_base base;
	uint64_t value; /* value to be applied */
};

/*
 * ulog_entry_buf - ulog buffer entry
 */
struct ulog_entry_buf_pmdk {
	struct ulog_entry_base base; /* offset with operation type flag */
	uint64_t checksum; /* checksum of the entire log entry */
	uint64_t size; /* size of the buffer to be modified - stored unencrypted*/
	uint8_t data[]; /* content to fill in */
};


#define ULOG_PMDK_UNUSED ((CACHELINE_SIZE - 40) / 8)
struct ulog_pmdk {
	/* 64 bytes of metadata */
	uint64_t checksum; /* checksum of ulog header and its entries */
	uint64_t next; /* offset of ulog extension */
	uint64_t capacity; /* capacity of this ulog in bytes */
	uint64_t gen_num; /* generation counter */
	uint64_t flags; /* ulog flags */
	uint64_t unused[ULOG_PMDK_UNUSED]; /* must be 0 */
	uint8_t data[]; /* N bytes of data */
};
#endif

VEC(ulog_next, uint64_t);

typedef uint64_t ulog_operation_type;

#define ULOG_OPERATION_SET		(0b000ULL << 61ULL)
#define ULOG_OPERATION_AND		(0b001ULL << 61ULL)
#define ULOG_OPERATION_OR		(0b010ULL << 61ULL)
#define ULOG_OPERATION_BUF_SET		(0b101ULL << 61ULL)
#define ULOG_OPERATION_BUF_CPY		(0b110ULL << 61ULL)

#define ULOG_BIT_OPERATIONS (ULOG_OPERATION_AND | ULOG_OPERATION_OR)

/* immediately frees all associated ulog structures */
#define ULOG_FREE_AFTER_FIRST (1U << 0)
/* increments gen_num of the first, preallocated, ulog */
#define ULOG_INC_FIRST_GEN_NUM (1U << 1)
/* informs if there was any buffer allocated by user in the tx  */
#define ULOG_ANY_USER_BUFFER (1U << 2)

typedef int (*ulog_check_offset_fn)(void *ctx, uint64_t offset);
#ifdef ANCHOR_FUNCS
typedef int (*ulog_extend_fn)(void *, uint64_t *, uint64_t, struct ulog *ulog_v,
								int is_first_log, uint64_t tx_lane_id);
#else
typedef int (*ulog_extend_fn)(void *, uint64_t *, uint64_t);
#endif
typedef int (*ulog_entry_cb)(struct ulog_entry_base *e, void *arg,
	const struct pmem_ops *p_ops);
typedef void (*ulog_free_fn)(void *base, uint64_t *next);
typedef int (*ulog_rm_user_buffer_fn)(void *, void *addr);

struct ulog *ulog_next(struct ulog *ulog, const struct pmem_ops *p_ops);

void ulog_construct(uint64_t offset, size_t capacity, uint64_t gen_num,
		int flush, uint64_t flags, const struct pmem_ops *p_ops);


size_t ulog_capacity(struct ulog *ulog, size_t ulog_base_bytes,
	const struct pmem_ops *p_ops);
void ulog_rebuild_next_vec(struct ulog *ulog, struct ulog_next *next,
	const struct pmem_ops *p_ops);

int ulog_foreach_entry(struct ulog *ulog,
	ulog_entry_cb cb, void *arg, const struct pmem_ops *ops);

int ulog_reserve(struct ulog *ulog,
	size_t ulog_base_nbytes, size_t gen_num,
	int auto_reserve, size_t *new_capacity_bytes,
	ulog_extend_fn extend, struct ulog_next *next,
	const struct pmem_ops *p_ops);

void ulog_store(struct ulog *dest,
	struct ulog *src, size_t nbytes, size_t ulog_base_nbytes,
	size_t ulog_total_capacity,
	struct ulog_next *next, const struct pmem_ops *p_ops);

int ulog_free_next(struct ulog *u, const struct pmem_ops *p_ops,
		ulog_free_fn ulog_free, ulog_rm_user_buffer_fn user_buff_remove,
		uint64_t flags);
void ulog_clobber(struct ulog *dest, struct ulog_next *next,
	const struct pmem_ops *p_ops);
#ifdef ANCHOR_FUNCS
int ulog_clobber_data(struct ulog *dest,
	size_t nbytes, size_t ulog_base_nbytes,
	struct ulog_next *next, ulog_free_fn ulog_free,
	ulog_rm_user_buffer_fn user_buff_remove,
	const struct pmem_ops *p_ops, unsigned flags, uint64_t tx_lane_id);
#else
int ulog_clobber_data(struct ulog *dest,
	size_t nbytes, size_t ulog_base_nbytes,
	struct ulog_next *next, ulog_free_fn ulog_free,
	ulog_rm_user_buffer_fn user_buff_remove,
	const struct pmem_ops *p_ops, unsigned flags);
#endif
void ulog_clobber_entry(const struct ulog_entry_base *e,
	const struct pmem_ops *p_ops);

void ulog_process(struct ulog *ulog, ulog_check_offset_fn check,
	const struct pmem_ops *p_ops);

size_t ulog_base_nbytes(struct ulog *ulog);

int ulog_recovery_needed(struct ulog *ulog, int verify_checksum);
struct ulog *ulog_by_offset(size_t offset, const struct pmem_ops *p_ops);

uint64_t ulog_entry_offset(const struct ulog_entry_base *entry);
ulog_operation_type ulog_entry_type(
	const struct ulog_entry_base *entry);

struct ulog_entry_val *ulog_entry_val_create(struct ulog *ulog,
	size_t offset, uint64_t *dest, uint64_t value,
	ulog_operation_type type, const struct pmem_ops *p_ops);

struct ulog_entry_buf *
ulog_entry_buf_create(struct ulog *ulog, size_t offset,
	uint64_t gen_num, uint64_t *dest, const void *src, uint64_t size,
	ulog_operation_type type, const struct pmem_ops *p_ops);

void ulog_entry_apply(const struct ulog_entry_base *e, int persist,
	const struct pmem_ops *p_ops);

size_t ulog_entry_size(const struct ulog_entry_base *entry);

void ulog_recover(struct ulog *ulog, ulog_check_offset_fn check,
	const struct pmem_ops *p_ops);

int ulog_check(struct ulog *ulog, ulog_check_offset_fn check,
	const struct pmem_ops *p_ops);

int ulog_entry_valid(struct ulog *ulog, const struct ulog_entry_base *entry);

#ifdef ANCHOR_FUNCS

struct ulog_vol_entries{
	void* ulog_entry;
	struct ulog_vol_entries* next;
};

struct ulog *ulog_hdr_decrypt(struct ulog *ulog, const struct pmem_ops *p_ops);
struct ulog *ulog_hdr_encrypt(struct ulog *ulog, uint8_t *iv, uint8_t *tag);

typedef int (*sec_ulog_counter_cb)(struct ulog_entry_base *e, void *arg,
	const uint64_t expected_counter);

size_t sec_ulog_base_nbytes_redo(struct ulog *ulog, const struct pmem_ops* p_ops);
size_t sec_ulog_base_nbytes_undo(struct ulog *ulog, const struct pmem_ops* p_ops);

void ulog_header_validate(const struct pmem_ops* p_ops, uint64_t tx_lane_id);

struct ulog *sec_ulog_next(struct ulog *ulog, const struct pmem_ops *p_ops);

int sec_ulog_foreach_entry(struct ulog *ulog,
	ulog_entry_cb cb, void *arg, const struct pmem_ops *ops);

typedef int (*ulog_entry_cb_shadow_log)(struct ulog_entry_base *e, void *arg,
	const struct pmem_ops *p_ops, uint64_t tx_lane_id);

int sec_ulog_foreach_entry_shadow_log(struct ulog *ulog,
	ulog_entry_cb_shadow_log cb, void *arg, const struct pmem_ops *ops);

struct ulog_vol_entries*
ulog_fetch_each_entry(struct ulog *ulog, sec_ulog_counter_cb cb, void *arg, 
		const struct pmem_ops *ops, struct Counter *tcv, uint64_t starting_counter);

struct ulog_vol_entries*
ulog_fetch_each_entry_redo(struct ulog *ulog, sec_ulog_counter_cb cb, void *arg, 
		const struct pmem_ops *ops, struct Counter *tcv, uint64_t starting_counter);

void sec_ulog_apply_entries(struct ulog_vol_entries* decr_entries, int persist, 
							const struct pmem_ops *p_ops, uint64_t tx_lane_id);

void sec_ulog_apply_entries_redo(struct ulog_vol_entries* decr_entries, int persist, 
							const struct pmem_ops *p_ops, uint64_t tx_lane_id);

void sec_ulog_free_entries(struct ulog_vol_entries* decr_entries);

int sec_ulog_counter_check(struct ulog_entry_base *e, void *arg,
	const uint64_t expected_counter);

size_t sec_ulog_entry_size_decrypted(const struct ulog_entry_base *entry);

void sec_ulog_construct(uint64_t offset, size_t capacity, uint64_t gen_num,
		int flush, uint64_t flags, const struct pmem_ops *p_ops, uint64_t tx_lane_id);

size_t sec_ulog_capacity(struct ulog *ulog, size_t ulog_base_bytes,
	const struct pmem_ops *p_ops);
void sec_ulog_rebuild_next_vec(struct ulog *ulog, struct ulog_next *next,
	const struct pmem_ops *p_ops);

int sec_ulog_reserve(struct ulog *ulog,
	size_t ulog_base_nbytes, size_t gen_num,
	int auto_reserve, size_t *new_capacity_bytes,
	ulog_extend_fn extend, struct ulog_next *next,
	const struct pmem_ops *p_ops, uint64_t tx_lane_id);

void sec_ulog_store(struct ulog *dest,
	struct ulog *src, size_t nbytes, size_t ulog_base_nbytes,
	size_t ulog_total_capacity,
	struct ulog_next *next, const struct pmem_ops *p_ops,
	uint64_t tx_lane_id);

int sec_ulog_check(struct ulog *ulog, ulog_check_offset_fn check,
	const struct pmem_ops *p_ops);

void sec_ulog_process(struct ulog *ulog, ulog_check_offset_fn check,
	const struct pmem_ops *p_ops, struct Counter* tcv, uint64_t starting_counter, uint64_t tx_lane_id);

int sec_ulog_process_persistent_redo(struct ulog *ulog, ulog_check_offset_fn check,
	const struct pmem_ops *p_ops, struct Counter* tcv, uint64_t starting_counter, uint64_t tx_lane_id);

int sec_ulog_recovery_needed(struct ulog *ulog, int verify_checksum, 
							const struct pmem_ops *p_ops);

struct ulog_entry_buf *sec_ulog_entry_buf_create(struct ulog *ulog, 
	size_t offset, uint64_t gen_num, uint64_t *dest, const void *src, uint64_t size,
	ulog_operation_type type, const struct pmem_ops *p_ops, const uint64_t oid,
	...);

struct ulog_entry_val *sec_ulog_entry_val_create(struct ulog *ulog,
	size_t offset, uint64_t *dest, uint64_t value,
	ulog_operation_type type, const struct pmem_ops *p_ops, 
	...);

size_t sec_ulog_entry_size(const struct ulog_entry_base *entry, const struct pmem_ops* p_ops);

void sec_ulog_entry_apply(const struct ulog_entry_base *e, int persist,
	const struct pmem_ops *p_ops, uint64_t tx_lane_id);

void sec_ulog_entry_apply_decrypted(const struct ulog_entry_base *e, int persist,
	const struct pmem_ops *p_ops, uint64_t tx_lane_id);
void sec_ulog_entry_apply_decrypted_recovery(const struct ulog_entry_base *e, int persist,
	const struct pmem_ops *p_ops, uint64_t tx_lane_id);
int sec_ulog_entry_apply_decrypted_check(const struct ulog_entry_base *e, int persist,
	const struct pmem_ops *p_ops, uint64_t tx_lane_id);

int first_entry_applied(struct ulog_vol_entries* decr_entries, int persist, const struct pmem_ops *p_ops, uint64_t tx_lane_id);

void sec_ulog_redo_bitmap_action(struct ulog_entry_val *ev, uint64_t *dst, uint64_t tx_lane_id, 
								const struct pmem_ops *p_ops, ulog_operation_type t);
void sec_ulog_redo_bitmap_action_recovery(struct ulog_entry_val *ev, uint64_t *dst, uint64_t tx_lane_id, 
								const struct pmem_ops *p_ops, ulog_operation_type t);
int sec_ulog_redo_bitmap_action_check(struct ulog_entry_val *ev, uint64_t *dst, uint64_t tx_lane_id, 
									const struct pmem_ops *p_ops, ulog_operation_type t);



void* sec_ulog_entry_verify(const struct ulog_entry_base *entry, const struct pmem_ops *p_ops);

int sec_ulog_recover(struct ulog *ulog, ulog_check_offset_fn check,
	const struct pmem_ops *p_ops, uint64_t tx_lane_id, int log_idx, uint64_t tx_stage);
#endif


#endif
