/** \file
 *
 *  \brief GTK+ 3 drive control window.
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

#ifndef XROAR_GTK3_DRIVECONTROL_H_
#define XROAR_GTK3_DRIVECONTROL_H_

struct ui_gtk3_interface;
struct uigtk3_dialog;

struct uigtk3_dialog *gtk3_dc_dialog_new(struct ui_gtk3_interface *);

void gtk3_insert_disk(struct ui_gtk3_interface *, int drive);

#endif
