/** \file
 *
 *  \brief Premier Microsystems' Delta disk system.
 *
 *  \copyright Copyright 2007-2024 Ciaran Anscomb
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
 *  Delta cartridge detail:
 *
 *  - Partly inferred from disassembly of Delta ROM,
 *
 *  - Partly from information provided by Phill Harvey-Smith.
 */

#include "top-config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"

#include "cart.h"
#include "logging.h"
#include "part.h"
#include "rombank.h"
#include "serialise.h"
#include "vdrive.h"
#include "wd279x.h"

struct deltados {
	struct cart cart;
	unsigned latch_old;
	unsigned latch_drive_select;
	_Bool latch_side_select;
	_Bool latch_density;
	struct WD279X *fdc;
	struct vdrive_interface *vdrive_interface;
};

static const struct ser_struct ser_struct_deltados[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_ELEM(2, struct deltados, latch_drive_select),
	SER_ID_STRUCT_ELEM(3, struct deltados, latch_side_select),
	SER_ID_STRUCT_ELEM(4, struct deltados, latch_density),
};

static const struct ser_struct_data deltados_ser_struct_data = {
	.elems = ser_struct_deltados,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_deltados),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void deltados_config_complete(struct cart_config *);

/* Cart interface */

static uint8_t deltados_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t deltados_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void deltados_reset(struct cart *c, _Bool hard);
static void deltados_detach(struct cart *c);
static _Bool deltados_has_interface(struct cart *c, const char *ifname);
static void deltados_attach_interface(struct cart *c, const char *ifname, void *intf);

/* Latch */

static void latch_write(struct deltados *d, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Delta system part creation

static struct part *deltados_allocate(void);
static void deltados_initialise(struct part *p, void *options);
static _Bool deltados_finish(struct part *p);
static void deltados_free(struct part *p);

static const struct partdb_entry_funcs deltados_funcs = {
	.allocate = deltados_allocate,
	.initialise = deltados_initialise,
	.finish = deltados_finish,
	.free = deltados_free,

	.ser_struct_data = &deltados_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry deltados_part = { .partdb_entry = { .name = "delta", .description = "Premier Microsystems | Delta System", .funcs = &deltados_funcs }, .config_complete = deltados_config_complete };

static struct part *deltados_allocate(void) {
	struct deltados *d = part_new(sizeof(*d));
	struct cart *c = &d->cart;
	struct part *p = &c->part;

	*d = (struct deltados){0};

	cart_rom_init(c);

	c->detach = deltados_detach;
	c->read = deltados_read;
	c->write = deltados_write;
	c->reset = deltados_reset;
	c->has_interface = deltados_has_interface;
	c->attach_interface = deltados_attach_interface;

	return p;
}

static void deltados_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	cart_rom_initialise(p, options);
	part_add_component(p, part_create("WD2791", "WD2791"), "FDC");
}

static _Bool deltados_finish(struct part *p) {
	struct deltados *d = (struct deltados *)p;

	// Find attached parts
	d->fdc = (struct WD279X *)part_component_by_id_is_a(p, "FDC", "WD2791");

	// Check all required parts are attached
	if (d->fdc == NULL) {
		return 0;
	}

	if (!cart_rom_finish(p)) {
		return 0;
	}

	return 1;
}

static void deltados_free(struct part *p) {
	cart_rom_free(p);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void deltados_config_complete(struct cart_config *cc) {
	// Default ROM
	if (!cc->rom_dfn && !cc->rom) {
		cc->rom = xstrdup("@delta");
	}
}

static void deltados_reset(struct cart *c, _Bool hard) {
	struct deltados *d = (struct deltados *)c;
	cart_rom_reset(c, hard);
	wd279x_reset(d->fdc);
	d->latch_old = -1;
	latch_write(d, 0);
}

static void deltados_detach(struct cart *c) {
	struct deltados *d = (struct deltados *)c;
	vdrive_disconnect(d->vdrive_interface);
	wd279x_disconnect(d->fdc);
	cart_rom_detach(c);
}

static uint8_t deltados_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct deltados *d = (struct deltados *)c;
	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
	}
	if (!P2) {
		return D;
	}
	if ((A & 4) == 0)
		return wd279x_read(d->fdc, A);
	return D;
}

static uint8_t deltados_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct deltados *d = (struct deltados *)c;
	(void)R2;
	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
	}
	if (!P2) {
		return D;
	}
	if ((A & 4) == 0) {
		wd279x_write(d->fdc, A, D);
	} else {
		latch_write(d, D);
	}
	return D;
}

static _Bool deltados_has_interface(struct cart *c, const char *ifname) {
	return c && (0 == strcmp(ifname, "floppy"));
}

static void deltados_attach_interface(struct cart *c, const char *ifname, void *intf) {
	if (!c || (0 != strcmp(ifname, "floppy")))
		return;
	struct deltados *d = (struct deltados *)c;
	d->vdrive_interface = intf;

	d->fdc->set_dirc = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dirc, d->vdrive_interface);
	d->fdc->set_dden = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dden, d->vdrive_interface);
	d->fdc->step = DELEGATE_AS0(void, d->vdrive_interface->step, d->vdrive_interface);
	d->fdc->write = DELEGATE_AS1(void, uint8, d->vdrive_interface->write, d->vdrive_interface);
	d->fdc->skip = DELEGATE_AS0(void, d->vdrive_interface->skip, d->vdrive_interface);
	d->fdc->read = DELEGATE_AS0(uint8, d->vdrive_interface->read, d->vdrive_interface);
	d->fdc->write_idam = DELEGATE_AS0(void, d->vdrive_interface->write_idam, d->vdrive_interface);
	d->fdc->time_to_next_byte = DELEGATE_AS0(unsigned, d->vdrive_interface->time_to_next_byte, d->vdrive_interface);
	d->fdc->time_to_next_idam = DELEGATE_AS0(unsigned, d->vdrive_interface->time_to_next_idam, d->vdrive_interface);
	d->fdc->next_idam = DELEGATE_AS0(uint8p, d->vdrive_interface->next_idam, d->vdrive_interface);
	d->fdc->update_connection = DELEGATE_AS0(void, d->vdrive_interface->update_connection, d->vdrive_interface);

	d->vdrive_interface->tr00 = DELEGATE_AS1(void, bool, wd279x_tr00, d->fdc);
	d->vdrive_interface->index_pulse = DELEGATE_AS1(void, bool, wd279x_index_pulse, d->fdc);
	d->vdrive_interface->write_protect = DELEGATE_AS1(void, bool, wd279x_write_protect, d->fdc);
	wd279x_update_connection(d->fdc);

	// tied high (assumed)
	wd279x_ready(d->fdc, 1);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void latch_write(struct deltados *d, unsigned D) {
	if (D != d->latch_old) {
		LOG_MOD_DEBUG(2, "delta", "config reg: ");
		if ((D ^ d->latch_old) & 0x03) {
			LOG_DEBUG(2, "DRIVE SELECT %01u, ", D & 0x03);
		}
		if ((D ^ d->latch_old) & 0x04) {
			LOG_DEBUG(2, "SIDE %s, ", (D & 0x04)?"1":"0");
		}
		if ((D ^ d->latch_old) & 0x10) {
			LOG_DEBUG(2, "DENSITY %s, ", (D & 0x10)?"DOUBLE":"SINGLE");
		}
		LOG_DEBUG(2, "\n");
		d->latch_old = D;
	}
	d->latch_drive_select = D & 0x03;
	d->latch_side_select = D & 0x04;
	if (d->vdrive_interface) {
		d->vdrive_interface->set_drive(d->vdrive_interface, d->latch_drive_select);
		d->vdrive_interface->set_sso(d->vdrive_interface, d->latch_side_select ? 1 : 0);
	}
	d->latch_density = !(D & 0x10);
	wd279x_set_dden(d->fdc, !d->latch_density);
}
