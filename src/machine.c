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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
#include "machine.h"
#include "logging.h"
#include "serialise.h"
#include "xroar.h"

static const struct ser_struct ser_struct_machine_config[] = {
	SER_STRUCT_ELEM(struct machine_config, description, ser_type_string), // 1
	SER_STRUCT_ELEM(struct machine_config, architecture, ser_type_int), // 2
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
};

#define N_SER_STRUCT_MACHINE_CONFIG ARRAY_N_ELEMENTS(ser_struct_machine_config)

#define MACHINE_SER_MACHINE_CONFIG (1)

static const struct ser_struct ser_struct_machine[] = {
        SER_STRUCT_ELEM(struct machine, config, ser_type_unhandled), // 1
};

#define N_SER_STRUCT_MACHINE ARRAY_N_ELEMENTS(ser_struct_machine)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct xconfig_enum machine_arch_list[] = {
	{ XC_ENUM_INT("dragon64", ARCH_DRAGON64, "Dragon 64") },
	{ XC_ENUM_INT("dragon32", ARCH_DRAGON32, "Dragon 32") },
	{ XC_ENUM_INT("coco", ARCH_COCO, "Tandy CoCo 1/2") },
	{ XC_ENUM_INT("coco3", ARCH_COCO3, "Tandy CoCo 3") },
	{ XC_ENUM_INT("mc10", ARCH_MC10, "Tandy MC-10") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_keyboard_list[] = {
	{ XC_ENUM_INT("dragon", dkbd_layout_dragon, "Dragon") },
	{ XC_ENUM_INT("dragon200e", dkbd_layout_dragon200e, "Dragon 200-E") },
	{ XC_ENUM_INT("coco", dkbd_layout_coco, "Tandy CoCo 1/2") },
	{ XC_ENUM_INT("coco3", dkbd_layout_coco3, "Tandy CoCo 3") },
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
	new->architecture = ANY_AUTO;
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

void machine_config_serialise(struct ser_handle *sh, unsigned otag, struct machine_config *mc) {
	if (!mc)
		return;
	ser_write_open_string(sh, otag, mc->name);
	ser_write_struct(sh, ser_struct_machine_config, N_SER_STRUCT_MACHINE_CONFIG, 1, mc);
	ser_write_close_tag(sh);
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
	ser_read_struct(sh, ser_struct_machine_config, N_SER_STRUCT_MACHINE_CONFIG, mc);
	return mc;
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

void dragon_config_complete(struct machine_config *mc);
void coco3_config_complete(struct machine_config *mc);
void mc10_config_complete(struct machine_config *mc);

void machine_config_complete(struct machine_config *mc) {
	if (!mc->description) {
		mc->description = xstrdup(mc->name);
	}
	switch (mc->architecture) {
	case ARCH_COCO3:
		coco3_config_complete(mc);
		break;
	case ARCH_MC10:
		mc10_config_complete(mc);
		break;
	default:
		dragon_config_complete(mc);
		break;
	}
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
		xroar_cfg_print_enum(f, all, "machine-arch", mc->architecture, ANY_AUTO, machine_arch_list);
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
		xroar_cfg_print_dec_indent();
		fprintf(f, "\n");
	}
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

// Machine arch to part name.  Obviously we could be storing machine arch as a
// string here, but do this mapping for now.
static const char *machine_part(struct machine_config *mc) {
	switch (mc->architecture) {
	case ARCH_DRAGON32:
		return "dragon32";
	case ARCH_DRAGON64:
	default:
		return "dragon64";
	case ARCH_COCO3:
		return "coco3";
	case ARCH_MC10:
		return "mc10";
	}
}

struct machine *machine_new(struct machine_config *mc) {
	assert(mc != NULL);
	LOG_DEBUG(1, "Machine: %s\n", mc->description);
	const char *partname = machine_part(mc);
	// sanity check that the part is a machine
	if (!partdb_is_a(partname, "machine")) {
		return NULL;
	}
	LOG_DEBUG(2, "Machine part: %s\n", partname);
	struct machine *m = (struct machine *)part_create(partname, mc);
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

void machine_serialise(struct machine *m, struct ser_handle *sh, unsigned otag) {
	ser_write_open_string(sh, otag, "MACHINE");
	for (int tag = 1; !ser_error(sh) && (tag = ser_write_struct(sh, ser_struct_machine, N_SER_STRUCT_MACHINE, tag, m)) > 0; tag++) {
		switch (tag) {
		case MACHINE_SER_MACHINE_CONFIG:
			machine_config_serialise(sh, tag, m->config);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	ser_write_close_tag(sh);
}

void machine_deserialise(struct machine *m, struct ser_handle *sh) {
	int tag;
	while (!ser_error(sh) && (tag = ser_read_struct(sh, ser_struct_machine, N_SER_STRUCT_MACHINE, m))) {
		switch (tag) {
		case MACHINE_SER_MACHINE_CONFIG:
			m->config = machine_config_deserialise(sh);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
}
