/*  Copyright 2003-2015 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

// For strsep()
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "pl-string.h"
#include "xalloc.h"

#include "events.h"
#include "joystick.h"
#include "logging.h"
#include "machine.h"
#include "mc6847.h"
#include "module.h"
#include "xroar.h"

#include "sdl2/common.h"

unsigned sdl_window_x = 0, sdl_window_y = 0;
unsigned sdl_window_w = 320, sdl_window_h = 240;

VideoModule * const sdl_video_module_list[] = {
	&video_sdl_module,
	NULL
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);

static struct joystick_interface sdl_js_if_mouse = {
	.name = "mouse",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
};

static float mouse_xoffset = 34.0;
static float mouse_yoffset = 25.5;
static float mouse_xdiv = 252.;
static float mouse_ydiv = 189.;

static unsigned mouse_axis[2] = { 0, 0 };
static _Bool mouse_button[3] = { 0, 0, 0 };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void sdl_js_shutdown(void);

// If the SDL UI is active, more joystick interfaces are available

static struct joystick_interface *js_iflist[] = {
	&sdl_js_if_physical,
	&sdl_js_if_keyboard,
	&sdl_js_if_mouse,
	NULL
};

struct joystick_module sdl_js_internal = {
	.common = { .name = "sdl", .description = "SDL2 joystick input",
	            .shutdown = sdl_js_shutdown },
	.intf_list = js_iflist,
};

struct joystick_module * const sdl_js_modlist[] = {
	&sdl_js_internal,
	NULL
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_js_shutdown(void) {
	sdl_js_physical_shutdown();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_run(void) {
	while (xroar_run()) {
		SDL_Event event;
		while (SDL_PollEvent(&event) == 1) {
			switch(event.type) {
			case SDL_WINDOWEVENT:
				switch(event.window.event) {
				case SDL_WINDOWEVENT_RESIZED:
					if (video_module->resize) {
						video_module->resize(event.window.data1, event.window.data2);
					}
					break;
				}
				break;
			case SDL_QUIT:
				xroar_quit();
				break;
			case SDL_KEYDOWN:
				sdl_keypress(&event.key.keysym);
				break;
			case SDL_KEYUP:
				sdl_keyrelease(&event.key.keysym);
				break;
			case SDL_MOUSEMOTION:
				if (event.motion.windowID == sdl_windowID) {
					float x = ((float)event.motion.x - mouse_xoffset) / mouse_xdiv;
					float y = ((float)event.motion.y - mouse_yoffset) / mouse_ydiv;
					if (x < 0.0) x = 0.0;
					if (x > 1.0) x = 1.0;
					if (y < 0.0) y = 0.0;
					if (y > 1.0) y = 1.0;
					mouse_axis[0] = x * 255.;
					mouse_axis[1] = y * 255.;
				}
				break;
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEBUTTONDOWN:
				if (event.button.button >= 1 && event.button.button <= 3) {
					mouse_button[event.button.button-1] = event.button.state;
				}
				break;
#ifdef WINDOWS32
			case SDL_SYSWMEVENT:
				sdl_windows32_handle_syswmevent(event.syswm.msg);
				break;
#endif
			default:
				break;
			}
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static unsigned read_axis(unsigned *a) {
	return *a;
}

static _Bool read_button(_Bool *b) {
	return *b;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
	jaxis %= 2;
	float off0 = (jaxis == 0) ? 2.0 : 1.5;
	float off1 = (jaxis == 0) ? 254.0 : 190.5;
	char *tmp = NULL;
	if (spec)
		tmp = strsep(&spec, ",");
	if (tmp && *tmp)
		off0 = strtof(tmp, NULL);
	if (spec && *spec)
		off1 = strtof(spec, NULL);
	if (jaxis == 0) {
		if (off0 < -32.0) off0 = -32.0;
		if (off1 > 288.0) off0 = 288.0;
		mouse_xoffset = off0 + 32.0;
		mouse_xdiv = off1 - off0;
	} else {
		if (off0 < -24.0) off0 = -24.0;
		if (off1 > 216.0) off0 = 216.0;
		mouse_yoffset = off0 + 24.0;
		mouse_ydiv = off1 - off0;
	}
	struct joystick_axis *axis = xmalloc(sizeof(*axis));
	axis->read = (js_read_axis_func)read_axis;
	axis->data = &mouse_axis[jaxis];
	return axis;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	jbutton %= 3;
	if (spec && *spec)
		jbutton = strtol(spec, NULL, 0) - 1;
	if (jbutton >= 3)
		return NULL;
	struct joystick_button *button = xmalloc(sizeof(*button));
	button->read = (js_read_button_func)read_button;
	button->data = &mouse_button[jbutton];
	return button;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_zoom_in(void) {
	int xscale = sdl_window_w / 160;
	int yscale = sdl_window_h / 120;
	int scale;
	if (xscale < yscale)
		scale = yscale;
	else if (xscale > yscale)
		scale = xscale;
	else
		scale = xscale + 1;
	if (scale < 1)
		scale = 1;
	SDL_SetWindowSize(sdl_window, 160*scale, 120*scale);
	if (video_module->resize) {
		video_module->resize(160*scale, 120*scale);
	}
}

void sdl_zoom_out(void) {
	int xscale = sdl_window_w / 160;
	int yscale = sdl_window_h / 120;
	int scale;
	if (xscale < yscale)
		scale = xscale;
	else if (xscale > yscale)
		scale = yscale;
	else
		scale = xscale - 1;
	if (scale < 1)
		scale = 1;
	SDL_SetWindowSize(sdl_window, 160*scale, 120*scale);
	if (video_module->resize) {
		video_module->resize(160*scale, 120*scale);
	}
}
