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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "logging.h"
#include "module.h"

/**** Default file requester module list ****/

extern struct module filereq_cocoa_module;
extern struct module filereq_windows32_module;
extern struct module filereq_gtk2_module;
extern struct module filereq_cli_module;
extern struct module filereq_null_module;
static struct module * const default_filereq_module_list[] = {
#ifdef HAVE_COCOA
	&filereq_cocoa_module,
#endif
#ifdef WINDOWS32
	&filereq_windows32_module,
#endif
#ifdef HAVE_GTK2
	&filereq_gtk2_module,
#endif
#ifdef HAVE_CLI
	&filereq_cli_module,
#endif
	&filereq_null_module,
	NULL
};

struct module * const *filereq_module_list = default_filereq_module_list;
struct module *filereq_module = NULL;

void module_print_list(struct module * const *list) {
	int i;
	if (list == NULL || list[0]->name == NULL) {
		puts("\tNone found.");
		return;
	}
	for (i = 0; list[i]; i++) {
		printf("\t%-10s %s\n", list[i]->name, list[i]->description);
	}
}

struct module *module_select(struct module * const *list, const char *name) {
	int i;
	if (list == NULL)
		return NULL;
	for (i = 0; list[i]; i++) {
		if (strcmp(list[i]->name, name) == 0)
			return list[i];
	}
	return NULL;
}

struct module *module_select_by_arg(struct module * const *list, const char *name) {
	if (name == NULL)
		return list[0];
	if (0 == strcmp(name, "help")) {
		module_print_list(list);
		exit(EXIT_SUCCESS);
	}
	return module_select(list, name);
}

void *module_init(struct module *module, void *cfg) {
	if (!module)
		return NULL;
	const char *description = module->description ? module->description : "unknown";
	LOG_DEBUG(1, "%s: init: %s\n", module->name, description);
	// New interface?
	if (module->new) {
		void *m = module->new(cfg);
		if (!m) {
			LOG_DEBUG(1, "%s: init failed: %s\n", module->name, description);
		}
		return m;
	}
	LOG_ERROR("%s: old module interface called\n", module->name);
	abort();
}

void *module_init_from_list(struct module * const *list, struct module *module, void *cfg) {
	int i;
	/* First attempt to initialise selected module (if given) */
	void *m = module_init(module, cfg);
	if (m)
		return m;
	if (list == NULL)
		return NULL;
	/* If that fails, try every *other* module in the list */
	for (i = 0; list[i]; i++) {
		if (list[i] != module && (m = module_init(list[i], cfg)))
			return m;
	}
	return NULL;
}
