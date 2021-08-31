/** \file
 *
 *  \brief Tandy Colour Computer 3 machine.
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
 *
 *  Tandy CoCo 3 support is UNFINISHED and UNSUPPORTED, and much is KNOWN NOT
 *  TO WORK.  Please do not use except for testing.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "delegate.h"
#include "sds.h"
#include "xalloc.h"

#include "cart.h"
#include "crc32.h"
#include "crclist.h"
#include "hd6309.h"
#include "hd6309_trace.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "mc6809_trace.h"
#include "mc6821.h"
#include "ntsc.h"
#include "part.h"
#include "printer.h"
#include "romlist.h"
#include "sound.h"
#include "tape.h"
#include "tcc1014/tcc1014.h"
#include "vo.h"
#include "xroar.h"

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

static struct machine *coco3_new(struct machine_config *mc, struct vo_interface *vo,
				 struct sound_interface *snd, struct tape_interface *ti);
static void coco3_config_complete(struct machine_config *mc);

struct machine_module machine_coco3_module = {
	.name = "coco3",
	.description = "CoCo 3 machine",
	.config_complete = coco3_config_complete,
	.new = coco3_new,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_coco3 {
	struct machine public;
	struct MC6809 *CPU0;
	struct TCC1014 *GIME0;
	struct MC6821 *PIA0, *PIA1;

	struct vo_interface *vo;
	int frame;  // track frameskip
	struct sound_interface *snd;

	unsigned ram_size;
	unsigned ram_mask;
	uint8_t *ram;
	uint8_t *rom;
	uint8_t rom0[0x8000];

	_Bool inverted_text;
	_Bool fast_sound;
	struct cart *cart;
	unsigned frameskip;

	int cycles;

	struct bp_session *bp_session;
	/*
	_Bool single_step;
	int stop_signal;
#ifdef WANT_GDB_TARGET
	struct gdb_interface *gdb_interface;
#endif
	*/
	_Bool trace;

	// NTSC palettes for GIME
	struct ntsc_palette *ntsc_palette;
	struct ntsc_palette *dummy_palette;

	// NTSC colour bursts
	struct ntsc_burst *ntsc_burst[2];

	struct tape_interface *tape_interface;
	struct keyboard_interface *keyboard_interface;
	struct printer_interface *printer_interface;

	// Useful configuration side-effect tracking
	_Bool has_secb;
	uint32_t crc_secb;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco3_config_complete(struct machine_config *mc) {
	if (!mc->description) {
		mc->description = xstrdup(mc->name);
	}
	if (mc->tv_standard == ANY_AUTO)
		mc->tv_standard = TV_PAL;
	if (mc->vdg_type == ANY_AUTO)
		mc->vdg_type = VDG_GIME_1986;
	if (mc->vdg_type != VDG_GIME_1986 && mc->vdg_type != VDG_GIME_1987)
		mc->vdg_type = VDG_GIME_1986;
	if (mc->ram != 128 && mc->ram != 512)
		mc->ram = 128;
	mc->keymap = dkbd_layout_coco3;
	/* Now find which ROMs we're actually going to use */
	if (!mc->noextbas && !mc->extbas_rom) {
		mc->extbas_rom = xstrdup("@coco3");
	}
	// Determine a default DOS cartridge if necessary
	if (!mc->default_cart) {
		struct cart_config *cc = cart_find_working_dos(mc);
		if (cc)
			mc->default_cart = xstrdup(cc->name);
	}
}

static float hue_intensity_map[4] = { 0.30, 0.50, 0.80, 1.0 };
static float grey_intensity_map[4] = { 0.03, 0.23, 0.5, 1.0 };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco3_free(struct part *p);

static void coco3_insert_cart(struct machine *m, struct cart *c);
static void coco3_remove_cart(struct machine *m);
static void coco3_reset(struct machine *m, _Bool hard);
static enum machine_run_state coco3_run(struct machine *m, int ncycles);
//static void coco3_single_step(struct machine *m);
//static void coco3_signal(struct machine *m, int sig);
static void coco3_bp_add_n(struct machine *m, struct machine_bp *list, int n, void *sptr);
static void coco3_bp_remove_n(struct machine *m, struct machine_bp *list, int n);

static _Bool coco3_set_pause(struct machine *m, int state);
static _Bool coco3_set_inverted_text(struct machine *m, int state);
static void *coco3_get_component(struct machine *m, const char *cname);
static void *coco3_get_interface(struct machine *m, const char *ifname);
static void coco3_set_vo_cmp(struct machine *m, int mode);
static void coco3_set_frameskip(struct machine *m, unsigned fskip);
static void coco3_set_ratelimit(struct machine *m, _Bool ratelimit);

static uint8_t coco3_read_byte(struct machine *m, unsigned A);
static void coco3_write_byte(struct machine *m, unsigned A, unsigned D);
static void coco3_op_rts(struct machine *m);

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
static void gime_render_line(void *sptr, uint8_t *data, unsigned burst);

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

static struct machine *coco3_new(struct machine_config *mc, struct vo_interface *vo,
				 struct sound_interface *snd, struct tape_interface *ti) {
	if (!mc)
		return NULL;

	LOG_WARN("Tandy CoCo 3 support is UNFINISHED and UNSUPPORTED.\n");
	LOG_WARN("Please do not use except for testing.\n");

	struct machine_coco3 *mcc3 = part_new(sizeof(*mcc3));
	*mcc3 = (struct machine_coco3){0};
	struct machine *m = &mcc3->public;

	part_init(&m->part, "coco3");
	m->part.free = coco3_free;

	coco3_config_complete(mc);

	m->config = mc;
	m->insert_cart = coco3_insert_cart;
	m->remove_cart = coco3_remove_cart;
	m->reset = coco3_reset;
	m->run = coco3_run;
	//m->single_step = coco3_single_step;
	//m->signal = coco3_signal;
	m->bp_add_n = coco3_bp_add_n;
	m->bp_remove_n = coco3_bp_remove_n;

	m->set_pause = coco3_set_pause;
	m->set_inverted_text = coco3_set_inverted_text;
	m->get_component = coco3_get_component;
	m->get_interface = coco3_get_interface;
	m->set_vo_cmp = coco3_set_vo_cmp;
	m->set_frameskip = coco3_set_frameskip;
	m->set_ratelimit = coco3_set_ratelimit;

	m->read_byte = coco3_read_byte;
	m->write_byte = coco3_write_byte;
	m->op_rts = coco3_op_rts;

	mcc3->vo = vo;
	mcc3->snd = snd;

	// GIME
	mcc3->GIME0 = tcc1014_new(mc->vdg_type);
	part_add_component(&m->part, (struct part *)mcc3->GIME0, "GIME");
	tcc1014_set_palette(mcc3->GIME0, mcc3->dummy_palette);
	mcc3->GIME0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
	mcc3->GIME0->fetch_vram = DELEGATE_AS1(uint8, uint32, fetch_vram, mcc3);

	mcc3->ntsc_palette = ntsc_palette_new();
	mcc3->dummy_palette = ntsc_palette_new();
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
		ntsc_palette_add_ybr(mcc3->ntsc_palette, j, y, b_y, r_y);
		ntsc_palette_add_direct(mcc3->dummy_palette, j);
	}

	//mcc3->ntsc_burst[0] = ntsc_burst_new(-33);  // No burst (hi-res, css=1)
	mcc3->ntsc_burst[0] = ntsc_burst_new(0);  // Normal burst (mode modes)
	mcc3->ntsc_burst[1] = ntsc_burst_new(0);  // Normal burst (mode modes)

	// CPU
	switch (mc->cpu) {
	case CPU_MC6809: default:
		mcc3->CPU0 = mc6809_new();
		break;
	case CPU_HD6309:
		mcc3->CPU0 = hd6309_new();
		break;
	}
	part_add_component(&m->part, (struct part *)mcc3->CPU0, "CPU");
	mcc3->CPU0->mem_cycle = DELEGATE_AS2(void, bool, uint16, tcc1014_mem_cycle, mcc3->GIME0);

	mcc3->GIME0->CPUD = &mcc3->CPU0->D;

	// Breakpoint session
	mcc3->bp_session = bp_session_new(m);

	// Keyboard interface
	mcc3->keyboard_interface = keyboard_interface_new(m);
	mcc3->keyboard_interface->update = DELEGATE_AS0(void, keyboard_update, mcc3);

	// Tape interface
	mcc3->tape_interface = ti;

	// Printer interface
	mcc3->printer_interface = printer_interface_new(m);

	// PIAs
	mcc3->PIA0 = mc6821_new();
	part_add_component(&m->part, (struct part *)mcc3->PIA0, "PIA0");
	mcc3->PIA0->a.data_preread = DELEGATE_AS0(void, pia0a_data_preread, mcc3);
	mcc3->PIA0->a.data_postwrite = DELEGATE_AS0(void, pia0a_data_postwrite, mcc3);
	mcc3->PIA0->a.control_postwrite = DELEGATE_AS0(void, pia0a_control_postwrite, mcc3);
	mcc3->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, mcc3);
	mcc3->PIA0->b.data_postwrite = DELEGATE_AS0(void, pia0b_data_postwrite, mcc3);
	mcc3->PIA0->b.control_postwrite = DELEGATE_AS0(void, pia0b_control_postwrite, mcc3);
	mcc3->PIA1 = mc6821_new();
	part_add_component(&m->part, (struct part *)mcc3->PIA1, "PIA1");
	mcc3->PIA1->a.data_preread = DELEGATE_AS0(void, pia1a_data_preread, mcc3);
	mcc3->PIA1->a.data_postwrite = DELEGATE_AS0(void, pia1a_data_postwrite, mcc3);
	mcc3->PIA1->a.control_postwrite = DELEGATE_AS0(void, pia1a_control_postwrite, mcc3);
	mcc3->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread, mcc3);
	mcc3->PIA1->b.data_postwrite = DELEGATE_AS0(void, pia1b_data_postwrite, mcc3);
	mcc3->PIA1->b.control_postwrite = DELEGATE_AS0(void, pia1b_control_postwrite, mcc3);

	// Single-bit sound feedback
	snd->sbs_feedback = DELEGATE_AS1(void, bool, single_bit_feedback, mcc3);

	// Tape
	mcc3->tape_interface->update_audio = DELEGATE_AS1(void, float, update_audio_from_tape, mcc3);

	mcc3->GIME0->signal_hs = DELEGATE_AS1(void, bool, gime_hs, mcc3);
	mcc3->GIME0->signal_fs = DELEGATE_AS1(void, bool, gime_fs, mcc3);
	mcc3->GIME0->render_line = DELEGATE_AS2(void, uint8p, unsigned, gime_render_line, mcc3);
	tcc1014_set_inverted_text(mcc3->GIME0, mcc3->inverted_text);

	/* Load appropriate ROMs */
	memset(mcc3->rom0, 0, sizeof(mcc3->rom0));

	// The CoCo 3 ROMs is a single 32K image: Super Extended Colour BASIC.

	mcc3->has_secb = 0;
	mcc3->crc_secb = 0;

	/* ... Super Extended BASIC */
	if (!mc->noextbas && mc->extbas_rom) {
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
	mcc3->ram = xmalloc(mcc3->ram_size);
	/* This will be under PIA control on a Dragon 64 */
	mcc3->rom = mcc3->rom0;

	/* CRCs */

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

	/* Default all PIA connections to unconnected (no source, no sink) */
	mcc3->PIA0->b.in_source = 0;
	mcc3->PIA1->b.in_source = 0;
	mcc3->PIA0->a.in_sink = mcc3->PIA0->b.in_sink = 0xff;
	mcc3->PIA1->a.in_sink = mcc3->PIA1->b.in_sink = 0xff;

	keyboard_set_chord_mode(mcc3->keyboard_interface, keyboard_chord_mode_coco_basic);

	mcc3->fast_sound = xroar_cfg.fast_sound;

	keyboard_set_keymap(mcc3->keyboard_interface, xroar_machine_config->keymap);

	/*
#ifdef WANT_GDB_TARGET
	// GDB
	if (xroar_cfg.gdb) {
		mcc3->gdb_interface = gdb_interface_new(xroar_cfg.gdb_ip, xroar_cfg.gdb_port, m, mcc3->bp_session);
		gdb_set_debug(mcc3->gdb_interface, xroar_cfg.debug_gdb);
	}
#endif
*/

	return m;
}

static void coco3_free(struct part *p) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)p;
	struct machine *m = &mcc3->public;
	if (m->config && m->config->description) {
		LOG_DEBUG(1, "Machine shutdown: %s\n", m->config->description);
	}
	//m->remove_cart(m);
#ifdef WANT_GDB_TARGET
	/*
	if (mcc3->gdb_interface) {
		gdb_interface_free(mcc3->gdb_interface);
	}
	*/
#endif
	if (mcc3->keyboard_interface) {
		keyboard_interface_free(mcc3->keyboard_interface);
	}
	if (mcc3->printer_interface) {
		printer_interface_free(mcc3->printer_interface);
	}
	if (mcc3->bp_session) {
		bp_session_free(mcc3->bp_session);
	}
	ntsc_burst_free(mcc3->ntsc_burst[1]);
	ntsc_burst_free(mcc3->ntsc_burst[0]);
	ntsc_palette_free(mcc3->dummy_palette);
	ntsc_palette_free(mcc3->ntsc_palette);
	free(mcc3->ram);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco3_insert_cart(struct machine *m, struct cart *c) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	m->remove_cart(m);
	if (c) {
		assert(c->read != NULL);
		assert(c->write != NULL);
		mcc3->cart = c;
		c->signal_firq = DELEGATE_AS1(void, bool, cart_firq, mcc3);
		c->signal_nmi = DELEGATE_AS1(void, bool, cart_nmi, mcc3);
		c->signal_halt = DELEGATE_AS1(void, bool, cart_halt, mcc3);
		part_add_component(&m->part, (struct part *)c, "CART");
	}
}

static void coco3_remove_cart(struct machine *m) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	part_free((struct part *)mcc3->cart);
	mcc3->cart = NULL;
}

static void coco3_reset(struct machine *m, _Bool hard) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	xroar_set_keymap(1, xroar_machine_config->keymap);
	switch (xroar_machine_config->tv_standard) {
	case TV_PAL: default:
		xroar_set_cross_colour(1, VO_PHASE_OFF);
		break;
	case TV_NTSC:
		xroar_set_cross_colour(1, VO_PHASE_KBRW);
		break;
	}
	if (hard) {
		/* Intialise RAM contents */
		unsigned loc = 0, val = 0xff;
		/* Don't know why, but RAM seems to start in this state: */
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
		mcc3->cart->reset(mcc3->cart);
	}
	tcc1014_reset(mcc3->GIME0);
	mcc3->CPU0->reset(mcc3->CPU0);
	tape_reset(mcc3->tape_interface);
	printer_reset(mcc3->printer_interface);
}

static enum machine_run_state coco3_run(struct machine *m, int ncycles) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;

	/*
#ifdef WANT_GDB_TARGET
	if (mcc3->gdb_interface) {
		switch (gdb_run_lock(mcc3->gdb_interface)) {
		case gdb_run_state_timeout:
			return machine_run_state_timeout;
		case gdb_run_state_running:
			mcc3->stop_signal = 0;
			mcc3->cycles += ncycles;
			mcc3->CPU0->running = 1;
			mcc3->CPU0->run(mcc3->CPU0);
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
		return mcc3->stop_signal ? machine_run_state_stopped : machine_run_state_ok;
	} else {
#endif
*/
		mcc3->cycles += ncycles;
		mcc3->CPU0->running = 1;
		mcc3->CPU0->run(mcc3->CPU0);
		return machine_run_state_ok;
		/*
#ifdef WANT_GDB_TARGET
	}
#endif
*/
}

/*
static void coco3_single_step(struct machine *m) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->single_step = 1;
	mcc3->CPU0->running = 0;
	mcc3->CPU0->instruction_posthook = DELEGATE_AS0(void, coco3_instruction_posthook, mcc3);
	do {
		mcc3->CPU0->run(mcc3->CPU0);
	} while (mcc3->single_step);
	if (xroar_cfg.trace_enabled)
		mcc3->CPU0->instruction_posthook.func = NULL;
}
*/

/*
 * Stop emulation and set stop_signal to reflect the reason.
 */

/*
static void coco3_signal(struct machine *m, int sig) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->stop_signal = sig;
	mcc3->CPU0->running = 0;
}
*/

static void coco3_bp_add_n(struct machine *m, struct machine_bp *list, int n, void *sptr) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	for (int i = 0; i < n; i++) {
		if ((list[i].add_cond & BP_MACHINE_ARCH) && xroar_machine_config->architecture != list[i].cond_machine_arch)
			continue;
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

static _Bool coco3_set_pause(struct machine *m, int state) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	switch (state) {
	case 0: case 1:
		mcc3->CPU0->halt = state;
		break;
	case 2:
		mcc3->CPU0->halt = !mcc3->CPU0->halt;
		break;
	default:
		break;
	}
	return mcc3->CPU0->halt;
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
	tcc1014_set_inverted_text(mcc3->GIME0, mcc3->inverted_text);
	return mcc3->inverted_text;
}

/*
 * Device inspection.
 */

/* Note, this is SLOW.  Could be sped up by maintaining a hash by component
 * name, but will only ever be used outside critical path, so don't bother for
 * now. */

static void *coco3_get_component(struct machine *m, const char *cname) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	if (0 == strcmp(cname, "CPU0")) {
		return mcc3->CPU0;
	} else if (0 == strcmp(cname, "GIME0")) {
		return mcc3->GIME0;
	} else if (0 == strcmp(cname, "PIA0")) {
		return mcc3->PIA0;
	} else if (0 == strcmp(cname, "PIA1")) {
		return mcc3->PIA1;
	}
	return NULL;
}

/* Similarly SLOW.  Used to populate UI. */

static void *coco3_get_interface(struct machine *m, const char *ifname) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	if (0 == strcmp(ifname, "cart")) {
		return mcc3->cart;
	} else if (0 == strcmp(ifname, "keyboard")) {
		return mcc3->keyboard_interface;
	} else if (0 == strcmp(ifname, "printer")) {
		return mcc3->printer_interface;
	} else if (0 == strcmp(ifname, "tape-update-audio")) {
		return update_audio_from_tape;
	}
	return NULL;
}

static void coco3_set_vo_cmp(struct machine *m, int mode) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	switch (mode) {
	case VO_CMP_PALETTE:
	default:
		tcc1014_set_palette(mcc3->GIME0, mcc3->dummy_palette);
		break;
	case VO_CMP_SIMULATED:
		tcc1014_set_palette(mcc3->GIME0, mcc3->ntsc_palette);
		break;
	}
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

/*
 * Used when single-stepping or tracing.
 */

static void coco3_instruction_posthook(void *sptr) {
	(void)sptr;
	//struct machine_coco3 *mcc3 = sptr;
	//mcc3->single_step = 0;  // XXX why is this commented out?
}

static void read_byte(struct machine_coco3 *mcc3, unsigned A) {
	if (mcc3->cart) {
		mcc3->CPU0->D = mcc3->cart->read(mcc3->cart, A, 0, 0, mcc3->CPU0->D);
		if (mcc3->cart->EXTMEM) {
			return;
		}
	}
	switch (mcc3->GIME0->S) {
	case 0:
		// rom?
		mcc3->CPU0->D = mcc3->rom[A & 0x7fff];
		break;
	case 1:
		if (mcc3->cart)
			mcc3->CPU0->D = mcc3->cart->read(mcc3->cart, A, 0, 1, mcc3->CPU0->D);
		break;
	case 2:
		// seems to be general io area
		if ((A & 32) == 0) {
			mcc3->CPU0->D = mc6821_read(mcc3->PIA0, A);
		} else {
			mcc3->CPU0->D = mc6821_read(mcc3->PIA1, A);
		}
		break;
	case 6:
		if (mcc3->cart)
			mcc3->CPU0->D = mcc3->cart->read(mcc3->cart, A, 1, 0, mcc3->CPU0->D);
		break;
	case 7:
		break;
	default:
		break;
	}
	if (mcc3->GIME0->RAS) {
		uint32_t Z = mcc3->GIME0->Z;
		mcc3->CPU0->D = mcc3->ram[Z & mcc3->ram_mask];
	}
}

static void write_byte(struct machine_coco3 *mcc3, unsigned A) {
	if (mcc3->cart) {
		mcc3->cart->write(mcc3->cart, A, 0, 0, mcc3->CPU0->D);
		if (mcc3->cart->EXTMEM) {
			return;
		}
	}
	switch (mcc3->GIME0->S) {
	case 0:
		// rom?
		mcc3->CPU0->D = mcc3->rom[A & 0x7fff];
		break;
	case 1:
		// seems to be CTS?
		if (mcc3->cart)
			mcc3->cart->write(mcc3->cart, A, 0, 1, mcc3->CPU0->D);
		break;
	case 2:
		// seems to be general io area
		if ((A & 32) == 0) {
			mc6821_write(mcc3->PIA0, A, mcc3->CPU0->D);
		} else {
			mc6821_write(mcc3->PIA1, A, mcc3->CPU0->D);
		}
		break;
	case 6:
		if (mcc3->cart)
			mcc3->cart->write(mcc3->cart, A, 1, 0, mcc3->CPU0->D);
		break;
	case 7:
		break;
	default:
		break;
	}
	if (mcc3->GIME0->RAS) {
		uint32_t Z = mcc3->GIME0->Z;
		mcc3->ram[Z & mcc3->ram_mask] = mcc3->CPU0->D;
	}
}

/*
 * RAM access on the CoCo 3 is interesting.  For reading, 16 bits of data are
 * strobed into two 8-bit buffers.  Each buffer is selected in turn using the
 * CAS signal, and presumably the GIME then latches one or the other to its
 * RAMD output based on the A0 line.  For writing, the CPU's data bus is
 * latched to one of the two banks based on two WE signals.
 *
 * As the hi-res text modes use pairs of bytes (character and attribute), this
 * allows all the data to be fetched in one cycle.
 */

static void cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_coco3 *mcc3 = sptr;
	mcc3->cycles -= ncycles;
	if (mcc3->cycles <= 0) mcc3->CPU0->running = 0;
	event_current_tick += ncycles;
	event_run_queue(&MACHINE_EVENT_LIST);
	MC6809_IRQ_SET(mcc3->CPU0, mcc3->PIA0->a.irq | mcc3->PIA0->b.irq | mcc3->GIME0->IRQ);
	MC6809_FIRQ_SET(mcc3->CPU0, mcc3->PIA1->a.irq | mcc3->PIA1->b.irq | mcc3->GIME0->FIRQ);

	if (RnW) {
		read_byte(mcc3, A);
		bp_wp_read_hook(mcc3->bp_session, A);
	} else {
		write_byte(mcc3, A);
		bp_wp_write_hook(mcc3->bp_session, A);
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

static uint8_t coco3_read_byte(struct machine *m, unsigned A) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->GIME0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, mcc3);
	tcc1014_mem_cycle(mcc3->GIME0, 1, A);
	mcc3->GIME0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
	return mcc3->CPU0->D;
}

/* Write a byte without advancing clock.  Used for debugging & breakpoints. */

static void coco3_write_byte(struct machine *m, unsigned A, unsigned D) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	mcc3->CPU0->D = D;
	mcc3->GIME0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, mcc3);
	tcc1014_mem_cycle(mcc3->GIME0, 0, A);
	mcc3->GIME0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
}

/* simulate an RTS without otherwise affecting machine state */
static void coco3_op_rts(struct machine *m) {
	struct machine_coco3 *mcc3 = (struct machine_coco3 *)m;
	unsigned int new_pc = m->read_byte(m, mcc3->CPU0->reg_s) << 8;
	new_pc |= m->read_byte(m, mcc3->CPU0->reg_s + 1);
	mcc3->CPU0->reg_s += 2;
	mcc3->CPU0->reg_pc = new_pc;
}

static uint8_t fetch_vram(void *sptr, uint32_t A) {
	struct machine_coco3 *mcc3 = sptr;
	return mcc3->ram[A & mcc3->ram_mask];
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void keyboard_update(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	unsigned buttons = ~(joystick_read_buttons() & 3);
	struct keyboard_state state = {
		.row_source = mcc3->PIA0->a.out_sink,
		.row_sink = mcc3->PIA0->a.out_sink & buttons,
		.col_source = mcc3->PIA0->b.out_source,
		.col_sink = mcc3->PIA0->b.out_sink,
	};
	keyboard_read_matrix(mcc3->keyboard_interface, &state);
	mcc3->PIA0->a.in_sink = state.row_sink;
	mcc3->PIA0->b.in_source = state.col_source;
	mcc3->PIA0->b.in_sink = state.col_sink;
	mcc3->GIME0->IL1 = (PIA_VALUE_A(mcc3->PIA0) | 0x80) != 0xff;
}

static void joystick_update(void *sptr) {
	struct machine_coco3 *mcc3 = sptr;
	int port = (mcc3->PIA0->b.control_register & 0x08) >> 3;
	int axis = (mcc3->PIA0->a.control_register & 0x08) >> 3;
	int dac_value = (mcc3->PIA1->a.out_sink & 0xfc) << 8;
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
	(void)sptr;
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
	tape_update_motor(mcc3->tape_interface, mcc3->PIA1->a.control_register & 0x08);
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

static void gime_render_line(void *sptr, uint8_t *data, unsigned burst) {
	struct machine_coco3 *mcc3 = sptr;
	struct ntsc_burst *nb = mcc3->ntsc_burst[burst];
	unsigned phase = 2*mcc3->public.config->cross_colour_phase;
	DELEGATE_CALL(mcc3->vo->render_scanline, data, nb, phase);
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
	mcc3->GIME0->IL0 = level;
}

static void cart_nmi(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	MC6809_NMI_SET(mcc3->CPU0, level);
}

static void cart_halt(void *sptr, _Bool level) {
	struct machine_coco3 *mcc3 = sptr;
	MC6809_HALT_SET(mcc3->CPU0, level);
}
