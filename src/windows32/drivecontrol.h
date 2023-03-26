/** \file
 *
 *  \brief Windows drive control window.
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

#ifndef XROAR_WINDOWS_DRIVECONTROL_H_
#define XROAR_WINDOWS_DRIVECONTROL_H_

struct ui_sdl2_interface;

void windows32_dc_create_window(struct ui_sdl2_interface *uisdl2);
void windows32_dc_show_window(struct ui_sdl2_interface *uisdl2);

void windows32_dc_update_drive_disk(struct ui_sdl2_interface *uisdl2,
				    int drive, const struct vdisk *disk);
void windows32_dc_update_drive_write_enable(struct ui_sdl2_interface *uisdl2,
					    int drive, _Bool write_enable);
void windows32_dc_update_drive_write_back(struct ui_sdl2_interface *uisdl2,
					  int drive, _Bool write_back);

#endif
