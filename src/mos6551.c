/** \file
 *
 *  \brief MOS 6551 Asynchronous Communication Interface Adapter.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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

// Completely non-functional.  Simulates enough to keep the Dragon 64 ROM's
// probe of its registers happy.

#include "top-config.h"

#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "logging.h"
#include "mos6551.h"
#include "part.h"
#include "serialise.h"

static const struct ser_struct ser_struct_mos6551[] = {
	SER_ID_STRUCT_ELEM(1, struct MOS6551, status_reg),
	SER_ID_STRUCT_ELEM(2, struct MOS6551, command_reg),
	SER_ID_STRUCT_ELEM(3, struct MOS6551, control_reg),
};

static const struct ser_struct_data mos6551_ser_struct_data = {
	.elems = ser_struct_mos6551,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mos6551),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MOS6551 ACIA part creation

static struct part *mos6551_allocate(void);
static _Bool mos6551_finish(struct part *p);

static const struct partdb_entry_funcs mos6551_funcs = {
	.allocate = mos6551_allocate,
	.finish = mos6551_finish,

	.ser_struct_data = &mos6551_ser_struct_data,
};

const struct partdb_entry mos6551_part = { .name = "MOS6551", .description = "MOS Technology | 6551 ACIA", .funcs = &mos6551_funcs };

static struct part *mos6551_allocate(void) {
	struct MOS6551 *acia = part_new(sizeof(*acia));
	struct part *p = &acia->part;

	*acia = (struct MOS6551){0};

	acia->status_reg = 0x10;

	return p;
}

static _Bool mos6551_finish(struct part *p) {
	struct MOS6551 *acia = (struct MOS6551 *)p;

	// No-op
	(void)acia;

	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void mos6551_reset(struct MOS6551 *acia) {
	// W65C51N datasheet says bit 4 of the status register (Transmitter
	// Data Register Empty) is always set.  Not sure if that's common
	// across variants, but I think this bit is also something the Dragon
	// 64 ROM checks for.
	acia->status_reg = 0x10;
	acia->command_reg = 0;
	acia->control_reg = 0;
}

static void mos6551_read(struct MOS6551 *acia, unsigned A, uint8_t *D) {
	switch (A & 3) {
	default:
	case 0:
		// Receive data
		*D = 0;
		break;
	case 1:
		// Status register
		*D = acia->status_reg;
		acia->IRQ = 0;
		acia->status_reg &= ~0x80;
		break;
	case 2:
		// Command register
		*D = acia->command_reg;
		break;
	case 3:
		// Control register
		*D = acia->control_reg;
		break;
	}
}

static void mos6551_write(struct MOS6551 *acia, unsigned A, uint8_t *D) {
	switch (A & 3) {
	default:
	case 0:
		// Transmit data
		break;
	case 1:
		// Programmed reset
		acia->command_reg &= ~0x1f;
		// NOTE: the W65C51N datasheet claims their part clears
		// this bit on programmed reset (i.e. _enables_ IRQ).
		acia->command_reg |= 0x02;
		break;
	case 2:
		// Command register
		acia->command_reg = *D;
		break;
	case 3:
		// Control register
		acia->control_reg = *D;
		break;
	}
}

void mos6551_access(void *sptr, _Bool RnW, unsigned A, uint8_t *D) {
	struct MOS6551 *acia = sptr;

	if (RnW) {
		mos6551_read(acia, A, D);
	} else {
		mos6551_write(acia, A, D);
	}
}
