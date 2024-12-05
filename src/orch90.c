/** \file
 *
 *  \brief Orchestra 90-CC sound cartridge.
 *
 *  \copyright Copyright 2013-2024 Ciaran Anscomb
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

#include "array.h"

#include "cart.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"
#include "sound.h"

struct orch90 {
	struct cart cart;
	uint8_t left;
	uint8_t right;
	struct sound_interface *snd;
};

static const struct ser_struct ser_struct_orch90[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_ELEM(2, struct orch90, left),
	SER_ID_STRUCT_ELEM(3, struct orch90, right),
};

static const struct ser_struct_data orch90_ser_struct_data = {
	.elems = ser_struct_orch90,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_orch90),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void orch90_config_complete(struct cart_config *);

static uint8_t orch90_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void orch90_reset(struct cart *c, _Bool hard);
static void orch90_attach(struct cart *c);
static void orch90_detach(struct cart *c);
static _Bool orch90_has_interface(struct cart *c, const char *ifname);
static void orch90_attach_interface(struct cart *c, const char *ifname, void *intf);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Orchestra 90-CC part creation

static struct part *orch90_allocate(void);
static void orch90_initialise(struct part *p, void *options);
static _Bool orch90_finish(struct part *p);

static const struct partdb_entry_funcs orch90_funcs = {
	.allocate = orch90_allocate,
	.initialise = orch90_initialise,
	.finish = orch90_finish,
	.free = cart_rom_free,

	.ser_struct_data = &orch90_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry orch90_part = { .partdb_entry = { .name = "orch90", .description = "Tandy | Orchestra 90-CC", .funcs = &orch90_funcs }, .config_complete = orch90_config_complete };

static struct part *orch90_allocate(void) {
	struct orch90 *o = part_new(sizeof(*o));
	struct cart *c = &o->cart;
	struct part *p = &c->part;

	*o = (struct orch90){0};

	cart_rom_init(c);

	c->write = orch90_write;
	c->reset = orch90_reset;
	c->attach = orch90_attach;
	c->detach = orch90_detach;
	c->has_interface = orch90_has_interface;
	c->attach_interface = orch90_attach_interface;

	return p;
}

static void orch90_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	cart_rom_initialise(p, options);
}

static _Bool orch90_finish(struct part *p) {
	if (!cart_rom_finish(p)) {
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void orch90_config_complete(struct cart_config *cc) {
	// Default ROM
	if (!cc->rom_dfn && !cc->rom) {
		cc->rom = xstrdup("orch90");
	}
}

static void orch90_reset(struct cart *c, _Bool hard) {
	cart_rom_reset(c, hard);
}

static void orch90_attach(struct cart *c) {
	cart_rom_attach(c);
}

static void orch90_detach(struct cart *c) {
	cart_rom_detach(c);
}

static _Bool orch90_has_interface(struct cart *c, const char *ifname) {
	return c && (0 == strcmp(ifname, "sound"));
}

static void orch90_attach_interface(struct cart *c, const char *ifname, void *intf) {
	if (!c || (0 != strcmp(ifname, "sound")))
		return;
	struct orch90 *o = (struct orch90 *)c;
	o->snd = intf;
}

static uint8_t orch90_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct orch90 *o = (struct orch90 *)c;
	(void)P2;
	(void)R2;
	if (A == 0xff7a) {
		o->left = D;
		sound_set_external_left(o->snd, (float)D / 255.);
	}
	if (A == 0xff7b) {
		o->right = D;
		sound_set_external_right(o->snd, (float)D / 255.);
	}
	return D;
}
