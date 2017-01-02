/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2017 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

Extended keyboard handling for X11 using SDL.

SDL2 cleverly "fingerprint"s the X11 keyboard scancode order using a small
subset of KeySyms that are unlikely to be remapped across locales. While this
is limited to the scancode tables included, it does yield good positional data,
which XRoar needs.

However, it then also (as of 2.0.2):

a) ignores modifier mappings (e.g., Caps Lock remapped to Control)
b) hides the KeySym of *modified* keys

XRoar needs a) for interface functions and b) for "translated" keyboard mode.
SDL1.2 provided a "Unicode" mode for b), and got a) right.

Therefore, we redo some of that work here to get a reverse mapping (SDL
scancode to X11 keycode). A lot of work to fix a leaky abstraction.

*/

#include "config.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include "array.h"
#include "xalloc.h"

#include "logging.h"
#include "sdl2/common.h"
#include "sdl2/sdl_x11_keycode_tables.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Keycode "fingerprints" - determine physical layout in the same way as SDL
 * does it. */

static const struct {
	const int keycode_fingerprint[6];
	const int *keycode_table;
} fingerprint_to_map[] = {
	{
		.keycode_fingerprint = { 97, 99, 98, 100, 107, 108 },
		.keycode_table = xfree86_keycode_table,
	},
	{
		.keycode_fingerprint = { 110, 112, 111, 113, 119, 104 },
		.keycode_table = xfree86_keycode_table2,
	},
	{
		.keycode_fingerprint = { 123, 124, 134, 131, 125, 60 },
		.keycode_table = darwin_keycode_table,
	},
};

/* Special KeySym to SDL_Keycode mappings. */

static const struct {
	KeySym keysym;
	SDL_Keycode keycode;
} keysym_to_keycode[] = {

	{ XK_BackSpace, SDLK_BACKSPACE },
	{ XK_Tab, SDLK_TAB },
	{ XK_Linefeed, SDLK_RETURN },
	{ XK_Clear, SDLK_CLEAR },
	{ XK_Return, SDLK_RETURN },
	{ XK_Pause, SDLK_PAUSE },
	{ XK_Escape, SDLK_ESCAPE },
	{ XK_Delete, SDLK_DELETE },

	{ XK_Multi_key, SDLK_APPLICATION },

	{ XK_Home, SDLK_HOME },
	{ XK_Left, SDLK_LEFT },
	{ XK_Up, SDLK_UP },
	{ XK_Right, SDLK_RIGHT },
	{ XK_Down, SDLK_DOWN },
	{ XK_Prior, SDLK_PAGEUP },
	{ XK_Next, SDLK_PAGEDOWN },
	{ XK_End, SDLK_END },

	{ XK_Insert, SDLK_INSERT },
	{ XK_Mode_switch, SDLK_MODE },

	{ XK_Shift_L, SDLK_LSHIFT },
	{ XK_Shift_R, SDLK_RSHIFT },
	{ XK_Control_L, SDLK_LCTRL },
	{ XK_Control_R, SDLK_RCTRL },
	{ XK_Caps_Lock, SDLK_CAPSLOCK },

	{ XK_Alt_L, SDLK_LALT },
	{ XK_Alt_R, SDLK_RALT },
	{ XK_Super_L, SDLK_LGUI },
	{ XK_Super_R, SDLK_RGUI },

	{ XK_ISO_Level3_Shift, SDLK_MODE },

};

#define NLEVELS (4)
#define NKEYCODES (248)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Selected keycode table. */
static const int *keycode_table = NULL;

/* Map X11 keycode to SDL_Keycode. */
static int *x11_to_sdl_keycode = NULL;

/* Track current modifier state. */
static Uint16 mod_state = 0;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void update_mapping_tables(Display *display) {

	// Re-fingerprint the keyboard in the same way that SDL2 did.
	struct {
		KeySym keysym;
		KeyCode keycode;
	} fingerprint[] = {
		{ .keysym = XK_Home },
		{ .keysym = XK_Prior },
		{ .keysym = XK_Up },
		{ .keysym = XK_Left },
		{ .keysym = XK_Delete },
		{ .keysym = XK_KP_Enter },
	};

	for (int i = 0; i < (int)ARRAY_N_ELEMENTS(fingerprint); i++) {
		fingerprint[i].keycode = XKeysymToKeycode(display, fingerprint[i].keysym);
	}

	// Select a keycode table by closest match.
	int max_matched = -1;
	keycode_table = NULL;
	for (int i = 0; i < (int)ARRAY_N_ELEMENTS(fingerprint_to_map); i++) {
		int matched = 0;
		for (int j = 0; j < (int)ARRAY_N_ELEMENTS(fingerprint); j++) {
			if (fingerprint_to_map[i].keycode_fingerprint[j] == fingerprint[j].keycode)
				matched++;
		}
		if (matched > max_matched) {
			max_matched = matched;
			keycode_table = fingerprint_to_map[i].keycode_table;
		}
	}

	// Build the KeyCode to SDL_Keycode mapping table.
	if (x11_to_sdl_keycode)
		free(x11_to_sdl_keycode);
	x11_to_sdl_keycode = xmalloc(sizeof(int) * NLEVELS * NKEYCODES);
	for (int i = 0; i < NKEYCODES * NLEVELS; i++) {
		x11_to_sdl_keycode[i] = SDLK_UNKNOWN;
	}
	for (int i = 0; i < NKEYCODES; i++) {
		int nlevels;
		KeySym *xsyms = XGetKeyboardMapping(display, i+8, 1, &nlevels);
		for (int j = 0; j < NLEVELS; j++) {
			int k = i*NLEVELS + j;
			int symi = j;
			if (symi >= 2) symi += 2;
			symi %= nlevels;
			KeySym xsym;
			xsym = xsyms[symi];
			if ((xsym >= 0x20 && xsym <= 0x7e) ||
			    (xsym >= 0xa0 && xsym <= 0xff)) {
				// Latin-1 characters are identical
				x11_to_sdl_keycode[k] = xsym;
			} else if (xsym >= XK_F1 && xsym <= XK_F12) {
				x11_to_sdl_keycode[k] = (xsym - XK_F1) + SDLK_F1;
			} else for (int ii = 0; ii < (int)ARRAY_N_ELEMENTS(keysym_to_keycode); ii++) {
				if (xsym == keysym_to_keycode[ii].keysym) {
					// Only accept one keysym for keys in this list
					while (j < NLEVELS) {
						x11_to_sdl_keycode[k] = keysym_to_keycode[ii].keycode;
						k++;
						j++;
					}
				}
			}
		}
		XFree(xsyms);
	}
}

void sdl_x11_keyboard_init(SDL_Window *sw) {
	// Get display from SDL.
	SDL_version sdlver;
	SDL_SysWMinfo sdlinfo;
	SDL_VERSION(&sdlver);
	sdlinfo.version = sdlver;
	SDL_GetWindowWMInfo(sw, &sdlinfo);
	Display *display = sdlinfo.info.x11.display;
	update_mapping_tables(display);
}

void sdl_x11_keyboard_free(SDL_Window *sw) {
	(void)sw;
	if (x11_to_sdl_keycode) {
		free(x11_to_sdl_keycode);
		x11_to_sdl_keycode = NULL;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Called on receipt of an X11 MappingNotify event. Update our tables if
 * necessary. */

void sdl_x11_mapping_notify(XMappingEvent *xmapping) {
	if (xmapping->request == MappingModifier || xmapping->request == MappingKeyboard)
		update_mapping_tables(xmapping->display);
}

/* Called on receipt of an X11 KeymapNotify event. Scan the supplied bitmap for
 * modifier keys and update our idea of mod_state. This accounts for the
 * modifier state being changed while our window does not have focus. */

void sdl_x11_keymap_notify(XKeymapEvent *xkeymap) {
	mod_state = 0;
	// Start from 1 - skip the first 8 (invalid) keycodes.
	for (int i = 1; i < 32; i++) {
		if (xkeymap->key_vector[i] == 0)
			continue;
		for (int j = 0; j < 8; j++) {
			if ((xkeymap->key_vector[i] & (1 << j)) == 0)
				continue;
			int keycode = i*8 + j;
			int k = (keycode - 8) * NLEVELS + 0;  // unmodified key
			switch (x11_to_sdl_keycode[k]) {
			case SDLK_LCTRL: mod_state |= KMOD_LCTRL; break;
			case SDLK_RCTRL: mod_state |= KMOD_RCTRL; break;
			case SDLK_LSHIFT: mod_state |= KMOD_LSHIFT; break;
			case SDLK_RSHIFT: mod_state |= KMOD_RSHIFT; break;
			case SDLK_LALT: mod_state |= KMOD_LALT; break;
			case SDLK_RALT: mod_state |= KMOD_RALT; break;
			case SDLK_LGUI: mod_state |= KMOD_LGUI; break;
			case SDLK_RGUI: mod_state |= KMOD_RGUI; break;
			case SDLK_MODE: mod_state |= KMOD_MODE; break;
			default: break;
			}
		}
	}
}

/* Rewrite an SDL_KeyboardEvent based on the actual keyboard map. */

void sdl_x11_fix_keyboard_event(SDL_Event *event) {
	int keycode = keycode_table[event->key.keysym.scancode];
	if (keycode < 8) {
		event->key.keysym.sym = SDLK_UNKNOWN;
		return;
	}
	int k = (keycode - 8) * NLEVELS + 0;  // unmodified key
	event->key.keysym.sym = x11_to_sdl_keycode[k];

	// Update SDL modifier state according to key press/release.
	Uint16 mod_bit = 0;
	switch (event->key.keysym.sym) {
	case SDLK_LCTRL: mod_bit = KMOD_LCTRL; break;
	case SDLK_RCTRL: mod_bit = KMOD_RCTRL; break;
	case SDLK_LSHIFT: mod_bit = KMOD_LSHIFT; break;
	case SDLK_RSHIFT: mod_bit = KMOD_RSHIFT; break;
	case SDLK_LALT: mod_bit = KMOD_LALT; break;
	case SDLK_RALT: mod_bit = KMOD_RALT; break;
	case SDLK_LGUI: mod_bit = KMOD_LGUI; break;
	case SDLK_RGUI: mod_bit = KMOD_RGUI; break;
	case SDLK_MODE: mod_bit = KMOD_MODE; break;
	default: break;
	}
	if (event->key.type == SDL_KEYDOWN)
		mod_state |= mod_bit;
	else
		mod_state &= ~mod_bit;
	event->key.keysym.mod = mod_state;
}

/* Return an 'expanded' SDL_Keycode based on actual keyboard map and modifier
 * state. This includes the symbols on modified keys. */

int sdl_x11_keysym_to_unicode(SDL_Keysym *keysym) {
	int shift_level = (keysym->mod & KMOD_MODE) ? 2 : 0;
	shift_level |= (keysym->mod & (KMOD_LSHIFT|KMOD_RSHIFT)) ? 1 : 0;

	// Determine expanded SDL_Keycode based on shift level.
	int keycode = keycode_table[keysym->scancode];
	if (keycode < 8)
		return 0;
	int k = (keycode - 8) * NLEVELS + shift_level;

	return x11_to_sdl_keycode[k];
}
