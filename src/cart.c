/*

Dragon/CoCo cartridge support.

Copyright 2005-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "c-strcase.h"
#include "slist.h"
#include "xalloc.h"

#include "cart.h"
#include "crc32.h"
#include "delegate.h"
#include "events.h"
#include "fs.h"
#include "idecart.h"
#include "logging.h"
#include "machine.h"
#include "romlist.h"
#include "xconfig.h"
#include "xroar.h"

static struct slist *config_list = NULL;
static int next_id = 0;

/* Single config for auto-defined ROM carts */
static struct cart_config *rom_cart_config = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct cart *cart_rom_new(struct cart_config *cc);

static struct cart_module cart_rom_module = {
	.name = "rom",
	.description = "ROM cartridge",
	.new = cart_rom_new,
};

extern struct cart_module cart_dragondos_module;
extern struct cart_module cart_deltados_module;
extern struct cart_module cart_rsdos_module;
extern struct cart_module cart_gmc_module;
extern struct cart_module cart_orch90_module;
extern struct cart_module cart_mpi_module;
extern struct cart_module cart_ide_module;
extern struct cart_module cart_nx32_module;
extern struct cart_module cart_mooh_module;

static struct slist *cart_modules = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static uint8_t cart_rom_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t cart_rom_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void do_firq(void *);
static _Bool cart_rom_has_interface(struct cart *c, const char *ifname);

/**************************************************************************/

struct cart_config *cart_config_new(void) {
	struct cart_config *new = xmalloc(sizeof(*new));
	*new = (struct cart_config){0};
	new->id = next_id;
	new->autorun = ANY_AUTO;
	config_list = slist_append(config_list, new);
	next_id++;
	return new;
}

struct cart_config *cart_config_by_id(int id) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct cart_config *cc = l->data;
		if (cc->id == id)
			return cc;
	}
	return NULL;
}

struct cart_config *cart_config_by_name(const char *name) {
	if (!name) return NULL;
	for (struct slist *l = config_list; l; l = l->next) {
		struct cart_config *cc = l->data;
		if (0 == strcmp(cc->name, name)) {
			return cc;
		}
	}
	/* If "name" turns out to be a loadable ROM file, create a special
	   ROM cart config for it. */
	if (xroar_filetype_by_ext(name) == FILETYPE_ROM) {
		if (!rom_cart_config) {
			if (!(rom_cart_config = cart_config_new())) {
				return NULL;
			}
			rom_cart_config->name = xstrdup("romcart");
		}
		if (rom_cart_config->description) {
			free(rom_cart_config->description);
		}
		/* Make up a description from filename */
		char tmp_name[strlen(name) + 1];
		strcpy(tmp_name, name);
		char *bname = basename(tmp_name);
		if (bname && *bname) {
			char *sep;
			/* this will strip off file extensions or TOSEC-style
			   metadata in brackets */
			for (sep = bname + 1; *sep; sep++) {
				if ((*sep == '(') ||
				    (*sep == '.') ||
				    (isspace((int)*sep) && *(sep+1) == '(')) {
					*sep = 0;
					break;
				}
			}
			rom_cart_config->description = xstrdup(bname);
		} else {
			rom_cart_config->description = xstrdup("ROM cartridge");
		}
		if (rom_cart_config->rom) free(rom_cart_config->rom);
		rom_cart_config->rom = xstrdup(name);
		rom_cart_config->autorun = 1;
		FILE *fd = fopen(name, "rb");
		if (fd) {
			off_t fsize = fs_file_size(fd);
			if (fsize > 0x4000) {
				rom_cart_config->type = xstrdup("gmc");
			}
			fclose(fd);
		}
		return rom_cart_config;
	}
	return NULL;
}

struct cart_config *cart_find_working_dos(struct machine_config *mc) {
	char *tmp = NULL;
	struct cart_config *cc = NULL;
	if (!mc || mc->architecture != ARCH_COCO) {
		if ((tmp = romlist_find("@dragondos_compat"))) {
			cc = cart_config_by_name("dragondos");
		} else if ((tmp = romlist_find("@delta"))) {
			cc = cart_config_by_name("delta");
		}
	} else {
		if (xroar_cfg.becker && (tmp = romlist_find("@rsdos_becker"))) {
			cc = cart_config_by_name("becker");
		} else if ((tmp = romlist_find("@rsdos"))) {
			cc = cart_config_by_name("rsdos");
		} else if (!xroar_cfg.becker && (tmp = romlist_find("@rsdos_becker"))) {
			cc = cart_config_by_name("becker");
		}
	}
	if (tmp)
		free(tmp);
	return cc;
}

void cart_config_complete(struct cart_config *cc) {
	if (!cc->type) {
		cc->type = xstrdup("rom");
	}
	if (!cc->description) {
		cc->description = xstrdup(cc->name);
	}
	if (cc->autorun == ANY_AUTO) {
		if (c_strcasecmp(cc->type, "rom") == 0) {
			cc->autorun = 1;
		} else {
			cc->autorun = 0;
		}
	}
}

static void cart_config_free(struct cart_config *cc) {
	if (cc->name)
		free(cc->name);
	if (cc->description)
		free(cc->description);
	if (cc->type)
		free(cc->type);
	if (cc->rom)
		free(cc->rom);
	if (cc->rom2)
		free(cc->rom2);
	free(cc);
}

_Bool cart_config_remove(const char *name) {
	struct cart_config *cc = cart_config_by_name(name);
	if (!cc)
		return 0;
	config_list = slist_remove(config_list, cc);
	cart_config_free(cc);
	return 1;
}

struct slist *cart_config_list(void) {
	return config_list;
}

void cart_config_print_all(FILE *f, _Bool all) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct cart_config *cc = l->data;
		fprintf(f, "cart %s\n", cc->name);
		xroar_cfg_print_inc_indent();
		xroar_cfg_print_string(f, all, "cart-desc", cc->description, NULL);
		xroar_cfg_print_string(f, all, "cart-type", cc->type, NULL);
		xroar_cfg_print_string(f, all, "cart-rom", cc->rom, NULL);
		xroar_cfg_print_string(f, all, "cart-rom2", cc->rom2, NULL);
		xroar_cfg_print_bool(f, all, "cart-autorun", cc->autorun, (strcmp(cc->type, "rom") == 0));
		xroar_cfg_print_bool(f, all, "cart-becker", cc->becker_port, 0);
		xroar_cfg_print_dec_indent();
		fprintf(f, "\n");
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void cart_init(void) {
	// reverse order
	cart_modules = slist_prepend(cart_modules, &cart_mooh_module);
	cart_modules = slist_prepend(cart_modules, &cart_nx32_module);
	cart_modules = slist_prepend(cart_modules, &cart_ide_module);
	cart_modules = slist_prepend(cart_modules, &cart_mpi_module);
	cart_modules = slist_prepend(cart_modules, &cart_orch90_module);
	cart_modules = slist_prepend(cart_modules, &cart_gmc_module);
	cart_modules = slist_prepend(cart_modules, &cart_rsdos_module);
	cart_modules = slist_prepend(cart_modules, &cart_deltados_module);
	cart_modules = slist_prepend(cart_modules, &cart_dragondos_module);
	cart_modules = slist_prepend(cart_modules, &cart_rom_module);
}

void cart_shutdown(void) {
	slist_free_full(config_list, (slist_free_func)cart_config_free);
	config_list = NULL;
}

static void cart_type_help_func(struct cart_module *cm, void *udata) {
	(void)udata;
	if (!cm)
		return;
	printf("\t%-10s %s\n", cm->name, cm->description);
}

void cart_type_help(void) {
	slist_foreach(cart_modules, (slist_iter_func)cart_type_help_func, NULL);
}

/* ---------------------------------------------------------------------- */

struct cart *cart_new(struct cart_config *cc) {
	if (!cc) return NULL;
	cart_config_complete(cc);
	struct cart *c = NULL;
	const char *req_type = cc->type;
	for (struct slist *iter = cart_modules; iter; iter = iter->next) {
		struct cart_module *cm = iter->data;
		if (c_strcasecmp(req_type, cm->name) == 0) {
			if (cc->description) {
				LOG_DEBUG(2, "Cartridge module: %s\n", req_type);
				LOG_DEBUG(1, "Cartridge: %s\n", cc->description);
			}
			c = cm->new(cc);
			break;
		}
	}
	if (!c) {
		LOG_WARN("Cartridge module '%s' not found for cartridge '%s'\n", req_type, cc->name);
		return NULL;
	}
	if (c->attach)
		c->attach(c);
	return c;
}

struct cart *cart_new_named(const char *cc_name) {
	struct cart_config *cc = cart_config_by_name(cc_name);
	return cart_new(cc);
}

void cart_free(struct cart *c) {
	if (!c) return;
	if (c->detach)
		c->detach(c);
	if (c->free)
		c->free(c);
	else
		cart_rom_free(c);
}

/* ROM cart routines */

void cart_rom_init(struct cart *c) {
	struct cart_config *cc = c->config;
	assert(cc != NULL);
	c->read = cart_rom_read;
	c->write = cart_rom_write;
	c->reset = cart_rom_reset;
	c->attach = cart_rom_attach;
	c->detach = cart_rom_detach;
	c->free = cart_rom_free;
	c->rom_data = xzalloc(0x10000);
	c->rom_bank = 0;
	if (cc->rom) {
		char *tmp = romlist_find(cc->rom);
		if (tmp) {
			int size = machine_load_rom(tmp, c->rom_data, 0x10000);
			(void)size;  // avoid warnings if...
#ifdef LOGGING
			if (size > 0) {
				uint32_t crc = crc32_block(CRC32_RESET, c->rom_data, size);
				LOG_DEBUG(1, "\tCRC = 0x%08x\n", crc);
			}
#endif
			free(tmp);
		}
	}
	if (cc->rom2) {
		char *tmp = romlist_find(cc->rom2);
		if (tmp) {
			int size = machine_load_rom(tmp, c->rom_data + 0x2000, 0x2000);
			(void)size;  // avoid warnings if...
#ifdef LOGGING
			if (size > 0) {
				uint32_t crc = crc32_block(CRC32_RESET, c->rom_data + 0x2000, size);
				LOG_DEBUG(1, "\tCRC = 0x%08x\n", crc);
			}
#endif
			free(tmp);
		}
	}
	c->signal_firq = DELEGATE_DEFAULT1(void, bool);
	c->signal_nmi = DELEGATE_DEFAULT1(void, bool);
	c->signal_halt = DELEGATE_DEFAULT1(void, bool);
	c->EXTMEM = 0;
	c->has_interface = cart_rom_has_interface;
}

static struct cart *cart_rom_new(struct cart_config *cc) {
	if (!cc) return NULL;
	struct cart *c = xmalloc(sizeof(*c));
	c->config = cc;
	cart_rom_init(c);
	return c;
}

static uint8_t cart_rom_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	(void)P2;
	if (R2)
		return c->rom_data[c->rom_bank | (A & 0x3fff)];
	return D;
}

static uint8_t cart_rom_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	(void)P2;
	if (R2)
		return c->rom_data[c->rom_bank | (A & 0x3fff)];
	return D;
}

void cart_rom_reset(struct cart *c) {
	c->rom_bank = 0;
}

void cart_rom_attach(struct cart *c) {
	struct cart_config *cc = c->config;
	if (cc->autorun) {
		c->firq_event = event_new(DELEGATE_AS0(void, do_firq, c));
		c->firq_event->at_tick = event_current_tick + EVENT_MS(100);
		event_queue(&MACHINE_EVENT_LIST, c->firq_event);
	} else {
		c->firq_event = NULL;
	}
}

void cart_rom_detach(struct cart *c) {
	if (c->firq_event) {
		event_dequeue(c->firq_event);
		event_free(c->firq_event);
		c->firq_event = NULL;
	}
}

void cart_rom_free(struct cart *c) {
	if (c->rom_data) {
		free(c->rom_data);
	}
	free(c);
}

void cart_rom_select_bank(struct cart *c, uint16_t bank) {
	c->rom_bank = bank;
}

static void do_firq(void *data) {
	struct cart *c = data;
	DELEGATE_SAFE_CALL1(c->signal_firq, 1);
	c->firq_event->at_tick = event_current_tick + EVENT_MS(100);
	event_queue(&MACHINE_EVENT_LIST, c->firq_event);
}

/* Default has_interface() - no interfaces supported */

static _Bool cart_rom_has_interface(struct cart *c, const char *ifname) {
	(void)c;
	(void)ifname;
	return 0;
}
