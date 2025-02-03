/** \file
 *
 *  \brief SDL2 user-interface common functions.
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

#include <SDL.h>

#include "auto_kbd.h"
#include "events.h"
#include "joystick.h"
#include "logging.h"
#include "vo.h"
#include "xroar.h"

#include "sdl2/common.h"

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_sdl2 and make it available
// globally.
struct ui_sdl2_interface *global_uisdl2 = NULL;

extern inline void sdl_os_handle_syswmevent(struct ui_sdl2_interface *, SDL_SysWMmsg *wmmsg);

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
	.common = { .name = "sdl", .description = "SDL2 joystick input" },
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
	struct ui_sdl2_interface *uisdl2 = userdata;
	(void)uisdl2;

	if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F11) {
		return 0;
	}
	return 1;
}
#endif

void run_sdl_event_loop(struct ui_sdl2_interface *uisdl2) {
	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;
	SDL_Event event;
	while (SDL_PollEvent(&event) == 1) {
		switch(event.type) {
		case SDL_WINDOWEVENT:
			switch(event.window.event) {
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				sdl_vo_notify_size_changed(uisdl2, event.window.data1, event.window.data2);
				break;
			}
			break;
		case SDL_RENDER_DEVICE_RESET:
			sdl_vo_notify_render_device_reset(uisdl2);
			break;
		case SDL_QUIT:
			xroar_quit();
			break;
		case SDL_KEYDOWN:
			sdl_keypress(uisdl2, &event.key.keysym);
			break;
		case SDL_KEYUP:
			sdl_keyrelease(uisdl2, &event.key.keysym);
			break;

		case SDL_JOYDEVICEADDED:
			sdl_js_device_added(event.jdevice.which);
			break;

		case SDL_CONTROLLERDEVICEADDED:
			sdl_js_device_added(event.cdevice.which);
			break;

		case SDL_JOYDEVICEREMOVED:
			sdl_js_device_removed(event.jdevice.which);
			break;

		case SDL_CONTROLLERDEVICEREMOVED:
			sdl_js_device_removed(event.cdevice.which);
			break;

		case SDL_MOUSEMOTION:
			if (uisdl2->mouse_hidden) {
				SDL_ShowCursor(SDL_ENABLE);
				uisdl2->mouse_hidden = 0;
			}
			if (event.motion.windowID == uisdl2->vo_window_id) {
				vo->mouse.axis[0] = event.motion.x;
				vo->mouse.axis[1] = event.motion.y;
			}
			break;
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
			if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == 2) {
				if (SDL_HasClipboardText()) {
					_Bool uc = SDL_GetModState() & KMOD_SHIFT;
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
				vo->mouse.button[event.button.button-1] = event.button.state;
			}
			break;

		case SDL_SYSWMEVENT:
			sdl_os_handle_syswmevent(uisdl2, event.syswm.msg);
			break;

		default:
			break;
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_control *configure_mouse_axis(char *spec, unsigned jaxis) {
	return joystick_configure_mouse_axis(&global_uisdl2->ui_interface, spec, jaxis);
}

static struct joystick_control *configure_mouse_button(char *spec, unsigned jbutton) {
	return joystick_configure_mouse_button(&global_uisdl2->ui_interface, spec, jbutton);
}
