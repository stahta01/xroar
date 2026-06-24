/** \file
 *
 *  \brief X11 keyboard handling.
 *
 *  \copyright Copyright 2023-2024 Ciaran Anscomb
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

#ifndef XROAR_X11_HKBD_X11_H_
#define XROAR_X11_HKBD_X11_H_

#include <stdint.h>
#include <X11/Xlib.h>

// Toolkit should call this before calling hk_init()
void hk_x11_set_display(Display *d);

bool hk_x11_update_keymap(void);

// Map X11 keysym to HK sym.

uint16_t x11_keysym_to_hk_sym(KeySym x11_sym);

// Call on receipt of an X11 MappingNotify event. Updates tables if necessary.

void hk_x11_handle_mapping_event(XMappingEvent *xmapping);

// Call on receipt of an X11 KeymapNotify event.  Scans the supplied bitmap for
// modifier keys and update our idea of mod_state.  This accounts for the
// modifier state being changed while our window does not have focus.

void hk_x11_handle_keymap_event(XKeymapEvent *xkeymap);

// Call on focus event.

bool hk_x11_focus_in(void);

#endif
