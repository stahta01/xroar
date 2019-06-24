/*

GTK+2 user-interface module

Copyright 2010-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_GTK2_UI_GTK2_H_
#define XROAR_GTK2_UI_GTK2_H_

#include "joystick.h"
#include "vo.h"

extern struct vo_rect gtk2_display;
extern struct joystick_submodule gtk2_js_submod_keyboard;

extern GtkWidget *gtk2_top_window;
extern GtkWidget *gtk2_drawing_area;
extern GtkWidget *gtk2_menubar;
extern GtkUIManager *gtk2_menu_manager;

#endif  /* XROAR_GTK2_UI_GTK2_H_ */
