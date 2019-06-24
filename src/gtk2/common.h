/*

GTK+2 user-interface common functions

Copyright 2014-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_GTK2_COMMON_H_
#define XROAR_GTK2_COMMON_H_

#include <gtk/gtk.h>

#include "events.h"
#include "ui.h"
#include "vo.h"

struct ui_gtk2_interface {
	struct ui_interface public;

	struct ui_cfg *cfg;

	// Shared GTK+2 objects
	GtkWidget *top_window;
	GtkUIManager *menu_manager;
	GtkWidget *menubar;
	GtkWidget *drawing_area;
	// Dynamic menus
	GtkActionGroup *machine_action_group;
	guint merge_machines;
	GtkActionGroup *cart_action_group;
	guint merge_carts;

	// Window geometry
	struct vo_rect display_rect;

	// Mouse tracking
	float mouse_xoffset;
	float mouse_yoffset;
	float mouse_xdiv;
	float mouse_ydiv;
	unsigned mouse_axis[2];
	_Bool mouse_button[3];
	// Restrict polling rate
	event_ticks last_mouse_update_time;
	// Cursor hiding
	_Bool cursor_hidden;
	GdkCursor *old_cursor;
	GdkCursor *blank_cursor;

};

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_gtk2 and make it available
// globally.
extern struct ui_gtk2_interface *global_uigtk2;

void gtk2_keyboard_init(struct ui_cfg *ui_cfg);
gboolean gtk2_dummy_keypress(GtkWidget *, GdkEventKey *, gpointer);

extern struct joystick_module *gtk2_js_modlist[];

void gtk2_joystick_init(struct ui_gtk2_interface *uigtk2);

// Wrappers for notify-only updating of UI elements.  Blocks callback so that
// no further action is taken.

void uigtk2_notify_toggle_button_set(GtkToggleButton *o, gboolean v,
				     gpointer func, gpointer data);

void uigtk2_notify_toggle_action_set(GtkToggleAction *o, gboolean v,
				     gpointer func, gpointer data);

void uigtk2_notify_radio_action_set(GtkRadioAction *o, gint v, gpointer func, gpointer data);

#endif
