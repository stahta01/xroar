/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2015  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_CART_H_
#define XROAR_CART_H_

#include <stdint.h>

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
	uint8_t (*read)(struct cart *c, uint16_t A, _Bool P2, uint8_t D);
	void (*write)(struct cart *c, uint16_t A, _Bool P2, uint8_t D);
	void (*reset)(struct cart *c);
	void (*attach)(struct cart *c);
	void (*detach)(struct cart *c);
	uint8_t *rom_data;
	DELEGATE_T1(void, bool) signal_firq;
	DELEGATE_T1(void, bool) signal_nmi;
	DELEGATE_T1(void, bool) signal_halt;
	struct event *firq_event;
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
void cart_config_print_all(_Bool all);

void cart_init(void);
void cart_shutdown(void);
void cart_type_help(void);

struct cart *cart_new(struct cart_config *cc);
struct cart *cart_new_named(const char *cc_name);
void cart_free(struct cart *c);

void cart_rom_init(struct cart *c);
void cart_rom_attach(struct cart *c);
void cart_rom_detach(struct cart *c);

#endif  /* XROAR_CART_H_ */
