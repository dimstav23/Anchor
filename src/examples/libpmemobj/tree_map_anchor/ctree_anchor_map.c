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
 * ctree_map.c -- Crit-bit trie implementation
 */

#include <ex_common.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "ctree_anchor_map.h"

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
 * ctree_map_create -- allocates a new crit-bit tree instance
 */
int
ctree_map_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
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
 * ctree_map_clear_node -- (internal) clears this node and its children
 */
static void
ctree_map_clear_node(PMEMobjpool *pop, PMEMoid p)
{
	if (OID_IS_NULL(p))
		return;

	/*
	if (OID_INSTANCEOF(p, struct tree_map_node)) {
		TOID(struct tree_map_node) node;
		TOID_ASSIGN(node, p);

		ctree_map_clear_node(D_RW(node)->entries[0].slot);
		ctree_map_clear_node(D_RW(node)->entries[1].slot);
	}

	pmemobj_tx_free(p);
	*/
	void *p_p;
	uint64_t obj_size;
	p_p = sobj_read(pop, p, 1, &obj_size);
	
	if (obj_size == sizeof(struct tree_map_node)) {
		ctree_map_clear_node(pop, ((struct tree_map_node*)p_p)->entries[0].slot);
		ctree_map_clear_node(pop, ((struct tree_map_node*)p_p)->entries[1].slot);
	}

	free(p_p);
	sobj_tx_free(p);
}

/*
 * ctree_map_clear -- removes all elements from the map
 */
int
ctree_map_clear(PMEMobjpool *pop, PMEMoid map)
{
	/*
	TX_BEGIN(pop) {
		ctree_map_clear_node(D_RW(map)->root.slot);
		TX_ADD_FIELD(map, root);
		D_RW(map)->root.slot = OID_NULL;
	} TX_END
	*/
	struct ctree_map *ctree = sobj_read(pop, map, 1, NULL);
	TX_BEGIN(pop) {
		ctree_map_clear_node(pop, ctree->root.slot);
		sobj_tx_add_range(map, 0, sizeof(struct tree_map_entry));
		PMEMoid new_oid = OID_NULL;
		sobj_tx_write_part(pop, map, offsetof(struct tree_map_entry, slot), sizeof(PMEMoid), &new_oid);
	} TX_END
	free(ctree);
	return 0;
}

/*
 * ctree_map_destroy -- cleanups and frees crit-bit tree instance
 * ANCHOR : not needed for benchmarking
 */
int
ctree_map_destroy(PMEMobjpool *pop, PMEMoid *map)
{
	int ret = 0;
	TX_BEGIN(pop) {
		ctree_map_clear(pop, *map);
		//sobj_tx_add_range();
		pmemobj_tx_add_range_direct(map, sizeof(*map)); /*wrong*/
		sobj_tx_free(*map);
		*map = OID_NULL;

		//pmemobj_tx_add_range_direct(map, sizeof(*map));
		//TX_FREE(*map);
		//*map = TOID_NULL(struct ctree_map);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * ctree_map_insert_leaf -- (internal) inserts a new leaf at the position
 */
static void
ctree_map_insert_leaf(PMEMobjpool *pop, PMEMoid p_oid, struct tree_map_entry *p,
	struct tree_map_entry e, int diff)
{
	/*
	TOID(struct tree_map_node) new_node = TX_NEW(struct tree_map_node);
	D_RW(new_node)->diff = diff;

	int d = BIT_IS_SET(e.key, D_RO(new_node)->diff);

	// insert the leaf at the direction based on the critical bit 
	D_RW(new_node)->entries[d] = e;

	// find the appropriate position in the tree to insert the node 
	TOID(struct tree_map_node) node;
	while (OID_INSTANCEOF(p->slot, struct tree_map_node)) {
		TOID_ASSIGN(node, p->slot);

		// the critical bits have to be sorted 
		if (D_RO(node)->diff < D_RO(new_node)->diff)
			break;

		p = &D_RW(node)->entries[BIT_IS_SET(e.key, D_RO(node)->diff)];
	}

	// insert the found destination in the other slot 
	D_RW(new_node)->entries[!d] = *p;

	pmemobj_tx_add_range_direct(p, sizeof(*p));
	p->key = 0;
	p->slot = new_node.oid;
	*/
	PMEMoid new_node = sobj_tx_alloc(sizeof(struct tree_map_node), CTREE_MAP_TYPE_OFFSET + 1);
	struct tree_map_node *new_node_p = sobj_read(pop, new_node, 1, NULL);
	new_node_p->diff = diff;

	int d = BIT_IS_SET(e.key, new_node_p->diff);

	// insert the leaf at the direction based on the critical bit 
	new_node_p->entries[d] = e;

	// find the appropriate position in the tree to insert the node 
	struct tree_map_node *temp_node = NULL;
	void *p_slot_p = NULL;
	void *p_slot_p_free = NULL;
	uint64_t obj_size;
	int entry_idx = -1;

	if (!OID_IS_NULL(p->slot))
		p_slot_p = sobj_read(pop, p->slot, 1, &obj_size);
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
		
		if (p_slot_p_free != NULL) free(p_slot_p_free);

		p_slot_p_free = p_slot_p;
		p_slot_p = sobj_read(pop, p->slot, 1, &obj_size);	
	}
	
	if (p_slot_p != NULL) free(p_slot_p);
	
	// insert the found destination in the other slot 
	new_node_p->entries[!d] = *p;
	sobj_tx_write(pop, new_node, new_node_p);
	
	if (entry_idx != -1) { //leaf change - choose which entry by entry_idx should be updated in the object pointed by p_oid
		sobj_tx_add_range(p_oid, offsetof(struct tree_map_node, entries) + entry_idx * sizeof(struct tree_map_entry), sizeof(*p));
		p->key = 0;
		p->slot = new_node;
		sobj_tx_write_part(pop, p_oid, offsetof(struct tree_map_node, entries) + entry_idx * sizeof(struct tree_map_entry), sizeof(*p), p);
	}
	else { //if we did not enter the loop - root change
		sobj_tx_add_range(p_oid, 0, sizeof(*p));
		p->key = 0;
		p->slot = new_node;
		sobj_tx_write(pop, p_oid, p);
	}

	if (p_slot_p_free != NULL) free(p_slot_p_free);
	free(new_node_p);
}

/*
 * ctree_map_insert_new -- allocates a new object and inserts it into the tree
 */
int
ctree_map_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size, unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg)
{
	int ret = 0;
	/*
	TX_BEGIN(pop) {
		PMEMoid n = pmemobj_tx_alloc(size, type_num);
		constructor(pop, pmemobj_direct(n), arg);
		ctree_map_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END
	*/
	TX_BEGIN(pop) {
		PMEMoid n = sobj_tx_alloc(size, type_num);
		void* n_temp = malloc(size);
		constructor(pop, n_temp, arg);
		sobj_tx_write(pop, n, n_temp);
		free(n_temp);
		ctree_map_insert(pop, map, key, n);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * ctree_map_insert -- inserts a new key-value pair into the map
 */
int
ctree_map_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value)
{
	/*
	struct tree_map_entry *p = &D_RW(map)->root;
	int ret = 0;

	// descend the path until a best matching key is found
	TOID(struct tree_map_node) node;
	while (!OID_IS_NULL(p->slot) &&
		OID_INSTANCEOF(p->slot, struct tree_map_node)) {
		TOID_ASSIGN(node, p->slot);
		p = &D_RW(node)->entries[BIT_IS_SET(key, D_RW(node)->diff)];
	}

	struct tree_map_entry e = {key, value};
	TX_BEGIN(pop) {
		if (p->key == 0 || p->key == key) {
			pmemobj_tx_add_range_direct(p, sizeof(*p));
			*p = e;
		} else {
			ctree_map_insert_leaf(pop, map, &D_RW(map)->root, e,
					find_crit_bit(p->key, key));
		}
	} TX_ONABORT {
		ret = 1;
	} TX_END
	*/
	struct ctree_map *ctree = sobj_read(pop, map, 1, NULL);
	struct tree_map_entry *p = &ctree->root;
	int ret = 0;
	// descend the path until a best matching key is found
	PMEMoid node = map; // holds oid to tree_map_node struct pointing to p
	struct tree_map_node *temp_node = NULL;
	void *p_slot_p;
	void *p_slot_p_free = NULL;
	uint64_t obj_size = 0;
	int entry_idx;

	if (!OID_IS_NULL(p->slot)) {
		p_slot_p = sobj_read(pop, p->slot, 1, &obj_size);
	
		while (obj_size == sizeof(struct tree_map_node)) {
			node = p->slot;

			temp_node = (struct tree_map_node*)p_slot_p;
			entry_idx = BIT_IS_SET(key, temp_node->diff);
			p = &temp_node->entries[entry_idx];

			if (p_slot_p_free != NULL) free(p_slot_p_free);

			p_slot_p_free = p_slot_p;
			
			if (!OID_IS_NULL(p->slot)) {
				p_slot_p = sobj_read(pop, p->slot, 1, &obj_size);
			}
			else
				break;
		}
		free(p_slot_p);
	}

	struct tree_map_entry e = {key, value};
	TX_BEGIN(pop) {
		if (p->key == 0 || p->key == key) {
			if (OID_EQUALS(node, map)){ //if root has to be updated
				sobj_tx_add_range(node, 0, sizeof(*p));			
				sobj_tx_write(pop, node, &e);
			}
			else {
				if (p->key == key) {
					struct tree_map_node *node_p = sobj_read(pop, node, 1, NULL);
					if (!OID_EQUALS(node_p->entries[entry_idx].slot, e.slot)) {
						sobj_tx_add_range(node, offsetof(struct tree_map_node, entries) +
									entry_idx * sizeof(struct tree_map_entry), sizeof(*p));		
						sobj_tx_write_part(pop, node, offsetof(struct tree_map_node, entries) +
										entry_idx * sizeof(struct tree_map_entry),
										sizeof(*p), &e);
						sobj_tx_free(node_p->entries[entry_idx].slot); // free the value to be updated
					}
					free(node_p);
				}
				else {
					sobj_tx_add_range(node, offsetof(struct tree_map_node, entries) +
									entry_idx * sizeof(struct tree_map_entry), sizeof(*p));		
					sobj_tx_write_part(pop, node, offsetof(struct tree_map_node, entries) +
									entry_idx * sizeof(struct tree_map_entry),
									sizeof(*p), &e);
				}
				
			}
		} 
		else {
			ctree_map_insert_leaf(pop, map, &ctree->root, e,
					find_crit_bit(p->key, key));
		}
	} TX_ONABORT {
		if (p_slot_p_free != NULL) free(p_slot_p_free);
		free(ctree);
		ret = 1;
	} TX_END

	if (p_slot_p_free != NULL) free(p_slot_p_free);
	free(ctree);
	return ret;
}

/*
 * ctree_map_get_leaf -- (internal) searches for a leaf of the key
 */
static struct tree_map_entry *
ctree_map_get_leaf(PMEMobjpool *pop, PMEMoid map, uint64_t key,
	struct tree_map_entry **parent, PMEMoid *mode_node_oid, uint64_t *mode_node_offset)
{
	/*
	struct tree_map_entry *n = &D_RW(map)->root;
	struct tree_map_entry *p = NULL;

	TOID(struct tree_map_node) node;
	while (!OID_IS_NULL(n->slot) &&
				OID_INSTANCEOF(n->slot, struct tree_map_node)) {
		TOID_ASSIGN(node, n->slot);

		p = n;
		n = &D_RW(node)->entries[BIT_IS_SET(key, D_RW(node)->diff)];
	}

	if (n->key == key) {
		if (parent)
			*parent = p;

		return n;
	}
	*/
	struct ctree_map *ctree = sobj_read(pop, map, 1, NULL);

	struct tree_map_entry *n = &ctree->root;
	struct tree_map_node *n_node = NULL; //shows in which decrypted node, n belongs to
	struct tree_map_entry *p = NULL;
	struct tree_map_node *p_node = NULL; //shows in which decrypted node, p belongs to

	uint64_t n_off = 0;
	PMEMoid n_node_oid = map;
	uint64_t p_off = 0;	
	PMEMoid p_node_oid = OID_NULL;

	uint64_t obj_size;
	if (!OID_IS_NULL(n->slot)) {
		void* n_slot_p = sobj_read(pop, n->slot, 1, &obj_size);

		while (obj_size == sizeof(struct tree_map_node)){
			//node = n->slot;
			struct tree_map_node* node_temp = (struct tree_map_node*)n_slot_p;
			
			if (p_node != NULL) free(p_node);

			p_node_oid = n_node_oid;
			p_off = n_off;
			p_node = n_node;
			p = n;
			
			n_node = node_temp;
			n_node_oid = n->slot;
			n = &node_temp->entries[BIT_IS_SET(key, node_temp->diff)];
			n_off = offsetof(struct tree_map_node, entries) + BIT_IS_SET(key, node_temp->diff) * sizeof(struct tree_map_entry);
			
			if (!OID_IS_NULL(n->slot))
				n_slot_p = sobj_read(pop, n->slot, 1, &obj_size);
			else
				break;
		}
		free(n_slot_p);
		if (p_node != NULL) free(p_node);
	}
	
	
	if (n->key == key) {
		if (parent) {
			if (p != NULL) {
				struct tree_map_entry *ret_parent = malloc(sizeof(struct tree_map_entry));
				memcpy(ret_parent, p, sizeof(struct tree_map_entry)); // extra copy which should be freed!
				*mode_node_offset = p_off;
				*mode_node_oid = p_node_oid;
				*parent = ret_parent;
			}
			else {
				*parent = NULL;
				*mode_node_offset = n_off;
				*mode_node_oid = n_node_oid;
			}

		}
		struct tree_map_entry *ret_n = malloc(sizeof(struct tree_map_entry));
		memcpy(ret_n, n, sizeof(struct tree_map_entry)); // extra copy which should be freed!
		if (n_node!=NULL) free(n_node);
		free(ctree);
		return ret_n;
	}

	if (n_node!=NULL) free(n_node);
	free(ctree);
	return NULL;
}

/*
 * ctree_map_remove_free -- removes and frees an object from the tree
 */
int
ctree_map_remove_free(PMEMobjpool *pop, PMEMoid map,
		uint64_t key)
{
	int ret = 0;

	TX_BEGIN(pop) {
		PMEMoid val = ctree_map_remove(pop, map, key);
		sobj_tx_free(val);
		//pmemobj_tx_free(val);
	} TX_ONABORT {
		ret = 1;
	} TX_END

	return ret;
}

/*
 * ctree_map_remove -- removes key-value pair from the map
 */
PMEMoid
ctree_map_remove(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	/*
	struct tree_map_entry *parent = NULL;
	struct tree_map_entry *leaf = ctree_map_get_leaf(pop, map, key, &parent);
	if (leaf == NULL)
		return OID_NULL;

	PMEMoid ret = leaf->slot;

	if (parent == NULL) { // root
		TX_BEGIN(pop) {
			pmemobj_tx_add_range_direct(leaf, sizeof(*leaf));
			leaf->key = 0;
			leaf->slot = OID_NULL;
		} TX_END
	} else {
	*/
		/*
		 * In this situation:
		 *	 parent
		 *	/     \
		 *   LEFT   RIGHT
		 * there's no point in leaving the parent internal node
		 * so it's swapped with the remaining node and then also freed.
		 */
	/*
		TX_BEGIN(pop) {
			struct tree_map_entry *dest = parent;
			TOID(struct tree_map_node) node;
			TOID_ASSIGN(node, parent->slot);
			pmemobj_tx_add_range_direct(dest, sizeof(*dest));
			*dest = D_RW(node)->entries[
				D_RO(node)->entries[0].key == leaf->key];;

			TX_FREE(node);
		} TX_END
	}
	*/
	PMEMoid mod_node_oid = OID_NULL; // oid of the node that is going to be modified
	uint64_t mod_node_offset = 0; // offset inside the node that is going to be modified
	struct tree_map_entry *parent = NULL;
	struct tree_map_entry *leaf = ctree_map_get_leaf(pop, map, key, &parent, &mod_node_oid, &mod_node_offset);
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
			sobj_tx_write_part(pop, mod_node_oid, mod_node_offset, sizeof(*leaf), leaf);
			//pmemobj_tx_add_range_direct(leaf, sizeof(*leaf));
			//leaf->key = 0;
			//leaf->slot = OID_NULL;
			free(leaf);
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
	
		TX_BEGIN(pop) {
			struct tree_map_entry *dest = parent;
			PMEMoid node = parent->slot;
			//TOID(struct tree_map_node) node;
			//TOID_ASSIGN(node, parent->slot);
			//mod_node_oid is equal to parent_node_ oid
			//mod_node_offset is equal to offset of the parent entry in its node object
			sobj_tx_add_range(mod_node_oid, mod_node_offset, sizeof(*dest));
			struct tree_map_node *temp_node = sobj_read(pop, node, 1, NULL);
			sobj_tx_write_part(pop, mod_node_oid, mod_node_offset, 
								sizeof(struct tree_map_entry),
								&temp_node->entries[temp_node->entries[0].key == leaf->key]);

			sobj_tx_free(node);

			//pmemobj_tx_add_range_direct(dest, sizeof(*dest));
			//*dest = D_RW(node)->entries[
			//	D_RO(node)->entries[0].key == leaf->key];
			//TX_FREE(node);

			free(leaf);
			free(parent);
			free(temp_node);
			
		} TX_END
	}
	
	return ret;
}

/*
 * ctree_map_get -- searches for a value of the key
 */
PMEMoid
ctree_map_get(PMEMobjpool *pop, PMEMoid map, uint64_t key)
{
	struct tree_map_entry *entry = ctree_map_get_leaf(pop, map, key, NULL, NULL, NULL);
	PMEMoid ret;
	if (entry != NULL) {
		ret = entry->slot;
		free(entry);
	}
	else {
		ret = OID_NULL;
	}
	return ret;
	//return entry ? entry->slot : OID_NULL;
}

/*
 * ctree_map_lookup -- searches if a key exists
 */
int
ctree_map_lookup(PMEMobjpool *pop, PMEMoid map,
		uint64_t key)
{
	struct tree_map_entry *entry = ctree_map_get_leaf(pop, map, key, NULL, NULL, NULL);
	if (entry != NULL) {
		free(entry);
		return 1;
	}
	else {
		return 0;
	}
	//return entry != NULL;
}

/*
 * ctree_map_foreach_node -- (internal) recursively traverses tree
 */
static int
ctree_map_foreach_node(PMEMobjpool *pop, struct tree_map_entry e,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	int ret = 0;
	void *e_slot_p = NULL;
	uint64_t obj_size;
	if (!OID_IS_NULL(e.slot))
		e_slot_p = sobj_read(pop, e.slot, 1, &obj_size);
	else
		obj_size = 0;

	if (obj_size == sizeof(struct tree_map_node)) {
		if (ctree_map_foreach_node(pop, ((struct tree_map_node*)e_slot_p)->entries[0],
					cb, arg) == 0)
			ctree_map_foreach_node(pop, ((struct tree_map_node*)e_slot_p)->entries[1], cb, arg);
	} else { /* leaf */
		ret = cb(e.key, e.slot, arg);
	}
	if (e_slot_p != NULL) free(e_slot_p);
	/*
	if (OID_INSTANCEOF(e.slot, struct tree_map_node)) {
		TOID(struct tree_map_node) node;
		TOID_ASSIGN(node, e.slot);

		if (ctree_map_foreach_node(D_RO(node)->entries[0],
					cb, arg) == 0)
			ctree_map_foreach_node(D_RO(node)->entries[1], cb, arg);
	} else { // leaf
		ret = cb(e.key, e.slot, arg);
	}
	*/
	return ret;
}

/*
 * ctree_map_foreach -- initiates recursive traversal
 */
int
ctree_map_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	struct ctree_map *ctree = sobj_read(pop, map, 1, NULL);
	if (OID_IS_NULL(ctree->root.slot)) {
		free(ctree);
		return 0;
	}

	int ret = ctree_map_foreach_node(pop, ctree->root, cb, arg);
	free(ctree);
	return ret;

	/*
	if (OID_IS_NULL(D_RO(map)->root.slot))
		return 0;

	return ctree_map_foreach_node(D_RO(map)->root, cb, arg);
	*/
}

/*
 * ctree_map_is_empty -- checks whether the tree map is empty
 */
int
ctree_map_is_empty(PMEMobjpool *pop, PMEMoid map)
{
	struct ctree_map *ctree = sobj_read(pop, map, 1, NULL);
	int ret = (ctree->root.key == 0);
	free(ctree);
	return ret;
	//return D_RO(map)->root.key == 0;
}

/*
 * ctree_map_check -- check if given persistent object is a tree map
 */
int
ctree_map_check(PMEMobjpool *pop, PMEMoid map)
{
	return OID_IS_NULL(map);
	//return TOID_IS_NULL(map) || !TOID_VALID(map);
}
