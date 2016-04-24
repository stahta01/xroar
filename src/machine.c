/*  Copyright 2003-2016 Ciaran Anscomb
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
#include "gdb.h"
#include "hd6309.h"
#include "hd6309_trace.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "mc6809_trace.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
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

static struct slist *config_list = NULL;
static int next_id = 0;

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

struct machine_dragon_interface {
	struct machine_interface public;
	struct MC6809 *CPU0;
	struct MC6883 *SAM0;
	struct MC6821 *PIA0, *PIA1;
	struct MC6847 *VDG0;

	unsigned int ram_size;
	uint8_t ram[0x10000];
	uint8_t *rom;
	uint8_t rom0[0x4000];
	uint8_t rom1[0x4000];
	uint8_t ext_charset[0x1000];
	struct machine_memory ram0;  // introspection
	struct machine_memory ram1;  // introspection

	_Bool inverted_text;
	_Bool fast_sound;
	struct cart *cart;

	int cycles;

	struct bp_session *bp_session;
	_Bool single_step;
	int stop_signal;
#ifdef WANT_GDB_TARGET
	struct gdb_interface *gdb_interface;
#endif
	_Bool trace;

	struct tape_interface *tape_interface;
	struct keyboard_interface *keyboard_interface;
	struct printer_interface *printer_interface;

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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_config *machine_config_new(void) {
	struct machine_config *new = xmalloc(sizeof(*new));
	*new = (struct machine_config){0};
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

void machine_config_shutdown(void) {
	slist_free_full(config_list, (slist_free_func)machine_config_free);
	config_list = NULL;
}

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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct machine_interface *machine_dragon_new(struct machine_config *mc);

struct machine_interface *machine_interface_new(struct machine_config *mc) {
	switch (mc->architecture) {
	default:
		return machine_dragon_new(mc);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon_free(struct machine_interface *mi);

static void dragon_reset(struct machine_interface *mi, _Bool hard);
static enum machine_run_state dragon_run(struct machine_interface *mi, int ncycles);
static void dragon_single_step(struct machine_interface *mi);
static void dragon_signal(struct machine_interface *mi, int sig);

static _Bool dragon_set_pause(struct machine_interface *mi, int state);
static _Bool dragon_set_trace(struct machine_interface *mi, int state);
static _Bool dragon_set_fast_sound(struct machine_interface *mi, int state);
static _Bool dragon_set_inverted_text(struct machine_interface *mi, int state);
static void *dragon_get_component(struct machine_interface *mi, const char *cname);
static void *dragon_get_interface(struct machine_interface *mi, const char *ifname);

static uint8_t dragon_read_byte(struct machine_interface *mi, unsigned A);
static void dragon_write_byte(struct machine_interface *mi, unsigned A, unsigned D);
static void dragon_op_rts(struct machine_interface *mi);

static void keyboard_update(void *sptr);
static void joystick_update(void *sptr);
static void update_sound_mux_source(void *sptr);
static void update_vdg_mode(struct machine_dragon_interface *mdi);

static void single_bit_feedback(void *sptr, _Bool level);
static void update_audio_from_tape(void *sptr, float value);
static void cart_firq(void *sptr, _Bool level);
static void cart_nmi(void *sptr, _Bool level);
static void cart_halt(void *sptr, _Bool level);
static void vdg_hs(void *sptr, _Bool level);
static void vdg_hs_pal_coco(void *sptr, _Bool level);
static void vdg_fs(void *sptr, _Bool level);
static void printer_ack(void *sptr, _Bool ack);

static void cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A);
static void cpu_cycle_noclock(void *sptr, int ncycles, _Bool RnW, uint16_t A);
static void machine_instruction_posthook(void *sptr);
static void vdg_fetch_handler(void *sptr, int nbytes, uint16_t *dest);
static void vdg_fetch_handler_chargen(void *sptr, int nbytes, uint16_t *dest);

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

static struct machine_interface *machine_dragon_new(struct machine_config *mc) {
	if (!mc)
		return NULL;

	struct machine_dragon_interface *mdi = xmalloc(sizeof(*mdi));
	*mdi = (struct machine_dragon_interface){0};
	struct machine_interface *mi = &mdi->public;

	machine_config_complete(mc);
	if (mc->description) {
		LOG_DEBUG(1, "Machine: %s\n", mc->description);
	}

	mi->config = mc;
	mi->free = dragon_free;
	mi->reset = dragon_reset;
	mi->run = dragon_run;
	mi->single_step = dragon_single_step;
	mi->signal = dragon_signal;

	mi->set_pause = dragon_set_pause;
	mi->set_trace = dragon_set_trace;
	mi->set_fast_sound = dragon_set_fast_sound;
	mi->set_inverted_text = dragon_set_inverted_text;
	mi->get_component = dragon_get_component;
	mi->get_interface = dragon_get_interface;

	mi->read_byte = dragon_read_byte;
	mi->write_byte = dragon_write_byte;
	mi->op_rts = dragon_op_rts;

	switch (mc->architecture) {
	case ARCH_DRAGON32:
		mdi->is_dragon32 = mdi->is_dragon = 1;
		break;
	case ARCH_DRAGON64:
		mdi->is_dragon64 = mdi->is_dragon = 1;
		break;
	default:
		break;
	}

	// SAM
	mdi->SAM0 = sam_new();
	mdi->SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mdi);
	// CPU
	switch (mc->cpu) {
	case CPU_MC6809: default:
		mdi->CPU0 = mc6809_new();
		break;
	case CPU_HD6309:
		mdi->CPU0 = hd6309_new();
		break;
	}
	mdi->CPU0->mem_cycle = DELEGATE_AS2(void, bool, uint16, sam_mem_cycle, mdi->SAM0);

	// Breakpoint session
	mdi->bp_session = bp_session_new(mi);

	// Keyboard interface
	mdi->keyboard_interface = keyboard_interface_new(mi);

	// Tape interface
	mdi->tape_interface = tape_interface_new(mc->architecture, mi, mdi->keyboard_interface);

	// Printer interface
	mdi->printer_interface = printer_interface_new(mi);

	// PIAs
	mdi->PIA0 = mc6821_new();
	mdi->PIA0->a.data_preread = DELEGATE_AS0(void, pia0a_data_preread, mdi);
	mdi->PIA0->a.data_postwrite = DELEGATE_AS0(void, pia0a_data_postwrite, mdi);
	mdi->PIA0->a.control_postwrite = DELEGATE_AS0(void, pia0a_control_postwrite, mdi);
	mdi->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, mdi);
	mdi->PIA0->b.data_postwrite = DELEGATE_AS0(void, pia0b_data_postwrite, mdi);
	mdi->PIA0->b.control_postwrite = DELEGATE_AS0(void, pia0b_control_postwrite, mdi);
	mdi->PIA1 = mc6821_new();
	mdi->PIA1->a.data_preread = DELEGATE_AS0(void, pia1a_data_preread, mdi);
	mdi->PIA1->a.data_postwrite = DELEGATE_AS0(void, pia1a_data_postwrite, mdi);
	mdi->PIA1->a.control_postwrite = DELEGATE_AS0(void, pia1a_control_postwrite, mdi);
	mdi->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread, mdi);
	mdi->PIA1->b.data_postwrite = DELEGATE_AS0(void, pia1b_data_postwrite, mdi);
	mdi->PIA1->b.control_postwrite = DELEGATE_AS0(void, pia1b_control_postwrite, mdi);

	// Single-bit sound feedback
	sound_sbs_feedback = DELEGATE_AS1(void, bool, single_bit_feedback, mdi);

	// Tape
	mdi->tape_interface->update_audio = DELEGATE_AS1(void, float, update_audio_from_tape, mdi);

	// VDG
	mdi->VDG0 = mc6847_new(mc->vdg_type == VDG_6847T1);
	// XXX kludges that should be handled by machine-specific code
	mdi->VDG0->is_dragon64 = mdi->is_dragon64;
	mdi->VDG0->is_dragon32 = mdi->is_dragon32;
	mdi->VDG0->is_coco = !mdi->is_dragon;
	_Bool is_pal = (mc->tv_standard == TV_PAL);
	mdi->VDG0->is_pal = is_pal;

	if (!mdi->is_dragon && is_pal) {
		mdi->VDG0->signal_hs = DELEGATE_AS1(void, bool, vdg_hs_pal_coco, mdi);
	} else {
		mdi->VDG0->signal_hs = DELEGATE_AS1(void, bool, vdg_hs, mdi);
	}
	mdi->VDG0->signal_fs = DELEGATE_AS1(void, bool, vdg_fs, mdi);
	mdi->VDG0->fetch_data = DELEGATE_AS2(void, int, uint16p, vdg_fetch_handler, mdi);
	mc6847_set_inverted_text(mdi->VDG0, mdi->inverted_text);

	// Printer
	mdi->printer_interface->signal_ack = DELEGATE_AS1(void, bool, printer_ack, mdi);

	/* Load appropriate ROMs */
	memset(mdi->rom0, 0, sizeof(mdi->rom0));
	memset(mdi->rom1, 0, sizeof(mdi->rom1));
	memset(mdi->ext_charset, 0, sizeof(mdi->ext_charset));

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

	mdi->has_combined = mdi->has_extbas = mdi->has_bas = mdi->has_altbas = 0;
	mdi->crc_combined = mdi->crc_extbas = mdi->crc_bas = mdi->crc_altbas = 0;
	mdi->has_ext_charset = 0;
	mdi->crc_ext_charset = 0;

	/* ... Extended BASIC */
	if (!mc->noextbas && mc->extbas_rom) {
		char *tmp = romlist_find(mc->extbas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, mdi->rom0, sizeof(mdi->rom0));
			if (size > 0) {
				if (mdi->is_dragon)
					mdi->has_combined = 1;
				else
					mdi->has_extbas = 1;
			}
			if (size > 0x2000) {
				if (!mdi->has_combined)
					mdi->has_bas = 1;
			}
			free(tmp);
		}
	}

	/* ... BASIC */
	if (!mc->nobas && mc->bas_rom) {
		char *tmp = romlist_find(mc->bas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, mdi->rom0 + 0x2000, sizeof(mdi->rom0) - 0x2000);
			if (size > 0)
				mdi->has_bas = 1;
			free(tmp);
		}
	}

	/* ... 64K mode Extended BASIC */
	if (!mc->noaltbas && mc->altbas_rom) {
		char *tmp = romlist_find(mc->altbas_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, mdi->rom1, sizeof(mdi->rom1));
			if (size > 0)
				mdi->has_altbas = 1;
			free(tmp);
		}
	}
	mdi->ram_size = mc->ram * 1024;
	mdi->ram0.max_size = 0x8000;
	mdi->ram0.size = (mdi->ram_size > 0x8000) ? 0x8000 : mdi->ram_size;
	mdi->ram0.data = mdi->ram;
	mdi->ram1.max_size = 0x8000;
	mdi->ram1.size = (mdi->ram_size > 0x8000) ? (mdi->ram_size - 0x8000) : 0;
	mdi->ram1.data = mdi->ram + 0x8000;
	/* This will be under PIA control on a Dragon 64 */
	mdi->rom = mdi->rom0;

	if (mc->ext_charset_rom) {
		char *tmp = romlist_find(mc->ext_charset_rom);
		if (tmp) {
			int size = machine_load_rom(tmp, mdi->ext_charset, sizeof(mdi->ext_charset));
			if (size > 0)
				mdi->has_ext_charset = 1;
			free(tmp);
		}
	}

	/* CRCs */

	if (mdi->has_combined) {
		_Bool forced = 0, valid_crc = 0;

		mdi->crc_combined = crc32_block(CRC32_RESET, mdi->rom0, 0x4000);

		if (mdi->is_dragon64)
			valid_crc = crclist_match("@d64_1", mdi->crc_combined);
		else if (mdi->is_dragon32)
			valid_crc = crclist_match("@d32", mdi->crc_combined);

		if (xroar_cfg.force_crc_match) {
			if (mdi->is_dragon64) {
				mdi->crc_combined = 0x84f68bf9;  // Dragon 64 32K mode BASIC
				forced = 1;
			} else if (mdi->is_dragon32) {
				mdi->crc_combined = 0xe3879310;  // Dragon 32 32K mode BASIC
				forced = 1;
			}
		}

		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\t32K mode BASIC CRC = 0x%08x%s\n", mdi->crc_combined, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for combined BASIC ROM\n");
		}
	}

	if (mdi->has_altbas) {
		_Bool forced = 0, valid_crc = 0;

		mdi->crc_altbas = crc32_block(CRC32_RESET, mdi->rom1, 0x4000);

		if (mdi->is_dragon64)
			valid_crc = crclist_match("@d64_2", mdi->crc_altbas);

		if (xroar_cfg.force_crc_match) {
			if (mdi->is_dragon64) {
				mdi->crc_altbas = 0x17893a42;  // Dragon 64 64K mode BASIC
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\t64K mode BASIC CRC = 0x%08x%s\n", mdi->crc_altbas, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for alternate BASIC ROM\n");
		}
	}

	if (mdi->has_bas) {
		_Bool forced = 0, valid_crc = 0, coco4k = 0;

		mdi->crc_bas = crc32_block(CRC32_RESET, mdi->rom0 + 0x2000, 0x2000);

		if (!mdi->is_dragon) {
			if (mc->ram > 4) {
				valid_crc = crclist_match("@coco", mdi->crc_bas);
			} else {
				valid_crc = crclist_match("@bas10", mdi->crc_bas);
				coco4k = 1;
			}
		}

		if (xroar_cfg.force_crc_match) {
			if (!mdi->is_dragon) {
				if (mc->ram > 4) {
					mdi->crc_bas = 0xd8f4d15e;  // CoCo BASIC 1.3
				} else {
					mdi->crc_bas = 0x00b50aaa;  // CoCo BASIC 1.0
				}
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\tBASIC CRC = 0x%08x%s\n", mdi->crc_bas, forced ? " (forced)" : "");
		if (!valid_crc) {
			if (coco4k) {
				LOG_WARN("Invalid CRC for Colour BASIC 1.0 ROM\n");
			} else {
				LOG_WARN("Invalid CRC for Colour BASIC ROM\n");
			}
		}
	}

	if (mdi->has_extbas) {
		_Bool forced = 0, valid_crc = 0;

		mdi->crc_extbas = crc32_block(CRC32_RESET, mdi->rom0, 0x2000);

		if (!mdi->is_dragon) {
			valid_crc = crclist_match("@cocoext", mdi->crc_extbas);
		}

		if (xroar_cfg.force_crc_match) {
			if (!mdi->is_dragon) {
				mdi->crc_extbas = 0xa82a6254;  // CoCo Extended BASIC 1.1
				forced = 1;
			}
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\tExtended BASIC CRC = 0x%08x%s\n", mdi->crc_extbas, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for Extended Colour BASIC ROM\n");
		}
	}
	if (mdi->has_ext_charset) {
		mdi->crc_ext_charset = crc32_block(CRC32_RESET, mdi->ext_charset, 0x1000);
		LOG_DEBUG(1, "\tExternal charset CRC = 0x%08x\n", mdi->crc_ext_charset);
	}

	/* VDG external charset */
	if (mdi->has_ext_charset)
		mdi->VDG0->fetch_data = DELEGATE_AS2(void, int, uint16p, vdg_fetch_handler_chargen, mdi);

	/* Default all PIA connections to unconnected (no source, no sink) */
	mdi->PIA0->b.in_source = 0;
	mdi->PIA1->b.in_source = 0;
	mdi->PIA0->a.in_sink = mdi->PIA0->b.in_sink = 0xff;
	mdi->PIA1->a.in_sink = mdi->PIA1->b.in_sink = 0xff;
	/* Machine-specific PIA connections */
	if (mdi->is_dragon) {
		/* Centronics printer port - !BUSY */
		mdi->PIA1->b.in_source |= (1<<0);
	}
	if (mdi->is_dragon64) {
		mdi->have_acia = 1;
		mdi->PIA1->b.in_source |= (1<<2);
	} else if (!mdi->is_dragon && mdi->ram_size <= 0x1000) {
		/* 4K CoCo ties PB2 of mdi->PIA1 low */
		mdi->PIA1->b.in_sink &= ~(1<<2);
	} else if (!mdi->is_dragon && mdi->ram_size <= 0x4000) {
		/* 16K CoCo pulls PB2 of mdi->PIA1 high */
		mdi->PIA1->b.in_source |= (1<<2);
	}
	mdi->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread, mdi);
	if (mdi->is_dragon) {
		/* Dragons need to poll printer BUSY state */
		mdi->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread_dragon, mdi);
	}
	if (!mdi->is_dragon && mdi->ram_size > 0x4000) {
		/* 64K CoCo connects PB6 of mdi->PIA0 to PB2 of mdi->PIA1->
		 * Deal with this through a postwrite. */
		mdi->PIA0->b.data_preread = DELEGATE_AS0(void, pia0b_data_preread_coco64k, mdi);
		mdi->PIA1->b.data_preread = DELEGATE_AS0(void, pia1b_data_preread_coco64k, mdi);
	}

	if (mdi->is_dragon) {
		keyboard_set_chord_mode(mdi->keyboard_interface, keyboard_chord_mode_dragon_32k_basic);
	} else {
		keyboard_set_chord_mode(mdi->keyboard_interface, keyboard_chord_mode_coco_basic);
	}

	mdi->unexpanded_dragon32 = 0;
	mdi->relaxed_pia_decode = 0;
	mdi->ram_mask = 0xffff;

	if (!mdi->is_dragon) {
		if (mdi->ram_size <= 0x2000) {
			mdi->ram_organisation = RAM_ORGANISATION_4K;
			mdi->ram_mask = 0x3f3f;
		} else if (mdi->ram_size <= 0x4000) {
			mdi->ram_organisation = RAM_ORGANISATION_16K;
		} else {
			mdi->ram_organisation = RAM_ORGANISATION_64K;
			if (mdi->ram_size <= 0x8000)
				mdi->ram_mask = 0x7fff;
		}
		mdi->relaxed_pia_decode = 1;
	}

	if (mdi->is_dragon) {
		mdi->ram_organisation = RAM_ORGANISATION_64K;
		if (mdi->is_dragon32 && mdi->ram_size <= 0x8000) {
			mdi->unexpanded_dragon32 = 1;
			mdi->relaxed_pia_decode = 1;
			mdi->ram_mask = 0x7fff;
		}
	}

	mdi->fast_sound = xroar_cfg.fast_sound;

	keyboard_set_keymap(mdi->keyboard_interface, xroar_machine_config->keymap);

#ifdef WANT_GDB_TARGET
	// GDB
	if (xroar_cfg.gdb) {
		mdi->gdb_interface = gdb_interface_new(xroar_cfg.gdb_ip, xroar_cfg.gdb_port, mi, mdi->bp_session);
		gdb_set_debug(mdi->gdb_interface, xroar_cfg.debug_gdb);
	}
#endif

	return mi;
}

static void dragon_free(struct machine_interface *mi) {
	if (!mi)
		return;
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	if (mi->config && mi->config->description) {
		LOG_DEBUG(1, "Machine shutdown: %s\n", mi->config->description);
	}
	machine_remove_cart(mi);
	if (mdi->gdb_interface) {
		gdb_interface_free(mdi->gdb_interface);
	}
	if (mdi->keyboard_interface) {
		keyboard_interface_free(mdi->keyboard_interface);
	}
	if (mdi->tape_interface) {
		tape_interface_free(mdi->tape_interface);
	}
	if (mdi->printer_interface) {
		printer_interface_free(mdi->printer_interface);
	}
	if (mdi->bp_session) {
		bp_session_free(mdi->bp_session);
	}
	if (mdi->SAM0) {
		sam_free(mdi->SAM0);
	}
	if (mdi->CPU0) {
		mdi->CPU0->free(mdi->CPU0);
	}
	if (mdi->PIA0) {
		mc6821_free(mdi->PIA0);
	}
	if (mdi->PIA1) {
		mc6821_free(mdi->PIA1);
	}
	if (mdi->VDG0) {
		mc6847_free(mdi->VDG0);
	}
	free(mdi);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void keyboard_update(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	unsigned buttons = ~(joystick_read_buttons() & 3);
	struct keyboard_state state = {
		.row_source = mdi->PIA0->a.out_sink,
		.row_sink = mdi->PIA0->a.out_sink & buttons,
		.col_source = mdi->PIA0->b.out_source,
		.col_sink = mdi->PIA0->b.out_sink,
	};
	keyboard_read_matrix(mdi->keyboard_interface, &state);
	mdi->PIA0->a.in_sink = state.row_sink;
	mdi->PIA0->b.in_source = state.col_source;
	mdi->PIA0->b.in_sink = state.col_sink;
}

static void joystick_update(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	int port = (mdi->PIA0->b.control_register & 0x08) >> 3;
	int axis = (mdi->PIA0->a.control_register & 0x08) >> 3;
	int dac_value = (mdi->PIA1->a.out_sink & 0xfc) + 2;
	int js_value = joystick_read_axis(port, axis);
	if (js_value >= dac_value)
		mdi->PIA0->a.in_sink |= 0x80;
	else
		mdi->PIA0->a.in_sink &= 0x7f;
}

static void update_sound_mux_source(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	unsigned source = ((mdi->PIA0->b.control_register & (1<<3)) >> 2)
	                  | ((mdi->PIA0->a.control_register & (1<<3)) >> 3);
	sound_set_mux_source(source);
}

static void update_vdg_mode(struct machine_dragon_interface *mdi) {
	unsigned vmode = (mdi->PIA1->b.out_source & mdi->PIA1->b.out_sink) & 0xf8;
	// ¬INT/EXT = GM0
	vmode |= (vmode & 0x10) << 4;
	mc6847_set_mode(mdi->VDG0, vmode);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void pia0a_data_preread(void *sptr) {
	(void)sptr;
	keyboard_update(sptr);
	joystick_update(sptr);
}

static void pia0b_data_preread_coco64k(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	keyboard_update(mdi);
	/* PB6 of mdi->PIA0 is linked to PB2 of mdi->PIA1 on 64K CoCos */
	if ((mdi->PIA1->b.out_source & mdi->PIA1->b.out_sink) & (1<<2)) {
		mdi->PIA0->b.in_source |= (1<<6);
		mdi->PIA0->b.in_sink |= (1<<6);
	} else {
		mdi->PIA0->b.in_source &= ~(1<<6);
		mdi->PIA0->b.in_sink &= ~(1<<6);
	}
}

static void pia1a_data_postwrite(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	sound_set_dac_level((float)(PIA_VALUE_A(mdi->PIA1) & 0xfc) / 252.);
	tape_update_output(mdi->tape_interface, mdi->PIA1->a.out_sink & 0xfc);
	if (mdi->is_dragon) {
		keyboard_update(mdi);
		printer_strobe(mdi->printer_interface, PIA_VALUE_A(mdi->PIA1) & 0x02, PIA_VALUE_B(mdi->PIA0));
	}
}

static void pia1a_control_postwrite(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	tape_update_motor(mdi->tape_interface, mdi->PIA1->a.control_register & 0x08);
}

static void pia1b_data_preread_dragon(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	if (printer_busy(mdi->printer_interface))
		mdi->PIA1->b.in_sink |= 0x01;
	else
		mdi->PIA1->b.in_sink &= ~0x01;
}

static void pia1b_data_preread_coco64k(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	/* PB6 of mdi->PIA0 is linked to PB2 of mdi->PIA1 on 64K CoCos */
	if ((mdi->PIA0->b.out_source & mdi->PIA0->b.out_sink) & (1<<6)) {
		mdi->PIA1->b.in_source |= (1<<2);
		mdi->PIA1->b.in_sink |= (1<<2);
	} else {
		mdi->PIA1->b.in_source &= ~(1<<2);
		mdi->PIA1->b.in_sink &= ~(1<<2);
	}
}

static void pia1b_data_postwrite(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	if (mdi->is_dragon64) {
		_Bool is_32k = PIA_VALUE_B(mdi->PIA1) & 0x04;
		if (is_32k) {
			mdi->rom = mdi->rom0;
			keyboard_set_chord_mode(mdi->keyboard_interface, keyboard_chord_mode_dragon_32k_basic);
		} else {
			mdi->rom = mdi->rom1;
			keyboard_set_chord_mode(mdi->keyboard_interface, keyboard_chord_mode_dragon_64k_basic);
		}
	}
	// Single-bit sound
	_Bool sbs_enabled = !((mdi->PIA1->b.out_source ^ mdi->PIA1->b.out_sink) & (1<<1));
	_Bool sbs_level = mdi->PIA1->b.out_source & mdi->PIA1->b.out_sink & (1<<1);
	sound_set_sbs(sbs_enabled, sbs_level);
	// VDG mode
	update_vdg_mode(mdi);
}

static void pia1b_control_postwrite(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	sound_set_mux_enabled(mdi->PIA1->b.control_register & 0x08);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* VDG edge delegates */

static void vdg_hs(void *sptr, _Bool level) {
	struct machine_dragon_interface *mdi = sptr;
	mc6821_set_cx1(&mdi->PIA0->a, level);
	sam_vdg_hsync(mdi->SAM0, level);
}

// PAL CoCos invert HS
static void vdg_hs_pal_coco(void *sptr, _Bool level) {
	struct machine_dragon_interface *mdi = sptr;
	mc6821_set_cx1(&mdi->PIA0->a, !level);
	sam_vdg_hsync(mdi->SAM0, level);
}

static void vdg_fs(void *sptr, _Bool level) {
	struct machine_dragon_interface *mdi = sptr;
	mc6821_set_cx1(&mdi->PIA0->b, level);
	if (level)
		sound_update();
	sam_vdg_fsync(mdi->SAM0, level);
}

/* Dragon parallel printer line delegate. */

//ACK is active low
static void printer_ack(void *sptr, _Bool ack) {
	struct machine_dragon_interface *mdi = sptr;
	mc6821_set_cx1(&mdi->PIA1->a, !ack);
}

/* Sound output can feed back into the single bit sound pin when it's
 * configured as an input. */

static void single_bit_feedback(void *sptr, _Bool level) {
	struct machine_dragon_interface *mdi = sptr;
	if (level) {
		mdi->PIA1->b.in_source &= ~(1<<1);
		mdi->PIA1->b.in_sink &= ~(1<<1);
	} else {
		mdi->PIA1->b.in_source |= (1<<1);
		mdi->PIA1->b.in_sink |= (1<<1);
	}
}

/* Tape audio delegate */

static void update_audio_from_tape(void *sptr, float value) {
	struct machine_dragon_interface *mdi = sptr;
	sound_set_tape_level(value);
	if (value >= 0.5)
		mdi->PIA1->a.in_sink &= ~(1<<0);
	else
		mdi->PIA1->a.in_sink |= (1<<0);
}

/* Catridge signalling */

static void cart_firq(void *sptr, _Bool level) {
	struct machine_dragon_interface *mdi = sptr;
	(void)mdi;
	mc6821_set_cx1(&mdi->PIA1->b, level);
}

static void cart_nmi(void *sptr, _Bool level) {
	struct machine_dragon_interface *mdi = sptr;
	MC6809_NMI_SET(mdi->CPU0, level);
}

static void cart_halt(void *sptr, _Bool level) {
	struct machine_dragon_interface *mdi = sptr;
	MC6809_HALT_SET(mdi->CPU0, level);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon_reset(struct machine_interface *mi, _Bool hard) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
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
		/* Intialise RAM contents */
		int loc = 0, val = 0xff;
		/* Don't know why, but RAM seems to start in this state: */
		while (loc < 0x10000) {
			mdi->ram[loc++] = val;
			mdi->ram[loc++] = val;
			mdi->ram[loc++] = val;
			mdi->ram[loc++] = val;
			if ((loc & 0xff) != 0)
				val ^= 0xff;
		}
	}
	mc6821_reset(mdi->PIA0);
	mc6821_reset(mdi->PIA1);
	if (mdi->cart && mdi->cart->reset) {
		mdi->cart->reset(mdi->cart);
	}
	sam_reset(mdi->SAM0);
	mdi->CPU0->reset(mdi->CPU0);
#ifdef TRACE
	mc6809_trace_reset();
	hd6309_trace_reset();
#endif
	mc6847_reset(mdi->VDG0);
	tape_reset(mdi->tape_interface);
	printer_reset(mdi->printer_interface);
}

static enum machine_run_state dragon_run(struct machine_interface *mi, int ncycles) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;

#ifdef WANT_GDB_TARGET
	if (mdi->gdb_interface) {
		switch (gdb_run_lock(mdi->gdb_interface)) {
		case gdb_run_state_timeout:
			return machine_run_state_timeout;
		case gdb_run_state_running:
			mdi->stop_signal = 0;
			mdi->cycles += ncycles;
			mdi->CPU0->running = 1;
			mdi->CPU0->run(mdi->CPU0);
			if (mdi->stop_signal != 0) {
				gdb_stop(mdi->gdb_interface, mdi->stop_signal);
			}
			break;
		case gdb_run_state_single_step:
			mi->single_step(mi);
			gdb_single_step(mdi->gdb_interface);
			break;
		}
		gdb_run_unlock(mdi->gdb_interface);
		return mdi->stop_signal ? machine_run_state_stopped : machine_run_state_ok;
	} else {
#endif
		mdi->cycles += ncycles;
		mdi->CPU0->running = 1;
		mdi->CPU0->run(mdi->CPU0);
		return machine_run_state_ok;
#ifdef WANT_GDB_TARGET
	}
#endif
}

static void dragon_single_step(struct machine_interface *mi) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	mdi->single_step = 1;
	mdi->CPU0->running = 0;
	mdi->CPU0->instruction_posthook = DELEGATE_AS0(void, machine_instruction_posthook, mdi);
	do {
		mdi->CPU0->run(mdi->CPU0);
	} while (mdi->single_step);
	update_vdg_mode(mdi);
	if (xroar_cfg.trace_enabled)
		mdi->CPU0->instruction_posthook.func = NULL;
}

/*
 * Stop emulation and set stop_signal to reflect the reason.
 */

static void dragon_signal(struct machine_interface *mi, int sig) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	update_vdg_mode(mdi);
	mdi->stop_signal = sig;
	mdi->CPU0->running = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool dragon_set_pause(struct machine_interface *mi, int state) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	switch (state) {
	case 0: case 1:
		mdi->CPU0->halt = state;
		break;
	case 2:
		mdi->CPU0->halt = !mdi->CPU0->halt;
		break;
	default:
		break;
	}
	return mdi->CPU0->halt;
}

static _Bool dragon_set_trace(struct machine_interface *mi, int state) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	switch (state) {
	case 0: case 1:
		mdi->trace = state;
		break;
	case 2:
		mdi->trace = !mdi->trace;
		break;
	default:
		break;
	}
	if (mdi->trace || mdi->single_step)
		mdi->CPU0->instruction_posthook = DELEGATE_AS0(void, machine_instruction_posthook, mdi);
	else
		mdi->CPU0->instruction_posthook.func = NULL;
	return mdi->trace;
}

static _Bool dragon_set_fast_sound(struct machine_interface *mi, int action) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	switch (action) {
	case 0: case 1:
		mdi->fast_sound = action;
		break;
	case 2:
		mdi->fast_sound = !mdi->fast_sound;
		break;
	default:
		break;
	}
	// TODO: move dragon-specific sound code here
	xroar_cfg.fast_sound = mdi->fast_sound;
	return mdi->fast_sound;
}

static _Bool dragon_set_inverted_text(struct machine_interface *mi, int action) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	switch (action) {
	case 0: case 1:
		mdi->inverted_text = action;
		break;
	case 2:
		mdi->inverted_text = !mdi->inverted_text;
		break;
	default:
		break;
	}
	mc6847_set_inverted_text(mdi->VDG0, mdi->inverted_text);
	return mdi->inverted_text;
}

/*
 * Device inspection.
 */

/* Note, this is SLOW.  Could be sped up by maintaining a hash by component
 * name, but will only ever be used outside critical path, so don't bother for
 * now. */

static void *dragon_get_component(struct machine_interface *mi, const char *cname) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	if (0 == strcmp(cname, "CPU0")) {
		return mdi->CPU0;
	} else if (0 == strcmp(cname, "SAM0")) {
		return mdi->SAM0;
	} else if (0 == strcmp(cname, "PIA0")) {
		return mdi->PIA0;
	} else if (0 == strcmp(cname, "PIA1")) {
		return mdi->PIA1;
	} else if (0 == strcmp(cname, "RAM0")) {
		return &mdi->ram0;
	} else if (0 == strcmp(cname, "RAM1")) {
		return &mdi->ram1;
	}
	return NULL;
}

/* Similarly SLOW.  Used to populate UI. */

static void *dragon_get_interface(struct machine_interface *mi, const char *ifname) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	if (0 == strcmp(ifname, "cart")) {
		return mdi->cart;
	} else if (0 == strcmp(ifname, "keyboard")) {
		return mdi->keyboard_interface;
	} else if (0 == strcmp(ifname, "printer")) {
		return mdi->printer_interface;
	} else if (0 == strcmp(ifname, "tape")) {
		return mdi->tape_interface;
	}
	return NULL;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Used when single-stepping or tracing.
 */

static void machine_instruction_posthook(void *sptr) {
	struct machine_dragon_interface *mdi = sptr;
	if (xroar_cfg.trace_enabled) {
		switch (mdi->CPU0->variant) {
		case MC6809_VARIANT_MC6809: default:
			mc6809_trace_print(mdi->CPU0);
			break;
		case MC6809_VARIANT_HD6309:
			hd6309_trace_print(mdi->CPU0);
			break;
		}
	}
	mdi->single_step = 0;
}

static uint16_t decode_Z(struct machine_dragon_interface *mdi, unsigned Z) {
	switch (mdi->ram_organisation) {
	case RAM_ORGANISATION_4K:
		return (Z & 0x3f) | ((Z & 0x3f00) >> 2) | ((~Z & 0x8000) >> 3);
	case RAM_ORGANISATION_16K:
		return (Z & 0x7f) | ((Z & 0x7f00) >> 1) | ((~Z & 0x8000) >> 1);
	case RAM_ORGANISATION_64K: default:
		return Z & mdi->ram_mask;
	}
}

static void read_byte(struct machine_dragon_interface *mdi, unsigned A) {
	// Thanks to CrAlt on #coco_chat for verifying that RAM accesses
	// produce a different "null" result on his 16K CoCo
	if (mdi->SAM0->RAS)
		mdi->CPU0->D = 0xff;
	switch (mdi->SAM0->S) {
	case 0:
		if (mdi->SAM0->RAS) {
			unsigned Z = decode_Z(mdi, mdi->SAM0->Z);
			if (Z < mdi->ram_size)
				mdi->CPU0->D = mdi->ram[Z];
		}
		break;
	case 1:
	case 2:
		mdi->CPU0->D = mdi->rom[A & 0x3fff];
		break;
	case 3:
		if (mdi->cart)
			mdi->CPU0->D = mdi->cart->read(mdi->cart, A, 0, mdi->CPU0->D);
		break;
	case 4:
		if (mdi->relaxed_pia_decode) {
			mdi->CPU0->D = mc6821_read(mdi->PIA0, A);
		} else {
			if ((A & 4) == 0) {
				mdi->CPU0->D = mc6821_read(mdi->PIA0, A);
			} else {
				if (mdi->have_acia) {
					/* XXX Dummy ACIA reads */
					switch (A & 3) {
					default:
					case 0:  /* Receive Data */
					case 3:  /* Control */
						mdi->CPU0->D = 0x00;
						break;
					case 2:  /* Command */
						mdi->CPU0->D = 0x02;
						break;
					case 1:  /* Status */
						mdi->CPU0->D = 0x10;
						break;
					}
				}
			}
		}
		break;
	case 5:
		if (mdi->relaxed_pia_decode || (A & 4) == 0) {
			mdi->CPU0->D = mc6821_read(mdi->PIA1, A);
		}
		break;
	case 6:
		if (mdi->cart)
			mdi->CPU0->D = mdi->cart->read(mdi->cart, A, 1, mdi->CPU0->D);
		break;
		// Should call cart's read() whatever the address and
		// indicate P2 and CTS.
	case 7:
		if (mdi->cart)
			mdi->CPU0->D = mdi->cart->read(mdi->cart, A, 0, mdi->CPU0->D);
		break;
	default:
		break;
	}
}

static void write_byte(struct machine_dragon_interface *mdi, unsigned A) {
	if ((mdi->SAM0->S & 4) || mdi->unexpanded_dragon32) {
		switch (mdi->SAM0->S) {
		case 1:
		case 2:
			mdi->CPU0->D = mdi->rom[A & 0x3fff];
			break;
		case 3:
			if (mdi->cart)
				mdi->cart->write(mdi->cart, A, 0, mdi->CPU0->D);
			break;
		case 4:
			if (!mdi->is_dragon || mdi->unexpanded_dragon32) {
				mc6821_write(mdi->PIA0, A, mdi->CPU0->D);
			} else {
				if ((A & 4) == 0) {
					mc6821_write(mdi->PIA0, A, mdi->CPU0->D);
				}
			}
			break;
		case 5:
			if (mdi->relaxed_pia_decode || (A & 4) == 0) {
				mc6821_write(mdi->PIA1, A, mdi->CPU0->D);
			}
			break;
		case 6:
			if (mdi->cart)
				mdi->cart->write(mdi->cart, A, 1, mdi->CPU0->D);
			break;
			// Should call cart's write() whatever the address and
			// indicate P2 and CTS.
		case 7:
			if (mdi->cart)
				mdi->cart->write(mdi->cart, A, 0, mdi->CPU0->D);
			break;
		default:
			break;
		}
	}
	if (mdi->SAM0->RAS) {
		unsigned Z = decode_Z(mdi, mdi->SAM0->Z);
		mdi->ram[Z] = mdi->CPU0->D;
	}
}

static void cpu_cycle(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_dragon_interface *mdi = sptr;
	// Changing the SAM VDG mode can affect its idea of the current VRAM
	// address, so get the VDG output up to date:
	if (!RnW && A >= 0xffc0 && A < 0xffc6) {
		update_vdg_mode(mdi);
	}
	mdi->cycles -= ncycles;
	if (mdi->cycles <= 0) mdi->CPU0->running = 0;
	event_current_tick += ncycles;
	event_run_queue(&MACHINE_EVENT_LIST);
	MC6809_IRQ_SET(mdi->CPU0, mdi->PIA0->a.irq | mdi->PIA0->b.irq);
	MC6809_FIRQ_SET(mdi->CPU0, mdi->PIA1->a.irq | mdi->PIA1->b.irq);

	if (RnW) {
		read_byte(mdi, A);
#ifdef TRACE
		if (xroar_cfg.trace_enabled) {
			switch (mdi->CPU0->variant) {
			case MC6809_VARIANT_MC6809: default:
				mc6809_trace_byte(mdi->CPU0->D, A);
				break;
			case MC6809_VARIANT_HD6309:
				hd6309_trace_byte(mdi->CPU0->D, A);
				break;
			}
		}
#endif
		bp_wp_read_hook(mdi->bp_session, A);
	} else {
		write_byte(mdi, A);
		bp_wp_write_hook(mdi->bp_session, A);
	}
}

static void cpu_cycle_noclock(void *sptr, int ncycles, _Bool RnW, uint16_t A) {
	struct machine_dragon_interface *mdi = sptr;
	(void)ncycles;
	if (RnW) {
		read_byte(mdi, A);
	} else {
		write_byte(mdi, A);
	}
}

static void vdg_fetch_handler(void *sptr, int nbytes, uint16_t *dest) {
	struct machine_dragon_interface *mdi = sptr;
	uint16_t attr = (PIA_VALUE_B(mdi->PIA1) & 0x10) << 6;  // GM0 -> ¬INT/EXT
	while (nbytes > 0) {
		int n = sam_vdg_bytes(mdi->SAM0, nbytes);
		if (dest) {
			uint16_t V = decode_Z(mdi, mdi->SAM0->V);
			for (int i = n; i; i--) {
				uint16_t D = mdi->ram[V++] | attr;
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

static void vdg_fetch_handler_chargen(void *sptr, int nbytes, uint16_t *dest) {
	struct machine_dragon_interface *mdi = sptr;
	unsigned pia_vdg_mode = PIA_VALUE_B(mdi->PIA1);
	_Bool GnA = pia_vdg_mode & 0x80;
	_Bool EnI = pia_vdg_mode & 0x10;
	uint16_t Aram7 = EnI ? 0x80 : 0;
	while (nbytes > 0) {
		int n = sam_vdg_bytes(mdi->SAM0, nbytes);
		if (dest) {
			uint16_t V = decode_Z(mdi, mdi->SAM0->V);
			for (int i = n; i; i--) {
				uint16_t Dram = mdi->ram[V++];;
				_Bool SnA = Dram & 0x80;
				uint16_t D;
				if (!GnA && !SnA) {
					unsigned Aext = (mdi->VDG0->row << 8) | Aram7 | Dram;
					D = mdi->ext_charset[Aext&0xfff] | 0x100;  // set INV
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

static uint8_t dragon_read_byte(struct machine_interface *mi, unsigned A) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	mdi->SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, mdi);
	sam_mem_cycle(mdi->SAM0, 1, A);
	mdi->SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mdi);
	return mdi->CPU0->D;
}

/* Write a byte without advancing clock.  Used for debugging & breakpoints. */

static void dragon_write_byte(struct machine_interface *mi, unsigned A, unsigned D) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	mdi->CPU0->D = D;
	mdi->SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle_noclock, mdi);
	sam_mem_cycle(mdi->SAM0, 0, A);
	mdi->SAM0->cpu_cycle = DELEGATE_AS3(void, int, bool, uint16, cpu_cycle, mdi);
}

/* simulate an RTS without otherwise affecting machine state */
static void dragon_op_rts(struct machine_interface *mi) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	unsigned int new_pc = mi->read_byte(mi, mdi->CPU0->reg_s) << 8;
	new_pc |= mi->read_byte(mi, mdi->CPU0->reg_s + 1);
	mdi->CPU0->reg_s += 2;
	mdi->CPU0->reg_pc = new_pc;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void machine_bp_add_n(struct machine_interface *mi, struct machine_bp *list, int n, void *sptr) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	for (int i = 0; i < n; i++) {
		if ((list[i].add_cond & BP_MACHINE_ARCH) && xroar_machine_config->architecture != list[i].cond_machine_arch)
			continue;
		if ((list[i].add_cond & BP_CRC_COMBINED) && (!mdi->has_combined || !crclist_match(list[i].cond_crc_combined, mdi->crc_combined)))
			continue;
		if ((list[i].add_cond & BP_CRC_EXT) && (!mdi->has_extbas || !crclist_match(list[i].cond_crc_extbas, mdi->crc_extbas)))
			continue;
		if ((list[i].add_cond & BP_CRC_BAS) && (!mdi->has_bas || !crclist_match(list[i].cond_crc_bas, mdi->crc_bas)))
			continue;
		list[i].bp.handler.sptr = sptr;
		bp_add(mdi->bp_session, &list[i].bp);
	}
}

void machine_bp_remove_n(struct machine_interface *mi, struct machine_bp *list, int n) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	(void)mdi;
	for (int i = 0; i < n; i++) {
		bp_remove(mdi->bp_session, &list[i].bp);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void machine_insert_cart(struct machine_interface *mi, struct cart *c) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	machine_remove_cart(mi);
	if (c) {
		assert(c->read != NULL);
		assert(c->write != NULL);
		mdi->cart = c;
		c->signal_firq = DELEGATE_AS1(void, bool, cart_firq, mdi);
		c->signal_nmi = DELEGATE_AS1(void, bool, cart_nmi, mdi);
		c->signal_halt = DELEGATE_AS1(void, bool, cart_halt, mdi);
	}
}

void machine_remove_cart(struct machine_interface *mi) {
	struct machine_dragon_interface *mdi = (struct machine_dragon_interface *)mi;
	(void)mdi;
	cart_free(mdi->cart);
	mdi->cart = NULL;
}
