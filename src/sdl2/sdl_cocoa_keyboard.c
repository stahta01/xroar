/*

Extended keyboard handling for Mac OS X using SDL

Copyright 2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

/*

Initial version.  Doesn't know anything about keymap updates yet.

*/

#include "config.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <Carbon/Carbon.h>

#include "array.h"
//#include "xalloc.h"

#include "logging.h"
#include "sdl2/common.h"
#include "sdl2/sdl_x11_keycode_tables.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define NLEVELS (4)

/* Map scancode to keycode. */
static int scancode_to_keycode[SDL_NUM_SCANCODES][NLEVELS];

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void update_mapping_tables(void) {
	// Reset mapping
	for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
		for (int j = 0; j < NLEVELS; j++) {
			scancode_to_keycode[i][j] = SDLK_UNKNOWN;
		}
	}

	// Approach to navigating the convoluted way macosx hides its data away
	// adapted from a stackoverflow reply by jlstrecker.

	TISInputSourceRef kbd_ref = TISCopyCurrentKeyboardLayoutInputSource();

	CFDataRef layout_ref = TISGetInputSourceProperty(kbd_ref, kTISPropertyUnicodeKeyLayoutData);
	const UCKeyboardLayout *layout;
	if (layout_ref) {
		layout = (const UCKeyboardLayout *)CFDataGetBytePtr(layout_ref);
	} else {
		CFRelease(kbd_ref);
		return;
	}

	UniChar buf[8];

	for (int i = 0; i < (int)ARRAY_N_ELEMENTS(darwin_keycode_table); i++) {
		for (int j = 0; j < NLEVELS; j++) {
			UInt32 modifierKeyState = 0;
			if (j & 1)
				modifierKeyState |= ((1 << shiftKeyBit) >> 8);
			if (j & 2)
				modifierKeyState |= ((1 << optionKeyBit) >> 8);
			if (darwin_keycode_table[i] >= 8) {
				UInt32 deadKeyState = 0;
				UniCharCount uclen = 0;
				UCKeyTranslate(layout, darwin_keycode_table[i]-8,
						kUCKeyActionDown, modifierKeyState, LMGetKbdType(),
						kUCKeyTranslateNoDeadKeysMask, &deadKeyState, 8, &uclen, buf);
				if (uclen == 1) {
					scancode_to_keycode[i][j] = buf[0];
				}
			}
		}
	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_cocoa_keyboard_init(SDL_Window *sw) {
	(void)sw;
	update_mapping_tables();
}

int sdl_cocoa_keysym_to_unicode(SDL_Keysym *keysym) {
	int level = (keysym->mod & KMOD_ALT) ? 2 : 0;
	level |= (keysym->mod & KMOD_SHIFT) ? 1 : 0;
	return scancode_to_keycode[keysym->scancode][level];
}
