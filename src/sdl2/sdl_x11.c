/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2017 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

System event handling for X11 using SDL.

MappingNotify events trigger an update of keyboard mapping tables.

KeymapNotify events used to update internal modifier state.

*/

#include "config.h"

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
