/** \file
 *
 *  \brief RAM.
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
 *
 * Usage:
 *
 * Create part "RAM" with (struct ram_config *) passed for options, defining
 * data width and how rows & columns are organised.
 *
 * Then add banks with ram_add_bank().  Each bank's size will be determined
 * from the config used to create.
 */

#ifndef XROAR_RAM_H_
#define XROAR_RAM_H_

#include <stdint.h>

#include "delegate.h"

#include "part.h"

// Encode a RAM organisation.
//
// a = number of address bits (1-64, encoded as 0-63 << 12)
// r = number of row bits     (1-64, encoded as 0-63 << 6)
// cs = col shift             (0-63 << 0)
//
// Currently no more than 24 address bits (16M elements) accepted.
//
// Column shift must not be more than number of row bits.

#define RAM_ORG(a,r,cs) ( (((a)-1) << 12) | (((r)-1) << 6) | ((cs) << 0) )

// Decode

#define RAM_ORG_A(o)  ( (((o) >> 12) & 0x3f)+1 )
#define RAM_ORG_R(o)  ( (((o) >> 6)  & 0x3f)+1 )
#define RAM_ORG_CS(o) ( (o) & 0x3f )

// Some typical organisations:

#define RAM_ORG_4Kx1   RAM_ORG(12, 6, 0)  // 4K x 1 (e.g. MK4096)
#define RAM_ORG_16Kx1  RAM_ORG(14, 7, 0)  // 16K x 1 (e.g. 4116)
#define RAM_ORG_16Kx4  RAM_ORG(14, 8, 1)  // 16K x 4 (e.g. 4416)
#define RAM_ORG_32Kx1  RAM_ORG(15, 8, 0)  // 32K x 1 (e.g. 4532)
#define RAM_ORG_64Kx1  RAM_ORG(16, 8, 0)  // 64K x 1 (e.g. 4164)
#define RAM_ORG_256Kx1 RAM_ORG(18, 9, 0)  // 256K x 1 (e.g. 41256)

// Note no special entry for 4464 (64Kx4), as addressing is the same.

enum {
	ram_init_clear,
	ram_init_set,
	ram_init_pattern,
	ram_init_random
};

struct ram_config {
	unsigned d_width;       // 8 or 16
	unsigned organisation;  // from RAM_ORG() macro
};

struct ram {
	struct part part;

	unsigned d_width;
	unsigned organisation;
	unsigned nbanks;

	unsigned row_mask;
	unsigned col_mask;
	unsigned col_shift;
	size_t bank_nelems;

	void **d;
};

// Populate indicated bank (all will be empty by default)

void ram_add_bank(struct ram *ram, unsigned bank);

// Clear RAM, using the initialisation method specified (ram_init_*).
// Sometimes it's useful to be able to test random vs predictable startup
// states.

void ram_clear(struct ram *ram, int method);

// Read data from serialisation handle into RAM bank only if that bank is
// present.

void ram_ser_read_bank(struct ram *ram, struct ser_handle *sh, unsigned bank);

// Read data from serialisation handle into each present RAM bank in turn.

void ram_ser_read(struct ram *ram, struct ser_handle *sh);

// Inline access functions.

inline uint8_t *ram_a8(struct ram *ram, unsigned bank, unsigned row, unsigned col) {
	if (bank >= ram->nbanks || !ram->d[bank])
		return NULL;
	unsigned a = (row & ram->row_mask) | ((col & ram->col_mask) << ram->col_shift);
	return &(((uint8_t *)ram->d[bank])[a]);
}

inline uint16_t *ram_a16(struct ram *ram, unsigned bank, unsigned row, unsigned col) {
	if (bank >= ram->nbanks || !ram->d[bank])
		return NULL;
	unsigned a = (row & ram->row_mask) | ((col & ram->col_mask) << ram->col_shift);
	return &(((uint16_t *)ram->d[bank])[a]);
}

inline void ram_d8(struct ram *ram, _Bool RnW, unsigned bank,
		   unsigned row, unsigned col, uint8_t *d) {
	uint8_t *p = ram_a8(ram, bank, row, col);
	if (!p)
		return;
	if (RnW) {
		*d = *p;
	} else {
		*p = *d;
	}
}

inline void ram_d16(struct ram *ram, _Bool RnW, unsigned bank,
		    unsigned row, unsigned col, uint16_t *d) {
	uint16_t *p = ram_a16(ram, bank, row, col);
	if (!p)
		return;
	if (RnW) {
		*d = *p;
	} else {
		*p = *d;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

unsigned ram_report(struct ram *ram, const char *par, const char *name);

#endif
