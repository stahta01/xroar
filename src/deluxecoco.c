/** \file
 *
 *  \brief Tandy Deluxe Color Computer support.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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

#include "ay891x.h"
#include "mos6551.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_deluxecoco {
	struct machine_dragon_common machine_dragon;

	struct rombank *ROM0;
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
	SER_ID_STRUCT_ELEM(2, struct machine_deluxecoco, page),
	SER_ID_STRUCT_ELEM(3, struct machine_deluxecoco, page_enable),
	SER_ID_STRUCT_ELEM(4, struct machine_deluxecoco, burst),
	SER_ID_STRUCT_ELEM(5, struct machine_deluxecoco, irq_60hz_enable),
	SER_ID_STRUCT_ELEM(6, struct machine_deluxecoco, irq_60hz),
	SER_ID_STRUCT_ELEM(7, struct machine_deluxecoco, cart_inhibit),
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

static _Bool deluxecoco_read_byte(struct machine_dragon_common *, unsigned A);
static _Bool deluxecoco_write_byte(struct machine_dragon_common *, unsigned A);
static void deluxecoco_cpu_cycle(void *, int ncycles, _Bool RnW, uint16_t A);

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
	struct machine_deluxecoco *mdp = part_new(sizeof(*mdp));
	struct machine_dragon_common *md = &mdp->machine_dragon;
	struct machine *m = &md->public;
	struct part *p = &m->part;

	*mdp = (struct machine_deluxecoco){0};

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
	struct machine_deluxecoco *mdp = (struct machine_deluxecoco *)p;
	struct machine_dragon_common *md = &mdp->machine_dragon;
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
	struct machine_deluxecoco *mdp = (struct machine_deluxecoco *)p;
	struct machine_dragon_common *md = &mdp->machine_dragon;
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
	mdp->ROM0 = rombank_new(8, 8192, 4);

	// Advanced Colour BASIC
	if (mc->extbas_rom) {
		sds tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			rombank_load_image(mdp->ROM0, 0, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Bodge loading the ROM in four parts.  XXX need support for sets of
	// ROMs.
	if (!mdp->ROM0->d[1]) {
		sds tmp = romlist_find("@deluxecoco1");
		if (tmp) {
			rombank_load_image(mdp->ROM0, 1, tmp, 0);
			sdsfree(tmp);
		}
	}
	if (!mdp->ROM0->d[2]) {
		sds tmp = romlist_find("@deluxecoco2");
		if (tmp) {
			rombank_load_image(mdp->ROM0, 2, tmp, 0);
			sdsfree(tmp);
		}
	}
	if (!mdp->ROM0->d[3]) {
		sds tmp = romlist_find("@deluxecoco3");
		if (tmp) {
			rombank_load_image(mdp->ROM0, 3, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Report and check CRC (Advanced Colour BASIC)
	rombank_report(mdp->ROM0, "deluxecoco", "Advanced Colour BASIC");
	md->crc_combined = 0x1cce231e;  // ACB 00.00.07
	md->has_combined = rombank_verify_crc(mdp->ROM0, "Advanced Colour BASIC", -1, "@deluxecoco", xroar.cfg.force_crc_match, &md->crc_combined);

	md->SAM->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, deluxecoco_cpu_cycle, mdp);

	md->VDG->is_dragon64 = 0;
	md->VDG->is_dragon32 = 0;
	md->VDG->is_coco = 1;
	md->VDG->signal_hs = DELEGATE_AS1(void, bool, deluxecoco_vdg_hs, mdp);
	md->VDG->signal_fs = DELEGATE_AS1(void, bool, deluxecoco_vdg_fs, mdp);

	// Deluxe ROM depends on relaxed PIA0 decode
	md->relaxed_pia0_decode = 1;
	// But $FF20-$FF3F is shared with other devices
	md->relaxed_pia1_decode = 0;

	return 1;
}

static void deluxecoco_free(struct part *p) {
	struct machine_deluxecoco *mdp = (struct machine_deluxecoco *)p;
	struct machine_dragon_common *md = &mdp->machine_dragon;
	md->snd->get_non_muxed_audio.func = NULL;
        dragon_free_common(p);
	rombank_free(mdp->ROM0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void deluxecoco_config_complete(struct machine_config *mc) {
	// Default ROMs
	set_default_rom(mc->extbas_dfn, &mc->extbas_rom, "@deluxecoco");

	// Validate requested total RAM
	if (mc->ram < 32) {
		mc->ram = 16;
	} else if (mc->ram < 64) {
		mc->ram = 32;
	} else {
		mc->ram = 64;
	}

	// Pick RAM org based on requested total RAM if not specified
	if (mc->ram_org == ANY_AUTO) {
		if (mc->ram == 16) {
			mc->ram_org = RAM_ORG_16Kx1;
		} else if (mc->ram == 32) {
			mc->ram_org = RAM_ORG_32Kx1;
		} else {
			mc->ram_org = RAM_ORG_64Kx1;
		}
	}

	// Keyboard map
	if (mc->keymap == ANY_AUTO) {
		mc->keymap = dkbd_layout_coco3;
	}

	dragon_config_complete_common(mc);
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

	struct machine_deluxecoco *mdp = (struct machine_deluxecoco *)p;

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
        struct machine_deluxecoco *mdp = (struct machine_deluxecoco *)m;
	(void)mdp;
	dragon_reset(m, hard);
	mos6551_reset(mdp->ACIA);
	//ay891x_reset(mdp->PSG);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool deluxecoco_read_byte(struct machine_dragon_common *md, unsigned A) {
	struct machine_deluxecoco *mdp = (struct machine_deluxecoco *)md;

	switch (md->SAM->S) {
	case 1:
	case 2:
		rombank_d8(mdp->ROM0, A, &md->CPU->D);
		return 1;

	case 3:
		if (mdp->cart_inhibit) {
			rombank_d8(mdp->ROM0, A, &md->CPU->D);
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

static _Bool deluxecoco_write_byte(struct machine_dragon_common *md, unsigned A) {
	struct machine_deluxecoco *mdp = (struct machine_deluxecoco *)md;

	if (md->SAM->S & 4) switch (md->SAM->S) {
	case 1:
	case 2:
		rombank_d8(mdp->ROM0, A, &md->CPU->D);
		return 1;

	case 3:
		if (mdp->cart_inhibit) {
			rombank_d8(mdp->ROM0, A, &md->CPU->D);
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

static void deluxecoco_cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_deluxecoco *mdp = sptr;
	struct machine_dragon_common *md = &mdp->machine_dragon;

	if (ncycles && !md->clock_inhibit) {
		advance_clock(md, ncycles);
		_Bool supp_irq = mdp->irq_60hz;
		MC6809_IRQ_SET(md->CPU, md->PIA0->a.irq || md->PIA0->b.irq || supp_irq);
		MC6809_FIRQ_SET(md->CPU, md->PIA1->a.irq || md->PIA1->b.irq);
	}

	unsigned Zrow = md->SAM->Zrow;
	unsigned Zcol = md->SAM->Zcol;
	if (mdp->page_enable && (A & 0xc000) == 0x4000) {
		Zcol = (Zcol & 0x3f) | (mdp->page << 6);
	}

	dragon_cpu_cycle(md, RnW, A, Zrow, Zcol);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// VDG edge delegates

static void deluxecoco_vdg_hs(void *sptr, _Bool level) {
	struct machine_deluxecoco *mdp = sptr;
	struct machine_dragon_common *md = &mdp->machine_dragon;
	mc6821_set_cx1(&md->PIA0->a, level);
	mc6883_vdg_hsync(md->SAM, level);
	if (!level) {
		unsigned p1bval = md->PIA1->b.out_source & md->PIA1->b.out_sink;
		_Bool GM0 = p1bval & 0x10;
		_Bool CSS = p1bval & 0x08;
		md->ntsc_burst_mod = (md->use_ntsc_burst_mod && GM0 && CSS) ? 2 : 0;
		if (mdp->burst) {
			md->ntsc_burst_mod = 3;
		}
	}
}

static void deluxecoco_vdg_fs(void *sptr, _Bool level) {
	struct machine_deluxecoco *mdp = sptr;
	struct machine_dragon_common *md = &mdp->machine_dragon;
	mc6821_set_cx1(&md->PIA0->b, level);
	mc6883_vdg_fsync(md->SAM, level);
	if (level) {
		sound_update(md->snd);
		md->frame--;
		if (md->frame < 0)
			md->frame = md->frameskip;
		vo_vsync(md->vo, md->frame == 0);
	} else {
		if (mdp->irq_60hz_enable) {
			mdp->irq_60hz = 1;
		}
	}
}
