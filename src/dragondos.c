/*

DragonDOS cartridge

Copyright 2003-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

/* Sources:
 *     DragonDOS cartridge detail:
 *         http://www.dragon-archive.co.uk/
 */

/* TODO: I've hacked in an optional "becker port" at $FF49/$FF4A.  Is this the
 * best place for it? */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "becker.h"
#include "cart.h"
#include "delegate.h"
#include "logging.h"
#include "vdrive.h"
#include "wd279x.h"
#include "xroar.h"

static struct cart *dragondos_new(struct cart_config *cc);

struct cart_module cart_dragondos_module = {
	.name = "dragondos",
	.description = "DragonDOS",
	.new = dragondos_new,
};

struct dragondos {
	struct cart cart;
	unsigned latch_old;
	unsigned latch_drive_select;
	_Bool latch_motor_enable;
	_Bool latch_precomp_enable;
	_Bool latch_density;
	_Bool latch_nmi_enable;
	struct becker *becker;
	WD279X *fdc;
	struct vdrive_interface *vdrive_interface;
};

/* Cart interface */
static uint8_t dragondos_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t dragondos_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void dragondos_reset(struct cart *c);
static void dragondos_detach(struct cart *c);
static void dragondos_free(struct cart *c);
static _Bool dragondos_has_interface(struct cart *c, const char *ifname);
static void dragondos_attach_interface(struct cart *c, const char *ifname, void *intf);

/* Handle signals from WD2797 */
static void set_drq(void *sptr, _Bool value);
static void set_intrq(void *sptr, _Bool value);

/* Latch */
static void latch_write(struct dragondos *d, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct cart *dragondos_new(struct cart_config *cc) {
	struct dragondos *d = xmalloc(sizeof(*d));
	*d = (struct dragondos){0};
	struct cart *c = &d->cart;

	c->config = cc;
	cart_rom_init(c);

	c->detach = dragondos_detach;
	c->free = dragondos_free;

	c->read = dragondos_read;
	c->write = dragondos_write;
	c->reset = dragondos_reset;

	c->has_interface = dragondos_has_interface;
	c->attach_interface = dragondos_attach_interface;

	if (cc->becker_port) {
		d->becker = becker_new();
	}
	d->fdc = wd279x_new(WD2797);

	return c;
}

static void dragondos_reset(struct cart *c) {
	struct dragondos *d = (struct dragondos *)c;
	wd279x_reset(d->fdc);
	d->latch_old = -1;
	latch_write(d, 0);
	if (d->becker)
		becker_reset(d->becker);
}

static void dragondos_detach(struct cart *c) {
	struct dragondos *d = (struct dragondos *)c;
	vdrive_disconnect(d->vdrive_interface);
	wd279x_disconnect(d->fdc);
	if (d->becker)
		becker_reset(d->becker);
	cart_rom_detach(c);
}

static void dragondos_free(struct cart *c) {
	struct dragondos *d = (struct dragondos *)c;
	if (d->becker)
		becker_free(d->becker);
	wd279x_free(d->fdc);
	cart_rom_free(c);
}

static uint8_t dragondos_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct dragondos *d = (struct dragondos *)c;
	if (R2) {
		return c->rom_data[A & 0x3fff];
	}
	if (!P2) {
		return D;
	}
	if ((A & 0xc) == 0) {
		return wd279x_read(d->fdc, A);
	}
	if (!(A & 8))
		return D;
	if (d->becker) {
		switch (A & 3) {
		case 0x1:
			return becker_read_status(d->becker);
		case 0x2:
			return becker_read_data(d->becker);
		default:
			break;
		}
	}
	return D;
}

static uint8_t dragondos_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct dragondos *d = (struct dragondos *)c;
	(void)R2;
	if (R2) {
		return c->rom_data[A & 0x3fff];
	}
	if (!P2) {
		return D;
	}
	if ((A & 0xc) == 0) {
		wd279x_write(d->fdc, A, D);
		return D;
	}
	if (!(A & 8))
		return D;
	if (d->becker) {
		switch (A & 3) {
		case 0x0:
			latch_write(d, D);
			break;
		case 0x2:
			becker_write_data(d->becker, D);
			break;
		default:
			break;
		}
	} else {
		latch_write(d, D);
	}
	return D;
}

static _Bool dragondos_has_interface(struct cart *c, const char *ifname) {
	return c && (0 == strcmp(ifname, "floppy"));
}

static void dragondos_attach_interface(struct cart *c, const char *ifname, void *intf) {
	if (!c || (0 != strcmp(ifname, "floppy")))
		return;
	struct dragondos *d = (struct dragondos *)c;
	d->vdrive_interface = intf;

	d->fdc->set_dirc = DELEGATE_AS1(void, int, d->vdrive_interface->set_dirc, d->vdrive_interface);
	d->fdc->set_dden = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dden, d->vdrive_interface);
	d->fdc->set_sso = DELEGATE_AS1(void, unsigned, d->vdrive_interface->set_sso, d->vdrive_interface);
	d->fdc->set_drq = DELEGATE_AS1(void, bool, set_drq, d);
	d->fdc->set_intrq = DELEGATE_AS1(void, bool, set_intrq, d);
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

	// tied high
	wd279x_ready(d->fdc, 1);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void latch_write(struct dragondos *d, unsigned D) {
	if (D != d->latch_old) {
		LOG_DEBUG(2, "DragonDOS: Write to latch: ");
		if ((D ^ d->latch_old) & 0x03) {
			LOG_DEBUG(2, "DRIVE SELECT %01u, ", D & 0x03);
		}
		if ((D ^ d->latch_old) & 0x04) {
			LOG_DEBUG(2, "MOTOR %s, ", (D & 0x04)?"ON":"OFF");
		}
		if ((D ^ d->latch_old) & 0x08) {
			LOG_DEBUG(2, "DENSITY %s, ", (D & 0x08)?"SINGLE":"DOUBLE");
		}
		if ((D ^ d->latch_old) & 0x10) {
			LOG_DEBUG(2, "PRECOMP %s, ", (D & 0x10)?"ON":"OFF");
		}
		if ((D ^ d->latch_old) & 0x20) {
			LOG_DEBUG(2, "NMI %s, ", (D & 0x20)?"ENABLED":"DISABLED");
		}
		LOG_DEBUG(2, "\n");
		d->latch_old = D;
	}
	d->latch_drive_select = D & 0x03;
	d->vdrive_interface->set_drive(d->vdrive_interface, d->latch_drive_select);
	d->latch_motor_enable = D & 0x04;
	d->latch_density = D & 0x08;
	wd279x_set_dden(d->fdc, !d->latch_density);
	d->latch_precomp_enable = D & 0x10;
	d->latch_nmi_enable = D & 0x20;
}

static void set_drq(void *sptr, _Bool value) {
	struct dragondos *d = sptr;
	struct cart *c = &d->cart;
	DELEGATE_CALL1(c->signal_firq, value);
}

static void set_intrq(void *sptr, _Bool value) {
	struct dragondos *d = sptr;
	struct cart *c = &d->cart;
	if (value) {
		if (d->latch_nmi_enable) {
			DELEGATE_CALL1(c->signal_nmi, 1);
		}
	} else {
		DELEGATE_CALL1(c->signal_nmi, 0);
	}
}
