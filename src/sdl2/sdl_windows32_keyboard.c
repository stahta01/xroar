/*

Extended keyboard handling for Windows32 using SDL2

Copyright 2015-2017 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <windows.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "array.h"
#include "xalloc.h"

#include "logging.h"
#include "sdl2/common.h"
#include "sdl2/sdl_windows32_vsc_table.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define NLEVELS (4)
#define NVSC (256)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Map Windows32 scancode to SDL_Keycode. */
static int windows32_to_sdl_keycode[NVSC][NLEVELS];

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void update_mapping_tables(void) {
	// Build the vsc to SDL_Keycode mapping table.
	BYTE state[256];
	memset(state, 0, sizeof(state));

	for (int i = 0; i < NVSC; i++) {
		for (int j = 0; j < NLEVELS; j++) {
			windows32_to_sdl_keycode[i][j] = SDLK_UNKNOWN;
		}
	}

	// Under Wine, SDL ends up thinking my AltGr (ISO_Level3_Shift) is F16.
	// Odd.  However, all this is now tested on a real Windows box, and
	// AltGr characters seem to be correctly determined.

	for (int i = 0; i < NVSC; i++) {
		UINT vsc = windows_vsc_table[i];
		UINT vk = MapVirtualKey(vsc, MAPVK_VSC_TO_VK);
		for (int j = 0; j < NLEVELS; j++) {
			state[VK_SHIFT] = (j & 1) ? 0x80 : 0;
			state[VK_LSHIFT] = (j & 1) ? 0x80 : 0;
			state[VK_MENU] = (j & 2) ? 0x80 : 0;
			state[VK_RMENU] = (j & 2) ? 0x80 : 0;
			state[VK_CONTROL] = (j & 2) ? 0x80 : 0;
			state[VK_LCONTROL] = (j & 2) ? 0x80 : 0;
			Uint16 wchar;
			// Request the key twice.  This way, dead keys should
			// be mapped into the symbol representing the diacritic
			// and the dead state cleared before the next mapping.
			(void)ToUnicode(vk, vsc, state, &wchar, 1, 0);
			if (ToUnicode(vk, vsc, state, &wchar, 1, 0) > 0) {
				windows32_to_sdl_keycode[i][j] = wchar;
			}
		}
	}
}

void sdl_windows32_keyboard_init(SDL_Window *sw) {
	(void)sw;
	update_mapping_tables();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Return an 'expanded' SDL_Keycode based on keyboard map and modifier state.
 * This includes the symbols on modified keys. */

int sdl_windows32_keysym_to_unicode(SDL_Keysym *keysym) {
	if (keysym->scancode < 0 || keysym->scancode >= NVSC)
		return SDLK_UNKNOWN;

	// Determine expanded SDL_Keycode based on shift level.
	int level = (keysym->mod & KMOD_RALT) ? 2 : 0;
	level |= (keysym->mod & (KMOD_LSHIFT|KMOD_RSHIFT)) ? 1 : 0;
	return windows32_to_sdl_keycode[keysym->scancode][level];
}
