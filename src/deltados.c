/** \file
 *
 *  \brief Premier Microsystems' Delta disk system.
 *
 *  \copyright Copyright 2007-2021 Ciaran Anscomb
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
 *  - Partly from information provided by Phill Harvey-Smith on
 *    www.dragon-archive.co.uk.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"

#include "cart.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"
#include "vdrive.h"
#include "wd279x.h"

static struct cart *deltados_new(struct cart_config *cc);

struct cart_module cart_deltados_module = {
	.name = "delta",
	.description = "Delta System",
	.new = deltados_new,
};

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
	SER_STRUCT_ELEM(struct deltados, cart, ser_type_unhandled), // 1
	SER_STRUCT_ELEM(struct deltados, latch_drive_select, ser_type_unsigned), // 2
	SER_STRUCT_ELEM(struct deltados, latch_side_select, ser_type_bool), // 3
	SER_STRUCT_ELEM(struct deltados, latch_density, ser_type_bool), // 4
};
#define N_SER_STRUCT_DELTADOS ARRAY_N_ELEMENTS(ser_struct_deltados)

#define DELTADOS_SER_CART (1)

/* Cart interface */

static uint8_t deltados_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t deltados_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void deltados_reset(struct cart *c);
static void deltados_detach(struct cart *c);
static void deltados_free(struct part *p);
static void deltados_serialise(struct part *p, struct ser_handle *sh);
static _Bool deltados_has_interface(struct cart *c, const char *ifname);
static void deltados_attach_interface(struct cart *c, const char *ifname, void *intf);

/* Latch */

static void latch_write(struct deltados *d, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool deltados_finish(struct part *p) {
	struct deltados *d = (struct deltados *)p;

	// Find attached parts
	d->fdc = (struct WD279X *)part_component_by_id_is_a(p, "FDC", "WD2791");

	// Check all required parts are attached
	if (d->fdc == NULL) {
		return 0;
	}

	cart_finish(&d->cart);

	return 1;
}

static struct deltados *deltados_create(void) {
	struct deltados *d = part_new(sizeof(*d));
	struct cart *c = &d->cart;
	*d = (struct deltados){0};
	part_init(&c->part, "delta");
	c->part.free = deltados_free;
	c->part.serialise = deltados_serialise;
	c->part.finish = deltados_finish;
	c->part.is_a = cart_is_a;

	cart_rom_init(c);

	c->detach = deltados_detach;
	c->read = deltados_read;
	c->write = deltados_write;
	c->reset = deltados_reset;
	c->has_interface = deltados_has_interface;
	c->attach_interface = deltados_attach_interface;

	return d;
}

static struct cart *deltados_new(struct cart_config *cc) {
	assert(cc != NULL);

	struct deltados *d = deltados_create();
	struct cart *c = &d->cart;
	struct part *p = &c->part;
	c->config = cc;

	part_add_component(&c->part, (struct part *)wd279x_new(WD2791), "FDC");

	if (!deltados_finish(p)) {
		part_free(p);
		return NULL;
	}

	return c;
}

static void deltados_reset(struct cart *c) {
	struct deltados *d = (struct deltados *)c;
	cart_rom_reset(c);
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

static void deltados_free(struct part *p) {
	cart_rom_free(p);
}

static void deltados_serialise(struct part *p, struct ser_handle *sh) {
	struct deltados *d = (struct deltados *)p;
	for (int tag = 1; !ser_error(sh) && (tag = ser_write_struct(sh, ser_struct_deltados, N_SER_STRUCT_DELTADOS, tag, d)) > 0; tag++) {
		switch (tag) {
		case DELTADOS_SER_CART:
			cart_serialise(&d->cart, sh, tag);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	ser_write_close_tag(sh);
}

struct part *deltados_deserialise(struct ser_handle *sh) {
	struct deltados *d = deltados_create();
	int tag;
	while (!ser_error(sh) && (tag = ser_read_struct(sh, ser_struct_deltados, N_SER_STRUCT_DELTADOS, d))) {
		switch (tag) {
		case DELTADOS_SER_CART:
			cart_deserialise(&d->cart, sh);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	if (ser_error(sh)) {
		part_free((struct part *)d);
		return NULL;
	}
	return (struct part *)d;
}

static uint8_t deltados_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct deltados *d = (struct deltados *)c;
	if (R2) {
		return c->rom_data[A & 0x3fff];
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
		return c->rom_data[A & 0x3fff];
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

	d->fdc->set_dirc = (DELEGATE_T1(void,int)){d->vdrive_interface->set_dirc, d->vdrive_interface};
	d->fdc->set_dden = (DELEGATE_T1(void,bool)){d->vdrive_interface->set_dden, d->vdrive_interface};
	d->fdc->get_head_pos = DELEGATE_AS0(unsigned, d->vdrive_interface->get_head_pos, d->vdrive_interface);
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
		LOG_DEBUG(2, "Delta: Write to latch: ");
		if ((D ^ d->latch_old) & 0x03) {
			LOG_DEBUG(2, "DRIVE SELECT %01u, ", D & 0x03);
		}
		if ((D ^ d->latch_old) & 0x04) {
			LOG_DEBUG(2, "SIDE %s, ", (D & 0x04)?"1":"0");
		}
		if ((D ^ d->latch_old) & 0x08) {
			LOG_DEBUG(2, "DENSITY %s, ", (D & 0x08)?"DOUBLE":"SINGLE");
		}
		LOG_DEBUG(2, "\n");
		d->latch_old = D;
	}
	d->latch_drive_select = D & 0x03;
	d->vdrive_interface->set_drive(d->vdrive_interface, d->latch_drive_select);
	d->latch_side_select = D & 0x04;
	d->vdrive_interface->set_sso(d->vdrive_interface, d->latch_side_select ? 1 : 0);
	d->latch_density = !(D & 0x08);
	wd279x_set_dden(d->fdc, !d->latch_density);
}
