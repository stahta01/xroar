/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
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

static struct cart *dragondos_new(struct cart_config *);

struct cart_module cart_dragondos_module = {
	.name = "dragondos",
	.description = "DragonDOS",
	.new = dragondos_new,
};

struct dragondos {
	struct cart cart;
	unsigned ic1_old;
	unsigned ic1_drive_select;
	_Bool ic1_motor_enable;
	_Bool ic1_precomp_enable;
	_Bool ic1_density;
	_Bool ic1_nmi_enable;
	_Bool have_becker;
	WD279X *fdc;
};

static uint8_t dragondos_read(struct cart *c, uint16_t A, _Bool P2, uint8_t D);
static void dragondos_write(struct cart *c, uint16_t A, _Bool P2, uint8_t D);
static void dragondos_reset(struct cart *c);
static void dragondos_detach(struct cart *c);
static void ff48_write(struct dragondos *d, unsigned flags);

/* Handle signals from WD2797 */
static void set_drq(void *, _Bool);
static void set_intrq(void *, _Bool);

static struct cart *dragondos_new(struct cart_config *cc) {
	struct dragondos *d = xmalloc(sizeof(*d));
	struct cart *c = &d->cart;

	c->config = cc;
	cart_rom_init(c);
	c->read = dragondos_read;
	c->write = dragondos_write;
	c->reset = dragondos_reset;
	c->detach = dragondos_detach;

	d->have_becker = (cc->becker_port && becker_open());
	d->fdc = wd279x_new(WD2797);
	d->fdc->set_dirc = DELEGATE_AS1(void, int, vdrive_set_dirc, NULL);
	d->fdc->set_dden = DELEGATE_AS1(void, bool, vdrive_set_dden, NULL);
	d->fdc->set_sso = DELEGATE_AS1(void, unsigned, vdrive_set_sso, NULL);
	d->fdc->set_drq = DELEGATE_AS1(void, bool, set_drq, c);
	d->fdc->set_intrq = DELEGATE_AS1(void, bool, set_intrq, c);

	vdrive_ready = DELEGATE_AS1(void, bool, wd279x_ready, d->fdc);
	vdrive_tr00 = DELEGATE_AS1(void, bool, wd279x_tr00, d->fdc);
	vdrive_index_pulse = DELEGATE_AS1(void, bool, wd279x_index_pulse, d->fdc);
	vdrive_write_protect = DELEGATE_AS1(void, bool, wd279x_write_protect, d->fdc);
	wd279x_update_connection(d->fdc);
	vdrive_update_connection();

	return c;
}

static void dragondos_reset(struct cart *c) {
	struct dragondos *d = (struct dragondos *)c;
	wd279x_reset(d->fdc);
	d->ic1_old = 0xff;
	ff48_write(d, 0);
	if (d->have_becker)
		becker_reset();
}

static void dragondos_detach(struct cart *c) {
	struct dragondos *d = (struct dragondos *)c;
	vdrive_ready = DELEGATE_DEFAULT1(void, bool);
	vdrive_tr00 = DELEGATE_DEFAULT1(void, bool);
	vdrive_index_pulse = DELEGATE_DEFAULT1(void, bool);
	vdrive_write_protect = DELEGATE_DEFAULT1(void, bool);
	wd279x_free(d->fdc);
	d->fdc = NULL;
	if (d->have_becker)
		becker_close();
	cart_rom_detach(c);
}

static uint8_t dragondos_read(struct cart *c, uint16_t A, _Bool P2, uint8_t D) {
	struct dragondos *d = (struct dragondos *)c;
	if (!P2) {
		return c->rom_data[A & 0x3fff];
	}
	if ((A & 0xc) == 0) {
		return wd279x_read(d->fdc, A);
	}
	if (!(A & 8))
		return D;
	if (d->have_becker) {
		switch (A & 3) {
		case 0x1:
			return becker_read_status();
		case 0x2:
			return becker_read_data();
		default:
			break;
		}
	}
	return D;
}

static void dragondos_write(struct cart *c, uint16_t A, _Bool P2, uint8_t D) {
	struct dragondos *d = (struct dragondos *)c;
	if (!P2)
		return;
	if ((A & 0xc) == 0) {
		wd279x_write(d->fdc, A, D);
		return;
	}
	if (!(A & 8))
		return;
	if (d->have_becker) {
		switch (A & 3) {
		case 0x0:
			ff48_write(d, D);
			break;
		case 0x2:
			becker_write_data(D);
			break;
		default:
			break;
		}
	} else {
		ff48_write(d, D);
	}
}

/* DragonDOS cartridge circuitry */
static void ff48_write(struct dragondos *d, unsigned flags) {
	if (flags != d->ic1_old) {
		LOG_DEBUG(2, "DragonDOS: Write to FF48: ");
		if ((flags ^ d->ic1_old) & 0x03) {
			LOG_DEBUG(2, "DRIVE SELECT %01d, ", flags & 0x03);
		}
		if ((flags ^ d->ic1_old) & 0x04) {
			LOG_DEBUG(2, "MOTOR %s, ", (flags & 0x04)?"ON":"OFF");
		}
		if ((flags ^ d->ic1_old) & 0x08) {
			LOG_DEBUG(2, "DENSITY %s, ", (flags & 0x08)?"SINGLE":"DOUBLE");
		}
		if ((flags ^ d->ic1_old) & 0x10) {
			LOG_DEBUG(2, "PRECOMP %s, ", (flags & 0x10)?"ON":"OFF");
		}
		if ((flags ^ d->ic1_old) & 0x20) {
			LOG_DEBUG(2, "NMI %s, ", (flags & 0x20)?"ENABLED":"DISABLED");
		}
		LOG_DEBUG(2, "\n");
		d->ic1_old = flags;
	}
	d->ic1_drive_select = flags & 0x03;
	vdrive_set_drive(d->ic1_drive_select);
	d->ic1_motor_enable = flags & 0x04;
	d->ic1_density = flags & 0x08;
	wd279x_set_dden(d->fdc, !d->ic1_density);
	d->ic1_precomp_enable = flags & 0x10;
	d->ic1_nmi_enable = flags & 0x20;
}

static void set_drq(void *sptr, _Bool value) {
	struct cart *c = sptr;
	DELEGATE_CALL1(c->signal_firq, value);
}

static void set_intrq(void *sptr, _Bool value) {
	struct cart *c = sptr;
	struct dragondos *d = sptr;
	if (value) {
		if (d->ic1_nmi_enable) {
			DELEGATE_CALL1(c->signal_nmi, 1);
		}
	} else {
		DELEGATE_CALL1(c->signal_nmi, 0);
	}
}
