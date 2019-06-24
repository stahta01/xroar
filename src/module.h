/*

Generic module support

Copyright 2003-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_MODULE_H_
#define XROAR_MODULE_H_

#include <stdint.h>

#include "delegate.h"

struct joystick_module;
struct vdisk;

struct module {
	const char *name;
	const char *description;
	void *(*new)(void *cfg);
};

typedef DELEGATE_S1(char *, char const * const *) DELEGATE_T1(charp, charcpcp);

struct filereq_interface {
	DELEGATE_T0(void) free;
	DELEGATE_T1(charp, charcpcp) load_filename;
	DELEGATE_T1(charp, charcpcp) save_filename;
};

extern struct module * const *filereq_module_list;
extern struct filereq_interface *filereq_interface;

void module_print_list(struct module * const *list);
struct module *module_select(struct module * const *list, const char *name);
struct module *module_select_by_arg(struct module * const *list, const char *name);
void *module_init(struct module *module, void *cfg);
void *module_init_from_list(struct module * const *list, struct module *module, void *cfg);

#endif
