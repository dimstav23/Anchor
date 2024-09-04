/*
 * Copyright 2016-2019, Intel Corporation
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
 * rtree_map.c -- implementation of rtree
 */

#include <ex_common.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>

#include "rtree_anchor_map_opt.h"

TOID_DECLARE(struct tree_map_node, RTREE_MAP_TYPE_OFFSET + 1);

/* Good values: 0x10 an 0x100, but implementation is bound to 0x100 */
#ifndef ALPHABET_SIZE
#define ALPHABET_SIZE 0x100
#endif

struct tree_map_node {
	PMEMoid slots[ALPHABET_SIZE];
	unsigned has_value;
	PMEMoid value;
	uint64_t key_size;
	unsigned char key[];
};

struct rtree_map {
	PMEMoid root;
};

/*
 * rtree_map_opt_create -- allocates a new rtree instance
 */
int
rtree_map_opt_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	int ret = 0;
	TX_BEGIN(pop) {
		//pmemobj_tx_add_range_direct(map, sizeof(*map));
		//*map = TX_ZNEW(struct rtree_map);
		//sobj_tx_add_range() //should be here!
		*map = sobj_tx_zalloc(sizeof(struct rtree_map), RTREE_MAP_TYPE_OFFSET);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rtree_map_opt_clear_node -- (internal) removes all elements from the node
 */
static void
rtree_map_opt_clear_node(PMEMobjpool *pop, PMEMoid node)
{
	struct tree_map_node *node_p;

	if (!OID_IS_NULL(node))
		node_p = sobj_direct(pop, node);
	else
		return;

	for (unsigned i = 0; i < ALPHABET_SIZE; i++) {
		rtree_map_opt_clear_node(pop, node_p->slots[i]);
	}

	sobj_tx_add_range(node, 0, 
			sizeof(struct tree_map_node) + node_p->key_size);

	sobj_tx_free(node);
}

/*
 * rtree_map_opt_clear -- removes all elements from the map
 */
int
rtree_map_opt_clear(PMEMobjpool *pop, PMEMoid map)
{
	int ret = 0;
	struct rtree_map *rtree_map = sobj_direct(pop, map);
	TX_BEGIN(pop) {
		rtree_map_opt_clear_node(pop, rtree_map->root);
		sobj_tx_add_range(map, 0, sizeof(PMEMoid));
		PMEMoid new_oid = OID_NULL;
        rtree_map->root = new_oid;
	} TX_ONABORT {
		ret = 1;
	} TX_END
    
	return ret;
}

/*
 * rtree_map_opt_destroy -- cleanups and frees rtree instance
 */
int
rtree_map_opt_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	int ret = 0;
	TX_BEGIN(pop) {
		rtree_map_opt_clear(pop, *map);
		sobj_tx_free(*map);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rtree_new_node -- (internal) inserts a node into an empty map
 */
static PMEMoid
rtree_new_node(PMEMobjpool *pop, const unsigned char *key, uint64_t key_size,
		PMEMoid value, unsigned has_value)
{
	PMEMoid node;

	node = sobj_tx_zalloc(sizeof(struct tree_map_node) + key_size,
							RTREE_MAP_TYPE_OFFSET + 1);
	//node = TX_ZALLOC(struct tree_map_node,
	//		sizeof(struct tree_map_node) + key_size);
	/*
	 * !!! Here should be: D_RO(node)->value
	 * ... because we don't change map
	 */
	struct tree_map_node *new_node = sobj_direct(pop, node);								
	new_node->value = value;
	new_node->has_value = has_value;
	new_node->key_size = key_size;
	memcpy(new_node->key, key, key_size);

	return node;
}
/*
 * rtree_map_opt_insert_empty -- (internal) inserts a node into an empty map
 */
static void
rtree_map_opt_insert_empty(PMEMobjpool *pop, PMEMoid map,
	const unsigned char *key, uint64_t key_size, PMEMoid value)
{
    struct rtree_map *rtree_map = sobj_direct(pop, map);
	sobj_tx_add_range(map, 0, sizeof(PMEMoid));
    rtree_map->root = rtree_new_node(pop, key, key_size, value, 1);
}

/*
 * key_comm_len -- (internal) calculate the len of common part of keys
 */
static unsigned
key_comm_len(PMEMobjpool *pop, PMEMoid node,
	const unsigned char *key, uint64_t key_size)
{
	unsigned i;

	struct tree_map_node *node_p;
	if (!OID_IS_NULL(node))
		node_p = sobj_direct(pop, node);
	else 
		return 0;

	for (i = 0;
		i < MIN(key_size, node_p->key_size) &&
			key[i] == node_p->key[i];
		i++)
		;

	return i;
}

/*
 * rtree_map_opt_insert_value -- (internal) inserts a pair into a tree
 */
static void
rtree_map_opt_insert_value(PMEMobjpool *pop, PMEMoid *node,
	const unsigned char *key, uint64_t key_size, PMEMoid value,
	bool *modified)
{
	unsigned i;

	if (OID_IS_NULL(*node)) {
		//snapshotting is performed in the returned function 
		//along with persistent writing
		(*node) = rtree_new_node(pop, key, key_size, value, 1);
		(*modified) = true;
		return;
	}

	i = key_comm_len(pop, *node, key, key_size);

	struct tree_map_node *node_p = sobj_direct(pop, *node);
	
	if (i != node_p->key_size) {
		/* Node does not exist. Let's add. */
		PMEMoid orig_node = *node;
		struct tree_map_node *orig_node_p = node_p;

		(*modified) = true;

		if (i != key_size) {
			(*node) = rtree_new_node(pop, orig_node_p->key, i,
					OID_NULL, 0);
		} else {
			(*node) = rtree_new_node(pop, orig_node_p->key, i,
					value, 1);
		}

		struct tree_map_node *new_node_p = sobj_direct(pop, *node);
		new_node_p->slots[orig_node_p->key[i]] = orig_node;

		//add both fields directly to the undo log
		sobj_tx_add_range(orig_node, offsetof(struct tree_map_node, key_size),
							sizeof(orig_node_p->key_size) + orig_node_p->key_size);
		orig_node_p->key_size -= i;
		memmove(orig_node_p->key, orig_node_p->key + i,
				orig_node_p->key_size);

		if (i != key_size) {
			new_node_p->slots[key[i]] =
				rtree_new_node(pop, key + i, key_size - i, value, 1);
		}

		return;
	}

	if (i == key_size) {
		if (OID_IS_NULL(node_p->value) || node_p->has_value) {
			/* Just replace old value with new */
			sobj_tx_add_range(*node, offsetof(struct tree_map_node, has_value),
							sizeof(node_p->value) + sizeof(node_p->has_value));
			if (!OID_IS_NULL(node_p->value) && !OID_EQUALS(node_p->value, value)){
				sobj_tx_free(node_p->value);
			}
			node_p->value = value;
			node_p->has_value = 1;
		} else {
			/*
			 *  Ignore. By the fact current value should be
			 *  removed in advance, or handled in a different way.
			 */
		}
	} else {
		/* Recurse deeply */
		bool modified_rec = false;
		rtree_map_opt_insert_value(pop, &node_p->slots[key[i]],
				key + i, key_size - i, value, &modified_rec);
	
		if (modified_rec) {
			sobj_tx_add_range((*node), offsetof(struct tree_map_node, slots) + key[i] * sizeof(PMEMoid), 
							sizeof(PMEMoid));
		}

		return;
	}
}

/*
 * rtree_map_opt_is_empty -- checks whether the tree map is empty
 */
int
rtree_map_opt_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	struct rtree_map *rtree_map = sobj_direct(pop, map);
	return OID_IS_NULL(rtree_map->root);
}

/*
 * rtree_map_opt_insert -- inserts a new key-value pair into the map
 */
int
rtree_map_opt_insert(PMEMobjpool *pop, PMEMoid map,
	const unsigned char *key, uint64_t key_size, PMEMoid value)
{
	int ret = 0;

	TX_BEGIN(pop) {
		if (rtree_map_opt_is_empty(pop, map)) {
			rtree_map_opt_insert_empty(pop, map, key, key_size, value);
		} else {
			struct rtree_map *rtree_map = sobj_direct(pop, map);
			bool modified = false;
			rtree_map_opt_insert_value(pop, &rtree_map->root,
					key, key_size, value, &modified);

			if (modified) {
				sobj_tx_add_range(map, 0, sizeof(struct rtree_map));
			}
		}
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rtree_map_opt_insert_new -- allocates a new object and inserts it into the tree
 */
int
rtree_map_opt_insert_new(PMEMobjpool *pop, PMEMoid map,
		const unsigned char *key, uint64_t key_size,
		size_t size, unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		//PMEMoid n = pmemobj_tx_alloc(size, type_num);
		PMEMoid n = sobj_tx_alloc(size, type_num);
		constructor(pop, sec_pmemobj_direct(n), arg);
		rtree_map_opt_insert(pop, map, key, key_size, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * is_leaf -- (internal) check a node for zero qty of children
 */
static bool
is_leaf(PMEMobjpool *pop, PMEMoid node)
{
	unsigned j;
	struct tree_map_node *node_p;
	if (!OID_IS_NULL(node))
		node_p = sobj_direct(pop, node);
	else 
		return 0;

	for (j = 0;
		j < ALPHABET_SIZE &&
			OID_IS_NULL(node_p->slots[j]);
		j++)
		;

	return (j == ALPHABET_SIZE);
}

/*
 * has_only_one_child -- (internal) check a node for qty of children
 */
static bool
has_only_one_child(PMEMobjpool *pop, PMEMoid node, unsigned *child_idx)
{
	unsigned j, child_qty;
	struct tree_map_node *node_p;
	if (!OID_IS_NULL(node))
		node_p = sobj_direct(pop, node);
	else 
		return 0;

	for (j = 0, child_qty = 0; j < ALPHABET_SIZE; j++) {
		if (!OID_IS_NULL(node_p->slots[j])) {
			child_qty++;
			*child_idx = j;
		}
	}

	return (1 == child_qty);
}

/*
 * remove_extra_node -- (internal) remove unneeded extra node
 */
static void
remove_extra_node(PMEMobjpool *pop, PMEMoid *node)
{
	unsigned child_idx = 0;
	PMEMoid tmp, tmp_child;

	/* Our node has child with only one child. */
	tmp = *node;
	struct tree_map_node *tmp_p = sobj_direct(pop, tmp);

	has_only_one_child(pop, tmp, &child_idx);
	tmp_child = tmp_p->slots[child_idx];
	struct tree_map_node *tmp_child_p = sobj_direct(pop, tmp_child);

	/*
	 * That child's incoming label is appended to the ours incoming label
	 * and the child is removed.
	 */
	uint64_t new_key_size = tmp_p->key_size + tmp_child_p->key_size;
	unsigned char *new_key = (unsigned char *)malloc(new_key_size);
	assert(new_key != NULL);
	memcpy(new_key, tmp_p->key, tmp_p->key_size);
	memcpy(new_key + tmp_p->key_size,
		tmp_child_p->key,
		tmp_child_p->key_size);

	//snapshotting is performed in the returned function 
	//along with persistent writing
	*node = rtree_new_node(pop, new_key, new_key_size,
		tmp_child_p->value, tmp_child_p->has_value);

	free(new_key);
	sobj_tx_free(tmp);

	struct tree_map_node *node_p = sobj_direct(pop, *node);
	memcpy(node_p->slots, tmp_child_p->slots, sizeof(tmp_child_p->slots));
    
	//refresh to check
	tmp_child_p = sobj_direct(pop, *node);

	sobj_tx_free(tmp_child);

}

/*
 * rtree_map_opt_remove_node -- (internal) removes node from tree
 */
static PMEMoid
rtree_map_opt_remove_node(PMEMobjpool *pop, PMEMoid map,
	PMEMoid *node /* volatile copy is passed */,
	const unsigned char *key, uint64_t key_size,
	bool *check_for_child, bool *modified)
{
	bool c4c;
	unsigned i, child_idx;
	PMEMoid ret = OID_NULL;

	*check_for_child = false;

	if (OID_IS_NULL(*node))
		return OID_NULL;

	struct tree_map_node *node_p = sobj_direct(pop, *node);

	i = key_comm_len(pop, *node, key, key_size);

	if (i != node_p->key_size) {
		/* Node does not exist */
		return OID_NULL;
	}

	if (i == key_size) {
		if (0 == node_p->has_value) {
			return OID_NULL;
		}
		/* Node is found */
		ret = node_p->value;

		/* delete node from tree */
		sobj_tx_add_range((*node), offsetof(struct tree_map_node, value), sizeof(PMEMoid));
		sobj_tx_add_range((*node), offsetof(struct tree_map_node, has_value), sizeof(unsigned));
		node_p->value = OID_NULL;
		node_p->has_value = 0;

		if (is_leaf(pop, *node)) {
			(*modified) = true;
			sobj_tx_add_range((*node), 0, sizeof(*node) + node_p->key_size);
			sobj_tx_free(*node);
			(*node) = OID_NULL; //check on return to write it persistently with snapshot
			
		}
		return ret;
	}

	/* Recurse deeply */
	bool modified_rec = false;
	ret = rtree_map_opt_remove_node(pop, map,
			&node_p->slots[key[i]],
			key + i, key_size - i,
			&c4c, &modified_rec);

	if (modified_rec) {
		//snapshot and persist the modified node in rtree_map_opt_remove_node
		sobj_tx_add_range(*node, offsetof(struct tree_map_node, slots) + key[i] * sizeof(PMEMoid), 
							sizeof(PMEMoid));
		//persist the modified pmemoid
	}

	if (c4c) {
		/* Our node has child with only one child. Remove. */
		remove_extra_node(pop, &node_p->slots[key[i]]);		
		
		//snapshot and persist the modified node in remove_extra_node
		sobj_tx_add_range(*node, offsetof(struct tree_map_node, slots) + key[i] * sizeof(PMEMoid), 
							sizeof(PMEMoid));
		//persist the modified pmemoid
		return ret;
	}
	
	if (has_only_one_child(pop, *node, &child_idx) &&
			(0 == node_p->has_value)) {
		*check_for_child = true;
	}

	return ret;
}

/*
 * rtree_map_opt_remove -- removes key-value pair from the map
 */
PMEMoid
rtree_map_opt_remove(PMEMobjpool *pop, PMEMoid map,
		const unsigned char *key, uint64_t key_size)
{
	PMEMoid ret = OID_NULL;
	bool check_for_child;
	bool modified = false;

	if (OID_IS_NULL(map))
		return OID_NULL;

	struct rtree_map *rtree_map = sobj_direct(pop, map);
	
	TX_BEGIN(pop) {
		ret = rtree_map_opt_remove_node(pop, map,
				&rtree_map->root, key, key_size,
				&check_for_child, &modified);
		
		if (check_for_child) {
			// Our root node has only one child. Remove.
			modified = true;
			remove_extra_node(pop, &rtree_map->root);
		}

		if (modified) {
			//snapshot and persist
			sobj_tx_add_range(map, 0, sizeof(struct rtree_map));
			//persist the modified pmemoid
		}

	} TX_END

	return ret;
}

/*
 * rtree_map_opt_remove_free -- removes and frees an object from the tree
 */
int
rtree_map_opt_remove_free(PMEMobjpool *pop, PMEMoid map,
		const unsigned char *key, uint64_t key_size)
{
	int ret = 0;

	if (OID_IS_NULL(map))
		return 1;

	TX_BEGIN(pop) {
		sobj_tx_free(rtree_map_opt_remove(pop, map, key, key_size));
		//pmemobj_tx_free(rtree_map_opt_remove(pop, map, key, key_size));
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rtree_map_opt_get_in_node -- (internal) searches for a value in the node
 */
static PMEMoid
rtree_map_opt_get_in_node(PMEMobjpool *pop, PMEMoid node,
		const unsigned char *key, uint64_t key_size)
{
	unsigned i;

	if (OID_IS_NULL(node))
		return OID_NULL;

	i = key_comm_len(pop, node, key, key_size);

	struct tree_map_node *node_p = sobj_direct(pop, node);

	if (i != node_p->key_size) {
		/* Node does not exist */
		return OID_NULL;
	}

	if (i == key_size) {
		/* Node is found */
		return node_p->value;
	} else {
		/* Recurse deeply */
		return rtree_map_opt_get_in_node(pop, node_p->slots[key[i]],
				key + i, key_size - i);
	}
}

/*
 * rtree_map_opt_get -- searches for a value of the key
 */
PMEMoid
rtree_map_opt_get(PMEMobjpool *pop, PMEMoid map,
		const unsigned char *key, uint64_t key_size)
{
	struct rtree_map *rtree_map = sobj_direct(pop, map);

	if (OID_IS_NULL(rtree_map->root)) {
		return OID_NULL;
	}

	return rtree_map_opt_get_in_node(pop, rtree_map->root, key, key_size);
}

/*
 * rtree_map_opt_lookup_in_node -- (internal) searches for key if exists
 */
static int
rtree_map_opt_lookup_in_node(PMEMobjpool *pop, PMEMoid node,
		const unsigned char *key, uint64_t key_size)
{
	unsigned i;

	if (OID_IS_NULL(node))
		return 0;

	i = key_comm_len(pop, node, key, key_size);

	struct tree_map_node *node_p = sobj_direct(pop, node);

	if (i != node_p->key_size) {
		/* Node does not exist */
		return 0;
	}

	if (i == key_size) {
		/* Node is found */
		return 1;
	}

	/* Recurse deeply */
	return rtree_map_opt_lookup_in_node(pop, node_p->slots[key[i]],
			key + i, key_size - i);
}

/*
 * rtree_map_opt_lookup -- searches if key exists
 */
int
rtree_map_opt_lookup(PMEMobjpool *pop, PMEMoid map,
		const unsigned char *key, uint64_t key_size)
{
	struct rtree_map *rtree_map = sobj_direct(pop, map);
	if (OID_IS_NULL(rtree_map->root)) {
		return 0;
	}
	return rtree_map_opt_lookup_in_node(pop, rtree_map->root, key, key_size);
}

/*
 * rtree_map_opt_foreach_node -- (internal) recursively traverses tree
 */
static int
rtree_map_opt_foreach_node(PMEMobjpool *pop, const PMEMoid node,
	int (*cb)(const unsigned char *key, uint64_t key_size,
			PMEMoid, void *arg),
	void *arg)
{
	unsigned i;

	if (OID_IS_NULL(node))
		return 0;

	struct tree_map_node *node_p = sobj_direct(pop, node);

	for (i = 0; i < ALPHABET_SIZE; i++) {
		if (rtree_map_opt_foreach_node(pop, node_p->slots[i], cb, arg) != 0) {
			return 1;
		}	
	}

	if (NULL != cb && (node_p->has_value || node_p->key_size > 0)) {
		if (cb(node_p->key,node_p->key_size,
					node_p->value, arg) != 0) {
			return 1;
		}
	}

	return 0;
}

/*
 * rtree_map_opt_foreach -- initiates recursive traversal
 */
int
rtree_map_opt_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(const unsigned char *key, uint64_t key_size,
			PMEMoid value, void *arg),
	void *arg)
{
	struct rtree_map *rtree_map = sobj_direct(pop, map);
	return rtree_map_opt_foreach_node(pop, rtree_map->root, cb, arg);
}

/*
 * ctree_map_check -- check if given persistent object is a tree map
 */
int
rtree_map_opt_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
}
