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
 * map_rtree_opt.c -- common interface for maps
 */

#include <rtree_anchor_map_opt.h>

#include "map_rtree_opt.h"

/*
 * map_rtree_check -- wrapper for rtree_map_opt_check
 */
static int
map_rtree_check(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_check(pop, rtree_map);
}

/*
 * map_rtree_create -- wrapper for rtree_map_opt_new
 */
static int
map_rtree_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	PMEMoid *rtree_map =
		(PMEMoid *)map;

	return rtree_map_opt_create(pop, rtree_map, arg);
}

/*
 * map_rtree_destroy -- wrapper for rtree_map_opt_delete
 */
static int
map_rtree_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	PMEMoid *rtree_map =
		(PMEMoid *)map;

	return rtree_map_opt_destroy(pop, rtree_map);
}

/*
 * map_rtree_insert -- wrapper for rtree_map_opt_insert
 */
static int
map_rtree_insert(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, PMEMoid value)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_insert(pop, rtree_map,
			(unsigned char *)&key, sizeof(key), value);
}

/*
 * map_rtree_insert_new -- wrapper for rtree_map_opt_insert_new
 */
static int
map_rtree_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size,
		unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_insert_new(pop, rtree_map,
			(unsigned char *)&key, sizeof(key), size,
			type_num, constructor, arg);
}

/*
 * map_rtree_remove -- wrapper for rtree_map_opt_remove
 */
static PMEMoid
map_rtree_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_remove(pop, rtree_map,
			(unsigned char *)&key, sizeof(key));
}

/*
 * map_rtree_remove_free -- wrapper for rtree_map_opt_remove_free
 */
static int
map_rtree_remove_free(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_remove_free(pop, rtree_map,
			(unsigned char *)&key, sizeof(key));
}

/*
 * map_rtree_clear -- wrapper for rtree_map_opt_clear
 */
static int
map_rtree_clear(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_clear(pop, rtree_map);
}

/*
 * map_rtree_get -- wrapper for rtree_map_opt_get
 */
static PMEMoid
map_rtree_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_get(pop, rtree_map,
			(unsigned char *)&key, sizeof(key));
}

/*
 * map_rtree_lookup -- wrapper for rtree_map_opt_lookup
 */
static int
map_rtree_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_lookup(pop, rtree_map,
			(unsigned char *)&key, sizeof(key));
}

struct cb_arg2 {
	int (*cb)(uint64_t key, PMEMoid value, void *arg);
	void *arg;
};

/*
 * map_rtree_foreach_cb -- wrapper for callback
 */
static int
map_rtree_foreach_cb(const unsigned char *key,
		uint64_t key_size, PMEMoid value, void *arg2)
{
	const struct cb_arg2 *const a2 = (const struct cb_arg2 *)arg2;
	const uint64_t *const k2 = (uint64_t *)key;

	return a2->cb(*k2, value, a2->arg);
}

/*
 * map_rtree_foreach -- wrapper for rtree_map_opt_foreach
 */
static int
map_rtree_foreach(PMEMobjpool *pop, PMEMoid map,
		int (*cb)(uint64_t key, PMEMoid value, void *arg),
		void *arg)
{
	struct cb_arg2 arg2 = {cb, arg};

	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_foreach(pop, rtree_map, map_rtree_foreach_cb, &arg2);
}

/*
 * map_rtree_is_empty -- wrapper for rtree_map_opt_is_empty
 */
static int
map_rtree_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	PMEMoid rtree_map;
	rtree_map = map;

	return rtree_map_opt_is_empty(pop, rtree_map);
}

struct map_ops rtree_map_opt_ops = {
/*	.check		= */map_rtree_check,
/*	.create		= */map_rtree_create,
/*	.destroy	= */map_rtree_destroy,
/*	.init		= */NULL,
/*	.insert		= */map_rtree_insert,
/* .update	= */ NULL,
/*	.insert_new	= */map_rtree_insert_new,
/*	.remove		= */map_rtree_remove,
/*	.remove_free	= */map_rtree_remove_free,
/*	.clear		= */map_rtree_clear,
/*	.get		= */map_rtree_get,
/*	.lookup		= */map_rtree_lookup,
/*	.foreach	= */map_rtree_foreach,
/*	.is_empty	= */map_rtree_is_empty,
/*	.count		= */NULL,
/*	.cmd		= */NULL,
};
