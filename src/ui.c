/** \file
 *
 *  \brief User-interface modules & interfaces.
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

#include <stdlib.h>
#include <stdio.h>

#include "module.h"
#include "ui.h"
#include "xconfig.h"

// File requester modules
//
// Kept here for now, intention being to roll them into the UI

extern struct module filereq_cocoa_module;
extern struct module filereq_windows32_module;
extern struct module filereq_gtk3_module;
extern struct module filereq_gtk2_module;
extern struct module filereq_cli_module;
extern struct module filereq_null_module;
struct module * const default_filereq_module_list[] = {
#ifdef HAVE_COCOA
	&filereq_cocoa_module,
#endif
#ifdef WINDOWS32
	&filereq_windows32_module,
#endif
#ifdef HAVE_GTK3
	&filereq_gtk3_module,
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

// UI modules

extern struct ui_module ui_gtk3_module;
extern struct ui_module ui_gtk2_module;
extern struct ui_module ui_null_module;
extern struct ui_module ui_windows32_module;
extern struct ui_module ui_wasm_module;
extern struct ui_module ui_cocoa_module;
extern struct ui_module ui_sdl_module;
static struct ui_module * const default_ui_module_list[] = {
#ifdef HAVE_GTK3
	&ui_gtk3_module,
#endif
#ifdef HAVE_GTK2
#ifdef HAVE_GTKGL
	&ui_gtk2_module,
#endif
#endif
#ifdef WINDOWS32
	&ui_windows32_module,
#endif
#ifdef HAVE_WASM
	&ui_wasm_module,
#endif
#ifdef HAVE_COCOA
	&ui_cocoa_module,
#endif
#ifdef WANT_UI_SDL
	&ui_sdl_module,
#endif
	&ui_null_module,
	NULL
};

struct ui_module * const *ui_module_list = default_ui_module_list;

struct xconfig_enum ui_gl_filter_list[] = {
	{ XC_ENUM_INT("auto", UI_GL_FILTER_AUTO, "Automatic") },
	{ XC_ENUM_INT("nearest", UI_GL_FILTER_NEAREST, "Nearest-neighbour filter") },
	{ XC_ENUM_INT("linear", UI_GL_FILTER_LINEAR, "Linear filter") },
	{ XC_ENUM_END() }
};
