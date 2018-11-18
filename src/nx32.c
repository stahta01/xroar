/*

NX32 RAM expansion cartridge

Copyright 2016-2018 Tormod Volden
Copyright 2016-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include <xalloc.h>
#include "cart.h"
#include "becker.h"

/* number of 32KB banks in memory cartridge: 1, 4 or 16 */
#define EXTBANKS 16

/* 65SPI interface on Spinx-512 boards */
uint8_t spi65_read(uint8_t reg);
void spi65_write(uint8_t reg, uint8_t value);
void spi65_reset(void);

static struct cart *nx32_new(struct cart_config *);

struct cart_module cart_nx32_module = {
	.name = "nx32",
	.description = "NX32 memory cartridge",
	.new = nx32_new,
};

struct nx32 {
	struct cart cart;
	uint8_t extmem[0x8000 * EXTBANKS];
	_Bool extmem_map;
	_Bool extmem_ty;
	uint8_t extmem_bank;
	struct becker *becker;
};

static void nx32_reset(struct cart *c);
static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void nx32_detach(struct cart *c);
static void nx32_free(struct cart *c);

struct cart *nx32_new(struct cart_config *cc) {
	struct nx32 *n = xmalloc(sizeof(*n));
	*n = (struct nx32){0};
	struct cart *c = &n->cart;

	c->config = cc;
	cart_rom_init(c);
	c->read = nx32_read;
	c->write = nx32_write;
	c->reset = nx32_reset;
	c->detach = nx32_detach;
	c->free = nx32_free;

	if (cc->becker_port) {
		n->becker = becker_new();
	}

	return c;
}

static void nx32_reset(struct cart *c) {
	struct nx32 *n = (struct nx32 *)c;
	n->extmem_map = 0;
	n->extmem_ty = 0;
	n->extmem_bank = 0;
	if (n->becker)
		becker_reset(n->becker);
	spi65_reset();
}

static void nx32_detach(struct cart *c) {
	struct nx32 *n = (struct nx32 *)c;
	if (n->becker)
		becker_reset(n->becker);
	cart_rom_detach(c);
}

static void nx32_free(struct cart *c) {
	struct nx32 *n = (struct nx32 *)c;
	if (n->becker)
		becker_free(n->becker);
	cart_rom_free(c);
}

static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct nx32 *n = (struct nx32 *)c;
	(void)R2;
	c->EXTMEM = 0;

	if ((A & 0xFFFC) == 0xFF6C)
		return spi65_read(A & 3);

	if (A > 0x7fff && A < 0xff00 && !n->extmem_ty && n->extmem_map) {
		c->EXTMEM = 1;
		return n->extmem[0x8000 * n->extmem_bank + (A & 0x7fff)];
	}
	if (P2 && n->becker) {
		switch (A & 3) {
		case 0x1:
			return becker_read_status(n->becker);
		case 0x2:
			return becker_read_data(n->becker);
		default:
			break;
		}
	}
	return D;
}

static uint8_t nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct nx32 *n = (struct nx32 *)c;
	(void)R2;
	c->EXTMEM = 0;

	if ((A & 0xFFFC) == 0xFF6C)
		spi65_write(A & 3, D);

	if ((A & ~1) == 0xFFDE) {
		n->extmem_ty = A & 1;
	} else if ((A & ~1) == 0xFFBE) {
		n->extmem_map = A & 1;
		n->extmem_bank = D & (EXTBANKS - 1);
		c->EXTMEM = 1;
	} else if (A > 0x7fff && A < 0xff00 && !n->extmem_ty && n->extmem_map) {
		n->extmem[0x8000 * n->extmem_bank + (A & 0x7fff)] = D;
		c->EXTMEM = 1;
	}
	if (P2 && n->becker) {
		switch (A & 3) {
		case 0x2:
			becker_write_data(n->becker, D);
			break;
		default:
			break;
		}
	}
	return D;
}
