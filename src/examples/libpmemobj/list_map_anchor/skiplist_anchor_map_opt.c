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
 * skiplist_map_opt.c -- Skiplist implementation
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include "skiplist_anchor_map_opt.h"
#include <libanchor.h>

#define SKIPLIST_LEVELS_NUM 4
#define NULL_NODE TOID_NULL(struct skiplist_map_node)

struct skiplist_map_entry {
	uint64_t key;
	PMEMoid value;
};

struct skiplist_map_node {
	PMEMoid next[SKIPLIST_LEVELS_NUM];
	struct skiplist_map_entry entry;
};

/*
 * skiplist_map_opt_create -- allocates a new skiplist instance
 */
int
skiplist_map_opt_create(PMEMobjpool *pop, PMEMoid *map,
	void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		*map = sobj_tx_zalloc(sizeof(struct skiplist_map_node), SKIPLIST_MAP_TYPE_OFFSET);
	} TX_ONABORT {
		ret = 1;
	} TX_END
	return ret;
}

/*
 * skiplist_map_opt_clear -- removes all elements from the map
 */
int
skiplist_map_opt_clear(PMEMobjpool *pop, PMEMoid map)
{
	struct skiplist_map_node *skiplist_map = sobj_direct(pop, map);
	while(!OID_IS_NULL(skiplist_map->next[0])) {
		PMEMoid next = skiplist_map->next[0];
		struct skiplist_map_node *next_temp = sobj_direct(pop, next);
		skiplist_map_opt_remove_free(pop, map, next_temp->entry.key);
	}
	return 0;
}

/*
 * skiplist_map_opt_destroy -- cleanups and frees skiplist instance
 */
int
skiplist_map_opt_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	int ret = 0;

	TX_BEGIN(pop) {
		skiplist_map_opt_clear(pop, *map);
		sobj_tx_free(*map);
		*map = OID_NULL;
	} TX_ONABORT {
		ret = 1;
	} TX_END
	return ret;
}

/*
 * skiplist_map_opt_insert_new -- allocates a new object and inserts it into
 * the list
 */
int
skiplist_map_opt_insert_new(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, size_t size, unsigned type_num,
	void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
	void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid n = sobj_tx_alloc(size, type_num);
		constructor(pop, sec_pmemobj_direct(n), arg);
		skiplist_map_opt_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * skiplist_map_opt_insert_node -- (internal) adds new node in selected place
 */
static void
skiplist_map_opt_insert_node(PMEMobjpool* pop, PMEMoid new_node,
	PMEMoid path[SKIPLIST_LEVELS_NUM])
{
	unsigned current_level = 0;
	do {
		sobj_tx_add_range(path[current_level], current_level * sizeof(PMEMoid), sizeof(PMEMoid));
		struct skiplist_map_node *path_curr_p = sobj_direct(pop, path[current_level]);
		struct skiplist_map_node *new_node_p = sobj_direct(pop, new_node);
		new_node_p->next[current_level] = path_curr_p->next[current_level];
		path_curr_p->next[current_level] = new_node;
	} while (++current_level < SKIPLIST_LEVELS_NUM && rand() % 2 == 0);
}

/*
 * skiplist_map_opt_find -- (internal) returns path to searched node, or if
 * node doesn't exist, it will return path to place where key should be.
 */
static void
skiplist_map_opt_find(PMEMobjpool *pop, uint64_t key, PMEMoid map,
	PMEMoid *path)
{
	int current_level;
	PMEMoid active = map;
	for (current_level = SKIPLIST_LEVELS_NUM - 1;
			current_level >= 0; current_level--) {
		struct skiplist_map_node* active_p = sobj_direct(pop, active);		
		for (PMEMoid next = active_p->next[current_level];
			!OID_IS_NULL(next);
			next = active_p->next[current_level])
		{		
			struct skiplist_map_node* next_p = sobj_direct(pop, next);
			if (!(next_p->entry.key < key)) {
				break;
			}
			active = next;
			active_p = sobj_direct(pop, active);
		}
		path[current_level] = active;
	}
}

/*
 * skiplist_map_opt_insert -- inserts a new key-value pair into the map
 */
int
skiplist_map_opt_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value)
{
	int ret = 0;
	PMEMoid new_node;
	PMEMoid path[SKIPLIST_LEVELS_NUM];

	TX_BEGIN(pop) {
		new_node = sobj_tx_zalloc(sizeof(struct skiplist_map_node), SKIPLIST_MAP_TYPE_OFFSET);
		struct skiplist_map_node *new_node_p = sobj_direct(pop, new_node);
		new_node_p->entry.key = key;
		new_node_p->entry.value = value;
		skiplist_map_opt_find(pop, key, map, path);
		skiplist_map_opt_insert_node(pop, new_node, path);
	} TX_ONABORT {
		ret = 1;
	} TX_END
	return ret;
}

/*
 * skiplist_map_opt_remove_free -- removes and frees an object from the list
 */
int
skiplist_map_opt_remove_free(PMEMobjpool *pop, PMEMoid map,
	uint64_t key)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid val = skiplist_map_opt_remove(pop, map, key);
		sobj_tx_free(val);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * skiplist_map_opt_remove_node -- (internal) removes selected node
 */
static void
skiplist_map_opt_remove_node(PMEMobjpool *pop, PMEMoid path[SKIPLIST_LEVELS_NUM])
{
	struct skiplist_map_node *path_p = sobj_direct(pop, path[0]);
	PMEMoid to_remove = path_p->next[0];
	int i;
	for (i = 0; i < SKIPLIST_LEVELS_NUM; i++) {
		if (i!=0) //first is already read
			path_p = sobj_direct(pop, path[i]);
		
		if (OID_EQUALS(path_p->next[i], to_remove)) {
			sobj_tx_add_range(path[i], i * sizeof(PMEMoid), sizeof(PMEMoid));
			struct skiplist_map_node *to_remove_p = sobj_direct(pop, to_remove); 
			path_p->next[i] = to_remove_p->next[i];
		}
	}
}

/*
 * skiplist_map_opt_remove -- removes key-value pair from the map
 */
PMEMoid
skiplist_map_opt_remove(PMEMobjpool *pop, PMEMoid map,
	uint64_t key)
{
	PMEMoid ret = OID_NULL;
	PMEMoid path[SKIPLIST_LEVELS_NUM];
	PMEMoid to_remove;
	TX_BEGIN(pop) {
		skiplist_map_opt_find(pop, key, map, path);
		struct skiplist_map_node *path_p = sobj_direct(pop, path[0]);
		to_remove = path_p->next[0];
		if (!OID_IS_NULL(to_remove)) {
			struct skiplist_map_node *to_remove_p = sobj_direct(pop, to_remove);
			if (to_remove_p->entry.key == key) {
				ret = to_remove_p->entry.value;
				skiplist_map_opt_remove_node(pop, path);
			}
		}
	} TX_ONABORT {
		ret = OID_NULL;
	} TX_END

	return ret;
}

/*
 * skiplist_map_opt_get -- searches for a value of the key
 */
PMEMoid
skiplist_map_opt_get(PMEMobjpool *pop, PMEMoid map,
	uint64_t key)
{
	PMEMoid ret = OID_NULL;
	PMEMoid path[SKIPLIST_LEVELS_NUM], found;
	skiplist_map_opt_find(pop, key, map, path);
	struct skiplist_map_node *path_p = sobj_direct(pop, path[0]);
	found = path_p->next[0];
	if (!OID_IS_NULL(found)) {
		struct skiplist_map_node *found_p = sobj_direct(pop, found);
		if (found_p->entry.key == key) {
			ret = found_p->entry.value;
		}
	}
	return ret;
}

/*
 * skiplist_map_opt_lookup -- searches if a key exists
 */
int
skiplist_map_opt_lookup(PMEMobjpool *pop, PMEMoid map,
	uint64_t key)
{
	int ret = 0;
	PMEMoid path[SKIPLIST_LEVELS_NUM], found;

	skiplist_map_opt_find(pop, key, map, path);
	struct skiplist_map_node *path_p = sobj_direct(pop, path[0]);
	found = path_p->next[0];
	if (!OID_IS_NULL(found)) {
		struct skiplist_map_node *found_p = sobj_direct(pop, found);
		if (found_p->entry.key == key) {
			ret = 1;
		}
	}
	return ret;
}

/*
 * skiplist_map_opt_foreach -- calls function for each node on a list
 */
int
skiplist_map_opt_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	PMEMoid next = map;
	struct skiplist_map_node *next_p = sobj_direct(pop, next);
	while(!OID_IS_NULL(next_p->next[0])) {
		next = next_p->next[0];
		next_p = sobj_direct(pop, next);
		cb(next_p->entry.key, next_p->entry.value, arg);
	}
	return 0;
}

/*
 * skiplist_map_opt_is_empty -- checks whether the list map is empty
 */
int
skiplist_map_opt_is_empty(PMEMobjpool *pop, PMEMoid map)
{	
	struct skiplist_map_node *skiplist_map = sobj_direct(pop, map);
	return OID_IS_NULL(skiplist_map->next[0]);
}

/*
 * skiplist_map_opt_check -- check if given persistent object is a skiplist
 */
int
skiplist_map_opt_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
}
