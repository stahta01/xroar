/** \file
 *
 *  \brief Tandy Colour Computer 1/2 machines.
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
#include "dragon/coco.h"
#include "dragon/dragon.h"
#include "keyboard.h"
#include "mc6809/mc6809.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
#include "mc6883.h"
#include "printer.h"
#include "ram.h"
#include "rombank.h"
#include "romlist.h"
#include "sound.h"
#include "tape.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco_config_complete(struct machine_config *mc);

static void coco_reset(struct machine *, _Bool hard);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// CoCo 1/2 part creation

static struct part *coco_allocate(void);
static void coco_initialise(struct part *p, void *options);
static _Bool coco_finish(struct part *p);
static void coco_free(struct part *p);

static const struct partdb_entry_funcs coco_funcs = {
	.allocate = coco_allocate,
	.initialise = coco_initialise,
	.finish = coco_finish,
	.free = coco_free,

	.ser_struct_data = &dragon_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_entry coco_part = { .partdb_entry = { .name = "coco", .description = "Tandy | Colour Computer", .funcs = &coco_funcs }, .config_complete = coco_config_complete, .is_working_config = dragon_is_working_config, .cart_arch = "dragon-cart" };

static void pia0b_data_preread_coco64k(void *sptr);
static void pia1a_data_postwrite_coco(void *sptr);
static void pia1b_data_preread_coco64k(void *sptr);
static void coco_print_byte(void *);

static struct machine_bp coco_print_breakpoint[] = {
	BP_COCO_ROM(.address = 0xa2c1, .handler = DELEGATE_INIT(coco_print_byte, NULL) ),
};

static struct part *coco_allocate(void) {
	struct dragon *md = part_new(sizeof(*md));
	struct machine *m = &md->public;
	struct part *p = &m->part;

	*md = (struct dragon){0};
	dragon_allocate_common(md);

	m->reset = coco_reset;

	return p;
}

static void coco_initialise(struct part *p, void *options) {
	assert(p != NULL);
	assert(options != NULL);
	struct dragon *md = (struct dragon *)p;
	struct machine_config *mc = options;

	coco_config_complete(mc);

	if (mc->ram_org == RAM_ORG_16Kx4 && md->option.sam_variant == ANY_AUTO) {
		md->option.sam_variant = DRAGON_SAM_74LS785;
	}

	md->is_dragon = 0;
	dragon_initialise_common(md, mc);
}

void coco_pia_configuration(struct dragon *md) {
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;

	// Override PIA configuration
	if (RAM_ORG_A(mc->ram_org) == 12) {
		// 4K CoCo ties PIA1 PB2 low
		md->PIA1->b.in_sink &= ~(1<<2);
	} else if (RAM_ORG_A(mc->ram_org) == 14) {
		// 16K CoCo pulls PIA1 PB2 high
		md->PIA1->b.in_source |= (1<<2);
	} else {
		// 64K CoCo connects PIA0 PB6 to PIA1 PB2:
		// Deal with this through a postwrite.
		md->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread_coco64k, md);
		md->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread_coco64k, md);
	}
	md->PIA1->a.data_postwrite = DELEGATE_AS0(void, pia1a_data_postwrite_coco, md);
}

static _Bool coco_finish(struct part *p) {
	struct dragon *md = (struct dragon *)p;
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;

	md->is_dragon = 0;
	if (!dragon_finish_common(md))
		return 0;

	// CoCo ROMs are always considered to be in two parts: Colour BASIC and
	// Extended Colour BASIC.

	// ROM
	md->ROM0 = rombank_new(8, 8192, 2);

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
	md->crc_bas = (mc->ram > 4) ? 0xd8f4d15e : 0x00b50aaa;  // CB 1.3/1.0
	const char *crclist = (mc->ram > 4) ? "@coco" : "@bas10";
	md->has_bas = rombank_verify_crc(md->ROM0, "Colour BASIC", 1, crclist, xroar.cfg.force_crc_match, &md->crc_bas);

	md->crc_extbas = 0xa82a6254;  // ECB 1.1
	md->has_extbas = rombank_verify_crc(md->ROM0, "Extended Colour BASIC", 0, "@cocoext", xroar.cfg.force_crc_match, &md->crc_extbas);

	coco_pia_configuration(md);

	// PAL overrides
	if (mc->tv_standard == TV_PAL) {
		vdg_pal_init(&md->vdg_pal, md->VDG);
		md->vdg_pal.pal_stop_0 = 16;
		md->vdg_pal.pal_delay_0 = 24;
		md->vdg_pal.pal_stop_1 = 256;
		md->vdg_pal.pal_delay_1 = 26;
		md->hs_invert = 1;
		DELEGATE_SAFE_CALL(md->vo->set_active_area, VDG_tWHS + VDG_tBP + VDG_tLB, VDG_ACTIVE_AREA_START + 24, 512, 192);
		ui_update_state(-1, ui_tag_cmp_fs, VO_RENDER_FS_14_23753, NULL);
	}

	return 1;
}

static void coco_free(struct part *p) {
	struct dragon *md = (struct dragon *)p;
	dragon_free_common(p);
	machine_bp_remove_list(&md->public, coco_print_breakpoint);
	rombank_free(md->ROM0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco_config_complete(struct machine_config *mc) {
	// Default ROMs
	dragon_set_default_rom(mc->bas_dfn, &mc->bas_rom, "@coco");
	dragon_set_default_rom(mc->extbas_dfn, &mc->extbas_rom, "@coco_ext");

	// Validate requested total RAM
	mc->ram = int_floor_list(mc->ram, 64, 0, 4, 8, 16, 32, 64, 512, 2048, -1);

	// Keyboard map
	if (mc->keymap == ANY_AUTO) {
		mc->keymap = dkbd_layout_coco;
	}

	dragon_config_complete(mc);
}

static void coco_reset(struct machine *m, _Bool hard) {
        struct dragon *md = (struct dragon *)m;
        dragon_reset(m, hard);
	machine_bp_remove_list(m, coco_print_breakpoint);
	machine_bp_add_list(m, coco_print_breakpoint, md);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void pia0b_data_preread_coco64k(void *sptr) {
	struct dragon *md = sptr;
	dragon_keyboard_update(md);
	// PIA0 PB6 is linked to PIA1 PB2 on 64K CoCos
	if ((md->PIA1->b.out_source & md->PIA1->b.out_sink) & (1<<2)) {
		md->PIA0->b.in_source |= (1<<6);
		md->PIA0->b.in_sink |= (1<<6);
	} else {
		md->PIA0->b.in_source &= ~(1<<6);
		md->PIA0->b.in_sink &= ~(1<<6);
	}
}

static void pia1a_data_postwrite_coco(void *sptr) {
	struct dragon *md = sptr;
	sound_set_dac_level(md->snd, (float)(PIA_VALUE_A(md->PIA1) & 0xfc) / 252.);
	tape_update_output(md->tape_interface, md->PIA1->a.out_sink & 0xfc);
}

static void pia1b_data_preread_coco64k(void *sptr) {
	struct dragon *md = sptr;
	// PIA0 PB6 is linked to PIA1 PB2 on 64K CoCos
	if ((md->PIA0->b.out_source & md->PIA0->b.out_sink) & (1<<6)) {
		md->PIA1->b.in_source |= (1<<2);
		md->PIA1->b.in_sink |= (1<<2);
	} else {
		md->PIA1->b.in_source &= ~(1<<2);
		md->PIA1->b.in_sink &= ~(1<<2);
	}
}

// CoCo serial printing ROM hook.

static void coco_print_byte(void *sptr) {
	struct dragon *md = sptr;
	if (!md->printer_interface) {
		return;
	}
	// Not ROM?
	if (md->SAM->decode(md->SAM, 1, md->CPU->reg_pc) != 2) {
		return;
	}
	int byte = MC6809_REG_A(md->CPU);
	printer_strobe(md->printer_interface, 0, byte);
	printer_strobe(md->printer_interface, 1, byte);
	md->CPU->reg_pc = 0xa2df;
}
