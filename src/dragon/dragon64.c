/** \file
 *
 *  \brief Dragon 64 support.
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
 *
 *  This file is included into dragon.c and provides the code specific to the
 *  Dragon 64.
 *
 *  This machine is basically the same as the Dragon 32, but includes 64K RAM
 *  by default, an extra BASIC ROM and an ACIA for serial comms.
 *
 *  The ACIA is not emulated beyond some status registers to fool the ROM code
 *  into thinking it is present.
 */

#include "top-config.h"

#include <assert.h>
#include <stdlib.h>

#include "dragon/dragon.h"
#include "keyboard.h"
#include "mc6809/mc6809.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
#include "mc6883.h"
#include "mos6551.h"
#include "ram.h"
#include "rombank.h"
#include "romlist.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct dragon64 {
	struct dragon dragon;

	struct rombank *ROM1;

	// Points to either dragon.ROM0 (32K BASIC) or ROM1 (64K BASIC)
	struct rombank *rom;

	struct MOS6551 *ACIA;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon64_config_complete(struct machine_config *);

static void dragon64_reset(struct machine *, _Bool hard);

static _Bool dragon64_read_byte(struct dragon *, unsigned A);
static _Bool dragon64_write_byte(struct dragon *, unsigned A);

static void dragon64_pia1b_data_postwrite(void *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct part *dragon64_allocate(void);
static void dragon64_initialise(struct part *, void *options);
static _Bool dragon64_finish(struct part *);
static void dragon64_free(struct part *);

static const struct partdb_entry_funcs dragon64_funcs = {
	.allocate = dragon64_allocate,
	.initialise = dragon64_initialise,
	.finish = dragon64_finish,
	.free = dragon64_free,

	// Dragon 64 needs to be kept in Dragon common data for compatibility
	// with old snapshots.  That's fine: there's no extra state not covered
	// by sub-parts.
	.ser_struct_data = &dragon_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_entry dragon64_part = { .partdb_entry = { .name = "dragon64", .description = "Dragon Data | Dragon 64", .funcs = &dragon64_funcs }, .config_complete = dragon64_config_complete, .is_working_config = dragon_is_working_config, .cart_arch = "dragon-cart" };

static struct part *dragon64_allocate(void) {
	struct dragon64 *mdp = part_new(sizeof(*mdp));
	struct dragon *md = &mdp->dragon;
	struct machine *m = &md->public;
	struct part *p = &m->part;

	*mdp = (struct dragon64){0};

	dragon_allocate_common(md);

	m->reset = dragon64_reset;

	md->read_byte = dragon64_read_byte;
	md->write_byte = dragon64_write_byte;

	return p;
}

static void dragon64_initialise(struct part *p, void *options) {
	assert(p != NULL);
	assert(options != NULL);
	struct dragon64 *mdp = (struct dragon64 *)p;
	struct dragon *md = &mdp->dragon;
	struct machine_config *mc = options;

	dragon64_config_complete(mc);

	md->is_dragon = 1;
	dragon_initialise_common(md, mc);

	// ACIA
	part_add_component(p, part_create("MOS6551", NULL), "ACIA");
}

static _Bool dragon64_finish(struct part *p) {
	assert(p != NULL);
	struct dragon64 *mdp = (struct dragon64 *)p;
	struct dragon *md = &mdp->dragon;
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;
	assert(mc != NULL);

	// Find attached parts
	mdp->ACIA = (struct MOS6551 *)part_component_by_id_is_a(p, "ACIA", "MOS6551");

	// Check all required parts are attached
	if (!mdp->ACIA) {
		return 0;
	}

	md->is_dragon = 1;
	if (!dragon_finish_common(md))
		return 0;

	// ROMs
	mdp->dragon.ROM0 = rombank_new(8, 16384, 1);
	mdp->ROM1 = rombank_new(8, 16384, 1);

	// 32K mode Extended BASIC
	if (mc->extbas_rom) {
		sds tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			rombank_load_image(mdp->dragon.ROM0, 0, tmp, 0);
			sdsfree(tmp);
		}
	}

	// 64K mode Extended BASIC
	if (mc->altbas_rom) {
		sds tmp = romlist_find(mc->altbas_rom);
		if (tmp) {
			rombank_load_image(mdp->ROM1, 0, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Report and check CRC (32K BASIC)
	rombank_report(mdp->dragon.ROM0, "dragon64", "32K BASIC");
	md->crc_combined = 0x84f68bf9;  // Dragon 64 32K mode BASIC
	md->has_combined = rombank_verify_crc(mdp->dragon.ROM0, "32K BASIC", -1, "@d64_1", xroar.cfg.force_crc_match, &md->crc_combined);

	// Report and check CRC (64K BASIC)
	rombank_report(mdp->ROM1, "dragon64", "64K BASIC");
	md->crc_altbas = 0x17893a42;  // Dragon 64 64K mode BASIC
	md->has_altbas = rombank_verify_crc(mdp->ROM1, "64K BASIC", -1, "@d64_2", xroar.cfg.force_crc_match, &md->crc_altbas);

	// Override PIA1 PB2 as ROMSEL
	md->PIA1->b.in_source |= (1<<2);  // pull-up
	md->PIA1->b.data_postwrite = DELEGATE_AS0(void, dragon64_pia1b_data_postwrite, mdp);

	// ROM selection from PIA
	mdp->rom = (PIA_VALUE_B(md->PIA1) & 0x04) ? mdp->dragon.ROM0 : mdp->ROM1;

	// PAL overrides
	if (mc->tv_standard == TV_PAL) {
		vdg_pal_init(&md->vdg_pal, md->VDG);
		md->vdg_pal.pal_stop_0 = 262;
		md->vdg_pal.pal_delay_0 = 25;
		md->vdg_pal.pal_stop_1 = 254;
		md->vdg_pal.pal_delay_1 = 25;
		md->vdg_pal.pal_hs_inhibit = 1;
		DELEGATE_SAFE_CALL(md->vo->set_active_area, VDG_tWHS + VDG_tBP + VDG_tLB, VDG_ACTIVE_AREA_START + 25, 512, 192);
		ui_update_state(-1, ui_tag_cmp_fs, VO_RENDER_FS_14_218, NULL);
	}

	return 1;
}

static void dragon64_free(struct part *p) {
	struct dragon64 *mdp = (struct dragon64 *)p;
	dragon_free_common(p);
	rombank_free(mdp->ROM1);
	rombank_free(mdp->dragon.ROM0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon64_config_complete(struct machine_config *mc) {
	// Default ROMs
	dragon_set_default_rom(mc->extbas_dfn, &mc->extbas_rom, "@dragon64");
	dragon_set_default_rom(mc->altbas_dfn, &mc->altbas_rom, "@dragon64_alt");

	// Validate requested total RAM
	mc->ram = int_floor_list(mc->ram, 64, 0, 16, 32, 64, 512, 2048, -1);

	dragon_config_complete(mc);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon64_reset(struct machine *m, _Bool hard) {
	struct dragon64 *mdp = (struct dragon64 *)m;
	dragon_reset(m, hard);
	mos6551_reset(mdp->ACIA);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool dragon64_read_byte(struct dragon *md, unsigned A) {
	struct dragon64 *mdp = (struct dragon64 *)md;

	switch (md->SAM->S) {
	case 1:
	case 2:
		rombank_d8(mdp->rom, A, &md->CPU->D);
		return 1;

	case 4:
		if ((A & 4) != 0) {
			mos6551_access(mdp->ACIA, 1, A, &md->CPU->D);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

static _Bool dragon64_write_byte(struct dragon *md, unsigned A) {
	struct dragon64 *mdp = (struct dragon64 *)md;

	if (md->SAM->S & 4) switch (md->SAM->S) {
	case 1:
	case 2:
		rombank_d8(mdp->rom, A, &md->CPU->D);
		return 1;

	case 4:
		if ((A & 4) != 0) {
			mos6551_access(mdp->ACIA, 0, A, &md->CPU->D);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon64_pia1b_data_postwrite(void *sptr) {
	struct dragon64 *mdp = sptr;
	struct dragon *md = &mdp->dragon;

	_Bool is_32k = PIA_VALUE_B(md->PIA1) & 0x04;
	if (is_32k) {
		mdp->rom = mdp->dragon.ROM0;
		keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_dragon_32k_basic);
	} else {
		mdp->rom = mdp->ROM1;
		keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_dragon_64k_basic);
	}
	dragon_pia1b_data_postwrite(sptr);
}
