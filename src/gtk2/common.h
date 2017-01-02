/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_GTK2_COMMON_H_
#define XROAR_GTK2_COMMON_H_

#include <gtk/gtk.h>

void gtk2_keyboard_init(void);
gboolean gtk2_dummy_keypress(GtkWidget *, GdkEventKey *, gpointer);

#endif  /* XROAR_GTK2_COMMON_H_ */
