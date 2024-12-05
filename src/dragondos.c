/** \file
 *
 *  \brief DragonDOS cartridge.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
 *  DragonDOS cartridge detail from http://www.dragon-archive.co.uk/
 */

/* TODO: I've hacked in an optional "becker port" at $FF49/$FF4A.  Is this the
 * best place for it? */

#include "top-config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"

#include "becker.h"
#include "cart.h"
#include "logging.h"
#include "part.h"
#include "rombank.h"
#include "serialise.h"
#include "vdrive.h"
#include "wd279x.h"

struct dragondos {
	struct cart cart;
	unsigned latch_old;
	unsigned latch_drive_select;
	_Bool latch_motor_enable;
	_Bool latch_precomp_enable;
	_Bool latch_density;
	_Bool latch_nmi_enable;
	struct becker *becker;
	struct WD279X *fdc;
	struct vdrive_interface *vdrive_interface;
};

static const struct ser_struct ser_struct_dragondos[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_ELEM(2, struct dragondos, latch_drive_select),
	SER_ID_STRUCT_ELEM(3, struct dragondos, latch_motor_enable),
	SER_ID_STRUCT_ELEM(4, struct dragondos, latch_precomp_enable),
	SER_ID_STRUCT_ELEM(5, struct dragondos, latch_density),
	SER_ID_STRUCT_ELEM(6, struct dragondos, latch_nmi_enable),
};

static const struct ser_struct_data dragondos_ser_struct_data = {
	.elems = ser_struct_dragondos,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_dragondos),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragondos_config_complete(struct cart_config *);

/* Cart interface */
static uint8_t dragondos_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t dragondos_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void dragondos_reset(struct cart *c, _Bool hard);
static void dragondos_detach(struct cart *c);
static _Bool dragondos_has_interface(struct cart *c, const char *ifname);
static void dragondos_attach_interface(struct cart *c, const char *ifname, void *intf);

/* Handle signals from WD2797 */
static void set_drq(void *sptr, _Bool value);
static void set_intrq(void *sptr, _Bool value);

/* Latch */
static void latch_write(struct dragondos *d, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// DragonDOS part creation

static struct part *dragondos_allocate(void);
static void dragondos_initialise(struct part *p, void *options);
static _Bool dragondos_finish(struct part *p);
static void dragondos_free(struct part *p);

static const struct partdb_entry_funcs dragondos_funcs = {
	.allocate = dragondos_allocate,
	.initialise = dragondos_initialise,
	.finish = dragondos_finish,
	.free = dragondos_free,

	.ser_struct_data = &dragondos_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry dragondos_part = { .partdb_entry = { .name = "dragondos", .description = "Dragon Data | DragonDOS", .funcs = &dragondos_funcs }, .config_complete = dragondos_config_complete };

static struct part *dragondos_allocate(void) {
	struct dragondos *d = part_new(sizeof(*d));
	struct cart *c = &d->cart;
	struct part *p = &c->part;

	*d = (struct dragondos){0};

	cart_rom_init(c);

	c->detach = dragondos_detach;
	c->read = dragondos_read;
	c->write = dragondos_write;
	c->reset = dragondos_reset;
	c->has_interface = dragondos_has_interface;
	c->attach_interface = dragondos_attach_interface;

	return p;
}

static void dragondos_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	cart_rom_initialise(p, options);
	part_add_component(p, part_create("WD2797", "WD2797"), "FDC");
}

static _Bool dragondos_finish(struct part *p) {
	struct dragondos *d = (struct dragondos *)p;
	struct cart *c = &d->cart;

	// Find attached parts
	d->fdc = (struct WD279X *)part_component_by_id_is_a(p, "FDC", "WD2797");

	// Check all required parts are attached
	if (d->fdc == NULL) {
		return 0;
	}

	if (c->config->becker_port) {
		d->becker = becker_open();
	}

	if (!cart_rom_finish(p)) {
		return 0;
	}

	return 1;
}

static void dragondos_free(struct part *p) {
	struct dragondos *d = (struct dragondos *)p;
	becker_close(d->becker);
	cart_rom_free(p);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragondos_config_complete(struct cart_config *cc) {
	// Default ROM
	if (!cc->rom_dfn && !cc->rom) {
		cc->rom = xstrdup("@dragondos_compat");
	}
}

static void dragondos_reset(struct cart *c, _Bool hard) {
	struct dragondos *d = (struct dragondos *)c;
	cart_rom_reset(c, hard);
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

static uint8_t dragondos_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct dragondos *d = (struct dragondos *)c;
	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
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
		rombank_d8(c->ROM, A, &D);
		return D;
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

	d->fdc->set_dirc = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dirc, d->vdrive_interface);
	d->fdc->set_dden = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dden, d->vdrive_interface);
	d->fdc->set_sso = DELEGATE_AS1(void, unsigned, d->vdrive_interface->set_sso, d->vdrive_interface);
	d->fdc->set_drq = DELEGATE_AS1(void, bool, set_drq, d);
	d->fdc->set_intrq = DELEGATE_AS1(void, bool, set_intrq, d);
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
		LOG_MOD_DEBUG(2, "dragondos", "config reg: ");
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
	if (d->vdrive_interface) {
		d->vdrive_interface->set_drive(d->vdrive_interface, d->latch_drive_select);
	}
	d->latch_motor_enable = D & 0x04;
	d->latch_density = D & 0x08;
	wd279x_set_dden(d->fdc, !d->latch_density);
	d->latch_precomp_enable = D & 0x10;
	d->latch_nmi_enable = D & 0x20;
}

static void set_drq(void *sptr, _Bool value) {
	struct dragondos *d = sptr;
	struct cart *c = &d->cart;
	DELEGATE_CALL(c->signal_firq, value);
}

static void set_intrq(void *sptr, _Bool value) {
	struct dragondos *d = sptr;
	struct cart *c = &d->cart;
	if (value) {
		if (d->latch_nmi_enable) {
			DELEGATE_CALL(c->signal_nmi, 1);
		}
	} else {
		DELEGATE_CALL(c->signal_nmi, 0);
	}
}
