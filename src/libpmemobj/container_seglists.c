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
 * container_seglists.c -- implementation of segregated lists block container
 *
 * This container is constructed from N (up to 64) intrusive lists and a
 * single 8 byte bitmap that stores the information whether a given list is
 * empty or not.
 */

#include "container_seglists.h"
#include "out.h"
#include "sys_util.h"
#include "util.h"
#include "valgrind_internal.h"
#include "vecq.h"

#define SEGLIST_BLOCK_LISTS 64U

struct block_container_seglists {
	struct block_container super;
	struct memory_block m;
	VECQ(, uint32_t) blocks[SEGLIST_BLOCK_LISTS];
	uint64_t nonempty_lists;
};

/*
 * container_seglists_insert_block -- (internal) inserts a new memory block
 *	into the container
 */
static int
container_seglists_insert_block(struct block_container *bc,
	const struct memory_block *m)
{
	LOG(11,"chunk id: %d, block off: %d, size idx: %d, type: %d", m->chunk_id, m->block_off, m->size_idx, m->type);
	ASSERT(m->chunk_id < MAX_CHUNK);
	ASSERT(m->zone_id < UINT16_MAX);
	ASSERTne(m->size_idx, 0);

	struct block_container_seglists *c =
		(struct block_container_seglists *)bc;

	if (c->nonempty_lists == 0)
		c->m = *m;

	ASSERT(m->size_idx <= SEGLIST_BLOCK_LISTS);
	ASSERT(m->chunk_id == c->m.chunk_id);
	ASSERT(m->zone_id == c->m.zone_id);

	if (VECQ_ENQUEUE(&c->blocks[m->size_idx - 1], m->block_off) != 0)
		return -1;

	/* marks the list as nonempty */
	c->nonempty_lists |= 1ULL << (m->size_idx - 1);

	return 0;
}

/*
 * container_seglists_get_rm_block_bestfit -- (internal) removes and returns the
 *	best-fit memory block for size
 */
static int
container_seglists_get_rm_block_bestfit(struct block_container *bc,
	struct memory_block *m)
{
	struct block_container_seglists *c =
		(struct block_container_seglists *)bc;

	ASSERT(m->size_idx <= SEGLIST_BLOCK_LISTS);
	uint32_t i = 0;

	/* applicable lists */
	uint64_t size_mask = (1ULL << (m->size_idx - 1)) - 1;
	uint64_t v = c->nonempty_lists & ~size_mask;
	if (v == 0)
		return ENOMEM;

	/* finds the list that serves the smallest applicable size */
	i = util_lssb_index64(v);

	uint32_t block_offset = VECQ_DEQUEUE(&c->blocks[i]);

	if (VECQ_SIZE(&c->blocks[i]) == 0) /* marks the list as empty */
		c->nonempty_lists &= ~(1ULL << (i));

	*m = c->m;
	m->block_off = block_offset;
	m->size_idx = i + 1;

	return 0;
}

/*
 * container_seglists_is_empty -- (internal) checks whether the container is
 * empty
 */
static int
container_seglists_is_empty(struct block_container *bc)
{
	struct block_container_seglists *c =
		(struct block_container_seglists *)bc;

	return c->nonempty_lists == 0;
}

/*
 * container_seglists_rm_all -- (internal) removes all elements from the tree
 */
static void
container_seglists_rm_all(struct block_container *bc)
{
	struct block_container_seglists *c =
		(struct block_container_seglists *)bc;

	for (unsigned i = 0; i < SEGLIST_BLOCK_LISTS; ++i)
		VECQ_CLEAR(&c->blocks[i]);

	c->nonempty_lists = 0;
}

/*
 * container_seglists_delete -- (internal) deletes the container
 */
static void
container_seglists_destroy(struct block_container *bc)
{
	struct block_container_seglists *c =
		(struct block_container_seglists *)bc;

	for (unsigned i = 0; i < SEGLIST_BLOCK_LISTS; ++i)
		VECQ_DELETE(&c->blocks[i]);

	Free(c);
}

/*
 * This container does not support retrieval of exact memory blocks, but other
 * than provides best-fit in O(1) time for unit sizes that do not exceed 64.
 */
static const struct block_container_ops container_seglists_ops = {
	.insert = container_seglists_insert_block,
	.get_rm_exact = NULL,
	.get_rm_bestfit = container_seglists_get_rm_block_bestfit,
	.is_empty = container_seglists_is_empty,
	.rm_all = container_seglists_rm_all,
	.destroy = container_seglists_destroy,
};

/*
 * container_new_seglists -- allocates and initializes a seglists container
 */
struct block_container *
container_new_seglists(struct palloc_heap *heap)
{
	struct block_container_seglists *bc = Malloc(sizeof(*bc));
	if (bc == NULL)
		goto error_container_malloc;

	bc->super.heap = heap;
	bc->super.c_ops = &container_seglists_ops;

	for (unsigned i = 0; i < SEGLIST_BLOCK_LISTS; ++i)
		VECQ_INIT(&bc->blocks[i]);
	bc->nonempty_lists = 0;

	return (struct block_container *)&bc->super;

error_container_malloc:
	return NULL;
}
