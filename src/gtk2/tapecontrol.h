/*

GTK+2 tape control window

Copyright 2011 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_GTK2_TAPECONTROL_H_
#define XROAR_GTK2_TAPECONTROL_H_

struct ui_gtk2_interface;

void gtk2_create_tc_window(struct ui_gtk2_interface *uigtk2);
void gtk2_toggle_tc_window(GtkToggleAction *current, gpointer user_data);

void gtk2_input_tape_filename_cb(struct ui_gtk2_interface *uigtk2, const char *filename);
void gtk2_output_tape_filename_cb(struct ui_gtk2_interface *uigtk2, const char *filename);
void gtk2_update_tape_state(struct ui_gtk2_interface *uigtk2, int flags);

#endif
