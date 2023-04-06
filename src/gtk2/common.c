/** \file
 *
 *  \brief GTK+ 2 user-interface common functions.
 *
 *  \copyright Copyright 2014-2023 Ciaran Anscomb
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

#include <ctype.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop
#include <gdk/gdkkeysyms.h>

#include "xalloc.h"

#include "keyboard.h"
#include "xroar.h"

#include "gtk2/common.h"

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_gtk2 and make it available
// globally.
struct ui_gtk2_interface *global_uigtk2 = NULL;

// Event handlers

// Used within tape/drive control dialogs to eat keypresses but still allow GUI
// controls.

gboolean gtk2_dummy_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	struct ui_gtk2_interface *uigtk2 = user_data;

	if (gtk_window_activate_key(GTK_WINDOW(uigtk2->top_window), event) == TRUE) {
		return TRUE;
	}

	return FALSE;
}

// Key press/release

gboolean gtk2_handle_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;

#ifndef WINDOWS32
	// Hide cursor
	if (!uigtk2->cursor_hidden) {
		GdkWindow *window = gtk_widget_get_window(uigtk2->drawing_area);
		uigtk2->old_cursor = gdk_window_get_cursor(window);
		gdk_window_set_cursor(window, uigtk2->blank_cursor);
		uigtk2->cursor_hidden = 1;
	}
#endif

	// Pass off to keyboard code
	return gtk2_keyboard_handle_key_press(widget, event, user_data);
}

gboolean gtk2_handle_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	// Pass off to keyboard code
	return gtk2_keyboard_handle_key_release(widget, event, user_data);
}

// Pointer motion

gboolean gtk2_handle_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)widget;

#ifndef WINDOWS32
	// Unhide cursor
	if (uigtk2->cursor_hidden) {
		GdkWindow *window = gtk_widget_get_window(uigtk2->drawing_area);
		gdk_window_set_cursor(window, uigtk2->old_cursor);
		uigtk2->cursor_hidden = 0;
	}
#endif

	// Update position data (for mouse mapped joystick)
	int x = (event->x - uigtk2->display_rect.x) * 320;
	int y = (event->y - uigtk2->display_rect.y) * 240;
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

	return FALSE;
}

// Button press/release

static void clipboard_text_received(GtkClipboard *clipboard, const gchar *text, gpointer data) {
	(void)clipboard;
	(void)data;
	char *ntext = xstrdup(text);
	if (!ntext)
		return;
	guint state = (uintptr_t)data;
	_Bool uc = state & GDK_SHIFT_MASK;
	for (char *p = ntext; *p; p++) {
		if (*p == '\n')
			*p = '\r';
		if (uc)
			*p = toupper(*p);
	}
	keyboard_queue_basic(xroar_keyboard_interface, ntext);
	free(ntext);
}

gboolean gtk2_handle_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)widget;

	if (event->button == 2) {
		GdkDisplay *d = gtk_widget_get_display(uigtk2->top_window);
		GtkClipboard *cb = gtk_clipboard_get_for_display(d, GDK_SELECTION_PRIMARY);
		gtk_clipboard_request_text(cb, clipboard_text_received, (gpointer)(uintptr_t)event->state);
		return FALSE;
	}

	// Update button data (for mouse mapped joystick)
	if (event->button >= 1 && event->button <= 3) {
		uigtk2->mouse_button[event->button-1] = 1;
	}

	return FALSE;
}

gboolean gtk2_handle_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)widget;

	// Update button data (for mouse mapped joystick)
	if (event->button >= 1 && event->button <= 3) {
		uigtk2->mouse_button[event->button-1] = 0;
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

void uigtk2_notify_spin_button_set(GtkSpinButton *o, gdouble value,
				   gpointer func, gpointer data) {
	g_signal_handlers_block_by_func(o, G_CALLBACK(func), data);
	gtk_spin_button_set_value(o, value);
	g_signal_handlers_unblock_by_func(o, G_CALLBACK(func), data);
}

FUNC_ATTR_NORETURN static void do_g_abort(const gchar *format, GError *error) {
	(void)format;
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
