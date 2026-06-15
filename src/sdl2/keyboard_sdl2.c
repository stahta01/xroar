/** \file
 *
 *  \brief SDL2 keyboard module.
 *
 *  \copyright Copyright 2015-2026 Ciaran Anscomb
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

#include <SDL.h>

#include "hkbd.h"
#include "xroar.h"

#include "sdl2/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_keypress(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym) {

	if (hkbd.layout == hk_layout_iso) {
		if (keysym->scancode == SDL_SCANCODE_BACKSLASH) {
			keysym->scancode = SDL_SCANCODE_NONUSHASH;
		}
	}

	if (keysym->scancode < 256) {
		hk_scan_press(keysym->scancode);
	}

	if (!uisdl2->mouse_hidden) {
		SDL_ShowCursor(SDL_DISABLE);
		uisdl2->mouse_hidden = 1;
	}

}

void sdl_keyrelease(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym) {
	(void)uisdl2;

	if (hkbd.layout == hk_layout_iso) {
		if (keysym->scancode == SDL_SCANCODE_BACKSLASH) {
			keysym->scancode = SDL_SCANCODE_NONUSHASH;
		}
	}

	if (keysym->scancode < 256) {
		hk_scan_release(keysym->scancode);
	}
}
