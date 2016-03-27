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

#include "module.h"
#include "ui.h"

extern struct ui_module ui_gtk2_module;
extern struct ui_module ui_macosx_module;
extern struct ui_module ui_null_module;
extern struct ui_module ui_sdl_module;
extern struct ui_module ui_windows32_module;
static struct ui_module * const default_ui_module_list[] = {
#ifdef HAVE_GTK2
#ifdef HAVE_GTKGL
	&ui_gtk2_module,
#endif
#endif
#ifdef HAVE_SDL2
#ifdef WINDOWS32
	&ui_windows32_module,
#endif
	&ui_sdl_module,
#endif
#ifdef HAVE_SDL
#ifdef HAVE_COCOA
	&ui_macosx_module,
#else
#ifdef WINDOWS32
	&ui_windows32_module,
#endif
	&ui_sdl_module,
#endif
#endif
	&ui_null_module,
	NULL
};

struct ui_module * const *ui_module_list = default_ui_module_list;
struct ui_module *ui_module = NULL;
