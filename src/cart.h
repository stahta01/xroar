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

#ifndef XROAR_CART_H_
#define XROAR_CART_H_

#include <stdint.h>
#include <stdio.h>

#include "delegate.h"
#include "xconfig.h"

struct slist;
struct machine_config;
struct event;

struct cart_config {
	char *name;
	char *description;
	char *type;
	int id;
	char *rom;
	char *rom2;
	_Bool becker_port;
	int autorun;
};

struct cart {
	struct cart_config *config;

	// Notify that the cartridge has been attached or detached (e.g. to set
	// up or destroy timed events).
	void (*attach)(struct cart *c);
	void (*detach)(struct cart *c);

	// Destroy cartridge.
	void (*free)(struct cart *c);

	// Read & write cycles.  Called every cycle before decode.  If EXTMEM
	// is not asserted, called again when cartridge IO (P2) or ROM (R2)
	// areas are accessed.
	uint8_t (*read)(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
	uint8_t (*write)(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);

	// Reset line.
	void (*reset)(struct cart *c);

	// Cartridge asserts this to inhibit usual address decode by host.
	_Bool EXTMEM;

	// Ways for the cartridge to signal interrupt events to the host.
	DELEGATE_T1(void, bool) signal_firq;
	DELEGATE_T1(void, bool) signal_nmi;
	DELEGATE_T1(void, bool) signal_halt;

	// ROM data.  Not a necessary feature of a cartridge, but included here
	// to avoid having to create a "cart_rom" struct that adds little else.
	uint8_t *rom_data;
	uint16_t rom_bank;

	// Used to schedule regular FIRQs when an "autorun" cartridge is
	// configured.
	struct event *firq_event;

	// Query if cartridge supports a named interface.
	_Bool (*has_interface)(struct cart *c, const char *ifname);
	// Connect a named interface.
	void (*attach_interface)(struct cart *c, const char *ifname, void *intf);
};

struct cart_module {
	const char *name;
	const char *description;
	struct cart *(* const new)(struct cart_config *);
};

struct cart_config *cart_config_new(void);
struct cart_config *cart_config_by_id(int id);
struct cart_config *cart_config_by_name(const char *name);
_Bool cart_config_remove(const char *name);
struct slist *cart_config_list(void);
struct cart_config *cart_find_working_dos(struct machine_config *mc);
void cart_config_complete(struct cart_config *cc);
void cart_config_print_all(FILE *f, _Bool all);

void cart_init(void);
void cart_shutdown(void);
void cart_type_help(void);

struct cart *cart_new(struct cart_config *cc);
struct cart *cart_new_named(const char *cc_name);
void cart_free(struct cart *c);

void cart_rom_init(struct cart *c);
void cart_rom_reset(struct cart *c);
void cart_rom_attach(struct cart *c);
void cart_rom_detach(struct cart *c);
void cart_rom_free(struct cart *c);
void cart_rom_select_bank(struct cart *c, uint16_t bank);

#endif
