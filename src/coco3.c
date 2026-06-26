/** \file
 *
 *  \brief Tandy Colour Computer 3 machine.
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
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809/hd6309.h"
#include "mc6809/mc6809.h"
#include "mc6821.h"
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
#include "tcc1014/tcc1014.h"
#include "ui.h"
#include "vo.h"
#include "xroar.h"

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

// Measured peak-to-peak voltages: 0V, 1V, 1.6V, 2V.  Scaled here:
static float rgb_intensity_map[4] = { 0.000, 0.460, 0.736, 0.920 };

// The GIME appears to generate its composite output (used in NTSC machines
// only) by switching between a set of 7 voltages at 3.58Mhz (presumably with
// R/C net to smooth to almost-but-not-quite sine waves).  AFAICT there is no
// black level separate to blank (which would be usual for an NTSC signal).
//
// The grey and colour luminances are different at each intensity because there
// is no output voltage corresponding to the luminance of colour output: it is
// simply the average of the high and low voltages used to form the colour
// signal.
//
// The colour amplitude is similar for intensity levels 0-2, but reduced for
// intensity 3, leading to less saturated colour.
//
// Monochrome output (bit 4 of VMODE register at $FF98) only affects composite,
// and as well as removing the colourburst, will only emit the grey level for
// each intensity; TVs don't immediately (or ever) switch to mono just because
// the colourburst is missing.
//
// Observed on a scope, there is a larger jump in phase between hues 11 and 12
// than between any other adjacent hues (including between 1 and 15). This
// supports the theory that the colour phase is a simple offset counted in GIME
// clock edges, with a gap as there are 16 edges in one colour cycle, and only
// 15 hues.

// Approximate measured voltages (at composite video port, relative to blank):

#define CMP_V_SYNC (-0.350)
#define CMP_V_BURST_LOW (-0.210)
#define CMP_V_0 (0.000)
#define CMP_V_1 (0.170)
#define CMP_V_2 (0.380)
#define CMP_V_3 (0.580)
#define CMP_V_4 (0.750)

// Aliases:
#define CMP_V_BLANK (CMP_V_0)
#define CMP_V_BURST_HIGH (CMP_V_1)
#define CMP_V_GREY0 (CMP_V_0)
#define CMP_V_GREY1 (CMP_V_1)
#define CMP_V_GREY2 (CMP_V_2)
#define CMP_V_GREY3 (CMP_V_4)
#define CMP_V_PEAK (CMP_V_4)

// Map selected intensity level to the grey and and colour peak voltages:

static struct {
	float grey;
	float clr_low;
	float clr_high;
} const cmp_intensity[4] = {
	{ CMP_V_GREY0, CMP_V_BURST_LOW, CMP_V_2 },
	{ CMP_V_GREY1, CMP_V_0, CMP_V_3 },
	{ CMP_V_GREY2, CMP_V_1, CMP_V_4 },
	{ CMP_V_GREY3, CMP_V_2, CMP_V_4 },
};

// Note that the rest of XRoar's video system as been based on the idea that
// measured voltages will be Y'PbPr, but these measurements are at the
// composite port, so are already in Y'UV so will need some massaging.

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct coco3 {
	struct machine public;

	struct MC6809 *CPU;
	struct TCC1014 *GIME;
	struct MC6821 *PIA0, *PIA1;
	struct rombank *ROM0;
	struct ram *RAM;

	struct vo_interface *vo;
	int frame;  // track frameskip
	struct sound_interface *snd;

	bool inverted_text;
	struct cart *cart;
	unsigned configured_frameskip;
	unsigned frameskip;

	int cycles;

	// Debug
	bool single_step;
	int stop_signal;
#ifdef WANT_GDB_TARGET
	struct gdb_interface *gdb_interface;
#endif
	struct bp_breakpoint_set breakpoint_set;
	struct bp_watchpoint_set watchpoint_set;

	struct tape_interface *tape_interface;
	DELEGATE_T1(void, float) tape_update_audio;

	struct printer_interface *printer_interface;

	struct {
		struct keyboard_interface *interface;
	} keyboard;

	// Optional DAT board provides extra translation for up to 2M of RAM.
	struct {
		bool enabled;
		bool readable;
		bool MMUEN;
		bool MC3;
		unsigned task;
		uint32_t mask;
		uint32_t mmu_bank[16];
		uint32_t vram_bank;
	} dat;

	// UI message receipt
	int msgr_client_id;

	// Useful configuration side-effect tracking
	bool has_secb;
	uint32_t crc_secb;
};

#define COCO3_SER_RAM           (2)
#define COCO3_SER_RAM_SIZE      (3)
#define COCO3_SER_RAM_MASK      (4)
#define COCO3_SER_DAT_MMU_BANK  (11)
#define COCO3_SER_DAT_VRAM_BANK (12)

static const struct ser_struct ser_struct_coco3[] = {
	SER_ID_STRUCT_NEST(1,  &machine_ser_struct_data),
	SER_ID_STRUCT_UNHANDLED(COCO3_SER_RAM),
	SER_ID_STRUCT_UNHANDLED(COCO3_SER_RAM_SIZE),
	SER_ID_STRUCT_UNHANDLED(COCO3_SER_RAM_MASK),
	SER_ID_STRUCT_ELEM(5,  struct coco3, inverted_text),
	SER_ID_STRUCT_ELEM(6,  struct coco3, dat.enabled),
	SER_ID_STRUCT_ELEM(7,  struct coco3, dat.readable),
	SER_ID_STRUCT_ELEM(8,  struct coco3, dat.MMUEN),
	SER_ID_STRUCT_ELEM(9,  struct coco3, dat.MC3),
	SER_ID_STRUCT_ELEM(10, struct coco3, dat.task),
	SER_ID_STRUCT_UNHANDLED(COCO3_SER_DAT_MMU_BANK),
	SER_ID_STRUCT_UNHANDLED(COCO3_SER_DAT_VRAM_BANK),
};

static bool coco3_read_elem(void *sptr, struct ser_handle *sh, int tag);
static bool coco3_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data coco3_ser_struct_data = {
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
		mc->vdg_type = VDG_GIME_1986;
	if (mc->vdg_type != VDG_GIME_1986 && mc->vdg_type != VDG_GIME_1987)
		mc->vdg_type = VDG_GIME_1986;

	if (mc->ram_init == ANY_AUTO) {
		mc->ram_init = ram_init_pattern;
	}

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

static bool coco3_is_working_config(struct machine_config *mc) {
	if (!mc)
		return 0;
	if (mc->extbas_rom) {
		sds tmp = romlist_find(mc->extbas_rom);
		if (!tmp)
			return 0;
		sdsfree(tmp);
	} else {
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static bool coco3_has_interface(struct part *p, const char *ifname);
static void coco3_attach_interface(struct part *p, const char *ifname, void *intf);

static void coco3_connect_cart(struct part *p);
static void coco3_insert_cart(struct machine *m, struct cart *c);
static void coco3_remove_cart(struct machine *m);

static void coco3_reset(struct machine *m, bool hard);
static enum machine_run_state coco3_run(struct machine *m, int ncycles);
static void coco3_single_step(struct machine *m);
static void coco3_signal(struct machine *m, int sig);

static void coco3_ui_set_keymap(void *, int tag, void *smsg);
static bool coco3_set_pause(struct machine *m, int state);
static void coco3_ui_set_picture(void *, int tag, void *smsg);
static void coco3_ui_set_tv_input(void *, int tag, void *smsg);
static void coco3_ui_set_text_invert(void *, int tag, void *smsg);
static void *coco3_get_interface(struct machine *m, const char *ifname);
static void coco3_ui_set_frameskip(void *, int tag, void *smsg);
static void coco3_ui_set_ratelimit(void *, int tag, void *smsg);

static uint8_t coco3_read_byte(struct machine *m, unsigned A, uint8_t D);
static void coco3_write_byte(struct machine *m, unsigned A, uint8_t D);
static void coco3_op_rts(struct machine *m);
static void coco3_dump_ram(struct machine *m, FILE *fd);
static int32_t coco3_get_symbol(struct machine *m, const char *label);
static void coco3_add_breakpoint(struct machine *m, uint32_t A,
				 DELEGATE_T2(void, bool, uint32) handler);
static void coco3_remove_breakpoint(struct machine *m, int32_t A,
				    DELEGATE_T2(void, bool, uint32) handler);
static void coco3_add_watchpoint(struct machine *m, bool RnW,
				 uint32_t Astart, uint32_t Aend,
				 DELEGATE_T2(void, bool, uint32) handler);
static void coco3_remove_watchpoint(struct machine *m, int RnW,
				    int32_t Astart, uint32_t Aend,
				    DELEGATE_T2(void, bool, uint32) handler);

static void keyboard_update(void *sptr);
static void joystick_update(void *sptr);
static void update_sound_mux_source(void *sptr);

static void single_bit_feedback(void *sptr, bool level);
static void update_audio_from_tape(void *sptr, float value);
static void cart_firq(void *sptr, bool level);
static void cart_nmi(void *sptr, bool level);
static void cart_halt(void *sptr, bool level);
static void gime_hs(void *sptr, bool level);
// static void gime_hs_pal_coco(void *sptr, bool level);
static void gime_fs(void *sptr, bool level);
static void gime_render_line(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data);
static void coco3_print_byte(void *, bool RnW, uint32_t A);

static void cpu_cycle(void *sptr, int ncycles, bool RnW, uint16_t A);
static void cpu_cycle_noclock(void *sptr, int ncycles, bool RnW, uint16_t A);
static void coco3_instruction_posthook(void *sptr);
static uint16_t fetch_vram(void *sptr, uint32_t A);

static void pia0a_data_preread(void *sptr);
#define pia0a_data_postwrite keyboard_update
#define pia0a_control_postwrite update_sound_mux_source
#define pia0b_data_postwrite keyboard_update
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
static bool coco3_finish(struct part *p);
static void coco3_free(struct part *p);

static const struct partdb_entry_funcs coco3_funcs = {
	.allocate = coco3_allocate,
	.initialise = coco3_initialise,
	.finish = coco3_finish,
	.free = coco3_free,

	.ser_struct_data = &coco3_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_entry coco3_part = { .partdb_entry = { .name = "coco3", .description = "Tandy | Colour Computer 3", .funcs = &coco3_funcs }, .config_complete = coco3_config_complete, .is_working_config = coco3_is_working_config, .cart_arch = "dragon-cart" };

static struct part *coco3_allocate(void) {
        struct coco3 *mcc3 = part_new(sizeof(*mcc3));
        struct machine *m = &mcc3->public;
        struct part *p = &m->part;

	*mcc3 = (struct coco3){0};

	m->has_interface = coco3_has_interface;
	m->attach_interface = coco3_attach_interface;

	m->insert_cart = coco3_insert_cart;
	m->remove_cart = coco3_remove_cart;
	m->reset = coco3_reset;
	m->run = coco3_run;
	m->single_step = coco3_single_step;
	m->signal = coco3_signal;

	m->set_pause = coco3_set_pause;
	m->get_interface = coco3_get_interface;

	m->read_byte = coco3_read_byte;
	m->write_byte = coco3_write_byte;
	m->op_rts = coco3_op_rts;
	m->dump_ram = coco3_dump_ram;
	m->debug.get_symbol = coco3_get_symbol;
	m->debug.add_breakpoint = coco3_add_breakpoint;
	m->debug.remove_breakpoint = coco3_remove_breakpoint;
	m->debug.add_watchpoint = coco3_add_watchpoint;
	m->debug.remove_watchpoint = coco3_remove_watchpoint;

	m->keyboard.type = dkbd_layout_coco3;

	return p;
}

static void create_ram(struct coco3 *mcc3) {
	struct machine *m = &mcc3->public;
	struct part *p = &m->part;
	struct machine_config *mc = m->config;

	if (mc->ram < 512) {
		mc->ram = 128;
	} else if (mc->ram < 1024) {
		mc->ram = 512;
	} else if (mc->ram < 2048) {
		mc->ram = 1024;
	} else {
		mc->ram = 2048;
	}

	// TODO: rejig everything to use 16-bit RAM!
	struct ram_config ram_config = {
		.d_width = 8,
		.organisation = RAM_ORG(19, 9, 0),
	};
	if (mc->ram == 128) {
		ram_config.organisation = RAM_ORG(17, 9, 0);
	}
	struct ram *ram = (struct ram *)part_create("ram", &ram_config);

	unsigned nbanks = mc->ram / 512;
	if (nbanks < 1)
		nbanks = 1;
	if (nbanks > 4)
		nbanks = 4;

	if (nbanks > 1) {
		mcc3->dat.enabled = 1;
		if (nbanks > 2)
			mcc3->dat.mask = 0xc0;  // 2MB
		else
			mcc3->dat.mask = 0x40;  // 1MB
	}

	for (unsigned i = 0; i < nbanks; i++)
		ram_add_bank(ram, i);

	part_add_component(p, (struct part *)ram, "RAM");
}

static void coco3_initialise(struct part *p, void *options) {
        struct machine_config *mc = options;
        assert(mc != NULL);

        struct coco3 *mcc3 = (struct coco3 *)p;
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

	// RAM
	create_ram(mcc3);

	// Keyboard
	m->keyboard.type = mc->keymap;
}

static bool coco3_finish(struct part *p) {
	struct coco3 *mcc3 = (struct coco3 *)p;
	struct machine *m = &mcc3->public;
	struct machine_config *mc = m->config;

	// Interfaces
	mcc3->vo = xroar.vo_interface;
	mcc3->snd = xroar.ao_interface->sound_interface;
	mcc3->tape_interface = xroar.tape_interface;

	mcc3->tape_interface->default_paused = 0;

	// Find attached parts
	mcc3->GIME = (struct TCC1014 *)part_component_by_id_is_a(p, "GIME", "TCC1014");
	mcc3->CPU = (struct MC6809 *)part_component_by_id_is_a(p, "CPU", "MC6809");
	mcc3->PIA0 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA0", "MC6821");
	mcc3->PIA1 = (struct MC6821 *)part_component_by_id_is_a(p, "PIA1", "MC6821");
	mcc3->RAM = (struct ram *)part_component_by_id_is_a(p, "RAM", "ram");

	// Check all required parts are attached
	if (!mcc3->GIME || !mcc3->CPU || !mcc3->PIA0 || !mcc3->PIA1 ||
	    !mcc3->RAM ||
	    !mcc3->vo || !mcc3->snd || !mcc3->tape_interface) {
		return 0;
	}

	// Register as a messenger client
	mcc3->msgr_client_id = messenger_client_register();

	// Join the ui messenger groups we're interested in
	ui_messenger_preempt_group(mcc3->msgr_client_id, ui_tag_picture, MESSENGER_NOTIFY_DELEGATE(coco3_ui_set_picture, mcc3));
	ui_messenger_preempt_group(mcc3->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(coco3_ui_set_tv_input, mcc3));
	ui_messenger_preempt_group(mcc3->msgr_client_id, ui_tag_vdg_inverse, MESSENGER_NOTIFY_DELEGATE(coco3_ui_set_text_invert, mcc3));
	ui_messenger_preempt_group(mcc3->msgr_client_id, ui_tag_keymap, MESSENGER_NOTIFY_DELEGATE(coco3_ui_set_keymap, mcc3));
	ui_messenger_join_group(mcc3->msgr_client_id, ui_tag_frameskip, MESSENGER_NOTIFY_DELEGATE(coco3_ui_set_frameskip, mcc3));
	ui_messenger_join_group(mcc3->msgr_client_id, ui_tag_ratelimit, MESSENGER_NOTIFY_DELEGATE(coco3_ui_set_ratelimit, mcc3));

	memcpy(&m->debug.cpu, &mcc3->CPU->debug.cpu, sizeof(m->debug.cpu));
	m->debug.target = debug_target_new(m->debug.cpu.architecture, m->debug.cpu.endian);
	debug_target_add_part(m->debug.target, NULL, &mcc3->CPU->debug.part);
#ifdef WANT_GDB_TARGET
	if (xroar.cfg.debug.gdb_pseudo_regs) {
		debug_target_add_part(m->debug.target, NULL, &mcc3->GIME->debug.part);
		debug_target_add_part(m->debug.target, "pia0", &mcc3->PIA0->debug.part);
		debug_target_add_part(m->debug.target, "pia1", &mcc3->PIA1->debug.part);
	}
	m->debug.target_xml = debug_target_xml(m->debug.target);
#endif

	// ROM
	mcc3->ROM0 = rombank_new(8, 32768, 1);

	// Super Extended Colour BASIC
        if (mc->extbas_rom) {
                sds tmp = romlist_find(mc->extbas_rom);
                if (tmp) {
                        rombank_load_image(mcc3->ROM0, 0, tmp, 0);
                        sdsfree(tmp);
                }
	}

	// Report and check CRC (Super Extended Colour BASIC)
	rombank_report(mcc3->ROM0, "coco3", "Super Extended Colour BASIC");
	mcc3->crc_secb = 0xb4c88d6c;  // Super Extended Colour BASIC (NTSC)
	mcc3->has_secb = rombank_verify_crc(mcc3->ROM0, "Super Extended Colour BASIC", -1, "@coco3", xroar.cfg.force_crc_match, &mcc3->crc_secb);

	// RAM configuration
	ram_report(mcc3->RAM, "coco3", "motherboard RAM");

	// Connect any cartridge part
	coco3_connect_cart(p);

	// GIME

	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
	mcc3->GIME->fetch_vram = DELEGATE_AS1(uint16, uint32, fetch_vram, mcc3);

	// GIME reports changes in active area
	mcc3->GIME->set_active_area = mcc3->vo->set_active_area;

	// PAL CoCo 3s only emit the RGB palette, even over composite.  At the
	// moment, there's no special handling for PAL composite.  If we decide
	// to implement it properly, it's worth noting that the GIME crystal in
	// PAL machines is 28.475MHz (2 * 14.2373MHz).

	// Actual GIME clock is 2× this, but we treat it the same as a SAM
	ui_update_state(-1, ui_tag_cmp_fs, VO_RENDER_FS_14_31818, NULL);
	ui_update_state(-1, ui_tag_cmp_fsc, VO_RENDER_FSC_3_579545, NULL);
	ui_update_state(-1, ui_tag_cmp_system, VO_RENDER_SYSTEM_NTSC, NULL);

	// Bodge factor to bring centred active area in line with chroma
	DELEGATE_SAFE_CALL(mcc3->vo->set_cmp_phase_offset, 0);

	DELEGATE_SAFE_CALL(mcc3->vo->set_cmp_lead_lag, 0., 100.);

	if (mc->tv_standard != TV_PAL) {
		// Very slight tweak to the phase
		double hue_offset = (2. * M_PI * 15.) / 1600.;
		for (int intensity = 0; intensity < 4; intensity++) {
			float grey = cmp_intensity[intensity].grey;
			float clr_low = cmp_intensity[intensity].clr_low;
			float clr_high = cmp_intensity[intensity].clr_high;

			// Scale signal and add a little brightness.
			grey     = grey     * (1.00 / CMP_V_PEAK) + 0.20;
			clr_low  = clr_low  * (1.00 / CMP_V_PEAK) + 0.20;
			clr_high = clr_high * (1.00 / CMP_V_PEAK) + 0.20;

			for (int phase = 0; phase < 16; phase++) {
				int c = (intensity * 16) + phase;

				double y, b_y, r_y;
				if (phase == 0 || c == 63) {
					y = grey;
					b_y = 0.0;
					r_y = 0.0;
				} else {
					int ph = ((phase + (phase >= 12)) + 9) % 16;
					double hue = ((2.0 * M_PI * (double)ph) / 16.0) + hue_offset;
					b_y = ((clr_high - clr_low) / 2.0) * sin(hue) / 1.414;
					r_y = ((clr_high - clr_low) / 2.0) * cos(hue) / 1.414;
					y = (clr_high + clr_low) / 2.0;
				}
				// These values were measured at the composite port,
				// already in U/V, so we need to scale to Pb/Pr before
				// adding them to the palette.
				b_y /= 0.504;
				r_y /= 0.711;
				DELEGATE_SAFE_CALL(mcc3->vo->palette_set_ybr, c, y, b_y, r_y);
			}
		}
	}

	for (int j = 0; j < 64; j++) {
		float r = rgb_intensity_map[((j>>4)&2)|((j>>2)&1)];
		float g = rgb_intensity_map[((j>>3)&2)|((j>>1)&1)];
		float b = rgb_intensity_map[((j>>2)&2)|((j>>0)&1)];
		DELEGATE_SAFE_CALL(mcc3->vo->palette_set_rgb, j, r, g, b);
		if (mc->tv_standard == TV_PAL) {
			// XXX need to check the actual PAL CoCo 3 RGB to composite
			// circuit.  For now, bodge composite colours:
			r *= 1.08;
			g *= 1.08;
			b *= 1.08;
			float y = 0.299 * r + 0.587 * g + 0.114 * b;
			float b_y = b - y;
			float r_y = r - y;
			DELEGATE_SAFE_CALL(mcc3->vo->palette_set_ybr, j, y, b_y, r_y);
		}
	}

	DELEGATE_SAFE_CALL(mcc3->vo->set_cmp_burst, 1, 0);    // Normal burst
	DELEGATE_SAFE_CALL(mcc3->vo->set_cmp_burst, 2, 180);  // Phase inverted burst

	// CPU

	mcc3->CPU->mem_cycle = DELEGATE_AS2(void, bool, uint16, tcc1014_mem_cycle, mcc3->GIME);
	mcc3->GIME->CPUD = &mcc3->CPU->D;

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

	// Default all PIA connections to unconnected (no source, no sink)
	mcc3->PIA0->b.in_source = 0;
	mcc3->PIA1->b.in_source = 0;
	mcc3->PIA0->a.in_sink = mcc3->PIA0->b.in_sink = 0xff;
	mcc3->PIA1->a.in_sink = mcc3->PIA1->b.in_sink = 0xff;

	mcc3->GIME->signal_hs = DELEGATE_AS1(void, bool, gime_hs, mcc3);
	mcc3->GIME->signal_fs = DELEGATE_AS1(void, bool, gime_fs, mcc3);
	mcc3->GIME->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, gime_render_line, mcc3);
	ui_update_state(-1, ui_tag_tv_input, mc->tv_input, NULL);
	ui_update_state(-1, ui_tag_vdg_inverse, mcc3->inverted_text, NULL);

	// Until I implement serial, this appear to pull low by default
	mcc3->PIA1->b.in_sink &= ~(1<<0);

	// Keyboard interface
	mcc3->keyboard.interface = keyboard_interface_new();
	mcc3->keyboard.interface->update = DELEGATE_AS0(void, keyboard_update, mcc3);
	keyboard_set_chord_mode(mcc3->keyboard.interface, keyboard_chord_mode_coco_basic);
	ui_update_state(-1, ui_tag_keymap, m->keyboard.type, NULL);

	// Printer interface
	mcc3->printer_interface = printer_interface_new();

	mcc3->tape_update_audio = DELEGATE_AS1(void, float, update_audio_from_tape, mcc3);

#ifdef WANT_GDB_TARGET
	// GDB
	if (xroar.cfg.debug.gdb) {
		mcc3->gdb_interface = gdb_interface_new(xroar.cfg.debug.gdb_ip, xroar.cfg.debug.gdb_port, m);
	}
#endif

	// XXX until we serialise sound information
	update_sound_mux_source(mcc3);
	sound_set_mux_enabled(mcc3->snd, PIA_VALUE_CB2(mcc3->PIA1));

	mcc3->dat.readable = 1;

	tcc1014_notify_mode(mcc3->GIME);

	return 1;
}

static void coco3_free(struct part *p) {
	struct coco3 *mcc3 = (struct coco3 *)p;
	// Stop receiving any UI state updates
	messenger_client_unregister(mcc3->msgr_client_id);
	machine_remove_breakpoint_all(&mcc3->public, NULL);
	debug_target_free(mcc3->public.debug.target);
#ifdef WANT_GDB_TARGET
	if (mcc3->gdb_interface) {
		gdb_interface_free(mcc3->gdb_interface);
	}
	sdsfree(mcc3->public.debug.target_xml);
#endif
	if (mcc3->keyboard.interface) {
		keyboard_interface_free(mcc3->keyboard.interface);
	}
	machine_remove_breakpoint_all(&mcc3->public, mcc3);
	if (mcc3->printer_interface) {
		printer_interface_free(mcc3->printer_interface);
	}
	rombank_free(mcc3->ROM0);
}

static bool coco3_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct coco3 *mcc3 = sptr;
	struct machine *m = &mcc3->public;
	struct part *p = &m->part;
	size_t length = ser_data_length(sh);
	switch (tag) {
	case COCO3_SER_RAM:
		{
			if (!mcc3->public.config) {
				return 0;
			}
			if (length != ((unsigned)mcc3->public.config->ram * 1024)) {
				LOG_MOD_WARN("coco3", "deserialise: RAM size mismatch %zu != %d\n", length, mcc3->public.config->ram * 1024);
				return 0;
			}
			part_free(part_component_by_id_is_a(p, "RAM", "ram"));
			create_ram(mcc3);
			struct ram *ram = (struct ram *)part_component_by_id_is_a(p, "RAM", "ram");
			ram_ser_read(ram, sh);
		}
		break;

	case COCO3_SER_RAM_SIZE:
	case COCO3_SER_RAM_MASK:
		// no-op: RAM is now a sub-component
		break;

	case COCO3_SER_DAT_MMU_BANK:
		for (int i = 0; i < 16; i++) {
			mcc3->dat.mmu_bank[i] = ser_read_uint8(sh);
		}
		break;

	case COCO3_SER_DAT_VRAM_BANK:
		{
			uint32_t vbank = ser_read_vuint32(sh);
			mcc3->dat.vram_bank = vbank >> 13;
		}
		break;

	default:
		return 0;
	}
	return 1;
}

static bool coco3_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct coco3 *mcc3 = sptr;
	switch (tag) {
	case COCO3_SER_RAM:
	case COCO3_SER_RAM_SIZE:
	case COCO3_SER_RAM_MASK:
		// no-op: RAM is now a sub-component
		break;

	case COCO3_SER_DAT_MMU_BANK:
		ser_write_tag(sh, tag, 16);
		for (int i = 0; i < 16; i++) {
			ser_write_uint8_untagged(sh, mcc3->dat.mmu_bank[i]);
		}
		ser_write_close_tag(sh);
		break;

	case COCO3_SER_DAT_VRAM_BANK:
		// compatibility
		ser_write_vuint32(sh, tag, mcc3->dat.vram_bank << 13);
		break;

	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static bool coco3_has_interface(struct part *p, const char *ifname) {
	struct coco3 *mp = (struct coco3 *)p;

	struct cart *c = mp->cart;
	if (c) {
		if (c->has_interface) {
			return c->has_interface(c, ifname);
		}
	}

	return 0;
}

static void coco3_attach_interface(struct part *p, const char *ifname, void *intf) {
	struct coco3 *mp = (struct coco3 *)p;

	struct cart *c = mp->cart;
	if (c) {
		if (c->attach_interface) {
			c->attach_interface(c, ifname, intf);
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco3_connect_cart(struct part *p) {
	struct coco3 *mcc3 = (struct coco3 *)p;
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
	struct coco3 *mcc3 = (struct coco3 *)m;
	part_free((struct part *)mcc3->cart);
	mcc3->cart = NULL;
}

static void coco3_reset(struct machine *m, bool hard) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	struct machine_config *mc = m->config;
	if (hard) {
		ram_clear(mcc3->RAM, mc->ram_init);
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
	machine_remove_breakpoint_all(m, mcc3);
	machine_add_breakpoint_sym(m, "serial.printchr", DELEGATE_AS2(void, bool, uint32, coco3_print_byte, mcc3));
}

static enum machine_run_state coco3_run(struct machine *m, int ncycles) {
	struct coco3 *mcc3 = (struct coco3 *)m;

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
	struct coco3 *mcc3 = (struct coco3 *)m;
	mcc3->single_step = 1;
	mcc3->CPU->running = 0;
	mcc3->CPU->instruction_posthook = DELEGATE_AS0(void, coco3_instruction_posthook, mcc3);
	do {
		mcc3->CPU->run(mcc3->CPU);
	} while (mcc3->single_step);
	mcc3->CPU->instruction_posthook.func = NULL;
}

/*
 * Stop emulation and set stop_signal to reflect the reason.
 */

static void coco3_signal(struct machine *m, int sig) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	mcc3->stop_signal = sig;
	mcc3->CPU->running = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void coco3_ui_set_keymap(void *sptr, int tag, void *smsg) {
	struct coco3 *mcc3 = sptr;
	struct machine *m = &mcc3->public;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_keymap);

	int type = m->keyboard.type;
	switch (uimsg->value) {
	case UI_NEXT:
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
	case UI_AUTO:
		type = m->config->keymap;
		break;
	default:
		type = uimsg->value;
		break;
	}
	m->keyboard.type = type;
	keyboard_set_keymap(mcc3->keyboard.interface, type);
	uimsg->value = type;
}

static bool coco3_set_pause(struct machine *m, int state) {
	struct coco3 *mcc3 = (struct coco3 *)m;
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

static void coco3_ui_set_picture(void *sptr, int tag, void *smsg) {
	struct coco3 *mp = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_picture);
	int picture = ui_msg_adjust_value_range(uimsg, mp->vo->picture, VO_PICTURE_ACTION,
						VO_PICTURE_ZOOMED, VO_PICTURE_UNDERSCAN,
						UI_ADJUST_FLAG_KEEP_AUTO);
	vo_set_viewport(mp->vo, picture);
}

// TV input selection.  CoCo 3 allows RGB.  RGB monitors pull PIA1 PB3 low.

static void coco3_ui_set_tv_input(void *sptr, int tag, void *smsg) {
	struct coco3 *mcc3 = sptr;
	struct machine *m = &mcc3->public;
	struct machine_config *mc = m->config;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_tv_input);

	mc->tv_input = ui_msg_adjust_value_range(uimsg, mc->tv_input, TV_INPUT_SVIDEO,
						 TV_INPUT_SVIDEO, TV_INPUT_RGB,
						 UI_ADJUST_FLAG_CYCLE);
	switch (mc->tv_input) {
	default:
	case TV_INPUT_SVIDEO:
		vo_set_signal(mcc3->vo, VO_SIGNAL_SVIDEO);
		tcc1014_set_composite(mcc3->GIME, 1);
		mcc3->PIA1->b.in_sink |= (1<<3);
		break;

	case TV_INPUT_CMP_KBRW:
		vo_set_signal(mcc3->vo, VO_SIGNAL_CMP);
		DELEGATE_SAFE_CALL(mcc3->vo->set_cmp_phase, 180);
		tcc1014_set_composite(mcc3->GIME, 1);
		mcc3->PIA1->b.in_sink |= (1<<3);
		break;

	case TV_INPUT_CMP_KRBW:
		vo_set_signal(mcc3->vo, VO_SIGNAL_CMP);
		DELEGATE_SAFE_CALL(mcc3->vo->set_cmp_phase, 0);
		tcc1014_set_composite(mcc3->GIME, 1);
		mcc3->PIA1->b.in_sink |= (1<<3);
		break;

	case TV_INPUT_RGB:
		vo_set_signal(mcc3->vo, VO_SIGNAL_RGB);
		tcc1014_set_composite(mcc3->GIME, 0);
		mcc3->PIA1->b.in_sink &= ~(1<<3);
		break;
	}
}

static void coco3_ui_set_text_invert(void *sptr, int tag, void *smsg) {
	struct coco3 *mcc3 = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_vdg_inverse);

	mcc3->inverted_text = ui_msg_adjust_value_range(uimsg, mcc3->inverted_text, 0,
							0, 1, UI_ADJUST_FLAG_CYCLE);
	tcc1014_set_inverted_text(mcc3->GIME, mcc3->inverted_text);
}

/*
 * Device inspection.
 */

/* Similarly SLOW.  Used to populate UI. */

static void *coco3_get_interface(struct machine *m, const char *ifname) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	if (0 == strcmp(ifname, "cart")) {
		return mcc3->cart;
	} else if (0 == strcmp(ifname, "keyboard")) {
		return mcc3->keyboard.interface;
	} else if (0 == strcmp(ifname, "printer")) {
		return mcc3->printer_interface;
	} else if (0 == strcmp(ifname, "tape-update-audio")) {
		return &mcc3->tape_update_audio;
	}
	return NULL;
}

static void coco3_ui_set_frameskip(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct coco3 *mp = sptr;
	struct ui_state_message *uimsg = smsg;
	mp->configured_frameskip = mp->frameskip = uimsg->value;
}

static void coco3_ui_set_ratelimit(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct coco3 *mp = sptr;
	struct ui_state_message *uimsg = smsg;
	sound_set_ratelimit(mp->snd, uimsg->value);
	if (uimsg->value) {
		mp->frameskip = mp->configured_frameskip;
	} else {
		mp->frameskip = 10;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used when single-stepping.

static void coco3_instruction_posthook(void *sptr) {
	struct coco3 *mcc3 = sptr;
	mcc3->single_step = 0;
}

static void read_byte(struct coco3 *mcc3, unsigned A) {
	if (mcc3->cart) {
		mcc3->CPU->D = mcc3->cart->read(mcc3->cart, A, 0, 0, mcc3->CPU->D);
		if (mcc3->cart->EXTMEM) {
			return;
		}
	}
	switch (mcc3->GIME->S) {
	case 0:
		// ROM
		rombank_d8(mcc3->ROM0, A, &mcc3->CPU->D);
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
			mcc3->CPU->D = (mcc3->CPU->D & ~0x03) | (mcc3->dat.vram_bank >> 6);
		} else if (A >= 0xffa0 && A < 0xffb0) {
			mcc3->CPU->D = (mcc3->CPU->D & ~0xc0) | mcc3->dat.mmu_bank[A & 15];
		}
		break;

	default:
		// All the rest are N/C
		break;
	}
	if (mcc3->GIME->RAS) {
		unsigned nWE = 1;
		unsigned Zrow = mcc3->GIME->Z;
		unsigned Zcol = mcc3->GIME->Z >> 9;
		if (!mcc3->dat.MMUEN || (mcc3->dat.MC3 && A >= 0xfe00 && A < 0xff00)) {
			// MMU not enabled, or CRM enabled and CRM region
			ram_d8(mcc3->RAM, nWE, 0, Zrow, Zcol, &mcc3->CPU->D);
		} else {
			// Otherwise, translate
			unsigned bank = mcc3->dat.mmu_bank[(A >> 13) | mcc3->dat.task] >> 6;
			ram_d8(mcc3->RAM, nWE, bank, Zrow, Zcol, &mcc3->CPU->D);
		}
	}
}

static void write_byte(struct coco3 *mcc3, unsigned A) {
	if (mcc3->cart) {
		mcc3->cart->write(mcc3->cart, A, 0, 0, mcc3->CPU->D);
	}
	if (!mcc3->cart || !mcc3->cart->EXTMEM) {
		switch (mcc3->GIME->S) {
		case 0:
			// ROM
			rombank_d8(mcc3->ROM0, A, &mcc3->CPU->D);
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
				mcc3->dat.vram_bank = ((mcc3->CPU->D & 0x03) << 6) & mcc3->dat.mask;
			} else if (A >= 0xffa0 && A < 0xffb0) {
				// MMU banking extended by 2 bits
				mcc3->dat.mmu_bank[A & 15] = (mcc3->CPU->D & 0xc0) & mcc3->dat.mask;
			}
			break;

		default:
			// All the rest are N/C
			break;
		}
	}
	if (mcc3->GIME->RAS) {
		unsigned nWE = 0;
		unsigned Zrow = mcc3->GIME->Z;
		unsigned Zcol = mcc3->GIME->Z >> 9;
		if (!mcc3->dat.MMUEN || (mcc3->dat.MC3 && A >= 0xfe00 && A < 0xff00)) {
			// MMU not enabled, or CRM enabled and CRM region
			ram_d8(mcc3->RAM, nWE, 0, Zrow, Zcol, &mcc3->CPU->D);
		} else {
			// Otherwise, translate
			unsigned bank = mcc3->dat.mmu_bank[(A >> 13) | mcc3->dat.task] >> 6;
			ram_d8(mcc3->RAM, nWE, bank, Zrow, Zcol, &mcc3->CPU->D);
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

static void cpu_cycle(void *sptr, int ncycles, bool RnW, uint16_t A) {
	struct coco3 *mcc3 = sptr;
	mcc3->cycles -= ncycles;
	if (mcc3->cycles <= 0) mcc3->CPU->running = 0;
	event_run_queue(MACHINE_EVENT_LIST, ncycles);
	MC6809_IRQ_SET(mcc3->CPU, mcc3->PIA0->a.irq | mcc3->PIA0->b.irq | mcc3->GIME->IRQ);
	MC6809_FIRQ_SET(mcc3->CPU, mcc3->PIA1->a.irq | mcc3->PIA1->b.irq | mcc3->GIME->FIRQ);

	if (RnW) {
		read_byte(mcc3, A);
	} else {
		write_byte(mcc3, A);
	}
#ifdef WANT_GDB_TARGET
	bp_check_watchpoints(&mcc3->watchpoint_set, RnW, A);
#endif
	mcc3->GIME->IL1 = (PIA_VALUE_A(mcc3->PIA0) | 0x80) != 0xff;
}

static void cpu_cycle_noclock(void *sptr, int ncycles, bool RnW, uint16_t A) {
	struct coco3 *mcc3 = sptr;
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
	struct coco3 *mcc3 = (struct coco3 *)m;
	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, mcc3);
	tcc1014_mem_cycle(mcc3->GIME, 1, A);
	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
	return mcc3->CPU->D;
}

/* Write a byte without advancing clock.  Used for debugging & breakpoints. */

static void coco3_write_byte(struct machine *m, unsigned A, uint8_t D) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	mcc3->CPU->D = D;
	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, mcc3);
	tcc1014_mem_cycle(mcc3->GIME, 0, A);
	mcc3->GIME->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mcc3);
}

/* simulate an RTS without otherwise affecting machine state */
static void coco3_op_rts(struct machine *m) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	unsigned int new_pc = m->read_byte(m, mcc3->CPU->reg_s, 0) << 8;
	new_pc |= m->read_byte(m, mcc3->CPU->reg_s + 1, 0);
	mcc3->CPU->reg_s += 2;
	mcc3->CPU->reg_pc = new_pc;
}

static void coco3_dump_ram(struct machine *m, FILE *fd) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	struct ram *ram = mcc3->RAM;
	for (unsigned bank = 0; bank < ram->nbanks; bank++) {
		if (ram->d && ram->d[bank]) {
			fwrite(ram->d[bank], ram->bank_nelems, 1, fd);
		}
	}
}

static int32_t coco3_get_symbol(struct machine *m, const char *label) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	int32_t A;
	if ((A = sym_find(&mcc3->ROM0->symtab, label)) >= 0)
		return A;
	// XXX: search cartridge ROM too
	return A;
}

static void coco3_add_breakpoint(struct machine *m, uint32_t A,
				 DELEGATE_T2(void, bool, uint32) handler) {
	struct part *p = &m->part;
	struct coco3 *mcc3 = (struct coco3 *)m;
	if (!bp_breakpoint_add(&mcc3->breakpoint_set, A, handler)) {
		LOG_MOD_WARN(p->partdb->name, "failed to add breakpoint @ 0x%04x\n", A);
	}
	if (mcc3->breakpoint_set.nbreakpoints) {
		mcc3->CPU->instruction_hook = DELEGATE_AS1(void, uint32, bp_instruction_hook, &mcc3->breakpoint_set);
	}
}

static void coco3_remove_breakpoint(struct machine *m, int32_t A,
				    DELEGATE_T2(void, bool, uint32) handler) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	bp_breakpoint_remove(&mcc3->breakpoint_set, A, handler);
	if (!mcc3->breakpoint_set.nbreakpoints) {
		mcc3->CPU->instruction_hook.func = NULL;
	}
}

static void coco3_add_watchpoint(struct machine *m, bool RnW,
				 uint32_t Astart, uint32_t Aend,
				 DELEGATE_T2(void, bool, uint32) handler) {
	struct part *p = &m->part;
	struct coco3 *mcc3 = (struct coco3 *)m;
	if (!bp_watchpoint_add(&mcc3->watchpoint_set, RnW, Astart, Aend, handler)) {
		LOG_MOD_WARN(p->partdb->name, "failed to add watchpoint @ 0x%04x-0x%04x\n", Astart, Aend);
	}
}

static void coco3_remove_watchpoint(struct machine *m, int RnW,
				    int32_t Astart, uint32_t Aend,
				    DELEGATE_T2(void, bool, uint32) handler) {
	struct coco3 *mcc3 = (struct coco3 *)m;
	bp_watchpoint_remove(&mcc3->watchpoint_set, RnW, Astart, Aend, handler);
}

static uint16_t fetch_vram(void *sptr, uint32_t A) {
	struct coco3 *mcc3 = sptr;
	unsigned bank = mcc3->dat.vram_bank >> 6;
	unsigned Zrow = A & ~1;
	unsigned Zcol = A >> 9;
	uint8_t *Vp = ram_a8(mcc3->RAM, bank, Zrow, Zcol);
	static uint16_t D = 0;
	if (Vp) {
		D = (*Vp << 8) | *(Vp+1);
	}
	return D;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void keyboard_update(void *sptr) {
	struct coco3 *mcc3 = sptr;
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
	struct coco3 *mcc3 = sptr;
	int port = PIA_VALUE_CB2(mcc3->PIA0);
	int axis = PIA_VALUE_CA2(mcc3->PIA0);
	int dac_value = ((mcc3->PIA1->a.out_sink & 0xfc) | 2) << 8;
	int js_value = joystick_read_axis(port, axis);
	if (js_value >= dac_value)
		mcc3->PIA0->a.in_sink |= 0x80;
	else
		mcc3->PIA0->a.in_sink &= 0x7f;
}

static void update_sound_mux_source(void *sptr) {
	struct coco3 *mcc3 = sptr;
	unsigned source = (PIA_VALUE_CB2(mcc3->PIA0) << 1)
	                  | PIA_VALUE_CA2(mcc3->PIA0);
	sound_set_mux_source(mcc3->snd, source);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void pia0a_data_preread(void *sptr) {
	keyboard_update(sptr);
	joystick_update(sptr);
}

static void pia1a_data_postwrite(void *sptr) {
	struct coco3 *mcc3 = sptr;
	sound_set_dac_level(mcc3->snd, (float)(PIA_VALUE_A(mcc3->PIA1) & 0xfc) / 252.);
	tape_update_output(mcc3->tape_interface, mcc3->PIA1->a.out_sink & 0xfc);
}

static void pia1a_control_postwrite(void *sptr) {
	struct coco3 *mcc3 = sptr;
	tape_set_motor(mcc3->tape_interface, PIA_VALUE_CA2(mcc3->PIA1));
}

static void pia1b_data_postwrite(void *sptr) {
	struct coco3 *mcc3 = sptr;
	// Single-bit sound
	bool sbs_enabled = !((mcc3->PIA1->b.out_source ^ mcc3->PIA1->b.out_sink) & (1<<1));
	bool sbs_level = mcc3->PIA1->b.out_source & mcc3->PIA1->b.out_sink & (1<<1);
	sound_set_sbs(mcc3->snd, sbs_enabled, sbs_level);
}

static void pia1b_control_postwrite(void *sptr) {
	struct coco3 *mcc3 = sptr;
	sound_set_mux_enabled(mcc3->snd, PIA_VALUE_CB2(mcc3->PIA1));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* VDG edge delegates */

static void gime_hs(void *sptr, bool level) {
	struct coco3 *mcc3 = sptr;
	mc6821_set_cx1(&mcc3->PIA0->a, level);
}

/*
// PAL CoCos 1&2 invert HS - is this true for coco3?  Probably not...
static void gime_hs_pal_coco(void *sptr, bool level) {
	struct coco3 *mcc3 = sptr;
	mc6821_set_cx1(&mcc3->PIA0->a, !level);
}
*/

static void gime_fs(void *sptr, bool level) {
	struct coco3 *mcc3 = sptr;
	mc6821_set_cx1(&mcc3->PIA0->b, level);
	if (level) {
		sound_update(mcc3->snd);
		mcc3->frame--;
		if (mcc3->frame < 0)
			mcc3->frame = mcc3->frameskip;
		vo_vsync(mcc3->vo, mcc3->frame == 0);
	}
}

static void gime_render_line(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data) {
	struct coco3 *mcc3 = sptr;
	DELEGATE_CALL(mcc3->vo->render_line, burst, npixels, data);
}

// CoCo serial printing ROM hook.
//
// XXX this will generally not work, as it requires a ROM hook and SECB usually
// runs in RAM.  We need a way of tracking that we have a valid image in RAM
// without incurring a massive performance penalty.

static void coco3_print_byte(void *sptr, bool RnW, uint32_t A) {
	struct coco3 *mcc3 = sptr;
	(void)RnW;
	(void)A;
	if (!mcc3->printer_interface) {
		return;
	}
	// Not ROM?
	if (tcc1014_decode(mcc3->GIME, mcc3->CPU->reg_pc) != 0) {
		return;
	}
	int byte = MC6809_REG_A(mcc3->CPU);
	printer_strobe(mcc3->printer_interface, 0, byte);
	printer_strobe(mcc3->printer_interface, 1, byte);
	mcc3->CPU->reg_pc = 0xa2df;
}

/* Sound output can feed back into the single bit sound pin when it's
 * configured as an input. */

static void single_bit_feedback(void *sptr, bool level) {
	struct coco3 *mcc3 = sptr;
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
	struct coco3 *mcc3 = sptr;
	sound_set_tape_level(mcc3->snd, value);
	if (value >= 0.5)
		mcc3->PIA1->a.in_sink &= ~(1<<0);
	else
		mcc3->PIA1->a.in_sink |= (1<<0);
}

/* Catridge signalling */

static void cart_firq(void *sptr, bool level) {
	struct coco3 *mcc3 = sptr;
	mc6821_set_cx1(&mcc3->PIA1->b, level);
	mcc3->GIME->IL0 = level;
}

static void cart_nmi(void *sptr, bool level) {
	struct coco3 *mcc3 = sptr;
	MC6809_NMI_SET(mcc3->CPU, level);
}

static void cart_halt(void *sptr, bool level) {
	struct coco3 *mcc3 = sptr;
	MC6809_HALT_SET(mcc3->CPU, level);
}
