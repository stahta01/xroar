/*

User-interface modules & interfaces

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

#include "module.h"
#include "ui.h"
#include "xconfig.h"

extern struct ui_module ui_gtk2_module;
extern struct ui_module ui_null_module;
extern struct ui_module ui_sdl_module;
static struct ui_module * const default_ui_module_list[] = {
#ifdef HAVE_GTK2
#ifdef HAVE_GTKGL
	&ui_gtk2_module,
#endif
#endif
#ifdef HAVE_SDL2
	&ui_sdl_module,
#endif
	&ui_null_module,
	NULL
};

struct ui_module * const *ui_module_list = default_ui_module_list;

struct xconfig_enum ui_ccr_list[] = {
	{ XC_ENUM_INT("simple", UI_CCR_SIMPLE, "four colour palette") },
	{ XC_ENUM_INT("5bit", UI_CCR_5BIT, "5-bit lookup table") },
#ifdef WANT_SIMULATED_NTSC
	{ XC_ENUM_INT("simulated", UI_CCR_SIMULATED, "simulated filtered analogue") },
#endif
	{ XC_ENUM_END() }
};

struct xconfig_enum ui_gl_filter_list[] = {
	{ XC_ENUM_INT("auto", UI_GL_FILTER_AUTO, "Automatic") },
	{ XC_ENUM_INT("nearest", UI_GL_FILTER_NEAREST, "Nearest-neighbour filter") },
	{ XC_ENUM_INT("linear", UI_GL_FILTER_LINEAR, "Linear filter") },
	{ XC_ENUM_END() }
};

void ui_print_vo_help(void) {
	for (int i = 0; ui_module_list[i]; i++) {
		if (!ui_module_list[i]->vo_module_list)
			continue;
		printf("Video modules for %s (ui %s)\n", ui_module_list[i]->common.description, ui_module_list[i]->common.name);
		module_print_list((struct module * const*)ui_module_list[i]->vo_module_list);
	}
}
