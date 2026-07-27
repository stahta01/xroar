/** \file
 *
 *  \brief iMMUnity support.
 *
 *  \copyright Copyright 2026 Jim Brain
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
 */

#ifndef XROAR_DRAGON_IMMUNITY_H_
#define XROAR_DRAGON_IMMUNITY_H_

#include <stdint.h>

#include "debug.h"
#include "part.h"

struct dragon;
struct ram;

struct immunity {
	struct part part;

	struct dragon *dragon;

	uint8_t dat[2][8];
	uint8_t init0;
	uint8_t init1;
	struct ram *mem;

	struct {
		struct debug_part part;
	} debug;
};

// iMMUnity interposes the CPU when used.

void immunity_reset(struct immunity *cj, bool hard);

void immunity_cpu_cycle(void *sptr, bool RnW, uint16_t A);

#endif
