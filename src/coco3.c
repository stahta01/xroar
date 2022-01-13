/** \file
 *
 *  \brief Tandy Colour Computer 3 machine.
 *
 *  \copyright Copyright 2003-2022 Ciaran Anscomb
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
 *  Tandy CoCo 3 support is decent enough, but still has some noticeable issues
 *  with respect to the timer.
 */

#include "top-config.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "array.h"
#include "delegate.h"
#include "sds.h"
#include "xalloc.h"

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
#include "ntsc.h"
#include "part.h"
#include "printer.h"
#include "romlist.h"
#include "serialise.h"
#include "sound.h"
#include "tape.h"
#include "tcc1014/tcc1014.h"
#include "vo.h"
#include "xroar.h"

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

static float hue_intensity_map[4] = { 0.30, 0.50, 0.80, 1.0 };
static float grey_intensity_map[4] = { 0.03, 0.23, 0.5, 1.0 };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_coco3 {
	struct machine public;

	struct MC6809 *CPU;
	struct TCC1014 *GIME;
	struct MC6821 *PIA0, *PIA1;

	struct vo_interface *vo;
	int frame;  // track frameskip
	struct sound_interface *snd;

	unsigned ram_size;
	unsigned ram_mask;
	uint8_t *ram;
	uint8_t rom0[0x8000];

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

	// NTSC colour bursts.  The GIME can choose to invert the phase, so we
	// maintain one normal, one 180° shifted.
	struct ntsc_burst *ntsc_burst[2];

	struct tape_interface *tape_interface;
	struct printer_interface *printer_interface;

	struct {
		struct keyboard_interface *interface;
	} keyboard;

	// Optional DAT board provides extra translation for up to 2M of RAM.
	struct {
		_Bool enabled;
		_Bool readable;
		_Bool MMUEN;
		_Bool MC3;
		unsigned task;
		uint32_t mask;
		uint32_t mmu_bank[16];
		uint32_t vram_bank;
	} dat;

	// Useful configuration side-effect tracking
	_Bool has_secb;
	uint32_t crc_secb;
};

static const struct ser_struct ser_struct_coco3[] = {
	SER_STRUCT_NEST(&machine_ser_struct_data), // 1
	SER_STRUCT_ELEM(struct machine_coco3, ram, ser_type_unhandled), // 2
	SER_STRUCT_ELEM(struct machine_coco3, ram_size, ser_type_unsigned), // 3
	SER_STRUCT_ELEM(struct machine_coco3, ram_mask, ser_type_unsigned), // 4
	SER_STRUCT_ELEM(struct machine_coco3, inverted_text, ser_type_bool), // 5
	SER_STRUCT_ELEM(struct machine_coco3, dat.enabled, ser_type_bool), // 6
	SER_STRUCT_ELEM(struct machine_coco3, dat.readable, ser_type_bool), // 7
	SER_STRUCT_ELEM(struct machine_coco3, dat.MMUEN, ser_type_bool), // 8
	SER_STRUCT_ELEM(struct machine_coco3, dat.MC3, ser_type_bool), // 9
	SER_STRUCT_ELEM(struct machine_coco3, dat.task, ser_type_unsigned), // 10
	SER_STRUCT_ELEM(struct machine_coco3, dat.mmu_bank, ser_type_unhandled), // 11
	SER_STRUCT_ELEM(struct machine_coco3, dat.vram_bank, ser_type_uint32), // 12
};

#define COCO3_SER_RAM          (2)
#define COCO3_SER_DAT_MMU_BANK (11)

static _Bool coco3_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool coco3_write_elem(void *sptr, struct ser_handle *sh, int tag);

const struct ser_struct_data coco3_ser_struct_data = {
	.elems = ser_struct_coco3,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_coco3),
	.read_elem = coco3_read_elem,
	.write_elem = coco3_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco3_config_complete(struct machine_config *mc) {
	if (!mc->description) {
		mc->description = xstrdup(mc->name);
	}
	if (mc->tv_standard == ANY_AUTO)
		mc->tv_standard = TV_PAL;
	if (mc->tv_input == ANY_AUTO) {
		switch (mc->tv_standard) {
		default:
		case TV_PAL:
			mc->tv_input = TV_INPUT_RGB;
			break;
		case TV_NTSC:
		case TV_PAL_M:
			mc->tv_input = TV_INPUT_CMP_KBRW;
			break;
		}
	}
	if (mc->vdg_type == ANY_AUTO)
		mc->vdg_type = VDG_GIME_1986;
	if (mc->vdg_type != VDG_GIME_1986 && mc->vdg_type != VDG_GIME_1987)
		mc->vdg_type = VDG_GIME_1986;
	if (mc->ram != 128 && mc->ram != 512 && mc->ram != 1024 && mc->ram != 2048)
		mc->ram = 128;
	mc->keymap = dkbd_layout_coco3;
	/* Now find which ROMs we're actually going to use */
	if (!mc->extbas_dfn && !mc->extbas_rom) {
		mc->extbas_rom = xstrdup("@coco3");
	}
	// Determine a default DOS cartridge if necessary
	if (!mc->default_cart_dfn && !mc->default_cart) {
		struct cart_config *cc = cart_find_working_dos(mc);
		if (cc)
			mc->default_cart = xstrdup(cc->name);
	}
}

static _Bool coco3_is_working_config(struct machine_config *mc) {
	if (!mc)
		return 0;
	sds tmp;
	if (mc->extbas_rom) {
		tmp = romlist_find(mc->extbas_rom);
		if (!tmp)
			return 0;
		sdsfree(tmp);
	} else {
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco3_connect_cart(struct part *p);
static void coco3_insert_cart(struct machine *m, struct cart *c);
static void coco3_remove_cart(struct machine *m);

static void coco3_reset(struct machine *m, _Bool hard);
static enum machine_run_state coco3_run(struct machine *m, int ncycles);
static void coco3_single_step(struct machine *m);
static void coco3_signal(struct machine *m, int sig);
static void coco3_trap(void *sptr);
static void coco3_bp_add_n(struct machine *m, struct machine_bp *list, int n, void *sptr);
static void coco3_bp_remove_n(struct machine *m, struct machine_bp *list, int n);

static int coco3_set_keyboard_type(struct machine *m, int action);
static _Bool coco3_set_pause(struct machine *m, int state);
static _Bool coco3_set_inverted_text(struct machine *m, int state);
static void *coco3_get_interface(struct machine *m, const char *ifname);
static void coco3_set_frameskip(struct machine *m, unsigned fskip);
static void coco3_set_ratelimit(struct machine *m, _Bool ratelimit);

static uint8_t coco3_read_byte(struct machine *m, unsigned A, uint8_t D);
static void coco3_write_byte(struct machine *m, unsigned A, uint8_t D);
static void coco3_op_rts(struct machine *m);
static void coco3_dump_ram(struct machine *m, FILE *fd);

static void keyboard_update(void *sptr);
static void joystick_update(void *sptr);
static void update_sound_mux_source(void *sptr);

static void single_bit_feedback(void *sptr, _Bool level);
static void update_audio_from_tape(void *sptr, float value);
static void cart_firq(void *sptr, _Bool level);
static void cart_nmi(void *sptr, _Bool level);
static void cart_halt(void *sptr, _Bool level);
static void gime_hs(void *sptr, _Bool level);
// static void gime_hs_pal_coco(void *sptr, _Bool level);
static void gime_fs(void *sptr, _Bool level);
static void gime_render_line(void *sptr, const uint8_t *data, _Bool phase_invert);

static void cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A);
static void cpu_cycle_noclock(void *sptr, int ncycles, _Bool RnW, uint16_t A);
static void coco3_instruction_posthook(void *sptr);
static uint8_t fetch_vram(void *sptr, uint32_t A);

static void pia0a_data_preread(void *sptr);
#define pia0a_data_postwrite NULL
#define pia0a_control_postwrite update_sound_mux_source
#define pia0b_data_postwrite NULL
#define pia0b_control_postwrite update_sound_mux_source
#define pia0b_data_preread keyboard_update

#define pia1a_data_preread NULL
static void pia1a_data_postwrite(void *sptr);
static void pia1a_control_postwrite(void *sptr);
#define pia1b_data_preread NULL
static void pia1b_data_postwrite(void *sptr);
static void pia1b_control_postwrite(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// CoCo 3 part creation

static struct part *coco3_allocate(void);
static void coco3_initialise(struct part *p, void *options);
static _Bool coco3_finish(struct part *p);
static void coco3_free(struct part *p);

static const struct partdb_entry_funcs coco3_funcs = {
	.allocate = coco3_allocate,
	.initialise = coco3_initialise,
	.finish = coco3_finish,
	.free = coco3_free,

	.ser_struct_data = &coco3_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_extra coco3_machine_extra = {
	.config_complete = coco3_config_complete,
	.is_working_config = coco3_is_working_config,
	.cart_arch = "dragon-cart",
};

const struct partdb_entry coco3_part = { .name = "coco3", .funcs = &coco3_funcs, .extra = { &coco3_machine_extra } };

static struct part *coco3_allocate(void) {
        struct machine_coco3 *mcc3 = part_new(sizeof(*mcc3));
        struct machine *m = &mcc3->public;
        struct part *p = &m->part;

	*mcc3 = (struct machine_coco3){0};

	m->insert_cart = coco3_insert_cart;
	m->remove_cart = coco3_remove_cart;
	m->reset = coco3_reset;
	m->run = coco3_run;
	m->single_step = coco3_single_step;
	m->signal = coco3_signal;
	m->bp_add_n = coco3_bp_add_n;
	m->bp_remove_n = coco3_bp_remove_n;

	m->set_keyboard_type = coco3_set_keyboard_type;
	m->set_pause = coco3_set_pause;
	m->set_inverted_text = coco3_set_inverted_text;
	m->get_interface = coco3_get_interface;
	m->set_frameskip = coco3_set_frameskip;
	m->set_ratelimit = coco3_set_ratelimit;

	m->read_byte = coco3_read_byte;
	m->write_byte = coco3_write_byte;
	m->op_rts = coco3_op_rts;
	m->dump_ram = coco3_dump_ram;

	m->keyboard.type = dkbd_layout_coco3;

	return p;
}

static void coco3_initialise(struct part *p, void *options) {
        struct machine_config *mc = options;
        assert(mc != NULL);

        struct machine_coco3 *mcc3 = (struct machine_coco3 *)p;
        struct machine *m = &mcc3->public;

        coco3_config_complete(mc);
        m->config = mc;

	// GIME
	part_add_component(&m->part, part_create((mc->vdg_type == VDG_GIME_1986) ? "TCC1014-1986" : "TCC1014-1987", NULL), "GIME");

	// CPU
	part_add_component(&m->part, part_create((mc->cpu == CPU_HD6309) ? "HD6309" : "MC6809", NULL), "CPU");

	// PIAs
	part_add_component(&m->part, part_create("MC6821", NULL), "PIA0");
	part_add_component(&m->part, part_create("MC6821", NULL), "PIA1");

	// Keyboard
	m->keyboard.type = mc->keymap;
}

static _Bool coco3_finish(struct part *p) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)p;
	struct machine *m = &mcc3->public;
	struct machine_config *mc = m->config;

	// Interfaces
	mcc3->vo = xroar_vo_interface;
	mcc3->snd = xroar_ao_interface->sound_interface;
	mcc3->tape_interface = xroar_tape_interface;

	mcc3->tape_interface->default_paused = 0;

	// Find attached parts
	mcc3->GIME = (struct TCC1014 *)part_component_by_id_is_a(p, "GIME", "TCC1014");
	mcc3->CPU = (struct MC6809 *)part_component_by_id_is_a(p, "CPU", "MC6809");
	mcc3->PIA0 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA0", "MC6821");
	mcc3->PIA1 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA1", "MC6821");

	// Check all required parts are attached
	if (!mcc3->GIME || !mcc3->CPU || !mcc3->PIA0 || !mcc3->PIA1 ||
	    !mcc3->vo || !mcc3->snd || !mcc3->tape_interface) {
		return 0;
	}

	// Connect any cartridge part
	coco3_connect_cart(p);

	// GIME

	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
	mcc3->GIME->fetch_vram = DELEGATE_AS1(uint8, uint32, fetch_vram, mcc3);

	for (int j = 0; j < 64; j++) {
		int intensity = (j >> 4) & 3;
		int phase = j & 15;
		double y, b_y, r_y;
		if (phase == 0 || j == 63) {
			y = (grey_intensity_map[intensity] * .6860) + .1715;
			b_y = 0.0;
			r_y = 0.0;
		} else {
			double hue = (2.0 * M_PI * (double)(phase+7.5)) / 15.0;
			y = (hue_intensity_map[intensity] * .6860) + .1715;
			b_y = 0.5 * sin(hue);
			r_y = 0.5 * cos(hue);
		}
		DELEGATE_CALL(mcc3->vo->palette_set_ybr, j, y, b_y, r_y);
	}

	for (int j = 0; j < 64; j++) {
		float r = hue_intensity_map[((j>>4)&2)|((j>>2)&1)];
		float g = hue_intensity_map[((j>>3)&2)|((j>>1)&1)];
		float b = hue_intensity_map[((j>>2)&2)|((j>>0)&1)];
		DELEGATE_CALL(mcc3->vo->palette_set_rgb, j, r, g, b);
	}

	mcc3->ntsc_burst[0] = ntsc_burst_new(0);    // Normal burst
	mcc3->ntsc_burst[1] = ntsc_burst_new(180);  // Phase inverted burst

	// CPU

	mcc3->CPU->mem_cycle = DELEGATE_AS2(void, bool, uint16, tcc1014_mem_cycle, mcc3->GIME);
	mcc3->GIME->CPUD = &mcc3->CPU->D;

	// Breakpoint session
	mcc3->bp_session = bp_session_new(m);
	assert(mcc3->bp_session != NULL);  // this shouldn't fail
	mcc3->bp_session->trap_handler = DELEGATE_AS0(void, coco3_trap, m);

	// PIAs

	mcc3->PIA0->a.data_preread = DELEGATE_AS0(void, pia0a_data_preread, mcc3);
	mcc3->PIA0->a.data_postwrite = DELEGATE_AS0(void, pia0a_data_postwrite, mcc3);
	mcc3->PIA0->a.control_postwrite = DELEGATE_AS0(void, pia0a_control_postwrite, mcc3);
	mcc3->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, mcc3);
	mcc3->PIA0->b.data_postwrite = DELEGATE_AS0(void, pia0b_data_postwrite, mcc3);
	mcc3->PIA0->b.control_postwrite = DELEGATE_AS0(void, pia0b_control_postwrite, mcc3);

	mcc3->PIA1->a.data_preread = DELEGATE_AS0(void, pia1a_data_preread, mcc3);
	mcc3->PIA1->a.data_postwrite = DELEGATE_AS0(void, pia1a_data_postwrite, mcc3);
	mcc3->PIA1->a.control_postwrite = DELEGATE_AS0(void, pia1a_control_postwrite, mcc3);
	mcc3->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread, mcc3);
	mcc3->PIA1->b.data_postwrite = DELEGATE_AS0(void, pia1b_data_postwrite, mcc3);
	mcc3->PIA1->b.control_postwrite = DELEGATE_AS0(void, pia1b_control_postwrite, mcc3);

	// Single-bit sound feedback
	mcc3->snd->sbs_feedback = DELEGATE_AS1(void, bool, single_bit_feedback, mcc3);

	// Tape
	mcc3->tape_interface->update_audio = DELEGATE_AS1(void, float, update_audio_from_tape, mcc3);

	mcc3->GIME->signal_hs = DELEGATE_AS1(void, bool, gime_hs, mcc3);
	mcc3->GIME->signal_fs = DELEGATE_AS1(void, bool, gime_fs, mcc3);
	mcc3->GIME->render_line = (tcc1014_render_line_func){gime_render_line, mcc3};
	tcc1014_set_inverted_text(mcc3->GIME, mcc3->inverted_text);

	// Load appropriate ROMs.  The CoCo 3 ROM is a single 32K image: Super
	// Extended Colour BASIC.  There are NTSC and PAL variants though.

	memset(mcc3->rom0, 0, sizeof(mcc3->rom0));

	mcc3->has_secb = 0;
	mcc3->crc_secb = 0;

	/* ... Super Extended BASIC */
	if (mc->extbas_rom) {
		sds tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, mcc3->rom0, sizeof(mcc3->rom0));
			if (size > 0) {
				mcc3->has_secb = 1;
			}
			sdsfree(tmp);
		}
	}

	switch (mc->ram) {
	case 2048:
		mcc3->ram_size = 2048 * 1024;
		mcc3->ram_mask = 0x7ffff;
		mcc3->dat.enabled = 1;
		mcc3->dat.mask = 0xc0 << 13;
		//mcc3->dat.readable = 1;  // XXX make configurable
		break;
	case 1024:
		mcc3->ram_size = 1024 * 1024;
		mcc3->ram_mask = 0x7ffff;
		mcc3->dat.enabled = 1;
		mcc3->dat.mask = 0x40 << 13;
		//mcc3->dat.readable = 1;  // XXX make configurable
		break;
	case 512:
		mcc3->ram_size = 512 * 1024;
		mcc3->ram_mask = 0x7ffff;
		break;
	default:
	case 128:
		mcc3->ram_size = 128 * 1024;
		mcc3->ram_mask = 0x1ffff;
		break;
	}
	if (!mcc3->ram) {
		mcc3->ram = xmalloc(mcc3->ram_size);
	}

	// Check CRCs

	if (mcc3->has_secb) {
		_Bool forced = 0, valid_crc = 0;

		mcc3->crc_secb = crc32_block(CRC32_RESET, mcc3->rom0, 0x8000);

		valid_crc = crclist_match("@coco3", mcc3->crc_secb);

		if (xroar_cfg.force_crc_match) {
			mcc3->crc_secb = 0xb4c88d6c;  // CoCo 3 Super Extended BASIC
			forced = 1;
		}

		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\tSuper Extended BASIC CRC = 0x%08x%s\n", mcc3->crc_secb, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for Super Extended BASIC ROM\n");
		}
	}

	// Default all PIA connections to unconnected (no source, no sink)
	mcc3->PIA0->b.in_source = 0;
	mcc3->PIA1->b.in_source = 0;
	mcc3->PIA0->a.in_sink = mcc3->PIA0->b.in_sink = 0xff;
	mcc3->PIA1->a.in_sink = mcc3->PIA1->b.in_sink = 0xff;

	// Until I implement serial, this appear to pull low by default
	mcc3->PIA1->b.in_sink &= ~(1<<0);

	// Keyboard interface
	mcc3->keyboard.interface = keyboard_interface_new(m);
	mcc3->keyboard.interface->update = DELEGATE_AS0(void, keyboard_update, mcc3);
	keyboard_set_chord_mode(mcc3->keyboard.interface, keyboard_chord_mode_coco_basic);
	keyboard_set_keymap(mcc3->keyboard.interface, m->keyboard.type);

	// Printer interface
	mcc3->printer_interface = printer_interface_new(m);

#ifdef WANT_GDB_TARGET
	// GDB
	if (xroar_cfg.gdb) {
		mcc3->gdb_interface = gdb_interface_new(xroar_cfg.gdb_ip, xroar_cfg.gdb_port, m, mcc3->bp_session);
	}
#endif

	// XXX until we serialise sound information
	update_sound_mux_source(mcc3);
	sound_set_mux_enabled(mcc3->snd, mcc3->PIA1->b.control_register & 0x08);

	return 1;
}

static void coco3_free(struct part *p) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)p;
#ifdef WANT_GDB_TARGET
	if (mcc3->gdb_interface) {
		gdb_interface_free(mcc3->gdb_interface);
	}
#endif
	if (mcc3->keyboard.interface) {
		keyboard_interface_free(mcc3->keyboard.interface);
	}
	if (mcc3->printer_interface) {
		printer_interface_free(mcc3->printer_interface);
	}
	if (mcc3->bp_session) {
		bp_session_free(mcc3->bp_session);
	}
	ntsc_burst_free(mcc3->ntsc_burst[1]);
	ntsc_burst_free(mcc3->ntsc_burst[0]);
	free(mcc3->ram);
}

static _Bool coco3_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_coco3 *mcc3 = sptr;
	size_t length = ser_data_length(sh);
	switch (tag) {
	case COCO3_SER_RAM:
		if (!mcc3->public.config) {
			return 0;
		}
		if (length != ((unsigned)mcc3->public.config->ram * 1024)) {
			LOG_WARN("COCO3/DESERIALISE: RAM size mismatch %zd != %d\n", length, mcc3->public.config->ram * 1024);
			return 0;
		}
		if (mcc3->ram) {
			free(mcc3->ram);
		}
		mcc3->ram = ser_read_new(sh, length);
		break;

	case COCO3_SER_DAT_MMU_BANK:
		for (int i = 0; i < 16; i++) {
			mcc3->dat.mmu_bank[i] = ser_read_uint8(sh) << 13;
		}
		break;

	default:
		return 0;
	}
	return 1;
}

static _Bool coco3_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_coco3 *mcc3 = sptr;
	switch (tag) {
	case COCO3_SER_RAM:
		ser_write(sh, tag, mcc3->ram, mcc3->ram_size);
		break;

	case COCO3_SER_DAT_MMU_BANK:
		ser_write_tag(sh, tag, 16);
		for (int i = 0; i < 16; i++) {
			ser_write_uint8_untagged(sh, mcc3->dat.mmu_bank[i] >> 13);
		}
		ser_write_close_tag(sh);
		break;

	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco3_connect_cart(struct part *p) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)p;
	struct cart *c = (struct cart *)part_component_by_id_is_a(p, "cart", "dragon-cart");
	mcc3->cart = c;
	if (!c)
		return;
	assert(c->read != NULL);
	assert(c->write != NULL);
	c->signal_firq = DELEGATE_AS1(void, bool, cart_firq, mcc3);
	c->signal_nmi = DELEGATE_AS1(void, bool, cart_nmi, mcc3);
	c->signal_halt = DELEGATE_AS1(void, bool, cart_halt, mcc3);
}

static void coco3_insert_cart(struct machine *m, struct cart *c) {
	coco3_remove_cart(m);
	part_add_component(&m->part, (struct part *)c, "cart");
	coco3_connect_cart(&m->part);
}

static void coco3_remove_cart(struct machine *m) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	part_free((struct part *)mcc3->cart);
	mcc3->cart = NULL;
}

static void coco3_reset(struct machine *m, _Bool hard) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	xroar_set_keyboard_type(1, m->keyboard.type);
	if (hard) {
		// This initial RAM state is carried over from Dragon/CoCo1/2
		// code, but might as well keep it.  As noted there, some
		// non-uniform state is required for Wildcatting to work.
		unsigned loc = 0, val = 0xff;
		while (loc < (mcc3->ram_size-3)) {
			mcc3->ram[loc++] = val;
			mcc3->ram[loc++] = val;
			mcc3->ram[loc++] = val;
			mcc3->ram[loc++] = val;
			if ((loc & 0xff) != 0)
				val ^= 0xff;
		}
	}
	mc6821_reset(mcc3->PIA0);
	mc6821_reset(mcc3->PIA1);
	if (mcc3->cart && mcc3->cart->reset) {
		mcc3->cart->reset(mcc3->cart, hard);
	}
	tcc1014_reset(mcc3->GIME);
	mcc3->CPU->reset(mcc3->CPU);
	tape_reset(mcc3->tape_interface);
	printer_reset(mcc3->printer_interface);
}

static enum machine_run_state coco3_run(struct machine *m, int ncycles) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;

#ifdef WANT_GDB_TARGET
	if (mcc3->gdb_interface) {
		switch (gdb_run_lock(mcc3->gdb_interface)) {
		case gdb_run_state_stopped:
			return machine_run_state_stopped;
		case gdb_run_state_running:
			mcc3->stop_signal = 0;
			mcc3->cycles += ncycles;
			mcc3->CPU->running = 1;
			mcc3->CPU->run(mcc3->CPU);
			if (mcc3->stop_signal != 0) {
				gdb_stop(mcc3->gdb_interface, mcc3->stop_signal);
			}
			break;
		case gdb_run_state_single_step:
			m->single_step(m);
			gdb_single_step(mcc3->gdb_interface);
			break;
		}
		gdb_run_unlock(mcc3->gdb_interface);
		return machine_run_state_ok;
	} else {
#endif
		mcc3->cycles += ncycles;
		mcc3->CPU->running = 1;
		mcc3->CPU->run(mcc3->CPU);
		return machine_run_state_ok;
#ifdef WANT_GDB_TARGET
	}
#endif
}

static void coco3_single_step(struct machine *m) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->single_step = 1;
	mcc3->CPU->running = 0;
	mcc3->CPU->debug_cpu.instruction_posthook = DELEGATE_AS0(void, coco3_instruction_posthook, mcc3);
	do {
		mcc3->CPU->run(mcc3->CPU);
	} while (mcc3->single_step);
	mcc3->CPU->debug_cpu.instruction_posthook.func = NULL;
}

/*
 * Stop emulation and set stop_signal to reflect the reason.
 */

static void coco3_signal(struct machine *m, int sig) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->stop_signal = sig;
	mcc3->CPU->running = 0;
}

static void coco3_trap(void *sptr) {
	struct machine *m = sptr;
	coco3_signal(m, MACHINE_SIGTRAP);
}

static void coco3_bp_add_n(struct machine *m, struct machine_bp *list, int n, void *sptr) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	for (int i = 0; i < n; i++) {
		if (list[i].add_cond & BP_CRC_COMBINED)
			continue;
		if ((list[i].add_cond & BP_CRC_EXT) && (!mcc3->has_secb || !crclist_match(list[i].cond_crc_extbas, mcc3->crc_secb)))
			continue;
		if (list[i].add_cond & BP_CRC_BAS)
			continue;
		list[i].bp.handler.sptr = sptr;
		bp_add(mcc3->bp_session, &list[i].bp);
	}
}

static void coco3_bp_remove_n(struct machine *m, struct machine_bp *list, int n) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	for (int i = 0; i < n; i++) {
		bp_remove(mcc3->bp_session, &list[i].bp);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int coco3_set_keyboard_type(struct machine *m, int action) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	int type = m->keyboard.type;
	switch (action) {
	case XROAR_QUERY:
		break;
	case XROAR_NEXT:
		if (type == m->config->keymap) {
			switch (m->config->keymap) {
			case dkbd_layout_coco3:
			case dkbd_layout_coco:
				type = dkbd_layout_dragon;
				break;
			default:
				type = dkbd_layout_coco3;
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
	keyboard_set_keymap(mcc3->keyboard.interface, type);
	return type;
}

static _Bool coco3_set_pause(struct machine *m, int state) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	switch (state) {
	case 0: case 1:
		mcc3->CPU->halt = state;
		break;
	case XROAR_NEXT:
		mcc3->CPU->halt = !mcc3->CPU->halt;
		break;
	default:
		break;
	}
	return mcc3->CPU->halt;
}

static _Bool coco3_set_inverted_text(struct machine *m, int action) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	switch (action) {
	case 0: case 1:
		mcc3->inverted_text = action;
		break;
	case 2:
		mcc3->inverted_text = !mcc3->inverted_text;
		break;
	default:
		break;
	}
	tcc1014_set_inverted_text(mcc3->GIME, mcc3->inverted_text);
	return mcc3->inverted_text;
}

/*
 * Device inspection.
 */

/* Similarly SLOW.  Used to populate UI. */

static void *coco3_get_interface(struct machine *m, const char *ifname) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	if (0 == strcmp(ifname, "cart")) {
		return mcc3->cart;
	} else if (0 == strcmp(ifname, "keyboard")) {
		return mcc3->keyboard.interface;
	} else if (0 == strcmp(ifname, "printer")) {
		return mcc3->printer_interface;
	} else if (0 == strcmp(ifname, "tape-update-audio")) {
		return update_audio_from_tape;
	}
	return NULL;
}

static void coco3_set_frameskip(struct machine *m, unsigned fskip) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->frameskip = fskip;
}

static void coco3_set_ratelimit(struct machine *m, _Bool ratelimit) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	sound_set_ratelimit(mcc3->snd, ratelimit);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used when single-stepping.

static void coco3_instruction_posthook(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	mcc3->single_step = 0;
}

static void read_byte(struct machine_coco3 *mcc3, unsigned A) {
	if (mcc3->cart) {
		mcc3->CPU->D = mcc3->cart->read(mcc3->cart, A, 0, 0, mcc3->CPU->D);
		if (mcc3->cart->EXTMEM) {
			return;
		}
	}
	switch (mcc3->GIME->S) {
	case 0:
		// ROM
		mcc3->CPU->D = mcc3->rom0[A & 0x7fff];
		break;
	case 1:
		// CTS (cartridge ROM)
		if (mcc3->cart) {
			mcc3->CPU->D = mcc3->cart->read(mcc3->cart, A ^ 0x4000, 0, 1, mcc3->CPU->D);
		}
		break;
	case 2:
		// IO
		if ((A & 32) == 0) {
			mcc3->CPU->D = mc6821_read(mcc3->PIA0, A);
		} else {
			mcc3->CPU->D = mc6821_read(mcc3->PIA1, A);
		}
		break;
	case 6:
		// SCS (cartridge IO)
		if (mcc3->cart)
			mcc3->CPU->D = mcc3->cart->read(mcc3->cart, A, 1, 0, mcc3->CPU->D);
		break;
	case 7:
		if (!mcc3->dat.enabled || !mcc3->dat.readable) {
			break;
		}
		// Optional DAT board can optionally be read from
		if (A == 0xff9b) {
			mcc3->CPU->D = (mcc3->CPU->D & ~0x03) | (mcc3->dat.vram_bank >> 19);
		} else if (A >= 0xffa0 && A < 0xffb0) {
			mcc3->CPU->D = (mcc3->CPU->D & ~0xc0) | (mcc3->dat.mmu_bank[A & 15] >> 13);
		}
	default:
		// All the rest are N/C
		break;
	}
	if (mcc3->GIME->RAS) {
		uint32_t Z = mcc3->GIME->Z;
		if (!mcc3->dat.MMUEN || (mcc3->dat.MC3 && A >= 0xfe00 && A < 0xff00)) {
			// MMU not enabled, or CRM enabled and CRM region
			mcc3->CPU->D = mcc3->ram[Z & mcc3->ram_mask];
		} else {
			// Otherwise, translate
			uint32_t bank = (A >> 13) | mcc3->dat.task;
			mcc3->CPU->D = mcc3->ram[mcc3->dat.mmu_bank[bank] | (Z & mcc3->ram_mask)];
		}
	}
}

static void write_byte(struct machine_coco3 *mcc3, unsigned A) {
	if (mcc3->cart) {
		mcc3->cart->write(mcc3->cart, A, 0, 0, mcc3->CPU->D);
	}
	if (!mcc3->cart || !mcc3->cart->EXTMEM) {
		switch (mcc3->GIME->S) {
		case 0:
			// ROM
			mcc3->CPU->D = mcc3->rom0[A & 0x7fff];
			break;
		case 1:
			// CTS (cartridge ROM)
			if (mcc3->cart)
				mcc3->cart->write(mcc3->cart, A ^ 0x4000, 0, 1, mcc3->CPU->D);
			break;
		case 2:
			// IO
			if ((A & 32) == 0) {
				mc6821_write(mcc3->PIA0, A, mcc3->CPU->D);
			} else {
				mc6821_write(mcc3->PIA1, A, mcc3->CPU->D);
			}
			break;
		case 6:
			// SCS (cartridge IO)
			if (mcc3->cart)
				mcc3->cart->write(mcc3->cart, A, 1, 0, mcc3->CPU->D);
			break;
		case 7:
			if (!mcc3->dat.enabled) {
				break;
			}
			// Optional DAT board intercepts writes to MMU registers
			if (A == 0xff90) {
				mcc3->dat.MMUEN = mcc3->CPU->D & 0x40;
				mcc3->dat.MC3 = mcc3->CPU->D & 0x08;
			} else if (A == 0xff91) {
				// Task register - store as index into MMU banks
				mcc3->dat.task = (mcc3->CPU->D & 0x01) ? 8 : 0;
			} else if (A == 0xff9b) {
				// Video RAM limited to one of four 512K banks
				mcc3->dat.vram_bank = ((mcc3->CPU->D & 0x03) << 19) & mcc3->dat.mask;
			} else if (A >= 0xffa0 && A < 0xffb0) {
				// MMU banking extended by 2 bits
				mcc3->dat.mmu_bank[A & 15] = ((mcc3->CPU->D & 0xc0) << 13) & mcc3->dat.mask;
			}
		default:
			// All the rest are N/C
			break;
		}
	}
	if (mcc3->GIME->RAS) {
		uint32_t Z = mcc3->GIME->Z;
		if (!mcc3->dat.MMUEN || (mcc3->dat.MC3 && A >= 0xfe00 && A < 0xff00)) {
			// MMU not enabled, or CRM enabled and CRM region
			mcc3->ram[Z & mcc3->ram_mask] = mcc3->CPU->D;
		} else {
			// Otherwise, translate
			uint32_t bank = (A >> 13) | mcc3->dat.task;
			mcc3->ram[mcc3->dat.mmu_bank[bank] | (Z & mcc3->ram_mask)] = mcc3->CPU->D;
		}
	}
}

/* RAM access on the CoCo 3 is interesting.  For reading, 16 bits of data are
 * strobed into two 8-bit buffers.  Each buffer is selected in turn using the
 * CAS signal, and presumably the GIME then latches one or the other to its
 * RAMD output based on the A0 line.  For writing, the CPU's data bus is
 * latched to one of the two banks based on two WE signals.
 *
 * As the hi-res text modes use pairs of bytes (character and attribute), this
 * allows all the data to be fetched in one cycle.
 *
 * Of course, I do none of that here - the GIME code just asks for another byte
 * if it needs it within the same cycle...  Good enough?
 */

static void cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_coco3 *mcc3 = sptr;
	mcc3->cycles -= ncycles;
	if (mcc3->cycles <= 0) mcc3->CPU->running = 0;
	event_current_tick += ncycles;
	event_run_queue(&MACHINE_EVENT_LIST);
	MC6809_IRQ_SET(mcc3->CPU, mcc3->PIA0->a.irq | mcc3->PIA0->b.irq | mcc3->GIME->IRQ);
	MC6809_FIRQ_SET(mcc3->CPU, mcc3->PIA1->a.irq | mcc3->PIA1->b.irq | mcc3->GIME->FIRQ);

	if (RnW) {
		read_byte(mcc3, A);
#ifdef WANT_GDB_TARGET
		if (mcc3->bp_session->wp_read_list)
			bp_wp_read_hook(mcc3->bp_session, A);
#endif
	} else {
		write_byte(mcc3, A);
#ifdef WANT_GDB_TARGET
		if (mcc3->bp_session->wp_write_list)
			bp_wp_write_hook(mcc3->bp_session, A);
#endif
	}
}

static void cpu_cycle_noclock(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_coco3 *mcc3 = sptr;
	(void)ncycles;
	if (RnW) {
		read_byte(mcc3, A);
	} else {
		write_byte(mcc3, A);
	}
}

/* Read a byte without advancing clock.  Used for debugging & breakpoints. */

static uint8_t coco3_read_byte(struct machine *m, unsigned A, uint8_t D) {
	(void)D;
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, mcc3);
	tcc1014_mem_cycle(mcc3->GIME, 1, A);
	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
	return mcc3->CPU->D;
}

/* Write a byte without advancing clock.  Used for debugging & breakpoints. */

static void coco3_write_byte(struct machine *m, unsigned A, uint8_t D) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->CPU->D = D;
	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, mcc3);
	tcc1014_mem_cycle(mcc3->GIME, 0, A);
	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
}

/* simulate an RTS without otherwise affecting machine state */
static void coco3_op_rts(struct machine *m) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	unsigned int new_pc = m->read_byte(m, mcc3->CPU->reg_s, 0) << 8;
	new_pc |= m->read_byte(m, mcc3->CPU->reg_s + 1, 0);
	mcc3->CPU->reg_s += 2;
	mcc3->CPU->reg_pc = new_pc;
}

static void coco3_dump_ram(struct machine *m, FILE *fd) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	fwrite(mcc3->ram, mcc3->ram_size, 1, fd);
}

static uint8_t fetch_vram(void *sptr, uint32_t A) {
	struct machine_coco3 *mcc3 = sptr;
	return mcc3->ram[mcc3->dat.vram_bank | (A & mcc3->ram_mask)];
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void keyboard_update(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	unsigned buttons = ~(joystick_read_buttons() & 15);
	struct keyboard_state state = {
		.row_source = mcc3->PIA0->a.out_sink,
		.row_sink = mcc3->PIA0->a.out_sink & buttons,
		.col_source = mcc3->PIA0->b.out_source,
		.col_sink = mcc3->PIA0->b.out_sink,
	};
	keyboard_read_matrix(mcc3->keyboard.interface, &state);
	mcc3->PIA0->a.in_sink = state.row_sink;
	mcc3->PIA0->b.in_source = state.col_source;
	mcc3->PIA0->b.in_sink = state.col_sink;
	mcc3->PIA1->b.in_source = (mcc3->PIA1->b.in_sink & ~(1<<2)) | ((state.col_source & (1<<6)) ? (1<<2) : 0);
	mcc3->PIA1->b.in_sink = (mcc3->PIA1->b.in_sink & ~(1<<2)) | ((state.col_sink & (1<<6)) ? (1<<2) : 0);
	mcc3->GIME->IL1 = (PIA_VALUE_A(mcc3->PIA0) | 0x80) != 0xff;
}

static void joystick_update(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	int port = (mcc3->PIA0->b.control_register & 0x08) >> 3;
	int axis = (mcc3->PIA0->a.control_register & 0x08) >> 3;
	int dac_value = ((mcc3->PIA1->a.out_sink & 0xfc) | 2) << 8;
	int js_value = joystick_read_axis(port, axis);
	if (js_value >= dac_value)
		mcc3->PIA0->a.in_sink |= 0x80;
	else
		mcc3->PIA0->a.in_sink &= 0x7f;
}

static void update_sound_mux_source(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	unsigned source = ((mcc3->PIA0->b.control_register & (1<<3)) >> 2)
	                  | ((mcc3->PIA0->a.control_register & (1<<3)) >> 3);
	sound_set_mux_source(mcc3->snd, source);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void pia0a_data_preread(void *sptr) {
	keyboard_update(sptr);
	joystick_update(sptr);
}

static void pia1a_data_postwrite(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	sound_set_dac_level(mcc3->snd, (float)(PIA_VALUE_A(mcc3->PIA1) & 0xfc) / 252.);
	tape_update_output(mcc3->tape_interface, mcc3->PIA1->a.out_sink & 0xfc);
}

static void pia1a_control_postwrite(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	tape_set_motor(mcc3->tape_interface, mcc3->PIA1->a.control_register & 0x08);
}

static void pia1b_data_postwrite(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	// Single-bit sound
	_Bool sbs_enabled = !((mcc3->PIA1->b.out_source ^ mcc3->PIA1->b.out_sink) & (1<<1));
	_Bool sbs_level = mcc3->PIA1->b.out_source & mcc3->PIA1->b.out_sink & (1<<1);
	sound_set_sbs(mcc3->snd, sbs_enabled, sbs_level);
}

static void pia1b_control_postwrite(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	sound_set_mux_enabled(mcc3->snd, mcc3->PIA1->b.control_register & 0x08);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* VDG edge delegates */

static void gime_hs(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	mc6821_set_cx1(&mcc3->PIA0->a, level);
}

/*
// PAL CoCos 1&2 invert HS - is this true for coco3?  Probably not...
static void gime_hs_pal_coco(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	mc6821_set_cx1(&mcc3->PIA0->a, !level);
}
*/

static void gime_fs(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	mc6821_set_cx1(&mcc3->PIA0->b, level);
	if (level) {
		sound_update(mcc3->snd);
		mcc3->frame--;
		if (mcc3->frame < 0)
			mcc3->frame = mcc3->frameskip;
		if (mcc3->frame == 0) {
			DELEGATE_CALL(mcc3->vo->vsync);
		}
	}
}

static void gime_render_line(void *sptr, const uint8_t *data, _Bool phase_invert) {
	struct machine_coco3 *mcc3 = sptr;
	struct ntsc_burst *nb = mcc3->ntsc_burst[phase_invert];
	DELEGATE_CALL(mcc3->vo->render_scanline, data, nb);
}

/* Sound output can feed back into the single bit sound pin when it's
 * configured as an input. */

static void single_bit_feedback(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	if (level) {
		mcc3->PIA1->b.in_source &= ~(1<<1);
		mcc3->PIA1->b.in_sink &= ~(1<<1);
	} else {
		mcc3->PIA1->b.in_source |= (1<<1);
		mcc3->PIA1->b.in_sink |= (1<<1);
	}
}

/* Tape audio delegate */

static void update_audio_from_tape(void *sptr, float value) {
	struct machine_coco3 *mcc3 = sptr;
	sound_set_tape_level(mcc3->snd, value);
	if (value >= 0.5)
		mcc3->PIA1->a.in_sink &= ~(1<<0);
	else
		mcc3->PIA1->a.in_sink |= (1<<0);
}

/* Catridge signalling */

static void cart_firq(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	mc6821_set_cx1(&mcc3->PIA1->b, level);
	mcc3->GIME->IL0 = level;
}

static void cart_nmi(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	MC6809_NMI_SET(mcc3->CPU, level);
}

static void cart_halt(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	MC6809_HALT_SET(mcc3->CPU, level);
}
