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
 * palloc.h -- internal definitions for persistent allocator
 */

#ifndef LIBPMEMOBJ_PALLOC_H
#define LIBPMEMOBJ_PALLOC_H 1

#include <stddef.h>
#include <stdint.h>

#include "libpmemobj.h"
#include "memops.h"
#include "ulog.h"
#include "valgrind_internal.h"
#include "stats.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PALLOC_CTL_DEBUG_NO_PATTERN (-1)

struct palloc_heap {
	struct pmem_ops p_ops;
	struct heap_layout *layout;
	struct heap_rt *rt;
	uint64_t *sizep;
	uint64_t growsize;

	struct stats *stats;
	struct pool_set *set;

	void *base;
	void *base_v;

	int alloc_pattern;
};

struct memory_block;

typedef int (*palloc_constr)(void *base, void *ptr,
		size_t usable_size, void *arg);

int palloc_operation(struct palloc_heap *heap, uint64_t off, uint64_t *dest_off,
	size_t size, palloc_constr constructor, void *arg,
	uint64_t extra_field, uint16_t object_flags,
	uint16_t class_id, uint16_t arena_id,
	struct operation_context *ctx);

int
palloc_reserve(struct palloc_heap *heap, size_t size,
	palloc_constr constructor, void *arg,
	uint64_t extra_field, uint16_t object_flags,
	uint16_t class_id, uint16_t arena_id,
	struct pobj_action *act);

void
palloc_defer_free(struct palloc_heap *heap, uint64_t off,
	struct pobj_action *act);

void
palloc_cancel(struct palloc_heap *heap,
	struct pobj_action *actv, size_t actvcnt);

void
palloc_publish(struct palloc_heap *heap,
	struct pobj_action *actv, size_t actvcnt,
	struct operation_context *ctx);

void
palloc_set_value(struct palloc_heap *heap, struct pobj_action *act,
	uint64_t *ptr, uint64_t value);

uint64_t palloc_first(struct palloc_heap *heap);
uint64_t palloc_next(struct palloc_heap *heap, uint64_t off);

size_t palloc_usable_size(struct palloc_heap *heap, uint64_t off);
uint64_t palloc_extra(struct palloc_heap *heap, uint64_t off);
uint16_t palloc_flags(struct palloc_heap *heap, uint64_t off);

int palloc_boot(struct palloc_heap *heap, void *heap_start,
		uint64_t heap_size, uint64_t *sizep,
		void *base, struct pmem_ops *p_ops,
		struct stats *stats, struct pool_set *set);

int palloc_buckets_init(struct palloc_heap *heap);

int palloc_init(void *heap_start, uint64_t heap_size, uint64_t *sizep,
	struct pmem_ops *p_ops);
void *palloc_heap_end(struct palloc_heap *h);
int palloc_heap_check(void *heap_start, uint64_t heap_size);
int palloc_heap_check_remote(void *heap_start, uint64_t heap_size,
	struct remote_ops *ops);
void palloc_heap_cleanup(struct palloc_heap *heap);
size_t palloc_heap(void *heap_start);

int palloc_defrag(struct palloc_heap *heap, uint64_t **objv, size_t objcnt,
	struct operation_context *ctx, struct pobj_defrag_result *result);

/* foreach callback, terminates iteration if return value is non-zero */
typedef int (*object_callback)(const struct memory_block *m, void *arg);

#if VG_MEMCHECK_ENABLED
void palloc_heap_vg_open(struct palloc_heap *heap, int objects);
#endif

#ifdef ANCHOR_FUNCS
void *sec_palloc_heap_end(struct palloc_heap *h);
void sec_heap_zone_metadata_alloc(struct palloc_heap *heap);
void sec_palloc_defer_free(struct palloc_heap *heap, uint64_t off, struct pobj_action *act);
int sec_palloc_operation(struct palloc_heap *heap, uint64_t off, uint64_t *dest_off, uint64_t *dest_off_v, //ANCHOR volatile offset addition
	size_t size, palloc_constr constructor, void *arg,
	uint64_t extra_field, uint16_t object_flags,
	uint16_t class_id, uint16_t arena_id,
	struct operation_context *ctx, uint64_t value_to_update);
int sec_palloc_operation_init(struct palloc_heap *heap,
	uint64_t off, size_t size, palloc_constr constructor, void *arg,
	uint64_t extra_field, uint16_t object_flags, uint16_t class_id, 
	uint16_t arena_id, struct operation_context *ctx, size_t *nops,
	uint64_t *offset, void** ops);
int sec_palloc_operation_exec(struct palloc_heap *heap,uint64_t *dest_off,
	size_t nops, void* ops,
	struct operation_context *ctx, uint64_t value_to_update);
uint8_t* sec_alloc_prepare_object_id_update(PMEMobjpool *pop, uint64_t new_value, PMEMoid *object_id,
												uint64_t internal_obj_off, struct operation_context *ctx);
uint8_t* sec_free_prepare_object_id_update(PMEMobjpool *pop, uint64_t new_value, PMEMoid *object_id,
												uint64_t internal_obj_off, struct operation_context *ctx);
uint8_t* sec_alloc_root_prepare_pool_header(PMEMobjpool *pop, uint64_t new_value, uint64_t size, 
												struct operation_context *ctx);
uint8_t* sec_extend_prepare_ulog_header(PMEMobjpool *pop, uint64_t new_value, struct ulog *ulog_v, 
												uint64_t ulog_offset, struct operation_context *ctx);
int sec_alloc_prepare_new_object(uint64_t pool_id, uint64_t obj_off, uint64_t size, 
												struct operation_context *ctx, uint64_t flags);
int sec_realloc_prepare_new_object(PMEMobjpool *pop, uint64_t new_obj_off, uint64_t new_size, 
				PMEMoid old_obj_id, uint64_t old_size, struct operation_context *ctx, uint64_t flags);
int sec_free_remove_object(uint64_t pool_id, uint64_t obj_off, struct operation_context *ctx);

int
sec_palloc_reserve(struct palloc_heap *heap, size_t size,
	palloc_constr constructor, void *arg,
	uint64_t extra_field, uint16_t object_flags,
	uint16_t class_id, uint16_t arena_id,
	struct pobj_action *act);
void
palloc_set_gen_num(struct palloc_heap *heap, struct pobj_action *act,
	uint64_t *ulog_ptr, uint64_t gen_num_value);

size_t sec_pmemobj_alloc_usable_size(PMEMoid oid);
size_t sec_palloc_usable_size(struct palloc_heap *heap, uint64_t off);
int sec_palloc_init(void *heap_start, uint64_t heap_size, uint64_t *sizep,
	struct pmem_ops *p_ops, uint64_t heap_off);
int sec_palloc_heap_check(void *heap_start, uint64_t heap_size, uint64_t heap_off);
#endif

#ifdef __cplusplus
}
#endif

#endif
