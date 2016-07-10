/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2016 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

Machine & machine config handling.

*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "c-strcase.h"
#include "slist.h"
#include "xalloc.h"

#include "dkbd.h"
#include "machine.h"
#include "logging.h"
#include "xroar.h"

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
	{ XC_ENUM_INT("pal-m", TV_PAL_M, "PAL-M (60Hz)") },
	{ XC_ENUM_END() }
};

struct xconfig_enum machine_vdg_type_list[] = {
	{ XC_ENUM_INT("6847", VDG_6847, "Original 6847") },
	{ XC_ENUM_INT("6847t1", VDG_6847T1, "6847T1 with lowercase") },
	{ XC_ENUM_END() }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern struct machine_module machine_dragon_module;

static struct slist *machine_modules = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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

void machine_init(void) {
	// reverse order
	machine_modules = slist_prepend(machine_modules, &machine_dragon_module);
}

void machine_shutdown(void) {
	slist_free_full(config_list, (slist_free_func)machine_config_free);
	config_list = NULL;
	slist_free(machine_modules);
	machine_modules = NULL;
}

struct machine *machine_dragon_new(struct machine_config *mc, struct vo_module *vo);

struct machine *machine_new(struct machine_config *mc, struct vo_module *vo, struct tape_interface *ti) {
	if (!mc) {
		return NULL;
	}
	const char *req_type = NULL;
	switch (mc->architecture) {
	default:
		req_type = "dragon";
		break;
	}
	struct machine *m = NULL;
	for (struct slist *iter = machine_modules; iter; iter = iter->next) {
		struct machine_module *mm = iter->data;
		if (c_strcasecmp(req_type, mm->name) == 0) {
			if (mc->description) {
				LOG_DEBUG(2, "Machine module: %s\n", req_type);
				LOG_DEBUG(1, "Machine: %s\n", mc->description);
			}
			m = mm->new(mc, vo, ti);
			break;
		}
	}
	if (!m) {
		LOG_WARN("Machine module '%s' not found for machine '%s'\n", req_type, mc->name);
		return NULL;
	}
	return m;
}
