/** \file
 *
 *  \brief GTK+ 3 video options window.
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
 */

#ifndef XROAR_GTK3_VIDEO_OPTIONS_H_
#define XROAR_GTK3_VIDEO_OPTIONS_H_

struct ui_gtk3_interface;

void gtk3_vo_create_window(struct ui_gtk3_interface *uigtk3);
void gtk3_vo_toggle_window(GtkToggleAction *current, gpointer user_data);

void gtk3_vo_update_state(struct ui_gtk3_interface *,
			  int tag, int value, const void *data);

void gtk3_vo_update_tv_input(struct ui_gtk3_interface *uigtk3, int value);
void gtk3_vo_update_cmp_renderer(struct ui_gtk3_interface *uigtk3, int value);

#endif
