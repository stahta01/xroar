/* Screenshots.
 *
 * Copyright 2023 Ciaran Anscomb
 *
 * This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 * XRoar is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * See COPYING.GPL for redistribution conditions.
 */

#ifndef XROAR_SCREENSHOT_H_
#define XROAR_SCREENSHOT_H_

struct vo_interface;

int screenshot_write_png(const char *filename, struct vo_interface *vo);

#endif
