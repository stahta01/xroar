/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2017 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

Extended keyboard handling for Windows32 using SDL.

*/

#include "config.h"

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
static int *windows32_to_sdl_keycode = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void update_mapping_tables(void) {
	// Build the vsc to SDL_Keycode mapping table.
	if (windows32_to_sdl_keycode)
		free(windows32_to_sdl_keycode);
	windows32_to_sdl_keycode = xmalloc(sizeof(int) * NLEVELS * NVSC);
	BYTE state[256];
	memset(state, 0, sizeof(state));

	for (int i = 0; i < NVSC * NLEVELS; i++) {
		windows32_to_sdl_keycode[i] = SDLK_UNKNOWN;
	}

	for (int i = 0; i < NVSC; i++) {
		UINT vsc = windows_vsc_table[i];
		for (int j = 0; j < NLEVELS; j++) {
			int k = i*NLEVELS + j;
			state[VK_SHIFT] = (j & 1) ? 0x80 : 0;
			// Wine seems to take the host input method rather than
			// simulate AltGr+key, so I can't test this. SDL ends
			// up thinking my AltGr (ISO_Level3_Shift) is F16.
			state[VK_RMENU] = (j & 2) ? 0x80 : 0;
			Uint16 wchars[2];
			UINT vk = MapVirtualKey(vsc, MAPVK_VSC_TO_VK);
			if (ToUnicode(vk, vsc, state, wchars, sizeof(wchars)/sizeof(wchars[0]), 0) > 0) {
				windows32_to_sdl_keycode[k] = wchars[0];
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
	int shift_level = (keysym->mod & KMOD_RALT) ? 2 : 0;
	shift_level |= (keysym->mod & (KMOD_LSHIFT|KMOD_RSHIFT)) ? 1 : 0;

	// Determine expanded SDL_Keycode based on shift level.
	int k = (keysym->scancode * NLEVELS) + shift_level;
	return windows32_to_sdl_keycode[k];
}
