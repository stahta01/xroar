/** \file
 *
 *  \brief Dragon and Tandy Colour Computer machines.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "array.h"
#include "delegate.h"
#include "sds.h"

#include "ao.h"
#include "breakpoint.h"
#include "cart.h"
#include "crc32.h"
#include "crclist.h"
#include "gdb.h"
#include "hd6309.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
#include "ntsc.h"
#include "part.h"
#include "printer.h"
#include "romlist.h"
#include "sam.h"
#include "serialise.h"
#include "sound.h"
#include "tape.h"
#include "vdg_palette.h"
#include "vo.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static const struct {
	const char *bas;
	const char *extbas;
	const char *altbas;
} rom_list[] = {
	{ NULL, "@dragon32", NULL },
	{ NULL, "@dragon64", "@dragon64_alt" },
	{ "@coco", "@coco_ext", NULL }
};

enum machine_ram_organisation {
	RAM_ORGANISATION_4K,
	RAM_ORGANISATION_16K,
	RAM_ORGANISATION_64K
};

struct machine_dragon {
	struct machine public;  // first element in turn is part

	struct MC6809 *CPU;
	struct MC6883 *SAM;
	struct MC6821 *PIA0, *PIA1;
	struct MC6847 *VDG;

	struct vo_interface *vo;
	int frame;  // track frameskip
	struct sound_interface *snd;

	unsigned int ram_size;
	uint8_t ram[0x10000];
	uint8_t *rom;
	uint8_t rom0[0x4000];
	uint8_t rom1[0x4000];
	uint8_t ext_charset[0x1000];
	struct machine_memory ram0;  // introspection
	struct machine_memory ram1;  // introspection

	_Bool inverted_text;
	struct cart *cart;
	unsigned frameskip;

	int cycles;

	// Debug
	struct bp_session *bp_session;
	_Bool single_step;
	int stop_signal;
#ifdef WANT_GDB_TARGET
	struct gdb_interface *gdb_interface;
#endif

	struct tape_interface *tape_interface;
	struct printer_interface *printer_interface;

	struct {
		struct keyboard_interface *interface;
	} keyboard;

	// NTSC colour bursts
	_Bool use_ntsc_burst_mod; // 0 for PAL-M (green-magenta artifacting)
	unsigned ntsc_burst_mod;
	struct ntsc_burst *ntsc_burst[4];

	// Useful configuration side-effect tracking
	_Bool has_bas, has_extbas, has_altbas, has_combined;
	_Bool has_ext_charset;
	uint32_t crc_bas, crc_extbas, crc_altbas, crc_combined;
	uint32_t crc_ext_charset;
	enum machine_ram_organisation ram_organisation;
	uint16_t ram_mask;
	_Bool is_dragon;
	_Bool is_dragon32;
	_Bool is_dragon64;
	_Bool unexpanded_dragon32;
	_Bool relaxed_pia_decode;
	_Bool have_acia;
};

static const struct ser_struct ser_struct_dragon[] = {
	SER_STRUCT_NEST(&machine_ser_struct_data), // 1
        SER_STRUCT_ELEM(struct machine_dragon, ram, ser_type_unhandled), // 2
        SER_STRUCT_ELEM(struct machine_dragon, ram_size, ser_type_unsigned), // 3
        SER_STRUCT_ELEM(struct machine_dragon, ram_mask, ser_type_unsigned), // 4
        SER_STRUCT_ELEM(struct machine_dragon, inverted_text, ser_type_bool), // 5
};

#define DRAGON_SER_RAM     (2)

static _Bool dragon_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool dragon_write_elem(void *sptr, struct ser_handle *sh, int tag);

const struct ser_struct_data dragon_ser_struct_data = {
	.elems = ser_struct_dragon,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_dragon),
	.read_elem = dragon_read_elem,
	.write_elem = dragon_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void verify_ram_size(struct machine_config *mc) {
	if (mc->ram < 4 || mc->ram > 64) {
		mc->ram = (mc->architecture == ARCH_DRAGON32) ? 32 : 64;
	} else if (mc->ram < 8) {
		mc->ram = 4;
	} else if (mc->ram < 16) {
		mc->ram = 8;
	} else if (mc->ram < 32) {
		mc->ram = 16;
	} else if (mc->ram < 64) {
		mc->ram = 32;
	}
}

static void dragon_config_complete(struct machine_config *mc) {
	if (mc->tv_standard == ANY_AUTO)
		mc->tv_standard = TV_PAL;
	if (mc->tv_input == ANY_AUTO) {
		switch (mc->tv_standard) {
		default:
		case TV_PAL:
			mc->tv_input = TV_INPUT_CMP_PALETTE;
			break;
		case TV_NTSC:
		case TV_PAL_M:
			mc->tv_input = TV_INPUT_CMP_KBRW;
			break;
		}
	}
	if (mc->vdg_type == ANY_AUTO)
		mc->vdg_type = VDG_6847;
	if (mc->vdg_type != VDG_6847 && mc->vdg_type != VDG_6847T1)
		mc->vdg_type = VDG_6847;
	/* Various heuristics to find a working architecture */
	if (mc->architecture == ANY_AUTO) {
		/* TODO: checksum ROMs to help determine arch */
		if (mc->bas_rom) {
			mc->architecture = ARCH_COCO;
		} else if (mc->altbas_rom) {
			mc->architecture = ARCH_DRAGON64;
		} else if (mc->extbas_rom) {
			struct stat statbuf;
			mc->architecture = ARCH_DRAGON64;
			if (stat(mc->extbas_rom, &statbuf) == 0) {
				if (statbuf.st_size <= 0x2000) {
					mc->architecture = ARCH_COCO;
				}
			}
		} else {
			mc->architecture = ARCH_DRAGON64;
		}
	}
	verify_ram_size(mc);
	if (mc->keymap == ANY_AUTO) {
		switch (mc->architecture) {
		case ARCH_DRAGON64: case ARCH_DRAGON32: default:
			mc->keymap = dkbd_layout_dragon;
			break;
		case ARCH_COCO:
			mc->keymap = dkbd_layout_coco;
			break;
		}
	}
	/* Now find which ROMs we're actually going to use */
	if (!mc->bas_dfn && !mc->bas_rom && rom_list[mc->architecture].bas) {
		mc->bas_rom = xstrdup(rom_list[mc->architecture].bas);
	}
	if (!mc->extbas_dfn && !mc->extbas_rom && rom_list[mc->architecture].extbas) {
		mc->extbas_rom = xstrdup(rom_list[mc->architecture].extbas);
	}
	if (!mc->altbas_dfn && !mc->altbas_rom && rom_list[mc->architecture].altbas) {
		mc->altbas_rom = xstrdup(rom_list[mc->architecture].altbas);
	}
	// Determine a default DOS cartridge if necessary
	if (!mc->default_cart_dfn && !mc->default_cart) {
		struct cart_config *cc = cart_find_working_dos(mc);
		if (cc)
			mc->default_cart = xstrdup(cc->name);
	}
}

static _Bool dragon_is_working_config(struct machine_config *mc) {
	if (!mc)
		return 0;
	sds tmp;
	if (mc->bas_rom) {
		tmp = romlist_find(mc->bas_rom);
		if (!tmp)
			return 0;
		sdsfree(tmp);
	}
	if (mc->extbas_rom) {
		tmp = romlist_find(mc->extbas_rom);
		if (!tmp)
			return 0;
		sdsfree(tmp);
	}
	// but one of them should exist...
	if (!mc->bas_rom && !mc->extbas_rom)
		return 0;
	// No need to check altbas - it's an alternate, not a requirement.
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon_connect_cart(struct part *p);
static void dragon_insert_cart(struct machine *m, struct cart *c);
static void dragon_remove_cart(struct machine *m);

static void dragon_reset(struct machine *m, _Bool hard);
static enum machine_run_state dragon_run(struct machine *m, int ncycles);
static void dragon_single_step(struct machine *m);
static void dragon_signal(struct machine *m, int sig);
static void dragon_trap(void *sptr);
static void dragon_bp_add_n(struct machine *m, struct machine_bp *list, int n, void *sptr);
static void dragon_bp_remove_n(struct machine *m, struct machine_bp *list, int n);

static int dragon_set_keyboard_type(struct machine *m, int action);
static _Bool dragon_set_pause(struct machine *m, int state);
static _Bool dragon_set_inverted_text(struct machine *m, int state);
static void *dragon_get_component(struct machine *m, const char *cname);
static void *dragon_get_interface(struct machine *m, const char *ifname);
static void dragon_set_frameskip(struct machine *m, unsigned fskip);
static void dragon_set_ratelimit(struct machine *m, _Bool ratelimit);

static uint8_t dragon_read_byte(struct machine *m, unsigned A, uint8_t D);
static void dragon_write_byte(struct machine *m, unsigned A, uint8_t D);
static void dragon_op_rts(struct machine *m);
static void dragon_dump_ram(struct machine *m, FILE *fd);

static void keyboard_update(void *sptr);
static void joystick_update(void *sptr);
static void update_sound_mux_source(void *sptr);
static void update_vdg_mode(struct machine_dragon *md);

static void single_bit_feedback(void *sptr, _Bool level);
static void update_audio_from_tape(void *sptr, float value);
static void cart_firq(void *sptr, _Bool level);
static void cart_nmi(void *sptr, _Bool level);
static void cart_halt(void *sptr, _Bool level);
static void vdg_hs(void *sptr, _Bool level);
static void vdg_hs_pal_coco(void *sptr, _Bool level);
static void vdg_fs(void *sptr, _Bool level);
static void vdg_render_line(void *sptr, uint8_t *data, unsigned burst);
static void printer_ack(void *sptr, _Bool ack);

static void cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A);
static void cpu_cycle_noclock(void *sptr, int ncycles, _Bool RnW, uint16_t A);
static void dragon_instruction_posthook(void *sptr);
static void vdg_fetch_handler(void *sptr, uint16_t A, int nbytes, uint16_t *dest);
static void vdg_fetch_handler_chargen(void *sptr, uint16_t A, int nbytes, uint16_t *dest);

static void pia0a_data_preread(void *sptr);
#define pia0a_data_postwrite NULL
#define pia0a_control_postwrite update_sound_mux_source
#define pia0b_data_preread keyboard_update
#define pia0b_data_postwrite NULL
#define pia0b_control_postwrite update_sound_mux_source
static void pia0b_data_preread_coco64k(void *sptr);

#define pia1a_data_preread NULL
static void pia1a_data_postwrite(void *sptr);
static void pia1a_control_postwrite(void *sptr);
#define pia1b_data_preread NULL
static void pia1b_data_preread_dragon(void *sptr);
static void pia1b_data_preread_coco64k(void *sptr);
static void pia1b_data_postwrite(void *sptr);
static void pia1b_control_postwrite(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Dragon part creation

static struct part *dragon_allocate(void);
static void dragon_initialise(struct part *p, void *options);
static _Bool dragon_finish(struct part *p);
static void dragon_free(struct part *p);

static const struct partdb_entry_funcs dragon_funcs = {
	.allocate = dragon_allocate,
	.initialise = dragon_initialise,
	.finish = dragon_finish,
	.free = dragon_free,

	.ser_struct_data = &dragon_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_extra dragon_machine_extra = {
	.config_complete = dragon_config_complete,
	.is_working_config = dragon_is_working_config,
};

const struct partdb_entry dragon64_part = { .name = "dragon64", .funcs = &dragon_funcs, .extra = { &dragon_machine_extra } };
const struct partdb_entry dragon32_part = { .name = "dragon32", .funcs = &dragon_funcs, .extra = { &dragon_machine_extra } };
const struct partdb_entry coco_part = { .name = "coco", .funcs = &dragon_funcs, .extra = { &dragon_machine_extra } };

static struct part *dragon_allocate(void) {
	struct machine_dragon *md = part_new(sizeof(*md));
	struct machine *m = &md->public;
	struct part *p = &m->part;

	*md = (struct machine_dragon){0};

	m->insert_cart = dragon_insert_cart;
	m->remove_cart = dragon_remove_cart;
	m->reset = dragon_reset;
	m->run = dragon_run;
	m->single_step = dragon_single_step;
	m->signal = dragon_signal;
	m->bp_add_n = dragon_bp_add_n;
	m->bp_remove_n = dragon_bp_remove_n;

	m->set_keyboard_type = dragon_set_keyboard_type;
	m->set_pause = dragon_set_pause;
	m->set_inverted_text = dragon_set_inverted_text;
	m->get_component = dragon_get_component;
	m->get_interface = dragon_get_interface;
	m->set_frameskip = dragon_set_frameskip;
	m->set_ratelimit = dragon_set_ratelimit;

	m->read_byte = dragon_read_byte;
	m->write_byte = dragon_write_byte;
	m->op_rts = dragon_op_rts;
	m->dump_ram = dragon_dump_ram;

	m->keyboard.type = dkbd_layout_dragon;

	return p;
}

static void dragon_initialise(struct part *p, void *options) {
	struct machine_config *mc = options;
	assert(mc != NULL);

	struct machine_dragon *md = (struct machine_dragon *)p;
	struct machine *m = &md->public;

	dragon_config_complete(mc);
	m->config = mc;

	// SAM
	part_add_component(&m->part, part_create("SN74LS783", NULL), "SAM");

	// CPU
	part_add_component(&m->part, part_create((mc->cpu == CPU_HD6309) ? "HD6309" : "MC6809", NULL), "CPU");

	// PIAs
	part_add_component(&m->part, part_create("MC6821", NULL), "PIA0");
	part_add_component(&m->part, part_create("MC6821", NULL), "PIA1");

	// VDG
	part_add_component(&m->part, part_create("MC6847", (mc->vdg_type == VDG_6847T1 ? "6847T1" : "6847")), "VDG");

	// Keyboard
	m->keyboard.type = mc->keymap;
}

static _Bool dragon_finish(struct part *p) {
	struct machine_dragon *md = (struct machine_dragon *)p;
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;

	// Interfaces
	md->vo = xroar_vo_interface;
	md->snd = xroar_ao_interface->sound_interface;
	md->tape_interface = xroar_tape_interface;

	md->tape_interface->default_paused = 0;

	// Find attached parts
	md->SAM = (struct MC6883 *)part_component_by_id_is_a(p, "SAM", "SN74LS783");
	md->CPU = (struct MC6809 *)part_component_by_id_is_a(p, "CPU", "MC6809");
	md->PIA0 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA0", "MC6821");
	md->PIA1 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA1", "MC6821");
	md->VDG = (struct MC6847 *)part_component_by_id_is_a(p, "VDG", "MC6847");

	// Check all required parts are attached
	if (!md->SAM || !md->CPU || !md->PIA0 || !md->PIA1 || !md->VDG ||
	    !md->vo || !md->snd || !md->tape_interface) {
		return 0;
	}

	// Connect any cartridge part
	dragon_connect_cart(p);

	switch (mc->architecture) {
	case ARCH_DRAGON32:
		md->is_dragon32 = md->is_dragon = 1;
		break;
	case ARCH_DRAGON64:
		md->is_dragon64 = md->is_dragon = 1;
		break;
	default:
		break;
	}

	md->SAM->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, md);
	md->CPU->mem_cycle = DELEGATE_AS2(void, bool, uint16, sam_mem_cycle, md->SAM);

	// Breakpoint session
	md->bp_session = bp_session_new(m);
	assert(md->bp_session != NULL);  // this shouldn't fail
	md->bp_session->trap_handler = DELEGATE_AS0(void, dragon_trap, m);

	// PIAs
	md->PIA0->a.data_preread = DELEGATE_AS0(void, pia0a_data_preread, md);
	md->PIA0->a.data_postwrite = DELEGATE_AS0(void, pia0a_data_postwrite, md);
	md->PIA0->a.control_postwrite = DELEGATE_AS0(void, pia0a_control_postwrite, md);
	md->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, md);
	md->PIA0->b.data_postwrite = DELEGATE_AS0(void, pia0b_data_postwrite, md);
	md->PIA0->b.control_postwrite = DELEGATE_AS0(void, pia0b_control_postwrite, md);

	md->PIA1->a.data_preread = DELEGATE_AS0(void, pia1a_data_preread, md);
	md->PIA1->a.data_postwrite = DELEGATE_AS0(void, pia1a_data_postwrite, md);
	md->PIA1->a.control_postwrite = DELEGATE_AS0(void, pia1a_control_postwrite, md);
	md->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread, md);
	md->PIA1->b.data_postwrite = DELEGATE_AS0(void, pia1b_data_postwrite, md);
	md->PIA1->b.control_postwrite = DELEGATE_AS0(void, pia1b_control_postwrite, md);

	// Single-bit sound feedback
	md->snd->sbs_feedback = DELEGATE_AS1(void, bool, single_bit_feedback, md);

	// VDG
	// XXX kludges that should be handled by machine-specific code
	md->VDG->is_dragon64 = md->is_dragon64;
	md->VDG->is_dragon32 = md->is_dragon32;
	md->VDG->is_coco = !md->is_dragon;
	_Bool is_pal = (mc->tv_standard == TV_PAL);
	md->VDG->is_pal = is_pal;
	md->use_ntsc_burst_mod = (mc->tv_standard != TV_PAL_M);

	if (!md->is_dragon && is_pal) {
		md->VDG->signal_hs = DELEGATE_AS1(void, bool, vdg_hs_pal_coco, md);
	} else {
		md->VDG->signal_hs = DELEGATE_AS1(void, bool, vdg_hs, md);
	}
	md->VDG->signal_fs = DELEGATE_AS1(void, bool, vdg_fs, md);
	md->VDG->render_line = DELEGATE_AS2(void, uint8p, unsigned, vdg_render_line, md);
	md->VDG->fetch_data = DELEGATE_AS3(void, uint16, int, uint16p, vdg_fetch_handler, md);
	mc6847_set_inverted_text(md->VDG, md->inverted_text);

	// Set up VDG palette in video module
	{
		struct vdg_palette *palette = vdg_palette_by_name(mc->vdg_palette);
		if (!palette) {
			palette = vdg_palette_by_name("ideal");
		}
		float blank_y = palette->blank_y;
		//float white_y = palette->white_y;
		//float scale_y = 1. / (blank_y - white_y);
		for (int c = 0; c < NUM_VDG_COLOURS; c++) {
			float y = palette->palette[c].y;
			float chb = palette->palette[c].chb;
			float b_y = palette->palette[c].b - chb;
			float r_y = palette->palette[c].a - chb;
			y = (blank_y - y) * 2.850;  //scale_y;
			DELEGATE_CALL(md->vo->palette_set_ybr, c, y, b_y, r_y);
		}
	}

	md->ntsc_burst[0] = ntsc_burst_new(-33);  // No burst (hi-res, css=1)
	md->ntsc_burst[1] = ntsc_burst_new(0);  // Normal burst (mode modes)
	md->ntsc_burst[2] = ntsc_burst_new(33);  // Modified burst (coco hi-res css=1)
	// This was going to represent the extra colourburst mode achievable by
	// switching to/from colour modes at the right time that Sock Master
	// demoed.  Until I look into that properly, it's actually used for CSS
	// + GM0 in non-resolution-graphics mode, so just set it to same as
	// normal burst.
	md->ntsc_burst[3] = ntsc_burst_new(0);

	verify_ram_size(mc);
	md->ram_size = mc->ram * 1024;

	/* Load appropriate ROMs */
	memset(md->rom0, 0, sizeof(md->rom0));
	memset(md->rom1, 0, sizeof(md->rom1));
	memset(md->ext_charset, 0, sizeof(md->ext_charset));

	/*
	 * Dragon ROMs are always Extended BASIC only, and even though (some?)
	 * Dragon 32s split this across two pieces of hardware, it doesn't make
	 * sense to consider the two regions separately.
	 *
	 * Dragon 64s contain a separate 64K mode Extended BASIC.
	 *
	 * CoCo ROMs are always considered to be in two parts: BASIC and
	 * Extended BASIC.
	 *
	 * Later CoCos and clones may have been distributed with only one ROM
	 * containing the combined image.  If Extended BASIC is found to be
	 * more than 8K, it's assumed to be one of these combined ROMs.
	 */

	md->has_combined = md->has_extbas = md->has_bas = md->has_altbas = 0;
	md->crc_combined = md->crc_extbas = md->crc_bas = md->crc_altbas = 0;
	md->has_ext_charset = 0;
	md->crc_ext_charset = 0;

	/* ... Extended BASIC */
	if (mc->extbas_rom) {
		sds tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, md->rom0, sizeof(md->rom0));
			if (size > 0) {
				if (md->is_dragon)
					md->has_combined = 1;
				else
					md->has_extbas = 1;
			}
			if (size > 0x2000) {
				if (!md->has_combined)
					md->has_bas = 1;
			}
			sdsfree(tmp);
		}
	}

	/* ... BASIC */
	if (mc->bas_rom) {
		sds tmp = romlist_find(mc->bas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, md->rom0 + 0x2000, sizeof(md->rom0) - 0x2000);
			if (size > 0)
				md->has_bas = 1;
			sdsfree(tmp);
		}
	}

	/* ... 64K mode Extended BASIC */
	if (mc->altbas_rom) {
		sds tmp = romlist_find(mc->altbas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, md->rom1, sizeof(md->rom1));
			if (size > 0)
				md->has_altbas = 1;
			sdsfree(tmp);
		}
	}

	/* This will be under PIA control on a Dragon 64 */
	md->rom = md->rom0;

	if (mc->ext_charset_rom) {
		sds tmp = romlist_find(mc->ext_charset_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, md->ext_charset, sizeof(md->ext_charset));
			if (size > 0)
				md->has_ext_charset = 1;
			sdsfree(tmp);
		}
	}

	/* CRCs */

	if (md->has_combined) {
		_Bool forced = 0, valid_crc = 0;

		md->crc_combined = crc32_block(CRC32_RESET, md->rom0, 0x4000);

		if (md->is_dragon64)
			valid_crc = crclist_match("@d64_1", md->crc_combined);
		else if (md->is_dragon32)
			valid_crc = crclist_match("@d32", md->crc_combined);

		if (xroar_cfg.force_crc_match) {
			if (md->is_dragon64) {
				md->crc_combined = 0x84f68bf9;  // Dragon 64 32K mode BASIC
				forced = 1;
			} else if (md->is_dragon32) {
				md->crc_combined = 0xe3879310;  // Dragon 32 32K mode BASIC
				forced = 1;
			}
		}

		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\t32K mode BASIC CRC = 0x%08x%s\n", md->crc_combined, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for combined BASIC ROM\n");
		}
	}

	if (md->has_altbas) {
		_Bool forced = 0, valid_crc = 0;

		md->crc_altbas = crc32_block(CRC32_RESET, md->rom1, 0x4000);

		if (md->is_dragon64)
			valid_crc = crclist_match("@d64_2", md->crc_altbas);

		if (xroar_cfg.force_crc_match) {
			if (md->is_dragon64) {
				md->crc_altbas = 0x17893a42;  // Dragon 64 64K mode BASIC
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\t64K mode BASIC CRC = 0x%08x%s\n", md->crc_altbas, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for alternate BASIC ROM\n");
		}
	}

	if (md->has_bas) {
		_Bool forced = 0, valid_crc = 0, coco4k = 0;

		md->crc_bas = crc32_block(CRC32_RESET, md->rom0 + 0x2000, 0x2000);

		if (!md->is_dragon) {
			if (mc->ram > 4) {
				valid_crc = crclist_match("@coco", md->crc_bas);
			} else {
				valid_crc = crclist_match("@bas10", md->crc_bas);
				coco4k = 1;
			}
		}

		if (xroar_cfg.force_crc_match) {
			if (!md->is_dragon) {
				if (mc->ram > 4) {
					md->crc_bas = 0xd8f4d15e;  // CoCo BASIC 1.3
				} else {
					md->crc_bas = 0x00b50aaa;  // CoCo BASIC 1.0
				}
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\tBASIC CRC = 0x%08x%s\n", md->crc_bas, forced ? " (forced)" : "");
		if (!valid_crc) {
			if (coco4k) {
				LOG_WARN("Invalid CRC for Colour BASIC 1.0 ROM\n");
			} else {
				LOG_WARN("Invalid CRC for Colour BASIC ROM\n");
			}
		}
	}

	if (md->has_extbas) {
		_Bool forced = 0, valid_crc = 0;

		md->crc_extbas = crc32_block(CRC32_RESET, md->rom0, 0x2000);

		if (!md->is_dragon) {
			valid_crc = crclist_match("@cocoext", md->crc_extbas);
		}

		if (xroar_cfg.force_crc_match) {
			if (!md->is_dragon) {
				md->crc_extbas = 0xa82a6254;  // CoCo Extended BASIC 1.1
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\tExtended BASIC CRC = 0x%08x%s\n", md->crc_extbas, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for Extended Colour BASIC ROM\n");
		}
	}
	if (md->has_ext_charset) {
		md->crc_ext_charset = crc32_block(CRC32_RESET, md->ext_charset, 0x1000);
		LOG_DEBUG(1, "\tExternal charset CRC = 0x%08x\n", md->crc_ext_charset);
	}

	/* VDG external charset */
	if (md->has_ext_charset)
		md->VDG->fetch_data = DELEGATE_AS3(void, uint16, int, uint16p, vdg_fetch_handler_chargen, md);

	/* Default all PIA connections to unconnected (no source, no sink) */
	md->PIA0->b.in_source = 0;
	md->PIA1->b.in_source = 0;
	md->PIA0->a.in_sink = md->PIA0->b.in_sink = 0xff;
	md->PIA1->a.in_sink = md->PIA1->b.in_sink = 0xff;
	/* Machine-specific PIA connections */
	if (md->is_dragon) {
		// Pull-up resistor on centronics !BUSY (PIA1 PB2)
		md->PIA1->b.in_source |= (1<<0);
	}
	if (md->is_dragon64) {
		md->have_acia = 1;
		// Pull-up resistor on ROMSEL (PIA1 PB2)
		md->PIA1->b.in_source |= (1<<2);
	} else if (!md->is_dragon && mc->ram <= 4) {
		// 4K CoCo ties PIA1 PB2 low
		md->PIA1->b.in_sink &= ~(1<<2);
	} else if (!md->is_dragon && mc->ram <= 16) {
		// 16K CoCo pulls PIA1 PB2 high
		md->PIA1->b.in_source |= (1<<2);
	}
	md->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, md);
	if (md->is_dragon) {
		/* Dragons need to poll printer BUSY state */
		md->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread_dragon, md);
	}
	if (!md->is_dragon && mc->ram > 16) {
		// 64K CoCo connects PIA0 PB6 to PIA1 PB2:
		// Deal with this through a postwrite.
		md->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread_coco64k, md);
		md->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread_coco64k, md);
	}

	// RAM configuration

	md->ram0.max_size = 0x8000;
	md->ram0.size = (md->ram_size > 0x8000) ? 0x8000 : md->ram_size;
	md->ram0.data = md->ram;
	md->ram1.max_size = 0x8000;
	md->ram1.size = (md->ram_size > 0x8000) ? (md->ram_size - 0x8000) : 0;
	md->ram1.data = md->ram + 0x8000;

	// Defaults: Dragon 64 with 64K
	md->unexpanded_dragon32 = 0;
	md->relaxed_pia_decode = 0;
	md->ram_mask = 0xffff;

	if (!md->is_dragon) {
		if (mc->ram <= 4) {
			md->ram_organisation = RAM_ORGANISATION_4K;
			md->ram_mask = 0x3f3f;
		} else if (mc->ram <= 16) {
			md->ram_organisation = RAM_ORGANISATION_16K;
		} else {
			md->ram_organisation = RAM_ORGANISATION_64K;
			if (mc->ram <= 32)
				md->ram_mask = 0x7fff;
		}
		md->relaxed_pia_decode = 1;
	}

	if (md->is_dragon) {
		md->ram_organisation = RAM_ORGANISATION_64K;
		if (md->is_dragon32 && mc->ram <= 32) {
			md->unexpanded_dragon32 = 1;
			md->relaxed_pia_decode = 1;
			md->ram_mask = 0x7fff;
		}
	}

	// Keyboard interface
	md->keyboard.interface = keyboard_interface_new(m);
	if (md->is_dragon) {
		keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_dragon_32k_basic);
	} else {
		keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_coco_basic);
	}
	keyboard_set_keymap(md->keyboard.interface, m->keyboard.type);

	// Printer interface
	md->printer_interface = printer_interface_new(m);
	md->printer_interface->signal_ack = DELEGATE_AS1(void, bool, printer_ack, md);

#ifdef WANT_GDB_TARGET
	// GDB
	if (xroar_cfg.gdb) {
		md->gdb_interface = gdb_interface_new(xroar_cfg.gdb_ip, xroar_cfg.gdb_port, m, md->bp_session);
	}
#endif

	// XXX until we serialise sound information
	update_sound_mux_source(md);
	sound_set_mux_enabled(md->snd, md->PIA1->b.control_register & 0x08);

	return 1;
}

// Called from part_free(), which handles freeing the struct itself
static void dragon_free(struct part *p) {
	struct machine_dragon *md = (struct machine_dragon *)p;
#ifdef WANT_GDB_TARGET
	if (md->gdb_interface) {
		gdb_interface_free(md->gdb_interface);
	}
#endif
	if (md->keyboard.interface) {
		keyboard_interface_free(md->keyboard.interface);
	}
	if (md->printer_interface) {
		printer_interface_free(md->printer_interface);
	}
	if (md->bp_session) {
		bp_session_free(md->bp_session);
	}
	ntsc_burst_free(md->ntsc_burst[3]);
	ntsc_burst_free(md->ntsc_burst[2]);
	ntsc_burst_free(md->ntsc_burst[1]);
	ntsc_burst_free(md->ntsc_burst[0]);
}

static _Bool dragon_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_dragon *md = sptr;
	size_t length = ser_data_length(sh);
	switch (tag) {
	case DRAGON_SER_RAM:
		if (!md->public.config) {
			return 0;
		}
		if (length != ((unsigned)md->public.config->ram * 1024)) {
			LOG_WARN("DRAGON/DESERIALISE: RAM size mismatch\n");
			return 0;
		}
		ser_read(sh, md->ram, length);
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool dragon_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_dragon *md = sptr;
	switch (tag) {
	case DRAGON_SER_RAM:
		ser_write(sh, tag, md->ram, md->ram_size);
		break;
	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon_connect_cart(struct part *p) {
	struct machine_dragon *md = (struct machine_dragon *)p;
	struct cart *c = (struct cart *)part_component_by_id_is_a(p, "cart", "dragon-cart");
	md->cart = c;
	if (!c)
		return;
	assert(c->read != NULL);
	assert(c->write != NULL);
	c->signal_firq = DELEGATE_AS1(void, bool, cart_firq, md);
	c->signal_nmi = DELEGATE_AS1(void, bool, cart_nmi, md);
	c->signal_halt = DELEGATE_AS1(void, bool, cart_halt, md);
}

static void dragon_insert_cart(struct machine *m, struct cart *c) {
	dragon_remove_cart(m);
	part_add_component(&m->part, &c->part, "cart");
	dragon_connect_cart(&m->part);
}

static void dragon_remove_cart(struct machine *m) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	part_free((struct part *)md->cart);
	md->cart = NULL;
}

static void dragon_reset(struct machine *m, _Bool hard) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	xroar_set_keyboard_type(1, m->keyboard.type);
	if (hard) {
		// Initial RAM pattern is approximately what I see on my Dragon
		// 64, though it can probably vary based on manufacturer.  It
		// actually does matter that we set it to something
		// non-uniform, else Wildcatting won't work on the CoCo.
		unsigned loc = 0, val = 0xff;
		while (loc <= 0xfffc) {
			md->ram[loc++] = val;
			md->ram[loc++] = val;
			md->ram[loc++] = val;
			md->ram[loc++] = val;
			if ((loc & 0xff) != 0)
				val ^= 0xff;
		}
	}
	mc6821_reset(md->PIA0);
	mc6821_reset(md->PIA1);
	if (md->cart && md->cart->reset) {
		md->cart->reset(md->cart, hard);
	}
	sam_reset(md->SAM);
	md->CPU->reset(md->CPU);
	mc6847_reset(md->VDG);
	tape_reset(md->tape_interface);
	printer_reset(md->printer_interface);
}

static enum machine_run_state dragon_run(struct machine *m, int ncycles) {
	struct machine_dragon *md = (struct machine_dragon *)m;

#ifdef WANT_GDB_TARGET
	if (md->gdb_interface) {
		switch (gdb_run_lock(md->gdb_interface)) {
		case gdb_run_state_stopped:
			return machine_run_state_stopped;
		case gdb_run_state_running:
			md->stop_signal = 0;
			md->cycles += ncycles;
			md->CPU->running = 1;
			md->CPU->run(md->CPU);
			if (md->stop_signal != 0) {
				gdb_stop(md->gdb_interface, md->stop_signal);
			}
			break;
		case gdb_run_state_single_step:
			m->single_step(m);
			gdb_single_step(md->gdb_interface);
			break;
		default:
			break;
		}
		gdb_run_unlock(md->gdb_interface);
		return machine_run_state_ok;
	} else {
#endif
		md->cycles += ncycles;
		md->CPU->running = 1;
		md->CPU->run(md->CPU);
		return machine_run_state_ok;
#ifdef WANT_GDB_TARGET
	}
#endif
}

static void dragon_single_step(struct machine *m) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	md->single_step = 1;
	md->CPU->running = 0;
	md->CPU->debug_cpu.instruction_posthook = DELEGATE_AS0(void, dragon_instruction_posthook, md);
	do {
		md->CPU->run(md->CPU);
	} while (md->single_step);
	md->CPU->debug_cpu.instruction_posthook.func = NULL;
	update_vdg_mode(md);
}

/*
 * Stop emulation and set stop_signal to reflect the reason.
 */

static void dragon_signal(struct machine *m, int sig) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	update_vdg_mode(md);
	md->stop_signal = sig;
	md->CPU->running = 0;
}

static void dragon_trap(void *sptr) {
	struct machine *m = sptr;
	dragon_signal(m, MACHINE_SIGTRAP);
}

static void dragon_bp_add_n(struct machine *m, struct machine_bp *list, int n, void *sptr) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	for (int i = 0; i < n; i++) {
		if ((list[i].add_cond & BP_MACHINE_ARCH) && xroar_machine_config->architecture != list[i].cond_machine_arch)
			continue;
		if ((list[i].add_cond & BP_CRC_COMBINED) && (!md->has_combined || !crclist_match(list[i].cond_crc_combined, md->crc_combined)))
			continue;
		if ((list[i].add_cond & BP_CRC_EXT) && (!md->has_extbas || !crclist_match(list[i].cond_crc_extbas, md->crc_extbas)))
			continue;
		if ((list[i].add_cond & BP_CRC_BAS) && (!md->has_bas || !crclist_match(list[i].cond_crc_bas, md->crc_bas)))
			continue;
		list[i].bp.handler.sptr = sptr;
		bp_add(md->bp_session, &list[i].bp);
	}
}

static void dragon_bp_remove_n(struct machine *m, struct machine_bp *list, int n) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	for (int i = 0; i < n; i++) {
		bp_remove(md->bp_session, &list[i].bp);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int dragon_set_keyboard_type(struct machine *m, int action) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	int type = m->keyboard.type;
	switch (action) {
	case XROAR_QUERY:
		break;
	case XROAR_NEXT:
		if (type == m->config->keymap) {
			switch (m->config->keymap) {
			case dkbd_layout_dragon:
			case dkbd_layout_dragon200e:
				type = dkbd_layout_coco;
				break;
			default:
				type = dkbd_layout_dragon;
				break;
			}
		} else {
			type = m->config->keymap;
		}
		break;
	case XROAR_AUTO:
		type = m->config->keymap;
		break;
	default:
		type = action;
		break;
	}
	m->keyboard.type = type;
	keyboard_set_keymap(md->keyboard.interface, type);
	return type;
}

static _Bool dragon_set_pause(struct machine *m, int state) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	switch (state) {
	case 0: case 1:
		md->CPU->halt = state;
		break;
	case XROAR_NEXT:
		md->CPU->halt = !md->CPU->halt;
		break;
	default:
		break;
	}
	return md->CPU->halt;
}

static _Bool dragon_set_inverted_text(struct machine *m, int action) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	switch (action) {
	case 0: case 1:
		md->inverted_text = action;
		break;
	case -2:
		md->inverted_text = !md->inverted_text;
		break;
	default:
		break;
	}
	mc6847_set_inverted_text(md->VDG, md->inverted_text);
	return md->inverted_text;
}

/*
 * Device inspection.
 */

/* Note, this is SLOW.  Could be sped up by maintaining a hash by component
 * name, but will only ever be used outside critical path, so don't bother for
 * now. */

static void *dragon_get_component(struct machine *m, const char *cname) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	if (0 == strcmp(cname, "RAM0")) {
		return &md->ram0;
	} else if (0 == strcmp(cname, "RAM1")) {
		return &md->ram1;
	}
	return NULL;
}

/* Similarly SLOW.  Used to populate UI. */

static void *dragon_get_interface(struct machine *m, const char *ifname) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	if (0 == strcmp(ifname, "cart")) {
		return md->cart;
	} else if (0 == strcmp(ifname, "keyboard")) {
		return md->keyboard.interface;
	} else if (0 == strcmp(ifname, "printer")) {
		return md->printer_interface;
	} else if (0 == strcmp(ifname, "tape-update-audio")) {
		return update_audio_from_tape;
	}
	return NULL;
}

static void dragon_set_frameskip(struct machine *m, unsigned fskip) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	md->frameskip = fskip;
}

static void dragon_set_ratelimit(struct machine *m, _Bool ratelimit) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	sound_set_ratelimit(md->snd, ratelimit);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used when single-stepping.

static void dragon_instruction_posthook(void *sptr) {
	struct machine_dragon *md = sptr;
	md->single_step = 0;
}

static uint16_t decode_Z(struct machine_dragon *md, unsigned Z) {
	switch (md->ram_organisation) {
	case RAM_ORGANISATION_4K:
		return (Z & 0x3f) | ((Z & 0x3f00) >> 2) | ((~Z & 0x8000) >> 3);
	case RAM_ORGANISATION_16K:
		return (Z & 0x7f) | ((Z & 0x7f00) >> 1) | ((~Z & 0x8000) >> 1);
	case RAM_ORGANISATION_64K: default:
		return Z & md->ram_mask;
	}
}

static void read_byte(struct machine_dragon *md, unsigned A) {
	// Thanks to CrAlt on #coco_chat for verifying that RAM accesses
	// produce a different "null" result on his 16K CoCo
	if (md->SAM->RAS)
		md->CPU->D = 0xff;
	if (md->cart) {
		md->CPU->D = md->cart->read(md->cart, A, 0, 0, md->CPU->D);
		if (md->cart->EXTMEM) {
			return;
		}
	}
	switch (md->SAM->S) {
	case 0:
		if (md->SAM->RAS) {
			unsigned Z = decode_Z(md, md->SAM->Z);
			if (Z < md->ram_size)
				md->CPU->D = md->ram[Z];
		}
		break;
	case 1:
	case 2:
		md->CPU->D = md->rom[A & 0x3fff];
		break;
	case 3:
		if (md->cart)
			md->CPU->D = md->cart->read(md->cart, A & 0x3fff, 0, 1, md->CPU->D);
		break;
	case 4:
		if (md->relaxed_pia_decode) {
			md->CPU->D = mc6821_read(md->PIA0, A);
		} else {
			if ((A & 4) == 0) {
				md->CPU->D = mc6821_read(md->PIA0, A);
			} else {
				if (md->have_acia) {
					/* XXX Dummy ACIA reads */
					switch (A & 3) {
					default:
					case 0:  /* Receive Data */
					case 3:  /* Control */
						md->CPU->D = 0x00;
						break;
					case 2:  /* Command */
						md->CPU->D = 0x02;
						break;
					case 1:  /* Status */
						md->CPU->D = 0x10;
						break;
					}
				}
			}
		}
		break;
	case 5:
		if (md->relaxed_pia_decode || (A & 4) == 0) {
			md->CPU->D = mc6821_read(md->PIA1, A);
		}
		break;
	case 6:
		if (md->cart)
			md->CPU->D = md->cart->read(md->cart, A, 1, 0, md->CPU->D);
		break;
	default:
		break;
	}
}

static void write_byte(struct machine_dragon *md, unsigned A) {
	if (md->cart) {
		md->CPU->D = md->cart->write(md->cart, A, 0, 0, md->CPU->D);
	}
	if ((!md->cart || !md->cart->EXTMEM) && ((md->SAM->S & 4) || md->unexpanded_dragon32)) {
		switch (md->SAM->S) {
		case 1:
		case 2:
			md->CPU->D = md->rom[A & 0x3fff];
			break;
		case 3:
			if (md->cart)
				md->CPU->D = md->cart->write(md->cart, A & 0x3fff, 0, 1, md->CPU->D);
			break;
		case 4:
			if (!md->is_dragon || md->unexpanded_dragon32) {
				mc6821_write(md->PIA0, A, md->CPU->D);
			} else {
				if ((A & 4) == 0) {
					mc6821_write(md->PIA0, A, md->CPU->D);
				}
			}
			break;
		case 5:
			if (md->relaxed_pia_decode || (A & 4) == 0) {
				mc6821_write(md->PIA1, A, md->CPU->D);
			}
			break;
		case 6:
			if (md->cart)
				md->CPU->D = md->cart->write(md->cart, A, 1, 0, md->CPU->D);
			break;
		default:
			break;
		}
	}
	if (md->SAM->RAS) {
		unsigned Z = decode_Z(md, md->SAM->Z);
		md->ram[Z] = md->CPU->D;
	}
}

static void cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_dragon *md = sptr;
	// Changing the SAM VDG mode can affect its idea of the current VRAM
	// address, so get the VDG output up to date:
	if (!RnW && A >= 0xffc0 && A < 0xffc6) {
		update_vdg_mode(md);
	}
	md->cycles -= ncycles;
	if (md->cycles <= 0) md->CPU->running = 0;
	event_current_tick += ncycles;
	event_run_queue(&MACHINE_EVENT_LIST);
	MC6809_IRQ_SET(md->CPU, md->PIA0->a.irq || md->PIA0->b.irq);
	MC6809_FIRQ_SET(md->CPU, md->PIA1->a.irq || md->PIA1->b.irq);

	if (RnW) {
		read_byte(md, A);
		bp_wp_read_hook(md->bp_session, A);
	} else {
		write_byte(md, A);
		bp_wp_write_hook(md->bp_session, A);
	}
}

static void cpu_cycle_noclock(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_dragon *md = sptr;
	(void)ncycles;
	if (RnW) {
		read_byte(md, A);
	} else {
		write_byte(md, A);
	}
}

static void vdg_fetch_handler(void *sptr, uint16_t A, int nbytes, uint16_t *dest) {
	(void)A;
	struct machine_dragon *md = sptr;
	uint16_t attr = (PIA_VALUE_B(md->PIA1) & 0x10) << 6;  // GM0 -> ¬INT/EXT
	while (nbytes > 0) {
		int n = sam_vdg_bytes(md->SAM, nbytes);
		if (dest) {
			uint16_t V = decode_Z(md, md->SAM->V);
			for (int i = n; i; i--) {
				uint16_t D = md->ram[V++] | attr;
				D |= (D & 0xc0) << 2;  // D7,D6 -> ¬A/S,INV
				*(dest++) = D;
			}
		}
		nbytes -= n;
	}
}

// Used in the Dragon 200-E, this may contain logic that is not common to all
// chargen modules (e.g. as provided for the CoCo). As I don't have schematics
// for any of the others, those will have to wait!

static void vdg_fetch_handler_chargen(void *sptr, uint16_t A, int nbytes, uint16_t *dest) {
	(void)A;
	struct machine_dragon *md = sptr;
	unsigned pia_vdg_mode = PIA_VALUE_B(md->PIA1);
	_Bool GnA = pia_vdg_mode & 0x80;
	_Bool EnI = pia_vdg_mode & 0x10;
	uint16_t Aram7 = EnI ? 0x80 : 0;
	while (nbytes > 0) {
		int n = sam_vdg_bytes(md->SAM, nbytes);
		if (dest) {
			uint16_t V = decode_Z(md, md->SAM->V);
			for (int i = n; i; i--) {
				uint16_t Dram = md->ram[V++];
				_Bool SnA = Dram & 0x80;
				uint16_t D;
				if (!GnA && !SnA) {
					unsigned Aext = (md->VDG->row << 8) | Aram7 | Dram;
					D = md->ext_charset[Aext&0xfff] | 0x100;  // set INV
					D |= (~Dram & 0x80) << 3;
				} else {
					D = Dram;
				}
				D |= (Dram & 0x80) << 2;  // D7 -> ¬A/S
				*(dest++) = D;
			}
		}
		nbytes -= n;
	}
}

/* Read a byte without advancing clock.  Used for debugging & breakpoints. */

static uint8_t dragon_read_byte(struct machine *m, unsigned A, uint8_t D) {
	(void)D;
	struct machine_dragon *md = (struct machine_dragon *)m;
	md->SAM->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, md);
	sam_mem_cycle(md->SAM, 1, A);
	md->SAM->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, md);
	return md->CPU->D;
}

/* Write a byte without advancing clock.  Used for debugging & breakpoints. */

static void dragon_write_byte(struct machine *m, unsigned A, uint8_t D) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	md->CPU->D = D;
	md->SAM->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, md);
	sam_mem_cycle(md->SAM, 0, A);
	md->SAM->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, md);
}

/* simulate an RTS without otherwise affecting machine state */
static void dragon_op_rts(struct machine *m) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	unsigned int new_pc = m->read_byte(m, md->CPU->reg_s, 0) << 8;
	new_pc |= m->read_byte(m, md->CPU->reg_s + 1, 0);
	md->CPU->reg_s += 2;
	md->CPU->reg_pc = new_pc;
}

static void dragon_dump_ram(struct machine *m, FILE *fd) {
	struct machine_dragon *md = (struct machine_dragon *)m;
	fwrite(md->ram, md->ram_size, 1, fd);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void keyboard_update(void *sptr) {
	struct machine_dragon *md = sptr;
	unsigned buttons = ~(joystick_read_buttons() & 3);
	struct keyboard_state state = {
		.row_source = md->PIA0->a.out_sink,
		.row_sink = md->PIA0->a.out_sink & buttons,
		.col_source = md->PIA0->b.out_source,
		.col_sink = md->PIA0->b.out_sink,
	};
	keyboard_read_matrix(md->keyboard.interface, &state);
	md->PIA0->a.in_sink = state.row_sink;
	md->PIA0->b.in_source = state.col_source;
	md->PIA0->b.in_sink = state.col_sink;
}

static void joystick_update(void *sptr) {
	struct machine_dragon *md = sptr;
	int port = (md->PIA0->b.control_register & 0x08) >> 3;
	int axis = (md->PIA0->a.control_register & 0x08) >> 3;
	int dac_value = (md->PIA1->a.out_sink & 0xfc) << 8;
	int js_value = joystick_read_axis(port, axis);
	if (js_value >= dac_value)
		md->PIA0->a.in_sink |= 0x80;
	else
		md->PIA0->a.in_sink &= 0x7f;
}

static void update_sound_mux_source(void *sptr) {
	struct machine_dragon *md = sptr;
	unsigned source = ((md->PIA0->b.control_register & (1<<3)) >> 2)
	                  | ((md->PIA0->a.control_register & (1<<3)) >> 3);
	sound_set_mux_source(md->snd, source);
}

static void update_vdg_mode(struct machine_dragon *md) {
	unsigned vmode = (md->PIA1->b.out_source & md->PIA1->b.out_sink) & 0xf8;
	// ¬INT/EXT = GM0
	vmode |= (vmode & 0x10) << 4;
	mc6847_set_mode(md->VDG, vmode);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void pia0a_data_preread(void *sptr) {
	keyboard_update(sptr);
	joystick_update(sptr);
}

static void pia0b_data_preread_coco64k(void *sptr) {
	struct machine_dragon *md = sptr;
	keyboard_update(md);
	// PIA0 PB6 is linked to PIA1 PB2 on 64K CoCos
	if ((md->PIA1->b.out_source & md->PIA1->b.out_sink) & (1<<2)) {
		md->PIA0->b.in_source |= (1<<6);
		md->PIA0->b.in_sink |= (1<<6);
	} else {
		md->PIA0->b.in_source &= ~(1<<6);
		md->PIA0->b.in_sink &= ~(1<<6);
	}
}

static void pia1a_data_postwrite(void *sptr) {
	struct machine_dragon *md = sptr;
	sound_set_dac_level(md->snd, (float)(PIA_VALUE_A(md->PIA1) & 0xfc) / 252.);
	tape_update_output(md->tape_interface, md->PIA1->a.out_sink & 0xfc);
	if (md->is_dragon) {
		keyboard_update(md);
		printer_strobe(md->printer_interface, PIA_VALUE_A(md->PIA1) & 0x02, PIA_VALUE_B(md->PIA0));
	}
}

static void pia1a_control_postwrite(void *sptr) {
	struct machine_dragon *md = sptr;
	tape_set_motor(md->tape_interface, md->PIA1->a.control_register & 0x08);
	tape_update_output(md->tape_interface, md->PIA1->a.out_sink & 0xfc);
}

static void pia1b_data_preread_dragon(void *sptr) {
	struct machine_dragon *md = sptr;
	if (printer_busy(md->printer_interface))
		md->PIA1->b.in_sink |= 0x01;
	else
		md->PIA1->b.in_sink &= ~0x01;
}

static void pia1b_data_preread_coco64k(void *sptr) {
	struct machine_dragon *md = sptr;
	// PIA0 PB6 is linked to PIA1 PB2 on 64K CoCos
	if ((md->PIA0->b.out_source & md->PIA0->b.out_sink) & (1<<6)) {
		md->PIA1->b.in_source |= (1<<2);
		md->PIA1->b.in_sink |= (1<<2);
	} else {
		md->PIA1->b.in_source &= ~(1<<2);
		md->PIA1->b.in_sink &= ~(1<<2);
	}
}

static void pia1b_data_postwrite(void *sptr) {
	struct machine_dragon *md = sptr;
	if (md->is_dragon64) {
		_Bool is_32k = PIA_VALUE_B(md->PIA1) & 0x04;
		if (is_32k) {
			md->rom = md->rom0;
			keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_dragon_32k_basic);
		} else {
			md->rom = md->rom1;
			keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_dragon_64k_basic);
		}
	}
	// Single-bit sound
	_Bool sbs_enabled = !((md->PIA1->b.out_source ^ md->PIA1->b.out_sink) & (1<<1));
	_Bool sbs_level = md->PIA1->b.out_source & md->PIA1->b.out_sink & (1<<1);
	sound_set_sbs(md->snd, sbs_enabled, sbs_level);
	// VDG mode
	update_vdg_mode(md);
}

static void pia1b_control_postwrite(void *sptr) {
	struct machine_dragon *md = sptr;
	sound_set_mux_enabled(md->snd, md->PIA1->b.control_register & 0x08);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* VDG edge delegates */

static void vdg_hs(void *sptr, _Bool level) {
	struct machine_dragon *md = sptr;
	mc6821_set_cx1(&md->PIA0->a, level);
	sam_vdg_hsync(md->SAM, level);
	if (!level) {
		unsigned p1bval = md->PIA1->b.out_source & md->PIA1->b.out_sink;
		_Bool GM0 = p1bval & 0x10;
		_Bool CSS = p1bval & 0x08;
		md->ntsc_burst_mod = (md->use_ntsc_burst_mod && GM0 && CSS) ? 2 : 0;
	}
}

// PAL CoCos invert HS
static void vdg_hs_pal_coco(void *sptr, _Bool level) {
	struct machine_dragon *md = sptr;
	mc6821_set_cx1(&md->PIA0->a, !level);
	sam_vdg_hsync(md->SAM, level);
	// PAL uses palletised output so this wouldn't technically matter, but
	// user is able to cycle to a faux-NTSC colourscheme, so update phase
	// here as in NTSC code:
	if (level) {
		unsigned p1bval = md->PIA1->b.out_source & md->PIA1->b.out_sink;
		_Bool GM0 = p1bval & 0x10;
		_Bool CSS = p1bval & 0x08;
		md->ntsc_burst_mod = (md->use_ntsc_burst_mod && GM0 && CSS) ? 2 : 0;
	}
}

static void vdg_fs(void *sptr, _Bool level) {
	struct machine_dragon *md = sptr;
	mc6821_set_cx1(&md->PIA0->b, level);
	sam_vdg_fsync(md->SAM, level);
	if (level) {
		sound_update(md->snd);
		md->frame--;
		if (md->frame < 0)
			md->frame = md->frameskip;
		if (md->frame == 0) {
			DELEGATE_CALL(md->vo->vsync);
		}
	}
}

static void vdg_render_line(void *sptr, uint8_t *data, unsigned burst) {
	struct machine_dragon *md = sptr;
	burst = (burst | md->ntsc_burst_mod) & 3;
	struct ntsc_burst *nb = md->ntsc_burst[burst];
	DELEGATE_CALL(md->vo->render_scanline, data, nb);
}

/* Dragon parallel printer line delegate. */

//ACK is active low
static void printer_ack(void *sptr, _Bool ack) {
	struct machine_dragon *md = sptr;
	mc6821_set_cx1(&md->PIA1->a, !ack);
}

/* Sound output can feed back into the single bit sound pin when it's
 * configured as an input. */

static void single_bit_feedback(void *sptr, _Bool level) {
	struct machine_dragon *md = sptr;
	if (level) {
		md->PIA1->b.in_source &= ~(1<<1);
		md->PIA1->b.in_sink &= ~(1<<1);
	} else {
		md->PIA1->b.in_source |= (1<<1);
		md->PIA1->b.in_sink |= (1<<1);
	}
}

/* Tape audio delegate */

static void update_audio_from_tape(void *sptr, float value) {
	struct machine_dragon *md = sptr;
	sound_set_tape_level(md->snd, value);
	if (value >= 0.5)
		md->PIA1->a.in_sink &= ~(1<<0);
	else
		md->PIA1->a.in_sink |= (1<<0);
}

/* Catridge signalling */

static void cart_firq(void *sptr, _Bool level) {
	struct machine_dragon *md = sptr;
	mc6821_set_cx1(&md->PIA1->b, level);
}

static void cart_nmi(void *sptr, _Bool level) {
	struct machine_dragon *md = sptr;
	MC6809_NMI_SET(md->CPU, level);
}

static void cart_halt(void *sptr, _Bool level) {
	struct machine_dragon *md = sptr;
	MC6809_HALT_SET(md->CPU, level);
}
