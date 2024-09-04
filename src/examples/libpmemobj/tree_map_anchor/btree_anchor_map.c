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
 * btree_map.c -- textbook implementation of btree /w preemptive splitting
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "btree_anchor_map.h"

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
	//TOID(struct tree_map_node) slots[BTREE_ORDER];
	PMEMoid slots[BTREE_ORDER];
};

struct btree_map {
	//TOID(struct tree_map_node) root;
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
 * btree_map_create -- allocates a new btree instance
 */
int
btree_map_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
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
 * btree_map_clear_node -- (internal) removes all elements from the node
 */
static void
btree_map_clear_node(PMEMobjpool *pop, PMEMoid node)
{
	
	struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
	
	for (int i = 0; i < node_p->n; ++i) {
		btree_map_clear_node(pop, node_p->slots[i]);
	}

	sobj_tx_free(node);
	free(node_p);

	/*
	for (int i = 0; i < D_RO(node)->n; ++i) {
		btree_map_clear_node(pop, D_RO(node)->slots[i]);
	}

	TX_FREE(node);
	*/
}

/*
 * btree_map_clear -- removes all elements from the map
 */
int
btree_map_clear(PMEMobjpool *pop, PMEMoid map)
{
	
	int ret = 0;
	struct btree_map *btree_map = sobj_direct(pop, map);
	TX_BEGIN(pop) {
		btree_map_clear_node(pop, btree_map->root);
		
		sobj_tx_add_range(map, 0, sizeof(PMEMoid));
		//TX_ADD_FIELD(map, root);

		////btree_map->root = OID_NULL;
		btree_map->root = OID_NULL;
		////sobj_tx_write(pop, map, btree_map);
		
		//D_RW(map)->root = TOID_NULL(struct tree_map_node);
	} TX_ONABORT {
		//free(btree_map);
		ret = 1;
	} TX_END
	//free(btree_map);
	return ret;
}

/*
 * btree_map_destroy -- cleanups and frees btree instance
 */
int
btree_map_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	
	int ret = 0;
	TX_BEGIN(pop) {
		btree_map_clear(pop, *map);
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
 * btree_map_insert_item_at -- (internal) inserts an item at position
 */
static void
btree_map_insert_item_at(PMEMobjpool *pop, PMEMoid node, int pos,
	struct tree_map_node_item item)
{
	
	struct tree_map_node *node_p = sobj_direct(pop, node);
	node_p->items[pos] = item;
	node_p->n += 1;
	//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
	
	////sobj_tx_write(pop, node, node_p);
	//memcpy(node_p_mod, node_p, sizeof(struct tree_map_node));
	//free(node_p);
	
	//D_RW(node)->items[pos] = item;
	//D_RW(node)->n += 1;
}

/*
 * btree_map_insert_empty -- (internal) inserts an item into an empty node
 */
static void
btree_map_insert_empty(PMEMobjpool *pop, PMEMoid map,
	struct tree_map_node_item item)
{
	
	sobj_tx_add_range(map, 0, sizeof(PMEMoid));
	//TX_ADD_FIELD(map, root);
	
	struct btree_map *btree_map = sobj_direct(pop, map);
	btree_map->root = sobj_tx_zalloc(sizeof(struct tree_map_node), BTREE_MAP_TYPE_OFFSET + 1);
	//D_RW(map)->root = TX_ZNEW(struct tree_map_node);
	//struct btree_map *btree_map_mod = sobj_direct(pop, map);
	//btree_map_mod->root = btree_map->root;
	////sobj_tx_write(pop, map, btree_map);

	btree_map_insert_item_at(pop, btree_map->root, 0, item);
	//free(btree_map);
}

/*
 * btree_map_insert_node -- (internal) inserts and makes space for new node
 */
static void
btree_map_insert_node(PMEMobjpool *pop, PMEMoid node, 
	int p, struct tree_map_node_item item,
	PMEMoid left, PMEMoid right)
{
	
	//TX_ADD(node);
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
	//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
	//memcpy(node_p_mod, node_p, sizeof(struct tree_map_node));
	////sobj_tx_write(pop, node, node_p);
	btree_map_insert_item_at(pop, node, p, item);
	//free(node_p);
	/*
	if (D_RO(node)->items[p].key != 0) {
		memmove(&D_RW(node)->items[p + 1], &D_RW(node)->items[p],
		sizeof(struct tree_map_node_item) * ((BTREE_ORDER - 2 - p)));

		memmove(&D_RW(node)->slots[p + 1], &D_RW(node)->slots[p],
		sizeof(TOID(struct tree_map_node)) * ((BTREE_ORDER - 1 - p)));
	}
	D_RW(node)->slots[p] = left;
	D_RW(node)->slots[p + 1] = right;
	btree_map_insert_item_at(pop, node, p, item);
	*/
}

/*
 * btree_map_create_split_node -- (internal) splits a node into two
 */
static PMEMoid
btree_map_create_split_node(PMEMobjpool *pop, PMEMoid node,
	struct tree_map_node_item *m)
{
	
	PMEMoid right = sobj_tx_zalloc(sizeof(struct tree_map_node), BTREE_MAP_TYPE_OFFSET + 1);
	struct tree_map_node *node_p = sobj_direct(pop, node);

	int c = (BTREE_ORDER / 2);
	*m = node_p->items[c-1]; /* select median item */
	//*m = D_RO(node)->items[c - 1]; 
	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	//TX_ADD(node);
	
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
	//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
	//memcpy(node_p_mod, node_p, sizeof(struct tree_map_node));
	////sobj_tx_write(pop, node, node_p);
	//struct tree_map_node *right_p_mod = sobj_direct(pop, right);
	//memcpy(right_p_mod, right_p, sizeof(struct tree_map_node));
	////sobj_tx_write(pop, right, right_p);
	//free(node_p);
	//free(right_p);

	return right;

	/*
	for (int i = c; i < BTREE_ORDER; ++i) {
		if (i != BTREE_ORDER - 1) {
			D_RW(right)->items[D_RW(right)->n++] =
				D_RO(node)->items[i];
			set_empty_item(&D_RW(node)->items[i]);
		}
		D_RW(right)->slots[i - c] = D_RO(node)->slots[i];
		D_RW(node)->slots[i] = TOID_NULL(struct tree_map_node);
	}
	D_RW(node)->n = c - 1;

	return right;
	*/
}

/*
 * btree_map_find_dest_node -- (internal) finds a place to insert the new key at
 */
static PMEMoid
btree_map_find_dest_node(PMEMobjpool *pop, PMEMoid map,
	PMEMoid n, PMEMoid parent, uint64_t key, int *p)
{
	
	struct tree_map_node *n_p = sobj_read(pop, n, 1, NULL);
	if (n_p->n == BTREE_ORDER - 1) { /* node is full, perform a split */
		struct tree_map_node_item m;
		PMEMoid right =	btree_map_create_split_node(pop, n, &m);

		if (!OID_IS_NULL(parent)) {
			btree_map_insert_node(pop, parent, *p, m, n, right);
			if (key > m.key) /* select node to continue search */
				n = right;
		} else { /* replacing root node, the tree grows in height */
			PMEMoid up = sobj_tx_zalloc(sizeof(struct tree_map_node), BTREE_MAP_TYPE_OFFSET + 1);
			struct tree_map_node *up_p_mod = sobj_direct(pop, up);
			up_p_mod->n = 1;
			up_p_mod->items[0] = m;
			up_p_mod->slots[0] = n;
			up_p_mod->slots[1] = right;
			/*
			struct tree_map_node up_temp = { 0 };	
			up_temp.n = 1;
			up_temp.items[0] = m;
			up_temp.slots[0] = n;
			up_temp.slots[1] = right;
			*/
			//struct tree_map_node *up_p_mod = sobj_direct(pop, up);
			//memcpy(up_p_mod, &up_temp, sizeof(struct tree_map_node));
			////sobj_tx_write(pop, up, &up_temp); 

			sobj_tx_add_range(map, 0, sizeof(struct btree_map));
			//TX_ADD_FIELD(map, root);
			//D_RW(map)->root = up;
			struct btree_map *btree_map_mod = sobj_direct(pop, map);
			btree_map_mod->root = up;
			////sobj_tx_write(pop, map, &up);
			n = up;
		}
	}
	free(n_p);
	n_p = sobj_read(pop, n, 1, NULL); //read the new n

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
					btree_map_find_dest_node(pop, map,
					n_p->slots[i], n, key, p);
			free(n_p);
			return ret;
		}
	}

	/*
	 * The key is bigger than the last node element, go one level deeper
	 * in the rightmost child.
	 */
	ret =  btree_map_find_dest_node(pop, map, n_p->slots[i], n, key, p);
	free(n_p);
	return ret;
}

/*
 * btree_map_insert_item -- (internal) inserts and makes space for new item
 */
static void
btree_map_insert_item(PMEMobjpool *pop, PMEMoid node, 
	int p, struct tree_map_node_item item)
{
	
	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	//TX_ADD(node);
	struct tree_map_node *node_p = sobj_direct(pop, node);
	if (node_p->items[p].key != 0) {
		memmove(&node_p->items[p + 1], &node_p->items[p],
		sizeof(struct tree_map_node_item) * ((BTREE_ORDER - 2 - p)));
	}
	//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
	//memcpy(node_p_mod, node_p, sizeof(struct tree_map_node));
	////sobj_tx_write(pop, node, node_p);
	//free(node_p);
	btree_map_insert_item_at(pop, node, p, item);
	/*
	if (D_RO(node)->items[p].key != 0) {
		memmove(&D_RW(node)->items[p + 1], &D_RW(node)->items[p],
		sizeof(struct tree_map_node_item) * ((BTREE_ORDER - 2 - p)));
	}
	btree_map_insert_item_at(pop, node, p, item);
	*/
}

/*
 * btree_map_is_empty -- checks whether the tree map is empty
 */
int
btree_map_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	
	int ret = 0;
	struct btree_map *btree_map = sobj_read(pop, map, 1, NULL);
	if (OID_IS_NULL(btree_map->root)) {
		ret = 1;
	}
	else {
		struct tree_map_node *root_p = sobj_read(pop, btree_map->root, 1, NULL);
		if (root_p->n == 0) {
			ret = 1;
		}
		free(root_p);
	}
	free(btree_map);
	return ret;
	//return TOID_IS_NULL(D_RO(map)->root) || D_RO(D_RO(map)->root)->n == 0;
}


/*
 * btree_map_check_in_node -- (internal) searches for a value in the node
 */
static int
btree_map_check_in_node(PMEMobjpool *pop, PMEMoid node, uint64_t key, PMEMoid value)
{
	struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
	int ret = 0;
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
					struct tree_map_node *node_p_mod = sobj_direct(pop, node);
					node_p_mod->items[i].value = value;
					
					////sobj_tx_write_part(pop, node, offsetof(struct tree_map_node, items) +
					////		i * sizeof(struct tree_map_node_item) + 
					////		offsetof(struct tree_map_node_item, value), 
					////		sizeof(PMEMoid), &node_p->items[i].value);
				} TX_END
			}
			free(node_p);
			return 1;
		}
		else if (ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(node_p, i, key)) {
			ret = btree_map_check_in_node(pop, node_p->slots[i], key, value);
			free(node_p);
			return ret;
		}
	}
	free(node_p);
	return ret;
}

/*
 * btree_map_update_check -- searches for a value of the key
 */
static int
btree_map_update_check(PMEMobjpool *pop, PMEMoid map, uint64_t key, PMEMoid value)
{	
	
	struct btree_map *btree_map = sobj_read(pop, map, 1, NULL);
	if (OID_IS_NULL(btree_map->root)) {
		free(btree_map);
		return 0;
	}
	int ret = btree_map_check_in_node(pop, btree_map->root, key, value);
	free(btree_map);
	return ret;
}
/*
 * btree_map_insert -- inserts a new key-value pair into the map
 */
int
btree_map_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value)
{
	//first check if the value exists in order to update it
	if (btree_map_update_check(pop, map, key, value)) {
		return 0;
	}

	struct tree_map_node_item item = {key, value};

	TX_BEGIN(pop) {
		if (btree_map_is_empty(pop, map)) {
			btree_map_insert_empty(pop, map, item);
		} else {
			int p; /* position at the dest node to insert */
			struct btree_map *btree_map = sobj_read(pop, map, 1, NULL);
			PMEMoid parent = OID_NULL;
			PMEMoid dest =
				btree_map_find_dest_node(pop, map, btree_map->root,
					parent, key, &p);
			free(btree_map);
			btree_map_insert_item(pop, dest, p, item);
		}
	} TX_END
	
	return 0;
}

/*
 * btree_map_rotate_right -- (internal) takes one element from right sibling
 */
static void
btree_map_rotate_right(PMEMobjpool *pop, PMEMoid rsb,
	PMEMoid node, PMEMoid parent, int p)
{
	
	struct tree_map_node *parent_p = sobj_direct(pop, parent);
	struct tree_map_node *node_p = sobj_direct(pop, node);
	struct tree_map_node *rsb_p = sobj_direct(pop, rsb);

	/* move the separator from parent to the deficient node */
	struct tree_map_node_item sep = parent_p->items[p];
	btree_map_insert_item(pop, node, node_p->n, sep);
	//free(node_p);
	//node_p = sobj_read(pop, node, 1, NULL); //re-read because it might have been changed 
	
	//struct tree_map_node_item sep = D_RO(parent)->items[p];
	//btree_map_insert_item(pop, node, D_RO(node)->n, sep);

	/* the first element of the right sibling is the new separator */
	//TX_ADD_FIELD(parent, items[p]);
	//D_RW(parent)->items[p] = D_RO(rsb)->items[0];
	sobj_tx_add_range(parent, offsetof(struct tree_map_node, items) + 
					p * sizeof(struct tree_map_node_item),
					sizeof(struct tree_map_node_item));
	parent_p->items[p] = rsb_p->items[0];
	//struct tree_map_node *parent_p_mod = sobj_direct(pop, parent);
	//parent_p_mod->items[p] = rsb_p->items[0];
	////sobj_tx_write_part(pop, parent, offsetof(struct tree_map_node, items) + 
	////				p * sizeof(struct tree_map_node_item), 
	////				sizeof(struct tree_map_node_item), &parent_p->items[p]);

	/* the nodes are not necessarily leafs, so copy also the slot */
	//TX_ADD_FIELD(node, slots[D_RO(node)->n]);
	//D_RW(node)->slots[D_RO(node)->n] = D_RO(rsb)->slots[0];
	sobj_tx_add_range(node, offsetof(struct tree_map_node, slots) + 
					(node_p->n) * sizeof(PMEMoid),
					sizeof(PMEMoid));
	node_p->slots[node_p->n] = rsb_p->slots[0];
	//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
	//node_p_mod->slots[node_p->n] = rsb_p->slots[0];
	////sobj_tx_write_part(pop, node, offsetof(struct tree_map_node, slots) + 
	////				node_p->n * sizeof(PMEMoid), sizeof(PMEMoid), &node_p->slots[node_p->n]);

	//TX_ADD(rsb);
	//D_RW(rsb)->n -= 1; /* it loses one element, but still > min */
	sobj_tx_add_range(rsb, 0, sizeof(struct tree_map_node));
	rsb_p->n -= 1;

	/* move all existing elements back by one array slot */
	memmove(rsb_p->items, rsb_p->items + 1,
		sizeof(struct tree_map_node_item) * (rsb_p->n));
	memmove(rsb_p->slots, rsb_p->slots + 1,
		sizeof(PMEMoid) * ((rsb_p->n) + 1));
	
	//struct tree_map_node *rsb_p_mod = sobj_direct(pop, rsb);
	//memcpy(rsb_p_mod, rsb_p, sizeof(struct tree_map_node));
	////sobj_tx_write(pop, rsb, rsb_p);

	//free(parent_p);
	//free(node_p);
	//free(rsb_p);
}

/*
 * btree_map_rotate_left -- (internal) takes one element from left sibling
 */
static void
btree_map_rotate_left(PMEMobjpool *pop, PMEMoid lsb,
	PMEMoid node, PMEMoid parent, int p)
{	
	struct tree_map_node *parent_p = sobj_direct(pop, parent);

	/* move the separator from parent to the deficient node */
	//struct tree_map_node_item sep = D_RO(parent)->items[p - 1];
	//btree_map_insert_item(pop, node, 0, sep);
	struct tree_map_node_item sep = parent_p->items[p - 1];
	btree_map_insert_item(pop, node, 0, sep);

	struct tree_map_node *node_p = sobj_direct(pop, node); //re-read because it might have been changed 
	struct tree_map_node *lsb_p = sobj_direct(pop, lsb);

	/* the last element of the left sibling is the new separator */
	//TX_ADD_FIELD(parent, items[p - 1]);
	//D_RW(parent)->items[p - 1] = D_RO(lsb)->items[D_RO(lsb)->n - 1];
	sobj_tx_add_range(parent, offsetof(struct tree_map_node, items) + 
					(p-1) * sizeof(struct tree_map_node_item),
					sizeof(struct tree_map_node_item));
	parent_p->items[p - 1] = lsb_p->items[lsb_p->n - 1];
	//struct tree_map_node *parent_p_mod = sobj_direct(pop, parent);
	//parent_p_mod->items[p - 1] = lsb_p->items[lsb_p->n - 1];
	////sobj_tx_write_part(pop, parent, offsetof(struct tree_map_node, items) + 
	////				(p-1) * sizeof(struct tree_map_node_item), 
	////				sizeof(struct tree_map_node_item), &parent_p->items[p - 1]);

	/* rotate the node children */
	//memmove(D_RW(node)->slots + 1, D_RO(node)->slots,
	//	sizeof(TOID(struct tree_map_node)) * (D_RO(node)->n));
	memmove(node_p->slots + 1, node_p->slots,
		sizeof(PMEMoid) * (node_p->n));

	/* the nodes are not necessarily leafs, so copy also the slot */
	//D_RW(node)->slots[0] = D_RO(lsb)->slots[D_RO(lsb)->n];
	node_p->slots[0] = lsb_p->slots[lsb_p->n];
	//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
	//node_p_mod->slots[0] = lsb_p->slots[lsb_p->n];
	////sobj_tx_write(pop, node, node_p);
	//sobj_tx_write_part(pop, node, offsetof(struct tree_map_node, slots), sizeof(PMEMoid), &node_p->slots[0]);

	//TX_ADD_FIELD(lsb, n);
	//D_RW(lsb)->n -= 1; /* it loses one element, but still > min */
	lsb_p->n -= 1;
	sobj_tx_add_range(lsb, offsetof(struct tree_map_node, n), sizeof(int));
	//struct tree_map_node *lsb_p_mod = sobj_direct(pop, lsb);
	//lsb_p_mod->n -= 1;
	////sobj_tx_write_part(pop, lsb, offsetof(struct tree_map_node, n), sizeof(int), &lsb_p->n);

	//free(parent_p);
	//free(node_p);
	//free(lsb_p);
}

/*
 * btree_map_merge -- (internal) merges node and right sibling
 */
static void
btree_map_merge(PMEMobjpool *pop, PMEMoid map, PMEMoid rn,
	PMEMoid node, PMEMoid parent, int p)
{
	
	struct tree_map_node *parent_p = sobj_direct(pop, parent);
	struct tree_map_node *node_p = sobj_direct(pop, node);
	struct tree_map_node *rn_p = sobj_read(pop, rn, 1, NULL);

	struct tree_map_node_item sep = parent_p->items[p];

	//TX_ADD(node);
	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	/* add separator to the deficient node */
	//D_RW(node)->items[D_RW(node)->n++] = sep;
	node_p->items[node_p->n++] = sep;
	//sobj_tx_write_part(pop, node, offsetof(struct tree_map_node, items) + 
	//					node_p->n * sizeof(struct(tree_map_node_item)),
	//					sizeof(PMEMoid), &node_p->items[node_p->n])
	
	/* copy right sibling data to node */
	memcpy(&node_p->items[node_p->n], rn_p->items,
	sizeof(struct tree_map_node_item) * rn_p->n);
	memcpy(&node_p->slots[node_p->n], rn_p->slots,
	sizeof(PMEMoid) * (rn_p->n + 1));

	//D_RW(node)->n += D_RO(rn)->n;
	node_p->n += rn_p->n;

	sobj_tx_free(rn); /* right node is now empty */
	//TX_FREE(rn); /* right node is now empty */
	//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
	//memcpy(node_p_mod, node_p, sizeof(struct tree_map_node));
	////sobj_tx_write(pop, node, node_p);

	//TX_ADD(parent);
	sobj_tx_add_range(parent, 0, sizeof(struct tree_map_node));
	parent_p->n -= 1;

	/* move everything to the right of the separator by one array slot */
	memmove(parent_p->items + p, parent_p->items + p + 1,
	sizeof(struct tree_map_node_item) * (parent_p->n - p));

	memmove(parent_p->slots + p + 1, parent_p->slots + p + 2,
	sizeof(PMEMoid) * (parent_p->n - p + 1));

	//struct tree_map_node *parent_p_mod = sobj_direct(pop, parent);
	//memcpy(parent_p_mod, parent_p, sizeof(struct tree_map_node));
	////sobj_tx_write(pop, parent, parent_p);

	/* if the parent is empty then the tree shrinks in height */
	struct btree_map *btree_map = sobj_read(pop, map, 1, NULL);
	if (parent_p->n == 0 && OID_EQUALS(parent, btree_map->root)) {
		//TX_ADD(map);
		sobj_tx_add_range(map, 0, sizeof(struct btree_map));
		sobj_tx_free(btree_map->root);
		struct btree_map *btree_map_mod = sobj_direct(pop, map);
		btree_map_mod->root = node;
		////sobj_tx_write(pop, map, &node);
		//TX_FREE(D_RO(map)->root);
		//D_RW(map)->root = node;
	}

	free(btree_map);
	//free(parent_p);
	//free(node_p);
	free(rn_p);
}

/*
 * btree_map_rebalance -- (internal) performs tree rebalance
 */
static void
btree_map_rebalance(PMEMobjpool *pop, PMEMoid map, PMEMoid node,
	PMEMoid parent, int p)
{
	
	struct tree_map_node *parent_p = sobj_read(pop, parent, 1, NULL);

	PMEMoid rsb = p >= parent_p->n ? OID_NULL : parent_p->slots[p + 1];
	PMEMoid lsb = p == 0 ? OID_NULL : parent_p->slots[p - 1];
	free(parent_p);

	if (!OID_IS_NULL(rsb)) {
		struct tree_map_node *rsb_p = sobj_read(pop, rsb, 1, NULL);
		if (rsb_p->n > BTREE_MIN) {
			btree_map_rotate_right(pop, rsb, node, parent, p);
			free(rsb_p);
			return;
		}
		free(rsb_p);
	}
	
	if (!OID_IS_NULL(lsb)) {
		struct tree_map_node *lsb_p = sobj_read(pop, lsb, 1, NULL);
		if (lsb_p->n > BTREE_MIN) {
			btree_map_rotate_left(pop, lsb, node, parent, p);
			free(lsb_p);
			return;
		}
		free(lsb_p);		
	}		
	
	if (OID_IS_NULL(rsb)) /* always merge with rightmost node */
		btree_map_merge(pop, map, node, lsb, parent, p - 1);
	else
		btree_map_merge(pop, map, rsb, node, parent, p);
}

/*
 * btree_map_get_leftmost_leaf -- (internal) searches for the successor
 */
static PMEMoid
btree_map_get_leftmost_leaf(PMEMobjpool *pop, PMEMoid map,
	PMEMoid n, PMEMoid *p)
{
	
	struct tree_map_node *n_p = sobj_read(pop, n, 1, NULL);
	if (OID_IS_NULL(n_p->slots[0])) {
		free(n_p);
		return n;
	}

	*p = n;
	PMEMoid ret = btree_map_get_leftmost_leaf(pop, map, n_p->slots[0], p);
	free(n_p);
	return ret;
}

/*
 * btree_map_remove_from_node -- (internal) removes element from node
 */
static void
btree_map_remove_from_node(PMEMobjpool* pop, PMEMoid map,
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
		//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
		//memcpy(node_p_mod, node_p, sizeof(struct tree_map_node));
		////sobj_tx_write(pop, node, node_p);
		//free(node_p);
		return;
	}

	/* can't delete from non-leaf nodes, remove successor */
	PMEMoid rchild = node_p->slots[p + 1];
	PMEMoid lp = node;
	PMEMoid lm = btree_map_get_leftmost_leaf(pop, map, rchild, &lp);
	//free(node_p);

	//TX_ADD_FIELD(node, items[p]);
	//D_RW(node)->items[p] = D_RO(lm)->items[0];
	sobj_tx_add_range(node, offsetof(struct tree_map_node, items) +
						p * sizeof(struct tree_map_node_item), 
						sizeof(struct tree_map_node_item));
	struct tree_map_node *lm_p = sobj_read(pop, lm, 1, NULL);
	node_p->items[p] = lm_p->items[0];
	//struct tree_map_node *node_p_mod = sobj_direct(pop, node);
	//node_p_mod->items[p] = lm_p->items[0];
	
	////sobj_tx_write_part(pop, node, offsetof(struct tree_map_node, items) +
	////					p * sizeof(struct tree_map_node_item), 
	////					sizeof(struct tree_map_node_item), &lm_p->items[0]);
	free(lm_p);

	btree_map_remove_from_node(pop, map, lm, lp, 0);
	lm_p = sobj_read(pop, lm, 1, NULL); //re-read because it might have been changed 

	if (lm_p->n < BTREE_MIN) /* right child can be deficient now */
		btree_map_rebalance(pop, map, lm, lp,
			OID_EQUALS(lp, node) ? p + 1 : 0);
	free(lm_p);
}

/*
 * btree_map_remove_item -- (internal) removes item from node
 */
static PMEMoid
btree_map_remove_item(PMEMobjpool *pop, PMEMoid map,
	PMEMoid node, PMEMoid parent, uint64_t key, int p)
{
	
	PMEMoid ret = OID_NULL;
	struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
	for (int i = 0; i <= node_p->n; ++i) {
		if (ANCHOR_NODE_CONTAINS_ITEM(node_p, i, key)) {
			ret = node_p->items[i].value;
			btree_map_remove_from_node(pop, map, node, parent, i);
			break;
		} else if (ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(node_p, i, key)) {
			ret = btree_map_remove_item(pop, map, node_p->slots[i],
				node, key, i);
			break;
		}
	}
	/* node might have been changed here */
	free(node_p);

	if (!OID_IS_NULL(parent)){
		node_p = sobj_read(pop, node, 1, NULL);
		/* check for deficient nodes walking up */
		if (node_p->n < BTREE_MIN)
			btree_map_rebalance(pop, map, node, parent, p);

		free(node_p);
	}
	return ret;
}

/*
 * btree_map_remove -- removes key-value pair from the map
 */
PMEMoid
btree_map_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid ret = OID_NULL;

	struct btree_map *btree_map = sobj_read(pop, map, 1, NULL);
	
	TX_BEGIN(pop) {
		ret = btree_map_remove_item(pop, map, btree_map->root,
				OID_NULL, key, 0);
	} TX_END

	free(btree_map);

	return ret;
}

/*
 * btree_map_get_in_node -- (internal) searches for a value in the node
 */
static PMEMoid
btree_map_get_in_node(PMEMobjpool *pop, PMEMoid node, uint64_t key)
{
	
	struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
	PMEMoid ret = OID_NULL;
	for (int i = 0; i <= node_p->n; ++i) {
		if (ANCHOR_NODE_CONTAINS_ITEM(node_p, i, key)) {
			ret = node_p->items[i].value;
			free(node_p);
			return ret;
		}
		else if (ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(node_p, i, key)) {
			ret = btree_map_get_in_node(pop, node_p->slots[i], key);
			free(node_p);
			return ret;
		}
	}
	free(node_p);
	return ret;
}

/*
 * btree_map_get -- searches for a value of the key
 */
PMEMoid
btree_map_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{	
	
	struct btree_map *btree_map = sobj_read(pop, map, 1, NULL);
	if (OID_IS_NULL(btree_map->root)) {
		free(btree_map);
		return OID_NULL;
	}
	PMEMoid ret = btree_map_get_in_node(pop, btree_map->root, key);
	free(btree_map);
	return ret;
}

/*
 * btree_map_lookup_in_node -- (internal) searches for key if exists
 */
static int
btree_map_lookup_in_node(PMEMobjpool *pop, PMEMoid node, uint64_t key)
{
	
	struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
	for (int i = 0; i <= node_p->n; ++i) {
		if (ANCHOR_NODE_CONTAINS_ITEM(node_p, i, key)) {
			free(node_p);
			return 1;
		}
		else if (ANCHOR_NODE_CHILD_CAN_CONTAIN_ITEM(node_p, i, key)) {
			int ret = btree_map_lookup_in_node(pop,
					node_p->slots[i], key);
			free(node_p);
			return ret;
		}
	}
	free(node_p);
	return 0;
}

/*
 * btree_map_lookup -- searches if key exists
 */
int
btree_map_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	
	struct btree_map *btree_map = sobj_read(pop, map, 1, NULL);
	if (OID_IS_NULL(btree_map->root)) {
		free(btree_map);
		return 0;
	}
	int ret = btree_map_lookup_in_node(pop, btree_map->root, key);
	free(btree_map);
	return ret;
}

/*
 * btree_map_foreach_node -- (internal) recursively traverses tree
 */
static int
btree_map_foreach_node(PMEMobjpool *pop, const PMEMoid p,
	int (*cb)(uint64_t key, PMEMoid, void *arg), void *arg)
{
	
	if (OID_IS_NULL(p))
		return 0;

	struct tree_map_node *p_p = sobj_read(pop, p, 1, NULL);

	for (int i = 0; i <= p_p->n; ++i) {
		if (btree_map_foreach_node(pop, p_p->slots[i], cb, arg) != 0) {
			free(p_p);
			return 1;
		}
		if (i != p_p->n && p_p->items[i].key != 0) {
			if (cb(p_p->items[i].key, p_p->items[i].value,
					arg) != 0){
				free(p_p);
				return 1;
			}
		}
	}
	free(p_p);
	return 0;
}

/*
 * btree_map_foreach -- initiates recursive traversal
 */
int
btree_map_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	
	struct btree_map *btree_map = sobj_read(pop, map, 1, NULL);
	int ret = btree_map_foreach_node(pop, btree_map->root, cb, arg);
	free(btree_map);
	return ret;
}

/*
 * ctree_map_check -- check if given persistent object is a tree map
 */
int
btree_map_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
	//return TOID_IS_NULL(map) || !TOID_VALID(map);
}

/*
 * btree_map_insert_new -- allocates a new object and inserts it into the tree
 */
int
btree_map_insert_new(PMEMobjpool *pop, PMEMoid map,
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
		btree_map_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * btree_map_remove_free -- removes and frees an object from the tree
 */
int
btree_map_remove_free(PMEMobjpool *pop, PMEMoid map,
		uint64_t key)
{
	
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid val = btree_map_remove(pop, map, key);
		sobj_tx_free(val);
		//pmemobj_tx_free(val);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}
