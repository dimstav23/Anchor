/*
 * Copyright 2017-2019, Intel Corporation
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
 * stats.c -- implementation of statistics
 */

#include "obj.h"
#include "stats.h"

STATS_CTL_HANDLER(persistent, curr_allocated, heap_curr_allocated);

STATS_CTL_HANDLER(transient, run_allocated, heap_run_allocated);
STATS_CTL_HANDLER(transient, run_active, heap_run_active);

static const struct ctl_node CTL_NODE(heap)[] = {
	STATS_CTL_LEAF(persistent, curr_allocated),
	STATS_CTL_LEAF(transient, run_allocated),
	STATS_CTL_LEAF(transient, run_active),

	CTL_NODE_END
};

/*
 * CTL_READ_HANDLER(enabled) -- returns whether or not statistics are enabled
 */
static int
CTL_READ_HANDLER(enabled)(void *ctx,
	enum ctl_query_source source, void *arg,
	struct ctl_indexes *indexes)
{
	PMEMobjpool *pop = ctx;

	enum pobj_stats_enabled *arg_out = arg;

	*arg_out = pop->stats->enabled;

	return 0;
}

/*
 * stats_enabled_parser -- parses the stats enabled type
 */
static int
stats_enabled_parser(const void *arg, void *dest, size_t dest_size)
{
	const char *vstr = arg;
	enum pobj_stats_enabled *enabled = dest;
	ASSERTeq(dest_size, sizeof(enum pobj_stats_enabled));

	int bool_out;
	if (ctl_arg_boolean(arg, &bool_out, sizeof(bool_out)) == 0) {
		*enabled = bool_out ?
			POBJ_STATS_ENABLED_BOTH : POBJ_STATS_DISABLED;
		return 0;
	}

	if (strcmp(vstr, "disabled") == 0) {
		*enabled = POBJ_STATS_DISABLED;
	} else if (strcmp(vstr, "both") == 0) {
		*enabled = POBJ_STATS_ENABLED_BOTH;
	} else if (strcmp(vstr, "persistent") == 0) {
		*enabled = POBJ_STATS_ENABLED_PERSISTENT;
	} else if (strcmp(vstr, "transient") == 0) {
		*enabled = POBJ_STATS_ENABLED_TRANSIENT;
	} else {
		ERR("invalid enable type");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

/*
 * CTL_WRITE_HANDLER(enabled) -- enables or disables statistics counting
 */
static int
CTL_WRITE_HANDLER(enabled)(void *ctx,
	enum ctl_query_source source, void *arg,
	struct ctl_indexes *indexes)
{
	PMEMobjpool *pop = ctx;

	pop->stats->enabled = *(enum pobj_stats_enabled *)arg;

	return 0;
}

static const struct ctl_argument CTL_ARG(enabled) = {
	.dest_size = sizeof(enum pobj_stats_enabled),
	.parsers = {
		CTL_ARG_PARSER(sizeof(enum pobj_stats_enabled),
			stats_enabled_parser),
		CTL_ARG_PARSER_END
	}
};

static const struct ctl_node CTL_NODE(stats)[] = {
	CTL_CHILD(heap),
	CTL_LEAF_RW(enabled),

	CTL_NODE_END
};

/*
 * stats_new -- allocates and initializes statistics instance
 */
struct stats *
stats_new(PMEMobjpool *pop)
{
	struct stats *s = Malloc(sizeof(*s));
	if (s == NULL) {
		ERR("!Malloc");
		return NULL;
	}

	s->enabled = POBJ_STATS_ENABLED_TRANSIENT;
	s->persistent = &pop->stats_persistent;
	VALGRIND_ADD_TO_GLOBAL_TX_IGNORE(s->persistent, sizeof(*s->persistent));
	s->transient = Zalloc(sizeof(struct stats_transient));
	if (s->transient == NULL)
		goto error_transient_alloc;

	return s;

error_transient_alloc:
	Free(s);
	return NULL;
}

/*
 * stats_delete -- deletes statistics instance
 */
void
stats_delete(PMEMobjpool *pop, struct stats *s)
{
	pmemops_persist(&pop->p_ops, s->persistent,
	sizeof(struct stats_persistent));
	Free(s->transient);
	Free(s);
}

/*
 * stats_ctl_register -- registers ctl nodes for statistics
 */
void
stats_ctl_register(PMEMobjpool *pop)
{
	CTL_REGISTER_MODULE(pop->ctl, stats);
}

#ifdef ANCHOR_FUNCS
/*
 * sec_stats_delete -- deletes statistics instance
 */
void
sec_stats_delete(PMEMobjpool *pop, struct stats *s)
{
	Free(s->transient);
	Free(s);
}
#endif
