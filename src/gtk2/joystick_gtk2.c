/** \file
 *
 *  \brief GTK+2 joystick interfaces.
 *
 *  \copyright Copyright 2010-2019 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "pl-string.h"
#include "slist.h"

#include "events.h"
#include "joystick.h"
#include "logging.h"
#include "module.h"
#include "ui.h"
#include "xroar.h"

#include "gtk2/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);

extern struct joystick_submodule gtk2_js_submod_keyboard;

static struct joystick_submodule gtk2_js_submod_mouse = {
	.name = "mouse",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_submodule *js_submodlist[] = {
	&gtk2_js_submod_keyboard,
	&gtk2_js_submod_mouse,
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

void gtk2_joystick_init(struct ui_gtk2_interface *uigtk2) {
	// Mouse tracking
	uigtk2->mouse_xoffset = 34.0;
	uigtk2->mouse_yoffset = 25.5;
	uigtk2->mouse_xdiv = 252.;
	uigtk2->mouse_ydiv = 189.;
	uigtk2->last_mouse_update_time = event_current_tick;
}

static void update_mouse_state(struct ui_gtk2_interface *uigtk2) {
	int x, y;
	GdkModifierType buttons;
	GdkWindow *window = gtk_widget_get_window(uigtk2->drawing_area);
	gdk_window_get_pointer(window, &x, &y, &buttons);
	x = (x - uigtk2->display_rect.x) * 320;
	y = (y - uigtk2->display_rect.y) * 240;
	float xx = (float)x / (float)uigtk2->display_rect.w;
	float yy = (float)y / (float)uigtk2->display_rect.h;
	xx = (xx - uigtk2->mouse_xoffset) / uigtk2->mouse_xdiv;
	yy = (yy - uigtk2->mouse_yoffset) / uigtk2->mouse_ydiv;
	if (xx < 0.0) xx = 0.0;
	if (xx > 1.0) xx = 1.0;
	if (yy < 0.0) yy = 0.0;
	if (yy > 1.0) yy = 1.0;
	uigtk2->mouse_axis[0] = xx * 65535.;
	uigtk2->mouse_axis[1] = yy * 65535.;
	uigtk2->mouse_button[0] = buttons & GDK_BUTTON1_MASK;
	uigtk2->mouse_button[1] = buttons & GDK_BUTTON2_MASK;
	uigtk2->mouse_button[2] = buttons & GDK_BUTTON3_MASK;
	uigtk2->last_mouse_update_time = event_current_tick;
}

static unsigned read_axis(unsigned *a) {
	if ((event_current_tick - global_uigtk2->last_mouse_update_time) >= EVENT_MS(10))
		update_mouse_state(global_uigtk2);
	return *a;
}

static _Bool read_button(_Bool *b) {
	if ((event_current_tick - global_uigtk2->last_mouse_update_time) >= EVENT_MS(10))
		update_mouse_state(global_uigtk2);
	return *b;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
	jaxis %= 2;
	float off0 = (jaxis == 0) ? 2.0 : 1.5;
	float off1 = (jaxis == 0) ? 254.0 : 190.5;
	char *tmp = NULL;
	if (spec)
		tmp = strsep(&spec, ",");
	if (tmp && *tmp)
		off0 = strtof(tmp, NULL);
	if (spec && *spec)
		off1 = strtof(spec, NULL);
	if (jaxis == 0) {
		if (off0 < -32.0) off0 = -32.0;
		if (off1 > 288.0) off0 = 288.0;
		global_uigtk2->mouse_xoffset = off0 + 32.0;
		global_uigtk2->mouse_xdiv = off1 - off0;
	} else {
		if (off0 < -24.0) off0 = -24.0;
		if (off1 > 216.0) off0 = 216.0;
		global_uigtk2->mouse_yoffset = off0 + 24.0;
		global_uigtk2->mouse_ydiv = off1 - off0;
	}
	struct joystick_axis *axis = g_malloc(sizeof(*axis));
	axis->read = (js_read_axis_func)read_axis;
	axis->data = &global_uigtk2->mouse_axis[jaxis];
	global_uigtk2->last_mouse_update_time = event_current_tick - EVENT_MS(10);
	return axis;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	jbutton %= 3;
	if (spec && *spec)
		jbutton = strtol(spec, NULL, 0) - 1;
	if (jbutton >= 3)
		return NULL;
	struct joystick_button *button = g_malloc(sizeof(*button));
	button->read = (js_read_button_func)read_button;
	button->data = &global_uigtk2->mouse_button[jbutton];
	global_uigtk2->last_mouse_update_time = event_current_tick - EVENT_MS(10);
	return button;
}
