/*

SDL2 keyboard module

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

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "array.h"
#include "c-strcase.h"
#include "pl-string.h"
#include "xalloc.h"

#include "dkbd.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "module.h"
#include "printer.h"
#include "xroar.h"

#include "sdl2/common.h"

// Note that a lot of shortcuts are omitted in WebAssembly builds - browsers
// tend to steal all those keys for themselves.

struct scancode_dkey_mapping {
	SDL_Scancode scancode;
	int8_t dkey;
	_Bool priority;  // key overrides unicode translation
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);
static void unmap_axis(struct joystick_axis *axis);
static void unmap_button(struct joystick_button *button);

struct joystick_submodule sdl_js_submod_keyboard = {
	.name = "keyboard",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
	.unmap_axis = unmap_axis,
	.unmap_button = unmap_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct axis {
	SDL_Keycode key0, key1;
	unsigned value;
};

struct button {
	SDL_Keycode key;
	_Bool value;
};

#define MAX_AXES (4)
#define MAX_BUTTONS (4)

static struct axis *enabled_axis[MAX_AXES];
static struct button *enabled_button[MAX_BUTTONS];

/* Default host key mappings likely to be common across all keyboard types. */

static struct scancode_dkey_mapping scancode_dkey_default[] = {
	// Rest of the normal keys
	{ SDL_SCANCODE_MINUS, DSCAN_COLON, 0 },
	{ SDL_SCANCODE_EQUALS, DSCAN_MINUS, 0 },
	{ SDL_SCANCODE_LEFTBRACKET, DSCAN_AT, 0 },
	{ SDL_SCANCODE_SEMICOLON, DSCAN_SEMICOLON, 0 },
	{ SDL_SCANCODE_GRAVE, DSCAN_CLEAR, 1 },
	{ SDL_SCANCODE_COMMA, DSCAN_COMMA, 0 },
	{ SDL_SCANCODE_PERIOD, DSCAN_FULL_STOP, 0 },
	{ SDL_SCANCODE_SLASH, DSCAN_SLASH, 0 },

	// Common
	{ SDL_SCANCODE_ESCAPE, DSCAN_BREAK, 1 },
	{ SDL_SCANCODE_RETURN, DSCAN_ENTER, 0 },
	{ SDL_SCANCODE_HOME, DSCAN_CLEAR, 1 },
	{ SDL_SCANCODE_LSHIFT, DSCAN_SHIFT, 1 },
	{ SDL_SCANCODE_RSHIFT, DSCAN_SHIFT, 1 },
	{ SDL_SCANCODE_SPACE, DSCAN_SPACE, 0 },
	{ SDL_SCANCODE_F1, DSCAN_F1, 1 },
	{ SDL_SCANCODE_F2, DSCAN_F2, 1 },

	// Not so common
	{ SDL_SCANCODE_CLEAR, DSCAN_CLEAR, 1 },

	// Cursor keys
	{ SDL_SCANCODE_UP, DSCAN_UP, 1 },
	{ SDL_SCANCODE_DOWN, DSCAN_DOWN, 1 },
	{ SDL_SCANCODE_LEFT, DSCAN_LEFT, 1 },
	{ SDL_SCANCODE_RIGHT, DSCAN_RIGHT, 1 },
	{ SDL_SCANCODE_BACKSPACE, DSCAN_LEFT, 1 },
	{ SDL_SCANCODE_DELETE, DSCAN_LEFT, 1 },
	{ SDL_SCANCODE_TAB, DSCAN_RIGHT, 1 },

	// Keypad
	{ SDL_SCANCODE_KP_MULTIPLY, DSCAN_COLON, 1 },
	{ SDL_SCANCODE_KP_MINUS, DSCAN_MINUS, 1 },
	{ SDL_SCANCODE_KP_PLUS, DSCAN_SEMICOLON, 1 },
	{ SDL_SCANCODE_KP_PERIOD, DSCAN_FULL_STOP, 1 },
	{ SDL_SCANCODE_KP_DIVIDE, DSCAN_SLASH, 1 },
	{ SDL_SCANCODE_KP_ENTER, DSCAN_ENTER, 0 },
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_keyboard_init(struct ui_sdl2_interface *uisdl2) {

	// First clear the table and map obvious keys.
	for (unsigned i = 0; i < SDL_NUM_SCANCODES; i++) {
		uisdl2->keyboard.scancode_to_dkey[i] = DSCAN_INVALID;
		uisdl2->keyboard.scancode_priority[i] = 0;
	}
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(scancode_dkey_default); i++) {
		uisdl2->keyboard.scancode_to_dkey[scancode_dkey_default[i].scancode] = scancode_dkey_default[i].dkey;
		uisdl2->keyboard.scancode_priority[scancode_dkey_default[i].scancode] = scancode_dkey_default[i].priority;
	}
	for (unsigned i = 0; i < 9; i++) {
		uisdl2->keyboard.scancode_to_dkey[SDL_SCANCODE_1 + i] = DSCAN_1 + i;
		uisdl2->keyboard.scancode_to_dkey[SDL_SCANCODE_KP_1 + i] = DSCAN_1 + i;
	}
	uisdl2->keyboard.scancode_to_dkey[SDL_SCANCODE_0] = DSCAN_0;
	uisdl2->keyboard.scancode_to_dkey[SDL_SCANCODE_KP_0] = DSCAN_0;
	for (unsigned i = 0; i <= 25; i++)
		uisdl2->keyboard.scancode_to_dkey[SDL_SCANCODE_A + i] = DSCAN_A + i;

	for (unsigned i = 0; i < SDL_NUM_SCANCODES; i++)
		uisdl2->keyboard.unicode_last_scancode[i] = 0;

	// Clear the keystick mappings.

	for (unsigned i = 0; i < MAX_AXES; i++)
		enabled_axis[i] = NULL;
	for (unsigned i = 0; i < MAX_BUTTONS; i++)
		enabled_button[i] = NULL;

}

static void emulator_command(struct ui_sdl2_interface *uisdl2, int cmdkey, _Bool shift) {
	switch (cmdkey) {
	case '1': case '2': case '3': case '4':
		if (shift) {
			xroar_new_disk(cmdkey - '1');
		} else {
			xroar_insert_disk(cmdkey - '1');
		}
		return;
	case '5': case '6': case '7': case '8':
		if (shift) {
			xroar_set_write_back(1, cmdkey - '5', XROAR_NEXT);
		} else {
			xroar_set_write_enable(1, cmdkey - '5', XROAR_NEXT);
		}
		return;
	case 'a': xroar_set_cross_colour(1, XROAR_NEXT); return;
	case 'q': xroar_quit(); return;
	case 'e': xroar_toggle_cart(); return;
	case 'f': xroar_set_fullscreen(1, XROAR_NEXT); return;
	case 'h':
		     if (shift) {
			     xroar_set_pause(1, XROAR_NEXT);
		     }
		     return;
	case 'i':
		     if (shift) {
			     xroar_set_vdg_inverted_text(1, XROAR_NEXT);
		     } else {
			     xroar_run_file(NULL);
		     }
		     return;
	case 'j':
		     if (shift) {
			     xroar_swap_joysticks(1);
		     } else {
			     xroar_cycle_joysticks(1);
		     }
		     return;
	case 'k': xroar_set_keymap(1, XROAR_NEXT); return;
	case 'l':
		     if (shift) {
			     xroar_run_file(NULL);
		     } else {
			     xroar_load_file(NULL);
		     }
		     return;
	case 'm': xroar_set_machine(1, XROAR_NEXT); return;
	case 'p':
		     if (shift) {
			     printer_flush(xroar_printer_interface);
		     }
		     return;
	case 'r':
		     if (shift) {
			     xroar_hard_reset();
		     } else {
			     xroar_soft_reset();
		     }
		     return;
	case 's': xroar_save_snapshot(); return;
	case 'w': xroar_insert_output_tape(); return;
#ifdef TRACE
	case 'v': xroar_set_trace(XROAR_NEXT); return;
#endif
	case 'z': xroar_set_kbd_translate(1, XROAR_NEXT); return;
	case '-':
		     sdl_zoom_out(uisdl2);
		     return;
	case '+':
		     sdl_zoom_in(uisdl2);
		     return;
	default:
		break;
	}
}

static void control_keypress(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym) {
	SDL_Scancode scancode = keysym->scancode;
	SDL_Keycode sym = keysym->sym;
	Uint16 mod = keysym->mod;

	_Bool shift = mod & KMOD_SHIFT;

	int cmdkey = 0;
	// Number keys may have a non-numeric sym (e.g., French mapping), so
	// handle these functions based on scancode (which should be pretty
	// constant).
	if (scancode == SDL_SCANCODE_0) {
		cmdkey = '0';
	} else if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
		cmdkey = (scancode - SDL_SCANCODE_1) + '1';
	}

	// Letter keys are likely to be available unshifted.
	if (cmdkey == 0 && sym >= SDLK_a && sym <= SDLK_z) {
		cmdkey = sym;
	}

	// Otherwise, if there's an OS-level translation, try that.
	if (cmdkey == 0) {
		cmdkey = sdl_os_keysym_to_unicode(keysym);
	}

	// Other keys that *might* be available as unshifted syms and are
	// useful as command keys.
	if (cmdkey == 0) switch (sym) {
	case SDLK_MINUS: cmdkey = '-'; break;
	case SDLK_PLUS: cmdkey = '+'; break;
	default: break;
	}

	if (cmdkey == 0)
		return;
#ifndef HAVE_WASM
	emulator_command(uisdl2, cmdkey, shift);
#endif
}


void sdl_keypress(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym) {
	SDL_Scancode scancode = keysym->scancode;
	SDL_Keycode sym = keysym->sym;
	Uint16 mod = keysym->mod;

	if (scancode == SDL_SCANCODE_UNKNOWN)
		return;
	if (scancode >= SDL_NUM_SCANCODES)
		return;
	if (sym == SDL_SCANCODE_UNKNOWN)
		return;

	if (!uisdl2->mouse_hidden) {
		SDL_ShowCursor(SDL_DISABLE);
		uisdl2->mouse_hidden = 1;
	}

	if (xroar_cfg.debug_ui & XROAR_DEBUG_UI_KBD_EVENT) {
		int unicode = sdl_os_keysym_to_unicode(keysym);
		if (unicode & 0x40000000)
			unicode = 0;
		LOG_PRINT("sdl.key press   scan=%3d   sym=%08x   mod=%04x   unicode=%04x   name=%s\n", scancode, sym, mod, unicode, SDL_GetKeyName(sym));
	}

	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (enabled_axis[i]) {
			if (sym == enabled_axis[i]->key0) {
				enabled_axis[i]->value = 0;
				return;
			}
			if (sym == enabled_axis[i]->key1) {
				enabled_axis[i]->value = 65535;
				return;
			}
		}
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (enabled_button[i]) {
			if (sym == enabled_button[i]->key) {
				enabled_button[i]->value = 1;
				return;
			}
		}
	}

	_Bool shift = mod & KMOD_SHIFT;
	_Bool ralt = mod & KMOD_RALT;
	_Bool control = !ralt && (mod & KMOD_CTRL);

	switch (sym) {
	case SDLK_LSHIFT: case SDLK_RSHIFT:
		KEYBOARD_PRESS_SHIFT(xroar_keyboard_interface);
		return;
	case SDLK_CLEAR:
		KEYBOARD_PRESS_CLEAR(xroar_keyboard_interface);
		return;
	case SDLK_LCTRL: case SDLK_RCTRL:
		return;
#ifndef HAVE_WASM
	case SDLK_F11:
		xroar_set_fullscreen(1, XROAR_NEXT);
		return;
	case SDLK_F12:
		if (shift) {
			xroar_set_ratelimit_latch(1, XROAR_NEXT);
		} else {
			xroar_set_ratelimit(0);
		}
		return;
#endif
	case SDLK_PAUSE:
		xroar_set_pause(1, XROAR_NEXT);
		return;
	default:
		break;
	}

	if (control) {
		control_keypress(uisdl2, keysym);
		return;
	}

	// If scancode has priority, never do a unicode lookup.
	if (uisdl2->keyboard.scancode_priority[scancode]) {
		keyboard_press(xroar_keyboard_interface, uisdl2->keyboard.scancode_to_dkey[scancode]);
		return;
	}

	if (uisdl2->keyboard.translate) {
		int unicode = sdl_os_keysym_to_unicode(keysym);
		/* shift + backspace -> erase line */
		if (shift && (unicode == 0x08 || unicode == 0x7f))
			unicode = DKBD_U_ERASE_LINE;
		/* shift + enter -> caps lock */
		if (uisdl2->keyboard.scancode_to_dkey[scancode] == DSCAN_ENTER)
			unicode = shift ? DKBD_U_CAPS_LOCK : 0x0d;
		/* shift + clear -> pause output */
		if (uisdl2->keyboard.scancode_to_dkey[scancode] == DSCAN_SPACE)
			unicode = shift ? DKBD_U_PAUSE_OUTPUT : 0x20;
		uisdl2->keyboard.unicode_last_scancode[scancode] = unicode;
		keyboard_unicode_press(xroar_keyboard_interface, unicode);
		return;
	}

	keyboard_press(xroar_keyboard_interface, uisdl2->keyboard.scancode_to_dkey[scancode]);
}

void sdl_keyrelease(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym) {
	(void)uisdl2;
	SDL_Scancode scancode = keysym->scancode;
	SDL_Keycode sym = keysym->sym;
	Uint16 mod = keysym->mod;

	if (scancode == SDL_SCANCODE_UNKNOWN)
		return;
	if (scancode >= SDL_NUM_SCANCODES)
		return;
	if (sym == SDL_SCANCODE_UNKNOWN)
		return;

	if (xroar_cfg.debug_ui & XROAR_DEBUG_UI_KBD_EVENT) {
		int unicode = 0;
		if (scancode < SDL_NUM_SCANCODES)
			unicode = uisdl2->keyboard.unicode_last_scancode[scancode];
		if (unicode & 0x40000000)
			unicode = 0;
		LOG_PRINT("sdl.key release scan=%3d   sym=%08x   mod=%04x   unicode=%04x   name=%s\n", scancode, sym, mod, unicode, SDL_GetKeyName(sym));
	}

	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (enabled_axis[i]) {
			if (sym == enabled_axis[i]->key0) {
				if (enabled_axis[i]->value < 32768)
					enabled_axis[i]->value = 32767;
				return;
			}
			if (sym == enabled_axis[i]->key1) {
				if (enabled_axis[i]->value >= 32768)
					enabled_axis[i]->value = 32768;
				return;
			}
		}
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (enabled_button[i]) {
			if (sym == enabled_button[i]->key) {
				enabled_button[i]->value = 0;
				return;
			}
		}
	}

	_Bool shift = mod & KMOD_SHIFT;
	//_Bool ralt = mod & KMOD_RALT;
	//_Bool control = !ralt && (mod & KMOD_CTRL);

	switch (sym) {
	case SDLK_LSHIFT: case SDLK_RSHIFT:
		if (!shift)
			KEYBOARD_RELEASE_SHIFT(xroar_keyboard_interface);
		return;
	case SDLK_CLEAR:
		KEYBOARD_RELEASE_CLEAR(xroar_keyboard_interface);
		return;
	case SDLK_LCTRL: case SDLK_RCTRL:
		return;
	case SDLK_F12:
		xroar_set_ratelimit(1);
		return;
	default:
		break;
	}

	// If scancode has priority, never do a unicode lookup.
	if (uisdl2->keyboard.scancode_priority[scancode]) {
		keyboard_release(xroar_keyboard_interface, uisdl2->keyboard.scancode_to_dkey[scancode]);
		return;
	}

	if (uisdl2->keyboard.translate) {
		int unicode;
		if (scancode >= SDL_NUM_SCANCODES)
			return;
		unicode = uisdl2->keyboard.unicode_last_scancode[scancode];
		keyboard_unicode_release(xroar_keyboard_interface, unicode);
		/* Put shift back the way it should be */
		if (shift)
			KEYBOARD_PRESS_SHIFT(xroar_keyboard_interface);
		else
			KEYBOARD_RELEASE_SHIFT(xroar_keyboard_interface);
		return;
	}

	keyboard_release(xroar_keyboard_interface, uisdl2->keyboard.scancode_to_dkey[scancode]);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static unsigned read_axis(struct axis *a) {
	return a->value;
}

static _Bool read_button(struct button *b) {
	return b->value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static SDL_Keycode get_key_by_name(const char *name) {
	if (isdigit(name[0]))
		return strtol(name, NULL, 0);
	for (SDL_Scancode i = 0; i < SDL_NUM_SCANCODES; i++) {
		SDL_Keycode sym = SDL_GetKeyFromScancode(i);
		if (0 == c_strcasecmp(name, SDL_GetKeyName(sym)))
			return sym;
	}
	return SDL_SCANCODE_UNKNOWN;
}

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
	SDL_Keycode key0, key1;
	// sensible defaults
	if (jaxis == 0) {
		key0 = SDLK_LEFT;
		key1 = SDLK_RIGHT;
	} else {
		key0 = SDLK_UP;
		key1 = SDLK_DOWN;
	}
	char *a0 = NULL;
	char *a1 = NULL;
	if (spec) {
		a0 = strsep(&spec, ",");
		a1 = spec;
	}
	if (a0 && *a0)
		key0 = get_key_by_name(a0);
	if (a1 && *a1)
		key1 = get_key_by_name(a1);
	struct axis *axis_data = xmalloc(sizeof(*axis_data));
	axis_data->key0 = key0;
	axis_data->key1 = key1;
	axis_data->value = 32767;
	struct joystick_axis *axis = xmalloc(sizeof(*axis));
	axis->read = (js_read_axis_func)read_axis;
	axis->data = axis_data;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (!enabled_axis[i]) {
			enabled_axis[i] = axis_data;
			break;
		}
	}
	return axis;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	SDL_Keycode key = (jbutton == 0) ? SDLK_LALT : SDLK_UNKNOWN;
	if (spec && *spec)
		key = get_key_by_name(spec);
	struct button *button_data = xmalloc(sizeof(*button_data));
	button_data->key = key;
	button_data->value = 0;
	struct joystick_button *button = xmalloc(sizeof(*button));
	button->read = (js_read_button_func)read_button;
	button->data = button_data;
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (!enabled_button[i]) {
			enabled_button[i] = button_data;
			break;
		}
	}
	return button;
}

static void unmap_axis(struct joystick_axis *axis) {
	if (!axis)
		return;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (axis->data == enabled_axis[i]) {
			enabled_axis[i] = NULL;
		}
	}
	free(axis->data);
	free(axis);
}

static void unmap_button(struct joystick_button *button) {
	if (!button)
		return;
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (button->data == enabled_button[i]) {
			enabled_button[i] = NULL;
		}
	}
	free(button->data);
	free(button);
}
