/*  Copyright 2003-2015 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "array.h"
#include "c-strcase.h"
#include "delegate.h"
#include "slist.h"
#include "xalloc.h"

#include "breakpoint.h"
#include "cart.h"
#include "crc32.h"
#include "crclist.h"
#include "fs.h"
#include "hd6309.h"
#include "hd6309_trace.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "mc6809_trace.h"
#include "mc6821.h"
#include "mc6847.h"
#include "path.h"
#include "printer.h"
#include "romlist.h"
#include "sam.h"
#include "sound.h"
#include "tape.h"
#include "ui.h"
#include "vdrive.h"
#include "wd279x.h"
#include "xconfig.h"
#include "xroar.h"

unsigned int machine_ram_size = 0x10000;  /* RAM in bytes, up to 64K */
uint8_t machine_ram[0x10000];
static uint8_t *machine_rom;
static uint8_t rom0[0x4000];
static uint8_t rom1[0x4000];
struct MC6883 *SAM0 = NULL;
static struct MC6809 *CPU0 = NULL;
static struct MC6821 *PIA0, *PIA1;
static struct MC6847 *VDG0;
struct cart *machine_cart = NULL;
_Bool has_bas, has_extbas, has_altbas, has_combined;
uint32_t crc_bas, crc_extbas, crc_altbas, crc_combined;
static uint8_t ext_charset[0x1000];
_Bool has_ext_charset;
uint32_t crc_ext_charset;
static _Bool inverted_text = 0;

/* Useful configuration side-effect tracking */
static _Bool unexpanded_dragon32 = 0;
static _Bool relaxed_pia_decode = 0;
static enum {
	RAM_ORGANISATION_4K,
	RAM_ORGANISATION_16K,
	RAM_ORGANISATION_64K
} ram_organisation = RAM_ORGANISATION_64K;
static uint16_t ram_mask = 0xffff;
static _Bool have_acia = 0;

static struct {
	const char *bas;
	const char *extbas;
	const char *altbas;
} rom_list[] = {
	{ NULL, "@dragon32", NULL },
	{ NULL, "@dragon64", "@dragon64_alt" },
	{ "@coco", "@coco_ext", NULL }
};

static struct slist *config_list = NULL;
static int next_id = 0;

static void initialise_ram(void);

static int cycles;
static void cpu_cycle(void *m, int ncycles, _Bool RnW, uint16_t A);
static void cpu_cycle_noclock(void *m, int ncycles, _Bool RnW, uint16_t A);
static void vdg_fetch_handler(void *sptr, int nbytes, uint16_t *dest);

static void machine_instruction_posthook(void *);
static _Bool single_step = 0;
static int stop_signal = 0;

/**************************************************************************/

struct machine_config *machine_config_new(void) {
	struct machine_config *new;
	new = xzalloc(sizeof(*new));
	new->id = next_id;
	new->architecture = ANY_AUTO;
	new->cpu = CPU_MC6809;
	new->keymap = ANY_AUTO;
	new->tv_standard = ANY_AUTO;
	new->vdg_type = ANY_AUTO;
	new->ram = ANY_AUTO;
	new->cart_enabled = 1;
	config_list = slist_append(config_list, new);
	next_id++;
	return new;
}

struct machine_config *machine_config_by_id(int id) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct machine_config *mc = l->data;
		if (mc->id == id)
			return mc;
	}
	return NULL;
}

struct machine_config *machine_config_by_name(const char *name) {
	if (!name) return NULL;
	for (struct slist *l = config_list; l; l = l->next) {
		struct machine_config *mc = l->data;
		if (0 == strcmp(mc->name, name)) {
			return mc;
		}
	}
	return NULL;
}

struct machine_config *machine_config_by_arch(int arch) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct machine_config *mc = l->data;
		if (mc->architecture == arch) {
			return mc;
		}
	}
	return NULL;
}

static void machine_config_free(struct machine_config *mc) {
	if (mc->name)
		free(mc->name);
	if (mc->description)
		free(mc->description);
	if (mc->vdg_palette)
		free(mc->vdg_palette);
	if (mc->bas_rom)
		free(mc->bas_rom);
	if (mc->extbas_rom)
		free(mc->extbas_rom);
	if (mc->altbas_rom)
		free(mc->altbas_rom);
	if (mc->ext_charset_rom)
		free(mc->ext_charset_rom);
	if (mc->default_cart)
		free(mc->default_cart);
	free(mc);
}

_Bool machine_config_remove(const char *name) {
	struct machine_config *mc = machine_config_by_name(name);
	if (!mc)
		return 0;
	config_list = slist_remove(config_list, mc);
	machine_config_free(mc);
	return 1;
}

struct slist *machine_config_list(void) {
	return config_list;
}

static int find_working_arch(void) {
	int arch;
	char *tmp = NULL;
	if ((tmp = romlist_find("@dragon64"))) {
		arch = ARCH_DRAGON64;
	} else if ((tmp = romlist_find("@dragon32"))) {
		arch = ARCH_DRAGON32;
	} else if ((tmp = romlist_find("@coco"))) {
		arch = ARCH_COCO;
	} else {
		// Fall back to Dragon 64, which won't start up properly:
		LOG_WARN("Can't find ROMs for any machine.\n");
		arch = ARCH_DRAGON64;
	}
	if (tmp)
		free(tmp);
	return arch;
}

struct machine_config *machine_config_first_working(void) {
	struct machine_config *mc = machine_config_by_arch(find_working_arch());
	if (!mc && config_list)
		mc = config_list->data;
	return mc;
}

void machine_config_complete(struct machine_config *mc) {
	if (!mc->description) {
		mc->description = xstrdup(mc->name);
	}
	if (mc->tv_standard == ANY_AUTO)
		mc->tv_standard = TV_PAL;
	if (mc->vdg_type == ANY_AUTO)
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
			mc->architecture = find_working_arch();
		}
	}
	if (mc->ram < 4 || mc->ram > 64) {
		switch (mc->architecture) {
			case ARCH_DRAGON32:
				mc->ram = 32;
				break;
			default:
				mc->ram = 64;
				break;
		}
	}
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
	if (!mc->nobas && !mc->bas_rom && rom_list[mc->architecture].bas) {
		mc->bas_rom = xstrdup(rom_list[mc->architecture].bas);
	}
	if (!mc->noextbas && !mc->extbas_rom && rom_list[mc->architecture].extbas) {
		mc->extbas_rom = xstrdup(rom_list[mc->architecture].extbas);
	}
	if (!mc->noaltbas && !mc->altbas_rom && rom_list[mc->architecture].altbas) {
		mc->altbas_rom = xstrdup(rom_list[mc->architecture].altbas);
	}
	// Determine a default DOS cartridge if necessary
	if (!mc->default_cart) {
		struct cart_config *cc = cart_find_working_dos(mc);
		if (cc)
			mc->default_cart = xstrdup(cc->name);
	}
}

struct xconfig_enum machine_arch_list[] = {
	{ XC_ENUM_INT("dragon64", ARCH_DRAGON64, "Dragon 64") },
	{ XC_ENUM_INT("dragon32", ARCH_DRAGON32, "Dragon 32") },
	{ XC_ENUM_INT("coco", ARCH_COCO, "Tandy CoCo") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_keyboard_list[] = {
	{ XC_ENUM_INT("dragon", dkbd_layout_dragon, "Dragon") },
	{ XC_ENUM_INT("dragon200e", dkbd_layout_dragon200e, "Dragon 200-E") },
	{ XC_ENUM_INT("coco", dkbd_layout_coco, "Tandy CoCo") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_cpu_list[] = {
	{ XC_ENUM_INT("6809", CPU_MC6809, "Motorola 6809") },
	{ XC_ENUM_INT("6309", CPU_HD6309, "Hitachi 6309 - UNVERIFIED") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_tv_type_list[] = {
	{ XC_ENUM_INT("pal", TV_PAL, "PAL (50Hz)") },
	{ XC_ENUM_INT("ntsc", TV_NTSC, "NTSC (60Hz)") },
	{ XC_ENUM_INT("pal-m", TV_NTSC, "PAL-M (60Hz)") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_vdg_type_list[] = {
	{ XC_ENUM_INT("6847", VDG_6847, "Original 6847") },
	{ XC_ENUM_INT("6847t1", VDG_6847T1, "6847T1 with lowercase") },
	{ XC_ENUM_END() }
};

void machine_config_print_all(_Bool all) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct machine_config *mc = l->data;
		printf("machine %s\n", mc->name);
		xroar_cfg_print_inc_indent();
		xroar_cfg_print_string(all, "machine-desc", mc->description, NULL);
		xroar_cfg_print_enum(all, "machine-arch", mc->architecture, ANY_AUTO, machine_arch_list);
		xroar_cfg_print_enum(all, "machine-keyboard", mc->keymap, ANY_AUTO, machine_keyboard_list);
		xroar_cfg_print_enum(all, "machine-cpu", mc->cpu, CPU_MC6809, machine_cpu_list);
		xroar_cfg_print_string(all, "machine-palette", mc->vdg_palette, "ideal");
		xroar_cfg_print_string(all, "bas", mc->bas_rom, NULL);
		xroar_cfg_print_string(all, "extbas", mc->extbas_rom, NULL);
		xroar_cfg_print_string(all, "altbas", mc->altbas_rom, NULL);
		xroar_cfg_print_bool(all, "nobas", mc->nobas, 0);
		xroar_cfg_print_bool(all, "noextbas", mc->noextbas, 0);
		xroar_cfg_print_bool(all, "noaltbas", mc->noaltbas, 0);
		xroar_cfg_print_string(all, "ext-charset", mc->ext_charset_rom, NULL);
		xroar_cfg_print_enum(all, "tv-type", mc->tv_standard, ANY_AUTO, machine_tv_type_list);
		xroar_cfg_print_enum(all, "vdg-type", mc->vdg_type, ANY_AUTO, machine_vdg_type_list);
		xroar_cfg_print_int_nz(all, "ram", mc->ram);
		xroar_cfg_print_string(all, "machine-cart", mc->default_cart, NULL);
		xroar_cfg_print_bool(all, "nodos", mc->nodos, 0);
		xroar_cfg_print_dec_indent();
		printf("\n");
	}
}

/* ---------------------------------------------------------------------- */

static void keyboard_update(void *m) {
	(void)m;
	unsigned buttons = ~(joystick_read_buttons() & 3);
	struct keyboard_state state = {
		.row_source = PIA0->a.out_sink,
		.row_sink = PIA0->a.out_sink & buttons,
		.col_source = PIA0->b.out_source,
		.col_sink = PIA0->b.out_sink,
	};
	keyboard_read_matrix(&state);
	PIA0->a.in_sink = state.row_sink;
	PIA0->b.in_source = state.col_source;
	PIA0->b.in_sink = state.col_sink;
}

static void joystick_update(void *m) {
	(void)m;
	int port = (PIA0->b.control_register & 0x08) >> 3;
	int axis = (PIA0->a.control_register & 0x08) >> 3;
	int dac_value = (PIA1->a.out_sink & 0xfc) + 2;
	int js_value = joystick_read_axis(port, axis);
	if (js_value >= dac_value)
		PIA0->a.in_sink |= 0x80;
	else
		PIA0->a.in_sink &= 0x7f;
}

static void update_sound_mux_source(void *m) {
	(void)m;
	unsigned source = ((PIA0->b.control_register & (1<<3)) >> 2)
	                  | ((PIA0->a.control_register & (1<<3)) >> 3);
	sound_set_mux_source(source);
}

static void update_vdg_mode(void) {
	unsigned vmode = (PIA1->b.out_source & PIA1->b.out_sink) & 0xf8;
	// ¬INT/EXT = GM0
	vmode |= (vmode & 0x10) << 4;
	mc6847_set_mode(VDG0, vmode);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void pia0a_data_preread(void *m) {
	(void)m;
	keyboard_update(m);
	joystick_update(m);
}

#define pia0a_data_postwrite NULL
#define pia0a_control_postwrite update_sound_mux_source

#define pia0b_data_preread keyboard_update
#define pia0b_data_postwrite NULL
#define pia0b_control_postwrite update_sound_mux_source

static void pia0b_data_preread_coco64k(void *m) {
	(void)m;
	keyboard_update(m);
	/* PB6 of PIA0 is linked to PB2 of PIA1 on 64K CoCos */
	if ((PIA1->b.out_source & PIA1->b.out_sink) & (1<<2)) {
		PIA0->b.in_source |= (1<<6);
		PIA0->b.in_sink |= (1<<6);
	} else {
		PIA0->b.in_source &= ~(1<<6);
		PIA0->b.in_sink &= ~(1<<6);
	}
}

#define pia1a_data_preread NULL

static void pia1a_data_postwrite(void *m) {
	(void)m;
	sound_set_dac_level((float)(PIA_VALUE_A(PIA1) & 0xfc) / 252.);
	tape_update_output(PIA1->a.out_sink & 0xfc);
	if (IS_DRAGON) {
		keyboard_update(m);
		printer_strobe(PIA_VALUE_A(PIA1) & 0x02, PIA_VALUE_B(PIA0));
	}
}

static void pia1a_control_postwrite(void *m) {
	(void)m;
	tape_update_motor(PIA1->a.control_register & 0x08);
}

#define pia1b_data_preread NULL

static void pia1b_data_preread_dragon(void *m) {
	(void)m;
	if (printer_busy())
		PIA1->b.in_sink |= 0x01;
	else
		PIA1->b.in_sink &= ~0x01;
}

static void pia1b_data_preread_coco64k(void *m) {
	(void)m;
	/* PB6 of PIA0 is linked to PB2 of PIA1 on 64K CoCos */
	if ((PIA0->b.out_source & PIA0->b.out_sink) & (1<<6)) {
		PIA1->b.in_source |= (1<<2);
		PIA1->b.in_sink |= (1<<2);
	} else {
		PIA1->b.in_source &= ~(1<<2);
		PIA1->b.in_sink &= ~(1<<2);
	}
}

static void pia1b_data_postwrite(void *m) {
	(void)m;
	if (IS_DRAGON64) {
		_Bool is_32k = PIA_VALUE_B(PIA1) & 0x04;
		if (is_32k) {
			machine_rom = rom0;
			keyboard_set_chord_mode(keyboard_chord_mode_dragon_32k_basic);
		} else {
			machine_rom = rom1;
			keyboard_set_chord_mode(keyboard_chord_mode_dragon_64k_basic);
		}
	}
	// Single-bit sound
	_Bool sbs_enabled = !((PIA1->b.out_source ^ PIA1->b.out_sink) & (1<<1));
	_Bool sbs_level = PIA1->b.out_source & PIA1->b.out_sink & (1<<1);
	sound_set_sbs(sbs_enabled, sbs_level);
	// VDG mode
	update_vdg_mode();
}

static void pia1b_control_postwrite(void *m) {
	(void)m;
	sound_set_mux_enabled(PIA1->b.control_register & 0x08);
}

void machine_init(void) {
	vdrive_init();
	tape_init();
}

static void free_devices(void) {
	if (SAM0) {
		sam_free(SAM0);
		SAM0 = NULL;
	}
	if (CPU0) {
		CPU0->free(CPU0);
		CPU0 = NULL;
	}
	if (PIA0) {
		mc6821_free(PIA0);
		PIA0 = NULL;
	}
	if (PIA1) {
		mc6821_free(PIA1);
		PIA1 = NULL;
	}
	if (VDG0) {
		mc6847_free(VDG0);
		VDG0 = NULL;
	}
}

void machine_shutdown(void) {
	machine_remove_cart();
	tape_shutdown();
	vdrive_shutdown();
	free_devices();
	slist_free_full(config_list, (slist_free_func)machine_config_free);
	config_list = NULL;
}

/* VDG edge delegates */

static void vdg_hs(void *sptr, _Bool level) {
	(void)sptr;
	mc6821_set_cx1(&PIA0->a, level);
	sam_vdg_hsync(SAM0, level);
}

// PAL CoCos invert HS
static void vdg_hs_pal_coco(void *sptr, _Bool level) {
	(void)sptr;
	mc6821_set_cx1(&PIA0->a, !level);
	sam_vdg_hsync(SAM0, level);
}

static void vdg_fs(void *sptr, _Bool level) {
	(void)sptr;
	mc6821_set_cx1(&PIA0->b, level);
	if (level)
		sound_update();
	sam_vdg_fsync(SAM0, level);
}

/* Dragon parallel printer line delegate. */

//ACK is active low
static void printer_ack(void *sptr, _Bool ack) {
	(void)sptr;
	mc6821_set_cx1(&PIA1->a, !ack);
}

/* Sound output can feed back into the single bit sound pin when it's
 * configured as an input. */

static void single_bit_feedback(void *sptr, _Bool level) {
	(void)sptr;
	if (level) {
		PIA1->b.in_source &= ~(1<<1);
		PIA1->b.in_sink &= ~(1<<1);
	} else {
		PIA1->b.in_source |= (1<<1);
		PIA1->b.in_sink |= (1<<1);
	}
}

/* Tape audio delegate */

static void update_audio_from_tape(void *sptr, float value) {
	(void)sptr;
	sound_set_tape_level(value);
	if (value >= 0.5)
		PIA1->a.in_sink &= ~(1<<0);
	else
		PIA1->a.in_sink |= (1<<0);
}

/* Catridge signalling */

static void cart_firq(void *sptr, _Bool level) {
	(void)sptr;
	mc6821_set_cx1(&PIA1->b, level);
}

static void cart_nmi(void *sptr, _Bool level) {
	(void)sptr;
	MC6809_NMI_SET(CPU0, level);
}

static void cart_halt(void *sptr, _Bool level) {
	(void)sptr;
	MC6809_HALT_SET(CPU0, level);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void machine_configure(struct machine_config *mc) {
	if (!mc) return;
	machine_config_complete(mc);
	if (mc->description) {
		LOG_DEBUG(1, "Machine: %s\n", mc->description);
	}
	free_devices();
	// SAM
	SAM0 = sam_new();
	SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, NULL);
	// CPU
	switch (mc->cpu) {
	case CPU_MC6809: default:
		CPU0 = mc6809_new();
		break;
	case CPU_HD6309:
		CPU0 = hd6309_new();
		break;
	}
	CPU0->mem_cycle = DELEGATE_AS2(void, bool, uint16, sam_mem_cycle, SAM0);
	// PIAs
	PIA0 = mc6821_new();
	PIA0->a.data_preread = DELEGATE_AS0(void, pia0a_data_preread, NULL);
	PIA0->a.data_postwrite = DELEGATE_AS0(void, pia0a_data_postwrite, NULL);
	PIA0->a.control_postwrite = DELEGATE_AS0(void, pia0a_control_postwrite, NULL);
	PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, NULL);
	PIA0->b.data_postwrite = DELEGATE_AS0(void, pia0b_data_postwrite, NULL);
	PIA0->b.control_postwrite = DELEGATE_AS0(void, pia0b_control_postwrite, NULL);
	PIA1 = mc6821_new();
	PIA1->a.data_preread = DELEGATE_AS0(void, pia1a_data_preread, NULL);
	PIA1->a.data_postwrite = DELEGATE_AS0(void, pia1a_data_postwrite, NULL);
	PIA1->a.control_postwrite = DELEGATE_AS0(void, pia1a_control_postwrite, NULL);
	PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread, NULL);
	PIA1->b.data_postwrite = DELEGATE_AS0(void, pia1b_data_postwrite, NULL);
	PIA1->b.control_postwrite = DELEGATE_AS0(void, pia1b_control_postwrite, NULL);

	// Single-bit sound feedback
#ifndef FAST_SOUND
	sound_sbs_feedback = DELEGATE_AS1(void, bool, single_bit_feedback, NULL);
#endif

	// Tape
	tape_update_audio = DELEGATE_AS1(void, float, update_audio_from_tape, NULL);

	// VDG
	VDG0 = mc6847_new(mc->vdg_type == VDG_6847T1);

	if (IS_COCO && IS_PAL) {
		VDG0->signal_hs = DELEGATE_AS1(void, bool, vdg_hs_pal_coco, NULL);
	} else {
		VDG0->signal_hs = DELEGATE_AS1(void, bool, vdg_hs, NULL);
	}
	VDG0->signal_fs = DELEGATE_AS1(void, bool, vdg_fs, NULL);
	VDG0->fetch_data = DELEGATE_AS2(void, int, uint16p, vdg_fetch_handler, NULL);
	mc6847_set_inverted_text(VDG0, inverted_text);

	// Printer
	printer_signal_ack = DELEGATE_AS1(void, bool, printer_ack, NULL);

	/* Load appropriate ROMs */
	memset(rom0, 0, sizeof(rom0));
	memset(rom1, 0, sizeof(rom1));
	memset(ext_charset, 0, sizeof(ext_charset));

	/*
	 * CoCo ROMs are always considered to be in two parts: BASIC and
	 * Extended BASIC.
	 *
	 * Later CoCos and clones may have been distributed with only one ROM
	 * containing the combined image.  If Extended BASIC is found to be
	 * more than 8K, it's assumed to be one of these combined ROMs.
	 *
	 * Dragon ROMs are always Extended BASIC only, and even though (some?)
	 * Dragon 32s split this across two pieces of hardware, it doesn't make
	 * sense to consider the two regions separately.
	 *
	 * Dragon 64s also contain a separate 64K mode Extended BASIC.
	 */

	has_combined = has_extbas = has_bas = has_altbas = 0;
	crc_combined = crc_extbas = crc_bas = crc_altbas = 0;
	has_ext_charset = 0;
	crc_ext_charset = 0;

	/* ... Extended BASIC */
	if (!mc->noextbas && mc->extbas_rom) {
		char *tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, rom0, sizeof(rom0));
			if (size > 0) {
				if (IS_DRAGON)
					has_combined = 1;
				else
					has_extbas = 1;
			}
			if (size > 0x2000) {
				if (!has_combined)
					has_bas = 1;
			}
			free(tmp);
		}
	}

	/* ... BASIC */
	if (!mc->nobas && mc->bas_rom) {
		char *tmp = romlist_find(mc->bas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, rom0 + 0x2000, sizeof(rom0) - 0x2000);
			if (size > 0)
				has_bas = 1;
			free(tmp);
		}
	}

	/* ... 64K mode Extended BASIC */
	if (!mc->noaltbas && mc->altbas_rom) {
		char *tmp = romlist_find(mc->altbas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, rom1, sizeof(rom1));
			if (size > 0)
				has_altbas = 1;
			free(tmp);
		}
	}
	machine_ram_size = mc->ram * 1024;
	/* This will be under PIA control on a Dragon 64 */
	machine_rom = rom0;

	if (mc->ext_charset_rom) {
		char *tmp = romlist_find(mc->ext_charset_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, ext_charset, sizeof(ext_charset));
			if (size > 0)
				has_ext_charset = 1;
			free(tmp);
		}
	}

	/* CRCs */

	if (has_combined) {
		_Bool forced = 0, valid_crc = 0;

		crc_combined = crc32_block(CRC32_RESET, rom0, 0x4000);

		if (IS_DRAGON64)
			valid_crc = crclist_match("@d64_1", crc_combined);
		else if (IS_DRAGON32)
			valid_crc = crclist_match("@d32", crc_combined);

		if (xroar_cfg.force_crc_match) {
			if (IS_DRAGON64) {
				crc_combined = 0x84f68bf9;  // Dragon 64 32K mode BASIC
				forced = 1;
			} else if (IS_DRAGON32) {
				crc_combined = 0xe3879310;  // Dragon 32 32K mode BASIC
				forced = 1;
			}
		}

		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\t32K mode BASIC CRC = 0x%08x%s\n", crc_combined, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for combined BASIC ROM\n");
		}
	}

	if (has_altbas) {
		_Bool forced = 0, valid_crc = 0;

		crc_altbas = crc32_block(CRC32_RESET, rom1, 0x4000);

		if (IS_DRAGON64)
			valid_crc = crclist_match("@d64_2", crc_altbas);

		if (xroar_cfg.force_crc_match) {
			if (IS_DRAGON64) {
				crc_altbas = 0x17893a42;  // Dragon 64 64K mode BASIC
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\t64K mode BASIC CRC = 0x%08x%s\n", crc_altbas, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for alternate BASIC ROM\n");
		}
	}

	if (has_bas) {
		_Bool forced = 0, valid_crc = 0, coco4k = 0;

		crc_bas = crc32_block(CRC32_RESET, rom0 + 0x2000, 0x2000);

		if (IS_COCO) {
			if (mc->ram > 4) {
				valid_crc = crclist_match("@coco", crc_bas);
			} else {
				valid_crc = crclist_match("@bas10", crc_bas);
				coco4k = 1;
			}
		}

		if (xroar_cfg.force_crc_match) {
			if (IS_COCO) {
				if (mc->ram > 4) {
					crc_bas = 0xd8f4d15e;  // CoCo BASIC 1.3
				} else {
					crc_bas = 0x00b50aaa;  // CoCo BASIC 1.0
				}
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\tBASIC CRC = 0x%08x%s\n", crc_bas, forced ? " (forced)" : "");
		if (!valid_crc) {
			if (coco4k) {
				LOG_WARN("Invalid CRC for Colour BASIC 1.0 ROM\n");
			} else {
				LOG_WARN("Invalid CRC for Colour BASIC ROM\n");
			}
		}
	}

	if (has_extbas) {
		_Bool forced = 0, valid_crc = 0;

		crc_extbas = crc32_block(CRC32_RESET, rom0, 0x2000);

		if (IS_COCO) {
			valid_crc = crclist_match("@cocoext", crc_extbas);
		}

		if (xroar_cfg.force_crc_match) {
			if (IS_COCO) {
				crc_extbas = 0xa82a6254;  // CoCo Extended BASIC 1.1
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\tExtended BASIC CRC = 0x%08x%s\n", crc_extbas, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for Extended Colour BASIC ROM\n");
		}
	}
	if (has_ext_charset) {
		crc_ext_charset = crc32_block(CRC32_RESET, ext_charset, 0x1000);
		LOG_DEBUG(1, "\tExternal charset CRC = 0x%08x\n", crc_ext_charset);
	}

	/* VDG external charset */
	if (has_ext_charset)
		mc6847_set_ext_charset(VDG0, ext_charset);
	else
		mc6847_set_ext_charset(VDG0, NULL);

	/* Default all PIA connections to unconnected (no source, no sink) */
	PIA0->b.in_source = 0;
	PIA1->b.in_source = 0;
	PIA0->a.in_sink = PIA0->b.in_sink = 0xff;
	PIA1->a.in_sink = PIA1->b.in_sink = 0xff;
	/* Machine-specific PIA connections */
	if (IS_DRAGON) {
		/* Centronics printer port - !BUSY */
		PIA1->b.in_source |= (1<<0);
	}
	if (IS_DRAGON64) {
		have_acia = 1;
		PIA1->b.in_source |= (1<<2);
	} else if (IS_COCO && machine_ram_size <= 0x1000) {
		/* 4K CoCo ties PB2 of PIA1 low */
		PIA1->b.in_sink &= ~(1<<2);
	} else if (IS_COCO && machine_ram_size <= 0x4000) {
		/* 16K CoCo pulls PB2 of PIA1 high */
		PIA1->b.in_source |= (1<<2);
	}
	PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, NULL);
	if (IS_DRAGON) {
		/* Dragons need to poll printer BUSY state */
		PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread_dragon, NULL);
	}
	if (IS_COCO && machine_ram_size > 0x4000) {
		/* 64K CoCo connects PB6 of PIA0 to PB2 of PIA1->
		 * Deal with this through a postwrite. */
		PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread_coco64k, NULL);
		PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread_coco64k, NULL);
	}

	if (IS_DRAGON) {
		keyboard_set_chord_mode(keyboard_chord_mode_dragon_32k_basic);
	} else {
		keyboard_set_chord_mode(keyboard_chord_mode_coco_basic);
	}

	unexpanded_dragon32 = 0;
	relaxed_pia_decode = 0;
	ram_mask = 0xffff;

	if (IS_COCO) {
		if (machine_ram_size <= 0x2000) {
			ram_organisation = RAM_ORGANISATION_4K;
			ram_mask = 0x3f3f;
		} else if (machine_ram_size <= 0x4000) {
			ram_organisation = RAM_ORGANISATION_16K;
		} else {
			ram_organisation = RAM_ORGANISATION_64K;
			if (machine_ram_size <= 0x8000)
				ram_mask = 0x7fff;
		}
		relaxed_pia_decode = 1;
	}

	if (IS_DRAGON) {
		ram_organisation = RAM_ORGANISATION_64K;
		if (IS_DRAGON32 && machine_ram_size <= 0x8000) {
			unexpanded_dragon32 = 1;
			relaxed_pia_decode = 1;
			ram_mask = 0x7fff;
		}
	}

#ifndef FAST_SOUND
	machine_select_fast_sound(xroar_cfg.fast_sound);
#endif
}

void machine_reset(_Bool hard) {
	xroar_set_keymap(1, xroar_machine_config->keymap);
	switch (xroar_machine_config->tv_standard) {
	case TV_PAL: default:
		xroar_set_cross_colour(1, CROSS_COLOUR_OFF);
		break;
	case TV_NTSC:
		xroar_set_cross_colour(1, CROSS_COLOUR_KBRW);
		break;
	}
	if (hard) {
		initialise_ram();
	}
	mc6821_reset(PIA0);
	mc6821_reset(PIA1);
	if (machine_cart && machine_cart->reset) {
		machine_cart->reset(machine_cart);
	}
	sam_reset(SAM0);
	CPU0->reset(CPU0);
#ifdef TRACE
	mc6809_trace_reset();
	hd6309_trace_reset();
#endif
	mc6847_reset(VDG0);
	tape_reset();
}

int machine_run(int ncycles) {
	stop_signal = 0;
	cycles += ncycles;
	CPU0->running = 1;
	CPU0->run(CPU0);
	return stop_signal;
}

void machine_single_step(void) {
	single_step = 1;
	CPU0->running = 0;
	CPU0->instruction_posthook = DELEGATE_AS0(void, machine_instruction_posthook, CPU0);
	do {
		CPU0->run(CPU0);
	} while (single_step);
	update_vdg_mode();
	if (xroar_cfg.trace_enabled)
		CPU0->instruction_posthook.func = NULL;
}

/*
 * Stop emulation and set stop_signal to reflect the reason.
 */

void machine_signal(int sig) {
	update_vdg_mode();
	stop_signal = sig;
	CPU0->running = 0;
}

void machine_set_trace(_Bool trace_on) {
	if (trace_on || single_step)
		CPU0->instruction_posthook = DELEGATE_AS0(void, machine_instruction_posthook, CPU0);
	else
		CPU0->instruction_posthook.func = NULL;
}

/*
 * Device inspection.
 */

int machine_num_cpus(void) {
	return 1;
}

int machine_num_pias(void) {
	return 2;
}

struct MC6809 *machine_get_cpu(int n) {
	if (n != 0)
		return NULL;
	return CPU0;
}

struct MC6821 *machine_get_pia(int n) {
	if (n == 0)
		return PIA0;
	if (n == 1)
		return PIA1;
	return NULL;
}

/*
 * Used when single-stepping or tracing.
 */

static void machine_instruction_posthook(void *sptr) {
	struct MC6809 *cpu = sptr;
	if (xroar_cfg.trace_enabled) {
		switch (xroar_machine_config->cpu) {
		case CPU_MC6809: default:
			mc6809_trace_print(cpu);
			break;
		case CPU_HD6309:
			hd6309_trace_print(cpu);
			break;
		}
	}
	single_step = 0;
}

static uint16_t decode_Z(unsigned Z) {
	switch (ram_organisation) {
	case RAM_ORGANISATION_4K:
		return (Z & 0x3f) | ((Z & 0x3f00) >> 2) | ((~Z & 0x8000) >> 3);
	case RAM_ORGANISATION_16K:
		return (Z & 0x7f) | ((Z & 0x7f00) >> 1) | ((~Z & 0x8000) >> 1);
	case RAM_ORGANISATION_64K: default:
		return Z & ram_mask;
	}
}

static void read_byte(unsigned A) {
	// Thanks to CrAlt on #coco_chat for verifying that RAM accesses
	// produce a different "null" result on his 16K CoCo
	if (SAM0->RAS)
		CPU0->D = 0xff;
	switch (SAM0->S) {
	case 0:
		if (SAM0->RAS) {
			unsigned Z = decode_Z(SAM0->Z);
			if (Z < machine_ram_size)
				CPU0->D = machine_ram[Z];
		}
		break;
	case 1:
	case 2:
		CPU0->D = machine_rom[A & 0x3fff];
		break;
	case 3:
		if (machine_cart)
			CPU0->D = machine_cart->read(machine_cart, A, 0, CPU0->D);
		break;
	case 4:
		if (relaxed_pia_decode) {
			CPU0->D = mc6821_read(PIA0, A);
		} else {
			if ((A & 4) == 0) {
				CPU0->D = mc6821_read(PIA0, A);
			} else {
				if (have_acia) {
					/* XXX Dummy ACIA reads */
					switch (A & 3) {
					default:
					case 0:  /* Receive Data */
					case 3:  /* Control */
						CPU0->D = 0x00;
						break;
					case 2:  /* Command */
						CPU0->D = 0x02;
						break;
					case 1:  /* Status */
						CPU0->D = 0x10;
						break;
					}
				}
			}
		}
		break;
	case 5:
		if (relaxed_pia_decode || (A & 4) == 0) {
			CPU0->D = mc6821_read(PIA1, A);
		}
		break;
	case 6:
		if (machine_cart)
			CPU0->D = machine_cart->read(machine_cart, A, 1, CPU0->D);
		break;
		// Should call cart's read() whatever the address and
		// indicate P2 and CTS.
	case 7:
		if (machine_cart)
			CPU0->D = machine_cart->read(machine_cart, A, 0, CPU0->D);
		break;
	default:
		break;
	}
}

static void write_byte(unsigned A) {
	if ((SAM0->S & 4) || unexpanded_dragon32) {
		switch (SAM0->S) {
		case 1:
		case 2:
			CPU0->D = machine_rom[A & 0x3fff];
			break;
		case 3:
			if (machine_cart)
				machine_cart->write(machine_cart, A, 0, CPU0->D);
			break;
		case 4:
			if (IS_COCO || unexpanded_dragon32) {
				mc6821_write(PIA0, A, CPU0->D);
			} else {
				if ((A & 4) == 0) {
					mc6821_write(PIA0, A, CPU0->D);
				}
			}
			break;
		case 5:
			if (relaxed_pia_decode || (A & 4) == 0) {
				mc6821_write(PIA1, A, CPU0->D);
			}
			break;
		case 6:
			if (machine_cart)
				machine_cart->write(machine_cart, A, 1, CPU0->D);
			break;
			// Should call cart's write() whatever the address and
			// indicate P2 and CTS.
		case 7:
			if (machine_cart)
				machine_cart->write(machine_cart, A, 0, CPU0->D);
			break;
		default:
			break;
		}
	}
	if (SAM0->RAS) {
		unsigned Z = decode_Z(SAM0->Z);
		machine_ram[Z] = CPU0->D;
	}
}

static void cpu_cycle(void *m, int ncycles, _Bool RnW, uint16_t A) {
	(void)m;
	// Changing the SAM VDG mode can affect its idea of the current VRAM
	// address, so get the VDG output up to date:
	if (!RnW && A >= 0xffc0 && A < 0xffc6) {
		update_vdg_mode();
	}
	cycles -= ncycles;
	if (cycles <= 0) CPU0->running = 0;
	event_current_tick += ncycles;
	event_run_queue(&MACHINE_EVENT_LIST);
	MC6809_IRQ_SET(CPU0, PIA0->a.irq | PIA0->b.irq);
	MC6809_FIRQ_SET(CPU0, PIA1->a.irq | PIA1->b.irq);

	if (RnW) {
		read_byte(A);
#ifdef TRACE
		if (xroar_cfg.trace_enabled) {
			switch (xroar_machine_config->cpu) {
			case CPU_MC6809: default:
				mc6809_trace_byte(CPU0->D, A);
				break;
			case CPU_HD6309:
				hd6309_trace_byte(CPU0->D, A);
				break;
			}
		}
#endif
		bp_wp_read_hook(A);
	} else {
		write_byte(A);
		bp_wp_write_hook(A);
	}
}

static void cpu_cycle_noclock(void *m, int ncycles, _Bool RnW, uint16_t A) {
	(void)m;
	(void)ncycles;
	if (RnW) {
		read_byte(A);
	} else {
		write_byte(A);
	}
}

static void vdg_fetch_handler(void *sptr, int nbytes, uint16_t *dest) {
	(void)sptr;
	uint16_t attr = (PIA_VALUE_B(PIA1) & 0x10) << 6;  // GM0 -> ¬INT/EXT
	while (nbytes > 0) {
		int n = sam_vdg_bytes(SAM0, nbytes);
		if (dest) {
			uint16_t V = decode_Z(SAM0->V);
			if (has_ext_charset) {
				/* omit INV */
				for (int i = n; i; i--) {
					uint16_t D = machine_ram[V++] | EXT;
					D |= (D & 0x80) << 2;  // D7 -> ¬A/S
					*(dest++) = D;
				}
			} else {
				for (int i = n; i; i--) {
					uint16_t D = machine_ram[V++] | EXT;
					D |= (D & 0xc0) << 2;  // D7,D6 -> ¬A/S,INV
					*(dest++) = D;
				}
			}
		}
		nbytes -= n;
	}
}

void machine_toggle_pause(void) {
	CPU0->halt = !CPU0->halt;
}

/* Read a byte without advancing clock.  Used for debugging & breakpoints. */

uint8_t machine_read_byte(unsigned A) {
	SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, NULL);
	sam_mem_cycle(SAM0, 1, A);
	SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, NULL);
	return CPU0->D;
}

/* Write a byte without advancing clock.  Used for debugging & breakpoints. */

void machine_write_byte(unsigned A, unsigned D) {
	CPU0->D = D;
	SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, NULL);
	sam_mem_cycle(SAM0, 0, A);
	SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, NULL);
}

/* simulate an RTS without otherwise affecting machine state */
void machine_op_rts(struct MC6809 *cpu) {
	unsigned int new_pc = machine_read_byte(cpu->reg_s) << 8;
	new_pc |= machine_read_byte(cpu->reg_s + 1);
	cpu->reg_s += 2;
	cpu->reg_pc = new_pc;
}

#ifndef FAST_SOUND
void machine_set_fast_sound(_Bool fast) {
	xroar_cfg.fast_sound = fast;
}

void machine_select_fast_sound(_Bool fast) {
	ui_module->set_state(ui_tag_fast_sound, fast, NULL);
	machine_set_fast_sound(fast);
}
#endif

void machine_set_inverted_text(_Bool invert) {
	inverted_text = invert;
	mc6847_set_inverted_text(VDG0, invert);
}

void machine_insert_cart(struct cart *c) {
	machine_remove_cart();
	if (c) {
		assert(c->read != NULL);
		assert(c->write != NULL);
		machine_cart = c;
		c->signal_firq = DELEGATE_AS1(void, bool, cart_firq, NULL);
		c->signal_nmi = DELEGATE_AS1(void, bool, cart_nmi, NULL);
		c->signal_halt = DELEGATE_AS1(void, bool, cart_halt, NULL);
	}
}

void machine_remove_cart(void) {
	cart_free(machine_cart);
	machine_cart = NULL;
}

/* Intialise RAM contents */
static void initialise_ram(void) {
	int loc = 0, val = 0xff;
	/* Don't know why, but RAM seems to start in this state: */
	while (loc < 0x10000) {
		machine_ram[loc++] = val;
		machine_ram[loc++] = val;
		machine_ram[loc++] = val;
		machine_ram[loc++] = val;
		if ((loc & 0xff) != 0)
			val ^= 0xff;
	}
}

/**************************************************************************/

int machine_load_rom(const char *path, uint8_t *dest, off_t max_size) {
	FILE *fd;

	if (path == NULL)
		return -1;

	struct stat statbuf;
	if (stat(path, &statbuf) != 0)
		return -1;
	off_t file_size = statbuf.st_size;
	int header_size = file_size % 256;
	file_size -= header_size;
	if (file_size > max_size)
		file_size = max_size;

	if (!(fd = fopen(path, "rb"))) {
		return -1;
	}
	LOG_DEBUG(1, "Loading ROM image: %s\n", path);

	if (header_size > 0) {
		LOG_DEBUG(2, "\tskipping %d byte header\n", header_size);
		fseek(fd, header_size, SEEK_SET);
	}

	size_t size = fread(dest, 1, file_size, fd);
	fclose(fd);
	return size;
}
