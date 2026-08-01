/** \file
 *
 *  \brief CoCoSDC cartridge.
 *
 *  \copyright Copyright 2026 Ciaran Anscomb
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

#include "top-config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"
#include "sds.h"

#include "cart.h"
#include "logging.h"
#include "part.h"
//#include "rom.h"
#include "rombank.h"
#include "romlist.h"
#include "serialise.h"

struct cocosdc {
	struct cart cart;
	unsigned latch_old;
	unsigned latch_drive_select;
	bool latch_motor_enable;
	bool latch_precomp_enable;
	bool latch_density;
	bool latch_nmi_enable;
};

static const struct ser_struct ser_struct_cocosdc[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_ELEM(2, struct cocosdc, latch_drive_select),
	SER_ID_STRUCT_ELEM(3, struct cocosdc, latch_motor_enable),
	SER_ID_STRUCT_ELEM(4, struct cocosdc, latch_precomp_enable),
	SER_ID_STRUCT_ELEM(5, struct cocosdc, latch_density),
	SER_ID_STRUCT_ELEM(6, struct cocosdc, latch_nmi_enable),
};

static const struct ser_struct_data cocosdc_ser_struct_data = {
	.elems = ser_struct_cocosdc,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_cocosdc),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void cocosdc_config_complete(struct cart_config *);

/* Cart interface */
static uint8_t cocosdc_read(struct cart *c, uint16_t A, bool P2, bool R2, uint8_t D);
static uint8_t cocosdc_write(struct cart *c, uint16_t A, bool P2, bool R2, uint8_t D);
static void cocosdc_reset(struct cart *c, bool hard);
static void cocosdc_detach(struct cart *c);

/* Latch */
static void latch_write(struct cocosdc *n, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// CoCoSDC part creation

static struct part *cocosdc_allocate(void);
static void cocosdc_initialise(struct part *p, void *options);
static bool cocosdc_finish(struct part *p);
static void cocosdc_free(struct part *p);

static const struct partdb_entry_funcs cocosdc_funcs = {
	.allocate = cocosdc_allocate,
	.initialise = cocosdc_initialise,
	.finish = cocosdc_finish,
	.free = cocosdc_free,

	.ser_struct_data = &cocosdc_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry cocosdc_part = { .partdb_entry = { .name = "cocosdc", .description = "Darren Atkinson | CoCoSDC", .funcs = &cocosdc_funcs }, .config_complete = cocosdc_config_complete };

static struct part *cocosdc_allocate(void) {
	struct cocosdc *n = part_new(sizeof(*n));
	struct cart *c = &n->cart;
	struct part *p = &c->part;

	*n = (struct cocosdc){0};

	cart_rom_init(c);

	c->detach = cocosdc_detach;
	c->read = cocosdc_read;
	c->write = cocosdc_write;
	c->reset = cocosdc_reset;

	return p;
}

static void cocosdc_initialise(struct part *p, void *options) {
	struct cart *c = (struct cart *)p;
	struct cart_config *cc = options;
	assert(cc != NULL);

	c->config = cc;
}

static bool cocosdc_finish(struct part *p) {
	struct cocosdc *n = (struct cocosdc *)p;
	struct cart *c = &n->cart;

	// 8 * 16K ROM image
	c->ROM = rombank_new(8, 16384, 8);

	{
		sds tmp = romlist_find("sdcdos");
		if (tmp) {
			rombank_load_image(c->ROM, 0, tmp, 0);
		}
		sdsfree(tmp);

		tmp = romlist_find("@rsdos");
		if (tmp) {
			rombank_load_image(c->ROM, 1, tmp, 0);
		}
		sdsfree(tmp);

		tmp = romlist_find("dplus-sdc");
		if (tmp) {
			rombank_load_image(c->ROM, 4, tmp, 0);
		}
		sdsfree(tmp);

		tmp = romlist_find("@dragondos_compat");
		if (tmp) {
			rombank_load_image(c->ROM, 5, tmp, 0);
		}
		sdsfree(tmp);
	}

	rombank_report(c->ROM, "cocosdc", "ROM");

	return 1;
}

static void cocosdc_free(struct part *p) {
	struct cocosdc *n = (struct cocosdc *)p;
	struct cart *c = &n->cart;
	rombank_free(c->ROM);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void cocosdc_config_complete(struct cart_config *cc) {
	(void)cc;
}

static void cocosdc_reset(struct cart *c, bool hard) {
	struct cocosdc *n = (struct cocosdc *)c;
	cart_rom_reset(c, hard);
	n->latch_old = -1;
	latch_write(n, 0);
}

static void cocosdc_detach(struct cart *c) {
	/* struct cocosdc *n = (struct cocosdc *)c; */
	cart_rom_detach(c);
}

static uint8_t cocosdc_read(struct cart *c, uint16_t A, bool P2, bool R2, uint8_t D) {
	/* struct cocosdc *n = (struct cocosdc *)c; */
	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
	}
	if (!P2) {
		return D;
	}
	if ((A & 0xc) == 0) {
		// XXX FDC read
	}
	if (!(A & 8))
		return D;
	return D;
}

static uint8_t cocosdc_write(struct cart *c, uint16_t A, bool P2, bool R2, uint8_t D) {
	struct cocosdc *n = (struct cocosdc *)c;
	(void)R2;
	if (R2) {
		rombank_d8(c->ROM, A, &D);
		return D;
	}
	if (!P2) {
		return D;
	}
	if ((A & 0xc) == 0) {
		// XXX FDC write
		return D;
	}
	if (!(A & 8))
		return D;
	latch_write(n, D);
	return D;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void latch_write(struct cocosdc *n, unsigned D) {
	if (D != n->latch_old) {
		LOG_MOD_DEBUG(2, "cocosdc", "config reg: ");
		if ((D ^ n->latch_old) & 0x03) {
			LOG_DEBUG(2, "DRIVE SELECT %01u, ", D & 0x03);
		}
		if ((D ^ n->latch_old) & 0x04) {
			LOG_DEBUG(2, "MOTOR %s, ", (D & 0x04)?"ON":"OFF");
		}
		if ((D ^ n->latch_old) & 0x08) {
			LOG_DEBUG(2, "DENSITY %s, ", (D & 0x08)?"SINGLE":"DOUBLE");
		}
		if ((D ^ n->latch_old) & 0x10) {
			LOG_DEBUG(2, "PRECOMP %s, ", (D & 0x10)?"ON":"OFF");
		}
		if ((D ^ n->latch_old) & 0x20) {
			LOG_DEBUG(2, "NMI %s, ", (D & 0x20)?"ENABLED":"DISABLED");
		}
		LOG_DEBUG(2, "\n");
		n->latch_old = D;
	}
	n->latch_drive_select = D & 0x03;
	n->latch_motor_enable = D & 0x04;
	n->latch_density = D & 0x08;
	n->latch_precomp_enable = D & 0x10;
	n->latch_nmi_enable = D & 0x20;
}

/*
static void set_drq(void *sptr, bool value) {
	struct cocosdc *n = sptr;
	struct cart *c = &n->cart;
	DELEGATE_CALL(c->signal_firq, value);
}

static void set_intrq(void *sptr, bool value) {
	struct cocosdc *n = sptr;
	struct cart *c = &n->cart;
	if (value) {
		if (n->latch_nmi_enable) {
			DELEGATE_CALL(c->signal_nmi, 1);
		}
	} else {
		DELEGATE_CALL(c->signal_nmi, 0);
	}
}
*/
