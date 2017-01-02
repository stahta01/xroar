/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

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
