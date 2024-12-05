/** \file
 *
 *  \brief Games Master Cartridge support.
 *
 *  \copyright Copyright 2018-2024 Ciaran Anscomb
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
 *  John Linville's Games Master Cartridge.  Provides bank-switched ROM and
 *  SN76489 sound chip.
 *
 *  \par Sources
 *
 *  Games Master Cartridge:
 *
 *  - https://drive.google.com/drive/folders/1FWSpWshl_GJevk85hsm54b62SGGojyB1
 */

#include "top-config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "cart.h"
#include "events.h"
#include "logging.h"
#include "part.h"
#include "rombank.h"
#include "serialise.h"
#include "sn76489.h"
#include "sound.h"

struct gmc {
	struct cart cart;
	struct SN76489 *csg;
	struct sound_interface *snd;
	uint32_t rom_bank;
	uint32_t rom_bank_mask;
};

static const struct ser_struct ser_struct_gmc[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_ELEM(2, struct gmc, rom_bank),
};

static const struct ser_struct_data gmc_ser_struct_data = {
	.elems = ser_struct_gmc,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_gmc),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void gmc_attach(struct cart *c);
static void gmc_detach(struct cart *c);
static uint8_t gmc_read(struct cart *, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t gmc_write(struct cart *, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void gmc_reset(struct cart *c, _Bool hard);
static _Bool gmc_has_interface(struct cart *c, const char *ifname);
static void gmc_attach_interface(struct cart *c, const char *ifname, void *intf);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// GMC part creation

static struct part *gmc_allocate(void);
static void gmc_initialise(struct part *p, void *options);
static _Bool gmc_finish(struct part *p);
static void gmc_free(struct part *p);

static const struct partdb_entry_funcs gmc_funcs = {
	.allocate = gmc_allocate,
	.initialise = gmc_initialise,
	.finish = gmc_finish,
	.free = gmc_free,

	.ser_struct_data = &gmc_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry gmc_part = { .partdb_entry = { .name = "gmc", .description = "John Linville | Games Master Cartridge", .funcs = &gmc_funcs } };

static struct part *gmc_allocate(void) {
	struct gmc *gmc = part_new(sizeof(*gmc));
	struct cart *c = &gmc->cart;
	struct part *p = &c->part;

	*gmc = (struct gmc){0};

	cart_rom_init(c);

	c->attach = gmc_attach;
	c->detach = gmc_detach;
	c->read = gmc_read;
	c->write = gmc_write;
	c->reset = gmc_reset;
	c->has_interface = gmc_has_interface;
	c->attach_interface = gmc_attach_interface;

	return p;
}

static void gmc_initialise(struct part *p, void *options) {
	cart_rom_initialise(p, options);
	part_add_component(p, part_create("SN76489", NULL), "CSG");
}

static _Bool gmc_finish(struct part *p) {
	struct gmc *gmc = (struct gmc *)p;
	struct cart *c = &gmc->cart;

	// Find attached parts
	gmc->csg = (struct SN76489 *)part_component_by_id_is_a(p, "CSG", "SN76489");

	// Check all required parts are attached
	if (gmc->csg == NULL) {
		return 0;
	}

	if (!cart_rom_finish(p)) {
		return 0;
	}

	gmc->rom_bank_mask = (c->ROM->slot_size - 1) & 0x3c000;

	return 1;
}

static void gmc_free(struct part *p) {
	cart_rom_free(p);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void gmc_reset(struct cart *c, _Bool hard) {
	struct gmc *gmc = (struct gmc *)c;
	cart_rom_reset(c, hard);
	gmc->rom_bank = 0;
}

static void gmc_attach(struct cart *c) {
	cart_rom_attach(c);
}

static void gmc_detach(struct cart *c) {
	struct gmc *gmc = (struct gmc *)c;
	if (gmc->snd)
		gmc->snd->get_cart_audio.func = NULL;
	cart_rom_detach(c);
}

static _Bool gmc_has_interface(struct cart *c, const char *ifname) {
	return c && (0 == strcmp(ifname, "sound"));
}

static void gmc_attach_interface(struct cart *c, const char *ifname, void *intf) {
	if (!c || (0 != strcmp(ifname, "sound")))
		return;
	struct gmc *gmc = (struct gmc *)c;
	gmc->snd = intf;
	sn76489_configure(gmc->csg, 4000000, gmc->snd->framerate, EVENT_TICK_RATE, event_current_tick);
	gmc->snd->get_cart_audio = DELEGATE_AS3(float, uint32, int, floatp, sn76489_get_audio, gmc->csg);
}

static uint8_t gmc_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct gmc *gmc = (struct gmc *)c;
	(void)P2;
	if (R2) {
		rombank_d8(c->ROM, gmc->rom_bank | (A & 0x3fff), &D);
	}
	return D;
}

static uint8_t gmc_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct gmc *gmc = (struct gmc *)c;
	(void)R2;

	if (R2) {
		rombank_d8(c->ROM, gmc->rom_bank | (A & 0x3fff), &D);
		return D;
	}

	if (!P2) {
		return D;
	}

	if ((A & 1) == 0) {
		// bank switch
		gmc->rom_bank = (D << 14) & gmc->rom_bank_mask;
		return D;
	}

	// 76489 sound register
	sound_update(gmc->snd);
	if (gmc->csg) {
		sn76489_write(gmc->csg, event_current_tick, D);
	}
	return D;
}
