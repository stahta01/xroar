/** \file
 *
 *  \brief Games Master Cartridge support.
 *
 *  \copyright Copyright 2018-2021 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cart.h"
#include "events.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"
#include "sn76489.h"
#include "sound.h"

#define GMC_SER_CART (1)

static struct cart *gmc_new(struct cart_config *);

struct cart_module cart_gmc_module = {
	.name = "gmc",
	.description = "Games Master Cartridge",
	.new = gmc_new,
};

struct gmc {
	struct cart cart;
	struct SN76489 *csg;
	struct sound_interface *snd;
};

static void gmc_attach(struct cart *c);
static void gmc_detach(struct cart *c);
static void gmc_free(struct part *p);
static void gmc_serialise(struct part *p, struct ser_handle *sh);
static uint8_t gmc_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void gmc_reset(struct cart *c);
static _Bool gmc_has_interface(struct cart *c, const char *ifname);
static void gmc_attach_interface(struct cart *c, const char *ifname, void *intf);

static _Bool gmc_finish(struct part *p) {
	struct gmc *gmc = (struct gmc *)p;

	// Find attached parts
	gmc->csg = (struct SN76489 *)part_component_by_id_is_a(p, "CSG", "SN76489");

	// Check all required parts are attached
	if (gmc->csg == NULL) {
		return 0;
	}

	return 1;
}

static struct gmc *gmc_create(void) {
	struct gmc *gmc = part_new(sizeof(*gmc));
	struct cart *c = &gmc->cart;
	*gmc = (struct gmc){0};
	part_init(&c->part, "GMC");
	c->part.free = gmc_free;
	c->part.serialise = gmc_serialise;
	c->part.finish = gmc_finish;
	c->part.is_a = cart_is_a;

	cart_rom_init(c);

	c->attach = gmc_attach;
	c->detach = gmc_detach;
	c->write = gmc_write;
	c->reset = gmc_reset;
	c->has_interface = gmc_has_interface;
	c->attach_interface = gmc_attach_interface;

	return gmc;
}

static struct cart *gmc_new(struct cart_config *cc) {
	assert(cc != NULL);

	struct gmc *gmc = gmc_create();
	struct cart *c = &gmc->cart;
	struct part *p = &c->part;
	c->config = cc;

	part_add_component(&c->part, (struct part *)sn76489_new(), "CSG");

	if (!gmc_finish(p)) {
		part_free(p);
		return NULL;
	}

	return c;
}

static void gmc_serialise(struct part *p, struct ser_handle *sh) {
	struct gmc *gmc = (struct gmc *)p;
	(void)gmc;
	cart_serialise(&gmc->cart, sh, GMC_SER_CART);
	ser_write_close_tag(sh);
}

struct part *gmc_deserialise(struct ser_handle *sh) {
	struct gmc *gmc = gmc_create();
	int tag;
	while (!ser_error(sh) && (tag = ser_read_tag(sh)) > 0) {
		switch (tag) {
		case GMC_SER_CART:
			cart_deserialise(&gmc->cart, sh);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	if (ser_error(sh)) {
		part_free((struct part *)gmc);
		return NULL;
	}
	return (struct part *)gmc;
}

static void gmc_reset(struct cart *c) {
	cart_rom_reset(c);
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

static void gmc_free(struct part *p) {
	cart_rom_free(p);
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

static uint8_t gmc_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct gmc *gmc = (struct gmc *)c;
	(void)R2;

	if (R2) {
		return c->rom_data[A & 0x3fff];
	}

	if (!P2) {
		return D;
	}

	if ((A & 1) == 0) {
		// bank switch
		cart_rom_select_bank(c, (D & 3) << 14);
		return D;
	}

	// 76489 sound register
	sound_update(gmc->snd);
	if (gmc->csg) {
		sn76489_write(gmc->csg, event_current_tick, D);
	}
	return D;
}
