/** \file
 *
 *  \brief Tandy Deluxe Color Computer support.
 *
 *  \copyright Copyright 2024-2026 Ciaran Anscomb
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
 *  Deluxe Color Computer.
 *
 *  PROBABLY SOMEWHAT INCOMPLETE.
 *
 *  This is very much a work in progress based on the information coming out of
 *  Brian Wieseler's Deluxe CoCo prototype.
 *
 *  A GAL is added featuring an option register mapped to $FF30 and interfacing
 *  to the PSG.  Option register bits are documented as:
 *
 *  B7          ROM select (0=cartridge, 1=internal)
 *  B6          60Hz IRQ enable
 *  B5..4       N/A
 *  B3          Burst phase shift
 *  B2          Paging enable
 *  B1..0       Page select (which 16K is mapped to $4000-$7FFF)
 *
 *  An AY-3-8913 (no I/O port) PSG is added, interfaced through the GAL at the
 *  following addresses:
 *
 *  $FF38       Write data to PSG
 *  $FF39       Read data from PSG or write address to PSG
 *
 *  A 6551 ACIA is added, mapped to the following addresses:
 *
 *  $FF3C       TX/RX register
 *  $FF3D       Status register
 *  $FF3E       Command register
 *  $FF3F       Control register
 */

#include "top-config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "ay891x.h"
#include "dkbd.h"
#include "dragon/coco.h"
#include "dragon/dragon.h"
#include "events.h"
#include "mc6809/mc6809.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
#include "mc6883.h"
#include "mos6551.h"
#include "ram.h"
#include "rombank.h"
#include "romlist.h"
#include "serialise.h"
#include "sound.h"
#include "vo.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct deluxecoco {
	struct dragon dragon;

	struct MOS6551 *ACIA;
	struct AY891X *PSG;

	// Deluxe CoCo GAL
	unsigned page;
	_Bool page_enable;
	_Bool burst;
	_Bool irq_60hz_enable;
	_Bool irq_60hz;
	_Bool cart_inhibit;
};

static const struct ser_struct ser_struct_deluxecoco[] = {
        SER_ID_STRUCT_NEST(1, &dragon_ser_struct_data),
	SER_ID_STRUCT_ELEM(2, struct deluxecoco, page),
	SER_ID_STRUCT_ELEM(3, struct deluxecoco, page_enable),
	SER_ID_STRUCT_ELEM(4, struct deluxecoco, burst),
	SER_ID_STRUCT_ELEM(5, struct deluxecoco, irq_60hz_enable),
	SER_ID_STRUCT_ELEM(6, struct deluxecoco, irq_60hz),
	SER_ID_STRUCT_ELEM(7, struct deluxecoco, cart_inhibit),
};

static const struct ser_struct_data deluxecoco_ser_struct_data = {
	.elems = ser_struct_deluxecoco,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_deluxecoco),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void deluxecoco_config_complete(struct machine_config *);

static _Bool deluxecoco_has_interface(struct part *, const char *ifname);
static void deluxecoco_attach_interface(struct part *, const char *ifname, void *intf);

static void deluxecoco_reset(struct machine *, _Bool hard);

static _Bool deluxecoco_read_byte(struct dragon *, unsigned A);
static _Bool deluxecoco_write_byte(struct dragon *, unsigned A);
static void deluxecoco_cpu_cycle(void *, _Bool RnW, uint16_t A);

static void deluxecoco_vdg_hs(void *, _Bool level);
static void deluxecoco_vdg_fs(void *, _Bool level);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct part *deluxecoco_allocate(void);
static void deluxecoco_initialise(struct part *, void *options);
static _Bool deluxecoco_finish(struct part *);
static void deluxecoco_free(struct part *);

static const struct partdb_entry_funcs deluxecoco_funcs = {
	.allocate = deluxecoco_allocate,
	.initialise = deluxecoco_initialise,
	.finish = deluxecoco_finish,
	.free = deluxecoco_free,

	.ser_struct_data = &deluxecoco_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_entry deluxecoco_part = { .partdb_entry = { .name = "deluxecoco", .description = "Tandy | Deluxe Colour Computer", .funcs = &deluxecoco_funcs }, .config_complete = deluxecoco_config_complete, .is_working_config = dragon_is_working_config, .cart_arch = "dragon-cart" };

static struct part *deluxecoco_allocate(void) {
	struct deluxecoco *mdp = part_new(sizeof(*mdp));
	struct dragon *md = &mdp->dragon;
	struct machine *m = &md->public;
	struct part *p = &m->part;

	*mdp = (struct deluxecoco){0};

	dragon_allocate_common(md);

	m->has_interface = deluxecoco_has_interface;
	m->attach_interface = deluxecoco_attach_interface;

	m->reset = deluxecoco_reset;

	md->read_byte = deluxecoco_read_byte;
	md->write_byte = deluxecoco_write_byte;

	return p;
}

static void deluxecoco_initialise(struct part *p, void *options) {
	assert(p != NULL);
	assert(options != NULL);
	struct deluxecoco *mdp = (struct deluxecoco *)p;
	struct dragon *md = &mdp->dragon;
	struct machine_config *mc = options;

	deluxecoco_config_complete(mc);

	md->is_dragon = 0;
	dragon_initialise_common(md, mc);

	// ACIA
	part_add_component(p, part_create("MOS6551", NULL), "ACIA");

	// PSG
	part_add_component(p, part_create("AY891X", NULL), "PSG");

	// FDC
	part_add_component(p, part_create("WD2797", "WD2797"), "FDC");
}

static _Bool deluxecoco_finish(struct part *p) {
	assert(p != NULL);
	struct deluxecoco *mdp = (struct deluxecoco *)p;
	struct dragon *md = &mdp->dragon;
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;
	assert(mc != NULL);

	// Find attached parts
	mdp->ACIA = (struct MOS6551 *)part_component_by_id_is_a(p, "ACIA", "MOS6551");
	mdp->PSG = (struct AY891X *)part_component_by_id_is_a(p, "PSG", "AY891X");

	// Check all required parts are attached
	if (!mdp->ACIA || !mdp->PSG) {
		return 0;
	}

	md->is_dragon = 0;
	if (!dragon_finish_common(md))
		return 0;

	// ROM
	mdp->dragon.ROM0 = rombank_new(8, 8192, 4);

	// Advanced Colour BASIC
	if (mc->extbas_rom) {
		sds tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			rombank_load_image(mdp->dragon.ROM0, 0, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Bodge loading the ROM in four parts.  XXX need support for sets of
	// ROMs.
	if (!mdp->dragon.ROM0->d[1]) {
		sds tmp = romlist_find("@deluxecoco1");
		if (tmp) {
			rombank_load_image(mdp->dragon.ROM0, 1, tmp, 0);
			sdsfree(tmp);
		}
	}
	if (!mdp->dragon.ROM0->d[2]) {
		sds tmp = romlist_find("@deluxecoco2");
		if (tmp) {
			rombank_load_image(mdp->dragon.ROM0, 2, tmp, 0);
			sdsfree(tmp);
		}
	}
	if (!mdp->dragon.ROM0->d[3]) {
		sds tmp = romlist_find("@deluxecoco3");
		if (tmp) {
			rombank_load_image(mdp->dragon.ROM0, 3, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Report and check CRC (Advanced Colour BASIC)
	rombank_report(mdp->dragon.ROM0, "deluxecoco", "Advanced Colour BASIC");
	md->crc_combined = 0x1cce231e;  // ACB 00.00.07
	md->has_combined = rombank_verify_crc(mdp->dragon.ROM0, "Advanced Colour BASIC", -1, "@deluxecoco", xroar.cfg.force_crc_match, &md->crc_combined);

	coco_pia_configuration(md);

	md->CPU->mem_cycle = DELEGATE_AS2(void, bool, uint16, deluxecoco_cpu_cycle, mdp);

	md->VDG->signal_hs = DELEGATE_AS1(void, bool, deluxecoco_vdg_hs, mdp);
	md->VDG->signal_fs = DELEGATE_AS1(void, bool, deluxecoco_vdg_fs, mdp);

	// Deluxe ROM depends on relaxed PIA0 decode
	md->relaxed_pia0_decode = 1;
	// But $FF20-$FF3F is shared with other devices
	md->relaxed_pia1_decode = 0;

	// PAL overrides
	// Note: there probably never was a PAL Deluxe CoCo prototype, so
	// settings are borrowed from the CoCo 1/2 approach.
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

static void deluxecoco_free(struct part *p) {
	struct deluxecoco *mdp = (struct deluxecoco *)p;
	struct dragon *md = &mdp->dragon;
	md->snd->get_non_muxed_audio.func = NULL;
        dragon_free_common(p);
	rombank_free(mdp->dragon.ROM0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void deluxecoco_config_complete(struct machine_config *mc) {
	// Default ROMs
	dragon_set_default_rom(mc->extbas_dfn, &mc->extbas_rom, "@deluxecoco");

	// Validate requested total RAM
	mc->ram = int_floor_list(mc->ram, 64, 0, 16, 32, 64, 512, 2048, -1);

	// Keyboard map
	if (mc->keymap == ANY_AUTO) {
		mc->keymap = dkbd_layout_coco3;
	}

	dragon_config_complete(mc);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Called by dragon_has_interface()

static _Bool deluxecoco_has_interface(struct part *p, const char *ifname) {
	if (0 == strcmp(ifname, "sound"))
		return 1;
	return dragon_has_interface(p, ifname);
}

// Called by dragon_attach_interface()

static void deluxecoco_attach_interface(struct part *p, const char *ifname, void *intf) {
	if (!p)
		return;

	struct deluxecoco *mdp = (struct deluxecoco *)p;

	if (0 == strcmp(ifname, "sound")) {
		struct sound_interface *snd = intf;
		ay891x_configure(mdp->PSG, EVENT_TICK_RATE >> 3, snd->framerate, EVENT_TICK_RATE, event_current_tick);
		snd->get_non_muxed_audio = DELEGATE_AS3(float, uint32, int, floatp, ay891x_get_audio, mdp->PSG);
		return;
	}

	dragon_attach_interface(p, ifname, intf);
	return;
}

static void deluxecoco_reset(struct machine *m, _Bool hard) {
        struct deluxecoco *mdp = (struct deluxecoco *)m;
	(void)mdp;
	dragon_reset(m, hard);
	mos6551_reset(mdp->ACIA);
	//ay891x_reset(mdp->PSG);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool deluxecoco_read_byte(struct dragon *md, unsigned A) {
	struct deluxecoco *mdp = (struct deluxecoco *)md;

	switch (md->SAM->S) {
	case 1:
	case 2:
		rombank_d8(mdp->dragon.ROM0, A, &md->CPU->D);
		return 1;

	case 3:
		if (mdp->cart_inhibit) {
			rombank_d8(mdp->dragon.ROM0, A, &md->CPU->D);
			return 1;
		}
		break;

	case 5:
		if ((A & 0x1f) == 0x10) {
			// $FF30 not readable
			return 1;
		}
		if ((A & 0x1c) == 0x1c) {
			mos6551_access(mdp->ACIA, 1, A, &md->CPU->D);
			return 1;
		}
		if ((A & 0x1c) == 0x18) {
			// $FF38 - Inactive
			// $FF39 - Read data
			sound_update(md->snd);
			ay891x_cycle(mdp->PSG, 0, A & 1, &md->CPU->D);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

static _Bool deluxecoco_write_byte(struct dragon *md, unsigned A) {
	struct deluxecoco *mdp = (struct deluxecoco *)md;

	if (md->SAM->S & 4) switch (md->SAM->S) {
	case 1:
	case 2:
		rombank_d8(mdp->dragon.ROM0, A, &md->CPU->D);
		return 1;

	case 3:
		if (mdp->cart_inhibit) {
			rombank_d8(mdp->dragon.ROM0, A, &md->CPU->D);
			return 1;
		}
		break;

	case 5:
		if ((A & 0x1f) == 0x10) {
			mdp->page = md->CPU->D & 0x03;
			mdp->page_enable = md->CPU->D & 0x04;
			mdp->burst = md->CPU->D & 0x08;
			mdp->irq_60hz_enable = md->CPU->D & 0x40;
			mdp->cart_inhibit = md->CPU->D & 0x80;
			if (!mdp->irq_60hz_enable)
				mdp->irq_60hz = 0;
			return 1;
		}
		if ((A & 0x1c) == 0x1c) {
			mos6551_access(mdp->ACIA, 1, A, &md->CPU->D);
			return 1;
		}
		if ((A & 0x1c) == 0x18) {
			// $FF38 - Write data
			// $FF39 - Latch address
			sound_update(md->snd);
			ay891x_cycle(mdp->PSG, 1, A & 1, &md->CPU->D);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

static void deluxecoco_cpu_cycle(void *sptr, _Bool RnW, uint16_t A) {
	struct deluxecoco *mdp = sptr;
	struct dragon *md = &mdp->dragon;

	// Check traps
	dragon_check_traps(md, RnW, A);

	// SAM decode / timing
	int ncycles = md->SAM->mem_cycle(md->SAM, RnW, A);

	// Advance clock, collect IRQs
	if (!md->clock_inhibit) {
		dragon_advance_clock(md, ncycles);
		_Bool supp_irq = mdp->irq_60hz;
		MC6809_IRQ_SET(md->CPU, md->PIA0->a.irq || md->PIA0->b.irq || supp_irq);
		MC6809_FIRQ_SET(md->CPU, md->PIA1->a.irq || md->PIA1->b.irq);
	}

	// Transform DRAM addressing accoding to GAL settings
	unsigned Zrow = md->SAM->Zrow;
	unsigned Zcol = md->SAM->Zcol;
	if (mdp->page_enable && (A & 0xc000) == 0x4000) {
		Zcol = (Zcol & 0x3f) | (mdp->page << 6);
	}

	// Common cycle handling
	dragon_cpu_cycle(md, RnW, A, Zrow, Zcol);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// VDG edge delegates

static void deluxecoco_vdg_hs(void *sptr, _Bool level) {
	struct deluxecoco *mdp = sptr;
	struct dragon *md = &mdp->dragon;
	dragon_vdg_hs(md, level);
	if (!level && mdp->burst) {
		md->ntsc_burst_mod = 3;
	}
}

static void deluxecoco_vdg_fs(void *sptr, _Bool level) {
	struct deluxecoco *mdp = sptr;
	struct dragon *md = &mdp->dragon;
	dragon_vdg_fs(md, level);
	if (!level && mdp->irq_60hz_enable) {
		mdp->irq_60hz = 1;
	}
}
