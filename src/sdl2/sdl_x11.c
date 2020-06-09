/*

System event handling for X11 using SDL2

Copyright 2015 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

/*

MappingNotify events trigger an update of keyboard mapping tables.

KeymapNotify events used to update internal modifier state.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <SDL.h>
#include <SDL_syswm.h>
#include <X11/X.h>

#include "logging.h"
#include "sdl2/common.h"

void sdl_x11_handle_syswmevent(SDL_SysWMmsg *wmmsg) {

	switch (wmmsg->msg.x11.event.type) {

	case MappingNotify:
		// Keyboard mapping changed, rebuild our mapping tables.
		sdl_x11_mapping_notify(&wmmsg->msg.x11.event.xmapping);
		break;

	case KeymapNotify:
		// These are received after a window gets focus, so scan
		// keyboard for modifier state.
		sdl_x11_keymap_notify(&wmmsg->msg.x11.event.xkeymap);
		break;

	default:
		break;

	}

}
