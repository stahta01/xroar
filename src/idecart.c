/** \file
 *
 *  \brief "Glenside" IDE cartridge support.
 *
 *  \copyright Copyright 2015-2019 Alan Cox
 *
 *  \copyright Copyright 2015-2022 Ciaran Anscomb
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "array.h"

#include "becker.h"
#include "blockdev.h"
#include "cart.h"
#include "ide.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"
#include "xconfig.h"
#include "xroar.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct idecart {
	struct cart cart;
	struct ide_controller *controller;
	struct becker *becker;
	uint16_t io_region;
	uint8_t data_latch;  // upper 8-bits of 16-bit IDE data
};

static const struct ser_struct ser_struct_idecart[] = {
	SER_STRUCT_NEST(&cart_ser_struct_data), // 1
	SER_STRUCT_ELEM(struct idecart, controller, ser_type_unhandled), // 2
	SER_STRUCT_ELEM(struct idecart, io_region, ser_type_uint16), // 3
	SER_STRUCT_ELEM(struct idecart, data_latch, ser_type_uint8), // 4
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

const struct partdb_entry idecart_part = { .name = "ide", .description = "Glenside IDE", .funcs = &idecart_funcs };

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
	struct cart *c = &ide->cart;

	c->config = cc;

	xconfig_parse_list_struct(idecart_options, cc->opts, ide);
	ide->io_region &= 0xfff0;
}

static _Bool idecart_finish(struct part *p) {
	struct idecart *ide = (struct idecart *)p;
	struct cart *c = &ide->cart;

	// Controller code depends on a valid filehandle being attached.
	for (int i = 0; i < 2; i++) {
		if (xroar_cfg.load_hd[i]) {
			struct blkdev *bd = bd_open(xroar_cfg.load_hd[i]);
			if (!bd) {
				int fd = open(xroar_cfg.load_hd[i], O_RDWR|O_CREAT|O_TRUNC|O_EXCL|O_BINARY, 0600);
				if (fd == -1) {
					perror(xroar_cfg.load_hd[i]);
					continue;
				}
				if (ide_make_drive(ACME_ZIPPIBUS, fd)) {
					fprintf(stderr, "IDE: unable to create %s.\n", xroar_cfg.load_hd[i]);
					close(fd);
					continue;
				}
				close(fd);
				bd = bd_open(xroar_cfg.load_hd[i]);
			}
			if (bd) {
				ide_attach(ide->controller, i, bd);
			}
		}
	}
	ide_reset_begin(ide->controller);

	cart_finish(c);
	if (c->config->becker_port) {
		ide->becker = becker_open();
	}

	return 1;
}

static void idecart_free(struct part *p) {
	struct idecart *ide = (struct idecart *)p;
	becker_close(ide->becker);
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

static uint8_t idecart_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct idecart *ide = (struct idecart *)c;

	if (R2) {
		return c->rom_data[A & 0x3FFF];
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
		return c->rom_data[A & 0x3FFF];
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
