/** \file
 *
 *  \brief MCX128 expansion module for MC-10.
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
 *  \par Sources
 *
 *  - MCX128 GAL source, Darren Atkinson
 *    MCX128 Hardware Info, Darren Atkinson
 *    https://mcxwares.blogspot.com/2020/04/mcx128.html
 */

#include "top-config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "sds.h"

#include "cart.h"
#include "fs.h"
#include "logging.h"
#include "mc10_cart.h"
#include "part.h"
#include "ram.h"
#include "rombank.h"
#include "romlist.h"
#include "serialise.h"
#include "xroar.h"

struct mcx128 {
	struct mc10_cart cart;

	struct ram *ram;
	struct rombank *rom;

	unsigned map_mode;
	uint8_t P0;
	uint8_t P1;
	uint16_t intram_mask;
};

static const struct ser_struct ser_struct_mcx128[] = {
	SER_ID_STRUCT_NEST(1, &mc10_cart_ser_struct_data),
};

static const struct ser_struct_data mcx128_ser_struct_data = {
	.elems = ser_struct_mcx128,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mcx128),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mcx128_config_complete(struct cart_config *);

static void mcx128_reset(struct cart *, _Bool hard);
static uint8_t mcx128_read(struct mc10_cart *, uint16_t A, uint8_t D);
static uint8_t mcx128_write(struct mc10_cart *, uint16_t A, uint8_t D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MCX128 part creation

static struct part *mcx128_allocate(void);
static void mcx128_initialise(struct part *p, void *options);
static _Bool mcx128_finish(struct part *p);
static void mcx128_free(struct part *p);

static const struct partdb_entry_funcs mcx128_funcs = {
	.allocate = mcx128_allocate,
	.initialise = mcx128_initialise,
	.finish = mcx128_finish,
	.free = mcx128_free,

	.ser_struct_data = &mcx128_ser_struct_data,

	.is_a = mc10_cart_is_a,
};

const struct cart_partdb_entry mcx128_part = { .partdb_entry = { .name = "mcx128", .description = "Darren Atkinson | MCX128 memory expansion", .funcs = &mcx128_funcs }, .config_complete = mcx128_config_complete };

static struct part *mcx128_allocate(void) {
	struct mcx128 *n = part_new(sizeof(*n));
	struct mc10_cart *cm = &n->cart;
	struct cart *c = &cm->cart;
	struct part *p = &c->part;

	*n = (struct mcx128){0};

	cm->read = mcx128_read;
	cm->write = mcx128_write;
	c->reset = mcx128_reset;

	cm->signal_nmi = DELEGATE_DEFAULT1(void, bool);

	return p;
}

static struct ram *mcx128_create_ram(void) {
	// 8 banks * 16K SRAM
	struct ram_config ram_config = {
		.d_width = 8,
		.organisation = RAM_ORG(14, 14, 0),
	};
	struct ram *ram = (struct ram *)part_create("ram", &ram_config);
	for (unsigned i = 0; i < 8; ++i) {
		ram_add_bank(ram, i);
	}
	return ram;
}

static void mcx128_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	struct mcx128 *n = (struct mcx128 *)p;
	struct mc10_cart *cm = &n->cart;
	struct cart *c = &cm->cart;

	n->intram_mask = 0xf000;  // 4K internal
	//n->intram_mask = 0xe000;  // 8K internal

	c->config = cc;

	// RAM
	struct ram *ram = mcx128_create_ram();
	part_add_component(p, (struct part *)ram, "EXTRAM");
}

static _Bool mcx128_finish(struct part *p) {
	struct mcx128 *n = (struct mcx128 *)p;
	struct mc10_cart *cm = &n->cart;
	struct cart *c = &cm->cart;
	struct cart_config *cc = c->config;

	// Find attached parts
	n->ram = (struct ram *)part_component_by_id_is_a(p, "EXTRAM", "ram");

	// Check all required parts are attached
	if (!n->ram) {
		return 0;
	}

	{
		unsigned rom_size = 0x1000;

		if (cc->rom) {
			sds tmp = romlist_find(cc->rom);
			if (tmp) {
				FILE *fd = fopen(tmp, "rb");
				if (fd) {
					off_t fsize = fs_file_size(fd);
					if (fsize > 0x2000) {
						rom_size = 0x4000;
					} else if (fsize > 0x1000) {
						rom_size = 0x2000;
					}
					fclose(fd);
				}
				sdsfree(tmp);
			}
		}

		n->rom = rombank_new(8, rom_size, 1);
	}

	if (cc->rom) {
		sds tmp = romlist_find(cc->rom);
		if (tmp) {
			rombank_load_image(n->rom, 0, tmp, 0);
		}
		sdsfree(tmp);
	}

	rombank_report(n->rom, "mcx128", "MCX128 ROM");

	// RAM
	ram_report(n->ram, "mcx128", "extended RAM");

	return 1;
}

static void mcx128_free(struct part *p) {
	struct mcx128 *n = (struct mcx128 *)p;
	rombank_free(n->rom);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mcx128_config_complete(struct cart_config *cc) {
	// Default ROM
	if (!cc->rom_dfn && !cc->rom) {
		cc->rom = xstrdup("@mcx128");
	}
}

static void mcx128_reset(struct cart *c, _Bool hard) {
	struct mcx128 *n = (struct mcx128 *)c;

	n->P1 = n->P0 = 0;
	n->map_mode = 0;

	if (hard) {
		// SRAM, so assume a random pattern on startup
		ram_clear(n->ram, ram_init_random);
	}
}

enum {
	map_mode_all_rom = 0,
	map_mode_ext_rom_ram = 1,
	map_mode_int_rom_ram = 2,
	map_mode_all_ram = 3,
};

static uint8_t mcx128_cycle(struct mc10_cart *c, _Bool RnW, uint16_t A, uint8_t D) {
	struct mcx128 *n = (struct mcx128 *)c;

	unsigned page = (A >> 14) & 3;  // 16K pages
	_Bool intram = (A & n->intram_mask) == 0x4000; // 4000-4FFF (4K) / 4000-5FFF (8K)
	_Bool io_page   = (A & 0xff00) == 0xbf00;      // BF00-BFFF
	_Bool reg_addrs = (A & 0xff80) == 0xbf00;      // BF00-BF7F
	_Bool kbd_vdg   = (A & 0xff80) == 0xbf80;      // BF80-BFFF
	_Bool rom_low   = (A & 0xe000) == 0xc000;      // C000-DFFF
	_Bool rom_hi    = (A & 0xe000) == 0xe000;      // E000-FFFF
	_Bool unbanked  = (A & 0xff00) == 0xff00;      // FF00-FFFF

	_Bool internal = (intram && !n->P1)  // Internal RAM range (4000-4FFF or 5FFF) when Page 1 Bank is 0
	                 || kbd_vdg         // Keyboard / VDG (BF80-BFFF)
	                 // Reading the upper 8K ROM region (E000-FFFF) using Internal ROM map mode
		         || (rom_hi && (n->map_mode == map_mode_int_rom_ram) && RnW);

	_Bool rom = (page == 3 && (n->map_mode == map_mode_all_rom))  // Either ROM region (C000-FFFF) using All ROM map mode
	            || (rom_hi && (n->map_mode == map_mode_ext_rom_ram));  // Upper 8K ROM region (E000-FFFF) using External ROM map mode

	_Bool ram = (page == 0)                  // 0000-3FFF
	            || (page == 1 && !internal)  // 4000-7FFF (4000-4FFF or 5FFF only while Page 1 Bank is 1)
		    || (page == 2 && !io_page)   // 8000-BEFF
		    || (page == 3 && !RnW)       // C000-FFFF (Writes to ROM space always pass thru to RAM)
	            || (rom_low && (n->map_mode != map_mode_all_rom))     // Lower 8K ROM region (C000-DFFF) except when using All ROM map mode
	            || (rom_hi && (n->map_mode == map_mode_all_ram));      // Upper 8K ROM region when using All RAM map mode

	c->SEL = !internal;

	if (reg_addrs) {
		if ((A & 1) == 0) {
			// Read/write bank register on even address
			if (RnW) {
				D = (D & ~0x03) | n->P1 | n->P0;
			} else {
				n->P1 = D & 2;
				n->P0 = D & 1;
			}
		} else {
			// Read/write map mode register on odd address
			if (RnW) {
				D = (D & ~0x03) | n->map_mode;
			} else {
				n->map_mode = D & 3;
			}
		}
	} else if (rom && RnW) {
		rombank_d8(n->rom, A & 0x3fff, &D);
	} else if (ram) {
		unsigned ram_page = page;
		if ((page == 0 && n->P0)  // Use P0 for lower 16K (0000-3FFF)
		    || (page == 1 && n->P1)  // Use P1 for the..
		    || (page == 2 && n->P1)  // ..middle 32K (4000-BFFF)
		    // Use P0 for upper 16K minus last 256 bytes (C000-FEFF)
		    || (page == 3 && n->P0 && !unbanked)) {
			ram_page |= 4;
		}
		ram_d8(n->ram, RnW, ram_page, A & 0x3fff, 0, &D);
	}

	return D;
}

static uint8_t mcx128_read(struct mc10_cart *c, uint16_t A, uint8_t D) {
	return mcx128_cycle(c, 1, A, D);
}

static uint8_t mcx128_write(struct mc10_cart *c, uint16_t A, uint8_t D) {
	return mcx128_cycle(c, 0, A, D);
}
