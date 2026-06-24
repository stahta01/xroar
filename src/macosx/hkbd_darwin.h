/** \file
 *
 *  \brief Darwin keyboard handling.
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

#ifndef XROAR_HKBD_DARWIN_H_
#define XROAR_HKBD_DARWIN_H_

#include <stdint.h>

extern const uint8_t darwin_to_hk_scancode[128];

bool hk_darwin_update_keymap(void);

#endif
