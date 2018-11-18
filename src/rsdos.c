/*

RS-DOS Tandy CoCo disk controller

Copyright 2005-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

/* Sources:
 *     CoCo DOS cartridge detail:
 *         http://www.coco3.com/unravalled/disk-basic-unravelled.pdf
 */

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

static struct cart *rsdos_new(struct cart_config *cc);

struct cart_module cart_rsdos_module = {
	.name = "rsdos",
	.description = "RS-DOS",
	.new = rsdos_new,
};

struct rsdos {
	struct cart cart;
	unsigned latch_old;
	unsigned latch_drive_select;
	_Bool latch_density;
	_Bool drq_flag;
	_Bool intrq_flag;
	_Bool halt_enable;
	struct becker *becker;
	WD279X *fdc;
	struct vdrive_interface *vdrive_interface;
};

/* Cart interface */

static uint8_t rsdos_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t rsdos_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void rsdos_reset(struct cart *c);
static void rsdos_detach(struct cart *c);
static void rsdos_free(struct cart *c);
static _Bool rsdos_has_interface(struct cart *c, const char *ifname);
static void rsdos_attach_interface(struct cart *c, const char *ifname, void *intf);

/* Handle signals from WD2793 */

static void set_drq(void *sptr, _Bool value);
static void set_intrq(void *sptr, _Bool value);

/* Latch */

static void latch_write(struct rsdos *d, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct cart *rsdos_new(struct cart_config *cc) {
	struct rsdos *d = xmalloc(sizeof(*d));
	*d = (struct rsdos){0};
	struct cart *c = &d->cart;

	c->config = cc;
	cart_rom_init(c);

	c->detach = rsdos_detach;
	c->free = rsdos_free;

	c->read = rsdos_read;
	c->write = rsdos_write;
	c->reset = rsdos_reset;

	c->has_interface = rsdos_has_interface;
	c->attach_interface = rsdos_attach_interface;

	if (cc->becker_port) {
		d->becker = becker_new();
	}
	d->fdc = wd279x_new(WD2793);

	return c;
}

static void rsdos_reset(struct cart *c) {
	struct rsdos *d = (struct rsdos *)c;
	wd279x_reset(d->fdc);
	d->latch_old = -1;
	d->latch_drive_select = -1;
	d->drq_flag = d->intrq_flag = 0;
	latch_write(d, 0);
	if (d->becker)
		becker_reset(d->becker);
}

static void rsdos_detach(struct cart *c) {
	struct rsdos *d = (struct rsdos *)c;
	vdrive_disconnect(d->vdrive_interface);
	wd279x_disconnect(d->fdc);
	if (d->becker)
		becker_reset(d->becker);
	cart_rom_detach(c);
}

static void rsdos_free(struct cart *c) {
	struct rsdos *d = (struct rsdos *)c;
	wd279x_free(d->fdc);
	if (d->becker)
		becker_free(d->becker);
	cart_rom_free(c);
}

static uint8_t rsdos_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct rsdos *d = (struct rsdos *)c;
	if (R2) {
		return c->rom_data[A & 0x3fff];
	}
	if (!P2) {
		return D;
	}
	if (A & 0x8) {
		return wd279x_read(d->fdc, A);
	}
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

static uint8_t rsdos_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct rsdos *d = (struct rsdos *)c;
	if (R2) {
		return c->rom_data[A & 0x3fff];
	}
	if (!P2) {
		return D;
	}
	if (A & 0x8) {
		wd279x_write(d->fdc, A, D);
		return D;
	}
	if (d->becker) {
		/* XXX not exactly sure in what way anyone has tightened up the
		 * address decoding for the becker port */
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
		if (!(A & 8))
			latch_write(d, D);
	}
	return D;
}

static _Bool rsdos_has_interface(struct cart *c, const char *ifname) {
	return c && (0 == strcmp(ifname, "floppy"));
}

static void rsdos_attach_interface(struct cart *c, const char *ifname, void *intf) {
	if (!c || (0 != strcmp(ifname, "floppy")))
		return;
	struct rsdos *d = (struct rsdos *)c;
	d->vdrive_interface = intf;

	d->fdc->set_dirc = DELEGATE_AS1(void, int, d->vdrive_interface->set_dirc, d->vdrive_interface);
	d->fdc->set_dden = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dden, d->vdrive_interface);
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

static void latch_write(struct rsdos *d, unsigned D) {
	struct cart *c = (struct cart *)d;
	unsigned new_drive_select = 0;
	D ^= 0x20;
	if (D & 0x01) {
		new_drive_select = 0;
	} else if (D & 0x02) {
		new_drive_select = 1;
	} else if (D & 0x04) {
		new_drive_select = 2;
	} else if (D & 0x40) {
		new_drive_select = 3;
		D &= ~0x40;  // prevent interpreting as side select
	}
	d->vdrive_interface->set_sso(d->vdrive_interface, (D & 0x40) ? 1 : 0);
	if (D != d->latch_old) {
		LOG_DEBUG(2, "RSDOS: Write to latch: ");
		if (new_drive_select != d->latch_drive_select) {
			LOG_DEBUG(2, "DRIVE SELECT %u, ", new_drive_select);
		}
		if ((D ^ d->latch_old) & 0x08) {
			LOG_DEBUG(2, "MOTOR %s, ", (D & 0x08)?"ON":"OFF");
		}
		if ((D ^ d->latch_old) & 0x20) {
			LOG_DEBUG(2, "DENSITY %s, ", (D & 0x20)?"SINGLE":"DOUBLE");
		}
		if ((D ^ d->latch_old) & 0x10) {
			LOG_DEBUG(2, "PRECOMP %s, ", (D & 0x10)?"ON":"OFF");
		}
		if ((D ^ d->latch_old) & 0x40) {
			LOG_DEBUG(2, "SIDE %d, ", (D & 0x40) >> 6);
		}
		if ((D ^ d->latch_old) & 0x80) {
			LOG_DEBUG(2, "HALT %s, ", (D & 0x80)?"ENABLED":"DISABLED");
		}
		LOG_DEBUG(2, "\n");
		d->latch_old = D;
	}
	d->latch_drive_select = new_drive_select;
	d->vdrive_interface->set_drive(d->vdrive_interface, d->latch_drive_select);
	d->latch_density = D & 0x20;
	wd279x_set_dden(d->fdc, !d->latch_density);
	if (d->latch_density && d->intrq_flag) {
		DELEGATE_CALL1(c->signal_nmi, 1);
	}
	d->halt_enable = D & 0x80;
	if (d->intrq_flag) d->halt_enable = 0;
	DELEGATE_CALL1(c->signal_halt, d->halt_enable && !d->drq_flag);
}

static void set_drq(void *sptr, _Bool value) {
	struct rsdos *d = sptr;
	struct cart *c = &d->cart;
	d->drq_flag = value;
	if (value) {
		DELEGATE_CALL1(c->signal_halt, 0);
	} else {
		if (d->halt_enable) {
			DELEGATE_CALL1(c->signal_halt, 1);
		}
	}
}

static void set_intrq(void *sptr, _Bool value) {
	struct rsdos *d = sptr;
	struct cart *c = &d->cart;
	d->intrq_flag = value;
	if (value) {
		d->halt_enable = 0;
		DELEGATE_CALL1(c->signal_halt, 0);
		if (!d->latch_density && d->intrq_flag) {
			DELEGATE_CALL1(c->signal_nmi, 1);
		}
	} else {
		DELEGATE_CALL1(c->signal_nmi, 0);
	}
}
