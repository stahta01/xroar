/*

GTK+2 user-interface common functions

Copyright 2014-2015 Ciaran Anscomb

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

void gtk2_keyboard_init(void);
gboolean gtk2_dummy_keypress(GtkWidget *, GdkEventKey *, gpointer);

#endif
