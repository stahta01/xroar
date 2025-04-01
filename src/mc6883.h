/** \file
 *
 *  \brief Motorola SN74LS783/MC6883 Synchronous Address Multiplexer.
 *
 *  \copyright Copyright 2003-2025 Ciaran Anscomb
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

#include "part.h"

struct MC6883 {
	struct part part;

	uint8_t S;
	unsigned Zrow;
	unsigned Zcol;
	unsigned Vrow;
	unsigned Vcol;

	_Bool nWE;
	_Bool RAS0;
	_Bool RAS1;

	DELEGATE_T3(void, int, bool, uint16) cpu_cycle;
	DELEGATE_T0(void) vdg_update;

	void (*reset)(struct MC6883 *);
	void (*mem_cycle)(void *, _Bool RnW, uint16_t A);
	unsigned (*decode)(struct MC6883 *, _Bool RnW, uint16_t A);
	void (*vdg_fsync)(struct MC6883 *, _Bool level);
	void (*vdg_hsync)(struct MC6883 *, _Bool level);
	int (*vdg_bytes)(struct MC6883 *, int nbytes);
	void (*set_register)(struct MC6883 *, unsigned value);
	unsigned (*get_register)(struct MC6883 *);

	// Not used by real MC6883, may be required by some replacements:
	uint8_t *CPUD;
};

#endif
