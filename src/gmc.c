/*

Games Master Cartridge support

Copyright 2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

John Linville's Games Master Cartridge.  Provides bank-switched ROM and
SN76489 sound chip.

*/

/* Sources:
 *     Games Master Cartridge:
 *         https://drive.google.com/drive/folders/1FWSpWshl_GJevk85hsm54b62SGGojyB1
 */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "cart.h"
#include "events.h"
#include "logging.h"
#include "sn76489.h"
#include "sound.h"
#include "xroar.h"

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
static void gmc_free(struct cart *c);
static uint8_t gmc_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void gmc_reset(struct cart *c);
static _Bool gmc_has_interface(struct cart *c, const char *ifname);
static void gmc_attach_interface(struct cart *c, const char *ifname, void *intf);

static struct cart *gmc_new(struct cart_config *cc) {
	struct gmc *gmc = xmalloc(sizeof(*gmc));
	struct cart *c = &gmc->cart;

	c->config = cc;
	cart_rom_init(c);

	c->attach = gmc_attach;
	c->detach = gmc_detach;
	c->free = gmc_free;

	c->write = gmc_write;
	c->reset = gmc_reset;

	c->has_interface = gmc_has_interface;
	c->attach_interface = gmc_attach_interface;

	return c;
}

static void gmc_reset(struct cart *c) {
	cart_rom_reset(c);
}

static void gmc_attach(struct cart *c) {
	cart_rom_attach(c);
	gmc_reset(c);
}

static void gmc_detach(struct cart *c) {
	struct gmc *gmc = (struct gmc *)c;
	gmc->snd->get_cart_audio.func = NULL;
	cart_rom_detach(c);
}

static void gmc_free(struct cart *c) {
	struct gmc *gmc = (struct gmc *)c;
	if (gmc->csg) {
		sn76489_free(gmc->csg);
	}
	cart_rom_free(c);
}

static _Bool gmc_has_interface(struct cart *c, const char *ifname) {
	return c && (0 == strcmp(ifname, "sound"));
}

static void gmc_attach_interface(struct cart *c, const char *ifname, void *intf) {
	if (!c || (0 != strcmp(ifname, "sound")))
		return;
	struct gmc *gmc = (struct gmc *)c;
	gmc->snd = intf;
	gmc->csg = sn76489_new(4000000, gmc->snd->framerate, EVENT_TICK_RATE, event_current_tick);
	if (gmc->csg) {
		gmc->snd->get_cart_audio = DELEGATE_AS3(float, uint32, int, floatp, sn76489_get_audio, gmc->csg);
	}
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
