/** \file
 *
 *  \brief GTK+ 2 joystick interfaces.
 *
 *  \copyright Copyright 2010-2024 Ciaran Anscomb
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

#include <glib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "joystick.h"
#include "module.h"

#include "gtk2/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_control *configure_mouse_axis(char *, unsigned);
static struct joystick_control *configure_mouse_button(char *, unsigned);

static struct joystick_submodule gtk2_js_mouse = {
	.name = "mouse",
	.configure_axis = configure_mouse_axis,
	.configure_button = configure_mouse_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern struct joystick_submodule hkbd_js_keyboard;

static struct joystick_submodule *js_submodlist[] = {
	&hkbd_js_keyboard,
	&gtk2_js_mouse,
	NULL
};

struct joystick_module gtk2_js_internal = {
	.common = { .name = "gtk2", .description = "GTK+ joystick" },
	.submodule_list = js_submodlist,
};

struct joystick_module *gtk2_js_modlist[] = {
	&gtk2_js_internal,
	NULL
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_control *configure_mouse_axis(char *spec, unsigned jaxis) {
	return joystick_configure_mouse_axis(&global_uigtk2->public, spec, jaxis);
}

static struct joystick_control *configure_mouse_button(char *spec, unsigned jbutton) {
	return joystick_configure_mouse_button(&global_uigtk2->public, spec, jbutton);
}
