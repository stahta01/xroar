/** \file
 *
 *  \brief Generic module support.
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

#ifndef XROAR_MODULE_H_
#define XROAR_MODULE_H_

#include <stdint.h>

#include "delegate.h"

struct module {
	const char *name;
	const char *description;
	void *(*new)(void *cfg);
};

struct module *module_select(struct module * const *list, const char *name);
struct module *module_select_by_arg(struct module * const *list, const char *name);
void *module_init(struct module *module, const char *type, void *cfg);
void *module_init_from_list(struct module * const *list, const char *type,
			    struct module *module, void *cfg);

#endif
