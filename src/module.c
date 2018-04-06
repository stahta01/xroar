/*

Generic module support

Copyright 2003-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "logging.h"
#include "module.h"

/**** Default file requester module list ****/

extern FileReqModule filereq_cocoa_module;
extern FileReqModule filereq_windows32_module;
extern FileReqModule filereq_gtk2_module;
extern FileReqModule filereq_cli_module;
extern FileReqModule filereq_null_module;
static FileReqModule * const default_filereq_module_list[] = {
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

FileReqModule * const *filereq_module_list = default_filereq_module_list;
FileReqModule *filereq_module = NULL;

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
	const char *description = module->description ? module->description : "(unknown)";
	LOG_DEBUG(1, "Module init: %s\n", description);
	// New interface?
	if (module->new) {
		void *m = module->new(cfg);
		if (!m) {
			LOG_DEBUG(1, "Module init failed: %s\n", description);
		}
		return m;
	}
	if (!module->init || module->init(cfg)) {
		module->initialised = 1;
		return module;
	}
	LOG_DEBUG(1, "Module init failed: %s\n", description);
	return NULL;
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

void module_shutdown(struct module *module) {
	if (!module || !module->initialised)
		return;
	if (module->description) {
		LOG_DEBUG(1, "Module shutdown: %s\n", module->description);
	}
	if (module->shutdown)
		module->shutdown();
}
