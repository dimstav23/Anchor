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
 * ctree_map_opt.c -- Crit-bit trie implementation
 */

#include <ex_common.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "ctree_anchor_map_opt.h"

#define BIT_IS_SET(n, i) (!!((n) & (1ULL << (i))))

TOID_DECLARE(struct tree_map_node, CTREE_MAP_TYPE_OFFSET + 1);

struct tree_map_entry {
	uint64_t key;
	PMEMoid slot;
};

struct tree_map_node {
	int diff; /* most significant differing bit */
	struct tree_map_entry entries[2];
};

struct ctree_map {
	struct tree_map_entry root;
};

/*
 * find_crit_bit -- (internal) finds the most significant differing bit
 */
static int
find_crit_bit(uint64_t lhs, uint64_t rhs)
{
	return find_last_set_64(lhs ^ rhs);
}

/*
 * ctree_map_opt_create -- allocates a new crit-bit tree instance
 */
int
ctree_map_opt_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	int ret = 0;
	TX_BEGIN(pop) {
		//pmemobj_tx_add_range_direct(map, sizeof(*map));
		//*map = TX_ZNEW(struct ctree_map);
		//sobj_tx_add_range() //should be here!
		*map = sobj_tx_zalloc(sizeof(struct ctree_map), CTREE_MAP_TYPE_OFFSET);
	} TX_ONABORT {
		ret = 1;
	} TX_END
	return ret;
}

/*
 * ctree_map_opt_clear_node -- (internal) clears this node and its children
 */
static void
ctree_map_opt_clear_node(PMEMobjpool *pop, PMEMoid p)
{
	if (OID_IS_NULL(p))
		return;
	void *p_p;
	uint64_t obj_size;
	p_p = sobj_direct_size(pop, p, &obj_size);
	
	if (obj_size == sizeof(struct tree_map_node)) {
		ctree_map_opt_clear_node(pop, ((struct tree_map_node*)p_p)->entries[0].slot);
		ctree_map_opt_clear_node(pop, ((struct tree_map_node*)p_p)->entries[1].slot);
	}

	sobj_tx_free(p);
}

/*
 * ctree_map_opt_clear -- removes all elements from the map
 */
int
ctree_map_opt_clear(PMEMobjpool *pop, PMEMoid map)
{
	struct ctree_map *ctree = sobj_direct(pop, map);
	TX_BEGIN(pop) {
		ctree_map_opt_clear_node(pop, ctree->root.slot);
		sobj_tx_add_range(map, 0, sizeof(struct tree_map_entry));
		PMEMoid new_oid = OID_NULL;
        ctree->root.slot = new_oid;
	} TX_END
	return 0;
}

/*
 * ctree_map_opt_destroy -- cleanups and frees crit-bit tree instance
 * ANCHOR : not needed for benchmarking
 */
int
ctree_map_opt_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	int ret = 0;
	TX_BEGIN(pop) {
		ctree_map_opt_clear(pop, *map);
		//sobj_tx_add_range();
		pmemobj_tx_add_range_direct(map, sizeof(*map)); /*wrong*/
		sobj_tx_free(*map);
		*map = OID_NULL;
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * ctree_map_opt_insert_leaf -- (internal) inserts a new leaf at the position
 */
static void
ctree_map_opt_insert_leaf(PMEMobjpool *pop, PMEMoid p_oid, struct tree_map_entry *p,
	struct tree_map_entry e, int diff)
{
	PMEMoid new_node = sobj_tx_alloc(sizeof(struct tree_map_node), CTREE_MAP_TYPE_OFFSET + 1);
	struct tree_map_node *new_node_p = sobj_direct(pop, new_node);
	new_node_p->diff = diff;

	int d = BIT_IS_SET(e.key, new_node_p->diff);

	// insert the leaf at the direction based on the critical bit 
	new_node_p->entries[d] = e;

	// find the appropriate position in the tree to insert the node 
	struct tree_map_node *temp_node = NULL;
	void *p_slot_p = NULL;
	uint64_t obj_size;
	int entry_idx = -1;

	if (!OID_IS_NULL(p->slot))
		p_slot_p = sobj_direct_size(pop, p->slot, &obj_size);
	else
		obj_size = 0;


	while (obj_size == sizeof(struct tree_map_node)) {
		
		temp_node = (struct tree_map_node*)p_slot_p;

		// the critical bits have to be sorted 
		if (temp_node->diff < new_node_p->diff)
			break;
		
		p_oid = p->slot;

		entry_idx = BIT_IS_SET(e.key, temp_node->diff);
		p = &temp_node->entries[entry_idx];
		
		p_slot_p = sobj_direct_size(pop, p->slot, &obj_size);	
	}
	
	// insert the found destination in the other slot 
	new_node_p->entries[!d] = *p;
	
    /* HERE -- seems correct */
	if (entry_idx != -1) { //leaf change - choose which entry by entry_idx should be updated in the object pointed by p_oid
		sobj_tx_add_range(p_oid, offsetof(struct tree_map_node, entries) + entry_idx * sizeof(struct tree_map_entry), sizeof(*p));
		p->key = 0;
		p->slot = new_node;
	}
	else { //if we did not enter the loop - root change
		sobj_tx_add_range(p_oid, 0, sizeof(*p));
		p->key = 0;
		p->slot = new_node;
	}

}

/*
 * ctree_map_opt_insert_new -- allocates a new object and inserts it into the tree
 */
int
ctree_map_opt_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size, unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	int ret = 0;
	TX_BEGIN(pop) {
		PMEMoid n = sobj_tx_alloc(size, type_num);
		void* n_temp = sobj_direct(pop, n);
		constructor(pop, n_temp, arg);
		ctree_map_opt_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * ctree_map_opt_insert -- inserts a new key-value pair into the map
 */
int
ctree_map_opt_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value)
{
	struct ctree_map *ctree = sobj_direct(pop, map);
	struct tree_map_entry *p = &ctree->root;
	int ret = 0;
	// descend the path until a best matching key is found
	PMEMoid node = map; // holds oid to tree_map_node struct pointing to p
	struct tree_map_node *temp_node = NULL;
	void *p_slot_p;
	uint64_t obj_size = 0;
	int entry_idx;

	if (!OID_IS_NULL(p->slot)) {
		p_slot_p = sobj_direct_size(pop, p->slot, &obj_size);
	
		while (obj_size == sizeof(struct tree_map_node)) {
			node = p->slot;

			temp_node = (struct tree_map_node*)p_slot_p;
			entry_idx = BIT_IS_SET(key, temp_node->diff);
			p = &temp_node->entries[entry_idx];

			if (!OID_IS_NULL(p->slot)) {
				p_slot_p = sobj_direct_size(pop, p->slot, &obj_size);
			}
			else
				break;
		}
	}

	struct tree_map_entry e = {key, value};
	TX_BEGIN(pop) {
		if (p->key == 0 || p->key == key) {
			if (OID_EQUALS(node, map)){ //if root has to be updated
				sobj_tx_add_range(map, 0, sizeof(*p));		
                ctree->root = e;	
			}
			else {
				if (p->key == key) {
					struct tree_map_node *node_p = sobj_direct(pop, node);
					if (!OID_EQUALS(node_p->entries[entry_idx].slot, e.slot)) {
						sobj_tx_add_range(node, offsetof(struct tree_map_node, entries) +
									entry_idx * sizeof(struct tree_map_entry), sizeof(*p));	
                        PMEMoid to_be_freed = node_p->entries[entry_idx].slot;
                        node_p->entries[entry_idx] = e;
						sobj_tx_free(to_be_freed); // free the value to be updated
					}
				}
				else {
                    struct tree_map_node *node_p = sobj_direct(pop, node);
					sobj_tx_add_range(node, offsetof(struct tree_map_node, entries) +
									entry_idx * sizeof(struct tree_map_entry), sizeof(*p));		
                    node_p->entries[entry_idx] = e;
				}
				
			}
		} 
		else {
			ctree_map_opt_insert_leaf(pop, map, &ctree->root, e,
					find_crit_bit(p->key, key));
		}
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * ctree_map_opt_get_leaf -- (internal) searches for a leaf of the key
 */
static struct tree_map_entry *
ctree_map_opt_get_leaf(PMEMobjpool *pop, PMEMoid map, uint64_t key,
	struct tree_map_entry **parent, PMEMoid *mode_node_oid, uint64_t *mode_node_offset)
{
	struct ctree_map *ctree = sobj_direct(pop, map);

	struct tree_map_entry *n = &ctree->root;
	struct tree_map_entry *p = NULL;

	uint64_t n_off = 0;
	PMEMoid n_node_oid = map;
	uint64_t p_off = 0;	
	PMEMoid p_node_oid = OID_NULL;

	uint64_t obj_size;
	if (!OID_IS_NULL(n->slot)) {
		void* n_slot_p = sobj_direct_size(pop, n->slot, &obj_size);

		while (obj_size == sizeof(struct tree_map_node)){
			//node = n->slot;
			struct tree_map_node* node_temp = (struct tree_map_node*)n_slot_p;
			
			p_node_oid = n_node_oid;
			p_off = n_off;
			p = n;
			
			n_node_oid = n->slot;
			n = &node_temp->entries[BIT_IS_SET(key, node_temp->diff)];
			n_off = offsetof(struct tree_map_node, entries) + BIT_IS_SET(key, node_temp->diff) * sizeof(struct tree_map_entry);
			
			if (!OID_IS_NULL(n->slot))
				n_slot_p = sobj_direct_size(pop, n->slot, &obj_size);
			else
				break;
		}
	}
	
	if (n->key == key) {
		if (parent) {
			if (p != NULL) {
				*mode_node_offset = p_off;
				*mode_node_oid = p_node_oid;
				*parent = p;
			}
			else {
				*parent = NULL;
				*mode_node_offset = n_off;
				*mode_node_oid = n_node_oid;
			}

		}
        return n;
	}

	return NULL;
}

/*
 * ctree_map_opt_remove_free -- removes and frees an object from the tree
 */
int
ctree_map_opt_remove_free(PMEMobjpool *pop, PMEMoid map,
		uint64_t key)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid val = ctree_map_opt_remove(pop, map, key);
		sobj_tx_free(val);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * ctree_map_opt_remove -- removes key-value pair from the map
 */
PMEMoid
ctree_map_opt_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	PMEMoid mod_node_oid = OID_NULL; // oid of the node that is going to be modified
	uint64_t mod_node_offset = 0; // offset inside the node that is going to be modified
	struct tree_map_entry *parent = NULL;
	struct tree_map_entry *leaf = ctree_map_opt_get_leaf(pop, map, key, &parent, &mod_node_oid, &mod_node_offset);
	if (leaf == NULL)
		return OID_NULL;

	PMEMoid ret = leaf->slot;

    
	if (parent == NULL) { // root
		TX_BEGIN(pop) {
			//mod_node_oid is equal to leaf_node oid
			//mod_node_offset is equal to offset of the leaf in its node object
			sobj_tx_add_range(mod_node_oid, mod_node_offset, sizeof(*leaf));
			leaf->key = 0;
			leaf->slot = OID_NULL;
		} TX_END
	} else {
		/*
		 * In this situation:
		 *	 parent
		 *	/     \
		 *   LEFT   RIGHT
		 * there's no point in leaving the parent internal node
		 * so it's swapped with the remaining node and then also freed.
		 */
        /* HERE -- seems correct */
		TX_BEGIN(pop) {
			struct tree_map_entry *dest = parent;
			PMEMoid node = parent->slot;
			//mod_node_oid is equal to parent_node_ oid
			//mod_node_offset is equal to offset of the parent entry in its node object
			sobj_tx_add_range(mod_node_oid, mod_node_offset, sizeof(*dest));
			struct tree_map_node *temp_node = sobj_direct(pop, node);
            *dest = temp_node->entries[temp_node->entries[0].key == leaf->key];

			sobj_tx_free(node);			
		} TX_END
	}
	
	return ret;
}

/*
 * ctree_map_opt_get -- searches for a value of the key
 */
PMEMoid
ctree_map_opt_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	struct tree_map_entry *entry = ctree_map_opt_get_leaf(pop, map, key, NULL, NULL, NULL);
	PMEMoid ret;
	if (entry != NULL) {
		ret = entry->slot;
	}
	else {
		ret = OID_NULL;
	}
	return ret;
}

/*
 * ctree_map_opt_lookup -- searches if a key exists
 */
int
ctree_map_opt_lookup(PMEMobjpool *pop, PMEMoid map,
		uint64_t key)
{
	struct tree_map_entry *entry = ctree_map_opt_get_leaf(pop, map, key, NULL, NULL, NULL);
	if (entry != NULL) {
		return 1;
	}
	else {
		return 0;
	}
}

/*
 * ctree_map_opt_foreach_node -- (internal) recursively traverses tree
 */
static int
ctree_map_opt_foreach_node(PMEMobjpool *pop, struct tree_map_entry e,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	int ret = 0;
	void *e_slot_p = NULL;
	uint64_t obj_size;
	if (!OID_IS_NULL(e.slot))
		e_slot_p = sobj_direct_size(pop, e.slot, &obj_size);
	else
		obj_size = 0;

	if (obj_size == sizeof(struct tree_map_node)) {
		if (ctree_map_opt_foreach_node(pop, ((struct tree_map_node*)e_slot_p)->entries[0],
					cb, arg) == 0)
			ctree_map_opt_foreach_node(pop, ((struct tree_map_node*)e_slot_p)->entries[1], cb, arg);
	} else { /* leaf */
		ret = cb(e.key, e.slot, arg);
	}
	
	return ret;
}

/*
 * ctree_map_opt_foreach -- initiates recursive traversal
 */
int
ctree_map_opt_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	struct ctree_map *ctree = sobj_direct(pop, map);
	if (OID_IS_NULL(ctree->root.slot)) {
		return 0;
	}

	int ret = ctree_map_opt_foreach_node(pop, ctree->root, cb, arg);
	return ret;
}

/*
 * ctree_map_opt_is_empty -- checks whether the tree map is empty
 */
int
ctree_map_opt_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	struct ctree_map *ctree = sobj_direct(pop, map);
	return (ctree->root.key == 0);
}

/*
 * ctree_map_opt_check -- check if given persistent object is a tree map
 */
int
ctree_map_opt_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
}
