/** \file
 *
 *  \brief Orchestra 90-CC sound cartridge.
 *
 *  \copyright Copyright 2013-2021 Ciaran Anscomb
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
	SER_STRUCT_ELEM(struct orch90, cart, ser_type_unhandled), // 1
	SER_STRUCT_ELEM(struct orch90, left, ser_type_uint8), // 2
	SER_STRUCT_ELEM(struct orch90, right, ser_type_uint8), // 3
};

#define N_SER_STRUCT_ORCH90 ARRAY_N_ELEMENTS(ser_struct_orch90)

#define ORCH90_SER_CART  (1)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static uint8_t orch90_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void orch90_reset(struct cart *c);
static void orch90_attach(struct cart *c);
static void orch90_detach(struct cart *c);
static _Bool orch90_has_interface(struct cart *c, const char *ifname);
static void orch90_attach_interface(struct cart *c, const char *ifname, void *intf);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Orchestra 90-CC part creation

static struct part *orch90_allocate(void);
static void orch90_initialise(struct part *p, void *options);
static _Bool orch90_finish(struct part *p);

static struct part *orch90_deserialise(struct ser_handle *sh);
static void orch90_serialise(struct part *p, struct ser_handle *sh);

static const struct partdb_entry_funcs orch90_funcs = {
	.allocate = orch90_allocate,
	.initialise = orch90_initialise,
	.finish = orch90_finish,

	.deserialise = orch90_deserialise,
	.serialise = orch90_serialise,

	.is_a = cart_is_a,
};

const struct partdb_entry orch90_part = { .name = "orch90", .description = "Orchestra 90-CC", .funcs = &orch90_funcs };

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

	struct orch90 *o = (struct orch90 *)p;
	struct cart *c = &o->cart;

	c->config = cc;
}

static _Bool orch90_finish(struct part *p) {
	struct orch90 *o = (struct orch90 *)p;
	// Nothing to do...
	(void)o;
	return 1;
}

static struct part *orch90_deserialise(struct ser_handle *sh) {
	struct part *p = orch90_allocate();
	struct orch90 *o = (struct orch90 *)p;
	int tag;
	while (!ser_error(sh) && (tag = ser_read_struct(sh, ser_struct_orch90, N_SER_STRUCT_ORCH90, o))) {
		switch (tag) {
		case ORCH90_SER_CART:
			cart_deserialise(&o->cart, sh);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	if (ser_error(sh)) {
		part_free(p);
		return NULL;
	}
	return p;
}

static void orch90_serialise(struct part *p, struct ser_handle *sh) {
	struct orch90 *o = (struct orch90 *)p;
	for (int tag = 1; !ser_error(sh) && (tag = ser_write_struct(sh, ser_struct_orch90, N_SER_STRUCT_ORCH90, tag, o)) > 0; tag++) {
		switch (tag) {
		case ORCH90_SER_CART:
			cart_serialise(&o->cart, sh, tag);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	ser_write_close_tag(sh);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void orch90_reset(struct cart *c) {
	cart_rom_reset(c);
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
