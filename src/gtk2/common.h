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

#ifndef XROAR_GTK2_COMMON_H_
#define XROAR_GTK2_COMMON_H_

#include <gtk/gtk.h>

struct ui_cfg;

void gtk2_keyboard_init(struct ui_cfg *ui_cfg);
gboolean gtk2_dummy_keypress(GtkWidget *, GdkEventKey *, gpointer);

// Wrappers for notify-only updating of UI elements.  Blocks callback so that
// no further action is taken.

void uigtk2_notify_toggle_button_set(GtkToggleButton *o, gboolean v,
				     gpointer func, gpointer data);

void uigtk2_notify_toggle_action_set(GtkToggleAction *o, gboolean v,
				     gpointer func, gpointer data);

void uigtk2_notify_radio_action_set(GtkRadioAction *o, gint v, gpointer func, gpointer data);

#endif
