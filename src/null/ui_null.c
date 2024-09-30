/** \file
 *
 *  \brief Null user-interface module.
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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

static void *new(void *cfg);

struct ui_module ui_null_module = {
	.common = { .name = "null", .description = "No UI", .new = new, },
	.filereq_module_list = null_filereq_module_list,
};

/* */

static char *filereq_noop(void *sptr, char const *extensions) {
	(void)sptr;
	(void)extensions;
	return NULL;
}

static void null_free(void *sptr);
static void null_render(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data);

static void *new(void *cfg) {
	(void)cfg;
	struct ui_interface *uinull = xmalloc(sizeof(*uinull));
	*uinull = (struct ui_interface){0};

	uinull->free = DELEGATE_AS0(void, null_free, uinull);

	struct vo_interface *vo = xmalloc(sizeof(*vo));
	uinull->vo_interface = vo;
	*vo = (struct vo_interface){0};

	vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, null_render, vo->renderer);

	return uinull;
}

static void null_free(void *sptr) {
	struct ui_interface *uinull = sptr;
	free(uinull->vo_interface);
	free(uinull);
}

static void *filereq_null_new(void *cfg) {
	(void)cfg;
	struct filereq_interface *frnull = xmalloc(sizeof(*frnull));
	*frnull = (struct filereq_interface){0};
	frnull->free = DELEGATE_AS0(void, filereq_null_free, frnull);
	frnull->load_filename = DELEGATE_AS1(charp, charcp, filereq_noop, frnull);
	frnull->save_filename = DELEGATE_AS1(charp, charcp, filereq_noop, frnull);
	return frnull;
}

static void filereq_null_free(void *sptr) {
	struct filereq_interface *frnull = sptr;
	free(frnull);
}

static void null_render(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data) {
	(void)sptr;
	(void)burst;
	(void)npixels;
	(void)data;
}
