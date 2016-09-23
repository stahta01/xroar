/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdlib.h>

#include "xalloc.h"

#include "module.h"
#include "ui.h"
#include "vo.h"

static char *filereq_noop(char const * const *extensions);
FileReqModule filereq_null_module = {
	.common = { .name = "null", .description = "No file requester" },
	.load_filename = filereq_noop, .save_filename = filereq_noop
};

static FileReqModule * const null_filereq_module_list[] = {
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

static char *filereq_noop(char const * const *extensions) {
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
