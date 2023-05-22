/** \file
 *
 *  \brief GTK+ 2 video options window.
 *
 *  \copyright Copyright 2023 Ciaran Anscomb
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

#ifndef XROAR_GTK2_VIDEO_OPTIONS_H_
#define XROAR_GTK2_VIDEO_OPTIONS_H_

struct ui_gtk2_interface;

void gtk2_vo_create_window(struct ui_gtk2_interface *uigtk2);
void gtk2_vo_toggle_window(GtkToggleAction *current, gpointer user_data);

void gtk2_vo_update_gain(struct ui_gtk2_interface *uigtk2, float value);

void gtk2_vo_update_brightness(struct ui_gtk2_interface *uigtk2, int value);
void gtk2_vo_update_contrast(struct ui_gtk2_interface *uigtk2, int value);
void gtk2_vo_update_saturation(struct ui_gtk2_interface *uigtk2, int value);
void gtk2_vo_update_hue(struct ui_gtk2_interface *uigtk2, int value);

void gtk2_vo_update_cmp_fs(struct ui_gtk2_interface *uigtk2, int value);
void gtk2_vo_update_cmp_fsc(struct ui_gtk2_interface *uigtk2, int value);
void gtk2_vo_update_cmp_system(struct ui_gtk2_interface *uigtk2, int value);

#endif
