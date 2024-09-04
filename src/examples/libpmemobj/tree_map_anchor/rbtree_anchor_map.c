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
 * rbtree.c -- red-black tree implementation /w sentinel nodes
 */

#include <assert.h>
#include <errno.h>
#include "rbtree_anchor_map.h"

TOID_DECLARE(struct tree_map_node, RBTREE_MAP_TYPE_OFFSET + 1);

#define NODE_P(_n)\
D_RW(_n)->parent

#define NODE_GRANDP(_n)\
NODE_P(NODE_P(_n))

#define NODE_PARENT_AT(_n, _rbc)\
D_RW(NODE_P(_n))->slots[_rbc]

#define NODE_PARENT_RIGHT(_n)\
NODE_PARENT_AT(_n, RB_RIGHT)

#define NODE_IS(_n, _rbc)\
TOID_EQUALS(_n, NODE_PARENT_AT(_n, _rbc))

#define NODE_IS_RIGHT(_n)\
TOID_EQUALS(_n, NODE_PARENT_RIGHT(_n))

#define NODE_LOCATION(_n)\
NODE_IS_RIGHT(_n)

#define RB_FIRST(_m)\
D_RW(D_RW(_m)->root)->slots[RB_LEFT]

#define NODE_IS_NULL(_n)\
TOID_EQUALS(_n, s)

enum rb_color {
	COLOR_BLACK,
	COLOR_RED,

	MAX_COLOR
};

enum rb_children {
	RB_LEFT,
	RB_RIGHT,

	MAX_RB
};

struct tree_map_node {
	uint64_t key;
	PMEMoid value;
	enum rb_color color;
	//TOID(struct tree_map_node) parent;
	//TOID(struct tree_map_node) slots[MAX_RB];
	PMEMoid parent;
	PMEMoid slots[MAX_RB];
};

struct rbtree_map {
	//TOID(struct tree_map_node) sentinel;
	//TOID(struct tree_map_node) root;
	PMEMoid sentinel;
	PMEMoid root;
};

static inline PMEMoid anchor_node_p(PMEMobjpool *pop, PMEMoid n) 
{
	struct tree_map_node *n_p = sobj_read(pop, n, 1, NULL);
	PMEMoid ret = n_p->parent;
	free(n_p);
	return ret;
}

static inline PMEMoid anchor_node_grandp(PMEMobjpool *pop, PMEMoid n) 
{
	return anchor_node_p(pop, anchor_node_p(pop, n));
}

static inline PMEMoid anchor_node_parent_at(PMEMobjpool *pop, PMEMoid n,
											enum rb_children idx) 
{
	struct tree_map_node *n_p = sobj_read(pop, anchor_node_p(pop, n), 1, NULL);
	PMEMoid ret = n_p->slots[idx];
	free(n_p);
	return ret;
}

static inline PMEMoid anchor_node_parent_right(PMEMobjpool *pop, PMEMoid n) 
{
	return anchor_node_parent_at(pop, n, RB_RIGHT);
}

static inline int anchor_node_is(PMEMobjpool *pop, PMEMoid n, enum rb_children idx) 
{
	return OID_EQUALS(n, anchor_node_parent_at(pop, n, idx));
}

static inline int anchor_node_is_right(PMEMobjpool *pop, PMEMoid n) 
{
	return OID_EQUALS(n, anchor_node_parent_right(pop, n));
}

static inline int anchor_node_location(PMEMobjpool *pop, PMEMoid n) 
{
	return anchor_node_is_right(pop, n);
}

static inline PMEMoid anchor_rb_first(PMEMobjpool *pop, PMEMoid map) 
{
	struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
	struct tree_map_node *root_p = sobj_read(pop, rbtree_map->root, 1, NULL);
	PMEMoid ret = root_p->slots[RB_LEFT];
	free(rbtree_map);
	free(root_p);
	return ret;
}

static inline int anchor_node_is_null(PMEMoid n, PMEMoid s) 
{
	return OID_EQUALS(n, s);
}


/*
 * rbtree_map_create -- allocates a new red-black tree instance
 */
int
rbtree_map_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	int ret = 0;
	TX_BEGIN(pop) {
		//pmemobj_tx_add_range_direct(map, sizeof(*map));

		*map = sobj_tx_zalloc(sizeof(struct rbtree_map), RBTREE_MAP_TYPE_OFFSET);
		PMEMoid s = sobj_tx_zalloc(sizeof(struct tree_map_node), RBTREE_MAP_TYPE_OFFSET + 1);
		struct tree_map_node s_vol = {0};
		s_vol.color = COLOR_BLACK;
		s_vol.parent = s;
		s_vol.slots[RB_LEFT] = s;
		s_vol.slots[RB_RIGHT] = s;
		sobj_tx_write(pop, s, &s_vol);

		PMEMoid r = sobj_tx_zalloc(sizeof(struct tree_map_node), RBTREE_MAP_TYPE_OFFSET + 1);
		struct tree_map_node r_vol = {0};
		r_vol.color = COLOR_BLACK;
		r_vol.parent = s;
		r_vol.slots[RB_LEFT] = s;
		r_vol.slots[RB_RIGHT] = s;
		sobj_tx_write(pop, r, &r_vol);

		struct rbtree_map rbtree_map_vol = {0};
		rbtree_map_vol.sentinel = s;
		rbtree_map_vol.root = r;
		sobj_tx_write(pop, *map, &rbtree_map_vol);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rbtree_map_clear_node -- (internal) clears this node and its children
 */
static void
rbtree_map_clear_node(PMEMobjpool *pop, PMEMoid map, PMEMoid p)
{
	struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
	PMEMoid s = rbtree_map->sentinel;
	free(rbtree_map);

	struct tree_map_node *p_p = sobj_read(pop, p, 1, NULL);
	if (!anchor_node_is_null(p_p->slots[RB_LEFT], s))
		rbtree_map_clear_node(pop, map, p_p->slots[RB_LEFT]);

	if (!anchor_node_is_null(p_p->slots[RB_RIGHT], s))
		rbtree_map_clear_node(pop, map, p_p->slots[RB_RIGHT]);

	free(p_p);
	sobj_tx_free(p);
}

/*
 * rbtree_map_clear -- removes all elements from the map
 */
int
rbtree_map_clear(PMEMobjpool *pop, PMEMoid map)
{
	TX_BEGIN(pop) {
		struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
		rbtree_map_clear_node(pop, map, rbtree_map->root);
		sobj_tx_add_range(map, 0, sizeof(struct rbtree_map));
		//TX_ADD_FIELD(map, root);
		//TX_ADD_FIELD(map, sentinel);

		sobj_tx_free(rbtree_map->sentinel);

		rbtree_map->sentinel = OID_NULL;
		rbtree_map->root = OID_NULL;
		sobj_tx_write(pop, map, rbtree_map);
		free(rbtree_map);
	} TX_END

	return 0;
}

/*
 * rbtree_map_destroy -- cleanups and frees red-black tree instance
 */
int
rbtree_map_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	int ret = 0;
	TX_BEGIN(pop) {
		rbtree_map_clear(pop, *map);
		//TX_ADD_DIRECT(map);
		//TX_FREE(*map);
		//*map = TOID_NULL(struct rtree_map);
		sobj_tx_free(*map);
		*map = OID_NULL;

		//rbtree_map_clear(pop, *map);
		//pmemobj_tx_add_range_direct(map, sizeof(*map));
		//TX_FREE(*map);
		//*map = TOID_NULL(struct rbtree_map);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rbtree_map_rotate -- (internal) performs a left/right rotation around a node
 */
static void
rbtree_map_rotate(PMEMobjpool *pop, PMEMoid map,
	PMEMoid node, enum rb_children c)
{
	struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
	struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
	PMEMoid child = node_p->slots[!c];
	PMEMoid s = rbtree_map->sentinel;

	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	sobj_tx_add_range(child, 0, sizeof(struct tree_map_node));

	struct tree_map_node *child_p = sobj_read(pop, child, 1, NULL);

	node_p->slots[!c] = child_p->slots[c];

	if (!OID_EQUALS(child_p->slots[c], s)) {
		sobj_tx_add_range(child_p->slots[c], offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
		sobj_tx_write_part(pop, child_p->slots[c], offsetof(struct tree_map_node, parent), 
							sizeof(PMEMoid), &node);		
		//TX_SET(D_RW(child)->slots[c], parent, node);
	}
	
	child_p->parent = anchor_node_p(pop, node);
	//NODE_P(child) = NODE_P(node);

	//TX_SET(NODE_P(node), slots[NODE_LOCATION(node)], child);
	PMEMoid mod_node = anchor_node_p(pop, node);
	enum rb_children mod_idx = anchor_node_location(pop, node);
	sobj_tx_add_range(mod_node, offsetof(struct tree_map_node, slots) + mod_idx * sizeof(PMEMoid),
						sizeof(PMEMoid));
	sobj_tx_write_part(pop, mod_node, offsetof(struct tree_map_node, slots) + mod_idx * sizeof(PMEMoid),
						sizeof(PMEMoid), &child);

	child_p->slots[c] = node;
	node_p->parent = child;

	sobj_tx_write(pop, child, child_p);
	sobj_tx_write(pop, node, node_p);
	free(child_p);
	free(node_p);
	free(rbtree_map);
}

/*
 * rbtree_map_insert_bst -- (internal) inserts a node in regular BST fashion
 */
static void
rbtree_map_insert_bst(PMEMobjpool *pop, PMEMoid map, PMEMoid n)
{
	struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
	PMEMoid parent = rbtree_map->root;
	//PMEMoid *dst = &RB_FIRST(map);
	PMEMoid my_dst = anchor_rb_first(pop, map);
	enum rb_children my_dst_idx = RB_LEFT; //RB LEFT child of the parent
	PMEMoid s = rbtree_map->sentinel;

	struct tree_map_node *n_p = sobj_read(pop, n, 1, NULL);
	n_p->slots[RB_LEFT] = s;
	n_p->slots[RB_RIGHT] = s;
	sobj_tx_write_part(pop, n, offsetof(struct tree_map_node, slots), sizeof(n_p->slots), n_p->slots);
	//D_RW(n)->slots[RB_LEFT] = s;
	//D_RW(n)->slots[RB_RIGHT] = s;

	struct tree_map_node *temp;
	
	while (!anchor_node_is_null(my_dst, s)) {
		parent = my_dst;
		temp = sobj_read(pop, my_dst, 1, NULL);
		my_dst_idx = (enum rb_children)(n_p->key > temp->key);
		my_dst = temp->slots[my_dst_idx];
		free(temp);
		//dst = &D_RW(*dst)->slots[D_RO(n)->key > D_RO(*dst)->key];
	}
	/*
	while (!NODE_IS_NULL(*dst)) {
		parent = *dst;
		dst = &D_RW(*dst)->slots[D_RO(n)->key > D_RO(*dst)->key];
	}
	*/

	sobj_tx_add_range(n, offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
	//sobj_tx_add_range(n, 0, sizeof(struct tree_map_node)); //snapshot the whole object 
	n_p->parent = parent;
	//sobj_tx_write(pop, n, n_p);
	sobj_tx_write_part(pop, n, offsetof(struct tree_map_node, parent), sizeof(PMEMoid), &parent);
	//TX_SET(n, parent, parent);

	/*
	 *	parent points to the object that *dst in original version belongs to and
	 * 	my_dst_idx shows the appropriate slot to update
	 */
	sobj_tx_add_range(parent, offsetof(struct tree_map_node, slots) + my_dst_idx * sizeof(PMEMoid), sizeof(PMEMoid));
	sobj_tx_write_part(pop, parent, offsetof(struct tree_map_node, slots) + my_dst_idx * sizeof(PMEMoid),
						sizeof(PMEMoid), &n);
	//pmemobj_tx_add_range_direct(dst, sizeof(*dst));
	//*dst = n;

	free(n_p);
	free(rbtree_map);
}

/*
 * rbtree_map_recolor -- (internal) restores red-black tree properties
 */
static PMEMoid
rbtree_map_recolor(PMEMobjpool *pop, PMEMoid map,
	PMEMoid n, enum rb_children c)
{
	struct tree_map_node *temp = sobj_read(pop, anchor_node_grandp(pop, n), 1, NULL);
	PMEMoid uncle = temp->slots[!c];
	free(temp);
	//PMEMoid uncle = D_RO(NODE_GRANDP(n))->slots[!c];
	struct tree_map_node *uncle_p = sobj_read(pop, uncle, 1, NULL);
	enum rb_color black = COLOR_BLACK;
	enum rb_color red = COLOR_RED;

	if (uncle_p->color == COLOR_RED) {
		free(uncle_p);
		sobj_tx_add_range(uncle, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
		sobj_tx_write_part(pop, uncle, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &black);
		
		PMEMoid mod = anchor_node_p(pop, n);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
		sobj_tx_write_part(pop, mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &black);

		mod = anchor_node_grandp(pop, n);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
		sobj_tx_write_part(pop, mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &red);
		return mod;

	} else {
		if (anchor_node_is(pop, n, !c)) {
			n = anchor_node_p(pop, n);
			rbtree_map_rotate(pop, map, n, c);
		}
		PMEMoid mod = anchor_node_p(pop, n);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
		sobj_tx_write_part(pop, mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &black);
		mod = anchor_node_grandp(pop, n);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
		sobj_tx_write_part(pop, mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &red);	
		rbtree_map_rotate(pop, map, mod, (enum rb_children)!c);
	}

	free(uncle_p);
	return n;
}

/*
 * rbtree_map_find_node -- (internal) returns the node that contains the key
 */
static PMEMoid
rbtree_map_find_node(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid dst = anchor_rb_first(pop, map);
	struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
	PMEMoid s = rbtree_map->sentinel;
	free(rbtree_map);

	while (!anchor_node_is_null(dst, s)) {
		struct tree_map_node *dst_p = sobj_read(pop, dst, 1, NULL);
		if (dst_p->key == key){
			free(dst_p);
			return dst;
		}

		dst = dst_p->slots[key > dst_p->key];
		free(dst_p);
	}

	return OID_NULL;
}

/*
 * rbtree_map_update_check -- searches for a value of the key
 */
static int
rbtree_map_update_check(PMEMobjpool *pop, PMEMoid map, uint64_t key, PMEMoid value)
{
	PMEMoid node = rbtree_map_find_node(pop, map, key);
	if (OID_IS_NULL(node))
		return 0;

	struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
	if (!OID_EQUALS(node_p->value, value)) {
		TX_BEGIN(pop) {	
			sobj_tx_add_range(node, offsetof(struct tree_map_node, value), sizeof(PMEMoid));
			sobj_tx_free(node_p->value);
			node_p->value = value;
			sobj_tx_write_part(pop, node, offsetof(struct tree_map_node, value),
								sizeof(PMEMoid), &node_p->value);
		} TX_END
	}
	free(node_p);
	return 1;
}

/*
 * rbtree_map_insert -- inserts a new key-value pair into the map
 */
int
rbtree_map_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value)
{
	//first check if the value exists in order to update it
	if (rbtree_map_update_check(pop, map, key, value)) {
		return 0;
	}

	int ret = 0;

	TX_BEGIN(pop) {
		//TOID(struct tree_map_node) n = TX_ZNEW(struct tree_map_node);
		PMEMoid n = sobj_tx_zalloc(sizeof(struct tree_map_node), RBTREE_MAP_TYPE_OFFSET + 1);
		struct tree_map_node new_node = {0};
		new_node.key = key;
		new_node.value = value;
		//D_RW(n)->key = key;
		//D_RW(n)->value = value;
		sobj_tx_write(pop, n, &new_node);
		
		rbtree_map_insert_bst(pop, map, n);

		new_node.color = COLOR_RED;
		//D_RW(n)->color = COLOR_RED;
		sobj_tx_write_part(pop, n, offsetof(struct tree_map_node, color),
							sizeof(enum rb_color), &new_node.color);

		struct tree_map_node *temp = sobj_read(pop, anchor_node_p(pop, n), 1, NULL);
		while (temp->color == COLOR_RED) {
			n = rbtree_map_recolor(pop, map, n, (enum rb_children)
					anchor_node_location(pop, (anchor_node_p(pop, n))));
			free(temp);
			temp = sobj_read(pop, anchor_node_p(pop, n), 1, NULL);
		}
		free(temp);
		
		//TX_SET(RB_FIRST(map), color, COLOR_BLACK);
		sobj_tx_add_range(anchor_rb_first(pop, map), offsetof(struct tree_map_node, color), sizeof(enum rb_color));
		enum rb_color color = COLOR_BLACK;
		sobj_tx_write_part(pop, anchor_rb_first(pop, map), offsetof(struct tree_map_node, color),
							sizeof(enum rb_color), &color);
	} TX_END

	return ret;
}

/*
 * rbtree_map_successor -- (internal) returns the successor of a node
 */
static PMEMoid
rbtree_map_successor(PMEMobjpool *pop, PMEMoid map, PMEMoid n)
{
	struct tree_map_node *n_p = sobj_read(pop, n, 1, NULL);
	PMEMoid dst = n_p->slots[RB_RIGHT];
	struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
	PMEMoid s = rbtree_map->sentinel;

	struct tree_map_node *dst_p;
	if (!OID_EQUALS(s, dst)) {
		dst_p = sobj_read(pop, dst, 1, NULL);
		while (!anchor_node_is_null(dst_p->slots[RB_LEFT], s)) {
			//dst = D_RO(dst)->slots[RB_LEFT];
			dst = dst_p->slots[RB_LEFT];
			free(dst_p);
			dst_p = sobj_read(pop, dst, 1, NULL);
		}
	} else {
		dst = n_p->parent;
		dst_p = sobj_read(pop, dst, 1, NULL);
		while (OID_EQUALS(n, dst_p->slots[RB_RIGHT])) {
			n = dst;
			dst = anchor_node_p(pop, dst);
			free(dst_p);
			dst_p = sobj_read(pop, dst, 1, NULL);
		}
		if (OID_EQUALS(dst, rbtree_map->root)) {
			free(n_p);
			free(dst_p);
			free(rbtree_map);
			return s;
		}

	}

	free(n_p);
	free(dst_p);
	free(rbtree_map);
	return dst;
}

/*
 * rbtree_map_repair_branch -- (internal) restores red-black tree in one branch
 */
static PMEMoid
rbtree_map_repair_branch(PMEMobjpool *pop, PMEMoid map,
	PMEMoid n, enum rb_children c)
{
	PMEMoid sb = anchor_node_parent_at(pop, n, !c); /* sibling */
	struct tree_map_node *sb_p = sobj_read(pop, sb, 1, NULL);
	enum rb_color black = COLOR_BLACK;
	enum rb_color red = COLOR_RED;

	if (sb_p->color == COLOR_RED) {
		//TX_SET(sb, color, COLOR_BLACK);
		sb_p->color = COLOR_BLACK;
		sobj_tx_add_range(sb, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
		sobj_tx_write_part(pop, sb, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &sb_p->color);

		PMEMoid mod = anchor_node_p(pop, n);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
		sobj_tx_write_part(pop, mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &red);

		rbtree_map_rotate(pop, map, mod, c);
		sb = anchor_node_parent_at(pop, n, !c);
		free(sb_p);
		sb_p = sobj_read(pop, sb, 1, NULL);
	}
	
	struct tree_map_node *slots[MAX_RB];
	slots[RB_RIGHT] = sobj_read(pop, sb_p->slots[RB_RIGHT], 1, NULL);
	slots[RB_LEFT] = sobj_read(pop, sb_p->slots[RB_LEFT], 1, NULL);

	if (slots[RB_RIGHT]->color == COLOR_BLACK &&
		slots[RB_LEFT]->color == COLOR_BLACK) {
		sobj_tx_add_range(sb, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
		sobj_tx_write_part(pop, sb, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &red);	
		free(slots[RB_RIGHT]);
		free(slots[RB_LEFT]);
		free(sb_p);
		struct tree_map_node *n_p = sobj_read(pop, n, 1, NULL);
		PMEMoid ret = n_p->parent;
		free(n_p);
		return ret;
	} else {
		if (slots[!c]->color == COLOR_BLACK) {
			sobj_tx_add_range(sb_p->slots[c], offsetof(struct tree_map_node, color), sizeof(enum rb_color));
			sobj_tx_write_part(pop, sb_p->slots[c], offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &black);
			sobj_tx_add_range(sb, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
			sobj_tx_write_part(pop, sb, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &red);
			rbtree_map_rotate(pop, map, sb, (enum rb_children)!c);
			sb = anchor_node_parent_at(pop, n, !c);
			free(sb_p);
			sb_p = sobj_read(pop, sb, 1, NULL);
		}
		PMEMoid node_p_n = anchor_node_p(pop, n);
		struct tree_map_node *node_p_n_p = sobj_read(pop, node_p_n, 1, NULL);
		sobj_tx_add_range(sb, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
		sobj_tx_write_part(pop, sb, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &node_p_n_p->color);
		sobj_tx_add_range(node_p_n, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
		sobj_tx_write_part(pop, node_p_n, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &black);
		sobj_tx_add_range(sb_p->slots[!c], offsetof(struct tree_map_node, color), sizeof(enum rb_color));
		sobj_tx_write_part(pop, sb_p->slots[!c], offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color), &black);
		rbtree_map_rotate(pop, map, node_p_n, c);

		free(slots[RB_RIGHT]);
		free(slots[RB_LEFT]);
		free(sb_p);
		free(node_p_n_p);
		return anchor_rb_first(pop, map);
	}

	free(sb_p);
	free(slots[RB_RIGHT]);
	free(slots[RB_LEFT]);
	return n;
}

/*
 * rbtree_map_repair -- (internal) restores red-black tree properties
 * after remove
 */
static void
rbtree_map_repair(PMEMobjpool *pop, PMEMoid map, PMEMoid n)
{
	/* if left, repair right sibling, otherwise repair left sibling. */
	struct tree_map_node *n_p = sobj_read(pop, n, 1, NULL);
	while (!OID_EQUALS(n, anchor_rb_first(pop, map)) && n_p->color == COLOR_BLACK) {
		n = rbtree_map_repair_branch(pop, map, n, (enum rb_children)
				anchor_node_location(pop, n));
		free(n_p);
		n_p = sobj_read(pop, n, 1, NULL);
	}
	
	n_p->color = COLOR_BLACK;
	sobj_tx_add_range(n, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
	sobj_tx_write_part(pop, n, offsetof(struct tree_map_node, color), sizeof(enum rb_color), &n_p->color);
	//sobj_tx_write(pop, n, n_p);
	free(n_p);
	//TX_SET(n, color, COLOR_BLACK);
}

/*
 * rbtree_map_remove -- removes key-value pair from the map
 */
PMEMoid
rbtree_map_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid ret = OID_NULL;

	PMEMoid n = rbtree_map_find_node(pop, map, key);
	if (OID_IS_NULL(n))
		return ret;

	struct tree_map_node *n_p = sobj_read(pop, n, 1, NULL);
	ret = n_p->value;
	free(n_p);

	struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
	PMEMoid s = rbtree_map->sentinel;
	PMEMoid r = rbtree_map->root;
	free(rbtree_map);

	PMEMoid y = (anchor_node_is_null(n_p->slots[RB_LEFT], s) ||
					anchor_node_is_null(n_p->slots[RB_RIGHT], s))
					? n : rbtree_map_successor(pop, map, n);

	struct tree_map_node *y_p = sobj_read(pop, y, 1, NULL);
	PMEMoid x = anchor_node_is_null(y_p->slots[RB_LEFT], s) ?
			y_p->slots[RB_RIGHT] : y_p->slots[RB_LEFT];
	free(y_p);

	TX_BEGIN(pop) {
		PMEMoid node_p_y = anchor_node_p(pop, y);
		sobj_tx_add_range(x, offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
		sobj_tx_write_part(pop, x, offsetof(struct tree_map_node, parent), 
							sizeof(PMEMoid), &node_p_y);
		//TX_SET(x, parent, NODE_P(y));
		
		PMEMoid node_p_x = anchor_node_p(pop, x);
		if (OID_EQUALS(node_p_x, r)) {
			sobj_tx_add_range(r, offsetof(struct tree_map_node, slots) + RB_LEFT * sizeof(PMEMoid),
								 sizeof(PMEMoid));
			sobj_tx_write_part(pop, r, offsetof(struct tree_map_node, slots) + RB_LEFT * sizeof(PMEMoid), 
								sizeof(PMEMoid), &x);
			//TX_SET(r, slots[RB_LEFT], x);
		} else {
			enum rb_children idx = anchor_node_location(pop, y);
			sobj_tx_add_range(node_p_y, offsetof(struct tree_map_node, slots) + idx * sizeof(PMEMoid),
								 sizeof(PMEMoid));
			sobj_tx_write_part(pop, node_p_y, offsetof(struct tree_map_node, slots) + idx * sizeof(PMEMoid), 
								sizeof(PMEMoid), &x);
			//TX_SET(NODE_P(y), slots[NODE_LOCATION(y)], x);
		}

		y_p = sobj_read(pop, y, 1, NULL); //might have been overwritten
		if (y_p->color == COLOR_BLACK)
			rbtree_map_repair(pop, map, x);

		if (!OID_EQUALS(y, n)) {
			sobj_tx_add_range(y, 0, sizeof(struct tree_map_node));
			n_p = sobj_read(pop, n, 1, NULL); //might have been overwritten
			y_p->slots[RB_LEFT] = n_p->slots[RB_LEFT];
			y_p->slots[RB_RIGHT] = n_p->slots[RB_RIGHT];
			y_p->parent = n_p->parent;
			y_p->color = n_p->color;
			sobj_tx_write(pop, y, y_p);

			//TX_SET(D_RW(n)->slots[RB_LEFT], parent, y);
			sobj_tx_add_range(n_p->slots[RB_LEFT], offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
			sobj_tx_write_part(pop, n_p->slots[RB_LEFT], offsetof(struct tree_map_node, parent), 
								sizeof(PMEMoid), &y);
			//TX_SET(D_RW(n)->slots[RB_RIGHT], parent, y);
			sobj_tx_add_range(n_p->slots[RB_RIGHT], offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
			sobj_tx_write_part(pop, n_p->slots[RB_RIGHT], offsetof(struct tree_map_node, parent), 
								sizeof(PMEMoid), &y);
			//TX_SET(NODE_P(n), slots[NODE_LOCATION(n)], y);
			PMEMoid node_p_n = anchor_node_p(pop, n);
			enum rb_children idx = anchor_node_location(pop, n);
			sobj_tx_add_range(node_p_n, offsetof(struct tree_map_node, slots) + idx * sizeof(PMEMoid),
								 sizeof(PMEMoid));
			sobj_tx_write_part(pop, node_p_n, offsetof(struct tree_map_node, slots) + idx * sizeof(PMEMoid), 
								sizeof(PMEMoid), &y);
			free(n_p);
		}
		free(y_p);
		sobj_tx_free(n);
	} TX_END

	
	return ret;
}

/*
 * rbtree_map_get -- searches for a value of the key
 */
PMEMoid
rbtree_map_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid node = rbtree_map_find_node(pop, map, key);
	if (OID_IS_NULL(node))
		return OID_NULL;

	struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
	PMEMoid ret = node_p->value;
	free(node_p);
	return ret;
	//return D_RO(node)->value;
}

/*
 * rbtree_map_lookup -- searches if key exists
 */
int
rbtree_map_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid node = rbtree_map_find_node(pop, map, key);
	if (OID_IS_NULL(node))
		return 0;

	return 1;
}

/*
 * rbtree_map_foreach_node -- (internal) recursively traverses tree
 */
static int
rbtree_map_foreach_node(PMEMobjpool *pop, PMEMoid map,
	PMEMoid p,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	int ret = 0;

	struct rbtree_map *rbtree_map = sobj_read(pop, map, 1, NULL);
	if (OID_EQUALS(p, rbtree_map->sentinel)){
		free(rbtree_map);
		return 0;
	}
	free(rbtree_map);

	struct tree_map_node *p_p = sobj_read(pop, p, 1, NULL);
	if ((ret = rbtree_map_foreach_node(pop, map,
		p_p->slots[RB_LEFT], cb, arg)) == 0) {
		if ((ret = cb(p_p->key, p_p->value, arg)) == 0)
			rbtree_map_foreach_node(pop, map,
				p_p->slots[RB_RIGHT], cb, arg);
	}
	free(p_p);
	return ret;
}

/*
 * rbtree_map_foreach -- initiates recursive traversal
 */
int
rbtree_map_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	return rbtree_map_foreach_node(pop, map, anchor_rb_first(pop, map), cb, arg);
}

/*
 * rbtree_map_is_empty -- checks whether the tree map is empty
 */
int
rbtree_map_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(anchor_rb_first(pop, map));
	//return OID_IS_NULL(RB_FIRST(map));
}

/*
 * rbtree_map_check -- check if given persistent object is a tree map
 */
int
rbtree_map_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
	//return TOID_IS_NULL(map) || !TOID_VALID(map);
}

/*
 * rbtree_map_insert_new -- allocates a new object and inserts it into the tree
 */
int
rbtree_map_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size, unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		//PMEMoid n = pmemobj_tx_alloc(size, type_num);
		PMEMoid n = sobj_tx_alloc(size, type_num);
		constructor(pop, sec_pmemobj_direct(n), arg);
		rbtree_map_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rbtree_map_remove_free -- removes and frees an object from the tree
 */
int
rbtree_map_remove_free(PMEMobjpool *pop, PMEMoid map,
		uint64_t key)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid val = rbtree_map_remove(pop, map, key);
		sobj_tx_free(val);
		//pmemobj_tx_free(val);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}
