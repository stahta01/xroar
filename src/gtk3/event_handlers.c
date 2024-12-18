/** \file
 *
 *  \brief GTK+ 3 event handlers.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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
 *
 *  Mostly provides event handlers for the main window, but some more generally
 *  useful ones included too.
 */

#include "top-config.h"

#include <ctype.h>

#include <gtk/gtk.h>

#include "xalloc.h"

#include "auto_kbd.h"
#include "hkbd.h"
#include "logging.h"
#include "xroar.h"

#include "gtk3/common.h"
#include "gtk3/event_handlers.h"

// Key press event handler.  Hides the pointer; subsequent pointer motion will
// unhide it.

gboolean gtk3_handle_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	struct ui_gtk3_interface *uigtk3 = user_data;

	// Hide cursor
	if (!uigtk3->cursor_hidden) {
		GdkWindow *window = gtk_widget_get_window(uigtk3->drawing_area);
		uigtk3->old_cursor = gdk_window_get_cursor(window);
		gdk_window_set_cursor(window, uigtk3->blank_cursor);
		uigtk3->cursor_hidden = 1;
	}

	// If GTK+ has something configured for the current combo:
	if (gtk_window_activate_key(GTK_WINDOW(uigtk3->top_window), event) == TRUE) {
		return TRUE;
	}

	// If an OS-specific keyboard scancode mapping could be determined:
	if (event->hardware_keycode < hk_num_os_scancodes) {
		hk_scan_press(os_scancode_to_hk_scancode[event->hardware_keycode]);
	}
	return TRUE;
}

// Key release event handler.

gboolean gtk3_handle_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	(void)user_data;
	// If an OS-specific keyboard scancode mapping could be determined:
	if (event->hardware_keycode < hk_num_os_scancodes) {
		hk_scan_release(os_scancode_to_hk_scancode[event->hardware_keycode]);
	}
	return FALSE;
}

// Keymap change event handler.

gboolean gtk3_handle_keys_changed(GdkKeymap *gdk_keymap, gpointer user_data) {
        (void)gdk_keymap;
        (void)user_data;
        hk_update_keymap();
        return FALSE;
}

// Dummy keypress event handler.  Used within tape/drive control dialogs to eat
// keypresses but still allow GUI controls.

gboolean gtk3_dummy_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	struct ui_gtk3_interface *uigtk3 = user_data;

	if (gtk_window_activate_key(GTK_WINDOW(uigtk3->top_window), event) == TRUE) {
		return TRUE;
	}

	return FALSE;
}

// Pointer motion event handler.

gboolean gtk3_handle_motion_notify(GtkWidget *widget, GdkEventMotion *event,
				   gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	struct vo_interface *vo = uigtk3->public.vo_interface;
	(void)widget;

#ifndef WINDOWS32
	// Unhide cursor
	if (uigtk3->cursor_hidden) {
		GdkWindow *window = gtk_widget_get_window(uigtk3->drawing_area);
		gdk_window_set_cursor(window, uigtk3->old_cursor);
		uigtk3->cursor_hidden = 0;
	}
#endif

	// Update position data (for mouse mapped joystick)
	vo->mouse.axis[0] = event->x;
	vo->mouse.axis[1] = event->y;

	return FALSE;
}

// Mouse button press/release event handlers.

// Field middle button press primary selection pasting.

static void clipboard_text_received(GtkClipboard *clipboard, const gchar *text,
				    gpointer data) {
	(void)clipboard;
	(void)data;
	if (!text)
		return;
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
	ak_parse_type_string(xroar.auto_kbd, ntext);
	free(ntext);
}

gboolean gtk3_handle_button_press(GtkWidget *widget, GdkEventButton *event,
				  gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	struct vo_interface *vo = uigtk3->public.vo_interface;
	(void)widget;

	if (event->button == 2) {
		GdkDisplay *d = gtk_widget_get_display(uigtk3->top_window);
		GtkClipboard *cb = gtk_clipboard_get_for_display(d, GDK_SELECTION_PRIMARY);
		gtk_clipboard_request_text(cb, clipboard_text_received, (gpointer)(uintptr_t)event->state);
		return FALSE;
	}

	// Update button data (for mouse mapped joystick)
	if (event->button >= 1 && event->button <= 3) {
		vo->mouse.button[event->button-1] = 1;
	}

	return FALSE;
}

gboolean gtk3_handle_button_release(GtkWidget *widget, GdkEventButton *event,
				    gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	struct vo_interface *vo = uigtk3->public.vo_interface;
	(void)widget;

	// Update button data (for mouse mapped joystick)
	if (event->button >= 1 && event->button <= 3) {
		vo->mouse.button[event->button-1] = 0;
	}

	return FALSE;
}

// Focus event handler.

gboolean gtk3_handle_focus_in(GtkWidget *self, GdkEventFocus *event,
			      gpointer user_data) {
	(void)self;
	(void)event;
	(void)user_data;
	hk_focus_in();
	return TRUE;
}
