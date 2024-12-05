/** \file
 *
 *  \brief Tandy CoCo disk controller ("RS-DOS").
 *
 *  \copyright Copyright 2005-2024 Ciaran Anscomb
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
 *  CoCo DOS cartridge detail:
 *
 *  - http://www.coco3.com/unravalled/disk-basic-unravelled.pdf
 */

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

struct rsdos {
	struct cart cart;
	unsigned latch_old;
	unsigned latch_drive_select;
	_Bool latch_density;
	_Bool drq_flag;
	_Bool intrq_flag;
	_Bool halt_enable;
	struct becker *becker;
	struct WD279X *fdc;
	struct vdrive_interface *vdrive_interface;
};

static const struct ser_struct ser_struct_rsdos[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_ELEM(2, struct rsdos, latch_drive_select),
	SER_ID_STRUCT_ELEM(3, struct rsdos, latch_density),
	SER_ID_STRUCT_ELEM(4, struct rsdos, drq_flag),
	SER_ID_STRUCT_ELEM(5, struct rsdos, intrq_flag),
	SER_ID_STRUCT_ELEM(6, struct rsdos, halt_enable),
};

static const struct ser_struct_data rsdos_ser_struct_data = {
	.elems = ser_struct_rsdos,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_rsdos),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void rsdos_config_complete(struct cart_config *);

/* Cart interface */

static uint8_t rsdos_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t rsdos_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void rsdos_reset(struct cart *c, _Bool hard);
static void rsdos_detach(struct cart *c);
static _Bool rsdos_has_interface(struct cart *c, const char *ifname);
static void rsdos_attach_interface(struct cart *c, const char *ifname, void *intf);

/* Handle signals from WD2793 */

static void set_drq(void *sptr, _Bool value);
static void set_intrq(void *sptr, _Bool value);

/* Latch */

static void latch_write(struct rsdos *d, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// RSDOS part creation

static struct part *rsdos_allocate(void);
static void rsdos_initialise(struct part *p, void *options);
static _Bool rsdos_finish(struct part *p);
static void rsdos_free(struct part *p);

static const struct partdb_entry_funcs rsdos_funcs = {
	.allocate = rsdos_allocate,
	.initialise = rsdos_initialise,
	.finish = rsdos_finish,
	.free = rsdos_free,

	.ser_struct_data = &rsdos_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry rsdos_part = { .partdb_entry = { .name = "rsdos", .description = "Tandy | RS-DOS", .funcs = &rsdos_funcs }, .config_complete = rsdos_config_complete };

static struct part *rsdos_allocate(void) {
	struct rsdos *d = part_new(sizeof(*d));
	struct cart *c = &d->cart;
	struct part *p = &c->part;

	*d = (struct rsdos){0};

	cart_rom_init(c);

	c->detach = rsdos_detach;
	c->read = rsdos_read;
	c->write = rsdos_write;
	c->reset = rsdos_reset;
	c->has_interface = rsdos_has_interface;
	c->attach_interface = rsdos_attach_interface;

	return p;
}

static void rsdos_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	cart_rom_initialise(p, options);
	part_add_component(p, part_create("WD2793", "WD2793"), "FDC");
}

static _Bool rsdos_finish(struct part *p) {
	struct rsdos *d = (struct rsdos *)p;
	struct cart *c = &d->cart;

	// Find attached parts
	d->fdc = (struct WD279X *)part_component_by_id_is_a(p, "FDC", "WD2793");

	// Check all required parts are attached
	if (d->fdc == NULL) {
		return 0;
	}

	if (!cart_rom_finish(p)) {
		return 0;
	}

	if (c->config->becker_port) {
		d->becker = becker_open();
	}

	return 1;
}

static void rsdos_free(struct part *p) {
	struct rsdos *d = (struct rsdos *)p;
	becker_close(d->becker);
	cart_rom_free(p);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void rsdos_config_complete(struct cart_config *cc) {
	// Default ROM
	if (!cc->rom_dfn && !cc->rom) {
		cc->rom = xstrdup("@rsdos");
	}
}

static void rsdos_reset(struct cart *c, _Bool hard) {
	struct rsdos *d = (struct rsdos *)c;
	cart_rom_reset(c, hard);
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

static uint8_t rsdos_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct rsdos *d = (struct rsdos *)c;
	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
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
		rombank_d8(c->ROM, A, &D);
		return D;
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

	d->fdc->set_dirc = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dirc, d->vdrive_interface);
	d->fdc->set_dden = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dden, d->vdrive_interface);
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
	if (D != d->latch_old) {
		LOG_MOD_DEBUG(2, "rsdos", "config reg: ");
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
			LOG_DEBUG(2, "SIDE %u, ", (D & 0x40) >> 6);
		}
		if ((D ^ d->latch_old) & 0x80) {
			LOG_DEBUG(2, "HALT %s, ", (D & 0x80)?"ENABLED":"DISABLED");
		}
		LOG_DEBUG(2, "\n");
		d->latch_old = D;
	}
	d->latch_drive_select = new_drive_select;
	if (d->vdrive_interface) {
		d->vdrive_interface->set_sso(d->vdrive_interface, (D & 0x40) ? 1 : 0);
		d->vdrive_interface->set_drive(d->vdrive_interface, d->latch_drive_select);
	}
	d->latch_density = D & 0x20;
	wd279x_set_dden(d->fdc, !d->latch_density);
	if (!d->latch_density && d->intrq_flag) {
		DELEGATE_CALL(c->signal_nmi, 1);
	}
	d->halt_enable = (D & 0x80) && !d->intrq_flag;
	DELEGATE_CALL(c->signal_halt, d->halt_enable && !d->drq_flag);
}

static void set_drq(void *sptr, _Bool value) {
	struct rsdos *d = sptr;
	struct cart *c = &d->cart;
	d->drq_flag = value;
	DELEGATE_CALL(c->signal_halt, d->halt_enable && !d->drq_flag);
}

static void set_intrq(void *sptr, _Bool value) {
	struct rsdos *d = sptr;
	struct cart *c = &d->cart;
	d->intrq_flag = value;
	if (value) {
		d->halt_enable = 0;
		DELEGATE_CALL(c->signal_halt, 0);
		if (!d->latch_density && d->intrq_flag) {
			DELEGATE_CALL(c->signal_nmi, 1);
		}
	} else {
		DELEGATE_CALL(c->signal_nmi, 0);
	}
}
