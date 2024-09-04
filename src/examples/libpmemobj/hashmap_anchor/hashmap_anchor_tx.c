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
#include "hashmap_anchor_tx.h"
#include "hashmap_internal.h"

/* layout definition */
TOID_DECLARE(struct buckets, HASHMAP_TX_TYPE_OFFSET + 1);
TOID_DECLARE(struct entry, HASHMAP_TX_TYPE_OFFSET + 2);

struct entry {
	uint64_t key;
	PMEMoid value;

	/* next entry list pointer */
	//TOID(struct entry) next;
	PMEMoid next;
};

struct buckets {
	/* number of buckets */
	size_t nbuckets;
	/* array of lists */
	//TOID(struct entry) bucket[];
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
	//TOID(struct buckets) buckets;
	PMEMoid buckets;
};

/*
 * create_hashmap -- hashmap initializer
 */
static void
create_hashmap(PMEMobjpool *pop, PMEMoid hashmap, uint32_t seed)
{
	size_t len = INIT_BUCKETS_NUM;
	//size_t sz = sizeof(struct buckets) +
	//		len * sizeof(TOID(struct entry));
	size_t sz = sizeof(struct buckets) +
			len * sizeof(PMEMoid);
	TX_BEGIN(pop) {
		/*
		TX_ADD(hashmap);

		D_RW(hashmap)->seed = seed;
		do {
			D_RW(hashmap)->hash_fun_a = (uint32_t)rand();
		} while (D_RW(hashmap)->hash_fun_a == 0);
		D_RW(hashmap)->hash_fun_b = (uint32_t)rand();
		D_RW(hashmap)->hash_fun_p = HASH_FUNC_COEFF_P;

		D_RW(hashmap)->buckets = TX_ZALLOC(struct buckets, sz);
		D_RW(D_RW(hashmap)->buckets)->nbuckets = len;
		*/

		sobj_tx_add_range(hashmap, 0, sizeof(struct hashmap_tx));
		struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
		hashmap_p->seed = seed;
		do {
			hashmap_p->hash_fun_a = (uint32_t)rand();
		} while (hashmap_p->hash_fun_a == 0);

		hashmap_p->hash_fun_b = (uint32_t)rand();
		hashmap_p->hash_fun_p = HASH_FUNC_COEFF_P;
 
		hashmap_p->buckets = sobj_tx_zalloc(sz, HASHMAP_TX_TYPE_OFFSET + 1);
		
		//D_RW(D_RW(hashmap)->buckets)->nbuckets = len;
		//struct buckets *buckets_p = sobj_tx_read(pop, hashmap_p->buckets);
		//buckets_p->nbuckets = len;

		sobj_tx_write_part(pop, hashmap_p->buckets, offsetof(struct buckets, nbuckets), sizeof(len), &len);
		sobj_tx_write(pop, hashmap, hashmap_p);
		
		//free(buckets_p);
		free(hashmap_p);
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
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, *hashmap);
	struct buckets *buckets_p = sobj_tx_read(pop, *buckets);

	uint32_t a = hashmap_p->hash_fun_a;
	uint32_t b = hashmap_p->hash_fun_b;
	uint64_t p = hashmap_p->hash_fun_p;
	size_t len = buckets_p->nbuckets;

	free(hashmap_p);
	free(buckets_p);
	//uint32_t a = D_RO(*hashmap)->hash_fun_a;
	//uint32_t b = D_RO(*hashmap)->hash_fun_b;
	//uint64_t p = D_RO(*hashmap)->hash_fun_p;
	//size_t len = D_RO(*buckets)->nbuckets;

	return ((a * value + b) % p) % len;
}

/*
 * hm_tx_rebuild -- rebuilds the hashmap with a new number of buckets
 */
static void
hm_tx_rebuild(PMEMobjpool *pop, PMEMoid hashmap, size_t new_len)
{
	/*
	TOID(struct buckets) buckets_old = D_RO(hashmap)->buckets;

	if (new_len == 0)
		new_len = D_RO(buckets_old)->nbuckets;

	size_t sz_old = sizeof(struct buckets) +
			D_RO(buckets_old)->nbuckets *
			sizeof(TOID(struct entry));
	size_t sz_new = sizeof(struct buckets) +
			new_len * sizeof(TOID(struct entry));

	TX_BEGIN(pop) {
		TX_ADD_FIELD(hashmap, buckets);
		TOID(struct buckets) buckets_new =
				TX_ZALLOC(struct buckets, sz_new);
		D_RW(buckets_new)->nbuckets = new_len;
		pmemobj_tx_add_range(buckets_old.oid, 0, sz_old);

		for (size_t i = 0; i < D_RO(buckets_old)->nbuckets; ++i) {
			while (!TOID_IS_NULL(D_RO(buckets_old)->bucket[i])) {
				TOID(struct entry) en =
					D_RO(buckets_old)->bucket[i];
				uint64_t h = hash(&hashmap, &buckets_new,
						D_RO(en)->key);

				D_RW(buckets_old)->bucket[i] = D_RO(en)->next;

				TX_ADD_FIELD(en, next);
				D_RW(en)->next = D_RO(buckets_new)->bucket[h];
				D_RW(buckets_new)->bucket[h] = en;
			}
		}

		D_RW(hashmap)->buckets = buckets_new;
		TX_FREE(buckets_old);
	} TX_ONABORT {
		fprintf(stderr, "%s: transaction aborted: %s\n", __func__,
			pmemobj_errormsg());
		//We don't need to do anything here, because everything is
		//consistent. The only thing affected is performance.	
	} TX_END
	*/
	
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	struct buckets *buckets_old = sobj_tx_read(pop, hashmap_p->buckets);

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
		struct buckets *buckets_new_p = calloc(1, sz_new); //sobj_tx_read(pop, buckets_new);
		buckets_new_p->nbuckets = new_len;
		sobj_tx_write_part(pop, buckets_new, offsetof(struct buckets, nbuckets), sizeof(new_len), &new_len);
		//D_RW(buckets_new)->nbuckets = new_len;
		
		sobj_tx_add_range(hashmap_p->buckets, 0, sz_old);
		//pmemobj_tx_add_range(buckets_old.oid, 0, sz_old);

		for (size_t i = 0; i < buckets_old->nbuckets; ++i) {
			while (!OID_IS_NULL(buckets_old->bucket[i])) {
				//TOID(struct entry) en =
				//	D_RO(buckets_old)->bucket[i];

				PMEMoid en = buckets_old->bucket[i];
				struct entry *en_temp = sobj_tx_read(pop, en);

				uint64_t h = hash(pop, &hashmap, &buckets_new,
						en_temp->key);

				//D_RW(buckets_old)->bucket[i] = D_RO(en)->next;
				sobj_tx_write_part(pop, hashmap_p->buckets, sizeof(struct buckets) + i * sizeof(PMEMoid), sizeof(PMEMoid), &en_temp->next);
				buckets_old->bucket[i] = en_temp->next;

				//TX_ADD_FIELD(en, next);
				sobj_tx_add_range(en, offsetof(struct entry, next), sizeof(PMEMoid));

				//D_RW(en)->next = D_RO(buckets_new)->bucket[h];
				//D_RW(buckets_new)->bucket[h] = en;
				sobj_tx_write_part(pop, en, offsetof(struct entry, next), sizeof(PMEMoid), &buckets_new_p->bucket[h]);
				sobj_tx_write_part(pop, buckets_new, sizeof(struct buckets) + h * sizeof(PMEMoid), sizeof(PMEMoid), &en);
				buckets_new_p->bucket[h] = en;

				//	sobj_tx_write_part(pop, buckets_new_p->bucket[h], 0, sizeof(PMEMoid), &en);
				free(en_temp);
			}
		}
		//D_RW(hashmap)->buckets = buckets_new;
		sobj_tx_write_part(pop, hashmap, offsetof(struct hashmap_tx, buckets), sizeof(PMEMoid), &buckets_new);
		//TX_FREE(buckets_old);
		sobj_tx_free(hashmap_p->buckets); // still resembling buckets_old pmemoid
		free(buckets_new_p);
	} TX_ONABORT {
		fprintf(stderr, "%s: transaction aborted: %s\n", __func__,
			pmemobj_errormsg());
		free(buckets_old);
		free(hashmap_p);
		//We don't need to do anything here, because everything is
		//consistent. The only thing affected is performance.	
	} TX_END

	

	free(buckets_old);
	free(hashmap_p);
}

/*
 * hm_tx_insert -- inserts specified value into the hashmap,
 * returns:
 * - 0 if successful,
 * - 1 if value already existed,
 * - -1 if something bad happened
 */
int
hm_tx_insert(PMEMobjpool *pop, PMEMoid hashmap,
	uint64_t key, PMEMoid value)
{
	/*
	TOID(struct buckets) buckets = D_RO(hashmap)->buckets;
	TOID(struct entry) var;

	uint64_t h = hash(&hashmap, &buckets, key);
	int num = 0;

	for (var = D_RO(buckets)->bucket[h];
			!TOID_IS_NULL(var);
			var = D_RO(var)->next) {
		if (D_RO(var)->key == key)
			return 1;
		num++;
	}

	int ret = 0;
	TX_BEGIN(pop) {
		TX_ADD_FIELD(D_RO(hashmap)->buckets, bucket[h]);
		TX_ADD_FIELD(hashmap, count);

		TOID(struct entry) e = TX_NEW(struct entry);
		D_RW(e)->key = key;
		D_RW(e)->value = value;
		D_RW(e)->next = D_RO(buckets)->bucket[h];
		D_RW(buckets)->bucket[h] = e;
		D_RW(hashmap)->count++;
		num++;
		//printf("%" PRIu64 " %" PRIu64 "\n", key, value.off);
	} TX_ONABORT {
		fprintf(stderr, "transaction aborted: %s\n",
			pmemobj_errormsg());
		ret = -1;
	} TX_END

	if (ret)
		return ret;

	if (num > MAX_HASHSET_THRESHOLD ||
			(num > MIN_HASHSET_THRESHOLD &&
			D_RO(hashmap)->count > 2 * D_RO(buckets)->nbuckets))
		hm_tx_rebuild(pop, hashmap, D_RO(buckets)->nbuckets * 2);
	*/
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	struct buckets *buckets = sobj_tx_read(pop, hashmap_p->buckets);

	PMEMoid var = OID_NULL;
	struct entry *var_p;

	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);
	int num = 0;

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_tx_read(pop, var);
		if (var_p->key == key) {
			//update the value
			if (!OID_EQUALS(var_p->value, value)) {
				sobj_tx_add_range(var, offsetof(struct entry, value), sizeof(PMEMoid));
				sobj_tx_free(var_p->value);
				var_p->value = value;
				sobj_tx_write_part(pop, var, offsetof(struct entry, value), sizeof(PMEMoid), &var_p->value);
			}
			free(hashmap_p);
			free(buckets);
			free(var_p);
			return 0;
		}
		num++;
		var = var_p->next;
		free(var_p);
	}

	int ret = 0;
	TX_BEGIN(pop) {
		sobj_tx_add_range(hashmap_p->buckets, sizeof(struct buckets) + h * sizeof(PMEMoid), sizeof(PMEMoid));
		sobj_tx_add_range(hashmap, offsetof(struct hashmap_tx, count), sizeof(uint64_t));

		PMEMoid e = sobj_tx_alloc(sizeof(struct entry), HASHMAP_TX_TYPE_OFFSET + 2);
		struct entry *e_temp = sobj_tx_read(pop, e);
		e_temp->key = key;
		e_temp->value = value;
		e_temp->next = buckets->bucket[h];
		buckets->bucket[h] = e;
		hashmap_p->count++;
		num++;

		sobj_tx_write(pop, e, e_temp);
		sobj_tx_write_part(pop, hashmap_p->buckets, sizeof(struct buckets) + h * sizeof(PMEMoid), sizeof(PMEMoid), &e);
		sobj_tx_write_part(pop, hashmap, offsetof(struct hashmap_tx, count), sizeof(uint64_t), &hashmap_p->count);
		free(e_temp);
		//printf("%" PRIu64 " %" PRIu64 "\n", key, value.off);
	} TX_ONABORT {
		fprintf(stderr, "transaction aborted: %s\n",
			pmemobj_errormsg());
		free(hashmap_p);
		free(buckets);
		ret = -1;
	} TX_END

	if (ret) {
		free(hashmap_p);
		free(buckets);
		return ret;
	}

	if (num > MAX_HASHSET_THRESHOLD ||
			(num > MIN_HASHSET_THRESHOLD &&
			hashmap_p->count > 2 * buckets->nbuckets))
		hm_tx_rebuild(pop, hashmap, buckets->nbuckets * 2);

	free(hashmap_p);
	free(buckets);
	return 0;
}

/*
 * hm_tx_update -- update specified value if key exists in the hashmap,
 * returns:
 * - 0 if successful,
 * - 1 if key not found,
 * - -1 if something bad happened
 */
int
hm_tx_update(PMEMobjpool *pop, PMEMoid hashmap,
	uint64_t key, char value_data[])
{
	/*
	TOID(struct buckets) buckets = D_RO(hashmap)->buckets;
	TOID(struct entry) var = TOID_NULL(struct entry);

	//look for the key
	uint64_t h = hash(&hashmap, &buckets, key);
	for (var = D_RO(buckets)->bucket[h];
			!TOID_IS_NULL(var);
			var = D_RO(var)->next) {
		if (D_RO(var)->key == key)
			break;
	}

	//key not found return 1
	if (TOID_IS_NULL(var))
		return 1;
	
	//else update its value transactionally
	int ret = 0;
	char* value_data_tmp;
    value_data_tmp = (char*)malloc(128* sizeof(char));
	TX_BEGIN(pop) {
		value_data_tmp = pmemobj_direct(D_RW(var)->value);
		TX_ADD_DIRECT(value_data_tmp);
		strcpy(value_data_tmp, value_data);		
	} TX_ONABORT {
		fprintf(stderr, "update transaction aborted: %s\n",
			pmemobj_errormsg());
		ret = -1;
	} TX_END
	*/
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	struct buckets *buckets = sobj_tx_read(pop, hashmap_p->buckets);

	PMEMoid var = OID_NULL;
	struct entry *var_p;

	//look for the key
	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_tx_read(pop, var);
		if (var_p->key == key)
			break;
		var = var_p->next;
		free(var_p);
	}

	//key not found return 1
	if (OID_IS_NULL(var))
		return 1;
	
	//else update its value transactionally
	int ret = 0;
	//char* value_data_tmp;
    //value_data_tmp = (char*)malloc(128* sizeof(char));
	
	TX_BEGIN(pop) {
		//value_data_tmp = pmemobj_direct(D_RW(var)->value);
		//TX_ADD_DIRECT(value_data_tmp);
		//strcpy(value_data_tmp, value_data);	
		sobj_tx_add_range(var_p->value, 0, 128 * sizeof(char));
		sobj_tx_write(pop, var_p->value, value_data);	
	} TX_ONABORT {
		fprintf(stderr, "update transaction aborted: %s\n",
			pmemobj_errormsg());
		free(var_p);
		free(buckets);
		free(hashmap_p);
		ret = -1;
	} TX_END
	
	free(var_p);
	free(buckets);
	free(hashmap_p);
	
	return ret;
}

/*
 * hm_tx_remove -- removes specified value from the hashmap,
 * returns:
 * - key's value if successful,
 * - OID_NULL if value didn't exist or if something bad happened
 */
PMEMoid
hm_tx_remove(PMEMobjpool *pop, PMEMoid hashmap, uint64_t key)
{
	/*
	TOID(struct buckets) buckets = D_RO(hashmap)->buckets;
	TOID(struct entry) var, prev = TOID_NULL(struct entry);

	uint64_t h = hash(&hashmap, &buckets, key);
	for (var = D_RO(buckets)->bucket[h];
			!TOID_IS_NULL(var);
			prev = var, var = D_RO(var)->next) {
		if (D_RO(var)->key == key)
			break;
	}

	if (TOID_IS_NULL(var))
		return OID_NULL;
	int ret = 0;

	PMEMoid retoid = D_RO(var)->value;
	TX_BEGIN(pop) {
		if (TOID_IS_NULL(prev))
			TX_ADD_FIELD(D_RO(hashmap)->buckets, bucket[h]);
		else
			TX_ADD_FIELD(prev, next);
		TX_ADD_FIELD(hashmap, count);

		if (TOID_IS_NULL(prev))
			D_RW(buckets)->bucket[h] = D_RO(var)->next;
		else
			D_RW(prev)->next = D_RO(var)->next;
		D_RW(hashmap)->count--;
		TX_FREE(var);
	} TX_ONABORT {
		fprintf(stderr, "transaction aborted: %s\n",
			pmemobj_errormsg());
		ret = -1;
	} TX_END

	if (ret)
		return OID_NULL;

	if (D_RO(hashmap)->count < D_RO(buckets)->nbuckets)
		hm_tx_rebuild(pop, hashmap, D_RO(buckets)->nbuckets / 2);
	*/
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	struct buckets *buckets = sobj_tx_read(pop, hashmap_p->buckets);

	PMEMoid var, prev = OID_NULL;
	struct entry *var_p;

	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_tx_read(pop, var);
		if (var_p->key == key) {
			break;
		}
		prev = var;
		var = var_p->next;
		free(var_p);
	}

	if (OID_IS_NULL(var))
		goto not_found;
	
	int ret = 0;

	PMEMoid retoid = var_p->value;
	TX_BEGIN(pop) {
		if (OID_IS_NULL(prev))
			sobj_tx_add_range(hashmap_p->buckets, sizeof(struct buckets) + h * sizeof(PMEMoid), sizeof(PMEMoid));
			//TX_ADD_FIELD(D_RO(hashmap)->buckets, bucket[h]);
		else
			sobj_tx_add_range(prev, offsetof(struct entry, next), sizeof(PMEMoid));
			//TX_ADD_FIELD(prev, next);
		
		sobj_tx_add_range(hashmap, offsetof(struct hashmap_tx, count), sizeof(uint64_t));
		//TX_ADD_FIELD(hashmap, count);

		if (OID_IS_NULL(prev)) {
			//buckets->bucket[h] = var_p->next;
			sobj_tx_write_part(pop, hashmap_p->buckets, sizeof(struct buckets) + h * sizeof(PMEMoid), sizeof(PMEMoid), &var_p->next);
			//sobj_tx_write(pop, buckets->bucket[h], &var_p->next);
		}
		else {
			//D_RW(prev)->next = var_p->next;
			sobj_tx_write_part(pop, prev, offsetof(struct entry, next), sizeof(PMEMoid), &var_p->next);
		}

		//D_RW(hashmap)->count--;
		hashmap_p->count--;
		sobj_tx_write_part(pop, hashmap, offsetof(struct hashmap_tx, count), sizeof(uint64_t), &hashmap_p->count);
		
		sobj_tx_free(var);

	} TX_ONABORT {
		fprintf(stderr, "transaction aborted: %s\n",
			pmemobj_errormsg());
		free(var_p);
		free(hashmap_p);
		free(buckets);	
		ret = -1;
	} TX_END

	if (ret) {
		free(var_p);
		goto not_found;
	}

	if (hashmap_p->count < buckets->nbuckets)
		hm_tx_rebuild(pop, hashmap, buckets->nbuckets / 2);
	
	free(var_p);
	free(hashmap_p);
	free(buckets);
	return retoid;

not_found:
	free(hashmap_p);
	free(buckets);
	return OID_NULL;
}

/*
 * hm_tx_foreach -- prints all values from the hashmap
 */
int
hm_tx_foreach(PMEMobjpool *pop, PMEMoid hashmap,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg)
{
	/*
	TOID(struct buckets) buckets = D_RO(hashmap)->buckets;
	TOID(struct entry) var;

	int ret = 0;
	for (size_t i = 0; i < D_RO(buckets)->nbuckets; ++i) {
		if (TOID_IS_NULL(D_RO(buckets)->bucket[i]))
			continue;

		for (var = D_RO(buckets)->bucket[i]; !TOID_IS_NULL(var);
				var = D_RO(var)->next) {
			ret = cb(D_RO(var)->key, D_RO(var)->value, arg);
			if (ret)
				break;
		}
	}
	*/
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	struct buckets *buckets = sobj_tx_read(pop, hashmap_p->buckets);
	PMEMoid var;
	struct entry *var_p;
	
	int ret = 0;
	for (size_t i = 0; i < buckets->nbuckets; ++i) {
		if (OID_IS_NULL(buckets->bucket[i])) {
			continue;
		}

		for (var = buckets->bucket[i]; !OID_IS_NULL(var); ) {
			var_p = sobj_tx_read(pop, var);
			ret = cb(var_p->key, var_p->value, arg);
			var = var_p->next;
			free(var_p);
			if (ret)
				break;
		}
	}

	free(hashmap_p);
	free(buckets);
	return ret;
}

/*
 * hm_tx_debug -- prints complete hashmap state
 * ANCHOR : not needed for benchmarking
 */
static void
hm_tx_debug(PMEMobjpool *pop, PMEMoid hashmap, FILE *out)
{
	/*
	TOID(struct buckets) buckets = D_RO(hashmap)->buckets;
	TOID(struct entry) var;

	fprintf(out, "a: %u b: %u p: %" PRIu64 "\n", D_RO(hashmap)->hash_fun_a,
		D_RO(hashmap)->hash_fun_b, D_RO(hashmap)->hash_fun_p);
	fprintf(out, "count: %" PRIu64 ", buckets: %zu\n",
		D_RO(hashmap)->count, D_RO(buckets)->nbuckets);

	for (size_t i = 0; i < D_RO(buckets)->nbuckets; ++i) {
		if (TOID_IS_NULL(D_RO(buckets)->bucket[i]))
			continue;

		int num = 0;
		fprintf(out, "%zu: ", i);
		for (var = D_RO(buckets)->bucket[i]; !TOID_IS_NULL(var);
				var = D_RO(var)->next) {
			fprintf(out, "%" PRIu64 " ", D_RO(var)->key);
			num++;
		}
		fprintf(out, "(%d)\n", num);
	}
	*/
	return;
}

/*
 * hm_tx_get -- checks whether specified value is in the hashmap
 */
PMEMoid
hm_tx_get(PMEMobjpool *pop, PMEMoid hashmap, uint64_t key)
{
	/*
	TOID(struct buckets) buckets = D_RO(hashmap)->buckets;
	TOID(struct entry) var;

	uint64_t h = hash(&hashmap, &buckets, key);

	for (var = D_RO(buckets)->bucket[h];
			!TOID_IS_NULL(var);
			var = D_RO(var)->next)
		if (D_RO(var)->key == key)
			return D_RO(var)->value;
	*/

	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	struct buckets *buckets = sobj_tx_read(pop, hashmap_p->buckets);
	
	PMEMoid var;
	struct entry *var_p;

	PMEMoid ret = OID_NULL;

	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_tx_read(pop, var);
		if (var_p->key == key) {
			ret = var_p->value;
			free(var_p);
			free(hashmap_p);
			free(buckets);
			return ret;
		}
		var = var_p->next;
		free(var_p);
	}			

	free(hashmap_p);
	free(buckets);

	return ret;
}

/*
 * hm_tx_lookup -- checks whether specified value exists
 */
int
hm_tx_lookup(PMEMobjpool *pop, PMEMoid hashmap, uint64_t key)
{
	/*
	TOID(struct buckets) buckets = D_RO(hashmap)->buckets;
	TOID(struct entry) var;

	uint64_t h = hash(&hashmap, &buckets, key);

	for (var = D_RO(buckets)->bucket[h];
			!TOID_IS_NULL(var);
			var = D_RO(var)->next)
		if (D_RO(var)->key == key)
			return 1;
	*/
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	struct buckets *buckets = sobj_tx_read(pop, hashmap_p->buckets);
	
	PMEMoid var;
	struct entry *var_p;

	uint64_t h = hash(pop, &hashmap, &hashmap_p->buckets, key);

	for (var = buckets->bucket[h]; !OID_IS_NULL(var); ) {
		var_p = sobj_tx_read(pop, var);
		if (var_p->key == key) {
			free(var_p);
			free(hashmap_p);
			free(buckets);
			return 1;
		}
		var = var_p->next;
		free(var_p);
	}			

	free(hashmap_p);
	free(buckets);
	return 0;
}

/*
 * hm_tx_count -- returns number of elements
 */
size_t
hm_tx_count(PMEMobjpool *pop, PMEMoid hashmap)
{
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	size_t ret = hashmap_p->count;
	free(hashmap_p);
	return ret;
	//return D_RO(hashmap)->count;
}

/*
 * hm_tx_init -- recovers hashmap state, called after pmemobj_open
 */
int
hm_tx_init(PMEMobjpool *pop, PMEMoid hashmap)
{
	struct hashmap_tx *hashmap_p = sobj_tx_read(pop, hashmap);
	srand(hashmap_p->seed);
	free(hashmap_p);
	//srand(D_RO(hashmap)->seed);
	return 0;
}

/*
 * hm_tx_create -- allocates new hashmap
 */
int
hm_tx_create(PMEMobjpool *pop, PMEMoid *map, void *arg)
{
	struct hashmap_args *args = (struct hashmap_args *)arg;
	int ret = 0;
	TX_BEGIN(pop) {
		//TX_ADD_DIRECT(map);
		//*map = TX_ZNEW(struct hashmap_tx);
		
		//sobj_tx_add_range(*map, 0, sizeof(struct hashmap_tx));
		*map = sobj_tx_zalloc(sizeof(struct hashmap_tx), HASHMAP_TX_TYPE_OFFSET);
		
		uint32_t seed = args ? args->seed : 0;
		create_hashmap(pop, *map, seed);
	} TX_ONABORT {
		ret = -1;
	} TX_END

	return ret;
}

/*
 * hm_tx_check -- checks if specified persistent object is an
 * instance of hashmap
 */
int
hm_tx_check(PMEMobjpool *pop, PMEMoid hashmap)
{
	return OID_IS_NULL(hashmap);
	//return TOID_IS_NULL(hashmap) || !TOID_VALID(hashmap);
}

/*
 * hm_tx_cmd -- execute cmd for hashmap
 * ANCHOR : not needed for benchmarking
 */
int
hm_tx_cmd(PMEMobjpool *pop, PMEMoid hashmap,
		unsigned cmd, uint64_t arg)
{
	
	switch (cmd) {
		case HASHMAP_CMD_REBUILD:
			hm_tx_rebuild(pop, hashmap, arg);
			return 0;
		case HASHMAP_CMD_DEBUG:
			if (!arg)
				return -EINVAL;
			hm_tx_debug(pop, hashmap, (FILE *)arg);
			return 0;
		default:
			return -EINVAL;
	}
	
	return 0;
}
