/*
 * Copyright 2015-2017, Intel Corporation
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
#ifndef HASHMAP_TX_H
#define HASHMAP_TX_H

#include <stddef.h>
#include <stdint.h>
#include <hashmap.h>
#include <libpmemobj.h>
#include <libanchor.h>

#ifndef HASHMAP_TX_TYPE_OFFSET
#define HASHMAP_TX_TYPE_OFFSET 1004
#endif

struct hashmap_tx;

TOID_DECLARE(struct hashmap_tx, HASHMAP_TX_TYPE_OFFSET + 0);

int hm_tx_check(PMEMobjpool *pop, PMEMoid hashmap);
int hm_tx_create(PMEMobjpool *pop, PMEMoid *map, void *arg);
int hm_tx_init(PMEMobjpool *pop, PMEMoid hashmap);
int hm_tx_destroy(PMEMobjpool *pop, PMEMoid hashmap);
int hm_tx_insert(PMEMobjpool *pop, PMEMoid hashmap,
		uint64_t key, PMEMoid value);
int hm_tx_update(PMEMobjpool *pop, PMEMoid hashmap,
		uint64_t key, char value_datap[]);
PMEMoid hm_tx_remove(PMEMobjpool *pop, PMEMoid hashmap,
		uint64_t key);
PMEMoid hm_tx_get(PMEMobjpool *pop, PMEMoid hashmap,
		uint64_t key);
int hm_tx_lookup(PMEMobjpool *pop, PMEMoid hashmap,
		uint64_t key);
int hm_tx_foreach(PMEMobjpool *pop, PMEMoid hashmap,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg);
size_t hm_tx_count(PMEMobjpool *pop, PMEMoid hashmap);
int hm_tx_cmd(PMEMobjpool *pop, PMEMoid hashmap,
		unsigned cmd, uint64_t arg);
		
#endif /* HASHMAP_TX_H */
