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

#ifndef XROAR_CLOCK_H_
#define XROAR_CLOCK_H_

#include <stdint.h>

#include "part.h"

typedef uint32_t clock_tick;

struct clock_group {
	struct part part;
};

struct clock_time {
	clock_tick ticks;
	clock_tick error;
};

struct clock {
	uint32_t rate;
	struct clock_time time;
	struct clock_group *group;
	unsigned ref;
};

struct clock_group *clock_group_new(void);

// Create a clock (if necessary) within group for given rate.  A NULL group is
// allowed: the clock will be created in the global group.
struct clock *clock_get(struct clock_group *, uint32_t rate);

// Free clock within group (actually decrement ref count, free when zero).
void clock_drop(struct clock *);

// Reset all clocks
void clock_reset(struct clock_group *);

// Add time to specified clock, and advance all others proportionally.
void clock_add_ticks(struct clock *, int nticks);

#endif
