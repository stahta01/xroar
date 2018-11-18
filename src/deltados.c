/*

Premier Microsystems' Delta disk system

Copyright 2007-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

/* Sources:
 *     Delta cartridge detail:
 *         Partly inferred from disassembly of Delta ROM,
 *         Partly from information provided by Phill Harvey-Smith on
 *         www.dragon-archive.co.uk.
 */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "cart.h"
#include "delegate.h"
#include "logging.h"
#include "vdrive.h"
#include "wd279x.h"
#include "xroar.h"

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
	WD279X *fdc;
	struct vdrive_interface *vdrive_interface;
};

/* Cart interface */

static uint8_t deltados_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t deltados_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void deltados_reset(struct cart *c);
static void deltados_detach(struct cart *c);
static void deltados_free(struct cart *c);
static _Bool deltados_has_interface(struct cart *c, const char *ifname);
static void deltados_attach_interface(struct cart *c, const char *ifname, void *intf);

/* Latch */

static void latch_write(struct deltados *d, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct cart *deltados_new(struct cart_config *cc) {
	struct deltados *d = xmalloc(sizeof(*d));
	struct cart *c = &d->cart;
	*d = (struct deltados){0};

	c->config = cc;
	cart_rom_init(c);

	c->detach = deltados_detach;
	c->free = deltados_free;

	c->read = deltados_read;
	c->write = deltados_write;
	c->reset = deltados_reset;

	c->has_interface = deltados_has_interface;
	c->attach_interface = deltados_attach_interface;

	d->fdc = wd279x_new(WD2791);

	return c;
}

static void deltados_reset(struct cart *c) {
	struct deltados *d = (struct deltados *)c;
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

static void deltados_free(struct cart *c) {
	struct deltados *d = (struct deltados *)c;
	wd279x_free(d->fdc);
	cart_rom_free(c);
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
