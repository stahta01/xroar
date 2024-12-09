/** \file
 *
 *  \brief NX32 RAM expansion cartridge.
 *
 *  \copyright Copyright 2016-2018 Tormod Volden
 *
 *  \copyright Copyright 2016-2024 Ciaran Anscomb
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

#include "top-config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "becker.h"
#include "cart.h"
#include "part.h"
#include "serialise.h"
#include "spi65.h"
#include "xroar.h"

// number of 32KB banks in memory cartridge: 1, 4 or 16
#define EXTBANKS 16

struct nx32 {
	struct cart cart;
	struct spi65 *spi65;
	uint8_t *extmem;
	_Bool extmem_map;
	_Bool extmem_ty;
	uint8_t extmem_bank;
	struct becker *becker;
	int msgr_client_id;  // messenger client id
};

#define NX32_SER_EXTMEM  (2)

static const struct ser_struct ser_struct_nx32[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_UNHANDLED(NX32_SER_EXTMEM),
	SER_ID_STRUCT_ELEM(3, struct nx32, extmem_map),
	SER_ID_STRUCT_ELEM(4, struct nx32, extmem_ty),
	SER_ID_STRUCT_ELEM(5, struct nx32, extmem_bank),
};

static _Bool nx32_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool nx32_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data nx32_ser_struct_data = {
	.elems = ser_struct_nx32,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_nx32),
	.read_elem = nx32_read_elem,
	.write_elem = nx32_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void nx32_ui_set_hd_filename(void *, int tag, void *smsg);

static void nx32_reset(struct cart *c, _Bool hard);
static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void nx32_detach(struct cart *c);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// NX32 part creation

static struct part *nx32_allocate(void);
static void nx32_initialise(struct part *p, void *options);
static _Bool nx32_finish(struct part *p);
static void nx32_free(struct part *p);

static const struct partdb_entry_funcs nx32_funcs = {
	.allocate = nx32_allocate,
	.initialise = nx32_initialise,
	.finish = nx32_finish,
	.free = nx32_free,

	.ser_struct_data = &nx32_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry nx32_part = { .partdb_entry = { .name = "nx32", .description = "Tormod Volden | NX32 memory cartridge", .funcs = &nx32_funcs } };

static struct part *nx32_allocate(void) {
	struct nx32 *n = part_new(sizeof(*n));
	struct cart *c = &n->cart;
	struct part *p = &c->part;

	*n = (struct nx32){0};

	cart_rom_init(c);

	c->read = nx32_read;
	c->write = nx32_write;
	c->reset = nx32_reset;
	c->detach = nx32_detach;

	return p;
}

static void nx32_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	struct nx32 *n = (struct nx32 *)p;
	struct cart *c = &n->cart;

	cart_rom_initialise(p, options);

	// 65SPI/B for interfacing to SD card
	struct spi65 *spi65 = (struct spi65 *)part_create("65SPI-B", NULL);
	part_add_component(&c->part, (struct part *)spi65, "SPI65");
}

static _Bool nx32_finish(struct part *p) {
	struct nx32 *n = (struct nx32 *)p;
	struct cart *c = &n->cart;

	// Find attached parts
	n->spi65 = (struct spi65 *)part_component_by_id_is_a(p, "SPI65", "65SPI-B");

	// Check all required parts are attached
	if (!n->spi65) {
		return 0;
	}

	if (!cart_rom_finish(p)) {
		return 0;
	}

	n->extmem = xmalloc(0x8000 * EXTBANKS);
	memset(n->extmem, 0, 0x8000 * EXTBANKS);

	// Join the ui messenger groups we're interested in
	n->msgr_client_id = messenger_client_register();
	ui_messenger_join_group(n->msgr_client_id, ui_tag_hd_filename, MESSENGER_NOTIFY_DELEGATE(nx32_ui_set_hd_filename, n));

	if (c->config->becker_port) {
		n->becker = becker_open();
	}

	return 1;
}

static void nx32_free(struct part *p) {
	struct nx32 *n = (struct nx32 *)p;
	becker_close(n->becker);
	messenger_client_unregister(n->msgr_client_id);
	free(n->extmem);
	cart_rom_free(p);
}

static _Bool nx32_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct nx32 *n = sptr;
	switch (tag) {
	case NX32_SER_EXTMEM:
		ser_read(sh, n->extmem, sizeof(n->extmem));
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool nx32_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct nx32 *n = sptr;
	switch (tag) {
	case NX32_SER_EXTMEM:
		ser_write(sh, tag, n->extmem, sizeof(n->extmem));
		break;
	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void nx32_ui_set_hd_filename(void *sptr, int tag, void *smsg) {
	struct nx32 *n = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_hd_filename);

	int drive = uimsg->value;

	if (drive != 0) {
		return;
	}

	spi65_remove_device(n->spi65, 0);

	if (uimsg->data) {
		// XXX can i make the options passed to part_create const?
		char *filename = xstrdup((const char *)uimsg->data);
		struct spi65_device *sdcard = (struct spi65_device *)part_create("SPI-SDCARD", filename);
		spi65_add_device(n->spi65, sdcard, 0);
		free(filename);
	}
}

static void nx32_reset(struct cart *c, _Bool hard) {
	struct nx32 *n = (struct nx32 *)c;
	cart_rom_reset(c, hard);
	n->extmem_map = 0;
	n->extmem_ty = 0;
	n->extmem_bank = 0;
	if (n->becker)
		becker_reset(n->becker);
	spi65_reset(n->spi65);
}

static void nx32_detach(struct cart *c) {
	struct nx32 *n = (struct nx32 *)c;
	if (n->becker)
		becker_reset(n->becker);
	cart_rom_detach(c);
}

static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct nx32 *n = (struct nx32 *)c;
	(void)R2;
	c->EXTMEM = 0;

	if ((A & 0xFFFC) == 0xFF6C)
		return spi65_read(n->spi65, A & 3);

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
		spi65_write(n->spi65, A & 3, D);

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
