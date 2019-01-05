/*

Motorola SN74LS783/MC6883 Synchronous Address Multiplexer

Copyright 2003-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_SAM_H_
#define XROAR_SAM_H_

#include <stdint.h>

#include "delegate.h"

#include "part.h"

#define EVENT_SAM_CYCLES(c) (c)

struct MC6883 {
	struct part part;

	unsigned S;
	unsigned Z;
	unsigned V;
	_Bool RAS;
	DELEGATE_T3(void, int, bool, uint16) cpu_cycle;
};

struct MC6883 *sam_new(void);

void sam_reset(struct MC6883 *);
void sam_mem_cycle(void *, _Bool RnW, uint16_t A);
void sam_vdg_hsync(struct MC6883 *, _Bool level);
void sam_vdg_fsync(struct MC6883 *, _Bool level);
int sam_vdg_bytes(struct MC6883 *, int nbytes);
void sam_set_register(struct MC6883 *, unsigned int value);
unsigned int sam_get_register(struct MC6883 *);

#endif
