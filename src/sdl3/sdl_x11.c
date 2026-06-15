/** \file
 *
 *  \brief Extended keyboard handling for X11 using SDL.
 *
 *  \copyright Copyright 2015-2024 Ciaran Anscomb
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

#include "top-config.h"

#include <X11/Xlib.h>
#include <SDL3/SDL.h>

#include "hkbd.h"
#include "sdl3/common.h"
#include "x11/hkbd_x11.h"

bool SDLCALL sdl_x11_event_hook(void *sptr, XEvent *xevent) {
	struct ui_sdl3_interface *uisdl3 = sptr;
	(void)uisdl3;

	switch (xevent->type) {
	case MappingNotify:
		// Keyboard mapping changed, rebuild our mapping tables.
		hk_x11_handle_mapping_event(&xevent->xmapping);
		break;

	case KeymapNotify:
		// These are received after a window gets focus, so scan
		// keyboard for modifier state.
		hk_x11_handle_keymap_event(&xevent->xkeymap);
		break;

	default:
		break;
	}

	return 1;
}
