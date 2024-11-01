/*
 * Copyright 2015-2017, Intel Corporation
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
 * map_rbtree_opt.c -- common interface for maps
 */

#include <map.h>
#include <rbtree_anchor_map_opt.h>

#include "map_rbtree_opt.h"

/*
 * map_rbtree_check -- wrapper for rbtree_map_opt_check
 */
static int
map_rbtree_check(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_check(pop, rbtree_map);
}

/*
 * map_rbtree_create -- wrapper for rbtree_map_opt_new
 */
static int
map_rbtree_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	PMEMoid *rbtree_map =
		(PMEMoid *)map;

	return rbtree_map_opt_create(pop, rbtree_map, arg);
}

/*
 * map_rbtree_destroy -- wrapper for rbtree_map_opt_delete
 */
static int
map_rbtree_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	PMEMoid *rbtree_map =
		(PMEMoid *)map;

	return rbtree_map_opt_destroy(pop, rbtree_map);
}

/*
 * map_rbtree_insert -- wrapper for rbtree_map_opt_insert
 */
static int
map_rbtree_insert(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, PMEMoid value)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_insert(pop, rbtree_map, key, value);
}

/*
 * map_rbtree_insert_new -- wrapper for rbtree_map_opt_insert_new
 */
static int
map_rbtree_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size,
		unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_insert_new(pop, rbtree_map, key, size,
			type_num, constructor, arg);
}

/*
 * map_rbtree_remove -- wrapper for rbtree_map_opt_remove
 */
static PMEMoid
map_rbtree_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_remove(pop, rbtree_map, key);
}

/*
 * map_rbtree_remove_free -- wrapper for rbtree_map_opt_remove_free
 */
static int
map_rbtree_remove_free(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_remove_free(pop, rbtree_map, key);
}

/*
 * map_rbtree_clear -- wrapper for rbtree_map_opt_clear
 */
static int
map_rbtree_clear(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_clear(pop, rbtree_map);
}

/*
 * map_rbtree_get -- wrapper for rbtree_map_opt_get
 */
static PMEMoid
map_rbtree_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_get(pop, rbtree_map, key);
}

/*
 * map_rbtree_lookup -- wrapper for rbtree_map_opt_lookup
 */
static int
map_rbtree_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_lookup(pop, rbtree_map, key);
}

/*
 * map_rbtree_foreach -- wrapper for rbtree_map_opt_foreach
 */
static int
map_rbtree_foreach(PMEMobjpool *pop, PMEMoid map,
		int (*cb)(uint64_t key, PMEMoid value, void *arg),
		void *arg)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_foreach(pop, rbtree_map, cb, arg);
}

/*
 * map_rbtree_is_empty -- wrapper for rbtree_map_opt_is_empty
 */
static int
map_rbtree_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid rbtree_map;
	rbtree_map = map;

	return rbtree_map_opt_is_empty(pop, rbtree_map);
}

struct map_ops rbtree_map_opt_ops = {
	/* .check	= */ map_rbtree_check,
	/* .create	= */ map_rbtree_create,
	/* .destroy	= */ map_rbtree_destroy,
	/* .init	= */ NULL,
	/* .insert	= */ map_rbtree_insert,
	/* .update	= */ NULL,
	/* .insert_new	= */ map_rbtree_insert_new,
	/* .remove	= */ map_rbtree_remove,
	/* .remove_free	= */ map_rbtree_remove_free,
	/* .clear	= */ map_rbtree_clear,
	/* .get		= */ map_rbtree_get,
	/* .lookup	= */ map_rbtree_lookup,
	/* .foreach	= */ map_rbtree_foreach,
	/* .is_empty	= */ map_rbtree_is_empty,
	/* .count	= */ NULL,
	/* .cmd		= */ NULL,
};
