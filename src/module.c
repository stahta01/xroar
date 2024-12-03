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

#include "top-config.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "c-strcase.h"

#include "logging.h"
#include "module.h"

static void module_print_list(struct module * const *list) {
	if (!list) {
		puts("\tNone found.");
		return;
	}
	for (int i = 0; list[i]; i++) {
		printf("\t%-10s %s\n", list[i]->name, list[i]->description);
	}
}

struct module *module_select(struct module * const *list, const char *name) {
	if (!list) {
		return NULL;
	}
	for (int i = 0; list[i]; i++) {
		if (0 == strcmp(list[i]->name, name)) {
			return list[i];
		}
	}
	return NULL;
}

struct module *module_select_by_arg(struct module * const *list, const char *name) {
	if (!name) {
		return list ? list[0] : NULL;
	}
	if (0 == c_strcasecmp(name, "help")) {
		module_print_list(list);
		exit(EXIT_SUCCESS);
	}
	if (!list) {
		return NULL;
	}
	return module_select(list, name);
}

void *module_init(struct module *module, const char *type, void *cfg) {
	if (!module) {
		return NULL;
	}
	const char *description = module->description ? module->description : "unknown";
	LOG_PAR_MOD_SUB_DEBUG(1, "module", module->name, type, "%s\n", description);
	assert(module->new != NULL);
	return module->new(cfg);
}

void *module_init_from_list(struct module * const *list, const char *type,
			    struct module *module, void *cfg) {
	/* First attempt to initialise selected module (if given) */
	void *m = module_init(module, type, cfg);
	if (m) {
		return m;
	}
	if (!list) {
		return NULL;
	}
	/* If that fails, try every *other* module in the list */
	for (int i = 0; list[i]; i++) {
		if (list[i] != module && (m = module_init(list[i], type, cfg))) {
			return m;
		}
	}
	return NULL;
}
