/*  Copyright 2003-2015 Ciaran Anscomb
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

#include "module.h"
#include "ui.h"

static char *filereq_noop(char const * const *extensions);
FileReqModule filereq_null_module = {
	.common = { .name = "null", .description = "No file requester" },
	.load_filename = filereq_noop, .save_filename = filereq_noop
};

static FileReqModule * const null_filereq_module_list[] = {
	&filereq_null_module, NULL
};

extern VideoModule video_null_module;
static VideoModule * const null_video_module_list[] = {
	&video_null_module,
	NULL
};

static void set_state(enum ui_tag tag, int value, const void *data);

struct ui_module ui_null_module = {
	.common = { .name = "null", .description = "No UI" },
	.filereq_module_list = null_filereq_module_list,
	.video_module_list = null_video_module_list,
	.set_state = set_state,
};

/* */

static char *filereq_noop(char const * const *extensions) {
	(void)extensions;
	return NULL;
}

static void set_state(enum ui_tag tag, int value, const void *data) {
	(void)tag;
	(void)value;
	(void)data;
}
