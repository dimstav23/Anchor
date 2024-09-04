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
 * btree_map_opt.c -- textbook implementation of btree /w preemptive splitting
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "btree_anchor_map_opt.h"

TOID_DECLARE(struct tree_map_node, BTREE_MAP_TYPE_OFFSET + 1);

#define BTREE_ORDER 8 /* can't be odd */
#define BTREE_MIN ((BTREE_ORDER / 2) - 1) /* min number of keys per node */

struct tree_map_node_item {
	uint64_t key;
	PMEMoid value;
};

struct tree_map_node {
	int n; /* number of occupied slots */
	struct tree_map_node_item items[BTREE_ORDER - 1];
	PMEMoid slots[BTREE_ORDER];
};

struct btree_map {
	PMEMoid root;
};

#define NODE_CONTAINS_ITEM(_n, _i, _k)\
((_i) != D_RO(_n)->n && D_RO(_n)->items[_i].key == (_k))

#define NODE_CHILD_CAN_CONTAIN_ITEM(_n, _i, _k)\
((_i) == D_RO(_n)->n || D_RO(_n)->items[_i].key > (_k)) &&\
!TOID_IS_NULL(D_RO(_n)->slots[_i])

#define ANCHOR_NODE_CONTAINS_ITEM(_n, _i, _k)\
((_i) != _n->n && _n->items[_i].key == (_k))

#define ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(_n, _i, _k)\
((_i) == _n->n || _n->items[_i].key > (_k)) &&\
!OID_IS_NULL(_n->slots[_i])

/*
 * set_empty_item -- (internal) sets null to the item
 */
static void
set_empty_item(struct tree_map_node_item *item)
{
	item->key = 0;
	item->value = OID_NULL;
}

/*
 * btree_map_opt_create -- allocates a new btree instance
 */
int
btree_map_opt_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		//pmemobj_tx_add_range_direct(map, sizeof(*map));
		//*map = TX_ZNEW(struct btree_map);
		//sobj_tx_add_range() //should be here!
		*map = sobj_tx_zalloc(sizeof(struct btree_map), BTREE_MAP_TYPE_OFFSET);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * btree_map_opt_clear_node -- (internal) removes all elements from the node
 */
static void
btree_map_opt_clear_node(PMEMobjpool *pop, PMEMoid node)
{
	struct tree_map_node *node_p = sobj_direct(pop, node);
	for (int i = 0; i < node_p->n; ++i) {
		btree_map_opt_clear_node(pop, node_p->slots[i]);
	}
	sobj_tx_free(node);
}

/*
 * btree_map_opt_clear -- removes all elements from the map
 */
int
btree_map_opt_clear(PMEMobjpool *pop, PMEMoid map)
{	
	int ret = 0;
	struct btree_map *btree_map = sobj_direct(pop, map);
	TX_BEGIN(pop) {
		btree_map_opt_clear_node(pop, btree_map->root);		
		sobj_tx_add_range(map, 0, sizeof(PMEMoid));
		btree_map->root = OID_NULL;
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * btree_map_opt_destroy -- cleanups and frees btree instance
 */
int
btree_map_opt_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	int ret = 0;
	TX_BEGIN(pop) {
		btree_map_opt_clear(pop, *map);
		//sobj_tx_add_range();
		//pmemobj_tx_add_range_direct(map, sizeof(*map)); /*wrong*/
		sobj_tx_free(*map);
		*map = OID_NULL;
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * btree_map_opt_insert_item_at -- (internal) inserts an item at position
 */
static void
btree_map_opt_insert_item_at(PMEMobjpool *pop, PMEMoid node, int pos,
	struct tree_map_node_item item)
{
	struct tree_map_node *node_p = sobj_direct(pop, node);
	node_p->items[pos] = item;
	node_p->n += 1;
	////node already snapshotted in the callers' function
}

/*
 * btree_map_opt_insert_empty -- (internal) inserts an item into an empty node
 */
static void
btree_map_opt_insert_empty(PMEMobjpool *pop, PMEMoid map,
	struct tree_map_node_item item)
{
	sobj_tx_add_range(map, 0, sizeof(PMEMoid));
	
	struct btree_map *btree_map = sobj_direct(pop, map);
	btree_map->root = sobj_tx_zalloc(sizeof(struct tree_map_node), BTREE_MAP_TYPE_OFFSET + 1);

	btree_map_opt_insert_item_at(pop, btree_map->root, 0, item);
}

/*
 * btree_map_opt_insert_node -- (internal) inserts and makes space for new node
 */
static void
btree_map_opt_insert_node(PMEMobjpool *pop, PMEMoid node, 
	int p, struct tree_map_node_item item,
	PMEMoid left, PMEMoid right)
{
	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	struct tree_map_node *node_p = sobj_direct(pop, node);

	if (node_p->items[p].key != 0) { /* move all existing data */
		memmove(&node_p->items[p + 1], &node_p->items[p],
		sizeof(struct tree_map_node_item) * ((BTREE_ORDER - 2 - p)));

		memmove(&node_p->slots[p + 1], &node_p->slots[p],
		sizeof(PMEMoid) * ((BTREE_ORDER - 1 - p)));
	}
	node_p->slots[p] = left;
	node_p->slots[p + 1] = right;

	btree_map_opt_insert_item_at(pop, node, p, item);
}

/*
 * btree_map_opt_create_split_node -- (internal) splits a node into two
 */
static PMEMoid
btree_map_opt_create_split_node(PMEMobjpool *pop, PMEMoid node,
	struct tree_map_node_item *m)
{
	PMEMoid right = sobj_tx_zalloc(sizeof(struct tree_map_node), BTREE_MAP_TYPE_OFFSET + 1);
	struct tree_map_node *node_p = sobj_direct(pop, node);

	int c = (BTREE_ORDER / 2);
	*m = node_p->items[c-1]; /* select median item */
	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	
	set_empty_item(&node_p->items[c - 1]);

	/* move everything right side of median to the new node */
	struct tree_map_node* right_p = sobj_direct(pop, right);

	for (int i = c; i < BTREE_ORDER; ++i) {
		if (i != BTREE_ORDER - 1) {
			right_p->items[right_p->n++] =
				node_p->items[i];
			set_empty_item(&node_p->items[i]);
		}
		right_p->slots[i - c] = node_p->slots[i];
		node_p->slots[i] = OID_NULL;
	}
	node_p->n = c - 1;

	return right;
}

/*
 * btree_map_opt_find_dest_node -- (internal) finds a place to insert the new key at
 */
static PMEMoid
btree_map_opt_find_dest_node(PMEMobjpool *pop, PMEMoid map,
	PMEMoid n, PMEMoid parent, uint64_t key, int *p)
{
	struct tree_map_node *n_p = sobj_direct(pop, n);
	if (n_p->n == BTREE_ORDER - 1) { /* node is full, perform a split */
		struct tree_map_node_item m;
		PMEMoid right =	btree_map_opt_create_split_node(pop, n, &m);

		if (!OID_IS_NULL(parent)) {
			btree_map_opt_insert_node(pop, parent, *p, m, n, right);
			if (key > m.key) /* select node to continue search */
				n = right;
		} else { /* replacing root node, the tree grows in height */
			PMEMoid up = sobj_tx_zalloc(sizeof(struct tree_map_node), BTREE_MAP_TYPE_OFFSET + 1);
			////struct tree_map_node up_temp = { 0 };	
			struct tree_map_node *up_temp = sobj_direct(pop, up);
			up_temp->n = 1;
			up_temp->items[0] = m;
			up_temp->slots[0] = n;
			up_temp->slots[1] = right;

			sobj_tx_add_range(map, 0, sizeof(struct btree_map));
            struct btree_map *btree_map = sobj_direct(pop, map);
            btree_map->root = up;

			n = up;
		}
	}
	n_p = sobj_direct(pop, n); //read the new n

	int i;
	PMEMoid ret;
	for (i = 0; i < BTREE_ORDER - 1; ++i) {
		*p = i;

		/*
		 * The key either fits somewhere in the middle or at the
		 * right edge of the node.
		 */
		if (n_p->n == i || n_p->items[i].key > key) {
			ret = OID_IS_NULL(n_p->slots[i]) ? n :
					btree_map_opt_find_dest_node(pop, map,
					n_p->slots[i], n, key, p);
			return ret;
		}
	}

	/*
	 * The key is bigger than the last node element, go one level deeper
	 * in the rightmost child.
	 */
	ret =  btree_map_opt_find_dest_node(pop, map, n_p->slots[i], n, key, p);
	return ret;
}

/*
 * btree_map_opt_insert_item -- (internal) inserts and makes space for new item
 */
static void
btree_map_opt_insert_item(PMEMobjpool *pop, PMEMoid node, 
	int p, struct tree_map_node_item item)
{
	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	//TX_ADD(node);
	struct tree_map_node *node_p = sobj_direct(pop, node);
	if (node_p->items[p].key != 0) {
		memmove(&node_p->items[p + 1], &node_p->items[p],
		sizeof(struct tree_map_node_item) * ((BTREE_ORDER - 2 - p)));
	}
	btree_map_opt_insert_item_at(pop, node, p, item);
}

/*
 * btree_map_opt_is_empty -- checks whether the tree map is empty
 */
int
btree_map_opt_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	int ret = 0;
	struct btree_map *btree_map = sobj_direct(pop, map);
	if (OID_IS_NULL(btree_map->root)) {
		ret = 1;
	}
	else {
		struct tree_map_node *root_p = sobj_direct(pop, btree_map->root);
		if (root_p->n == 0) {
			ret = 1;
		}
	}
	return ret;
}

/*
 * btree_map_opt_check_in_node -- (internal) searches for a value in the node
 */
static int
btree_map_opt_check_in_node(PMEMobjpool *pop, PMEMoid node, uint64_t key, PMEMoid value)
{

	struct tree_map_node *node_p = sobj_direct(pop, node);
	for (int i = 0; i <= node_p->n; ++i) {
		if (ANCHOR_NODE_CONTAINS_ITEM(node_p, i, key)) {
			if (!OID_EQUALS(node_p->items[i].value, value)) {
				TX_BEGIN(pop) {
					sobj_tx_add_range(node, offsetof(struct tree_map_node, items) +
							i * sizeof(struct tree_map_node_item) + 
							offsetof(struct tree_map_node_item, value), 
							sizeof(PMEMoid));
					sobj_tx_free(node_p->items[i].value);
					node_p->items[i].value = value;
				} TX_END
			}
			return 1;
		}
		else if (ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(node_p, i, key)) {
			return btree_map_opt_check_in_node(pop, node_p->slots[i], key, value);
		}
	}
	return 0;
}

/*
 * btree_map_opt_update_check -- searches for a value of the key
 */
static int
btree_map_opt_update_check(PMEMobjpool *pop, PMEMoid map, uint64_t key, PMEMoid value)
{	
	
	struct btree_map *btree_map = sobj_direct(pop, map);
	if (OID_IS_NULL(btree_map->root)) {
		return 0;
	}
	return btree_map_opt_check_in_node(pop, btree_map->root, key, value);
}

/*
 * btree_map_opt_insert -- inserts a new key-value pair into the map
 */
int
btree_map_opt_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value)
{
	//first check if the value exists in order to update it
	if (btree_map_opt_update_check(pop, map, key, value)) {
		return 0;
	}

	struct tree_map_node_item item = {key, value};
	TX_BEGIN(pop) {
		if (btree_map_opt_is_empty(pop, map)) {
			btree_map_opt_insert_empty(pop, map, item);
		} else {
			int p; /* position at the dest node to insert */
			struct btree_map *btree_map = sobj_direct(pop, map);
			PMEMoid parent = OID_NULL;
			PMEMoid dest =
				btree_map_opt_find_dest_node(pop, map, btree_map->root,
					parent, key, &p);
			btree_map_opt_insert_item(pop, dest, p, item);
		}
	} TX_END

	return 0;
}

/*
 * btree_map_opt_rotate_right -- (internal) takes one element from right sibling
 */
static void
btree_map_opt_rotate_right(PMEMobjpool *pop, PMEMoid rsb,
	PMEMoid node, PMEMoid parent, int p)
{
	struct tree_map_node *parent_p = sobj_direct(pop, parent);
	struct tree_map_node *node_p = sobj_direct(pop, node);
	struct tree_map_node *rsb_p = sobj_direct(pop, rsb);

	/* move the separator from parent to the deficient node */
	struct tree_map_node_item sep = parent_p->items[p];
	btree_map_opt_insert_item(pop, node, node_p->n, sep);
	node_p = sobj_direct(pop, node); //re-read because it might have been changed 

	/* the first element of the right sibling is the new separator */
	sobj_tx_add_range(parent, offsetof(struct tree_map_node, items) + 
					p * sizeof(struct tree_map_node_item),
					sizeof(struct tree_map_node_item));
	parent_p->items[p] = rsb_p->items[0];

	/* the nodes are not necessarily leafs, so copy also the slot */
	sobj_tx_add_range(node, offsetof(struct tree_map_node, slots) + 
					(node_p->n) * sizeof(PMEMoid),
					sizeof(PMEMoid));
	node_p->slots[node_p->n] = rsb_p->slots[0];

	sobj_tx_add_range(rsb, 0, sizeof(struct tree_map_node));
	rsb_p->n -= 1;

	/* move all existing elements back by one array slot */
	memmove(rsb_p->items, rsb_p->items + 1,
		sizeof(struct tree_map_node_item) * (rsb_p->n));
	memmove(rsb_p->slots, rsb_p->slots + 1,
		sizeof(PMEMoid) * ((rsb_p->n) + 1));
}

/*
 * btree_map_opt_rotate_left -- (internal) takes one element from left sibling
 */
static void
btree_map_opt_rotate_left(PMEMobjpool *pop, PMEMoid lsb,
	PMEMoid node, PMEMoid parent, int p)
{	
	struct tree_map_node *parent_p = sobj_direct(pop, parent);

	/* move the separator from parent to the deficient node */
	struct tree_map_node_item sep = parent_p->items[p - 1];
	btree_map_opt_insert_item(pop, node, 0, sep);

	struct tree_map_node *node_p = sobj_direct(pop, node); //re-read because it might have been changed 
	struct tree_map_node *lsb_p = sobj_direct(pop, lsb);

	/* the last element of the left sibling is the new separator */
	sobj_tx_add_range(parent, offsetof(struct tree_map_node, items) + 
					(p-1) * sizeof(struct tree_map_node_item),
					sizeof(struct tree_map_node_item));
	parent_p->items[p - 1] = lsb_p->items[lsb_p->n - 1];

	/* rotate the node children */;
	memmove(node_p->slots + 1, node_p->slots,
		sizeof(PMEMoid) * (node_p->n));

	/* the nodes are not necessarily leafs, so copy also the slot */
	node_p->slots[0] = lsb_p->slots[lsb_p->n];
	////snapshotted in btree_map_opt_insert_time

	sobj_tx_add_range(lsb, offsetof(struct tree_map_node, n), sizeof(int));
	lsb_p->n -= 1;
}

/*
 * btree_map_opt_merge -- (internal) merges node and right sibling
 */
static void
btree_map_opt_merge(PMEMobjpool *pop, PMEMoid map, PMEMoid rn,
	PMEMoid node, PMEMoid parent, int p)
{
	struct tree_map_node *parent_p = sobj_direct(pop, parent);
	struct tree_map_node *node_p = sobj_direct(pop, node);
	struct tree_map_node *rn_p = sobj_direct(pop, rn);

	struct tree_map_node_item sep = parent_p->items[p];

	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	/* add separator to the deficient node */
	node_p->items[node_p->n++] = sep;
	
	/* copy right sibling data to node */
	memcpy(&node_p->items[node_p->n], rn_p->items,
	sizeof(struct tree_map_node_item) * rn_p->n);
	memcpy(&node_p->slots[node_p->n], rn_p->slots,
	sizeof(PMEMoid) * (rn_p->n + 1));

	node_p->n += rn_p->n;

	sobj_tx_free(rn); /* right node is now empty */

	sobj_tx_add_range(parent, 0, sizeof(struct tree_map_node));
	parent_p->n -= 1;

	/* move everything to the right of the separator by one array slot */
	memmove(parent_p->items + p, parent_p->items + p + 1,
	sizeof(struct tree_map_node_item) * (parent_p->n - p));

	memmove(parent_p->slots + p + 1, parent_p->slots + p + 2,
	sizeof(PMEMoid) * (parent_p->n - p + 1));

	/* if the parent is empty then the tree shrinks in height */
	struct btree_map *btree_map = sobj_direct(pop, map);
	if (parent_p->n == 0 && OID_EQUALS(parent, btree_map->root)) {
		sobj_tx_add_range(map, 0, sizeof(struct btree_map));
		sobj_tx_free(btree_map->root);
        btree_map->root = node;
	}
}

/*
 * btree_map_opt_rebalance -- (internal) performs tree rebalance
 */
static void
btree_map_opt_rebalance(PMEMobjpool *pop, PMEMoid map, PMEMoid node,
	PMEMoid parent, int p)
{
	struct tree_map_node *parent_p = sobj_direct(pop, parent);

	PMEMoid rsb = p >= parent_p->n ? OID_NULL : parent_p->slots[p + 1];
	PMEMoid lsb = p == 0 ? OID_NULL : parent_p->slots[p - 1];

	if (!OID_IS_NULL(rsb)) {
		struct tree_map_node *rsb_p = sobj_direct(pop, rsb);
		if (rsb_p->n > BTREE_MIN) {
			btree_map_opt_rotate_right(pop, rsb, node, parent, p);
			return;
		}
	}
	
	if (!OID_IS_NULL(lsb)) {
		struct tree_map_node *lsb_p = sobj_direct(pop, lsb);
		if (lsb_p->n > BTREE_MIN) {
			btree_map_opt_rotate_left(pop, lsb, node, parent, p);
			return;
		}	
	}		
	
	if (OID_IS_NULL(rsb)) /* always merge with rightmost node */
		btree_map_opt_merge(pop, map, node, lsb, parent, p - 1);
	else
		btree_map_opt_merge(pop, map, rsb, node, parent, p);
}

/*
 * btree_map_opt_get_leftmost_leaf -- (internal) searches for the successor
 */
static PMEMoid
btree_map_opt_get_leftmost_leaf(PMEMobjpool *pop, PMEMoid map,
	PMEMoid n, PMEMoid *p)
{
	struct tree_map_node *n_p = sobj_direct(pop, n);
	if (OID_IS_NULL(n_p->slots[0])) {
		return n;
	}

	*p = n;
	PMEMoid ret = btree_map_opt_get_leftmost_leaf(pop, map, n_p->slots[0], p);
	return ret;
}

/*
 * btree_map_opt_remove_from_node -- (internal) removes element from node
 */
static void
btree_map_opt_remove_from_node(PMEMobjpool* pop, PMEMoid map,
	PMEMoid node, PMEMoid parent, int p)
{
	struct tree_map_node *node_p = sobj_direct(pop, node);
	
	if (OID_IS_NULL(node_p->slots[0])) { /* leaf */
		sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
		if (node_p->n == 1 || p == BTREE_ORDER - 2) {
			set_empty_item(&node_p->items[p]);
		} else if (node_p->n != 1) {
			memmove(&node_p->items[p],
				&node_p->items[p + 1],
				sizeof(struct tree_map_node_item) *
				((node_p->n) - p));
		}

		node_p->n -= 1;
		return;
	}

	/* can't delete from non-leaf nodes, remove successor */
	PMEMoid rchild = node_p->slots[p + 1];
	PMEMoid lp = node;
	PMEMoid lm = btree_map_opt_get_leftmost_leaf(pop, map, rchild, &lp);

	sobj_tx_add_range(node, offsetof(struct tree_map_node, items) +
						p * sizeof(struct tree_map_node_item), 
						sizeof(struct tree_map_node_item));
	struct tree_map_node *lm_p = sobj_direct(pop, lm);
    node_p->items[p] = lm_p->items[0];

	btree_map_opt_remove_from_node(pop, map, lm, lp, 0);
	lm_p = sobj_direct(pop, lm); //re-read because it might have been changed - might be redundant

	if (lm_p->n < BTREE_MIN) /* right child can be deficient now */
		btree_map_opt_rebalance(pop, map, lm, lp,
			OID_EQUALS(lp, node) ? p + 1 : 0);
}

/*
 * btree_map_opt_remove_item -- (internal) removes item from node
 */
static PMEMoid
btree_map_opt_remove_item(PMEMobjpool *pop, PMEMoid map,
	PMEMoid node, PMEMoid parent, uint64_t key, int p)
{
	PMEMoid ret = OID_NULL;
	struct tree_map_node *node_p = sobj_direct(pop, node);
	for (int i = 0; i <= node_p->n; ++i) {
		if (ANCHOR_NODE_CONTAINS_ITEM(node_p, i, key)) {
			ret = node_p->items[i].value;
			btree_map_opt_remove_from_node(pop, map, node, parent, i);
			break;
		} else if (ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(node_p, i, key)) {
			ret = btree_map_opt_remove_item(pop, map, node_p->slots[i],
				node, key, i);
			break;
		}
	}
	/* node might have been changed here */

	if (!OID_IS_NULL(parent)){
        //** might be redundant as the volatile copy is updated
		node_p = sobj_direct(pop, node);
		/* check for deficient nodes walking up */
		if (node_p->n < BTREE_MIN)
			btree_map_opt_rebalance(pop, map, node, parent, p);
	}
	return ret;
}

/*
 * btree_map_opt_remove -- removes key-value pair from the map
 */
PMEMoid
btree_map_opt_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid ret = OID_NULL;

	struct btree_map *btree_map = sobj_direct(pop, map);
	
	TX_BEGIN(pop) {
		ret = btree_map_opt_remove_item(pop, map, btree_map->root,
				OID_NULL, key, 0);
	} TX_END

	return ret;
}

/*
 * btree_map_opt_get_in_node -- (internal) searches for a value in the node
 */
static PMEMoid
btree_map_opt_get_in_node(PMEMobjpool *pop, PMEMoid node, uint64_t key)
{
	struct tree_map_node *node_p = sobj_direct(pop, node);
	PMEMoid ret = OID_NULL;
	for (int i = 0; i <= node_p->n; ++i) {
		if (ANCHOR_NODE_CONTAINS_ITEM(node_p, i, key)) {
			ret = node_p->items[i].value;
			return ret;
		}
		else if (ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(node_p, i, key)) {
			ret = btree_map_opt_get_in_node(pop, node_p->slots[i], key);
			return ret;
		}
	}
	return ret;
}

/*
 * btree_map_opt_get -- searches for a value of the key
 */
PMEMoid
btree_map_opt_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{	
	struct btree_map *btree_map = sobj_direct(pop, map);
	if (OID_IS_NULL(btree_map->root)) {
		return OID_NULL;
	}
	return btree_map_opt_get_in_node(pop, btree_map->root, key);
}

/*
 * btree_map_opt_lookup_in_node -- (internal) searches for key if exists
 */
static int
btree_map_opt_lookup_in_node(PMEMobjpool *pop, PMEMoid node, uint64_t key)
{
	struct tree_map_node *node_p = sobj_direct(pop, node);
	for (int i = 0; i <= node_p->n; ++i) {
		if (ANCHOR_NODE_CONTAINS_ITEM(node_p, i, key)) {
			return 1;
		}
		else if (ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(node_p, i, key)) {
			return btree_map_opt_lookup_in_node(pop,
					node_p->slots[i], key);
		}
	}
	return 0;
}

/*
 * btree_map_opt_lookup -- searches if key exists
 */
int
btree_map_opt_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	struct btree_map *btree_map = sobj_direct(pop, map);
	if (OID_IS_NULL(btree_map->root)) {
		return 0;
	}
	return btree_map_opt_lookup_in_node(pop, btree_map->root, key);
}

/*
 * btree_map_opt_foreach_node -- (internal) recursively traverses tree
 */
static int
btree_map_opt_foreach_node(PMEMobjpool *pop, const PMEMoid p,
	int (*cb)(uint64_t key, PMEMoid, void *arg), void *arg)
{
	if (OID_IS_NULL(p))
		return 0;

	struct tree_map_node *p_p = sobj_direct(pop, p);

	for (int i = 0; i <= p_p->n; ++i) {
		if (btree_map_opt_foreach_node(pop, p_p->slots[i], cb, arg) != 0) {
			return 1;
		}
		if (i != p_p->n && p_p->items[i].key != 0) {
			if (cb(p_p->items[i].key, p_p->items[i].value,
					arg) != 0){
				return 1;
			}
		}
	}
	return 0;
}

/*
 * btree_map_opt_foreach -- initiates recursive traversal
 */
int
btree_map_opt_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	struct btree_map *btree_map = sobj_direct(pop, map);
	return btree_map_opt_foreach_node(pop, btree_map->root, cb, arg);
}

/*
 * ctree_map_check -- check if given persistent object is a tree map
 */
int
btree_map_opt_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
}

/*
 * btree_map_opt_insert_new -- allocates a new object and inserts it into the tree
 */
int
btree_map_opt_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size, unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid n = sobj_tx_alloc(size, type_num);
		constructor(pop, sec_pmemobj_direct(n), arg);
		btree_map_opt_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * btree_map_opt_remove_free -- removes and frees an object from the tree
 */
int
btree_map_opt_remove_free(PMEMobjpool *pop, PMEMoid map,
		uint64_t key)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid val = btree_map_opt_remove(pop, map, key);
		sobj_tx_free(val);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}
