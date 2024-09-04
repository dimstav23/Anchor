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

/*
 * ctree_map_opt.h -- TreeMap sorted collection implementation
 */

#ifndef CTREE_MAP_OPT_H
#define CTREE_MAP_OPT_H

#include <libpmemobj.h>
#include <libanchor.h>

#ifndef CTREE_MAP_TYPE_OFFSET
#define CTREE_MAP_TYPE_OFFSET 1008
#endif

struct ctree_map;
TOID_DECLARE(struct ctree_map, CTREE_MAP_TYPE_OFFSET + 0);

int ctree_map_opt_check(PMEMobjpool *pop, PMEMoid map);
int ctree_map_opt_create(PMEMobjpool *pop, PMEMoid *map, void *arg);
int ctree_map_opt_destroy(PMEMobjpool *pop, PMEMoid *map);
int ctree_map_opt_insert(PMEMobjpool *pop, PMEMoid map,
	uint64_t key, PMEMoid value);
int ctree_map_opt_insert_new(PMEMobjpool *pop, PMEMoid map,
		uint64_t key, size_t size, unsigned type_num,
		void (*constructor)(PMEMobjpool *pop, void *ptr, void *arg),
		void *arg);
PMEMoid ctree_map_opt_remove(PMEMobjpool *pop, PMEMoid map,
		uint64_t key);
int ctree_map_opt_remove_free(PMEMobjpool *pop, PMEMoid map,
		uint64_t key);
int ctree_map_opt_clear(PMEMobjpool *pop, PMEMoid map);
PMEMoid ctree_map_opt_get(PMEMobjpool *pop, PMEMoid map,
		uint64_t key);
int ctree_map_opt_lookup(PMEMobjpool *pop, PMEMoid map,
		uint64_t key);
int ctree_map_opt_foreach(PMEMobjpool *pop, PMEMoid map,
	int (*cb)(uint64_t key, PMEMoid value, void *arg), void *arg);
int ctree_map_opt_is_empty(PMEMobjpool *pop, PMEMoid map);

#endif /* CTREE_MAP_OPT_H */
