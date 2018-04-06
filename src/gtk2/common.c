/*

GTK+2 user-interface common functions

Copyright 2014-2016 Ciaran Anscomb

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
