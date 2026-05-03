/** \file
 *
 *  \brief Dragon 32 and Tandy Colour Computer 1/2 machines.
 *
 *  \copyright Copyright 2003-2026 Ciaran Anscomb
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
 */

#include "top-config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "sds.h"

#include "dkbd.h"
#include "dragon/dragon.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
#include "ram.h"
#include "rombank.h"
#include "romlist.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon32_config_complete(struct machine_config *mc) {
	// Default ROMs
	dragon_set_default_rom(mc->extbas_dfn, &mc->extbas_rom, "@dragon32");

	// RAM
	mc->ram = int_floor_list(mc->ram, 32, 0, 4, 8, 16, 32, 64, 512, 2048, -1);

	dragon_config_complete(mc);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Dragon 32 part creation

static struct part *dragon32_allocate(void);
static void dragon32_initialise(struct part *p, void *options);
static _Bool dragon32_finish(struct part *p);
static void dragon32_free(struct part *p);

static const struct partdb_entry_funcs dragon32_funcs = {
	.allocate = dragon32_allocate,
	.initialise = dragon32_initialise,
	.finish = dragon32_finish,
	.free = dragon32_free,

	.ser_struct_data = &dragon_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_entry dragon32_part = { .partdb_entry = { .name = "dragon32", .description = "Dragon Data | Dragon 32", .funcs = &dragon32_funcs }, .config_complete = dragon32_config_complete, .is_working_config = dragon_is_working_config, .cart_arch = "dragon-cart" };

static struct part *dragon32_allocate(void) {
	struct dragon *md = part_new(sizeof(*md));
	struct machine *m = &md->public;
	struct part *p = &m->part;

	*md = (struct dragon){0};
	dragon_allocate_common(md);

	return p;
}

static void dragon32_initialise(struct part *p, void *options) {
	assert(p != NULL);
	assert(options != NULL);
	struct dragon *md = (struct dragon *)p;
	struct machine_config *mc = options;

	dragon32_config_complete(mc);

	md->is_dragon = 1;
	dragon_initialise_common(md, mc);
}

static _Bool dragon32_finish(struct part *p) {
	struct dragon *md = (struct dragon *)p;
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;

	md->is_dragon = 1;
	if (!dragon_finish_common(md))
		return 0;

	// Dragon ROMs are always Extended BASIC only, and even though (some?)
	// Dragon 32s split this across two pieces of hardware, it doesn't make
	// sense to consider the two regions separately.

	// ROM
	md->ROM0 = rombank_new(8, 16384, 1);

	// Extended Colour BASIC
	if (md->ROM0 && mc->extbas_rom) {
		sds tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			rombank_load_image(md->ROM0, 0, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Colour BASIC
	if (md->ROM0 && md->ROM0->nslots > 1 && mc->bas_rom) {
		sds tmp = romlist_find(mc->bas_rom);
		if (tmp) {
			rombank_load_image(md->ROM0, 1, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Report BASIC
	rombank_report(md->ROM0, p->partdb->name, "BASIC");

	// Check CRCs
	md->crc_combined = 0xe3879310;  // Dragon 32 BASIC
	md->has_combined = rombank_verify_crc(md->ROM0, "BASIC", -1, "@d32", xroar.cfg.force_crc_match, &md->crc_combined);

	// Machine-specific PIA connections
	switch (mc->ram_org) {
	case RAM_ORG_4Kx1:
	case RAM_ORG_16Kx1:
		md->PIA1->b.in_source |= (1<<2);
		break;
	default:
		md->PIA1->b.in_sink &= ~(1<<2);
	}

	// PAL overrides
	if (mc->tv_standard == TV_PAL) {
		vdg_pal_init(&md->vdg_pal, md->VDG);
		md->vdg_pal.pal_stop_0 = 262;
		md->vdg_pal.pal_delay_0 = 25;
		md->vdg_pal.pal_stop_1 = 254;
		md->vdg_pal.pal_delay_1 = 25;
		DELEGATE_SAFE_CALL(md->vo->set_active_area, VDG_tWHS + VDG_tBP + VDG_tLB, VDG_ACTIVE_AREA_START + 25, 512, 192);
		ui_update_state(-1, ui_tag_cmp_fs, VO_RENDER_FS_14_218, NULL);
	}

	return 1;
}

static void dragon32_free(struct part *p) {
	struct dragon *md = (struct dragon *)p;
	dragon_free_common(p);
	rombank_free(md->ROM0);
}
