/** \file
 *
 *  \brief Clocks and clock groups.
 *
 *  \copyright Copyright 2025 Ciaran Anscomb
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 *
 *  Maintains groups of clocks.  When you advance one clock, the others in the
 *  same group are incremented proportionally, tracking integer error.
 */

#include <stdlib.h>
#include <stdint.h>

#include "xalloc.h"

#include "clock.h"

struct clock_group_private {
	struct clock_group clock_group;

	unsigned nclocks;
	struct clock **clocks;
};

static struct clock_group_private global_clock_group;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Clock group part creation

static struct part *clock_group_allocate(void);
static void clock_group_free(struct part *p);

static const struct partdb_entry_funcs clock_group_funcs = {
        .allocate = clock_group_allocate,
        .free = clock_group_free,
};

const struct partdb_entry clock_group_part = { .name = "clockgroup", .funcs = &clock_group_funcs };


static struct part *clock_group_allocate(void) {
	struct clock_group_private *cgp = xmalloc(sizeof(*cgp));
	*cgp = (struct clock_group_private){0};
	return &cgp->clock_group.part;
}

static void clock_group_free(struct part *p) {
	struct clock_group_private *cgp = (struct clock_group_private *)p;
	for (unsigned i = 0; i < cgp->nclocks; ++i) {
		free(cgp->clocks[i]);
	}
	free(cgp->clocks);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Get system clock at the specified rate.  Created if it does not yet exist,
// reference count bumped if it does.
struct clock *clock_get(struct clock_group *cg, uint32_t rate) {
	struct clock_group_private *cgp = (struct clock_group_private *)cg;
	if (!cgp) {
		cgp = &global_clock_group;
	}
	for (unsigned i = 0; i < cgp->nclocks; ++i) {
		if (cgp->clocks[i]->rate == rate) {
			++cgp->clocks[i]->ref;
			return cgp->clocks[i];
		}
	}
	unsigned i = cgp->nclocks++;
	cgp->clocks = xrealloc(cgp->clocks, cgp->nclocks * sizeof(*cgp->clocks));
	struct clock *clk = xmalloc(sizeof(*clk));
	cgp->clocks[i] = clk;
	*clk = (struct clock){0};
	clk->rate = rate;
	clk->time = (struct clock_time){ 0, rate / 2 };
	clk->group = cg;
	++clk->ref;
	return clk;
}

// Decrement refcount and free (removing from list) if it reaches zero.
void clock_drop(struct clock *clk) {
	if (--clk->ref)
		return;
	struct clock_group *cg = clk->group;
	struct clock_group_private *cgp = (struct clock_group_private *)cg;
	for (unsigned i = 0; i < cgp->nclocks; ++i) {
		if (cgp->clocks[i] == clk) {
			for (unsigned j = i + 1; j < cgp->nclocks; ++i, ++j) {
				cgp->clocks[i] = cgp->clocks[j];
			}
			--cgp->nclocks;
			cgp->clocks = xrealloc(cgp->clocks, cgp->nclocks * sizeof(*cgp->clocks));
			break;
		}
	}
	free(clk);
}

// Reset all clocks
void clock_reset(struct clock_group *cg) {
	struct clock_group_private *cgp = (struct clock_group_private *)cg;
	for (unsigned i = 0; i < cgp->nclocks; ++i) {
		cgp->clocks[i]->time.ticks = 0;
		cgp->clocks[i]->time.error = cgp->clocks[i]->rate / 2;
	}
}

// Add time to specified clock, and advance all others proportionally.
void clock_add_ticks(struct clock *clk, int nticks) {
	struct clock_group *cg = clk->group;
	struct clock_group_private *cgp = (struct clock_group_private *)cg;
	clk->time.ticks += nticks;
	for (unsigned i = 0; i < cgp->nclocks; ++i) {
		struct clock *tclk = cgp->clocks[i];
		if (tclk == clk)
			continue;
		int64_t t = tclk->time.error + nticks * tclk->rate;
		int32_t dt = t / clk->rate;
		tclk->time.ticks += dt;
		tclk->time.error = t - dt * clk->rate;
	}
}
