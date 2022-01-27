/** \file
 *
 *  \brief Machine configuration.
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

#include "top-config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "array.h"
#include "c-strcase.h"
#include "slist.h"
#include "xalloc.h"

#include "dkbd.h"
#include "fs.h"
#include "machine.h"
#include "logging.h"
#include "serialise.h"
#include "xroar.h"

#ifdef HAVE_WASM
#include "wasm/wasm.h"
#endif

static const struct ser_struct ser_struct_machine_config[] = {
	SER_STRUCT_ELEM(struct machine_config, description, ser_type_string), // 1
	SER_STRUCT_UNHANDLED(), // 2 - old 'architecture' as int
	SER_STRUCT_ELEM(struct machine_config, cpu, ser_type_int), // 3
	SER_STRUCT_ELEM(struct machine_config, vdg_palette, ser_type_string), // 4
	SER_STRUCT_ELEM(struct machine_config, keymap, ser_type_int), // 5
	SER_STRUCT_ELEM(struct machine_config, tv_standard, ser_type_int), // 6
	SER_STRUCT_ELEM(struct machine_config, tv_input, ser_type_int), // 7
	SER_STRUCT_ELEM(struct machine_config, vdg_type, ser_type_int), // 8
	SER_STRUCT_ELEM(struct machine_config, ram, ser_type_int), // 9
	SER_STRUCT_ELEM(struct machine_config, bas_dfn, ser_type_bool), // 10
	SER_STRUCT_ELEM(struct machine_config, bas_rom, ser_type_string), // 11
	SER_STRUCT_ELEM(struct machine_config, extbas_dfn, ser_type_bool), // 12
	SER_STRUCT_ELEM(struct machine_config, extbas_rom, ser_type_string), // 13
	SER_STRUCT_ELEM(struct machine_config, altbas_dfn, ser_type_bool), // 14
	SER_STRUCT_ELEM(struct machine_config, altbas_rom, ser_type_string), // 15
	SER_STRUCT_ELEM(struct machine_config, ext_charset_rom, ser_type_string), // 16
	SER_STRUCT_ELEM(struct machine_config, default_cart_dfn, ser_type_bool), // 17
	SER_STRUCT_ELEM(struct machine_config, default_cart, ser_type_string), // 18
	SER_STRUCT_ELEM(struct machine_config, cart_enabled, ser_type_bool), // 19
	SER_STRUCT_ELEM(struct machine_config, architecture, ser_type_string), // 20
	SER_STRUCT_ELEM(struct machine_config, opts, ser_type_sds_list), // 21
};
#define N_SER_STRUCT_MACHINE_CONFIG ARRAY_N_ELEMENTS(ser_struct_machine_config)

#define MACHINE_CONFIG_SER_ARCHITECTURE_OLD (2)

static const struct ser_struct ser_struct_machine[] = {
        SER_STRUCT_ELEM(struct machine, config, ser_type_unhandled), // 1
        SER_STRUCT_ELEM(struct machine, keyboard.type, ser_type_int), // 2
};

#define MACHINE_SER_MACHINE_CONFIG (1)

static _Bool machine_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool machine_write_elem(void *sptr, struct ser_handle *sh, int tag);

// External; struct data nested by machines:
const struct ser_struct_data machine_ser_struct_data = {
	.elems = ser_struct_machine,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_machine),
	.read_elem = machine_read_elem,
	.write_elem = machine_write_elem,
};

// Translate old integer machine architecture to string
static const char *int_arch_to_string[5] = {
	"dragon32", "dragon64", "coco", "coco3", "mc10"
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct xconfig_enum machine_keyboard_list[] = {
	{ XC_ENUM_INT("dragon", dkbd_layout_dragon, "Dragon") },
	{ XC_ENUM_INT("dragon200e", dkbd_layout_dragon200e, "Dragon 200-E") },
	{ XC_ENUM_INT("coco", dkbd_layout_coco, "Tandy CoCo 1/2") },
	{ XC_ENUM_INT("coco3", dkbd_layout_coco3, "Tandy CoCo 3") },
	{ XC_ENUM_INT("mc10", dkbd_layout_mc10, "Tandy MC-10") },
	{ XC_ENUM_INT("alice", dkbd_layout_alice, "Matra & Hachette Alice") },
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
	{ XC_ENUM_INT("pal-m", TV_PAL_M, "PAL-M (60Hz)") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_tv_input_list[] = {
	{ XC_ENUM_INT("cmp", TV_INPUT_CMP_PALETTE, "Composite (no cross-colour)") },
	{ XC_ENUM_INT("cmp-br", TV_INPUT_CMP_KBRW, "Composite (blue-red)") },
	{ XC_ENUM_INT("cmp-rb", TV_INPUT_CMP_KRBW, "Composite (red-blue)") },
	{ XC_ENUM_INT("rgb", TV_INPUT_RGB, "RGB") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_vdg_type_list[] = {
	{ XC_ENUM_INT("6847", VDG_6847, "Original 6847") },
	{ XC_ENUM_INT("6847t1", VDG_6847T1, "6847T1 with lowercase") },
	{ XC_ENUM_INT("gime1986", VDG_GIME_1986, "1986 GIME") },
	{ XC_ENUM_INT("gime1987", VDG_GIME_1987, "1987 GIME") },
	{ XC_ENUM_END() }
};

static struct slist *config_list = NULL;
static int next_id = 0;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_config *machine_config_new(void) {
	struct machine_config *new = xmalloc(sizeof(*new));
	*new = (struct machine_config){0};
	new->id = next_id;
	new->cpu = CPU_MC6809;
	new->keymap = ANY_AUTO;
	new->tv_standard = ANY_AUTO;
	new->tv_input = ANY_AUTO;
	new->vdg_type = ANY_AUTO;
	new->ram = ANY_AUTO;
	new->cart_enabled = 1;
	config_list = slist_append(config_list, new);
	next_id++;
	return new;
}

struct machine_config *machine_config_deserialise(struct ser_handle *sh) {
	char *name = ser_read_string(sh);
	if (!name)
		return NULL;
	struct machine_config *mc = machine_config_by_name(name);
	if (!mc) {
		mc = machine_config_new();
		mc->name = xstrdup(name);
	}
	free(name);
	int tag;
	while (!ser_error(sh) && (tag = ser_read_struct(sh, ser_struct_machine_config, N_SER_STRUCT_MACHINE_CONFIG, mc)) > 0) {
		switch (tag) {
		case MACHINE_CONFIG_SER_ARCHITECTURE_OLD: {
			int old_arch = ser_read_vint32(sh);
			if (mc->architecture)
				free(mc->architecture);
			if (old_arch < 0 || old_arch >= (int)ARRAY_N_ELEMENTS(int_arch_to_string))
				old_arch = 0;
			mc->architecture = xstrdup(int_arch_to_string[old_arch]);
		} break;

		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	return mc;
}

void machine_config_serialise(struct ser_handle *sh, unsigned otag, struct machine_config *mc) {
	if (!mc)
		return;
	ser_write_open_string(sh, otag, mc->name);
	for (int tag = 1; !ser_error(sh) && (tag = ser_write_struct(sh, ser_struct_machine_config, N_SER_STRUCT_MACHINE_CONFIG, tag, mc)) > 0; tag++) {
		switch (tag) {
		case MACHINE_CONFIG_SER_ARCHITECTURE_OLD:
			// old field, just ignore here for now
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	ser_write_close_tag(sh);
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
	if (arch < 0  || arch >= (int)ARRAY_N_ELEMENTS(int_arch_to_string))
		return NULL;
	for (struct slist *l = config_list; l; l = l->next) {
		struct machine_config *mc = l->data;
		if (strcmp(mc->architecture, int_arch_to_string[arch]) == 0) {
			return mc;
		}
	}
	return NULL;
}

static _Bool machine_is_working_config(struct machine_config *mc) {
	if (!mc) {
		return 0;
	}
	const struct partdb_entry *pe = partdb_find_entry(mc->architecture);
	if (!partdb_ent_is_a(pe, "machine"))
		return 0;
	const struct machine_partdb_extra *mpe = pe->extra[0];
	assert(mpe != NULL);
	if (mpe->is_working_config && mpe->is_working_config(mc)) {
		return 1;
	}
	return 0;
}

struct machine_config *machine_config_first_working(void) {
	// dragon64 might not be first in the list, but it's been the first one
	// tested for the whole time, so avoid unexpected behaviour by checking
	// it first.
	struct machine_config *d64_mc = machine_config_by_name("dragon64");
	if (machine_is_working_config(d64_mc))
		return d64_mc;
	// otherwise, work through the list
	for (struct slist *iter = config_list; iter; iter = iter->next) {
		struct machine_config *mc = iter->data;
		if (machine_is_working_config(mc))
			return mc;
	}
	// and if none found, just return the non-working dragon64 one
	if (d64_mc)
		return d64_mc;
	assert(config_list != NULL);
	return config_list->data;
}

void machine_config_complete(struct machine_config *mc) {
	if (!mc->description) {
		mc->description = xstrdup(mc->name);
	}
	if (!mc->architecture) {
		mc->architecture = xstrdup("dragon64");
	}
	const struct partdb_entry *pe = partdb_find_entry(mc->architecture);
	if (!partdb_ent_is_a(pe, "machine"))
		return;
	const struct machine_partdb_extra *mpe = pe->extra[0];
	assert(mpe != NULL);
	mpe->config_complete(mc);
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
	slist_free_full(mc->opts, (slist_free_func)sdsfree);
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

void machine_config_remove_all(void) {
	slist_free_full(config_list, (slist_free_func)machine_config_free);
	config_list = NULL;
}

struct slist *machine_config_list(void) {
	return config_list;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void machine_config_print_all(FILE *f, _Bool all) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct machine_config *mc = l->data;
		fprintf(f, "machine %s\n", mc->name);
		xroar_cfg_print_inc_indent();
		xroar_cfg_print_string(f, all, "machine-desc", mc->description, NULL);
		xroar_cfg_print_string(f, all, "machine-arch", mc->architecture, NULL);
		xroar_cfg_print_enum(f, all, "machine-keyboard", mc->keymap, ANY_AUTO, machine_keyboard_list);
		xroar_cfg_print_enum(f, all, "machine-cpu", mc->cpu, CPU_MC6809, machine_cpu_list);
		xroar_cfg_print_string(f, all, "machine-palette", mc->vdg_palette, "ideal");
		// XXX need to indicate definedness here
		xroar_cfg_print_string(f, all, "bas", mc->bas_rom, NULL);
		xroar_cfg_print_string(f, all, "extbas", mc->extbas_rom, NULL);
		xroar_cfg_print_string(f, all, "altbas", mc->altbas_rom, NULL);
		xroar_cfg_print_string(f, all, "ext-charset", mc->ext_charset_rom, NULL);
		xroar_cfg_print_enum(f, all, "tv-type", mc->tv_standard, ANY_AUTO, machine_tv_type_list);
		xroar_cfg_print_enum(f, all, "tv-input", mc->tv_input, ANY_AUTO, machine_tv_input_list);
		xroar_cfg_print_enum(f, all, "vdg-type", mc->vdg_type, ANY_AUTO, machine_vdg_type_list);
		xroar_cfg_print_int_nz(f, all, "ram", mc->ram);
		xroar_cfg_print_string(f, all, "machine-cart", mc->default_cart, NULL); // XXX definedness?
		for (struct slist *i2 = mc->opts; i2; i2 = i2->next) {
			const char *s = i2->data;
			xroar_cfg_print_string(f, all, "machine-opt", s, NULL);
		}
		xroar_cfg_print_dec_indent();
		fprintf(f, "\n");
	}
}

int machine_load_rom(const char *path, uint8_t *dest, off_t max_size) {
	if (path == NULL)
		return -1;

#ifdef HAVE_WASM
	FILE *fd = wasm_fopen(path, "rb");
#else
	FILE *fd = fopen(path, "rb");
#endif

	if (!fd) {
		return -1;
	}

	off_t file_size = fs_file_size(fd);
	if (file_size < 0)
		return -1;
	int header_size = file_size % 256;
	file_size -= header_size;
	if (file_size > max_size)
		file_size = max_size;

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

struct machine *machine_new(struct machine_config *mc) {
	assert(mc != NULL);
	LOG_DEBUG(1, "Machine: [%s] %s\n", mc->architecture, mc->description);
	// sanity check that the part is a machine
	if (!partdb_is_a(mc->architecture, "machine")) {
		return NULL;
	}
	struct machine *m = (struct machine *)part_create(mc->architecture, mc);
	if (m && !part_is_a((struct part *)m, "machine")) {
		part_free((struct part *)m);
		m = NULL;
	}
	return m;
}

_Bool machine_is_a(struct part *p, const char *name) {
	(void)p;
	return strcmp(name, "machine") == 0;
}

static _Bool machine_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine *m = sptr;
	switch (tag) {
	case MACHINE_SER_MACHINE_CONFIG:
		m->config = machine_config_deserialise(sh);
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool machine_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine *m = sptr;
	switch (tag) {
	case MACHINE_SER_MACHINE_CONFIG:
		machine_config_serialise(sh, tag, m->config);
		break;
	default:
		return 0;
	}
	return 1;
}
