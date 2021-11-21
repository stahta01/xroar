/** \file
 *
 *  \brief Tandy MC-10 machine.
 *
 *  \copyright Copyright 2021 Ciaran Anscomb
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
 *  Tandy MC-10 support is UNFINISHED and UNSUPPORTED.
 *  Please do not use except for testing.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"
#include "xalloc.h"

#include "ao.h"
#include "breakpoint.h"
#include "crc32.h"
#include "crclist.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6801.h"
#include "mc6847/mc6847.h"
#include "ntsc.h"
#include "part.h"
#include "printer.h"
#include "romlist.h"
#include "serialise.h"
#include "sound.h"
#include "tape.h"
#include "vdg_palette.h"
#include "vo.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_mc10 {
	struct machine machine;

	struct MC6801 *CPU;
	struct MC6847 *VDG;

	struct vo_interface *vo;
	int frame;  // track frameskip
	struct sound_interface *snd;

	unsigned ram_size;
	uint8_t *ram;
	uint8_t rom0[0x2000];

	_Bool inverted_text;
	unsigned frameskip;
	unsigned video_mode;
	uint16_t video_attr;

	int cycles;

	// Debug
	struct bp_session *bp_session;
	_Bool single_step;
	int stop_signal;

	struct tape_interface *tape_interface;
	struct printer_interface *printer_interface;

	struct {
		struct keyboard_interface *interface;
		// Keyboard row read value is updated on port read, and also by
		// CPU on appropriate port write.
		unsigned rows;
	} keyboard;

	struct ntsc_burst *ntsc_burst[2];

	// Useful configuration side-effect tracking
	_Bool has_bas;
	uint32_t crc_bas;
};

static const struct ser_struct ser_struct_mc10[] = {
	SER_STRUCT_NEST(&machine_ser_struct_data), // 1
	SER_STRUCT_ELEM(struct machine_mc10, ram, ser_type_unhandled), // 2
	SER_STRUCT_ELEM(struct machine_mc10, ram_size, ser_type_unsigned), // 3
	SER_STRUCT_ELEM(struct machine_mc10, inverted_text, ser_type_bool), // 4
	SER_STRUCT_ELEM(struct machine_mc10, video_mode, ser_type_unsigned), // 5
	SER_STRUCT_ELEM(struct machine_mc10, video_attr, ser_type_unsigned), // 6
};

#define MC10_SER_RAM     (2)

static _Bool mc10_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool mc10_write_elem(void *sptr, struct ser_handle *sh, int tag);

const struct ser_struct_data mc10_ser_struct_data = {
	.elems = ser_struct_mc10,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc10),
	.read_elem = mc10_read_elem,
	.write_elem = mc10_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_config_complete(struct machine_config *mc) {
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
	mc->vdg_type = VDG_6847;
	mc->architecture = ARCH_MC10;
	if (mc->ram != 2 && mc->ram != 4 && mc->ram != 20) {
		if (mc->ram >= 16)
			mc->ram = 20;
		else
			mc->ram = 4;
	}
	mc->keymap = dkbd_layout_mc10;
	if (!mc->bas_dfn && !mc->bas_rom) {
		mc->bas_rom = xstrdup("@mc10");
	}
}

static _Bool mc10_is_working_config(struct machine_config *mc) {
	if (!mc)
		return 0;
	sds tmp;
	if (mc->bas_rom) {
		tmp = romlist_find(mc->bas_rom);
		if (!tmp)
			return 0;
		sdsfree(tmp);
	} else {
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_reset(struct machine *m, _Bool hard);
static enum machine_run_state mc10_run(struct machine *m, int ncycles);
static void mc10_single_step(struct machine *m);
static void mc10_signal(struct machine *m, int sig);
static void mc10_trap(void *sptr);
static void mc10_bp_add_n(struct machine *m, struct machine_bp *list, int n, void *sptr);
static void mc10_bp_remove_n(struct machine *m, struct machine_bp *list, int n);
static uint8_t mc10_read_byte(struct machine *m, unsigned A, uint8_t D);
static void mc10_write_byte(struct machine *m, unsigned A, uint8_t D);
static void mc10_op_rts(struct machine *m);
static void mc10_dump_ram(struct machine *m, FILE *fd);

static void mc10_vdg_hs(void *sptr, _Bool level);
static void mc10_vdg_fs(void *sptr, _Bool level);
static void mc10_vdg_render_line(void *sptr, uint8_t *data, unsigned burst);
static void mc10_vdg_fetch_handler(void *sptr, uint16_t A, int nbytes, uint16_t *dest);
static void mc10_vdg_update_mode(void *sptr);

static void mc10_mem_cycle(void *sptr, _Bool RnW, uint16_t A);

static _Bool mc10_set_inverted_text(struct machine *m, int action);
static void *mc10_get_interface(struct machine *m, const char *ifname);
static void mc10_set_frameskip(struct machine *m, unsigned fskip);
static void mc10_set_ratelimit(struct machine *m, _Bool ratelimit);

static void mc10_keyboard_update(void *sptr);
static void mc10_update_tape_input(void *sptr, float value);
static void mc10_mc6803_port2_postwrite(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MC-10 part creation

static struct part *mc10_allocate(void);
static void mc10_initialise(struct part *p, void *options);
static _Bool mc10_finish(struct part *p);
static void mc10_free(struct part *p);

static const struct partdb_entry_funcs mc10_funcs = {
	.allocate = mc10_allocate,
	.initialise = mc10_initialise,
	.finish = mc10_finish,
	.free = mc10_free,

	.ser_struct_data = &mc10_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_extra mc10_machine_extra = {
	.config_complete = mc10_config_complete,
	.is_working_config = mc10_is_working_config,
};

const struct partdb_entry mc10_part = { .name = "mc10", .funcs = &mc10_funcs, .extra = { &mc10_machine_extra } };

static struct part *mc10_allocate(void) {
        struct machine_mc10 *mp = part_new(sizeof(*mp));
        struct machine *m = &mp->machine;
        struct part *p = &m->part;

        *mp = (struct machine_mc10){0};

	m->reset = mc10_reset;
	m->run = mc10_run;
	m->single_step = mc10_single_step;
	m->signal = mc10_signal;
	m->bp_add_n = mc10_bp_add_n;
	m->bp_remove_n = mc10_bp_remove_n;
	m->read_byte = mc10_read_byte;
	m->write_byte = mc10_write_byte;
	m->op_rts = mc10_op_rts;
	m->dump_ram = mc10_dump_ram;

	m->set_inverted_text = mc10_set_inverted_text;
	m->get_interface = mc10_get_interface;
	m->set_frameskip = mc10_set_frameskip;
	m->set_ratelimit = mc10_set_ratelimit;

	m->keyboard.type = dkbd_layout_mc10;

	return p;
}

static void mc10_initialise(struct part *p, void *options) {
        struct machine_config *mc = options;
        assert(mc != NULL);

        struct machine_mc10 *mp = (struct machine_mc10 *)p;
        struct machine *m = &mp->machine;

        mc10_config_complete(mc);
        m->config = mc;

	// CPU
	part_add_component(&m->part, part_create("MC6803", "6803"), "CPU");

	// VDG
	part_add_component(&m->part, part_create("MC6847", "6847"), "VDG");

	// Keyboard
	m->keyboard.type = mc->keymap;
}

static _Bool mc10_finish(struct part *p) {
	struct machine_mc10 *mp = (struct machine_mc10 *)p;
	struct machine *m = &mp->machine;
	struct machine_config *mc = m->config;

	// Interfaces
	mp->vo = xroar_vo_interface;
	mp->snd = xroar_ao_interface->sound_interface;
	mp->tape_interface = xroar_tape_interface;

	mp->tape_interface->default_paused = 1;

	// Find attached parts
	mp->CPU = (struct MC6801 *)part_component_by_id_is_a(p, "CPU", "MC6803");
	mp->VDG = (struct MC6847 *)part_component_by_id_is_a(p, "VDG", "MC6847");

	// Check all required parts are attached
	if (!mp->CPU || !mp->VDG ||
	    !mp->vo || !mp->snd || !mp->tape_interface) {
		return 0;
	}

	mp->CPU->mem_cycle = DELEGATE_AS2(void, bool, uint16, mc10_mem_cycle, mp);
	mp->CPU->port2.preread = DELEGATE_AS0(void, mc10_keyboard_update, mp);
	mp->CPU->port2.postwrite = DELEGATE_AS0(void, mc10_mc6803_port2_postwrite, mp);

	// Breakpoint session
	mp->bp_session = bp_session_new(m);
	assert(mp->bp_session != NULL);  // this shouldn't fail
	mp->bp_session->trap_handler = DELEGATE_AS0(void, mc10_trap, m);

	// XXX probably need a more generic sound interface reset call, but for
	// now bodge this - other machines will have left this pointing to
	// something that no longer works if we switched to MC-10 afterwards
	mp->snd->sbs_feedback.func = NULL;

	// VDG

	// This only affects how PAL signal padding works, and for now I'm
	// going to assume it's like the CoCo.
	mp->VDG->is_coco = 1;
	_Bool is_pal = (mc->tv_standard == TV_PAL);
	mp->VDG->is_pal = is_pal;

	mp->VDG->signal_hs = DELEGATE_AS1(void, bool, mc10_vdg_hs, mp);
        mp->VDG->signal_fs = DELEGATE_AS1(void, bool, mc10_vdg_fs, mp);
        mp->VDG->render_line = DELEGATE_AS2(void, uint8p, unsigned, mc10_vdg_render_line, mp);
        mp->VDG->fetch_data = DELEGATE_AS3(void, uint16, int, uint16p, mc10_vdg_fetch_handler, mp);
        mc6847_set_inverted_text(mp->VDG, mp->inverted_text);

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
			DELEGATE_CALL(mp->vo->palette_set_ybr, c, y, b_y, r_y);
		}
	}

	mp->ntsc_burst[0] = ntsc_burst_new(0);    // Normal burst
	mp->ntsc_burst[1] = ntsc_burst_new(180);  // Phase inverted burst

	// Tape
	mp->tape_interface->update_audio = DELEGATE_AS1(void, float, mc10_update_tape_input, mp);

	memset(mp->rom0, 0, sizeof(mp->rom0));

	// BASIC
	if (mc->bas_rom) {
		sds tmp = romlist_find(mc->bas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, mp->rom0, sizeof(mp->rom0));
			if (size > 0) {
				mp->has_bas = 1;
			}
			sdsfree(tmp);
		}
	}

	mp->ram_size = mc->ram * 1024;
	if (!mp->ram) {
		mp->ram = xmalloc(mp->ram_size);
	}

	if (mp->has_bas) {
		_Bool forced = 0, valid_crc = 0;

		mp->crc_bas = crc32_block(CRC32_RESET, mp->rom0, 0x2000);
		valid_crc = crclist_match("@mc10", mp->crc_bas);

		if (xroar_cfg.force_crc_match) {
			mp->crc_bas = 0x11fda97e;  // MC-10 ROM
			forced = 1;
		}

		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\tBASIC CRC = 0x%08x%s\n", mp->crc_bas, forced ? " (forced)" : "");

		if (!valid_crc) {
			LOG_WARN("Invalid CRC for Micro Colour BASIC ROM\n");
		}
	}

	// Keyboard interface
	mp->keyboard.interface = keyboard_interface_new(m);
	mp->keyboard.interface->update = DELEGATE_AS0(void, mc10_keyboard_update, mp);
	keyboard_set_keymap(mp->keyboard.interface, m->keyboard.type);

	// Printer interface
	mp->printer_interface = printer_interface_new(m);

#ifdef WANT_GDB_TARGET
	// GDB
	/* if (xroar_cfg.gdb) {
		mp->gdb_interface = gdb_interface_new(xroar_cfg.gdb_ip, xroar_cfg.gdb_port, m, mmp>bp_session);
	} */
#endif

	return 1;
}

// Called from part_free(), which handles freeing the struct itself
static void mc10_free(struct part *p) {
	struct machine_mc10 *mp = (struct machine_mc10 *)p;
#ifdef WANT_GDB_TARGET
	/* if (mp->gdb_interface) {
		gdb_interface_free(mp->gdb_interface);
	} */
#endif
	if (mp->ram) {
		free(mp->ram);
		mp->ram = NULL;
	}
	if (mp->keyboard.interface) {
		keyboard_interface_free(mp->keyboard.interface);
	}
	if (mp->printer_interface) {
		printer_interface_free(mp->printer_interface);
	}
	if (mp->bp_session) {
		bp_session_free(mp->bp_session);
	}
	ntsc_burst_free(mp->ntsc_burst[1]);
	ntsc_burst_free(mp->ntsc_burst[0]);
}

static _Bool mc10_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_mc10 *mp = sptr;
	size_t length = ser_data_length(sh);
	switch (tag) {
	case MC10_SER_RAM:
		if (!mp->machine.config) {
			return 0;
		}
		if (length != ((unsigned)mp->machine.config->ram * 1024)) {
			LOG_WARN("MC10/DESERIALISE: RAM size mismatch\n");
			return 0;
		}
		if (mp->ram) {
			free(mp->ram);
		}
		mp->ram = ser_read_new(sh, length);
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool mc10_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_mc10 *mp = sptr;
	switch (tag) {
	case MC10_SER_RAM:
		ser_write(sh, tag, mp->ram, mp->ram_size);
		break;
	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_reset(struct machine *m, _Bool hard) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	xroar_set_keyboard_type(1, xroar_machine_config->keymap);
	if (hard) {
		memset(mp->ram, 0, mp->ram_size);
	}
	/* if (mp->cart && mp->cart->reset) {
		mp->cart->reset(mp->cart);
	} */
	mp->CPU->reset(mp->CPU);
	mc6847_reset(mp->VDG);
	tape_reset(mp->tape_interface);
	tape_set_motor(mp->tape_interface, 1);  // no motor control!
	printer_reset(mp->printer_interface);
	mp->video_attr = 0;
}

#undef WANT_GDB_TARGET

static enum machine_run_state mc10_run(struct machine *m, int ncycles) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;

#ifdef WANT_GDB_TARGET
	if (mp->gdb_interface) {
		switch (gdb_run_lock(mp->gdb_interface)) {
		case gdb_run_state_stopped:
			return machine_run_state_stopped;
		case gdb_run_state_running:
			mp->stop_signal = 0;
			mp->cycles += ncycles;
			mp->CPU->running = 1;
			mp->CPU->run(mp->CPU);
			if (mp->stop_signal != 0) {
				gdb_stop(mp->gdb_interface, mp->stop_signal);
			}
			break;
		case gdb_run_state_single_step:
			m->single_step(m);
			gdb_single_step(mp->gdb_interface);
			break;
		default:
			break;
		}
		gdb_run_unlock(mp->gdb_interface);
		return machine_run_state_ok;
	} else {
#endif
		mp->cycles += ncycles;
		mp->CPU->running = 1;
		mp->CPU->run(mp->CPU);
		return machine_run_state_ok;
#ifdef WANT_GDB_TARGET
	}
#endif
}

static void mc10_instruction_posthook(void *sptr) {
	struct machine_mc10 *mp = sptr;
	mp->single_step = 0;
}

static void mc10_single_step(struct machine *m) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	mp->single_step = 1;
	mp->CPU->running = 0;
	mp->CPU->debug_cpu.instruction_posthook = DELEGATE_AS0(void, mc10_instruction_posthook, mp);
	do {
		mp->CPU->run(mp->CPU);
	} while (mp->single_step);
	mp->CPU->debug_cpu.instruction_posthook.func = NULL;
	mc10_vdg_update_mode(mp);
}

static void mc10_signal(struct machine *m, int sig) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	mc10_vdg_update_mode(mp);
	mp->stop_signal = sig;
	mp->CPU->running = 0;
}

static void mc10_trap(void *sptr) {
        struct machine *m = sptr;
        mc10_signal(m, MACHINE_SIGTRAP);
}

static void mc10_bp_add_n(struct machine *m, struct machine_bp *list, int n, void *sptr) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	for (int i = 0; i < n; i++) {
		if ((list[i].add_cond & BP_MACHINE_ARCH) && xroar_machine_config->architecture != list[i].cond_machine_arch)
			continue;
		if ((list[i].add_cond & BP_CRC_BAS) && (!mp->has_bas || !crclist_match(list[i].cond_crc_bas, mp->crc_bas)))
			continue;
		list[i].bp.handler.sptr = sptr;
		bp_add(mp->bp_session, &list[i].bp);
	}
}

static void mc10_bp_remove_n(struct machine *m, struct machine_bp *list, int n) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	for (int i = 0; i < n; i++) {
		bp_remove(mp->bp_session, &list[i].bp);
	}
}

// Note: MC-10 address decoding appears to consist mostly of the top
// two address lines being fed to a 2-to-4 demux.

static uint8_t mc10_read_byte(struct machine *m, unsigned A, uint8_t D) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;

	switch ((A >> 14) & 3) {
	case 1:
		if (A < (0x4000 + mp->ram_size)) {
			D = mp->ram[A - 0x4000];
		}
		break;

	case 2:
		if (mp->ram_size > 0x4000 && A < (0x4000 + mp->ram_size)) {
			D = mp->ram[A - 0x4000];
		} else {
			// 16K of address space to read the keyboard rows...
			mc10_keyboard_update(mp);
			D = mp->keyboard.rows;
		}
		break;

	case 3:
		D = mp->rom0[A & 0x1fff];
		break;

	default:
		break;
	}

	return D;
}

static void mc10_write_byte(struct machine *m, unsigned A, uint8_t D) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;

	switch ((A >> 14) & 3) {
	case 1:
		if (A < (0x4000 + mp->ram_size)) {
			mp->ram[A - 0x4000] = D;
		}
		break;

	case 2:
		if (mp->ram_size > 0x4000 && A < (0x4000 + mp->ram_size)) {
			mp->ram[A - 0x4000] = D;
		} else {
			unsigned vmode = 0;
			vmode |= (mp->CPU->D & 0x20) ? 0x80 : 0;  // D5 -> GnA
			vmode |= (mp->CPU->D & 0x04) ? 0x40 : 0;  // D2 -> GM2
			vmode |= (mp->CPU->D & 0x08) ? 0x20 : 0;  // D3 -> GM1
			vmode |= (mp->CPU->D & 0x10) ? 0x10 : 0;  // D4 -> GM0
			vmode |= (mp->CPU->D & 0x40) ? 0x08 : 0;  // D6 -> CSS
			mp->video_mode = vmode;
			mp->video_attr = (mp->CPU->D & 0x10) << 6;  // GM0 -> ¬INT/EXT
			sound_set_sbs(mp->snd, 1, D & 0x80);  // D7 -> sound bit
			mc10_vdg_update_mode(mp);
		}
		break;

	default:
		break;
	}
}

static void mc10_op_rts(struct machine *m) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	unsigned int new_pc = m->read_byte(m, mp->CPU->reg_sp + 1, 0) << 8;
	new_pc |= m->read_byte(m, mp->CPU->reg_sp + 2, 0);
	mp->CPU->reg_sp += 2;
	mp->CPU->reg_pc = new_pc;
}

static void mc10_dump_ram(struct machine *m, FILE *fd) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	fwrite(mp->ram, mp->ram_size, 1, fd);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool mc10_set_inverted_text(struct machine *m, int action) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	switch (action) {
	case 0: case 1:
		mp->inverted_text = action;
		break;
	case -2:
		mp->inverted_text = !mp->inverted_text;
		break;
	default:
		break;
	}
	mc6847_set_inverted_text(mp->VDG, mp->inverted_text);
	return mp->inverted_text;
}

static void *mc10_get_interface(struct machine *m, const char *ifname) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	if (0 == strcmp(ifname, "keyboard")) {
		return mp->keyboard.interface;
	} else if (0 == strcmp(ifname, "printer")) {
		return mp->printer_interface;
	} else if (0 == strcmp(ifname, "tape-update-audio")) {
		return mc10_update_tape_input;
	}
	return NULL;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_vdg_hs(void *sptr, _Bool level) {
	(void)sptr;
	(void)level;
}

static void mc10_vdg_fs(void *sptr, _Bool level) {
	struct machine_mc10 *mp = sptr;
	if (level) {
		sound_update(mp->snd);
		mp->frame--;
		if (mp->frame < 0)
			mp->frame = mp->frameskip;
		if (mp->frame == 0) {
			DELEGATE_CALL(mp->vo->vsync);
		}
	}
}

static void mc10_vdg_render_line(void *sptr, uint8_t *data, unsigned burst) {
	struct machine_mc10 *mp = sptr;
	(void)burst;
	struct ntsc_burst *nb = mp->ntsc_burst[0];
	DELEGATE_CALL(mp->vo->render_scanline, data, nb);
}

static void mc10_vdg_fetch_handler(void *sptr, uint16_t A, int nbytes, uint16_t *dest) {
	struct machine_mc10 *mp = sptr;
	if (!dest)
		return;
	uint16_t attr = mp->video_attr;
	for (int i = nbytes; i; i--) {
		uint16_t D;
		if (A < mp->ram_size)
			D = mp->ram[A] | attr;
		else
			D = 0xff | attr;
		A++;
		D |= (D & 0xc0) << 2;  // D7,D6 -> ¬A/S,INV
		*(dest++) = D;
	}
}

static void mc10_vdg_update_mode(void *sptr) {
	struct machine_mc10 *mp = sptr;
	mc6847_set_mode(mp->VDG, mp->video_mode);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_mem_cycle(void *sptr, _Bool RnW, uint16_t A) {
	struct machine_mc10 *mp = sptr;
	struct machine *m = &mp->machine;

	if (RnW) {
		mp->CPU->D = mc10_read_byte(m, A, mp->CPU->D);
		bp_wp_read_hook(mp->bp_session, A);
	} else {
		mc10_write_byte(m, A, mp->CPU->D);
		bp_wp_write_hook(mp->bp_session, A);
	}

	int ncycles = 16;
	mp->cycles -= ncycles;
	if (mp->cycles <= 0)
		mp->CPU->running = 0;
	event_current_tick += ncycles;
        event_run_queue(&MACHINE_EVENT_LIST);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_set_frameskip(struct machine *m, unsigned fskip) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	mp->frameskip = fskip;
}

static void mc10_set_ratelimit(struct machine *m, _Bool ratelimit) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	sound_set_ratelimit(mp->snd, ratelimit);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_keyboard_update(void *sptr) {
	struct machine_mc10 *mp = sptr;
	uint8_t shift_sink = (mp->CPU->port2.out_sink & (1<<1)) ? (1<<6) : 0;
	struct keyboard_state state = {
		.row_source = ~(1<<6) | shift_sink,
		.row_sink = ~(1<<6) | shift_sink,
		.col_source = mp->CPU->port1.out_source,
		.col_sink = mp->CPU->port1.out_sink,
	};
	keyboard_read_matrix(mp->keyboard.interface, &state);
	if (state.row_source & (1<<6)) {
		mp->CPU->port2.in_source |= (1<<1);
	} else {
		mp->CPU->port2.in_source &= ~(1<<1);
	}
	if (state.row_sink & (1<<6)) {
		mp->CPU->port2.in_sink |= (1<<1);
	} else {
		mp->CPU->port2.in_sink &= ~(1<<1);
	}
	mp->keyboard.rows = state.row_sink | 0xc0;
}

static void mc10_update_tape_input(void *sptr, float value) {
	struct machine_mc10 *mp = sptr;
	sound_set_tape_level(mp->snd, value);
	if (value >= 0.5) {
		mp->CPU->port2.in_source &= ~(1<<4);
		mp->CPU->port2.in_sink &= ~(1<<4);
	} else {
		mp->CPU->port2.in_source |= (1<<4);
		mp->CPU->port2.in_sink |= (1<<4);
	}
}

static void mc10_mc6803_port2_postwrite(void *sptr) {
	struct machine_mc10 *mp = sptr;
	uint8_t port2 = MC6801_PORT_VALUE(&mp->CPU->port2);
	tape_update_output(mp->tape_interface, (port2 & 1) ? 0xfc : 0);
}
