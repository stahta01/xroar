/** \file
 *
 *  \brief GTK+ 2 user-interface common functions.
 *
 *  \copyright Copyright 2014-2019 Ciaran Anscomb
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop
#include <gdk/gdkkeysyms.h>

#include "gtk2/common.h"

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_gtk2 and make it available
// globally.
struct ui_gtk2_interface *global_uigtk2 = NULL;

gboolean gtk2_dummy_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	(void)user_data;
	if (gtk_window_activate_key(GTK_WINDOW(global_uigtk2->top_window), event) == TRUE) {
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

FUNC_ATTR_NORETURN static void do_g_abort(const gchar *format, GError *error) {
	if (error) {
		g_message("gtk_builder_new_from_resource() failed: %s", error->message);
		g_error_free(error);
	}
	g_abort();
}

GtkBuilder *gtk_builder_new_from_resource(const gchar *path) {
	GError *error = NULL;
	GBytes *resource = g_resources_lookup_data(path, 0, &error);
	if (!resource) {
		do_g_abort("g_resources_lookup_data() failed: %s", error);
	}

	gsize xml_size;
	const gchar *xml = g_bytes_get_data(resource, &xml_size);

	GtkBuilder *builder = gtk_builder_new();
	if (gtk_builder_add_from_string(builder, xml, xml_size, &error) == 0) {
		do_g_abort("gtk_builder_add_from_string() failed: %s", error);
	}

	g_bytes_unref(resource);
	return builder;
}
