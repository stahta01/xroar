/** \file
 *
 *  \brief GTK+ 2 joystick interfaces.
 *
 *  \copyright Copyright 2010-2022 Ciaran Anscomb
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

static gboolean handle_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	int x = (event->x - global_uigtk2->display_rect.x) * 320;
	int y = (event->y - global_uigtk2->display_rect.y) * 240;
	float xx = (float)x / (float)global_uigtk2->display_rect.w;
	float yy = (float)y / (float)global_uigtk2->display_rect.h;
	xx = (xx - global_uigtk2->mouse_xoffset) / global_uigtk2->mouse_xdiv;
	yy = (yy - global_uigtk2->mouse_yoffset) / global_uigtk2->mouse_ydiv;
	if (xx < 0.0) xx = 0.0;
	if (xx > 1.0) xx = 1.0;
	if (yy < 0.0) yy = 0.0;
	if (yy > 1.0) yy = 1.0;
	global_uigtk2->mouse_axis[0] = xx * 65535.;
	global_uigtk2->mouse_axis[1] = yy * 65535.;
	return FALSE;
}

static gboolean handle_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	int button = event->button - 1;
	if (button >= 0 && button <= 2) {
		global_uigtk2->mouse_button[button] = 1;
	}
	return FALSE;
}

static gboolean handle_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	int button = event->button - 1;
	if (button >= 0 && button <= 2) {
		global_uigtk2->mouse_button[button] = 0;
	}
	return FALSE;
}

void gtk2_joystick_init(void) {
	// Mouse tracking
	global_uigtk2->mouse_xoffset = 34.0;
	global_uigtk2->mouse_yoffset = 25.5;
	global_uigtk2->mouse_xdiv = 252.;
	global_uigtk2->mouse_ydiv = 189.;

	// Connect GTK+ events to handlers
	g_signal_connect(G_OBJECT(global_uigtk2->drawing_area), "motion-notify-event", G_CALLBACK(handle_motion_notify), NULL);
	g_signal_connect(G_OBJECT(global_uigtk2->drawing_area), "button-press-event", G_CALLBACK(handle_button_press), NULL);
	g_signal_connect(G_OBJECT(global_uigtk2->drawing_area), "button-release-event", G_CALLBACK(handle_button_release), NULL);

	// Make sure we get those events
	GdkWindow *window = gtk_widget_get_window(global_uigtk2->drawing_area);
	GdkEventMask m = gdk_window_get_events(window);
	m |= GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK;
	gdk_window_set_events(window, m);
}

static unsigned read_axis(unsigned *a) {
	return *a;
}

static _Bool read_button(_Bool *b) {
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
	off0 -= 1.0;
	off1 -= 0.75;
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
	return button;
}
