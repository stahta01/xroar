/** \file
 *
 *  \brief Machine configuration.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
#include "ram.h"
#include "serialise.h"
#include "xroar.h"

#define MACHINE_CONFIG_SER_ARCHITECTURE_OLD (2)

static const struct ser_struct ser_struct_machine_config[] = {
	SER_ID_STRUCT_ELEM(1,  struct machine_config, description),
	SER_ID_STRUCT_UNHANDLED(MACHINE_CONFIG_SER_ARCHITECTURE_OLD),  // old 'architecture'
	SER_ID_STRUCT_ELEM(3,  struct machine_config, cpu),
	SER_ID_STRUCT_ELEM(4,  struct machine_config, vdg_palette),
	SER_ID_STRUCT_ELEM(5,  struct machine_config, keymap),
	SER_ID_STRUCT_ELEM(6,  struct machine_config, tv_standard),
	SER_ID_STRUCT_ELEM(7,  struct machine_config, tv_input),
	SER_ID_STRUCT_ELEM(8,  struct machine_config, vdg_type),
	SER_ID_STRUCT_ELEM(22, struct machine_config, ram_org),
	SER_ID_STRUCT_ELEM(9,  struct machine_config, ram),
	SER_ID_STRUCT_ELEM(23, struct machine_config, ram_init),
	SER_ID_STRUCT_ELEM(10, struct machine_config, bas_dfn),
	SER_ID_STRUCT_ELEM(11, struct machine_config, bas_rom),
	SER_ID_STRUCT_ELEM(12, struct machine_config, extbas_dfn),
	SER_ID_STRUCT_ELEM(13, struct machine_config, extbas_rom),
	SER_ID_STRUCT_ELEM(14, struct machine_config, altbas_dfn),
	SER_ID_STRUCT_ELEM(15, struct machine_config, altbas_rom),
	SER_ID_STRUCT_ELEM(16, struct machine_config, ext_charset_rom),
	SER_ID_STRUCT_ELEM(17, struct machine_config, default_cart_dfn),
	SER_ID_STRUCT_ELEM(18, struct machine_config, default_cart),
	SER_ID_STRUCT_ELEM(19, struct machine_config, cart_enabled),
	SER_ID_STRUCT_ELEM(20, struct machine_config, architecture),
	SER_ID_STRUCT_TYPE(21, ser_type_sds_list, struct machine_config, opts),
};

static _Bool machine_config_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool machine_config_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data machine_config_ser_struct_data = {
	.elems = ser_struct_machine_config,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_machine_config),
	.read_elem = machine_config_read_elem,
	.write_elem = machine_config_write_elem,
};

#define MACHINE_SER_MACHINE_CONFIG (1)

static const struct ser_struct ser_struct_machine[] = {
	SER_ID_STRUCT_UNHANDLED(MACHINE_SER_MACHINE_CONFIG),
	SER_ID_STRUCT_ELEM(2, struct machine, keyboard.type),
};

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
	{ XC_ENUM_INT("cmp", TV_INPUT_SVIDEO, "S-Video") },
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

struct xconfig_enum machine_ram_org_list[] = {
	{ XC_ENUM_INT("4kx1", RAM_ORG_4Kx1, "4K x 1 (e.g. MK4096)") },
	{ XC_ENUM_INT("16kx1", RAM_ORG_16Kx1, "16K x 1 (e.g. 4116)") },
	//{ XC_ENUM_INT("16kx4", RAM_ORG_16Kx4, "16K x 4 (e.g. 4416)") },
	{ XC_ENUM_INT("32kx1", RAM_ORG_32Kx1, "32K x 1 (e.g. 4532)") },
	{ XC_ENUM_INT("64kx1", RAM_ORG_64Kx1, "64K x 1 (e.g. 4164)") },
	//{ XC_ENUM_INT("256kx1", RAM_ORG_256Kx1, "256K x 1 (e.g. 41256)") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_ram_init_list[] = {
	{ XC_ENUM_INT("clear", ram_init_clear, "Clear (all bits 0)") },
	{ XC_ENUM_INT("set", ram_init_set, "Set (all bits 1)") },
	{ XC_ENUM_INT("pattern", ram_init_pattern, "Pattern") },
	{ XC_ENUM_INT("random", ram_init_random, "Random") },
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
	new->ram_org = ANY_AUTO;
	new->ram = ANY_AUTO;
	new->ram_init = ANY_AUTO;
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
	ser_read_struct_data(sh, &machine_config_ser_struct_data, mc);
	return mc;
}

void machine_config_serialise(struct ser_handle *sh, unsigned otag, struct machine_config *mc) {
	if (!mc)
		return;
	ser_write_open_string(sh, otag, mc->name);
	ser_write_struct_data(sh, &machine_config_ser_struct_data, mc);
}

static _Bool machine_config_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct machine_config *mc = sptr;
	switch (tag) {
	case MACHINE_CONFIG_SER_ARCHITECTURE_OLD: {
		int old_arch = ser_read_vint32(sh);
		if (old_arch < 0 || (unsigned)old_arch >= ARRAY_N_ELEMENTS(int_arch_to_string)) {
			ser_set_error(sh, ser_error_format);
			return 0;
		}
		if (mc->architecture) {
			free(mc->architecture);
		}
		mc->architecture = xstrdup(int_arch_to_string[old_arch]);
	} break;

	default:
		return 0;
	}
	return 1;
}

static _Bool machine_config_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	(void)sptr;
	(void)sh;
	switch (tag) {
	case MACHINE_CONFIG_SER_ARCHITECTURE_OLD:
		// old field, just ignore here for now
		break;
	default:
		return 0;
	}
	return 1;
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
	const struct machine_partdb_entry *mpe = (const struct machine_partdb_entry *)pe;
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
	const struct machine_partdb_entry *mpe = (const struct machine_partdb_entry *)pe;
	if (mpe->config_complete) {
		mpe->config_complete(mc);
	}
}

static void machine_config_free(struct machine_config *mc) {
	if (mc->name)
		free(mc->name);
	if (mc->description)
		free(mc->description);
	if (mc->architecture)
		free(mc->architecture);
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

static void machine_config_free_void(void *sptr) {
	machine_config_free((struct machine_config *)sptr);
}

void machine_config_remove_all(void) {
	slist_free_full(config_list, (slist_free_func)machine_config_free_void);
	config_list = NULL;
}

struct slist *machine_config_list(void) {
	assert(config_list != NULL);
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
		xroar_cfg_print_enum(f, all, "ram-org", mc->ram_org, ANY_AUTO, machine_ram_org_list);
		xroar_cfg_print_int_nz(f, all, "ram", mc->ram);
		xroar_cfg_print_enum(f, all, "ram-init", mc->ram_init, ANY_AUTO, machine_ram_init_list);
		xroar_cfg_print_string(f, all, "machine-cart", mc->default_cart, NULL); // XXX definedness?
		for (struct slist *i2 = mc->opts; i2; i2 = i2->next) {
			const char *s = i2->data;
			xroar_cfg_print_string(f, all, "machine-opt", s, NULL);
		}
		xroar_cfg_print_dec_indent();
		fprintf(f, "\n");
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine *machine_new(struct machine_config *mc) {
	assert(mc != NULL);
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
