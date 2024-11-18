/** \file
 *
 *  \brief GTK+ 2 drive control window.
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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

#ifndef XROAR_GTK2_DRIVECONTROL_H_
#define XROAR_GTK2_DRIVECONTROL_H_

struct ui_gtk2_interface;
struct uigtk2_dialog;

struct uigtk2_dialog *gtk2_dc_dialog_new(struct ui_gtk2_interface *);

void gtk2_insert_disk(struct ui_gtk2_interface *, int drive);

#endif
