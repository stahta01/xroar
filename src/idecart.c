/** \file
 *
 *  \brief "Glenside" IDE cartridge support.
 *
 *  \copyright Copyright 2015-2019 Alan Cox
 *
 *  \copyright Copyright 2015-2024 Ciaran Anscomb
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
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "becker.h"
#include "blockdev.h"
#include "cart.h"
#include "ide.h"
#include "logging.h"
#include "part.h"
#include "rombank.h"
#include "serialise.h"
#include "xconfig.h"
#include "xroar.h"

struct idecart {
	struct cart cart;
	struct ide_controller *controller;
	struct becker *becker;
	uint16_t io_region;
	uint8_t data_latch;  // upper 8-bits of 16-bit IDE data
	int msgr_client_id;  // messenger client id
};

static const struct ser_struct ser_struct_idecart[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_TYPE(2, ser_type_unhandled, struct idecart, controller),
	SER_ID_STRUCT_ELEM(3, struct idecart, io_region),
	SER_ID_STRUCT_ELEM(4, struct idecart, data_latch),
};

#define IDECART_SER_CONTROLLER (2)

static _Bool idecart_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool idecart_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data idecart_ser_struct_data = {
	.elems = ser_struct_idecart,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_idecart),
	.read_elem = idecart_read_elem,
	.write_elem = idecart_write_elem,
};

static struct xconfig_option const idecart_options[] = {
	{ XCO_SET_UINT16("ide-addr", struct idecart, io_region) },
	{ XC_OPT_END() }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void idecart_config_complete(struct cart_config *);
static void idecart_ui_set_hd_filename(void *, int tag, void *smsg);

static uint8_t idecart_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t idecart_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void idecart_reset(struct cart *c, _Bool hard);
static void idecart_detach(struct cart *c);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// IDE cartridge part creation

static struct part *idecart_allocate(void);
static void idecart_initialise(struct part *p, void *options);
static _Bool idecart_finish(struct part *p);
static void idecart_free(struct part *p);

static const struct partdb_entry_funcs idecart_funcs = {
	.allocate = idecart_allocate,
	.initialise = idecart_initialise,
	.finish = idecart_finish,
	.free = idecart_free,

	.ser_struct_data = &idecart_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry idecart_part = { .partdb_entry = { .name = "ide", .description = "Glenside | IDE interface", .funcs = &idecart_funcs } , .config_complete = idecart_config_complete };

static struct part *idecart_allocate(void) {
	struct idecart *ide = part_new(sizeof(*ide));
	struct cart *c = &ide->cart;
	struct part *p = &c->part;

	*ide = (struct idecart){0};

	cart_rom_init(c);

	c->read = idecart_read;
	c->write = idecart_write;
	c->reset = idecart_reset;
	c->detach = idecart_detach;

	// Controller is an important component of the cartridge.
	// TODO: turn this into a "part".
	ide->controller = ide_allocate("ide0");
	if (ide->controller == NULL) {
		perror(NULL);
		part_free(&c->part);
		return NULL;
	}
	ide->io_region = 0xff50;

	return p;
}

static void idecart_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	struct idecart *ide = (struct idecart *)p;

	cart_rom_initialise(p, options);

	xconfig_parse_list_struct(idecart_options, cc->opts, ide);
	ide->io_region &= 0xfff0;
}

static _Bool idecart_finish(struct part *p) {
	struct idecart *ide = (struct idecart *)p;
	struct cart *c = &ide->cart;

	ide_reset_begin(ide->controller);

	if (!cart_rom_finish(p)) {
		return 0;
	}

	// Join the ui messenger groups we're interested in
	ide->msgr_client_id = messenger_client_register();
	ui_messenger_join_group(ide->msgr_client_id, ui_tag_hd_filename, MESSENGER_NOTIFY_DELEGATE(idecart_ui_set_hd_filename, ide));

	if (c->config->becker_port) {
		ide->becker = becker_open();
	}

	return 1;
}

static void idecart_free(struct part *p) {
	struct idecart *ide = (struct idecart *)p;
	becker_close(ide->becker);
	messenger_client_unregister(ide->msgr_client_id);
	cart_rom_free(p);
	ide_free(ide->controller);
}

static _Bool idecart_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct idecart *ide = sptr;
	switch (tag) {
	case IDECART_SER_CONTROLLER:
		ide_deserialise(ide->controller, sh);
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool idecart_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct idecart *ide = sptr;
	switch (tag) {
	case IDECART_SER_CONTROLLER:
		ide_serialise(ide->controller, sh, tag);
		break;
	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void idecart_config_complete(struct cart_config *cc) {
	// Default ROM
	if (!cc->rom_dfn && !cc->rom) {
		cc->rom = xstrdup("@glenside_ide");
	}
}

static void idecart_ui_set_hd_filename(void *sptr, int tag, void *smsg) {
	struct idecart *ide = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_hd_filename);

	int drive = uimsg->value;
	const char *filename = uimsg->data;

	if (drive < 0 || drive > 1) {
		return;
	}

	if (ide->controller->drive[drive].present) {
		ide_detach(&ide->controller->drive[drive]);
	}
	if (filename) {
		struct blkdev *bd = bd_open(filename);
		if (bd) {
			ide_attach(ide->controller, drive, bd);
		}
	}
	ide_reset_drive(&ide->controller->drive[drive]);
}

static uint8_t idecart_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct idecart *ide = (struct idecart *)c;

	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
	}

	if ((A & 0xfff0) != ide->io_region) {
		if (P2 && ide->becker) {
			if (A == 0xff41)
				D = becker_read_status(ide->becker);
			else if (A == 0xff42)
				D = becker_read_data(ide->becker);
		}
		return D;
	}

	if (P2) {
		// if mapped to $FF5x, we'd get called twice
		return D;
	}

	if (A & 8) {
		// Read from latch
		D = ide->data_latch;
	} else {
		// Read from IDE controller
		uint16_t v = ide_read16(ide->controller, A & 7);
		ide->data_latch = v >> 8;
		D = v & 0xff;
	}

	return D;
}

static uint8_t idecart_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct idecart *ide = (struct idecart *)c;

	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
	}

	if ((A & 0xfff0) != ide->io_region) {
		if (P2 && ide->becker) {
			if (A == 0xff42) {
				becker_write_data(ide->becker, D);
			}
		}
		return D;
	}

	if (P2) {
		// if mapped to $FF5x, we'd get called twice
		return D;
	}

	if (A & 8) {
		// Write to latch
		ide->data_latch = D;
	} else {
		// Write to IDE controller
		uint16_t v = (ide->data_latch << 8) | D;
		ide_write16(ide->controller, A & 7, v);
	}

	return D;
}

static void idecart_reset(struct cart *c, _Bool hard) {
	struct idecart *ide = (struct idecart *)c;
	cart_rom_reset(c, hard);
	if (ide->becker)
		becker_reset(ide->becker);

	ide_reset_begin(ide->controller);
}

static void idecart_detach(struct cart *c) {
	struct idecart *ide = (struct idecart *)c;
	if (ide->becker)
		becker_reset(ide->becker);
	cart_rom_detach(c);
}
