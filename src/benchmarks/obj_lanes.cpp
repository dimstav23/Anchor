/*
 * Copyright 2015-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *      * Neither the name of the copyright holder nor the names of its
 *        contributors may be used to endorse or promote products derived
 *        from this software without specific prior written permission.
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
 * obj_lanes.cpp -- lane benchmark definition
 */

#include <cassert>
#include <cerrno>
#include <unistd.h>

#include "benchmark.hpp"
#include "file.h"
#include "libpmemobj.h"

#ifdef ANCHOR_FUNCS
#include "libanchor.h"
#endif

/* an internal libpmemobj code */
#include "lane.h"

/*
 * The number of times to repeat the operation, used to get more accurate
 * results, because the operation time was minimal compared to the framework
 * overhead.
 */
#define OPERATION_REPEAT_COUNT 10000

/*
 * obj_bench - variables used in benchmark, passed within functions
 */
struct obj_bench {
	PMEMobjpool *pop;     /* persistent pool handle */
	struct prog_args *pa; /* prog_args structure */
};

/*
 * lanes_init -- benchmark initialization
 */
static int
lanes_init(struct benchmark *bench, struct benchmark_args *args)
{
	assert(bench != nullptr);
	assert(args != nullptr);
	assert(args->opts != nullptr);

#ifdef ANCHOR_FUNCS
	/* A 128 bit key */
	uint8_t *key = (uint8_t *)"012345678901234";

	/* A 128 bit IV */
	uint8_t *iv = (uint8_t *)"012345678901234";
	size_t iv_len = 16;
	
	char manifest_path[PATH_MAX];
	if (util_safe_strcpy(manifest_path, args->manifest_fname, sizeof(manifest_path)) != 0)
		return -1;
	char counters_path[PATH_MAX];
	if (util_safe_strcpy(counters_path, args->counters_fname, sizeof(counters_path)) != 0)
		return -1;
	char metadata_log_path[PATH_MAX];
	if (util_safe_strcpy(metadata_log_path, args->metadata_log_fname, sizeof(metadata_log_path)) != 0)
		return -1;
#endif

	enum file_type type = util_file_get_type(args->fname);
	if (type == OTHER_ERROR) {
		fprintf(stderr, "could not check type of file %s\n",
			args->fname);
		return -1;
	}

	auto *ob = (struct obj_bench *)malloc(sizeof(struct obj_bench));
	if (ob == nullptr) {
		perror("malloc");
		return -1;
	}
	pmembench_set_priv(bench, ob);

	ob->pa = (struct prog_args *)args->opts;
	size_t psize;

	if (args->is_poolset || type == TYPE_DEVDAX)
		psize = 0;
	else
		psize = PMEMOBJ_MIN_POOL;

#ifdef ANCHOR_FUNCS
	/* create pmemobj pool */
	ob->pop = spool_create(args->fname, "obj_lanes", psize, args->fmode, 
							manifest_path, counters_path, metadata_log_path,
							key, 16, iv, iv_len);
	if (ob->pop == nullptr) {
		fprintf(stderr, "%s\n", pmemobj_errormsg());
		goto err;
	}
#else
	/* create pmemobj pool */
	ob->pop = pmemobj_create(args->fname, "obj_lanes", psize, args->fmode);
	if (ob->pop == nullptr) {
		fprintf(stderr, "%s\n", pmemobj_errormsg());
		goto err;
	}
#endif
	return 0;

err:
	free(ob);
	return -1;
}

/*
 * lanes_exit -- benchmark clean up
 */
static int
lanes_exit(struct benchmark *bench, struct benchmark_args *args)
{
	auto *ob = (struct obj_bench *)pmembench_get_priv(bench);
#ifdef ANCHOR_FUNCS
	spool_close(ob->pop);
#else
	pmemobj_close(ob->pop);
#endif
	free(ob);

	return 0;
}

/*
 * lanes_op -- performs the lane hold and release operations
 */
static int
lanes_op(struct benchmark *bench, struct operation_info *info)
{
	auto *ob = (struct obj_bench *)pmembench_get_priv(bench);
	struct lane *lane;

	for (int i = 0; i < OPERATION_REPEAT_COUNT; i++) {
		lane_hold(ob->pop, &lane);

		lane_release(ob->pop);
	}

	return 0;
}
static struct benchmark_info lanes_info;

CONSTRUCTOR(obj_lines_constructor)
void
obj_lines_constructor(void)
{
	lanes_info.name = "obj_lanes";
	lanes_info.brief = "Benchmark for internal lanes "
			   "operation";
	lanes_info.init = lanes_init;
	lanes_info.exit = lanes_exit;
	lanes_info.multithread = true;
	lanes_info.multiops = true;
	lanes_info.operation = lanes_op;
	lanes_info.measure_time = true;
	lanes_info.clos = NULL;
	lanes_info.nclos = 0;
	lanes_info.opts_size = 0;
	lanes_info.rm_file = true;
	lanes_info.allow_poolset = true;
	REGISTER_BENCHMARK(lanes_info);
}
