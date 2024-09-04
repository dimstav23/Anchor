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
 * rbtree_anchor_map_opt.c -- red-black tree implementation /w sentinel nodes
 */

#include <assert.h>
#include <errno.h>
#include "rbtree_anchor_map_opt.h"

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
	PMEMoid parent;
	PMEMoid slots[MAX_RB];
};

struct rbtree_map {
	PMEMoid sentinel;
	PMEMoid root;
};

static inline PMEMoid anchor_node_p(PMEMobjpool *pop, PMEMoid n) 
{
	struct tree_map_node *n_p = sobj_direct(pop, n);
	return n_p->parent;
}

static inline PMEMoid anchor_node_grandp(PMEMobjpool *pop, PMEMoid n) 
{
	return anchor_node_p(pop, anchor_node_p(pop, n));
}

static inline PMEMoid anchor_node_parent_at(PMEMobjpool *pop, PMEMoid n,
											enum rb_children idx) 
{
	struct tree_map_node *n_p = sobj_direct(pop, anchor_node_p(pop, n));
	return n_p->slots[idx];
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
	struct rbtree_map *rbtree_map = sobj_direct(pop, map);
	struct tree_map_node *root_p = sobj_direct(pop, rbtree_map->root);
	return root_p->slots[RB_LEFT];
}

static inline int anchor_node_is_null(PMEMoid n, PMEMoid s) 
{
	return OID_EQUALS(n, s);
}


/*
 * rbtree_map_opt_create -- allocates a new red-black tree instance
 */
int
rbtree_map_opt_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	int ret = 0;
	TX_BEGIN(pop) {
		*map = sobj_tx_zalloc(sizeof(struct rbtree_map), RBTREE_MAP_TYPE_OFFSET);
		PMEMoid s = sobj_tx_zalloc(sizeof(struct tree_map_node), RBTREE_MAP_TYPE_OFFSET + 1);
		struct tree_map_node *s_vol = sobj_direct(pop, s);
		s_vol->color = COLOR_BLACK;
		s_vol->parent = s;
		s_vol->slots[RB_LEFT] = s;
		s_vol->slots[RB_RIGHT] = s;

		PMEMoid r = sobj_tx_zalloc(sizeof(struct tree_map_node), RBTREE_MAP_TYPE_OFFSET + 1);
		struct tree_map_node *r_vol = sobj_direct(pop, r);
		r_vol->color = COLOR_BLACK;
		r_vol->parent = s;
		r_vol->slots[RB_LEFT] = s;
		r_vol->slots[RB_RIGHT] = s;

		struct rbtree_map *rbtree_map_opt_vol = sobj_direct(pop, *map);
		rbtree_map_opt_vol->sentinel = s;
		rbtree_map_opt_vol->root = r;
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rbtree_map_opt_clear_node -- (internal) clears this node and its children
 */
static void
rbtree_map_opt_clear_node(PMEMobjpool *pop, PMEMoid map, PMEMoid p)
{
	struct rbtree_map *rbtree_map = sobj_direct(pop, map);
	PMEMoid s = rbtree_map->sentinel;

	struct tree_map_node *p_p = sobj_direct(pop, p);
	if (!anchor_node_is_null(p_p->slots[RB_LEFT], s))
		rbtree_map_opt_clear_node(pop, map, p_p->slots[RB_LEFT]);

	if (!anchor_node_is_null(p_p->slots[RB_RIGHT], s))
		rbtree_map_opt_clear_node(pop, map, p_p->slots[RB_RIGHT]);

	sobj_tx_free(p);
}

/*
 * rbtree_map_opt_clear -- removes all elements from the map
 */
int
rbtree_map_opt_clear(PMEMobjpool *pop, PMEMoid map)
{
	TX_BEGIN(pop) {
		struct rbtree_map *rbtree_map = sobj_direct(pop, map);
		rbtree_map_opt_clear_node(pop, map, rbtree_map->root);
		sobj_tx_add_range(map, 0, sizeof(struct rbtree_map));

		sobj_tx_free(rbtree_map->sentinel);

		rbtree_map->sentinel = OID_NULL;
		rbtree_map->root = OID_NULL;
	} TX_END

	return 0;
}

/*
 * rbtree_map_opt_destroy -- cleanups and frees red-black tree instance
 */
int
rbtree_map_opt_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	int ret = 0;
	TX_BEGIN(pop) {
		rbtree_map_opt_clear(pop, *map);
		sobj_tx_free(*map);
		*map = OID_NULL;
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rbtree_map_opt_rotate -- (internal) performs a left/right rotation around a node
 */
static void
rbtree_map_opt_rotate(PMEMobjpool *pop, PMEMoid map,
	PMEMoid node, enum rb_children c)
{
	struct rbtree_map *rbtree_map = sobj_direct(pop, map);
	struct tree_map_node *node_p = sobj_direct(pop, node);
	PMEMoid child = node_p->slots[!c];
	PMEMoid s = rbtree_map->sentinel;

	sobj_tx_add_range(node, 0, sizeof(struct tree_map_node));
	sobj_tx_add_range(child, 0, sizeof(struct tree_map_node));

	struct tree_map_node *child_p = sobj_direct(pop, child);

	node_p->slots[!c] = child_p->slots[c];

	if (!OID_EQUALS(child_p->slots[c], s)) {
        struct tree_map_node *temp_p = sobj_direct(pop, child_p->slots[c]);
		sobj_tx_add_range(child_p->slots[c], offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
        temp_p->parent = node;
	}
	
	child_p->parent = anchor_node_p(pop, node);
	PMEMoid mod_node = anchor_node_p(pop, node);
	enum rb_children mod_idx = anchor_node_location(pop, node);
    struct tree_map_node *temp_p = sobj_direct(pop, mod_node);
	sobj_tx_add_range(mod_node, offsetof(struct tree_map_node, slots) + mod_idx * sizeof(PMEMoid),
						sizeof(PMEMoid));
    temp_p->slots[mod_idx] = child;	

	child_p->slots[c] = node;
	node_p->parent = child;
}

/*
 * rbtree_map_opt_insert_bst -- (internal) inserts a node in regular BST fashion
 */
static void
rbtree_map_opt_insert_bst(PMEMobjpool *pop, PMEMoid map, PMEMoid n)
{
	struct rbtree_map *rbtree_map = sobj_direct(pop, map);
	PMEMoid parent = rbtree_map->root;
	PMEMoid my_dst = anchor_rb_first(pop, map);
	enum rb_children my_dst_idx = RB_LEFT; //RB LEFT child of the parent
	PMEMoid s = rbtree_map->sentinel;

	struct tree_map_node *n_p = sobj_direct(pop, n);
	n_p->slots[RB_LEFT] = s;
	n_p->slots[RB_RIGHT] = s;

	struct tree_map_node *temp;
	
	while (!anchor_node_is_null(my_dst, s)) {
		parent = my_dst;
		temp = sobj_direct(pop, my_dst);
		my_dst_idx = (enum rb_children)(n_p->key > temp->key);
		my_dst = temp->slots[my_dst_idx];
	}

	sobj_tx_add_range(n, offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
	n_p->parent = parent;

	/*
	 *	parent points to the object that *dst in original version belongs to and
	 * 	my_dst_idx shows the appropriate slot to update
	 */
    struct tree_map_node *parent_p = sobj_direct(pop, parent);
	sobj_tx_add_range(parent, offsetof(struct tree_map_node, slots) + my_dst_idx * sizeof(PMEMoid), sizeof(PMEMoid));
    parent_p->slots[my_dst_idx] = n;
}

/*
 * rbtree_map_opt_recolor -- (internal) restores red-black tree properties
 */
static PMEMoid
rbtree_map_opt_recolor(PMEMobjpool *pop, PMEMoid map,
	PMEMoid n, enum rb_children c)
{
	struct tree_map_node *temp = sobj_direct(pop, anchor_node_grandp(pop, n));
	PMEMoid uncle = temp->slots[!c];
	struct tree_map_node *uncle_p = sobj_direct(pop, uncle);

	if (uncle_p->color == COLOR_RED) {
		sobj_tx_add_range(uncle, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
        uncle_p->color = COLOR_BLACK;
		
		PMEMoid mod = anchor_node_p(pop, n);
        struct tree_map_node *mod_p = sobj_direct(pop, mod);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
        mod_p->color = COLOR_BLACK;

		mod = anchor_node_grandp(pop, n);
        mod_p = sobj_direct(pop, mod);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
        mod_p->color = COLOR_RED;
		return mod;

	} else {
		if (anchor_node_is(pop, n, !c)) {
			n = anchor_node_p(pop, n);
			rbtree_map_opt_rotate(pop, map, n, c);
		}
		PMEMoid mod = anchor_node_p(pop, n);
        struct tree_map_node *mod_p = sobj_direct(pop, mod);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
        mod_p->color = COLOR_BLACK;

		mod = anchor_node_grandp(pop, n);
        mod_p = sobj_direct(pop, mod);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), 
							sizeof(enum rb_color));
        mod_p->color = COLOR_RED;
		rbtree_map_opt_rotate(pop, map, mod, (enum rb_children)!c);
	}
	return n;
}

/*
 * rbtree_map_opt_find_node -- (internal) returns the node that contains the key
 */
static PMEMoid
rbtree_map_opt_find_node(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid dst = anchor_rb_first(pop, map);
	struct rbtree_map *rbtree_map = sobj_direct(pop, map);
	PMEMoid s = rbtree_map->sentinel;

	while (!anchor_node_is_null(dst, s)) {
		struct tree_map_node *dst_p = sobj_direct(pop, dst);
		if (dst_p->key == key){
			return dst;
		}
		dst = dst_p->slots[key > dst_p->key];
	}

	return OID_NULL;
}

/*
 * rbtree_map_opt_update_check -- searches for a value of the key
 */
static int
rbtree_map_opt_update_check(PMEMobjpool *pop, PMEMoid map, uint64_t key, PMEMoid value)
{
	PMEMoid node = rbtree_map_opt_find_node(pop, map, key);
	if (OID_IS_NULL(node))
		return 0;

	struct tree_map_node *node_p = sobj_direct(pop, node);
	if (!OID_EQUALS(node_p->value, value)) {
		TX_BEGIN(pop) {	
			sobj_tx_add_range(node, offsetof(struct tree_map_node, value), sizeof(PMEMoid));
			sobj_tx_free(node_p->value);
			node_p->value = value;
		} TX_END
	}
	return 1;
}

/*
 * rbtree_map_opt_insert -- inserts a new key-value pair into the map
 */
int
rbtree_map_opt_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value)
{
	//first check if the value exists in order to update it
	if (rbtree_map_opt_update_check(pop, map, key, value)) {
		return 0;
	}

	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid n = sobj_tx_zalloc(sizeof(struct tree_map_node), RBTREE_MAP_TYPE_OFFSET + 1);
		////struct tree_map_node new_node = {0};
		struct tree_map_node *n_p = sobj_direct(pop, n);
		n_p->key = key;
		n_p->value = value;
		
		rbtree_map_opt_insert_bst(pop, map, n);

       	//struct tree_map_node *n_p = sobj_direct(pop, n);
        n_p->color = COLOR_RED;

		struct tree_map_node *temp = sobj_direct(pop, anchor_node_p(pop, n));
		while (temp->color == COLOR_RED) {
			n = rbtree_map_opt_recolor(pop, map, n, (enum rb_children)
					anchor_node_location(pop, (anchor_node_p(pop, n))));
			temp = sobj_direct(pop, anchor_node_p(pop, n));
		}

        PMEMoid mod = anchor_rb_first(pop, map);
        struct tree_map_node *mod_p = sobj_direct(pop, mod);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
        mod_p->color = COLOR_BLACK;
	} TX_END

	return ret;
}

/*
 * rbtree_map_opt_successor -- (internal) returns the successor of a node
 */
static PMEMoid
rbtree_map_opt_successor(PMEMobjpool *pop, PMEMoid map, PMEMoid n)
{
	struct tree_map_node *n_p = sobj_direct(pop, n);
	PMEMoid dst = n_p->slots[RB_RIGHT];
	struct rbtree_map *rbtree_map = sobj_direct(pop, map);
	PMEMoid s = rbtree_map->sentinel;

	struct tree_map_node *dst_p;
	if (!OID_EQUALS(s, dst)) {
		dst_p = sobj_direct(pop, dst);
		while (!anchor_node_is_null(dst_p->slots[RB_LEFT], s)) {
			dst = dst_p->slots[RB_LEFT];
			dst_p = sobj_direct(pop, dst);
		}
	} else {
		dst = n_p->parent;
		dst_p = sobj_direct(pop, dst);
		while (OID_EQUALS(n, dst_p->slots[RB_RIGHT])) {
			n = dst;
			dst = anchor_node_p(pop, dst);
			dst_p = sobj_direct(pop, dst);
		}
		if (OID_EQUALS(dst, rbtree_map->root)) {
			return s;
		}

	}
	return dst;
}

/*
 * rbtree_map_opt_repair_branch -- (internal) restores red-black tree in one branch
 */
static PMEMoid
rbtree_map_opt_repair_branch(PMEMobjpool *pop, PMEMoid map,
	PMEMoid n, enum rb_children c)
{
	PMEMoid sb = anchor_node_parent_at(pop, n, !c); /* sibling */
	struct tree_map_node *sb_p = sobj_direct(pop, sb);

	if (sb_p->color == COLOR_RED) {
		sobj_tx_add_range(sb, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
		sb_p->color = COLOR_BLACK;

		PMEMoid mod = anchor_node_p(pop, n);
        struct tree_map_node *mod_p = sobj_direct(pop, mod);
		sobj_tx_add_range(mod, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
        mod_p->color = COLOR_RED;

		rbtree_map_opt_rotate(pop, map, mod, c);
		sb = anchor_node_parent_at(pop, n, !c);
		sb_p = sobj_direct(pop, sb);
	}
	
	struct tree_map_node *slots[MAX_RB];
	slots[RB_RIGHT] = sobj_direct(pop, sb_p->slots[RB_RIGHT]);
	slots[RB_LEFT] = sobj_direct(pop, sb_p->slots[RB_LEFT]);

	if (slots[RB_RIGHT]->color == COLOR_BLACK &&
		slots[RB_LEFT]->color == COLOR_BLACK) {
        sb_p = sobj_direct(pop, sb);
		sobj_tx_add_range(sb, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
        sb_p->color = COLOR_RED;
		struct tree_map_node *n_p = sobj_direct(pop, n);
		return n_p->parent;
	} else {
        struct tree_map_node *temp_p;
		if (slots[!c]->color == COLOR_BLACK) {
            temp_p = sobj_direct(pop, sb_p->slots[c]);
			sobj_tx_add_range(sb_p->slots[c], offsetof(struct tree_map_node, color), sizeof(enum rb_color));
            temp_p->color = COLOR_BLACK;

            temp_p = sobj_direct(pop, sb);
			sobj_tx_add_range(sb, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
            temp_p->color = COLOR_RED;

			rbtree_map_opt_rotate(pop, map, sb, (enum rb_children)!c);
			sb = anchor_node_parent_at(pop, n, !c);
			sb_p = sobj_direct(pop, sb);
		}
		PMEMoid node_p_n = anchor_node_p(pop, n);
		struct tree_map_node *node_p_n_p = sobj_direct(pop, node_p_n);
        temp_p = sobj_direct(pop, sb);
		sobj_tx_add_range(sb, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
        temp_p->color = node_p_n_p->color;
                        
		sobj_tx_add_range(node_p_n, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
        node_p_n_p->color = COLOR_BLACK;

        temp_p = sobj_direct(pop, sb_p->slots[!c]);
		sobj_tx_add_range(sb_p->slots[!c], offsetof(struct tree_map_node, color), sizeof(enum rb_color));
        temp_p->color = COLOR_BLACK;

		rbtree_map_opt_rotate(pop, map, node_p_n, c);

		return anchor_rb_first(pop, map);
	}

	return n;
}

/*
 * rbtree_map_opt_repair -- (internal) restores red-black tree properties
 * after remove
 */
static void
rbtree_map_opt_repair(PMEMobjpool *pop, PMEMoid map, PMEMoid n)
{
	/* if left, repair right sibling, otherwise repair left sibling. */
	struct tree_map_node *n_p = sobj_direct(pop, n);
	while (!OID_EQUALS(n, anchor_rb_first(pop, map)) && n_p->color == COLOR_BLACK) {
		n = rbtree_map_opt_repair_branch(pop, map, n, (enum rb_children)
				anchor_node_location(pop, n));
		n_p = sobj_direct(pop, n);
	}
	
	sobj_tx_add_range(n, offsetof(struct tree_map_node, color), sizeof(enum rb_color));
	n_p->color = COLOR_BLACK;
}

/*
 * rbtree_map_opt_remove -- removes key-value pair from the map
 */
PMEMoid
rbtree_map_opt_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid ret = OID_NULL;

	PMEMoid n = rbtree_map_opt_find_node(pop, map, key);
	if (OID_IS_NULL(n))
		return ret;

	struct tree_map_node *n_p = sobj_direct(pop, n);
	ret = n_p->value;

	struct rbtree_map *rbtree_map = sobj_direct(pop, map);
	PMEMoid s = rbtree_map->sentinel;
	PMEMoid r = rbtree_map->root;

	PMEMoid y = (anchor_node_is_null(n_p->slots[RB_LEFT], s) ||
					anchor_node_is_null(n_p->slots[RB_RIGHT], s))
					? n : rbtree_map_opt_successor(pop, map, n);

	struct tree_map_node *y_p = sobj_direct(pop, y);
	PMEMoid x = anchor_node_is_null(y_p->slots[RB_LEFT], s) ?
			y_p->slots[RB_RIGHT] : y_p->slots[RB_LEFT];

	TX_BEGIN(pop) {
		PMEMoid node_p_y = anchor_node_p(pop, y);
        struct tree_map_node *temp_p = sobj_direct(pop, x);
		sobj_tx_add_range(x, offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
        temp_p->parent = node_p_y;
		
		PMEMoid node_p_x = anchor_node_p(pop, x);
		if (OID_EQUALS(node_p_x, r)) {
            temp_p = sobj_direct(pop, r);
			sobj_tx_add_range(r, offsetof(struct tree_map_node, slots) + RB_LEFT * sizeof(PMEMoid),
								 sizeof(PMEMoid));
            temp_p->slots[RB_LEFT] = x;
		} else {
			enum rb_children idx = anchor_node_location(pop, y);
            temp_p = sobj_direct(pop, node_p_y);
			sobj_tx_add_range(node_p_y, offsetof(struct tree_map_node, slots) + idx * sizeof(PMEMoid),
								 sizeof(PMEMoid));
            temp_p->slots[idx] = x;
		}

		y_p = sobj_direct(pop, y); //might have been overwritten
		if (y_p->color == COLOR_BLACK)
			rbtree_map_opt_repair(pop, map, x);

		if (!OID_EQUALS(y, n)) {
			sobj_tx_add_range(y, 0, sizeof(struct tree_map_node));
			n_p = sobj_direct(pop, n); //might have been overwritten
			y_p->slots[RB_LEFT] = n_p->slots[RB_LEFT];
			y_p->slots[RB_RIGHT] = n_p->slots[RB_RIGHT];
			y_p->parent = n_p->parent;
			y_p->color = n_p->color;

            temp_p = sobj_direct(pop, n_p->slots[RB_LEFT]);
			sobj_tx_add_range(n_p->slots[RB_LEFT], offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
            temp_p->parent = y;
                                
            temp_p = sobj_direct(pop, n_p->slots[RB_RIGHT]);
			sobj_tx_add_range(n_p->slots[RB_RIGHT], offsetof(struct tree_map_node, parent), sizeof(PMEMoid));
            temp_p->parent = y;
			
			PMEMoid node_p_n = anchor_node_p(pop, n);
			enum rb_children idx = anchor_node_location(pop, n);
            temp_p = sobj_direct(pop, node_p_n);
			sobj_tx_add_range(node_p_n, offsetof(struct tree_map_node, slots) + idx * sizeof(PMEMoid),
								 sizeof(PMEMoid));
            temp_p->slots[idx] = y;
		}
		sobj_tx_free(n);
	} TX_END

	return ret;
}

/*
 * rbtree_map_opt_get -- searches for a value of the key
 */
PMEMoid
rbtree_map_opt_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid node = rbtree_map_opt_find_node(pop, map, key);
	if (OID_IS_NULL(node))
		return OID_NULL;

	struct tree_map_node *node_p = sobj_direct(pop, node);
	PMEMoid ret = node_p->value;
	return ret;
}

/*
 * rbtree_map_opt_lookup -- searches if key exists
 */
int
rbtree_map_opt_lookup(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid node = rbtree_map_opt_find_node(pop, map, key);
	if (OID_IS_NULL(node))
		return 0;

	return 1;
}

/*
 * rbtree_map_opt_foreach_node -- (internal) recursively traverses tree
 */
static int
rbtree_map_opt_foreach_node(PMEMobjpool *pop, PMEMoid map,
	PMEMoid p,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	int ret = 0;

	struct rbtree_map *rbtree_map = sobj_direct(pop, map);
	if (OID_EQUALS(p, rbtree_map->sentinel)){
		return 0;
	}

	struct tree_map_node *p_p = sobj_direct(pop, p);
	if ((ret = rbtree_map_opt_foreach_node(pop, map,
		p_p->slots[RB_LEFT], cb, arg)) == 0) {
		if ((ret = cb(p_p->key, p_p->value, arg)) == 0)
			rbtree_map_opt_foreach_node(pop, map,
				p_p->slots[RB_RIGHT], cb, arg);
	}
	return ret;
}

/*
 * rbtree_map_opt_foreach -- initiates recursive traversal
 */
int
rbtree_map_opt_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	return rbtree_map_opt_foreach_node(pop, map, anchor_rb_first(pop, map), cb, arg);
}

/*
 * rbtree_map_opt_is_empty -- checks whether the tree map is empty
 */
int
rbtree_map_opt_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(anchor_rb_first(pop, map));
}

/*
 * rbtree_map_opt_check -- check if given persistent object is a tree map
 */
int
rbtree_map_opt_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
}

/*
 * rbtree_map_opt_insert_new -- allocates a new object and inserts it into the tree
 */
int
rbtree_map_opt_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size, unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid n = sobj_tx_alloc(size, type_num);
		constructor(pop, sec_pmemobj_direct(n), arg);
		rbtree_map_opt_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * rbtree_map_opt_remove_free -- removes and frees an object from the tree
 */
int
rbtree_map_opt_remove_free(PMEMobjpool *pop, PMEMoid map,
		uint64_t key)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid val = rbtree_map_opt_remove(pop, map, key);
		sobj_tx_free(val);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}