/** \file
 *
 *  \brief GTK+ 2 tape control window.
 *
 *  \copyright Copyright 2011 Ciaran Anscomb
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

#ifndef XROAR_GTK2_TAPECONTROL_H_
#define XROAR_GTK2_TAPECONTROL_H_

struct ui_gtk2_interface;

void gtk2_create_tc_window(void);
void gtk2_toggle_tc_window(GtkToggleAction *current, gpointer user_data);

void gtk2_update_tape_state(int flags);
void gtk2_input_tape_filename_cb(const char *filename);
void gtk2_output_tape_filename_cb(const char *filename);
void gtk2_update_tape_playing(int playing);

#endif
