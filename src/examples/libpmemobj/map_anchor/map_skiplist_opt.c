/*
 * Copyright 2016, Intel Corporation
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
 * map_skiplist_opt.c -- common interface for maps
 */

#include <map.h>
#include <skiplist_anchor_map_opt.h>

#include "map_skiplist_opt.h"

/*
 * map_skiplist_check -- wrapper for skiplist_map_opt_check
 */
static int
map_skiplist_check(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_check(pop, skiplist_map);
}

/*
 * map_skiplist_create -- wrapper for skiplist_map_opt_new
 */
static int
map_skiplist_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	PMEMoid *skiplist_map =	(PMEMoid *)map;

	return skiplist_map_opt_create(pop, skiplist_map, arg);
}

/*
 * map_skiplist_destroy -- wrapper for skiplist_map_opt_delete
 */
static int
map_skiplist_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	PMEMoid *skiplist_map =	(PMEMoid *)map;

	return skiplist_map_opt_destroy(pop, skiplist_map);
}

/*
 * map_skiplist_insert -- wrapper for skiplist_map_opt_insert
 */
static int
map_skiplist_insert(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, PMEMoid value)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_insert(pop, skiplist_map, key, value);
}

/*
 * map_skiplist_insert_new -- wrapper for skiplist_map_opt_insert_new
 */
static int
map_skiplist_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size,
		unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_insert_new(pop, skiplist_map, key, size,
			type_num, constructor, arg);
}

/*
 * map_skiplist_remove -- wrapper for skiplist_map_opt_remove
 */
static PMEMoid
map_skiplist_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_remove(pop, skiplist_map, key);
}

/*
 * map_skiplist_remove_free -- wrapper for skiplist_map_opt_remove_free
 */
static int
map_skiplist_remove_free(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_remove_free(pop, skiplist_map, key);
}

/*
 * map_skiplist_clear -- wrapper for skiplist_map_opt_clear
 */
static int
map_skiplist_clear(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_clear(pop, skiplist_map);
}

/*
 * map_skiplist_get -- wrapper for skiplist_map_opt_get
 */
static PMEMoid
map_skiplist_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_get(pop, skiplist_map, key);
}

/*
 * map_skiplist_lookup -- wrapper for skiplist_map_opt_lookup
 */
static int
map_skiplist_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_lookup(pop, skiplist_map, key);
}

/*
 * map_skiplist_foreach -- wrapper for skiplist_map_opt_foreach
 */
static int
map_skiplist_foreach(PMEMobjpool *pop, PMEMoid map,
		int (*cb)(uint64_t key, PMEMoid value, void *arg),
		void *arg)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_foreach(pop, skiplist_map, cb, arg);
}

/*
 * map_skiplist_is_empty -- wrapper for skiplist_map_opt_is_empty
 */
static int
map_skiplist_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid skiplist_map;
	skiplist_map = map;
	//TOID_ASSIGN(skiplist_map, map.oid);

	return skiplist_map_opt_is_empty(pop, skiplist_map);
}

struct map_ops skiplist_map_opt_ops = {
	/* .check	= */ map_skiplist_check,
	/* .create	= */ map_skiplist_create,
	/* .destroy	= */ map_skiplist_destroy,
	/* .init	= */ NULL,
	/* .insert	= */ map_skiplist_insert,
	/* .update	= */ NULL,
	/* .insert_new	= */ map_skiplist_insert_new,
	/* .remove	= */ map_skiplist_remove,
	/* .remove_free	= */ map_skiplist_remove_free,
	/* .clear	= */ map_skiplist_clear,
	/* .get		= */ map_skiplist_get,
	/* .lookup	= */ map_skiplist_lookup,
	/* .foreach	= */ map_skiplist_foreach,
	/* .is_empty	= */ map_skiplist_is_empty,
	/* .count	= */ NULL,
	/* .cmd		= */ NULL,
};
