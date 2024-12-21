/** \file
 *
 *  \brief Dragon Professional (Alpha) support.
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
 *  Dragon Professional (Alpha).
 *
 *  PROBABLY SOMEWHAT INCOMPLETE.
 *
 *  The vast majority of the information for this support has come from
 *  comments in the MAME source code by Phill Harvey-Smith.  I've not been able
 *  to find anything written down anywhere else.
 *
 *  Further thanks to Phill Harvey-Smith for checking connectivity to more FDC
 *  control lines from the AY I/O port.
 *
 *  An extra PIA (PIA2) is added, addressed at $FF24-$FF27 with the following
 *  port use:
 *
 *  PA7..3      N/C
 *  PA2         ROM select (0=BASIC, 1=Boot)
 *  PA1         PSG BC1
 *  PA0         PSG BDIR
 *  PB7..0      PSG D7..0
 *
 *  CA2         FDC NMI enable
 *  CB1         FDC DRQ
 *
 *  An AY-3-8912 (single I/O port) PSG is added, interfaced through PIA2.  I/O
 *  port use is dedicated to the floppy disk controller:
 *
 *  IOA7        Drive type select (0=5.25", 1=8")
 *  IOA6        Write precompensation enable
 *  IOA5        Density select (0=double, 1=single)
 *  IOA4        Drive motor
 *  IOA3        Device select 3
 *  IOA2        Device select 2
 *  IOA1        Device select 1
 *  IOA0        Device select 0
 *
 *  A WD2797 FDC is added, addressed somewhat differently to DragonDOS:
 *
 *  $FF2C       Data register
 *  $FF2D       Sector register
 *  $FF2E       Track register
 *  $FF2F       Command / status register
 */

#include "ay891x.h"
#include "mos6551.h"
#include "vdrive.h"
#include "wd279x.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_dragonpro {
	struct machine_dragon_common machine_dragon;

	struct rombank *BOOT;
	struct rombank *ROM0;
	struct MOS6551 *ACIA;
	struct MC6821 *PIA2;
	struct AY891X *PSG;

	// Points to either BOOT or ROM0 (BASIC)
	struct rombank *rom;

	uint8_t old_ay_io;  // to test if AY I/O output has changed
	struct {
		unsigned device_select;
		_Bool motor_enable;
		_Bool single_density;
		_Bool precomp_enable;
		_Bool nmi_enable;
		struct WD279X *fdc;
		struct vdrive_interface *vdrive_interface;
	} dos;
};

static const struct ser_struct ser_struct_dragonpro[] = {
	// Nest common Dragon data
	SER_ID_STRUCT_NEST(1,  &dragon_ser_struct_data),

	// Floppy disk data
	SER_ID_STRUCT_ELEM(2, struct machine_dragonpro, dos.device_select),
	SER_ID_STRUCT_ELEM(3, struct machine_dragonpro, dos.motor_enable),
	SER_ID_STRUCT_ELEM(4, struct machine_dragonpro, dos.single_density),
	SER_ID_STRUCT_ELEM(5, struct machine_dragonpro, dos.precomp_enable),
	SER_ID_STRUCT_ELEM(6, struct machine_dragonpro, dos.nmi_enable),
};

static const struct ser_struct_data dragonpro_ser_struct_data = {
	.elems = ser_struct_dragonpro,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_dragonpro),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragonpro_config_complete(struct machine_config *);

static _Bool dragonpro_has_interface(struct part *, const char *ifname);
static void dragonpro_attach_interface(struct part *, const char *ifname, void *intf);

static void dragonpro_reset(struct machine *m, _Bool hard);

static _Bool dragonpro_read_byte(struct machine_dragon_common *, unsigned A);
static _Bool dragonpro_write_byte(struct machine_dragon_common *, unsigned A);
static void dragonpro_cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A);

#define dragonpro_pia2a_data_preread NULL
static void dragonpro_pia2a_data_postwrite(void *sptr);
static void dragonpro_pia2a_control_postwrite(void *sptr);
#define dragonpro_pia2b_data_preread NULL
static void dragonpro_pia2b_data_postwrite(void *sptr);
static void dragonpro_pia2b_control_postwrite(void *sptr);

static void dragonpro_ay891x_data_postwrite(void *sptr);

// Handle signals from WD2797
static void set_drq(void *sptr, _Bool value);
static void set_intrq(void *sptr, _Bool value);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct part *dragonpro_allocate(void);
static void dragonpro_initialise(struct part *, void *options);
static _Bool dragonpro_finish(struct part *);
static void dragonpro_free(struct part *);

static const struct partdb_entry_funcs dragonpro_funcs = {
	.allocate = dragonpro_allocate,
	.initialise = dragonpro_initialise,
	.finish = dragonpro_finish,
	.free = dragonpro_free,

	.ser_struct_data = &dragonpro_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_entry dragonpro_part = { .partdb_entry = { .name = "dragonpro", .description = "Dragon Data | Dragon Professional (Alpha)", .funcs = &dragonpro_funcs }, .config_complete = dragonpro_config_complete, .is_working_config = dragon_is_working_config, .cart_arch = "dragon-cart" };

static struct part *dragonpro_allocate(void) {
	struct machine_dragonpro *mdp = part_new(sizeof(*mdp));
	struct machine_dragon_common *md = &mdp->machine_dragon;
	struct machine *m = &md->public;
	struct part *p = &m->part;

	*mdp = (struct machine_dragonpro){0};

	dragon_allocate_common(md);

	m->has_interface = dragonpro_has_interface;
	m->attach_interface = dragonpro_attach_interface;

	m->reset = dragonpro_reset;

	md->read_byte = dragonpro_read_byte;
	md->write_byte = dragonpro_write_byte;

	return p;
}

static void dragonpro_initialise(struct part *p, void *options) {
	assert(p != NULL);
	assert(options != NULL);
	struct machine_dragonpro *mdp = (struct machine_dragonpro *)p;
	struct machine_dragon_common *md = &mdp->machine_dragon;
	struct machine_config *mc = options;

	dragonpro_config_complete(mc);

	md->is_dragon = 1;
	dragon_initialise_common(md, mc);

	// ACIA
	part_add_component(p, part_create("MOS6551", NULL), "ACIA");

	// PIAs
	part_add_component(p, part_create("MC6821", NULL), "PIA2");

	// PSG
	part_add_component(p, part_create("AY891X", NULL), "PSG");

	// FDC
	part_add_component(p, part_create("WD2797", "WD2797"), "FDC");
}

static _Bool dragonpro_finish(struct part *p) {
	assert(p != NULL);
	struct machine_dragonpro *mdp = (struct machine_dragonpro *)p;
	struct machine_dragon_common *md = &mdp->machine_dragon;
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;
	assert(mc != NULL);

	// Find attached parts
	mdp->ACIA = (struct MOS6551 *)part_component_by_id_is_a(p, "ACIA", "MOS6551");
	mdp->PIA2 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA2", "MC6821");
	mdp->PSG = (struct AY891X *)part_component_by_id_is_a(p, "PSG", "AY891X");
	mdp->dos.fdc = (struct WD279X *)part_component_by_id_is_a(p, "FDC", "WD2797");

	// Check all required parts are attached
	if (!mdp->PIA2 || !mdp->PSG || !mdp->dos.fdc) {
		return 0;
	}

	md->is_dragon = 1;
	if (!dragon_finish_common(md))
		return 0;

	// ROMs
	mdp->BOOT = rombank_new(8, 8192, 1);
	mdp->ROM0 = rombank_new(8, 16384, 1);

	// BOOT
	if (mc->extbas_rom) {
		sds tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			rombank_load_image(mdp->BOOT, 0, tmp, 0);
			sdsfree(tmp);
		}
	}

	// 32K BASIC
	if (mc->altbas_rom) {
		sds tmp = romlist_find(mc->altbas_rom);
		if (tmp) {
			rombank_load_image(mdp->ROM0, 0, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Report and check CRC (BOOT)
	rombank_report(mdp->BOOT, "dragonpro", "BOOT");
	uint32_t boot_crc32 = 0xc3dab585;  // Dragon Pro BOOT 1.0
	(void)rombank_verify_crc(mdp->BOOT, "BOOT", -1, "@dragonpro_boot", xroar.cfg.force_crc_match, &boot_crc32);

	// Report and check CRC (32K BASIC)
	rombank_report(mdp->ROM0, "dragonpro", "32K BASIC");
	md->crc_combined = 0x84f68bf9;  // Dragon 64 32K mode BASIC
	md->has_combined = rombank_verify_crc(mdp->ROM0, "32K BASIC", -1, "@dragonpro_basic", xroar.cfg.force_crc_match, &md->crc_combined);

	md->SAM->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, dragonpro_cpu_cycle, mdp);

	mdp->PIA2->a.data_preread = DELEGATE_AS0(void, dragonpro_pia2a_data_preread, mdp);
	mdp->PIA2->a.data_postwrite = DELEGATE_AS0(void, dragonpro_pia2a_data_postwrite, mdp);
	mdp->PIA2->a.control_postwrite = DELEGATE_AS0(void, dragonpro_pia2a_control_postwrite, mdp);
	mdp->PIA2->b.data_preread = DELEGATE_AS0(void, dragonpro_pia2b_data_preread, mdp);
	mdp->PIA2->b.data_postwrite = DELEGATE_AS0(void, dragonpro_pia2b_data_postwrite, mdp);
	mdp->PIA2->b.control_postwrite = DELEGATE_AS0(void, dragonpro_pia2b_control_postwrite, mdp);

	// Default all PIA connections to unconnected (no source, no sink)
	mdp->PIA2->b.in_source = 0;
	mdp->PIA2->a.in_sink = mdp->PIA2->b.in_sink = 0xff;

	// ROM selection from PIA
	mdp->rom = (PIA_VALUE_A(mdp->PIA2) & 0x04) ? mdp->BOOT : mdp->ROM0;

	mdp->PSG->a.data_postwrite = DELEGATE_AS0(void, dragonpro_ay891x_data_postwrite, mdp);

	// Note: the Dragon Professional ROM layout is somewhat different from
	// a normal Dragon 64.  At the moment, it's kludged by having the boot
	// ROM loaded as "extbas" and the BASIC ROM loaded as "altbas", but we
	// could do with a more general named ROM bank config scheme.

	// VDG
	// TODO: this needs verifying.  I'm assuming the same circuit as the
	// Dragon 64, but it may well have been corrected for the Professional.
	md->VDG->is_dragon64 = 1;
	md->VDG->is_dragon32 = 0;
	md->VDG->is_coco = 0;

	return 1;
}

static void dragonpro_free(struct part *p) {
	struct machine_dragonpro *mdp = (struct machine_dragonpro *)p;
	struct machine_dragon_common *md = &mdp->machine_dragon;
	md->snd->get_ay_audio.func = NULL;
	dragon_free_common(p);
	rombank_free(mdp->ROM0);
	rombank_free(mdp->BOOT);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragonpro_config_complete(struct machine_config *mc) {
	// Default ROMs
	set_default_rom(mc->extbas_dfn, &mc->extbas_rom, "@dragonpro_boot");
	set_default_rom(mc->altbas_dfn, &mc->altbas_rom, "@dragonpro_basic");

	// Validate requested total RAM
	if (mc->ram < 64) {
		mc->ram = 32;
	} else {
		mc->ram = 64;
	}

	// Pick RAM org based on requested total RAM if not specified
	if (mc->ram_org == ANY_AUTO) {
		if (mc->ram == 32) {
			mc->ram_org = RAM_ORG_32Kx1;
		} else {
			mc->ram_org = RAM_ORG_64Kx1;
		}
	}

	dragon_config_complete_common(mc);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool dragonpro_has_interface(struct part *p, const char *ifname) {
	if (0 == strcmp(ifname, "floppy"))
		return 1;
	if (0 == strcmp(ifname, "sound"))
		return 1;
	return dragon_has_interface(p, ifname);
}

// Called by dragon_attach_interface()

static void dragonpro_attach_interface(struct part *p, const char *ifname, void *intf) {
	if (!p)
		return;

	struct machine_dragonpro *mdp = (struct machine_dragonpro *)p;

	if (0 == strcmp(ifname, "sound")) {
		struct sound_interface *snd = intf;
		// XXX Testing against sound of MAME (my only reference right
		// now), SAM÷16 sounds too low, SAM÷8 is too high.  So I assume
		// this is derived from the FDC clock instead.
		ay891x_configure(mdp->PSG, 1000000, snd->framerate, EVENT_TICK_RATE, event_current_tick);
		snd->get_ay_audio = DELEGATE_AS3(float, uint32, int, floatp, ay891x_get_audio, mdp->PSG);
		return;
	}

	if (0 != strcmp(ifname, "floppy")) {
		dragon_attach_interface(p, ifname, intf);
		return;
	}

	mdp->dos.vdrive_interface = intf;

	mdp->dos.fdc->set_dirc = DELEGATE_AS1(void, bool, mdp->dos.vdrive_interface->set_dirc, mdp->dos.vdrive_interface);
	mdp->dos.fdc->set_dden = DELEGATE_AS1(void, bool, mdp->dos.vdrive_interface->set_dden, mdp->dos.vdrive_interface);
	mdp->dos.fdc->set_sso = DELEGATE_AS1(void, unsigned, mdp->dos.vdrive_interface->set_sso, mdp->dos.vdrive_interface);
	mdp->dos.fdc->set_drq = DELEGATE_AS1(void, bool, set_drq, mdp);
	mdp->dos.fdc->set_intrq = DELEGATE_AS1(void, bool, set_intrq, mdp);
	mdp->dos.fdc->step = DELEGATE_AS0(void, mdp->dos.vdrive_interface->step, mdp->dos.vdrive_interface);
	mdp->dos.fdc->write = DELEGATE_AS1(void, uint8, mdp->dos.vdrive_interface->write, mdp->dos.vdrive_interface);
	mdp->dos.fdc->skip = DELEGATE_AS0(void, mdp->dos.vdrive_interface->skip, mdp->dos.vdrive_interface);
	mdp->dos.fdc->read = DELEGATE_AS0(uint8, mdp->dos.vdrive_interface->read, mdp->dos.vdrive_interface);
	mdp->dos.fdc->write_idam = DELEGATE_AS0(void, mdp->dos.vdrive_interface->write_idam, mdp->dos.vdrive_interface);
	mdp->dos.fdc->time_to_next_byte = DELEGATE_AS0(unsigned, mdp->dos.vdrive_interface->time_to_next_byte, mdp->dos.vdrive_interface);
	mdp->dos.fdc->time_to_next_idam = DELEGATE_AS0(unsigned, mdp->dos.vdrive_interface->time_to_next_idam, mdp->dos.vdrive_interface);
	mdp->dos.fdc->next_idam = DELEGATE_AS0(uint8p, mdp->dos.vdrive_interface->next_idam, mdp->dos.vdrive_interface);
	mdp->dos.fdc->update_connection = DELEGATE_AS0(void, mdp->dos.vdrive_interface->update_connection, mdp->dos.vdrive_interface);

	mdp->dos.vdrive_interface->tr00 = DELEGATE_AS1(void, bool, wd279x_tr00, mdp->dos.fdc);
	mdp->dos.vdrive_interface->index_pulse = DELEGATE_AS1(void, bool, wd279x_index_pulse, mdp->dos.fdc);
	mdp->dos.vdrive_interface->write_protect = DELEGATE_AS1(void, bool, wd279x_write_protect, mdp->dos.fdc);
	wd279x_update_connection(mdp->dos.fdc);

	// tied high
	wd279x_ready(mdp->dos.fdc, 1);
}

static void dragonpro_reset(struct machine *m, _Bool hard) {
	struct machine_dragonpro *mdp = (struct machine_dragonpro *)m;
	dragon_reset(m, hard);
	mos6551_reset(mdp->ACIA);
	mc6821_reset(mdp->PIA2);
	dragonpro_ay891x_data_postwrite(mdp); // XXX reset AY instead
	wd279x_reset(mdp->dos.fdc);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static uint8_t dragonpro_dos_read(struct machine_dragon_common *, uint16_t A, uint8_t D);
static uint8_t dragonpro_dos_write(struct machine_dragon_common *, uint16_t A, uint8_t D);

static _Bool dragonpro_read_byte(struct machine_dragon_common *md, unsigned A) {
	struct machine_dragonpro *mdp = (struct machine_dragonpro *)md;

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

	case 5:
		if ((A & 0xc) == 0xc) {
			md->CPU->D = dragonpro_dos_read(md, ~A, md->CPU->D);
			return 1;
		}
		if ((A & 0x4) != 0) {
			md->CPU->D = mc6821_read(mdp->PIA2, A);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

static _Bool dragonpro_write_byte(struct machine_dragon_common *md, unsigned A) {
	struct machine_dragonpro *mdp = (struct machine_dragonpro *)md;

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

	case 5:
		if ((A & 0xc) == 0xc) {
			dragonpro_dos_write(md, ~A, md->CPU->D);
			return 1;
		}
		if ((A & 4) != 0) {
			mc6821_write(mdp->PIA2, A, md->CPU->D);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

static void dragonpro_cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_dragonpro *mdp = sptr;
	struct machine_dragon_common *md = &mdp->machine_dragon;

	if (ncycles && !md->clock_inhibit) {
		advance_clock(md, ncycles);
		_Bool supp_firq = mdp->PIA2->a.irq || mdp->PIA2->b.irq;
		MC6809_IRQ_SET(md->CPU, md->PIA0->a.irq || md->PIA0->b.irq);
		MC6809_FIRQ_SET(md->CPU, md->PIA1->a.irq || md->PIA1->b.irq || supp_firq);
	}

	unsigned Zrow = md->SAM->Zrow;
	unsigned Zcol = md->SAM->Zcol;

	dragon_cpu_cycle(md, RnW, A, Zrow, Zcol);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragonpro_pia2a_data_postwrite(void *sptr) {
	struct machine_dragonpro *mdp = sptr;
	struct machine_dragon_common *md = &mdp->machine_dragon;
	mdp->PIA2->b.in_sink = 0xff;
	mdp->PIA2->b.in_source = 0xff;
	uint8_t out = PIA_VALUE_A(mdp->PIA2);
	_Bool BDIR = out & 0x01;
	_Bool BC1 = out & 0x02;
	mdp->rom = (out & 0x04) ? mdp->BOOT : mdp->ROM0;
	sound_update(md->snd);
	uint8_t D = PIA_VALUE_B(mdp->PIA2);
	ay891x_cycle(mdp->PSG, BDIR, BC1, &D);
	if (!BDIR) {
		mdp->PIA2->b.in_sink = mdp->PIA2->b.in_source = D;
	}
}

static void dragonpro_pia2a_control_postwrite(void *sptr) {
	struct machine_dragonpro *mdp = sptr;
	_Bool nmi_enable = PIA_VALUE_CA2(mdp->PIA2);
	if (nmi_enable != mdp->dos.nmi_enable) {
		LOG_MOD_DEBUG(2, "dragonpro", "disk NMI %s\n", nmi_enable?"ENABLED":"DISABLED");
	}
	mdp->dos.nmi_enable = nmi_enable;
}

static void dragonpro_pia2b_data_postwrite(void *sptr) {
	struct machine_dragonpro *mdp = sptr;
	(void)mdp;
}

static void dragonpro_pia2b_control_postwrite(void *sptr) {
	struct machine_dragonpro *mdp = sptr;
	(void)mdp;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// PSG I/O (only port A available on AY-3-8912)

static void dragonpro_ay891x_data_postwrite(void *sptr) {
	struct machine_dragonpro *mdp = sptr;
	struct AY891X *psg = mdp->PSG;
	uint8_t D = AY891X_VALUE_A(psg);

	uint8_t changed = D ^ mdp->old_ay_io;
	mdp->old_ay_io = D;

	// XXX really we want the ability to not have any drive selected
	mdp->dos.device_select = 0;
	if (D & 0x01) {
		mdp->dos.device_select = 0;
	} else if (D & 0x02) {
		mdp->dos.device_select = 1;
	} else if (D & 0x04) {
		mdp->dos.device_select = 2;
	} else if (D & 0x08) {
		mdp->dos.device_select = 3;
	}

	mdp->dos.motor_enable = D & 0x10;
	mdp->dos.single_density = D & 0x20;
	mdp->dos.precomp_enable = D & 0x40;

	if (changed && logging.level >= 2) {
		char *comma = "";
		LOG_MOD_PRINT("dragonpro", "disk config reg: ");
		if (changed & 0x0f) {
			if (D & 0x0f) {
				LOG_PRINT("DEVICE %u", mdp->dos.device_select);
			} else {
				LOG_PRINT("DEVICE -");
			}
			comma = ", ";
		}
		if (changed & 0x10) {
			LOG_PRINT("%sMOTOR %s", comma, mdp->dos.motor_enable?"ON":"OFF");
			comma = ", ";
		}
		if (changed & 0x20) {
			LOG_PRINT("%sDENSITY %s", comma, mdp->dos.single_density?"SINGLE":"DOUBLE");
			comma = ", ";
		}
		if (changed & 0x40) {
			LOG_PRINT("%sPRECOMP %s", comma, mdp->dos.precomp_enable?"ON":"OFF");
			comma = ", ";
		}
		if (changed & 0x80) {
			LOG_PRINT("%sDRIVE %s", comma, (D & 0x80)?"8\"":"5.25\"");
		}
		LOG_PRINT("\n");
	}

	if (mdp->dos.vdrive_interface) {
		mdp->dos.vdrive_interface->set_drive(mdp->dos.vdrive_interface, mdp->dos.device_select);
	}
	wd279x_set_dden(mdp->dos.fdc, !mdp->dos.single_density);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Disk system
//
// This is mostly a duplicate of the DragonDOS cartridge code, but it forms
// part of the machine and is addressed differently.  Control lines are
// connected to the AY I/O port.

// TODO: optional "becker port" might make sense at $FF29/$FF2A?

static uint8_t dragonpro_dos_read(struct machine_dragon_common *md, uint16_t A, uint8_t D) {
	struct machine_dragonpro *mdp = (struct machine_dragonpro *)md;
	(void)D;
	return wd279x_read(mdp->dos.fdc, A);
}

static uint8_t dragonpro_dos_write(struct machine_dragon_common *md, uint16_t A, uint8_t D) {
	struct machine_dragonpro *mdp = (struct machine_dragonpro *)md;
	wd279x_write(mdp->dos.fdc, A, D);
	return D;
}

static void set_drq(void *sptr, _Bool value) {
	struct machine_dragonpro *mdp = sptr;
	mc6821_set_cx1(&mdp->PIA2->b, value);
}

static void set_intrq(void *sptr, _Bool value) {
	struct machine_dragonpro *mdp = sptr;
	struct machine_dragon_common *md = &mdp->machine_dragon;

	// XXX NMI may need to be merged with line from the cartridge.  There
	// may even be a way of selecting between them in the dragonpro...
	if (value) {
		if (mdp->dos.nmi_enable) {
			MC6809_NMI_SET(md->CPU, 1);
		}
	} else {
		MC6809_NMI_SET(md->CPU, 0);
	}
}
