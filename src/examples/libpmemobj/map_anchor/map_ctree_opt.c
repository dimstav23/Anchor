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
 * map_ctree_opt.c -- common interface for maps
 */

#include <map.h>
#include <ctree_anchor_map_opt.h>

#include "map_ctree_opt.h"

/*
 * map_ctree_check -- wrapper for ctree_map_opt_check
 */
static int
map_ctree_check(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_check(pop, ctree_map);
}

/*
 * map_ctree_create -- wrapper for ctree_map_opt_create
 */
static int
map_ctree_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	PMEMoid *ctree_map = (PMEMoid *)map;

	return ctree_map_opt_create(pop, ctree_map, arg);
}

/*
 * map_ctree_destroy -- wrapper for ctree_map_opt_destroy
 */
static int
map_ctree_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	PMEMoid *ctree_map = (PMEMoid *)map;

	return ctree_map_opt_destroy(pop, ctree_map);
}

/*
 * map_ctree_insert -- wrapper for ctree_map_opt_insert
 */
static int
map_ctree_insert(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, PMEMoid value)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_insert(pop, ctree_map, key, value);
}

/*
 * map_ctree_insert_new -- wrapper for ctree_map_opt_insert_new
 */
static int
map_ctree_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size,
		unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_insert_new(pop, ctree_map, key, size,
			type_num, constructor, arg);
}

/*
 * map_ctree_remove -- wrapper for ctree_map_opt_remove
 */
static PMEMoid
map_ctree_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_remove(pop, ctree_map, key);
}

/*
 * map_ctree_remove_free -- wrapper for ctree_map_opt_remove_free
 */
static int
map_ctree_remove_free(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_remove_free(pop, ctree_map, key);
}

/*
 * map_ctree_clear -- wrapper for ctree_map_opt_clear
 */
static int
map_ctree_clear(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_clear(pop, ctree_map);
}

/*
 * map_ctree_get -- wrapper for ctree_map_opt_get
 */
static PMEMoid
map_ctree_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_get(pop, ctree_map, key);
}

/*
 * map_ctree_lookup -- wrapper for ctree_map_opt_lookup
 */
static int
map_ctree_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_lookup(pop, ctree_map, key);
}

/*
 * map_ctree_foreach -- wrapper for ctree_map_opt_foreach
 */
static int
map_ctree_foreach(PMEMobjpool *pop, PMEMoid map,
		int (*cb)(uint64_t key, PMEMoid value, void *arg),
		void *arg)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_foreach(pop, ctree_map, cb, arg);
}

/*
 * map_ctree_is_empty -- wrapper for ctree_map_opt_is_empty
 */
static int
map_ctree_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid ctree_map;
	ctree_map = map;
	//TOID_ASSIGN(ctree_map, map.oid);

	return ctree_map_opt_is_empty(pop, ctree_map);
}

struct map_ops ctree_map_opt_ops = {
	/* .check	= */ map_ctree_check,
	/* .create	= */ map_ctree_create,
	/* .destroy	= */ map_ctree_destroy,
	/* .init	= */ NULL,
	/* .insert	= */ map_ctree_insert,
	/* .update	= */ NULL,
	/* .insert_new	= */ map_ctree_insert_new,
	/* .remove	= */ map_ctree_remove,
	/* .remove_free	= */ map_ctree_remove_free,
	/* .clear	= */ map_ctree_clear,
	/* .get		= */ map_ctree_get,
	/* .lookup	= */ map_ctree_lookup,
	/* .foreach	= */ map_ctree_foreach,
	/* .is_empty	= */ map_ctree_is_empty,
	/* .count	= */ NULL,
	/* .cmd		= */ NULL,
};
