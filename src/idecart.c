/*

"Glenside" IDE cartridge support

Copyright 2015 Alan Cox
Copyright 2015-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "xalloc.h"

#include "cart.h"
#include "logging.h"
#include "xroar.h"

#include "becker.h"
#include "ide.h"

static struct cart *idecart_new(struct cart_config *cc);

struct cart_module cart_ide_module = {
	.name = "ide",
	.description = "Glenside IDE",
	.new = idecart_new,
};

struct idecart {
	struct cart cart;
	struct ide_controller *controller;
	struct becker *becker;
};

static void idecart_reset(struct cart *c) {
	struct idecart *ide = (struct idecart *)c;
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

static void idecart_free(struct cart *c) {
	struct idecart *ide = (struct idecart *)c;
	if (ide->becker)
		becker_free(ide->becker);
	ide_free(ide->controller);
	cart_rom_free(c);
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

static void idecart_init(struct idecart *ide) {
	struct cart *c = (struct cart *)ide;
	struct cart_config *cc = c->config;
	int fd;

	cart_rom_init(c);

	c->detach = idecart_detach;
	c->free = idecart_free;

	c->read = idecart_read;
	c->write = idecart_write;
	c->reset = idecart_reset;

	if (cc->becker_port) {
		ide->becker = becker_new();
	}

	ide->controller = ide_allocate("ide0");
	if (ide->controller == NULL) {
		perror(NULL);
		exit(1);
	}
	fd = open("hd0.img", O_RDWR);
	if (fd == -1) {
		perror("hd0.img");
		return;
	}
	ide_attach(ide->controller, 0, fd);
	ide_reset_begin(ide->controller);
}

static struct cart *idecart_new(struct cart_config *cc) {
	struct idecart *ide = xmalloc(sizeof(*ide));
	ide->cart.config = cc;
	idecart_init(ide);
	return &ide->cart;
}
