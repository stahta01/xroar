/** \file
 *
 *  \brief SDL3 keyboard module.
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

#include <SDL3/SDL.h>

#include "hkbd.h"
#include "xroar.h"

#include "sdl3/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_keypress(struct ui_sdl3_interface *uisdl3, SDL_KeyboardEvent *ev) {

#ifdef WINDOWS32
	// In Windows, AltGr generates two events: Left Control then Right Alt.
	// Filter out the Control key here.
	if (ev->scancode == SDL_SCANCODE_LCTRL) {
		SDL_Event ev2;
		if (SDL_PeepEvents(&ev2, 1, SDL_PEEKEVENT, SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_DOWN) == 1) {
			if (ev2.key.scancode == SDL_SCANCODE_RALT) {
				return;
			}
		}
	}
#endif

	if (hkbd.layout == hk_layout_iso) {
		if (ev->scancode == SDL_SCANCODE_BACKSLASH) {
			ev->scancode = SDL_SCANCODE_NONUSHASH;
		}
	}

	if (ev->scancode < 256) {
		hk_scan_press(ev->scancode);
	}

	if (!uisdl3->mouse_hidden) {
		SDL_HideCursor();
		uisdl3->mouse_hidden = 1;
	}

}

void sdl_keyrelease(struct ui_sdl3_interface *uisdl3, SDL_KeyboardEvent *ev) {
	(void)uisdl3;

#ifdef WINDOWS32
	// In Windows, AltGr generates two events: Left Control then Right Alt.
	// Filter out the Control key here.
	if (ev->scancode == SDL_SCANCODE_LCTRL) {
		SDL_Event ev2;
		if (SDL_PeepEvents(&ev2, 1, SDL_PEEKEVENT, SDL_EVENT_KEY_UP, SDL_EVENT_KEY_UP) == 1) {
			if (ev2.key.scancode == SDL_SCANCODE_RALT) {
				return;
			}
		}
	}
#endif

	if (hkbd.layout == hk_layout_iso) {
		if (ev->scancode == SDL_SCANCODE_BACKSLASH) {
			ev->scancode = SDL_SCANCODE_NONUSHASH;
		}
	}

	if (ev->scancode < 256) {
		hk_scan_release(ev->scancode);
	}
}
