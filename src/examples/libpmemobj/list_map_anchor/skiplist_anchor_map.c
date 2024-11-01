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
 * skiplist_map.c -- Skiplist implementation
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include "skiplist_anchor_map.h"
#include <libanchor.h>

#define SKIPLIST_LEVELS_NUM 4
#define NULL_NODE TOID_NULL(struct skiplist_map_node)

struct skiplist_map_entry {
	uint64_t key;
	PMEMoid value;
};

struct skiplist_map_node {
	//TOID(struct skiplist_map_node) next[SKIPLIST_LEVELS_NUM];
	PMEMoid next[SKIPLIST_LEVELS_NUM];
	struct skiplist_map_entry entry;
};

/*
 * skiplist_map_create -- allocates a new skiplist instance
 */
int
skiplist_map_create(PMEMobjpool *pop, PMEMoid *map,
	void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		//pmemobj_tx_add_range_direct(map, sizeof(*map));
		//*map = TX_ZNEW(struct skiplist_map_node);
		*map = sobj_tx_zalloc(sizeof(struct skiplist_map_node), SKIPLIST_MAP_TYPE_OFFSET);

	} TX_ONABORT {
		ret = 1;
	} TX_END
	return ret;
}

/*
 * skiplist_map_clear -- removes all elements from the map
 */
int
skiplist_map_clear(PMEMobjpool *pop, PMEMoid map)
{
	struct skiplist_map_node *skiplist_map = sobj_read(pop, map, 1, NULL);
	while(!OID_IS_NULL(skiplist_map->next[0])) {
		PMEMoid next = skiplist_map->next[0];
		struct skiplist_map_node *next_temp = sobj_read(pop, next, 1, NULL);
		skiplist_map_remove_free(pop, map, next_temp->entry.key);
		free(next_temp);
	}
	free(skiplist_map);
	return 0;
	/*
	while (!TOID_EQUALS(D_RO(map)->next[0], NULL_NODE)) {
		TOID(struct skiplist_map_node) next = D_RO(map)->next[0];
		skiplist_map_remove_free(pop, map, D_RO(next)->entry.key);
	}
	return 0;
	*/
}

/*
 * skiplist_map_destroy -- cleanups and frees skiplist instance
 */
int
skiplist_map_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	int ret = 0;

	TX_BEGIN(pop) {
		skiplist_map_clear(pop, *map);
		//pmemobj_tx_add_range_direct(map, sizeof(*map)); //minor fix!
		sobj_tx_free(*map);
		//TX_FREE(*map);
		*map = OID_NULL; //TOID_NULL(struct skiplist_map_node);
	} TX_ONABORT {
		ret = 1;
	} TX_END
	return ret;
}

/*
 * skiplist_map_insert_new -- allocates a new object and inserts it into
 * the list
 */
int
skiplist_map_insert_new(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, size_t size, unsigned type_num,
	void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
	void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid n = sobj_tx_alloc(size, type_num);
		//PMEMoid n = pmemobj_tx_alloc(size, type_num);
		//constructor(pop, pmemobj_direct(n), arg);
		constructor(pop, sec_pmemobj_direct(n), arg);
		skiplist_map_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * skiplist_map_insert_node -- (internal) adds new node in selected place
 */
static void
skiplist_map_insert_node(PMEMobjpool* pop, PMEMoid new_node,
	PMEMoid path[SKIPLIST_LEVELS_NUM])
{
	unsigned current_level = 0;
	do {
		sobj_tx_add_range(path[current_level], current_level * sizeof(PMEMoid), sizeof(PMEMoid));
		//TX_ADD_FIELD(path[current_level], next[current_level]);
		struct skiplist_map_node *path_curr_p = sobj_read(pop, path[current_level], 1, NULL);
		struct skiplist_map_node *new_node_p = sobj_read(pop, new_node, 1, NULL);
		new_node_p->next[current_level] = path_curr_p->next[current_level];
		path_curr_p->next[current_level] = new_node;
		sobj_tx_write(pop, new_node, new_node_p);
		sobj_tx_write(pop, path[current_level], path_curr_p);
		//D_RW(new_node)->next[current_level] =
		//	D_RO(path[current_level])->next[current_level];
		//D_RW(path[current_level])->next[current_level] = new_node;
		free(new_node_p);
		free(path_curr_p);
	} while (++current_level < SKIPLIST_LEVELS_NUM && rand() % 2 == 0);
}

/*
 * skiplist_map_find -- (internal) returns path to searched node, or if
 * node doesn't exist, it will return path to place where key should be.
 */
static void
skiplist_map_find(PMEMobjpool *pop, uint64_t key, PMEMoid map,
	PMEMoid *path)
{
	int current_level;
	PMEMoid active = map;
	for (current_level = SKIPLIST_LEVELS_NUM - 1;
			current_level >= 0; current_level--) {
		struct skiplist_map_node* active_p = sobj_read(pop, active, 1, NULL);		
		for (PMEMoid next = active_p->next[current_level];
			!OID_IS_NULL(next);
			next = active_p->next[current_level])
		{		
			struct skiplist_map_node* next_p = sobj_read(pop, next, 1, NULL);
			if (!(next_p->entry.key < key)) {
				free(next_p);
				break;
			}
			active = next;
			free(next_p);
			free(active_p);
			active_p = sobj_read(pop, active, 1, NULL);
		}
		free(active_p);
		path[current_level] = active;

		/*
		for (TOID(struct skiplist_map_node) next = D_RO(active)->next[current_level];
				!TOID_EQUALS(next, NULL_NODE) && D_RO(next)->entry.key < key;
				next = D_RO(active)->next[current_level]) {
			active = next;
		}
		*/
	}
}

/*
 * skiplist_map_insert -- inserts a new key-value pair into the map
 */
int
skiplist_map_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value)
{
	int ret = 0;
	PMEMoid new_node;
	PMEMoid path[SKIPLIST_LEVELS_NUM];

	TX_BEGIN(pop) {
		//new_node = TX_ZNEW(struct skiplist_map_node);
		new_node = sobj_tx_zalloc(sizeof(struct skiplist_map_node), SKIPLIST_MAP_TYPE_OFFSET);
		struct skiplist_map_node *new_node_p = sobj_read(pop, new_node, 1, NULL);
		new_node_p->entry.key = key;
		new_node_p->entry.value = value;
		//D_RW(new_node)->entry.key = key;
		//D_RW(new_node)->entry.value = value;
		sobj_tx_write(pop, new_node, new_node_p);
		free(new_node_p);
		skiplist_map_find(pop, key, map, path);
		skiplist_map_insert_node(pop, new_node, path);
	} TX_ONABORT {
		ret = 1;
	} TX_END
	return ret;
}

/*
 * skiplist_map_remove_free -- removes and frees an object from the list
 */
int
skiplist_map_remove_free(PMEMobjpool *pop, PMEMoid map,
	uint64_t key)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid val = skiplist_map_remove(pop, map, key);
		sobj_tx_free(val);
		//pmemobj_tx_free(val);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * skiplist_map_remove_node -- (internal) removes selected node
 */
static void
skiplist_map_remove_node(PMEMobjpool *pop, PMEMoid path[SKIPLIST_LEVELS_NUM])
{
	struct skiplist_map_node *path_p = sobj_read(pop, path[0], 1, NULL);
	PMEMoid to_remove = path_p->next[0];
	//TOID(struct skiplist_map_node) to_remove = D_RO(path[0])->next[0];
	int i;
	for (i = 0; i < SKIPLIST_LEVELS_NUM; i++) {
		if (i!=0) //first is already read
			path_p = sobj_read(pop, path[i], 1, NULL);
		
		if (OID_EQUALS(path_p->next[i], to_remove)) {
			sobj_tx_add_range(path[i], i * sizeof(PMEMoid), sizeof(PMEMoid));
			struct skiplist_map_node *to_remove_p = sobj_read(pop, to_remove, 1, NULL); 
			path_p->next[i] = to_remove_p->next[i];
			sobj_tx_write(pop, path[i], path_p);
			free(path_p);
			free(to_remove_p);	
		}
		else {
			free(path_p);
		}
		/*
		if (TOID_EQUALS(D_RO(path[i])->next[i], to_remove)) { //first always succeeds?
			TX_ADD_FIELD(path[i], next[i]);
			D_RW(path[i])->next[i] = D_RO(to_remove)->next[i];
		}
		*/
	}
}

/*
 * skiplist_map_remove -- removes key-value pair from the map
 */
PMEMoid
skiplist_map_remove(PMEMobjpool *pop, PMEMoid map,
	uint64_t key)
{
	PMEMoid ret = OID_NULL;
	PMEMoid path[SKIPLIST_LEVELS_NUM];
	PMEMoid to_remove;
	TX_BEGIN(pop) {
		skiplist_map_find(pop, key, map, path);
		struct skiplist_map_node *path_p = sobj_read(pop, path[0], 1, NULL);
		to_remove = path_p->next[0];
		if (!OID_IS_NULL(to_remove)) {
			struct skiplist_map_node *to_remove_p = sobj_read(pop, to_remove, 1, NULL);
			if (to_remove_p->entry.key == key) {
				ret = to_remove_p->entry.value;
				skiplist_map_remove_node(pop, path);
			}
			free(to_remove_p);
		}
		free(path_p);
	} TX_ONABORT {
		ret = OID_NULL;
	} TX_END

	return ret;
}

/*
 * skiplist_map_get -- searches for a value of the key
 */
PMEMoid
skiplist_map_get(PMEMobjpool *pop, PMEMoid map,
	uint64_t key)
{
	PMEMoid ret = OID_NULL;
	PMEMoid path[SKIPLIST_LEVELS_NUM], found;
	skiplist_map_find(pop, key, map, path);
	struct skiplist_map_node *path_p = sobj_read(pop, path[0], 1, NULL);
	found = path_p->next[0];
	if (!OID_IS_NULL(found)) {
		struct skiplist_map_node *found_p = sobj_read(pop, found, 1, NULL);
		if (found_p->entry.key == key) {
			ret = found_p->entry.value;
		}
		free(found_p);
	}
	free(path_p);

	/*
	found = D_RO(path[0])->next[0];
	if (!TOID_EQUALS(found, NULL_NODE) &&
		D_RO(found)->entry.key == key) {
		ret = D_RO(found)->entry.value;
	}
	*/

	return ret;
}

/*
 * skiplist_map_lookup -- searches if a key exists
 */
int
skiplist_map_lookup(PMEMobjpool *pop, PMEMoid map,
	uint64_t key)
{
	int ret = 0;
	PMEMoid path[SKIPLIST_LEVELS_NUM], found;

	skiplist_map_find(pop, key, map, path);
	struct skiplist_map_node *path_p = sobj_read(pop, path[0], 1, NULL);
	found = path_p->next[0];
	if (!OID_IS_NULL(found)) {
		struct skiplist_map_node *found_p = sobj_read(pop, found, 1, NULL);
		if (found_p->entry.key == key) {
			ret = 1;
		}
		free(found_p);
	}
	free(path_p);
	return ret;
}

/*
 * skiplist_map_foreach -- calls function for each node on a list
 */
int
skiplist_map_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	PMEMoid next = map;
	struct skiplist_map_node *next_p = sobj_read(pop, next, 1, NULL);
	while(!OID_IS_NULL(next_p->next[0])) {
		next = next_p->next[0];
		free(next_p);
		next_p = sobj_read(pop, next, 1, NULL);
		cb(next_p->entry.key, next_p->entry.value, arg);
	}
	free(next_p);
	return 0;

	/*
	while (!TOID_EQUALS(D_RO(next)->next[0], NULL_NODE)) {
		next = D_RO(next)->next[0];
		cb(D_RO(next)->entry.key, D_RO(next)->entry.value, arg);
	}
	*/
}

/*
 * skiplist_map_is_empty -- checks whether the list map is empty
 */
int
skiplist_map_is_empty(PMEMobjpool *pop, PMEMoid map)
{	
	struct skiplist_map_node *skiplist_map = sobj_read(pop, map, 1, NULL);
	int ret = 0;
	if (OID_IS_NULL(skiplist_map->next[0]))
		ret = 1;
	free(skiplist_map);
	return ret;
	//return TOID_IS_NULL(D_RO(map)->next[0]);
}

/*
 * skiplist_map_check -- check if given persistent object is a skiplist
 */
int
skiplist_map_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
	//return TOID_IS_NULL(map) || !TOID_VALID(map);
}
