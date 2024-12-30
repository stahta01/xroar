/** \file
 *
 *  \brief Tandy MC-10 machine.
 *
 *  \copyright Copyright 2021-2024 Ciaran Anscomb
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

#include "top-config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"
#include "xalloc.h"

#include "ao.h"
#include "breakpoint.h"
#include "cart.h"
#include "crc32.h"
#include "crclist.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc10_cart.h"
#include "mc6801/mc6801.h"
#include "mc6847/mc6847.h"
#include "messenger.h"
#include "ntsc.h"
#include "part.h"
#include "printer.h"
#include "ram.h"
#include "rombank.h"
#include "romlist.h"
#include "serialise.h"
#include "sound.h"
#include "tape.h"
#include "ui.h"
#include "vdg_palette.h"
#include "vo.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_mc10 {
	struct machine machine;

	struct MC6801 *CPU;
	struct MC6847 *VDG;
	struct rombank *ROM0;
	struct ram *RAM0;
	struct ram *RAM1;

	struct vo_interface *vo;
	int frame;  // track frameskip
	struct sound_interface *snd;

	unsigned ram0_inhibit_bit;

	_Bool inverted_text;
	struct mc10_cart *cart;
	unsigned configured_frameskip;
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

	// UI message receipt
	int msgr_client_id;

	// Useful configuration side-effect tracking
	_Bool has_bas;
	uint32_t crc_bas;
};

#define MC10_SER_RAM        (2)
#define MC10_SER_RAM_SIZE   (3)

static const struct ser_struct ser_struct_mc10[] = {
	SER_ID_STRUCT_NEST(1, &machine_ser_struct_data),
	SER_ID_STRUCT_UNHANDLED(MC10_SER_RAM),
	SER_ID_STRUCT_UNHANDLED(MC10_SER_RAM_SIZE),
	SER_ID_STRUCT_ELEM(4, struct machine_mc10, inverted_text),
	SER_ID_STRUCT_ELEM(5, struct machine_mc10, video_mode),
	SER_ID_STRUCT_ELEM(6, struct machine_mc10, video_attr),
};

static _Bool mc10_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool mc10_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data mc10_ser_struct_data = {
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
			mc->tv_input = TV_INPUT_SVIDEO;
			break;
		case TV_NTSC:
		case TV_PAL_M:
			mc->tv_input = TV_INPUT_CMP_KBRW;
			break;
		}
	}
	mc->vdg_type = VDG_6847;
	if (mc->architecture)
		free(mc->architecture);
	mc->architecture = xstrdup("mc10");

	if (mc->ram_init == ANY_AUTO) {
		mc->ram_init = ram_init_clear;
	}

	if (mc->keymap == ANY_AUTO) {
		mc->keymap = dkbd_layout_mc10;
	}
	if (!mc->bas_dfn && !mc->bas_rom) {
		mc->bas_rom = xstrdup("@mc10");
	}
}

static _Bool mc10_is_working_config(struct machine_config *mc) {
	if (!mc)
		return 0;
	if (mc->bas_rom) {
		sds tmp = romlist_find(mc->bas_rom);
		if (!tmp)
			return 0;
		sdsfree(tmp);
	} else {
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool mc10_has_interface(struct part *p, const char *ifname);
static void mc10_attach_interface(struct part *p, const char *ifname, void *intf);

static void mc10_connect_cart(struct part *p);
static void mc10_insert_cart(struct machine *m, struct cart *c);
static void mc10_remove_cart(struct machine *m);

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
static void mc10_vdg_render_line(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data);
static void mc10_vdg_fetch_handler(void *sptr, uint16_t A, int nbytes, uint16_t *dest);
static void mc10_vdg_update_mode(void *sptr);

static void mc10_mem_cycle(void *sptr, _Bool RnW, uint16_t A);

static void mc10_ui_set_keymap(void *, int tag, void *smsg);
static void mc10_ui_set_picture(void *, int tag, void *smsg);
static void mc10_ui_set_tv_input(void *, int tag, void *smsg);
static void mc10_ui_set_text_invert(void *, int tag, void *smsg);
static void *mc10_get_interface(struct machine *m, const char *ifname);
static void mc10_ui_set_frameskip(void *, int tag, void *smsg);
static void mc10_ui_set_ratelimit(void *, int tag, void *smsg);

static void mc10_print_byte(void *);
static void mc10_keyboard_update(void *sptr);
static void mc10_update_tape_input(void *sptr, float value);
static void mc10_mc6803_port2_postwrite(void *sptr);

static void cart_nmi(void *sptr, _Bool level);

static struct machine_bp mc10_print_breakpoint[] = {
	BP_MC10_ROM(.address = 0xf9d0, .handler = DELEGATE_INIT(mc10_print_byte, NULL) ),
};

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

const struct machine_partdb_entry mc10_part = { .partdb_entry = { .name = "mc10", .description = "Tandy | Micro Colour Computer MC-10", .funcs = &mc10_funcs }, .config_complete = mc10_config_complete, .is_working_config = mc10_is_working_config, .cart_arch = "mc10-cart" };

static struct part *mc10_allocate(void) {
        struct machine_mc10 *mp = part_new(sizeof(*mp));
        struct machine *m = &mp->machine;
        struct part *p = &m->part;

        *mp = (struct machine_mc10){0};

	m->has_interface = mc10_has_interface;
	m->attach_interface = mc10_attach_interface;

	m->insert_cart = mc10_insert_cart;
	m->remove_cart = mc10_remove_cart;
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

	m->get_interface = mc10_get_interface;

	m->keyboard.type = dkbd_layout_mc10;

	return p;
}

static void create_ram(struct machine_mc10 *mp) {
	struct machine *m = &mp->machine;
	struct part *p = &m->part;
	struct machine_config *mc = m->config;

	// Bit if a mish-mash, but I'm suggesting here that if you specify <=
	// 8K, assume it's all internal and in multiples of 2K.  Any more and
	// it's in multiples of 4K as external expansion on top of an internal
	// 4K (minimum 12K).  More control over this would be useful

	unsigned ram0_nbanks = 0;
	unsigned ram1_nbanks = 0;
	if (mc->ram >= 12) {
		ram0_nbanks = 2;
		ram1_nbanks = (mc->ram - 4) / 4;
		if (ram1_nbanks > 4)
			ram1_nbanks = 4;
	} else {
		if (mc->ram >= 8) {
			mc->ram = 8;
		}
		ram0_nbanks = mc->ram / 2;
		if (ram0_nbanks < 1)
			ram0_nbanks = 1;
	}

	struct ram_config ram0_config = {
		.d_width = 8,
		.organisation = RAM_ORG(11, 11, 0),
	};
	struct ram_config ram1_config = {
		.d_width = 8,
		.organisation = RAM_ORG(12, 12, 0),
	};

	// Device inhibit is an OR of cartridge SEL line and A12.  Mods to add
	// more internal RAM would change this.
	mp->ram0_inhibit_bit = (1 << 12);
	if (ram0_nbanks > 2) {
		mp->ram0_inhibit_bit = (1 << 13);
	}

	struct ram *ram0 = (struct ram *)part_create("ram", &ram0_config);
	for (unsigned i = 0; i < ram0_nbanks; i++)
		ram_add_bank(ram0, i);
	part_add_component(p, (struct part *)ram0, "RAM0");

	// Specifying 20K implies an external 16K expansion on top of the
	// internal 4K (I can only assume the expansion would preempt any
	// internal mod).
	if (ram1_nbanks > 0) {
		struct ram *ram1 = (struct ram *)part_create("ram", &ram1_config);
		for (unsigned i = 0; i < ram1_nbanks; i++)
			ram_add_bank(ram1, (i + 1) & 3);
		part_add_component(p, (struct part *)ram1, "RAM1");
	}
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

	// RAM
	(void)create_ram(mp);

	// Keyboard
	m->keyboard.type = mc->keymap;
}

static _Bool mc10_finish(struct part *p) {
	struct machine_mc10 *mp = (struct machine_mc10 *)p;
	struct machine *m = &mp->machine;
	struct machine_config *mc = m->config;

	// Interfaces
	mp->vo = xroar.vo_interface;
	mp->snd = xroar.ao_interface->sound_interface;
	mp->tape_interface = xroar.tape_interface;

	mp->tape_interface->default_paused = 1;

	// Find attached parts
	mp->CPU = (struct MC6801 *)part_component_by_id_is_a(p, "CPU", "MC6803");
	mp->VDG = (struct MC6847 *)part_component_by_id_is_a(p, "VDG", "MC6847");
	mp->RAM0 = (struct ram *)part_component_by_id_is_a(p, "RAM0", "ram");
	mp->RAM1 = (struct ram *)part_component_by_id_is_a(p, "RAM1", "ram");

	// Check all required parts are attached
	if (!mp->CPU || !mp->VDG || !mp->RAM0 ||
	    !mp->vo || !mp->snd || !mp->tape_interface) {
		return 0;
	}

	// Register as a messenger client
	mp->msgr_client_id = messenger_client_register();

	// Join the ui messenger groups we're interested in
	ui_messenger_preempt_group(mp->msgr_client_id, ui_tag_picture, MESSENGER_NOTIFY_DELEGATE(mc10_ui_set_picture, mp));
	ui_messenger_preempt_group(mp->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(mc10_ui_set_tv_input, mp));
	ui_messenger_preempt_group(mp->msgr_client_id, ui_tag_vdg_inverse, MESSENGER_NOTIFY_DELEGATE(mc10_ui_set_text_invert, mp));
	ui_messenger_preempt_group(mp->msgr_client_id, ui_tag_keymap, MESSENGER_NOTIFY_DELEGATE(mc10_ui_set_keymap, mp));
	ui_messenger_join_group(mp->msgr_client_id, ui_tag_frameskip, MESSENGER_NOTIFY_DELEGATE(mc10_ui_set_frameskip, mp));
	ui_messenger_join_group(mp->msgr_client_id, ui_tag_ratelimit, MESSENGER_NOTIFY_DELEGATE(mc10_ui_set_ratelimit, mp));

	// ROM
	mp->ROM0 = rombank_new(8, 8192, 1);

	// Microcolour BASIC
	if (mc->bas_rom) {
		sds tmp = romlist_find(mc->bas_rom);
		if (tmp) {
			rombank_load_image(mp->ROM0, 0, tmp, 0);
			sdsfree(tmp);
		}
	}

	// Report and check CRC (Microcolour BASIC)
	rombank_report(mp->ROM0, "mc10", "MicroColour BASIC");
	mp->crc_bas = 0x11fda97e;  // MicroColour BASIC 1.0 (MC-10)
	mp->has_bas = rombank_verify_crc(mp->ROM0, "MicroColour BASIC", -1, "@mc10_compat", xroar.cfg.force_crc_match, &mp->crc_bas);

	// RAM configuration
	{
		unsigned ram0_k = ram_report(mp->RAM0, "mc10", "internal RAM");
		unsigned ram1_k = ram_report(mp->RAM1, "mc10", "external RAM");
		unsigned total_k = ram0_k + ram1_k;
		LOG_DEBUG(1, "\t%uK total RAM\n", total_k);
	}

	// Connect any cartridge part
	mc10_connect_cart(p);

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
        mp->VDG->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, mc10_vdg_render_line, mp);
        mp->VDG->fetch_data = DELEGATE_AS3(void, uint16, int, uint16p, mc10_vdg_fetch_handler, mp);
	ui_update_state(-1, ui_tag_tv_input, mc->tv_input, NULL);
	ui_update_state(-1, ui_tag_vdg_inverse, mp->inverted_text, NULL);

	// Active area is constant
	{
		int x = VDG_tWHS + VDG_tBP + VDG_tLB;
		int y = VDG_ACTIVE_AREA_START + (is_pal ? 24 : 0);
		DELEGATE_SAFE_CALL(mp->vo->set_active_area, x, y, 512, 192);
	}

	// Configure composite video
	switch (mc->tv_standard) {
	case TV_PAL:
	default:
		ui_update_state(-1, ui_tag_cmp_fs, VO_RENDER_FS_14_23753, NULL);  // assumed
		ui_update_state(-1, ui_tag_cmp_fsc, VO_RENDER_FSC_4_43361875, NULL);
		ui_update_state(-1, ui_tag_cmp_system, VO_RENDER_SYSTEM_PAL_I, NULL);
		break;

	case TV_NTSC:
		ui_update_state(-1, ui_tag_cmp_fs, VO_RENDER_FS_14_31818, NULL);
		ui_update_state(-1, ui_tag_cmp_fsc, VO_RENDER_FSC_3_579545, NULL);
		ui_update_state(-1, ui_tag_cmp_system, VO_RENDER_SYSTEM_NTSC, NULL);
		break;

	case TV_PAL_M:
		ui_update_state(-1, ui_tag_cmp_fs, VO_RENDER_FS_14_31818, NULL);
		ui_update_state(-1, ui_tag_cmp_fsc, VO_RENDER_FSC_3_579545, NULL);
		ui_update_state(-1, ui_tag_cmp_system, VO_RENDER_SYSTEM_PAL_M, NULL);
		break;
	}

	// Normal video phase
	DELEGATE_SAFE_CALL(mp->vo->set_cmp_phase_offset, 0);

	// Set up VDG palette in video module
	{
		struct vdg_palette *palette = vdg_palette_by_name(mc->vdg_palette);
		if (!palette) {
			palette = vdg_palette_by_name("ideal");
		}
		DELEGATE_SAFE_CALL(mp->vo->set_cmp_lead_lag, 0., 100.);
		// MC1372 datasheet suggests a conversion gain of 0.6 for the
		// chroma signals.
		for (int c = 0; c < NUM_VDG_COLOURS; c++) {
			float y = palette->palette[c].y;
			float chb = palette->palette[c].chb;
			float b_y = (palette->palette[c].b - chb) * 0.6;
			float r_y = (palette->palette[c].a - chb) * 0.6;
			y = (palette->blank_y - y) / (palette->blank_y - palette->white_y);
			DELEGATE_SAFE_CALL(mp->vo->palette_set_ybr, c, y, b_y, r_y);
		}
	}

	DELEGATE_SAFE_CALL(mp->vo->set_cmp_burst, 1, 0);    // Normal burst (most modes)

	// Tape
	mp->tape_interface->update_audio = DELEGATE_AS1(void, float, mc10_update_tape_input, mp);

	// Keyboard interface
	mp->keyboard.interface = keyboard_interface_new();
	mp->keyboard.interface->update = DELEGATE_AS0(void, mc10_keyboard_update, mp);
	ui_update_state(-1, ui_tag_keymap, m->keyboard.type, NULL);

	// Printer interface
	mp->printer_interface = printer_interface_new();

#ifdef WANT_GDB_TARGET
	// GDB
	/* if (xroar.cfg.gdb) {
		mp->gdb_interface = gdb_interface_new(xroar.cfg.gdb_ip, xroar.cfg.gdb_port, m, mmp>bp_session);
	} */
#endif

	return 1;
}

// Called from part_free(), which handles freeing the struct itself
static void mc10_free(struct part *p) {
	struct machine_mc10 *mp = (struct machine_mc10 *)p;
	// Stop receiving any UI state updates
	messenger_client_unregister(mp->msgr_client_id);
#ifdef WANT_GDB_TARGET
	/* if (mp->gdb_interface) {
	   gdb_interface_free(mp->gdb_interface);
	   } */
#endif
	if (mp->keyboard.interface) {
		keyboard_interface_free(mp->keyboard.interface);
	}
	machine_bp_remove_list(&mp->machine, mc10_print_breakpoint);
	if (mp->printer_interface) {
		printer_interface_free(mp->printer_interface);
	}
	if (mp->bp_session) {
		bp_session_free(mp->bp_session);
	}
	rombank_free(mp->ROM0);
}

static _Bool mc10_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_mc10 *mp = sptr;
        struct part *p = &mp->machine.part;
	size_t length = ser_data_length(sh);
	switch (tag) {
	case MC10_SER_RAM:
		{
			if (!mp->machine.config) {
				return 0;
			}
			if (length != ((unsigned)mp->machine.config->ram * 1024)) {
				LOG_MOD_WARN("mc10", "deserialise: RAM size mismatch\n");
				return 0;
			}
			part_free(part_component_by_id_is_a(p, "RAM0", "ram"));
			part_free(part_component_by_id_is_a(p, "RAM1", "ram"));
			create_ram(mp);

			struct ram *ram0 = (struct ram *)part_component_by_id_is_a(p, "RAM0", "ram");
			ram_ser_read(ram0, sh);

			struct ram *ram1 = (struct ram *)part_component_by_id_is_a(p, "RAM1", "ram");
			if (ram1) {
				for (unsigned i = 0; i <= 3; i++) {
					ram_ser_read_bank(ram1, sh, (i + 1) & 3);
				}
			}
		}
		break;

	case MC10_SER_RAM_SIZE:
		// no-op: RAM is now a sub-component
		break;

	default:
		return 0;
	}
	return 1;
}

static _Bool mc10_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_mc10 *mp = sptr;
	(void)mp;
	(void)sh;
	switch (tag) {
	case MC10_SER_RAM:
	case MC10_SER_RAM_SIZE:
		// no-op: RAM is now a sub-component
		break;

	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool mc10_has_interface(struct part *p, const char *ifname) {
	struct machine_mc10 *mp = (struct machine_mc10 *)p;
	(void)mp;
	(void)ifname;
	return 0;
}

static void mc10_attach_interface(struct part *p, const char *ifname, void *intf) {
	struct machine_mc10 *mp = (struct machine_mc10 *)p;
	(void)mp;
	(void)ifname;
	(void)intf;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_connect_cart(struct part *p) {
	struct machine_mc10 *mp = (struct machine_mc10 *)p;
	struct mc10_cart *cm = (struct mc10_cart *)part_component_by_id_is_a(p, "cart", "mc10-cart");
	mp->cart = cm;
	if (!cm) {
		return;
	}
	assert(cm->read != NULL);
	assert(cm->write != NULL);
	cm->signal_nmi = DELEGATE_AS1(void, bool, cart_nmi, mp);
}

static void mc10_insert_cart(struct machine *m, struct cart *c) {
	mc10_remove_cart(m);
	part_add_component(&m->part, &c->part, "cart");
	mc10_connect_cart(&m->part);
}

static void mc10_remove_cart(struct machine *m) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	if (mp->cart) {
		part_free(&mp->cart->cart.part);
		mp->cart = NULL;
	}
}

static void mc10_reset(struct machine *m, _Bool hard) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	struct machine_config *mc = m->config;
	if (hard) {
		ram_clear(mp->RAM0, mc->ram_init);
		if (mp->RAM1) {
			ram_clear(mp->RAM1, mc->ram_init);
		}
	}
	if (mp->cart && mp->cart->cart.reset) {
		mp->cart->cart.reset(&mp->cart->cart, hard);
	}
	mp->CPU->reset(mp->CPU);
	mc6847_reset(mp->VDG);
	tape_reset(mp->tape_interface);
	tape_set_motor(mp->tape_interface, 1);  // no motor control!
	printer_reset(mp->printer_interface);
	machine_bp_remove_list(m, mc10_print_breakpoint);
	machine_bp_add_list(m, mc10_print_breakpoint, mp);
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

// Notes:
//
// MC-10 address decoding appears to consist mostly of the top two address
// lines being fed to a 2-to-4 demux.
//
// External RAM should be handled by a cart, and wouldn't actually be tied to
// that 2-to-4 demux itself (indeed, it would only act to inhibit it).  Until I
// implement MC-10 carts, this is how it's going to be though.

static uint8_t mc10_read_byte(struct machine *m, unsigned A, uint8_t D) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;

	switch ((A >> 14) & 3) {
	case 1:
		{
			unsigned bank_4k = (A >> 12) & 3;
			if (mp->RAM1 && bank_4k != 0) {
				ram_d8(mp->RAM1, 1, bank_4k, A, 0, &D);
			} else if (!(A & mp->ram0_inhibit_bit)) {
				unsigned bank_2k = (A >> 11) & 3;
				ram_d8(mp->RAM0, 1, bank_2k, A, 0, &D);
			}
		}
		break;

	case 2:
		{
			unsigned bank_4k = (A >> 12) & 3;
			if (mp->RAM1 && bank_4k == 0) {
				ram_d8(mp->RAM1, 1, bank_4k, A, 0, &D);
			} else {
				// up to 16K of address space to read the
				// keyboard rows...
				mc10_keyboard_update(mp);
				D = (D & 0xc0) | mp->keyboard.rows;
			}
		}
		break;

	case 3:
		rombank_d8(mp->ROM0, A, &D);
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
		{
			unsigned bank_4k = (A >> 12) & 3;
			if (mp->RAM1 && bank_4k != 0) {
				ram_d8(mp->RAM1, 0, bank_4k, A, 0, &D);
			} else if (!(A & mp->ram0_inhibit_bit)) {
				unsigned bank_2k = (A >> 11) & 3;
				ram_d8(mp->RAM0, 0, bank_2k, A, 0, &D);
			}
		}
		break;

	case 2:
		{
			unsigned bank_4k = (A >> 12) & 3;
			if (mp->RAM1 && bank_4k == 0) {
				ram_d8(mp->RAM1, 0, bank_4k, A, 0, &D);
			} else {
				// And for writes, up to 16K address space to
				// update video mode...
				unsigned vmode = 0;
				vmode |= (mp->CPU->D & 0x20) ? 0x80 : 0;  // D5 -> GnA
				vmode |= (mp->CPU->D & 0x04) ? 0x40 : 0;  // D2 -> GM2
				vmode |= (mp->CPU->D & 0x08) ? 0x20 : 0;  // D3 -> GM1
				vmode |= (mp->CPU->D & 0x10) ? 0x10 : 0;  // D4 -> GM0
				vmode |= (mp->CPU->D & 0x40) ? 0x08 : 0;  // D6 -> CSS
				mp->video_mode = vmode;
				mp->video_attr = (mp->CPU->D & 0x04) << 8;  // GM2 -> ¬INT/EXT
				sound_set_sbs(mp->snd, 1, D & 0x80);  // D7 -> sound bit
				mc10_vdg_update_mode(mp);
			}
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
	struct ram *ram0 = mp->RAM0;
	for (unsigned bank = 0; bank < ram0->nbanks; bank++) {
		if (ram0->d && ram0->d[bank]) {
			fwrite(ram0->d[bank], ram0->bank_nelems, 1, fd);
		}
	}
	struct ram *ram1 = mp->RAM1;
	if (ram1) {
		for (unsigned i = 0; i < ram1->nbanks; i++) {
			unsigned bank = (i + 1) & 3;
			if (ram1->d && ram1->d[bank]) {
				fwrite(ram1->d[bank], ram1->bank_nelems, 1, fd);
			}
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_ui_set_keymap(void *sptr, int tag, void *smsg) {
	struct machine_mc10 *mp = sptr;
	struct machine *m = &mp->machine;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_keymap);

	int type = m->keyboard.type;
	switch (uimsg->value) {
	case UI_NEXT:
		type = (type == dkbd_layout_mc10) ? dkbd_layout_alice : dkbd_layout_mc10;
		break;
	case UI_AUTO:
		type = m->config->keymap;
		break;
	default:
		type = uimsg->value;
		break;
	}
	m->keyboard.type = type;
	keyboard_set_keymap(mp->keyboard.interface, type);
	uimsg->value = type;
}

static void mc10_ui_set_picture(void *sptr, int tag, void *smsg) {
	struct machine_mc10 *mp = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_picture);
	int picture = ui_msg_adjust_value_range(uimsg, mp->vo->picture, VO_PICTURE_TITLE,
						VO_PICTURE_ZOOMED, VO_PICTURE_UNDERSCAN,
						UI_ADJUST_FLAG_KEEP_AUTO);
	vo_set_viewport(mp->vo, picture);
}

static void mc10_ui_set_tv_input(void *sptr, int tag, void *smsg) {
	struct machine_mc10 *mp = sptr;
	struct machine *m = &mp->machine;
	struct machine_config *mc = m->config;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_tv_input);

	mc->tv_input = ui_msg_adjust_value_range(uimsg, mc->tv_input, TV_INPUT_SVIDEO,
						 TV_INPUT_SVIDEO, TV_INPUT_CMP_KRBW,
						 UI_ADJUST_FLAG_CYCLE);
	switch (mc->tv_input) {
	default:
	case TV_INPUT_SVIDEO:
		vo_set_signal(mp->vo, VO_SIGNAL_SVIDEO);
		break;

	case TV_INPUT_CMP_KBRW:
		vo_set_signal(mp->vo, VO_SIGNAL_CMP);
		DELEGATE_SAFE_CALL(mp->vo->set_cmp_phase, 180);
		break;

	case TV_INPUT_CMP_KRBW:
		vo_set_signal(mp->vo, VO_SIGNAL_CMP);
		DELEGATE_SAFE_CALL(mp->vo->set_cmp_phase, 0);
		break;
	}
}

static void mc10_ui_set_text_invert(void *sptr, int tag, void *smsg) {
	struct machine_mc10 *mp = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_vdg_inverse);

	mp->inverted_text = ui_msg_adjust_value_range(uimsg, mp->inverted_text, 0,
						      0, 1, UI_ADJUST_FLAG_CYCLE);
	mc6847_set_inverted_text(mp->VDG, mp->inverted_text);
}

static void *mc10_get_interface(struct machine *m, const char *ifname) {
	struct machine_mc10 *mp = (struct machine_mc10 *)m;
	if (0 == strcmp(ifname, "keyboard")) {
		return mp->keyboard.interface;
	} else if (0 == strcmp(ifname, "printer")) {
		return mp->printer_interface;
	} else if (0 == strcmp(ifname, "tape-update-audio")) {
		return mc10_update_tape_input;
	} else if (0 == strcmp(ifname, "bp-session")) {
		return mp->bp_session;
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
		vo_vsync(mp->vo, mp->frame == 0);
	}
}

static void mc10_vdg_render_line(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data) {
	struct machine_mc10 *mp = sptr;
	DELEGATE_CALL(mp->vo->render_line, burst, npixels, data);
}

static void mc10_vdg_fetch_handler(void *sptr, uint16_t A, int nbytes, uint16_t *dest) {
	struct machine_mc10 *mp = sptr;
	if (!dest)
		return;
	while (nbytes > 0) {
		unsigned bank_2k = (A >> 11) & 3;
		uint8_t *Vp = ram_a8(mp->RAM0, bank_2k, A, 0);
		uint16_t attr = mp->video_attr;
		// Fetch at most up to the next 16-byte boundary before we
		// recalculate RAM bank address
		int span = 16 - (A & 15);
		if (span > nbytes)
			span = nbytes;
		for (int i = 0; i < span; i++) {
			uint16_t D = Vp ? (Vp[i] | attr) : attr;
			D |= (D & 0xc0) << 2;  // D7,D6 -> ¬A/S,INV
			*(dest++) = D;
		}
		nbytes -= span;
		A += span;
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

	_Bool SEL = 0;
	if (mp->cart) {
		if (RnW) {
			mp->CPU->D = mp->cart->read(mp->cart, A, mp->CPU->D);
		} else {
			mp->CPU->D = mp->cart->write(mp->cart, A, mp->CPU->D);
		}
		SEL = mp->cart->SEL;
	}

	if (RnW) {
		if (!SEL) {
			mp->CPU->D = mc10_read_byte(m, A, mp->CPU->D);
		}
#ifdef WANT_GDB_TARGET
		if (mp->bp_session->wp_read_list)
			bp_wp_read_hook(mp->bp_session, A);
#endif
	} else {
		if (!SEL) {
			mc10_write_byte(m, A, mp->CPU->D);
		}
#ifdef WANT_GDB_TARGET
		if (mp->bp_session->wp_write_list)
			bp_wp_write_hook(mp->bp_session, A);
#endif
	}

	int ncycles = 16;
	mp->cycles -= ncycles;
	if (mp->cycles <= 0)
		mp->CPU->running = 0;
	event_current_tick += ncycles;
        event_run_queue(&MACHINE_EVENT_LIST);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mc10_ui_set_frameskip(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct machine_mc10 *mp = sptr;
	struct ui_state_message *uimsg = smsg;
	mp->configured_frameskip = mp->frameskip = uimsg->value;
}

static void mc10_ui_set_ratelimit(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct machine_mc10 *mp = sptr;
	struct ui_state_message *uimsg = smsg;
	sound_set_ratelimit(mp->snd, uimsg->value);
	if (uimsg->value) {
		mp->frameskip = mp->configured_frameskip;
	} else {
		mp->frameskip = 10;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MC-10 serial printing ROM hook

static void mc10_print_byte(void *sptr) {
	struct machine_mc10 *mp = sptr;
	if (!mp->printer_interface)
		return;
	int byte = MC6801_REG_A(mp->CPU);
	printer_strobe(mp->printer_interface, 0, byte);
	printer_strobe(mp->printer_interface, 1, byte);
	mp->CPU->reg_pc = 0xf9f0;
}

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
	mp->keyboard.rows = state.row_sink & 0x3f;
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

/* Catridge signalling */

static void cart_nmi(void *sptr, _Bool level) {
	struct machine_mc10 *mp = sptr;
	MC6801_NMI_SET(mp->CPU, level);
}
