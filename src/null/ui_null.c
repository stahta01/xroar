/*

Null user-interface module

Copyright 2011-2019 Ciaran Anscomb

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

#include "xalloc.h"

#include "module.h"
#include "ui.h"
#include "vo.h"

static void *filereq_null_new(void *cfg);
static void filereq_null_free(void *sptr);

struct module filereq_null_module = {
	.name = "null", .description = "No file requester",
	.new = filereq_null_new
};

static struct module * const null_filereq_module_list[] = {
	&filereq_null_module, NULL
};

extern struct module vo_null_module;
static struct module * const null_vo_module_list[] = {
	&vo_null_module,
	NULL
};

static void set_state(void *sptr, int tag, int value, const void *data);

static void *new(void *cfg);

struct ui_module ui_null_module = {
	.common = { .name = "null", .description = "No UI", .new = new, },
	.filereq_module_list = null_filereq_module_list,
	.vo_module_list = null_vo_module_list,
};

/* */

static char *filereq_noop(void *sptr, char const * const *extensions) {
	(void)sptr;
	(void)extensions;
	return NULL;
}

static void null_free(void *sptr);

static void *new(void *cfg) {
	(void)cfg;
	struct ui_interface *uinull = xmalloc(sizeof(*uinull));
	*uinull = (struct ui_interface){0};

	uinull->free = DELEGATE_AS0(void, null_free, uinull);
	uinull->set_state = DELEGATE_AS3(void, int, int, cvoidp, set_state, uinull);
	struct module *vo_mod = (struct module *)module_select_by_arg((struct module * const *)null_vo_module_list, NULL);
	if (!(uinull->vo_interface = module_init(vo_mod, uinull))) {
		return NULL;
	}

	return uinull;
}

static void null_free(void *sptr) {
	struct ui_interface *uinull = sptr;
	free(uinull);
}

static void set_state(void *sptr, int tag, int value, const void *data) {
	(void)sptr;
	(void)tag;
	(void)value;
	(void)data;
}

static void *filereq_null_new(void *cfg) {
	(void)cfg;
	struct filereq_interface *frnull = xmalloc(sizeof(*frnull));
	*frnull = (struct filereq_interface){0};
	frnull->free = DELEGATE_AS0(void, filereq_null_free, frnull);
	frnull->load_filename = DELEGATE_AS1(charp, charcpcp, filereq_noop, frnull);
	frnull->save_filename = DELEGATE_AS1(charp, charcpcp, filereq_noop, frnull);
	return frnull;
}

static void filereq_null_free(void *sptr) {
	struct filereq_interface *frnull = sptr;
	free(frnull);
}
