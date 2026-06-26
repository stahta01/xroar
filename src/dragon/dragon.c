/** \file
 *
 *  \brief Common code for Dragon and Tandy Colour Computer machines.
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
#include "debug.h"
#include "dragon/dragon.h"
#include "dragon/immunity.h"
#include "gdb.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809/hd6309.h"
#include "mc6809/mc6809.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
#include "mc6883.h"
#include "messenger.h"
#include "ntsc.h"
#include "part.h"
#include "printer.h"
#include "ram.h"
#include "rombank.h"
#include "romlist.h"
#include "serialise.h"
#include "sound.h"
#include "symtab.h"
#include "tape.h"
#include "ui.h"
#include "vdg_palette.h"
#include "vo.h"
#include "xconfig.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define DRAGON_SER_RAM      (2)
#define DRAGON_SER_RAM_SIZE (3)
#define DRAGON_SER_RAM_MASK (4)

static const struct ser_struct ser_struct_dragon[] = {
	SER_ID_STRUCT_NEST(1, &machine_ser_struct_data),
	SER_ID_STRUCT_UNHANDLED(DRAGON_SER_RAM),
	SER_ID_STRUCT_UNHANDLED(DRAGON_SER_RAM_SIZE),
	SER_ID_STRUCT_UNHANDLED(DRAGON_SER_RAM_MASK),
        SER_ID_STRUCT_ELEM(5, struct dragon, inverted_text),
};

static bool dragon_read_elem(void *sptr, struct ser_handle *sh, int tag);
static bool dragon_write_elem(void *sptr, struct ser_handle *sh, int tag);

const struct ser_struct_data dragon_ser_struct_data = {
	.elems = ser_struct_dragon,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_dragon),
	.read_elem = dragon_read_elem,
	.write_elem = dragon_write_elem,
};

static struct xconfig_enum dragon_sam_list[] = {
	{ XC_ENUM_INT("74ls783", DRAGON_SAM_74LS783, "74LS783") },
	{ XC_ENUM_INT("783", DRAGON_SAM_74LS783, NULL) },
	{ XC_ENUM_INT("sn74ls783", DRAGON_SAM_74LS783, NULL) },
	{ XC_ENUM_INT("mc6883", DRAGON_SAM_74LS783, NULL) },
	{ XC_ENUM_INT("74ls785", DRAGON_SAM_74LS785, "74LS785") },
	{ XC_ENUM_INT("785", DRAGON_SAM_74LS785, NULL) },
	{ XC_ENUM_INT("sn74ls785", DRAGON_SAM_74LS785, NULL) },
	{ XC_ENUM_INT("samx8", DRAGON_SAM_SAMX8, "SAMx8") },
};

static struct xconfig_option const dragon_options[] = {
	{ XCO_SET_ENUM("sam", struct dragon, option.sam_variant, dragon_sam_list) },
	{ XCO_SET_BOOL("immunity", struct dragon, option.immunity) },
};

static const struct xconfig_set dragon_option_set = {
	ARRAY_N_ELEMENTS(dragon_options),
	dragon_options
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Set a ROM configuration to a default value if not "defined"
void dragon_set_default_rom(bool dfn, char **romp, const char *dfl) {
	if (!dfn && romp && !*romp && dfl) {
		*romp = xstrdup(dfl);
	}
}

void dragon_config_complete(struct machine_config *mc) {
	if (mc->cpu != CPU_MC6809 && mc->cpu != CPU_HD6309) {
		mc->cpu = CPU_MC6809;
	}
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
	if (mc->vdg_type == ANY_AUTO)
		mc->vdg_type = VDG_6847;
	if (mc->vdg_type != VDG_6847 && mc->vdg_type != VDG_6847T1)
		mc->vdg_type = VDG_6847;

	if (mc->ram_init == ANY_AUTO) {
		mc->ram_init = ram_init_pattern;
	}

	if (mc->keymap == ANY_AUTO) {
		mc->keymap = dkbd_layout_dragon;
	}

	// Determine a default DOS cartridge if necessary
	if (!mc->default_cart_dfn && !mc->default_cart) {
		struct cart_config *cc = cart_find_working_dos(mc);
		if (cc)
			mc->default_cart = xstrdup(cc->name);
	}
}

bool dragon_is_working_config(struct machine_config *mc) {
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

// Although most callers will check RAM size themselves, we still do so here,
// as it's called during deserialisation.

void dragon_verify_ram_size(struct machine_config *mc) {
	// Validate requested total RAM
	mc->ram = int_floor_list(mc->ram, 64, 0, 4, 8, 16, 32, 64, 512, 2048, -1);

	// Pick RAM org based on requested total RAM if not specified
	if (mc->ram_org == ANY_AUTO) {
		if (mc->ram < 16) {
			mc->ram_org = RAM_ORG_4Kx1;
		} else if (mc->ram < 32) {
			mc->ram_org = RAM_ORG_16Kx1;
		} else if (mc->ram < 64) {
			mc->ram_org = RAM_ORG_32Kx1;
		} else if (mc->ram < 256) {
			mc->ram_org = RAM_ORG_64Kx1;
		} else {
			mc->ram_org = RAM_ORG_256Kx1;
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon_create_ram(struct dragon *);

static void dragon_connect_cart(struct part *p);
static void dragon_insert_cart(struct machine *m, struct cart *c);
static void dragon_remove_cart(struct machine *m);

static enum machine_run_state dragon_run(struct machine *m, int ncycles);
static void dragon_single_step(struct machine *m);
static void dragon_signal(struct machine *m, int sig);

static void dragon_ui_set_keymap(void *, int tag, void *smsg);
static bool dragon_set_pause(struct machine *m, int state);
static void dragon_ui_set_picture(void *, int tag, void *smsg);
static void dragon_ui_set_tv_input(void *, int tag, void *smsg);
static void dragon_ui_set_text_invert(void *, int tag, void *smsg);
static void *dragon_get_interface(struct machine *m, const char *ifname);
static void dragon_ui_set_frameskip(void *, int tag, void *smsg);
static void dragon_ui_set_ratelimit(void *, int tag, void *smsg);

static uint8_t dragon_read_byte(struct machine *m, unsigned A, uint8_t D);
static void dragon_write_byte(struct machine *m, unsigned A, uint8_t D);
static void dragon_op_rts(struct machine *m);
static void dragon_dump_ram(struct machine *m, FILE *fd);
static int32_t dragon_get_symbol(struct machine *m, const char *label);
static void dragon_add_breakpoint(struct machine *m, uint32_t A,
				  DELEGATE_T2(void, bool, uint32) handler);
static void dragon_remove_breakpoint(struct machine *m, int32_t A,
				     DELEGATE_T2(void, bool, uint32) handler);
static void dragon_add_watchpoint(struct machine *m, bool RnW,
				  uint32_t Astart, uint32_t Aend,
				  DELEGATE_T2(void, bool, uint32) handler);
static void dragon_remove_watchpoint(struct machine *m, int RnW,
				     int32_t Astart, uint32_t Aend,
				     DELEGATE_T2(void, bool, uint32) handler);

static void joystick_update(void *sptr);
static void update_sound_mux_source(void *sptr);

static void single_bit_feedback(void *sptr, bool level);
static void update_audio_from_tape(void *sptr, float value);
static void cart_firq(void *sptr, bool level);
static void cart_nmi(void *sptr, bool level);
static void cart_halt(void *sptr, bool level);
static void vdg_render_line(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data);
static void printer_ack(void *sptr, bool ack);

static void cpu_cycle(void *sptr, bool RnW, uint16_t A);
static void dragon_instruction_posthook(void *sptr);
static void vdg_fetch_handler(void *sptr, uint16_t A, int nbytes, uint16_t *dest);
static void vdg_fetch_handler_chargen(void *sptr, uint16_t A, int nbytes, uint16_t *dest);

static void pia0a_data_preread(void *sptr);
#define pia0a_data_postwrite NULL
#define pia0a_control_postwrite update_sound_mux_source
#define pia0b_data_preread dragon_keyboard_update
#define pia0b_data_postwrite NULL
#define pia0b_control_postwrite update_sound_mux_source

#define pia1a_data_preread NULL
static void pia1a_data_postwrite(void *sptr);
static void pia1a_control_postwrite(void *sptr);
static void pia1b_data_preread(void *sptr);
static void pia1b_control_postwrite(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Dragon part creation

void dragon_allocate_common(struct dragon *md) {
	struct machine *m = &md->public;

	m->has_interface = dragon_has_interface;
	m->attach_interface = dragon_attach_interface;

	m->insert_cart = dragon_insert_cart;
	m->remove_cart = dragon_remove_cart;
	m->reset = dragon_reset;
	m->run = dragon_run;
	m->single_step = dragon_single_step;
	m->signal = dragon_signal;

	m->set_pause = dragon_set_pause;
	m->get_interface = dragon_get_interface;

	m->read_byte = dragon_read_byte;
	m->write_byte = dragon_write_byte;
	m->op_rts = dragon_op_rts;
	m->dump_ram = dragon_dump_ram;
	m->debug.get_symbol = dragon_get_symbol;
	m->debug.add_breakpoint = dragon_add_breakpoint;
	m->debug.remove_breakpoint = dragon_remove_breakpoint;
	m->debug.add_watchpoint = dragon_add_watchpoint;
	m->debug.remove_watchpoint = dragon_remove_watchpoint;

	m->keyboard.type = dkbd_layout_dragon;

	md->option.sam_variant = ANY_AUTO;
}

void dragon_initialise_common(struct dragon *md, struct machine_config *mc) {
	struct machine *m = &md->public;

	m->config = mc;

	// Dragon-specific options
	xconfig_parse_list_struct(&dragon_option_set, mc->opts, md);

	dragon_verify_ram_size(mc);

	// SAM
	if (md->option.sam_variant == ANY_AUTO) {
		if (mc->ram == 512) {
			md->option.sam_variant = DRAGON_SAM_SAMX8;
			mc->ram = 0;
		} else {
			md->option.sam_variant = DRAGON_SAM_74LS783;
		}
	}
	switch (md->option.sam_variant) {
	case DRAGON_SAM_74LS783:
	default:
		part_add_component(&m->part, part_create("SN74LS783", NULL), "SAM");
		break;
	case DRAGON_SAM_74LS785:
		part_add_component(&m->part, part_create("SN74LS785", NULL), "SAM");
		break;
	case DRAGON_SAM_SAMX8:
		part_add_component(&m->part, part_create("SAMx8", NULL), "SAM");
		break;
	}

	// CPU
	part_add_component(&m->part, part_create((mc->cpu == CPU_HD6309) ? "HD6309" : "MC6809", NULL), "CPU");

	// PIAs
	part_add_component(&m->part, part_create("MC6821", NULL), "PIA0");
	part_add_component(&m->part, part_create("MC6821", NULL), "PIA1");

	// VDG
	part_add_component(&m->part, part_create("MC6847", (mc->vdg_type == VDG_6847T1 ? "6847T1" : "6847")), "VDG");

	// iMMUnity
	if (mc->ram == 2048) {
		if (md->option.sam_variant != DRAGON_SAM_SAMX8) {
			mc->ram = 64;
		}
		md->option.immunity = 1;
	}
	if (md->option.immunity) {
		part_add_component(&m->part, part_create("immunity", NULL), "iMMUnity");
	}

	// RAM
	dragon_create_ram(md);

	// Keyboard
	m->keyboard.type = mc->keymap;
}

bool dragon_finish_common(struct dragon *md) {
	struct machine *m = &md->public;
	struct part *p = &m->part;
	struct machine_config *mc = m->config;

	// Interfaces
	md->vo = xroar.vo_interface;
	md->snd = xroar.ao_interface->sound_interface;
	md->tape_interface = xroar.tape_interface;

	md->tape_interface->default_paused = 0;

	// Find attached parts
	md->SAM = (struct MC6883 *)part_component_by_id_is_a(p, "SAM", "SN74LS783");
	md->CPU = (struct MC6809 *)part_component_by_id_is_a(p, "CPU", "MC6809");
	md->PIA0 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA0", "MC6821");
	md->PIA1 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA1", "MC6821");
	md->VDG = (struct MC6847 *)part_component_by_id_is_a(p, "VDG", "MC6847");
	md->RAM = (struct ram *)part_component_by_id_is_a(p, "RAM", "ram");
	md->immunity = (struct immunity *)part_component_by_id_is_a(p, "iMMUnity", "immunity");

	// Check all required parts are attached
	if (!md->SAM || !md->CPU || !md->PIA0 || !md->PIA1 || !md->VDG ||
	    !md->RAM || !md->vo || !md->snd || !md->tape_interface) {
		return 0;
	}

	if (md->immunity) {
		md->immunity->dragon = md;
	}

	md->SAM->CPUD = &md->CPU->D;

	// Register as a messenger client
	md->msgr_client_id = messenger_client_register();

	// Join the ui messenger groups we're interested in
	ui_messenger_preempt_group(md->msgr_client_id, ui_tag_picture, MESSENGER_NOTIFY_DELEGATE(dragon_ui_set_picture, md));
	ui_messenger_preempt_group(md->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(dragon_ui_set_tv_input, md));
	ui_messenger_preempt_group(md->msgr_client_id, ui_tag_vdg_inverse, MESSENGER_NOTIFY_DELEGATE(dragon_ui_set_text_invert, md));
	ui_messenger_preempt_group(md->msgr_client_id, ui_tag_keymap, MESSENGER_NOTIFY_DELEGATE(dragon_ui_set_keymap, md));
	ui_messenger_join_group(md->msgr_client_id, ui_tag_frameskip, MESSENGER_NOTIFY_DELEGATE(dragon_ui_set_frameskip, md));
	ui_messenger_join_group(md->msgr_client_id, ui_tag_ratelimit, MESSENGER_NOTIFY_DELEGATE(dragon_ui_set_ratelimit, md));

	bool is_dragon32 = strcmp(mc->architecture, "dragon32") == 0;

	memcpy(&m->debug.cpu, &md->CPU->debug.cpu, sizeof(m->debug.cpu));
	m->debug.target = debug_target_new(m->debug.cpu.architecture, m->debug.cpu.endian);
	debug_target_add_part(m->debug.target, NULL, &md->CPU->debug.part);
#ifdef WANT_GDB_TARGET
	if (xroar.cfg.debug.gdb_pseudo_regs) {
		debug_target_add_part(m->debug.target, NULL, &md->SAM->debug.part);
		debug_target_add_part(m->debug.target, "pia0", &md->PIA0->debug.part);
		debug_target_add_part(m->debug.target, "pia1", &md->PIA1->debug.part);
	}
	m->debug.target_xml = debug_target_xml(m->debug.target);
#endif

	md->has_combined = md->has_extbas = md->has_bas = md->has_altbas = 0;
	md->crc_combined = md->crc_extbas = md->crc_bas = md->crc_altbas = 0;
	md->has_ext_charset = 0;
	md->crc_ext_charset = 0;

	if (mc->ext_charset_rom) {
		md->ext_charset = rombank_new(8, 4096, 1);

		sds tmp = romlist_find(mc->ext_charset_rom);
		if (tmp) {
			rombank_load_image(md->ext_charset, 0, tmp, 0);
			sdsfree(tmp);
		}

		if (!md->ext_charset->d[0]) {
			rombank_free(md->ext_charset);
			md->ext_charset = NULL;
		} else {
			rombank_report(md->ext_charset, p->partdb->name, "External character set");
			md->crc_ext_charset = md->ext_charset->combined_crc32;
			md->has_ext_charset = 1;
		}
	}

	// RAM configuration
	ram_report(md->RAM, p->partdb->name, "motherboard RAM");

	// But if the SAM implementation provides RAM, we assume it completely
	// overrides the on-board RAM, so from now on that's what we use.
	if (md->SAM->RAM) {
		md->RAM = md->SAM->RAM;
	}

	// Connect any cartridge part
	dragon_connect_cart(p);

	// CPU cycle handling
	if (md->immunity) {
		md->CPU->mem_cycle = DELEGATE_AS2(void, bool, uint16, immunity_cpu_cycle, md->immunity);
	} else {
		md->CPU->mem_cycle = DELEGATE_AS2(void, bool, uint16, cpu_cycle, md);
	}

	// SAM VDG update handler
	md->SAM->vdg_update = DELEGATE_AS0(void, mc6847_update, md->VDG);

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
	md->PIA1->b.data_postwrite = DELEGATE_AS0(void, dragon_pia1b_data_postwrite, md);
	md->PIA1->b.control_postwrite = DELEGATE_AS0(void, pia1b_control_postwrite, md);

	// Single-bit sound feedback
	md->snd->sbs_feedback = DELEGATE_AS1(void, bool, single_bit_feedback, md);

	// VDG
	md->use_ntsc_burst_mod = (mc->tv_standard != TV_PAL);

	md->VDG->signal_hs = DELEGATE_AS1(void, bool, dragon_vdg_hs, md);
	md->VDG->signal_fs = DELEGATE_AS1(void, bool, dragon_vdg_fs, md);
	md->VDG->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vdg_render_line, md);
	md->VDG->fetch_data = DELEGATE_AS3(void, uint16, int, uint16p, vdg_fetch_handler, md);
	ui_update_state(-1, ui_tag_tv_input, mc->tv_input, NULL);
	ui_update_state(-1, ui_tag_vdg_inverse, md->inverted_text, NULL);

	// Active area is constant
	DELEGATE_SAFE_CALL(md->vo->set_active_area, VDG_tWHS + VDG_tBP + VDG_tLB, VDG_ACTIVE_AREA_START, 512, 192);

	// Default composite sample frequency
	ui_update_state(-1, ui_tag_cmp_fs, VO_RENDER_FS_14_31818, NULL);

	switch (mc->tv_standard) {
	case TV_PAL:
	default:
		ui_update_state(-1, ui_tag_cmp_fsc, VO_RENDER_FSC_4_43361875, NULL);
		ui_update_state(-1, ui_tag_cmp_system, VO_RENDER_SYSTEM_PAL_I, NULL);
		break;

	case TV_NTSC:
		ui_update_state(-1, ui_tag_cmp_fsc, VO_RENDER_FSC_3_579545, NULL);
		ui_update_state(-1, ui_tag_cmp_system, VO_RENDER_SYSTEM_NTSC, NULL);
		break;

	case TV_PAL_M:
		ui_update_state(-1, ui_tag_cmp_fsc, VO_RENDER_FSC_3_579545, NULL);
		ui_update_state(-1, ui_tag_cmp_system, VO_RENDER_SYSTEM_PAL_M, NULL);
		break;
	}

	// Normal video phase
	DELEGATE_SAFE_CALL(md->vo->set_cmp_phase_offset, 0);

	// Set up VDG palette in video module
	{
		struct vdg_palette *palette = vdg_palette_by_name(mc->vdg_palette);
		if (!palette) {
			palette = vdg_palette_by_name("ideal");
		}
		// Lead/lag 90° (Dragon; LM1889N) or 100° (CoCo; MC1372)
		DELEGATE_SAFE_CALL(md->vo->set_cmp_lead_lag, 0., md->is_dragon ? 90. : 100.);
		for (int c = 0; c < NUM_VDG_COLOURS; c++) {
			float y = palette->palette[c].y;
			float chb = palette->palette[c].chb;
			// Both the LM1889 and MC1372 datasheets suggest a
			// conversion gain of 0.6 for the chroma inputs.
			float b_y = (palette->palette[c].b - chb) * 0.6;
			float r_y = (palette->palette[c].a - chb) * 0.6;
			y = (palette->blank_y - y) / (palette->blank_y - palette->white_y);
			DELEGATE_SAFE_CALL(md->vo->palette_set_ybr, c, y, b_y, r_y);
		}
	}

	// Normal burst (most modes)
	DELEGATE_SAFE_CALL(md->vo->set_cmp_burst_br, 1, -0.25, 0.0);

	// Modified bursts (coco hi-res css=1)
	switch (mc->tv_standard) {
	case TV_NTSC:
	case TV_PAL:
	default:
		// In an NTSC machine, a timer circuit provides a modified
		// burst in hi-res otherwise-mono modes in order to generate
		// red & blue hues.  Pulling øA low sets the burst along that
		// negative axis - +80° relative to the normal burst along
		// negative øB.
		DELEGATE_SAFE_CALL(md->vo->set_cmp_burst_br, 2,  0.0,  -1.5);
		DELEGATE_SAFE_CALL(md->vo->set_cmp_burst_br, 3, -0.25, -1.5);
		break;

	case TV_PAL_M:
		// PAL-M; not sure of the measurements here, or how the
		// Brazilian clones generated the swinging burst.  Youtube
		// videos seem to show green/blue artefacts (not green/purple).
		DELEGATE_SAFE_CALL(md->vo->set_cmp_burst, 2, 0);
		DELEGATE_SAFE_CALL(md->vo->set_cmp_burst, 3, 0);
		break;
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
		// Pull-up resistor on centronics !BUSY (PIA1 PB0)
		md->PIA1->b.in_source |= (1<<0);
	}

	md->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, md);

	// Defaults: Dragon 64 with 64K
	md->unexpanded_dragon32 = 0;
	md->relaxed_pia0_decode = 0;
	md->relaxed_pia1_decode = 0;

	if (!md->is_dragon) {
		md->relaxed_pia0_decode = 1;
		md->relaxed_pia1_decode = 1;
	}

	if (is_dragon32 && mc->ram <= 32) {
		md->unexpanded_dragon32 = 1;
		md->relaxed_pia0_decode = 1;
		md->relaxed_pia1_decode = 1;
	}

	// Keyboard interface
	md->keyboard.interface = keyboard_interface_new();
	if (md->is_dragon) {
		keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_dragon_32k_basic);
	} else {
		keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_coco_basic);
	}
	ui_update_state(-1, ui_tag_keymap, m->keyboard.type, NULL);

	// Printer interface
	md->printer_interface = printer_interface_new();
	if (md->is_dragon) {
		md->printer_interface->signal_ack = DELEGATE_AS1(void, bool, printer_ack, md);
	}

	md->tape_update_audio = DELEGATE_AS1(void, float, update_audio_from_tape, md);

#ifdef WANT_GDB_TARGET
	// GDB
	if (xroar.cfg.debug.gdb) {
		md->gdb_interface = gdb_interface_new(xroar.cfg.debug.gdb_ip, xroar.cfg.debug.gdb_port, m);
	}
#endif

	// XXX until we serialise sound information
	update_sound_mux_source(md);
	sound_set_mux_enabled(md->snd, PIA_VALUE_CB2(md->PIA1));

	// Until we serialise vdg_pal events:
	mc6847_unpause(md->VDG);

	return 1;
}

// Called from part_free(), which handles freeing the struct itself
void dragon_free_common(struct part *p) {
	struct dragon *md = (struct dragon *)p;
	// Stop receiving any UI state updates
	messenger_client_unregister(md->msgr_client_id);
	bp_breakpoint_remove(&md->breakpoint_set, -1, DELEGATE_AS2(void, bool, uint32, NULL, NULL));  // remove all breakpoints
	debug_target_free(md->public.debug.target);
#ifdef WANT_GDB_TARGET
	if (md->gdb_interface) {
		gdb_interface_free(md->gdb_interface);
	}
	sdsfree(md->public.debug.target_xml);
#endif
	if (md->keyboard.interface) {
		keyboard_interface_free(md->keyboard.interface);
	}
	if (md->printer_interface) {
		printer_interface_free(md->printer_interface);
	}
	rombank_free(md->ext_charset);
}

static bool dragon_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct dragon *md = sptr;
	struct machine *m = &md->public;
	struct part *p = &m->part;
	size_t length = ser_data_length(sh);
	switch (tag) {
	case DRAGON_SER_RAM:
		{
			if (!md->public.config) {
				return 0;
			}
			dragon_verify_ram_size(md->public.config);
			if (length != ((unsigned)md->public.config->ram * 1024)) {
				LOG_MOD_WARN(p->partdb->name, "deserialise: RAM size mismatch\n");
				return 0;
			}
			part_free(part_component_by_id_is_a(p, "RAM", "ram"));
			dragon_create_ram(md);
			struct ram *ram = (struct ram *)part_component_by_id_is_a(p, "RAM", "ram");
			assert(ram != NULL);
			ram_ser_read(ram, sh);
		}
		break;

	case DRAGON_SER_RAM_SIZE:
	case DRAGON_SER_RAM_MASK:
		// no-op: RAM is now a sub-component
		break;

	default:
		return 0;
	}
	return 1;
}

static bool dragon_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct dragon *md = sptr;
	(void)md;
	(void)sh;
	switch (tag) {
	case DRAGON_SER_RAM:
	case DRAGON_SER_RAM_SIZE:
	case DRAGON_SER_RAM_MASK:
		// no-op: RAM is now a sub-component
		break;

	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Memory expansions create their own RAM.  This may mean we're called on to
// create 0K of on-board RAM and that's fine.

static void dragon_create_ram(struct dragon *md) {
	struct machine *m = &md->public;
	struct part *p = &m->part;
	struct machine_config *mc = m->config;

	if (mc->ram == 0) {
		// This actually specifies 1-byte DRAMs, but that's small
		// enough for rounding to report it as 0K in the report, and we
		// won't actually end up adding any banks of it.
		mc->ram_org = RAM_ORG(1, 1, 0);
	}

	struct ram_config ram_config = {
		.d_width = 8,
		.organisation = mc->ram_org,
	};
	struct ram *ram = (struct ram *)part_create("ram", &ram_config);

	unsigned bank_size = ram->bank_nelems / 1024;
	if (bank_size == 0)
		bank_size = 1;
	unsigned ram_k = mc->ram >= 512 ? 512 : mc->ram;
	unsigned nbanks = ram_k / bank_size;
	if (nbanks > 2)
		nbanks = 2;

	for (unsigned i = 0; i < nbanks; i++)
		ram_add_bank(ram, i);

	part_add_component(p, (struct part *)ram, "RAM");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool dragon_has_interface(struct part *p, const char *ifname) {
	struct dragon *md = (struct dragon *)p;

	struct cart *c = md->cart;
	if (c) {
		if (c->has_interface) {
			return c->has_interface(c, ifname);
		}
	}

	return 0;
}

void dragon_attach_interface(struct part *p, const char *ifname, void *intf) {
	struct dragon *md = (struct dragon *)p;

	struct cart *c = md->cart;
	if (c) {
		if (c->attach_interface) {
			c->attach_interface(c, ifname, intf);
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon_connect_cart(struct part *p) {
	struct dragon *md = (struct dragon *)p;
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
	struct dragon *md = (struct dragon *)m;
	part_free((struct part *)md->cart);
	md->cart = NULL;
}

void dragon_reset(struct machine *m, bool hard) {
	struct dragon *md = (struct dragon *)m;
	struct machine_config *mc = m->config;
	if (md->immunity) {
		immunity_reset(md->immunity, hard);
	}
	if (hard) {
		ram_clear(md->RAM, mc->ram_init);
	}
	mc6821_reset(md->PIA0);
	mc6821_reset(md->PIA1);
	if (md->cart && md->cart->reset) {
		md->cart->reset(md->cart, hard);
	}
	md->SAM->reset(md->SAM);
	md->CPU->reset(md->CPU);
	mc6847_reset(md->VDG);
	tape_reset(md->tape_interface);
	printer_reset(md->printer_interface);
}

static enum machine_run_state dragon_run(struct machine *m, int ncycles) {
	struct dragon *md = (struct dragon *)m;

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
	struct dragon *md = (struct dragon *)m;
	md->single_step = 1;
	md->CPU->running = 0;
	md->CPU->instruction_posthook = DELEGATE_AS0(void, dragon_instruction_posthook, md);
	do {
		md->CPU->run(md->CPU);
	} while (md->single_step);
	md->CPU->instruction_posthook.func = NULL;
	dragon_update_vdg_mode(md);
}

/*
 * Stop emulation and set stop_signal to reflect the reason.
 */

static void dragon_signal(struct machine *m, int sig) {
	struct dragon *md = (struct dragon *)m;
	dragon_update_vdg_mode(md);
	md->stop_signal = sig;
	md->CPU->running = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon_ui_set_keymap(void *sptr, int tag, void *smsg) {
	struct dragon *md = sptr;
	struct machine *m = &md->public;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_keymap);

	int type = m->keyboard.type;
	switch (uimsg->value) {
	case UI_NEXT:
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
	case UI_AUTO:
		type = m->config->keymap;
		break;
	default:
		type = uimsg->value;
		break;
	}
	m->keyboard.type = type;
	keyboard_set_keymap(md->keyboard.interface, type);
	uimsg->value = type;
}

static bool dragon_set_pause(struct machine *m, int state) {
	struct dragon *md = (struct dragon *)m;
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

static void dragon_ui_set_picture(void *sptr, int tag, void *smsg) {
	struct dragon *md = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_picture);
	int picture = ui_msg_adjust_value_range(uimsg, md->vo->picture, VO_PICTURE_TITLE,
						VO_PICTURE_ZOOMED, VO_PICTURE_UNDERSCAN,
						UI_ADJUST_FLAG_KEEP_AUTO);
	vo_set_viewport(md->vo, picture);
}

static void dragon_ui_set_tv_input(void *sptr, int tag, void *smsg) {
	struct dragon *md = sptr;
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_tv_input);

	mc->tv_input = ui_msg_adjust_value_range(uimsg, mc->tv_input, TV_INPUT_SVIDEO,
						 TV_INPUT_SVIDEO, TV_INPUT_CMP_KRBW,
						 UI_ADJUST_FLAG_CYCLE);
	switch (mc->tv_input) {
	default:
	case TV_INPUT_SVIDEO:
		vo_set_signal(md->vo, VO_SIGNAL_SVIDEO);
		break;

	case TV_INPUT_CMP_KBRW:
		vo_set_signal(md->vo, VO_SIGNAL_CMP);
		DELEGATE_SAFE_CALL(md->vo->set_cmp_phase, 180);
		break;

	case TV_INPUT_CMP_KRBW:
		vo_set_signal(md->vo, VO_SIGNAL_CMP);
		DELEGATE_SAFE_CALL(md->vo->set_cmp_phase, 0);
		break;
	}
}

static void dragon_ui_set_text_invert(void *sptr, int tag, void *smsg) {
	struct dragon *md = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_vdg_inverse);

	md->inverted_text = ui_msg_adjust_value_range(uimsg, md->inverted_text, 0,
						      0, 1, UI_ADJUST_FLAG_CYCLE);
	mc6847_set_inverted_text(md->VDG, md->inverted_text);
}

/*
 * Device inspection.
 */

/* Note, this is SLOW.  Could be sped up by maintaining a hash by component
 * name, but will only ever be used outside critical path, so don't bother for
 * now. */

static void *dragon_get_interface(struct machine *m, const char *ifname) {
	struct dragon *md = (struct dragon *)m;
	if (0 == strcmp(ifname, "cart")) {
		return md->cart;
	} else if (0 == strcmp(ifname, "keyboard")) {
		return md->keyboard.interface;
	} else if (0 == strcmp(ifname, "printer")) {
		return md->printer_interface;
	} else if (0 == strcmp(ifname, "tape-update-audio")) {
		return &md->tape_update_audio;
	}
	return NULL;
}

static void dragon_ui_set_frameskip(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct dragon *md = sptr;
	struct ui_state_message *uimsg = smsg;
	md->configured_frameskip = md->frameskip = uimsg->value;
}

static void dragon_ui_set_ratelimit(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct dragon *md = sptr;
	struct ui_state_message *uimsg = smsg;
	sound_set_ratelimit(md->snd, uimsg->value);
	if (uimsg->value) {
		md->frameskip = md->configured_frameskip;
	} else {
		md->frameskip = 10;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used when single-stepping.

static void dragon_instruction_posthook(void *sptr) {
	struct dragon *md = sptr;
	md->single_step = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// CPU cycles

static void read_byte(struct dragon *md, unsigned A);
static void write_byte(struct dragon *md, unsigned A);

// Advance clock and run scheduled events
extern inline void dragon_advance_clock(struct dragon *md, int ncycles);

// The standard CPU cycle handling delegate.  May be overridden.
//
// See dragon/dragon.h for information on what CPU->cpu_cycle() is expected to
// do.

static void cpu_cycle(void *sptr, bool RnW, uint16_t A) {
	struct dragon *md = sptr;

	// Check watchpoints
	bp_check_watchpoints(&md->watchpoint_set, RnW, A);

	// SAM decode / timing
	int ncycles = md->SAM->mem_cycle(md->SAM, RnW, A);

	// Advance clock, collect IRQs
	if (ncycles && !md->clock_inhibit) {
		dragon_advance_clock(md, ncycles);
		MC6809_IRQ_SET(md->CPU, md->PIA0->a.irq || md->PIA0->b.irq);
		MC6809_FIRQ_SET(md->CPU, md->PIA1->a.irq || md->PIA1->b.irq);
	}

	// Common cycle handling
	dragon_cpu_cycle(md, RnW, A, md->SAM->Zrow, md->SAM->Zcol);
}

// Common routine called by cpu_cycle() (or override) to access RAM and devices
// for a CPU cycle.
//
// This in turn calls read_byte() and write_byte() as appropriate.  At the
// moment these have variations hard coded for derived machines.  It would be
// nice to abstract those somehow, but the call graph is already somewhat
// convoluted...

void dragon_cpu_cycle(struct dragon *md, bool RnW, uint16_t A, unsigned Zrow, unsigned Zcol) {
	md->Dread = 0xff;
	if (md->SAM->nWE) {
		if (md->SAM->RAS0) {
			ram_d8(md->RAM, 1, 0, Zrow, Zcol, &md->Dread);
		}
		if (md->SAM->RAS1) {
			ram_d8(md->RAM, 1, 1, Zrow, Zcol, &md->Dread);
		}
	}

	bool EXTMEM = 0;
	if (md->cart) {
		if (RnW) {
			md->CPU->D = md->cart->read(md->cart, A, 0, 0, md->CPU->D);
		} else {
			md->CPU->D = md->cart->write(md->cart, A, 0, 0, md->CPU->D);
		}
		EXTMEM = md->cart->EXTMEM;
	}

	if (!EXTMEM) {
		if (RnW) {
			if (!md->read_byte || !md->read_byte(md, A))
				read_byte(md, A);
		} else {
			if (!md->write_byte || !md->write_byte(md, A))
				write_byte(md, A);
		}
	}

	if (!md->SAM->nWE) {
		if (md->SAM->RAS0) {
			ram_d8(md->RAM, 0, 0, Zrow, Zcol, &md->CPU->D);
		}
		if (md->SAM->RAS1) {
			ram_d8(md->RAM, 0, 1, Zrow, Zcol, &md->CPU->D);
		}
	}
}

static void read_byte(struct dragon *md, unsigned A) {
	switch (md->SAM->S) {
	case 0:
		md->CPU->D = md->Dread;
		break;
	case 1:
	case 2:
		rombank_d8(md->ROM0, A, &md->CPU->D);
		break;
	case 3:
		if (md->cart) {
			md->CPU->D = md->cart->read(md->cart, A & 0x3fff, 0, 1, md->CPU->D);
			break;
		}
		break;
	case 4:
		if (md->relaxed_pia0_decode || (A & 4) == 0) {
			md->CPU->D = mc6821_read(md->PIA0, A);
			break;
		}
		break;
	case 5:
		if (md->relaxed_pia1_decode || (A & 4) == 0) {
			md->CPU->D = mc6821_read(md->PIA1, A);
			break;
		}
		break;
	case 6:
		if (md->cart) {
			md->CPU->D = md->cart->read(md->cart, A, 1, 0, md->CPU->D);
			break;
		}
		break;
	default:
		break;
	}
}

static void write_byte(struct dragon *md, unsigned A) {
	if ((md->SAM->S & 4) || md->unexpanded_dragon32) {
		switch (md->SAM->S) {
		case 1:
		case 2:
			rombank_d8(md->ROM0, A, &md->CPU->D);
			break;
		case 3:
			if (md->cart) {
				md->CPU->D = md->cart->write(md->cart, A & 0x3fff, 0, 1, md->CPU->D);
				break;
			}
			break;
		case 4:
			if (md->relaxed_pia0_decode || (A & 4) == 0) {
				mc6821_write(md->PIA0, A, md->CPU->D);
				break;
			}
			break;
		case 5:
			if (md->relaxed_pia1_decode || (A & 4) == 0) {
				mc6821_write(md->PIA1, A, md->CPU->D);
				break;
			}
			break;
		case 6:
			if (md->cart) {
				md->CPU->D = md->cart->write(md->cart, A, 1, 0, md->CPU->D);
				break;
			}
			break;
		default:
			break;
		}
	}
}

// dragon_read_byte() and dragon_write_byte() assert clock_inhibit before
// calling the above to do the same thing without advancing the clock.  Used
// for debugging & breakpoints.

static uint8_t dragon_read_byte(struct machine *m, unsigned A, uint8_t D) {
	(void)D;
	struct dragon *md = (struct dragon *)m;
	md->clock_inhibit = 1;
	DELEGATE_CALL(md->CPU->mem_cycle, 1, A);
	md->clock_inhibit = 0;
	return md->CPU->D;
}

static void dragon_write_byte(struct machine *m, unsigned A, uint8_t D) {
	struct dragon *md = (struct dragon *)m;
	md->CPU->D = D;
	md->clock_inhibit = 1;
	DELEGATE_CALL(md->CPU->mem_cycle, 0, A);
	md->clock_inhibit = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// VDG cycles

static void vdg_fetch_handler(void *sptr, uint16_t A, int nbytes, uint16_t *dest) {
	(void)A;
	struct dragon *md = sptr;
	uint16_t attr = (PIA_VALUE_B(md->PIA1) & 0x10) << 6;  // GM0 -> ¬INT/EXT
	while (nbytes > 0) {
		int n = md->SAM->vdg_bytes(md->SAM, nbytes);
		const uint8_t *Vp = ram_a8(md->RAM, 0, md->SAM->Vrow, md->SAM->Vcol);
		if (dest && Vp) {
			for (int i = n; i; i--) {
				uint16_t D = *(Vp++) | attr;
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
	struct dragon *md = sptr;
	unsigned pia_vdg_mode = PIA_VALUE_B(md->PIA1);
	bool GnA = pia_vdg_mode & 0x80;
	bool EnI = pia_vdg_mode & 0x10;
	uint16_t Aram7 = EnI ? 0x80 : 0;
	while (nbytes > 0) {
		int n = md->SAM->vdg_bytes(md->SAM, nbytes);
		const uint8_t *Vp = ram_a8(md->RAM, 0, md->SAM->Vrow, md->SAM->Vcol);
		if (dest && Vp) {
			for (int i = n; i; i--) {
				uint16_t Dram = *(Vp++);
				bool SnA = Dram & 0x80;
				uint16_t D;
				if (!GnA && !SnA) {
					unsigned Aext = (md->VDG->row << 8) | Aram7 | Dram;
					uint8_t Dext = 0xff;
					rombank_d8(md->ext_charset, Aext, &Dext);
					D = Dext | 0x100;  // set INV
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* simulate an RTS without otherwise affecting machine state */
static void dragon_op_rts(struct machine *m) {
	struct dragon *md = (struct dragon *)m;
	unsigned int new_pc = m->read_byte(m, md->CPU->reg_s, 0) << 8;
	new_pc |= m->read_byte(m, md->CPU->reg_s + 1, 0);
	md->CPU->reg_s += 2;
	md->CPU->reg_pc = new_pc;
}

static void dragon_dump_ram(struct machine *m, FILE *fd) {
	struct dragon *md = (struct dragon *)m;
	struct ram *ram = md->RAM;
	for (unsigned bank = 0; bank < ram->nbanks; bank++) {
		if (ram->d && ram->d[bank]) {
			fwrite(ram->d[bank], ram->bank_nelems, 1, fd);
		}
	}
}

static int32_t dragon_get_symbol(struct machine *m, const char *label) {
	struct dragon *md = (struct dragon *)m;
	int32_t A;
	if ((A = sym_find(&md->ROM0->symtab, label)) >= 0)
		return A;
	// XXX: search cartridge ROM too
	return A;
}

static void dragon_add_breakpoint(struct machine *m, uint32_t A,
				  DELEGATE_T2(void, bool, uint32) handler) {
	struct part *p = &m->part;
	struct dragon *md = (struct dragon *)m;
	if (!bp_breakpoint_add(&md->breakpoint_set, A, handler)) {
		LOG_MOD_WARN(p->partdb->name, "failed to add breakpoint @ 0x%04x\n", A);
	}
	if (md->breakpoint_set.nbreakpoints) {
		md->CPU->instruction_hook = DELEGATE_AS1(void, uint32, bp_instruction_hook, &md->breakpoint_set);
	}
}

static void dragon_remove_breakpoint(struct machine *m, int32_t A,
				     DELEGATE_T2(void, bool, uint32) handler) {
	struct dragon *md = (struct dragon *)m;
	bp_breakpoint_remove(&md->breakpoint_set, A, handler);
	if (!md->breakpoint_set.nbreakpoints) {
		md->CPU->instruction_hook.func = NULL;
	}
}

static void dragon_add_watchpoint(struct machine *m, bool RnW,
				  uint32_t Astart, uint32_t Aend,
				  DELEGATE_T2(void, bool, uint32) handler) {
	struct part *p = &m->part;
	struct dragon *md = (struct dragon *)m;
	if (!bp_watchpoint_add(&md->watchpoint_set, RnW, Astart, Aend, handler)) {
		LOG_MOD_WARN(p->partdb->name, "failed to add watchpoint @ 0x%04x-0x%04x\n", Astart, Aend);
	}
}

static void dragon_remove_watchpoint(struct machine *m, int RnW,
				     int32_t Astart, uint32_t Aend,
				     DELEGATE_T2(void, bool, uint32) handler) {
	struct dragon *md = (struct dragon *)m;
	bp_watchpoint_remove(&md->watchpoint_set, RnW, Astart, Aend, handler);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void dragon_keyboard_update(void *sptr) {
	struct dragon *md = sptr;
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
	struct dragon *md = sptr;
	int port = PIA_VALUE_CB2(md->PIA0);
	int axis = PIA_VALUE_CA2(md->PIA0);
	int dac_value = ((md->PIA1->a.out_sink & 0xfc) | 2) << 8;
	int js_value = joystick_read_axis(port, axis);
	if (js_value >= dac_value)
		md->PIA0->a.in_sink |= 0x80;
	else
		md->PIA0->a.in_sink &= 0x7f;
}

static void update_sound_mux_source(void *sptr) {
	struct dragon *md = sptr;
	unsigned source = (PIA_VALUE_CB2(md->PIA0) << 1)
	                  | PIA_VALUE_CA2(md->PIA0);
	sound_set_mux_source(md->snd, source);
}

void dragon_update_vdg_mode(struct dragon *md) {
	unsigned vmode = (md->PIA1->b.out_source & md->PIA1->b.out_sink) & 0xf8;
	// ¬INT/EXT = GM0
	vmode |= (vmode & 0x10) << 4;
	mc6847_set_mode(md->VDG, vmode);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void pia0a_data_preread(void *sptr) {
	dragon_keyboard_update(sptr);
	joystick_update(sptr);
}

/* Overridden in coco.c */

static void pia1a_data_postwrite(void *sptr) {
	struct dragon *md = sptr;
	sound_set_dac_level(md->snd, (float)(PIA_VALUE_A(md->PIA1) & 0xfc) / 252.);
	tape_update_output(md->tape_interface, md->PIA1->a.out_sink & 0xfc);
	dragon_keyboard_update(md);
	printer_strobe(md->printer_interface, PIA_VALUE_A(md->PIA1) & 0x02, PIA_VALUE_B(md->PIA0));
}

static void pia1a_control_postwrite(void *sptr) {
	struct dragon *md = sptr;
	tape_set_motor(md->tape_interface, PIA_VALUE_CA2(md->PIA1));
	tape_update_output(md->tape_interface, md->PIA1->a.out_sink & 0xfc);
}

static void pia1b_data_preread(void *sptr) {
	/* Dragons need to poll printer BUSY state */
	struct dragon *md = sptr;
	if (printer_busy(md->printer_interface))
		md->PIA1->b.in_sink |= 0x01;
	else
		md->PIA1->b.in_sink &= ~0x01;
}

void dragon_pia1b_data_postwrite(void *sptr) {
	struct dragon *md = sptr;
	// Single-bit sound
	bool sbs_enabled = !((md->PIA1->b.out_source ^ md->PIA1->b.out_sink) & (1<<1));
	bool sbs_level = md->PIA1->b.out_source & md->PIA1->b.out_sink & (1<<1);
	sound_set_sbs(md->snd, sbs_enabled, sbs_level);
	// VDG mode
	dragon_update_vdg_mode(md);
}

static void pia1b_control_postwrite(void *sptr) {
	struct dragon *md = sptr;
	sound_set_mux_enabled(md->snd, PIA_VALUE_CB2(md->PIA1));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* VDG edge delegates */

void dragon_vdg_hs(void *sptr, bool level) {
	struct dragon *md = sptr;
	mc6821_set_cx1(&md->PIA0->a, level ^ md->hs_invert);
	md->SAM->vdg_hsync(md->SAM, level);
	if (!level) {
		unsigned p1bval = md->PIA1->b.out_source & md->PIA1->b.out_sink;
		bool GM0 = p1bval & 0x10;
		bool CSS = p1bval & 0x08;
		md->ntsc_burst_mod = (md->use_ntsc_burst_mod && GM0 && CSS) ? 2 : 0;
	}
}

void dragon_vdg_fs(void *sptr, bool level) {
	struct dragon *md = sptr;
	mc6821_set_cx1(&md->PIA0->b, level);
	md->SAM->vdg_fsync(md->SAM, level);
	if (level) {
		sound_update(md->snd);
		md->frame--;
		if (md->frame < 0)
			md->frame = md->frameskip;
		vo_vsync(md->vo, md->frame == 0);
	}
}

static void vdg_render_line(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data) {
	struct dragon *md = sptr;
	burst = (burst | md->ntsc_burst_mod) & 3;
	DELEGATE_CALL(md->vo->render_line, burst, npixels, data);
}

/* Dragon parallel printer line delegate. */

// ACK is active low
static void printer_ack(void *sptr, bool ack) {
	struct dragon *md = sptr;
	mc6821_set_cx1(&md->PIA1->a, !ack);
}

/* Sound output can feed back into the single bit sound pin when it's
 * configured as an input. */

static void single_bit_feedback(void *sptr, bool level) {
	struct dragon *md = sptr;
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
	struct dragon *md = sptr;
	sound_set_tape_level(md->snd, value);
	if (value >= 0.5)
		md->PIA1->a.in_sink &= ~(1<<0);
	else
		md->PIA1->a.in_sink |= (1<<0);
}

/* Catridge signalling */

static void cart_firq(void *sptr, bool level) {
	struct dragon *md = sptr;
	mc6821_set_cx1(&md->PIA1->b, level);
}

static void cart_nmi(void *sptr, bool level) {
	struct dragon *md = sptr;
	MC6809_NMI_SET(md->CPU, level);
}

static void cart_halt(void *sptr, bool level) {
	struct dragon *md = sptr;
	MC6809_HALT_SET(md->CPU, level);
}
