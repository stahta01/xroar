/*

GTK+2 drive control window

Copyright 2011-2015 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_GTK2_DRIVECONTROL_H_
#define XROAR_GTK2_DRIVECONTROL_H_

struct ui_gtk2_interface;

struct vdisk;

void gtk2_insert_disk(struct ui_gtk2_interface *uigtk2, int drive);
void gtk2_create_dc_window(struct ui_gtk2_interface *uigtk2);
void gtk2_toggle_dc_window(GtkToggleAction *current, gpointer user_data);

void gtk2_update_drive_disk(int drive, const struct vdisk *disk);
void gtk2_update_drive_write_enable(int drive, _Bool write_enable);
void gtk2_update_drive_write_back(int drive, _Bool write_back);

#endif
