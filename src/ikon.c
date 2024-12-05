/** \file
 *
 *  \brief Ikon Ultra Drive cartridge.
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
 */

// XXX WIP
//
// Currently ONLY implements the presence of the ROM and a PIA on the
// cartridge.

#include "top-config.h"

#ifdef WANT_EXPERIMENTAL

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "delegate.h"

#include "cart.h"
#include "logging.h"
#include "mc6821.h"
#include "part.h"
#include "rombank.h"
#include "serialise.h"

struct ikon {
	struct cart cart;

	struct MC6821 *PIA;

	_Bool nmi_state;
};

static const struct ser_struct ser_struct_ikon[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
};

static const struct ser_struct_data ikon_ser_struct_data = {
	.elems = ser_struct_ikon,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_ikon),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void ikon_config_complete(struct cart_config *);

/* Cart interface */
static uint8_t ikon_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t ikon_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void ikon_reset(struct cart *c, _Bool hard);
static void ikon_detach(struct cart *c);
static _Bool ikon_has_interface(struct cart *c, const char *ifname);
static void ikon_attach_interface(struct cart *c, const char *ifname, void *intf);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Ikon Ultra Drive cartridge part creation

static struct part *ikon_allocate(void);
static void ikon_initialise(struct part *p, void *options);
static _Bool ikon_finish(struct part *p);
static void ikon_free(struct part *p);

static const struct partdb_entry_funcs ikon_funcs = {
	.allocate = ikon_allocate,
	.initialise = ikon_initialise,
	.finish = ikon_finish,
	.free = ikon_free,

	.ser_struct_data = &ikon_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry ikon_part = { .partdb_entry = { .name = "ikon", .description = "Ikon | Ultra Drive", .funcs = &ikon_funcs }, .config_complete = ikon_config_complete };

static struct part *ikon_allocate(void) {
	struct ikon *d = part_new(sizeof(*d));
	struct cart *c = &d->cart;
	struct part *p = &c->part;

	*d = (struct ikon){0};

	cart_rom_init(c);

	c->detach = ikon_detach;
	c->read = ikon_read;
	c->write = ikon_write;
	c->reset = ikon_reset;
	c->has_interface = ikon_has_interface;
	c->attach_interface = ikon_attach_interface;

	return p;
}

static void ikon_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	cart_rom_initialise(p, options);
	part_add_component(p, part_create("MC6821", "MC6821"), "PIA");
}

static _Bool ikon_finish(struct part *p) {
	struct ikon *d = (struct ikon *)p;
	struct cart *c = &d->cart;
	(void)c;

	// Find attached parts
	d->PIA = (struct MC6821 *)part_component_by_id_is_a(p, "PIA", "MC6821");

	// Check all required parts are attached
	if (d->PIA == NULL) {
		return 0;
	}

	if (!cart_rom_finish(p)) {
		return 0;
	}

	return 1;
}

static void ikon_free(struct part *p) {
	struct ikon *d = (struct ikon *)p;
	(void)d;
	cart_rom_free(p);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void ikon_config_complete(struct cart_config *cc) {
	// Default ROM
	if (!cc->rom_dfn && !cc->rom) {
		cc->rom = xstrdup("@ikon");
	}
}

static void ikon_reset(struct cart *c, _Bool hard) {
	struct ikon *d = (struct ikon *)c;
	cart_rom_reset(c, hard);
	mc6821_reset(d->PIA);
}

static void ikon_detach(struct cart *c) {
	struct ikon *d = (struct ikon *)c;
	(void)d;
	cart_rom_detach(c);
}

static uint8_t ikon_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct ikon *d = (struct ikon *)c;

	if (d->nmi_state != d->PIA->a.irq) {
		d->nmi_state = d->PIA->a.irq;
		DELEGATE_CALL(c->signal_nmi, d->nmi_state);
	}

	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
	}
	if (!P2) {
		return D;
	}
	if ((A & 0xc) != 0xc) {
		return D;
	}
	D = mc6821_read(d->PIA, A);
	return D;
}

static uint8_t ikon_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct ikon *d = (struct ikon *)c;

	if (d->nmi_state != d->PIA->a.irq) {
		d->nmi_state = d->PIA->a.irq;
		DELEGATE_CALL(c->signal_nmi, d->nmi_state);
	}

	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
	}
	if (!P2) {
		return D;
	}
	if ((A & 0xc) != 0xc) {
		return D;
	}
	mc6821_write(d->PIA, A, D);
	return D;
}

static _Bool ikon_has_interface(struct cart *c, const char *ifname) {
	(void)c;
	(void)ifname;
	return 0;
}

static void ikon_attach_interface(struct cart *c, const char *ifname, void *intf) {
	struct ikon *d = (struct ikon *)c;
	(void)d;
	(void)ifname;
	(void)intf;
}

#endif
