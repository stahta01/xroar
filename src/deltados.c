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

static struct cart *deltados_new(struct cart_config *);

struct cart_module cart_deltados_module = {
	.name = "delta",
	.description = "Delta System",
	.new = deltados_new,
};

struct deltados {
	struct cart cart;
	unsigned ic1_old;
	unsigned ic1_drive_select;
	_Bool ic1_side_select;
	_Bool ic1_density;
	WD279X *fdc;
};

static uint8_t deltados_read(struct cart *c, uint16_t A, _Bool P2, uint8_t D);
static void deltados_write(struct cart *c, uint16_t A, _Bool P2, uint8_t D);
static void deltados_reset(struct cart *c);
static void deltados_detach(struct cart *c);
static void ff44_write(struct deltados *d, unsigned flags);

static struct cart *deltados_new(struct cart_config *cc) {
	struct deltados *d = xmalloc(sizeof(*d));
	struct cart *c = &d->cart;

	c->config = cc;
	cart_rom_init(c);
	c->read = deltados_read;
	c->write = deltados_write;
	c->reset = deltados_reset;
	c->detach = deltados_detach;

	d->fdc = wd279x_new(WD2791);
	d->fdc->set_dirc = (DELEGATE_T1(void,int)){vdrive_set_dirc, NULL};
	d->fdc->set_dden = (DELEGATE_T1(void,bool)){vdrive_set_dden, NULL};

	vdrive_ready = DELEGATE_AS1(void, bool, wd279x_ready, d->fdc);
	vdrive_tr00 = DELEGATE_AS1(void, bool, wd279x_tr00, d->fdc);
	vdrive_index_pulse = DELEGATE_AS1(void, bool, wd279x_index_pulse, d->fdc);
	vdrive_write_protect = DELEGATE_AS1(void, bool, wd279x_write_protect, d->fdc);
	wd279x_update_connection(d->fdc);
	vdrive_update_connection();

	return c;
}

static void deltados_reset(struct cart *c) {
	struct deltados *d = (struct deltados *)c;
	wd279x_reset(d->fdc);
	d->ic1_old = -1;
	d->ic1_drive_select = -1;
	ff44_write(d, 0);
}

static void deltados_detach(struct cart *c) {
	struct deltados *d = (struct deltados *)c;
	vdrive_ready = DELEGATE_DEFAULT1(void, bool);
	vdrive_tr00 = DELEGATE_DEFAULT1(void, bool);
	vdrive_index_pulse = DELEGATE_DEFAULT1(void, bool);
	vdrive_write_protect = DELEGATE_DEFAULT1(void, bool);
	wd279x_free(d->fdc);
	d->fdc = NULL;
	cart_rom_detach(c);
}

static uint8_t deltados_read(struct cart *c, uint16_t A, _Bool P2, uint8_t D) {
	struct deltados *d = (struct deltados *)c;
	if (!P2) {
		return c->rom_data[A & 0x3fff];
	}
	if ((A & 4) == 0)
		return wd279x_read(d->fdc, A);
	return D;
}

static void deltados_write(struct cart *c, uint16_t A, _Bool P2, uint8_t D) {
	struct deltados *d = (struct deltados *)c;
	if (!P2)
		return;
	if ((A & 4) == 0) wd279x_write(d->fdc, A, D);
	if (A & 4) ff44_write(d, D);
}

/* Delta cartridge circuitry */
static void ff44_write(struct deltados *d, unsigned flags) {
	if (flags != d->ic1_old) {
		LOG_DEBUG(2, "Delta: Write to FF44: ");
		if ((flags ^ d->ic1_old) & 0x03) {
			LOG_DEBUG(2, "DRIVE SELECT %01d, ", flags & 0x03);
		}
		if ((flags ^ d->ic1_old) & 0x04) {
			LOG_DEBUG(2, "SIDE %s, ", (flags & 0x04)?"1":"0");
		}
		if ((flags ^ d->ic1_old) & 0x08) {
			LOG_DEBUG(2, "DENSITY %s, ", (flags & 0x08)?"DOUBLE":"SINGLE");
		}
		LOG_DEBUG(2, "\n");
		d->ic1_old = flags;
	}
	d->ic1_drive_select = flags & 0x03;
	vdrive_set_drive(d->ic1_drive_select);
	d->ic1_side_select = flags & 0x04;
	vdrive_set_sso(NULL, d->ic1_side_select ? 1 : 0);
	d->ic1_density = !(flags & 0x08);
	wd279x_set_dden(d->fdc, !d->ic1_density);
}
