/*  Copyright 2003-2017 Ciaran Anscomb
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "xalloc.h"

#include "events.h"
#include "logging.h"
#include "machine.h"
#include "module.h"
#include "sam.h"
#include "ui.h"
#include "vo.h"
#include "xroar.h"

#include "sdl/common.h"

/* Note: prefer the default order for sound and joystick modules, which
 * will include the SDL options. */

static void *ui_sdl_new(void *cfg);
static void ui_sdl_free(void *sptr);
static void ui_sdl_set_state(void *sptr, int tag, int value, const void *data);

struct ui_module ui_sdl_module = {
	.common = { .name = "sdl", .description = "SDL UI",
	            .new = ui_sdl_new,
	},
	.vo_module_list = sdl_vo_module_list,
	.joystick_module_list = sdl_js_modlist,
};

static void *ui_sdl_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;
	(void)ui_cfg;

	if (!SDL_WasInit(SDL_INIT_NOPARACHUTE)) {
		if (SDL_Init(SDL_INIT_NOPARACHUTE) < 0) {
			LOG_ERROR("Failed to initialise SDL: %s\n", SDL_GetError());
			return NULL;
		}
	}

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
		LOG_ERROR("Failed to initialise SDL video: %s\n", SDL_GetError());
		return NULL;
	}

	struct ui_interface *uisdl = xmalloc(sizeof(*uisdl));
	*uisdl = (struct ui_interface){0};

	uisdl->free = DELEGATE_AS0(void, ui_sdl_free, uisdl);
	uisdl->run = DELEGATE_AS0(void, ui_sdl_run, uisdl);
	uisdl->set_state = DELEGATE_AS3(void, int, int, cvoidp, ui_sdl_set_state, uisdl);

	sdl_keyboard_init();

	return uisdl;
}

static void ui_sdl_free(void *sptr) {
	struct ui_module *uisdl = sptr;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	free(uisdl);
}

static void ui_sdl_set_state(void *sptr, int tag, int value, const void *data) {
	(void)sptr;
	(void)data;
	switch (tag) {
	case ui_tag_kbd_translate:
		sdl_keyboard_set_translate(value);
		break;
	default:
		break;
	}
}
