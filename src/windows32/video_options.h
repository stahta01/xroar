/** \file
 *
 *  \brief Windows video options window.
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

#ifndef XROAR_WINDOWS_VIDEO_OPTIONS_H_
#define XROAR_WINDOWS_VIDEO_OPTIONS_H_

struct ui_sdl2_interface;

void windows32_vo_create_window(struct ui_sdl2_interface *uisdl2);
void windows32_vo_show_window(struct ui_sdl2_interface *uisdl2);

void windows32_vo_update_brightness(struct ui_sdl2_interface *uisdl2, int value);
void windows32_vo_update_contrast(struct ui_sdl2_interface *uisdl2, int value);
void windows32_vo_update_hue(struct ui_sdl2_interface *uisdl2, int value);

#endif
