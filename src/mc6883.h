/** \file
 *
 *  \brief Motorola SN74LS783/MC6883 Synchronous Address Multiplexer.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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

	_Bool RAS0;
	_Bool RAS1;

	DELEGATE_T3(void, int, bool, uint16) cpu_cycle;
	DELEGATE_T0(void) vdg_update;
};

void mc6883_reset(struct MC6883 *);
void mc6883_mem_cycle(void *, _Bool RnW, uint16_t A);
unsigned mc6883_decode(struct MC6883 *, _Bool RnW, uint16_t A);
void mc6883_vdg_hsync(struct MC6883 *, _Bool level);
void mc6883_vdg_fsync(struct MC6883 *, _Bool level);
int mc6883_vdg_bytes(struct MC6883 *, int nbytes);
void mc6883_set_register(struct MC6883 *, unsigned int value);
unsigned int mc6883_get_register(struct MC6883 *);

#endif
