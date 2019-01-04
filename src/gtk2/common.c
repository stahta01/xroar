/*

GTK+2 user-interface common functions

Copyright 2014-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop
#include <gdk/gdkkeysyms.h>

#include "gtk2/common.h"

extern GtkWidget *gtk2_top_window;

gboolean gtk2_dummy_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	(void)user_data;
	if (gtk_window_activate_key(GTK_WINDOW(gtk2_top_window), event) == TRUE) {
		return TRUE;
	}
	return FALSE;
}

// Wrappers for notify-only updating of UI elements.  Blocks callback so that
// no further action is taken.

void uigtk2_notify_toggle_button_set(GtkToggleButton *o, gboolean v,
				     gpointer func, gpointer data) {
	g_signal_handlers_block_by_func(o, G_CALLBACK(func), data);
	gtk_toggle_button_set_active(o, v);
	g_signal_handlers_unblock_by_func(o, G_CALLBACK(func), data);
}

void uigtk2_notify_toggle_action_set(GtkToggleAction *o, gboolean v,
				     gpointer func, gpointer data) {
	g_signal_handlers_block_by_func(o, G_CALLBACK(func), data);
	gtk_toggle_action_set_active(o, v);
	g_signal_handlers_unblock_by_func(o, G_CALLBACK(func), data);
}

void uigtk2_notify_radio_action_set(GtkRadioAction *o, gint v, gpointer func, gpointer data) {
	g_signal_handlers_block_by_func(o, G_CALLBACK(func), data);
	gtk_radio_action_set_current_value(o, v);
	g_signal_handlers_unblock_by_func(o, G_CALLBACK(func), data);
}
