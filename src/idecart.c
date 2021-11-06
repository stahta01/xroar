/** \file
 *
 *  \brief "Glenside" IDE cartridge support.
 *
 *  \copyright Copyright 2015-2019 Alan Cox
 *
 *  \copyright Copyright 2015-2021 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "array.h"

#include "becker.h"
#include "cart.h"
#include "ide.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"

struct idecart {
	struct cart cart;
	struct ide_controller *controller;
	struct becker *becker;
};

static const struct ser_struct ser_struct_idecart[] = {
	SER_STRUCT_NEST(&cart_ser_struct_data), // 1
	SER_STRUCT_ELEM(struct idecart, controller, ser_type_unhandled), // 2
};

#define N_SER_STRUCT_IDECART ARRAY_N_ELEMENTS(ser_struct_idecart)

#define IDECART_SER_CONTROLLER (2)

static _Bool idecart_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool idecart_write_elem(void *sptr, struct ser_handle *sh, int tag);

const struct ser_struct_data idecart_ser_struct_data = {
	.elems = ser_struct_idecart,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_idecart),
	.read_elem = idecart_read_elem,
	.write_elem = idecart_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static uint8_t idecart_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t idecart_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void idecart_reset(struct cart *c);
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

	.is_a = cart_is_a,
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

	return p;
}

static void idecart_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	struct idecart *ide = (struct idecart *)p;
	struct cart *c = &ide->cart;

	c->config = cc;
}

static _Bool idecart_finish(struct part *p) {
	struct idecart *ide = (struct idecart *)p;
	struct cart *c = &ide->cart;

	// Controller code depends on a valid filehandle being attached.
	int fd = open("hd0.img", O_RDWR);
	if (fd == -1) {
		fd = open("hd0.img", O_RDWR|O_CREAT|O_TRUNC|O_EXCL, 0600);
		if (fd == -1) {
			perror("hd0.img");
			return 0;
		}
		if (ide_make_drive(ACME_ZIPPIBUS, fd)) {
			fprintf(stderr, "Unable to create hd0.img.\n");
			close(fd);
			return 0;
		}
	}
	ide_attach(ide->controller, 0, fd);
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
	if (!P2) {
		return D;
	}
	if (A == 0xff58) {
		D = ide_read_latched(ide->controller, ide_data_latch);
	} else if (A == 0xff50) {
		D = ide_read_latched(ide->controller, ide_data);
	} else if (A > 0xff50 && A < 0xff58) {
		D = ide_read_latched(ide->controller, A - 0xff50);
	} else if (ide->becker) {
		// Becker port
		if (A == 0xff41)
			D = becker_read_status(ide->becker);
		else if (A == 0xff42)
			D = becker_read_data(ide->becker);
	}
	return D;
}

static uint8_t idecart_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct idecart *ide = (struct idecart *)c;
	(void)R2;

	if (R2) {
		return c->rom_data[A & 0x3FFF];
	}
	if (!P2) {
		return D;
	}

	if (A == 0xff58) {
		ide_write_latched(ide->controller, ide_data_latch, D);
		return D;
	}
	if (A == 0xff50) {
		ide_write_latched(ide->controller, ide_data, D);
		return D;
	}
	if (A > 0xff50 && A < 0xff58) {
		ide_write_latched(ide->controller, (A - 0xff50), D);
		return D;
	}
	if (ide->becker) {
		if (A == 0xff42)
			becker_write_data(ide->becker, D);
	}
	return D;
}

static void idecart_reset(struct cart *c) {
	struct idecart *ide = (struct idecart *)c;
	cart_rom_reset(c);
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
