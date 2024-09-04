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
 * map_hashmap_tx.c -- common interface for maps
 */

#include <map.h>
#include <hashmap_anchor_tx.h>

#include "map_hashmap_tx.h"

/*
 * map_hm_tx_check -- wrapper for hm_tx_check
 */
static int
map_hm_tx_check(PMEMobjpool *pop, PMEMoid map)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_check(pop, hashmap_tx);
}

/*
 * map_hm_tx_count -- wrapper for hm_tx_count
 */
static size_t
map_hm_tx_count(PMEMobjpool *pop, PMEMoid map)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_count(pop, hashmap_tx);
}

/*
 * map_hm_tx_init -- wrapper for hm_tx_init
 */
static int
map_hm_tx_init(PMEMobjpool *pop, PMEMoid map)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_init(pop, hashmap_tx);
}

/*
 * map_hm_tx_create -- wrapper for hm_tx_create
 */
static int
map_hm_tx_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	//TOID(struct hashmap_tx) *hashmap_tx =
	//	(TOID(struct hashmap_tx) *)map;

	PMEMoid *hashmap_tx = map;

	return hm_tx_create(pop, hashmap_tx, arg);
}

/*
 * map_hm_tx_insert -- wrapper for hm_tx_insert
 */
static int
map_hm_tx_insert(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, PMEMoid value)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_insert(pop, hashmap_tx, key, value);
}

/*
 * map_hm_tx_update -- wrapper for hm_tx_update
 */
static int
map_hm_tx_update(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, char value_data[])
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_update(pop, hashmap_tx, key, value_data);
}

/*
 * map_hm_tx_remove -- wrapper for hm_tx_remove
 */
static PMEMoid
map_hm_tx_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_remove(pop, hashmap_tx, key);
}

/*
 * map_hm_tx_get -- wrapper for hm_tx_get
 */
static PMEMoid
map_hm_tx_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_get(pop, hashmap_tx, key);
}

/*
 * map_hm_tx_lookup -- wrapper for hm_tx_lookup
 */
static int
map_hm_tx_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_lookup(pop, hashmap_tx, key);
}

/*
 * map_hm_tx_foreach -- wrapper for hm_tx_foreach
 */
static int
map_hm_tx_foreach(PMEMobjpool *pop, PMEMoid map,
		int (*cb)(uint64_t key, PMEMoid value, void *arg),
		void *arg)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_foreach(pop, hashmap_tx, cb, arg);
}

/*
 * map_hm_tx_cmd -- wrapper for hm_tx_cmd
 */
static int
map_hm_tx_cmd(PMEMobjpool *pop, PMEMoid map,
		unsigned cmd, uint64_t arg)
{
	//TOID(struct hashmap_tx) hashmap_tx;
	//TOID_ASSIGN(hashmap_tx, map.oid);

	PMEMoid hashmap_tx;
	hashmap_tx = map;

	return hm_tx_cmd(pop, hashmap_tx, cmd, arg);
}

struct map_ops hashmap_tx_ops = {
	/* .check	= */ map_hm_tx_check,
	/* .create	= */ map_hm_tx_create,
	/* .destroy	= */ NULL,
	/* .init	= */ map_hm_tx_init,
	/* .insert	= */ map_hm_tx_insert,
	/* .update	= */ map_hm_tx_update,
	/* .insert_new	= */ NULL,
	/* .remove	= */ map_hm_tx_remove,
	/* .remove_free	= */ NULL,
	/* .clear	= */ NULL,
	/* .get		= */ map_hm_tx_get,
	/* .lookup	= */ map_hm_tx_lookup,
	/* .foreach	= */ map_hm_tx_foreach,
	/* .is_empty	= */ NULL,
	/* .count	= */ map_hm_tx_count,
	/* .cmd		= */ map_hm_tx_cmd,
};
