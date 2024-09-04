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

/* integer hash set implementation which uses only transaction APIs */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>

#include <libpmemobj.h>
#include <libanchor.h>
#include "hashmap_anchor_tx_opt.h"
#include "hashmap_internal.h"

/* layout definition */
TOID_DECLARE(struct buckets, HASHMAP_TX_TYPE_OFFSET + 1);
TOID_DECLARE(struct entry, HASHMAP_TX_TYPE_OFFSET + 2);

#define VALUE_SIZE 128

struct entry {
	uint64_t key;
	PMEMoid value;

	/* next entry list pointer */
	PMEMoid next;
};

struct buckets {
	/* number of buckets */
	size_t nbuckets;
	/* array of lists */
	PMEMoid bucket[];
};

struct hashmap_tx {
	/* random number generator seed */
	uint32_t seed;

	/* hash function coefficients */
	uint32_t hash_fun_a;
	uint32_t hash_fun_b;
	uint64_t hash_fun_p;

	/* number of values inserted */
	uint64_t count;

	/* buckets */
	PMEMoid buckets;
};

/*
 * create_hashmap -- hashmap initializer
 */
static void
create_hashmap(PMEMobjpool *pop, PMEMoid hashmap, uint32_t seed)
{
	size_t len = INIT_BUCKETS_NUM;
	size_t sz = sizeof(struct buckets) +
			len * sizeof(PMEMoid);
	TX_BEGIN(pop) {
		sobj_tx_add_range(hashmap, 0, sizeof(struct hashmap_tx));
		struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
		hashmap_p->seed = seed;
		do {
			hashmap_p->hash_fun_a = (uint32_t)rand();
		} while (hashmap_p->hash_fun_a == 0);

		hashmap_p->hash_fun_b = (uint32_t)rand();
		hashmap_p->hash_fun_p = HASH_FUNC_COEFF_P;
 
		hashmap_p->buckets = sobj_tx_zalloc(sz, HASHMAP_TX_TYPE_OFFSET + 1);
		
		struct buckets *buckets_p = sobj_direct(pop, hashmap_p->buckets);
		buckets_p->nbuckets = len;
		
	} TX_ONABORT {
		fprintf(stderr, "%s: transaction aborted: %s\n", __func__,
			pmemobj_errormsg());
		abort();
	} TX_END
}

/*
 * hash -- the simplest hashing function,
 * see https://en.wikipedia.org/wiki/Universal_hashing#Hashing_integers
 */
static uint64_t
hash(PMEMobjpool* pop, const PMEMoid *hashmap,
	const PMEMoid *buckets, uint64_t value)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, *hashmap);
	struct buckets *buckets_p = sobj_direct(pop, *buckets);

	uint32_t a = hashmap_p->hash_fun_a;
	uint32_t b = hashmap_p->hash_fun_b;
	uint64_t p = hashmap_p->hash_fun_p;
	size_t len = buckets_p->nbuckets;

	return ((a * value + b) % p) % len;
}

/*
 * hm_tx_opt_rebuild -- rebuilds the hashmap with a new number of buckets
 */
static void
hm_tx_opt_rebuild(PMEMobjpool *pop, PMEMoid hashmap, size_t new_len)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	struct buckets *buckets_old = sobj_direct(pop, hashmap_p->buckets);

	if (new_len == 0)
		new_len = buckets_old->nbuckets;

	size_t sz_old = sizeof(struct buckets) +
			buckets_old->nbuckets *
			sizeof(PMEMoid);
	size_t sz_new = sizeof(struct buckets) +
			new_len * sizeof(PMEMoid);

	TX_BEGIN(pop) {
		sobj_tx_add_range(hashmap, offsetof(struct hashmap_tx, buckets), sizeof(PMEMoid));
		PMEMoid buckets_new = sobj_tx_zalloc(sz_new, HASHMAP_TX_TYPE_OFFSET + 1);
		struct buckets *buckets_new_p = sobj_direct(pop, buckets_new);
		buckets_new_p->nbuckets = new_len;
		
		sobj_tx_add_range(hashmap_p->buckets, 0, sz_old);

		for (size_t i = 0; i < buckets_old->nbuckets; ++i) {
			while (!OID_IS_NULL(buckets_old->bucket[i])) {
				PMEMoid en = buckets_old->bucket[i];
				struct entry *en_temp = sobj_direct(pop, en);

				uint64_t h = hash(pop, &hashmap, &buckets_new,
						en_temp->key);

                buckets_old->bucket[i] = en_temp->next;

				sobj_tx_add_range(en, offsetof(struct entry, next), sizeof(PMEMoid));

                en_temp->next = buckets_new_p->bucket[h];

                buckets_new_p->bucket[h] = en;
			}
		}
        PMEMoid buckets_old_oid = hashmap_p->buckets;
        hashmap_p->buckets = buckets_new;
		sobj_tx_free(buckets_old_oid);
        
	} TX_ONABORT {
		fprintf(stderr, "%s: transaction aborted: %s\n", __func__,
			pmemobj_errormsg());
		//We don't need to do anything here, because everything is
		//consistent. The only thing affected is performance.	
	} TX_END
}

/*
 * hm_tx_opt_insert -- inserts specified value into the hashmap,
 * returns:
 * - 0 if successful,
 * - 0 if value already existed,
 * - -1 if something bad happened
 */
int
hm_tx_opt_insert(PMEMobjpool *pop, PMEMoid hashmap,
	uint64_t key, PMEMoid value)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	struct buckets *buckets = sobj_direct(pop, hashmap_p->buckets);

	PMEMoid var = OID_NULL;
	struct entry *var_p;

	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);
	int num = 0;

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_direct(pop, var);
		if (var_p->key == key) {
			//update the value
			if (!OID_EQUALS(var_p->value, value)) {
				sobj_tx_add_range(var, offsetof(struct entry, value), sizeof(PMEMoid));
				sobj_tx_free(var_p->value);
				var_p->value = value;
			}
			return 0;
		}
		num++;
		var = var_p->next;
	}

	int ret = 0;
	TX_BEGIN(pop) {
		sobj_tx_add_range(hashmap_p->buckets, sizeof(struct buckets) + h * sizeof(PMEMoid), sizeof(PMEMoid));
		sobj_tx_add_range(hashmap, offsetof(struct hashmap_tx, count), sizeof(uint64_t));

		PMEMoid e = sobj_tx_alloc(sizeof(struct entry), HASHMAP_TX_TYPE_OFFSET + 2);
		struct entry *e_temp = sobj_direct(pop, e);
		e_temp->key = key;
		e_temp->value = value;
		e_temp->next = buckets->bucket[h];
		buckets->bucket[h] = e;
		hashmap_p->count++;
		num++;
	} TX_ONABORT {
		fprintf(stderr, "transaction aborted: %s\n",
			pmemobj_errormsg());
		ret = -1;
	} TX_END

	if (ret) {
		return ret;
	}

	hashmap_p = sobj_direct(pop, hashmap);
	buckets = sobj_direct(pop, hashmap_p->buckets);
	if (num > MAX_HASHSET_THRESHOLD ||
			(num > MIN_HASHSET_THRESHOLD &&
			hashmap_p->count > 2 * buckets->nbuckets))
		hm_tx_opt_rebuild(pop, hashmap, buckets->nbuckets * 2);

	return 0;
}

/*
 * hm_tx_opt_update -- update specified value if key exists in the hashmap,
 * returns:
 * - 0 if successful,
 * - 1 if key not found,
 * - -1 if something bad happened
 */
int
hm_tx_opt_update(PMEMobjpool *pop, PMEMoid hashmap,
	uint64_t key, char value_data[])
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	struct buckets *buckets = sobj_direct(pop, hashmap_p->buckets);

	PMEMoid var = OID_NULL;
	struct entry *var_p;

	//look for the key
	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_direct(pop, var);
		if (var_p->key == key)
			break;
		var = var_p->next;
	}

	//key not found return 1
	if (OID_IS_NULL(var))
		return 1;
	
	//else update its value transactionally
	int ret = 0;
	
	TX_BEGIN(pop) {
		sobj_tx_add_range(var_p->value, 0, VALUE_SIZE * sizeof(char));
		void *var_p_value = sobj_direct(pop, var_p->value);
		memcpy(var_p_value, value_data, VALUE_SIZE * sizeof(char));
	} TX_ONABORT {
		fprintf(stderr, "update transaction aborted: %s\n",
			pmemobj_errormsg());
		ret = -1;
	} TX_END
	
	return ret;
}

/*
 * hm_tx_opt_remove -- removes specified value from the hashmap,
 * returns:
 * - key's value if successful,
 * - OID_NULL if value didn't exist or if something bad happened
 */
PMEMoid
hm_tx_opt_remove(PMEMobjpool *pop, PMEMoid hashmap, uint64_t key)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	struct buckets *buckets = sobj_direct(pop, hashmap_p->buckets);

	PMEMoid var, prev = OID_NULL;
	struct entry *var_p;

	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_direct(pop, var);
		if (var_p->key == key) {
			break;
		}
		prev = var;
		var = var_p->next;
	}

	if (OID_IS_NULL(var))
		goto not_found;
	
	int ret = 0;

	PMEMoid retoid = var_p->value;
	TX_BEGIN(pop) {
		if (OID_IS_NULL(prev))
			sobj_tx_add_range(hashmap_p->buckets, sizeof(struct buckets) + h * sizeof(PMEMoid), sizeof(PMEMoid));
		else
			sobj_tx_add_range(prev, offsetof(struct entry, next), sizeof(PMEMoid));
		
		sobj_tx_add_range(hashmap, offsetof(struct hashmap_tx, count), sizeof(uint64_t));

		if (OID_IS_NULL(prev)) {
            buckets->bucket[h] = var_p->next;
		}
		else {
            struct entry *prev_p = sobj_direct(pop, prev);
            prev_p->next = var_p->next;
		}

		hashmap_p->count--;
		
		sobj_tx_free(var);

	} TX_ONABORT {
		fprintf(stderr, "transaction aborted: %s\n",
			pmemobj_errormsg());
		ret = -1;
	} TX_END

	if (ret) {
		goto not_found;
	}

	hashmap_p = sobj_direct(pop, hashmap);
	buckets = sobj_direct(pop, hashmap_p->buckets);
	if (hashmap_p->count < buckets->nbuckets)
		hm_tx_opt_rebuild(pop, hashmap, buckets->nbuckets / 2);

	return retoid;

not_found:
	return OID_NULL;
}

/*
 * hm_tx_opt_foreach -- prints all values from the hashmap
 */
int
hm_tx_opt_foreach(PMEMobjpool *pop, PMEMoid hashmap,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	struct buckets *buckets = sobj_direct(pop, hashmap_p->buckets);
	PMEMoid var;
	struct entry *var_p;
	
	int ret = 0;
	for (size_t i = 0; i < buckets->nbuckets; ++i) {
		if (OID_IS_NULL(buckets->bucket[i])){
            continue;
        }

		for (var = buckets->bucket[i]; !OID_IS_NULL(var); ) {
			var_p = sobj_direct(pop, var);
			ret = cb(var_p->key, var_p->value, arg);
			var = var_p->next;
			if (ret)
				break;
		}
	}
	return ret;
}

/*
 * hm_tx_opt_debug -- prints complete hashmap state
 * ANCHOR : not needed for benchmarking
 */
static void
hm_tx_opt_debug(PMEMobjpool *pop, PMEMoid hashmap, FILE *out)
{
	return;
}

/*
 * hm_tx_opt_get -- checks whether specified value is in the hashmap
 */
PMEMoid
hm_tx_opt_get(PMEMobjpool *pop, PMEMoid hashmap, uint64_t key)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	struct buckets *buckets = sobj_direct(pop, hashmap_p->buckets);
	
	PMEMoid var;
	struct entry *var_p;

	PMEMoid ret = OID_NULL;

	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_direct(pop, var);
		if (var_p->key == key) {
			ret = var_p->value;
			return ret;
		}
		var = var_p->next;
	}

	return ret;
}

/*
 * hm_tx_opt_lookup -- checks whether specified value exists
 */
int
hm_tx_opt_lookup(PMEMobjpool *pop, PMEMoid hashmap, uint64_t key)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	struct buckets *buckets = sobj_direct(pop, hashmap_p->buckets);
	
	PMEMoid var;
	struct entry *var_p;

	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_direct(pop, var);
		if (var_p->key == key) {
			return 1;
		}
		var = var_p->next;
	}			
	return 0;
}

/*
 * hm_tx_opt_count -- returns number of elements
 */
size_t
hm_tx_opt_count(PMEMobjpool *pop, PMEMoid hashmap)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	size_t ret = hashmap_p->count;
	return ret;
}

/*
 * hm_tx_opt_init -- recovers hashmap state, called after pmemobj_open
 */
int
hm_tx_opt_init(PMEMobjpool *pop, PMEMoid hashmap)
{
	struct hashmap_tx *hashmap_p = sobj_direct(pop, hashmap);
	srand(hashmap_p->seed);
	return 0;
}

/*
 * hm_tx_opt_create -- allocates new hashmap
 */
int
hm_tx_opt_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	struct hashmap_args *args = (struct hashmap_args *)arg;
	int ret = 0;
	TX_BEGIN(pop) {
		*map = sobj_tx_zalloc(sizeof(struct hashmap_tx), HASHMAP_TX_TYPE_OFFSET);		
		uint32_t seed = args ? args->seed : 0;
		create_hashmap(pop, *map, seed);
	} TX_ONABORT {
		ret = -1;
	} TX_END

	return ret;
}

/*
 * hm_tx_opt_check -- checks if specified persistent object is an
 * instance of hashmap
 */
int
hm_tx_opt_check(PMEMobjpool *pop, PMEMoid hashmap)
{
	return OID_IS_NULL(hashmap);
}

/*
 * hm_tx_opt_cmd -- execute cmd for hashmap
 * ANCHOR : not needed for benchmarking
 */
int
hm_tx_opt_cmd(PMEMobjpool *pop, PMEMoid hashmap,
		unsigned cmd, uint64_t arg)
{
	switch (cmd) {
		case HASHMAP_CMD_REBUILD:
			hm_tx_opt_rebuild(pop, hashmap, arg);
			return 0;
		case HASHMAP_CMD_DEBUG:
			if (!arg)
				return -EINVAL;
			hm_tx_opt_debug(pop, hashmap, (FILE *)arg);
			return 0;
		default:
			return -EINVAL;
	}
	return 0;
}
