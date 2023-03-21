/** \file
 *
 *  \brief Windows tape control window.
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

#ifndef XROAR_WINDOWS_TAPECONTROL_H_
#define XROAR_WINDOWS_TAPECONTROL_H_

struct ui_sdl2_interface;

void windows32_tc_create_window(struct ui_sdl2_interface *uisdl2);
void windows32_tc_show_window(struct ui_sdl2_interface *uisdl2);

void windows32_tc_update_tape_state(struct ui_sdl2_interface *uisdl2, int flags);
void windows32_tc_update_input_filename(struct ui_sdl2_interface *uisdl2, const char *filename);
void windows32_tc_update_output_filename(struct ui_sdl2_interface *uisdl2, const char *filename);
void windows32_tc_update_tape_playing(struct ui_sdl2_interface *uisdl2, int playing);

#endif
