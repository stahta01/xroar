/** \file
 *
 *  \brief CocoMEM Jr. support.
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

#ifndef XROAR_DRAGON_COCOMEMJR_H_
#define XROAR_DRAGON_COCOMEMJR_H_

#include <stdint.h>

#include "part.h"

struct dragon;
struct ram;

struct cocomem_jr {
	struct part part;

	struct dragon *dragon;

	uint8_t dat[2][8];
	uint8_t init0;
	uint8_t init1;
	struct ram *mem;
};

// CoCoMEM Jr. interposes the CPU when used.

void cocomem_jr_reset(struct cocomem_jr *cj, _Bool hard);

void cocomem_jr_cpu_cycle(void *sptr, _Bool RnW, uint16_t A);

#endif
