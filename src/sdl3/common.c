/** \file
 *
 *  \brief SDL3 user-interface common functions.
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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <SDL3/SDL.h>

#include "auto_kbd.h"
#include "events.h"
#include "joystick.h"
#include "logging.h"
#include "vo.h"
#include "xroar.h"

#include "sdl3/common.h"

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_sdl3 and make it available
// globally.
struct ui_sdl3_interface *global_uisdl3 = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_control *configure_mouse_axis(char *, unsigned);
static struct joystick_control *configure_mouse_button(char *, unsigned);

static struct joystick_submodule sdl_js_mouse = {
	.name = "mouse",
	.configure_axis = configure_mouse_axis,
	.configure_button = configure_mouse_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// If the SDL UI is active, more joystick interfaces are available

extern struct joystick_submodule hkbd_js_keyboard;

static struct joystick_submodule *js_submodlist[] = {
	&sdl_js_physical,
	&hkbd_js_keyboard,
	&sdl_js_mouse,
	NULL
};

struct joystick_module sdl_js_internal = {
	.common = { .name = "sdl", .description = "SDL3 joystick input" },
	.submodule_list = js_submodlist,
};

struct joystick_module * const sdl_js_modlist[] = {
	&sdl_js_internal,
	NULL
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#ifdef HAVE_WASM
// This currently only filters out certain keypresses from being handled by SDL
// in the WASM build.  It allows the normal browser action to occur for these
// keys.

int filter_sdl_events(void *userdata, SDL_Event *event) {
	struct ui_sdl3_interface *uisdl3 = userdata;
	(void)uisdl3;

	if (event->type == SDL_EVENT_KEY_DOWN && event->key.keysym.sym == SDLK_F11) {
		return 0;
	}
	return 1;
}
#endif

void run_sdl_event_loop(struct ui_sdl3_interface *uisdl3) {
	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;
	SDL_Event event;
	while (SDL_PollEvent(&event) == 1) {
		switch(event.type) {
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			sdl_vo_notify_size_changed(uisdl3, event.window.data1, event.window.data2);
			break;
		case SDL_EVENT_RENDER_DEVICE_RESET:
			sdl_vo_notify_render_device_reset(uisdl3);
			break;
		case SDL_EVENT_QUIT:
			xroar_quit();
			break;
		case SDL_EVENT_KEY_DOWN:
			sdl_keypress(uisdl3, &event.key);
			break;
		case SDL_EVENT_KEY_UP:
			sdl_keyrelease(uisdl3, &event.key);
			break;

		case SDL_EVENT_JOYSTICK_ADDED:
			sdl_js_device_added(event.jdevice.which);
			break;

		case SDL_EVENT_GAMEPAD_ADDED:
			sdl_js_device_added(event.cdevice.which);
			break;

		case SDL_EVENT_JOYSTICK_REMOVED:
			sdl_js_device_removed(event.jdevice.which);
			break;

		case SDL_EVENT_GAMEPAD_REMOVED:
			sdl_js_device_removed(event.cdevice.which);
			break;

		case SDL_EVENT_MOUSE_MOTION:
			if (uisdl3->mouse_hidden) {
				SDL_ShowCursor();
				uisdl3->mouse_hidden = 0;
			}
			if (event.motion.windowID == uisdl3->vo_window_id) {
				vo->mouse.axis[0] = event.motion.x;
				vo->mouse.axis[1] = event.motion.y;
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == 2) {
				if (SDL_HasClipboardText()) {
					bool uc = SDL_GetModState() & SDL_KMOD_SHIFT;
					char *text = SDL_GetClipboardText();
					for (char *p = text; *p; p++) {
						if (*p == '\n')
							*p = '\r';
						if (uc)
							*p = toupper(*p);
					}
					ak_parse_type_string(xroar.auto_kbd, text);
					SDL_free(text);
				}
				break;
			}
			if (event.button.button >= 1 && event.button.button <= 3) {
				vo->mouse.button[event.button.button-1] = event.button.down;
			}
			break;

		default:
			break;
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_control *configure_mouse_axis(char *spec, unsigned jaxis) {
	return joystick_configure_mouse_axis(&global_uisdl3->ui_interface, spec, jaxis);
}

static struct joystick_control *configure_mouse_button(char *spec, unsigned jbutton) {
	return joystick_configure_mouse_button(&global_uisdl3->ui_interface, spec, jbutton);
}
