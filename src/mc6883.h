/** \file
 *
 *  \brief Motorola SN74LS783/MC6883 Synchronous Address Multiplexer.
 *
 *  \copyright Copyright 2003-2026 Ciaran Anscomb
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

#ifndef XROAR_MC6883_H_
#define XROAR_MC6883_H_

#include <stdint.h>

#include "delegate.h"

#include "debug.h"
#include "part.h"

struct ram;

struct MC6883 {
	struct part part;

	uint8_t S;
	unsigned Zrow;
	unsigned Zcol;
	unsigned Vrow;
	unsigned Vcol;

	bool nWE;
	bool RAS0;
	bool RAS1;

	DELEGATE_T0(void) vdg_update;

	void (*reset)(struct MC6883 *);
	int (*mem_cycle)(void *, bool RnW, uint16_t A);
	unsigned (*decode)(struct MC6883 *, bool RnW, uint16_t A);
	void (*vdg_fsync)(struct MC6883 *, bool level);
	void (*vdg_hsync)(struct MC6883 *, bool level);
	int (*vdg_bytes)(struct MC6883 *, int nbytes);

	// Not used by real MC6883, may be required by some replacements:
	uint8_t *CPUD;    // provided by per-machine code
	struct ram *RAM;  // provided by SAMx8

	struct {
		struct debug_part part;
	} debug;
};

bool mc6883_is_a(struct part *p, const char *name);

#endif
