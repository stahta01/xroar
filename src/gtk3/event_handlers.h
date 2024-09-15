/** \file
 *
 *  \brief GTK+ 3 event handlers.
 *
 *  \copyright Copyright 2014-2024 Ciaran Anscomb
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

#ifndef XROAR_GTK3_EVENT_HANDLERS_H_
#define XROAR_GTK3_EVENT_HANDLERS_H_

#include <stdint.h>

#include <glib.h>
#include <gtk/gtk.h>

// Key press event handler.  Hides the pointer; subsequent pointer motion will
// unhide it.

gboolean gtk3_handle_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);

// Key release event handler.

gboolean gtk3_handle_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data);

// Keymap change event handler.

gboolean gtk3_handle_keys_changed(GdkKeymap *gdk_keymap, gpointer user_data);

// Dummy keypress event handler.  Used within tape/drive control dialogs to eat
// keypresses but still allow GUI controls.

gboolean gtk3_dummy_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data);

// Pointer motion event handler.

gboolean gtk3_handle_motion_notify(GtkWidget *widget, GdkEventMotion *event,
				   gpointer user_data);

// Mouse button press/release event handlers.

gboolean gtk3_handle_button_press(GtkWidget *widget, GdkEventButton *event,
				  gpointer user_data);

gboolean gtk3_handle_button_release(GtkWidget *widget, GdkEventButton *event,
				    gpointer user_data);

// Focus event handler.

gboolean gtk3_handle_focus_in(GtkWidget *self, GdkEventFocus *event,
			      gpointer user_data);

#endif
