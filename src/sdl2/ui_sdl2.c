/*

SDL2 user-interface module

Copyright 2015-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#include <assert.h>
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

#include "sdl2/common.h"

/* Note: prefer the default order for sound and joystick modules, which
 * will include the SDL options. */

static void *ui_sdl_new(void *cfg);
static void ui_sdl_free(void *sptr);
static void ui_sdl_set_state(void *sptr, int tag, int value, const void *data);

struct ui_module ui_sdl_module = {
	.common = { .name = "sdl", .description = "SDL2 UI",
	            .new = ui_sdl_new,
	},
	.vo_module_list = sdl_vo_module_list,
	.joystick_module_list = sdl_js_modlist,
};

static void *ui_sdl_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;
	(void)ui_cfg;

	// Be sure we've not made more than one of these
	assert(global_uisdl2 == NULL);

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

	struct ui_sdl2_interface *uisdl2 = xmalloc(sizeof(*uisdl2));
	*uisdl2 = (struct ui_sdl2_interface){0};
	struct ui_interface *ui = &uisdl2->public;
	// Make available globally for other SDL2 code
	global_uisdl2 = uisdl2;

	ui->free = DELEGATE_AS0(void, ui_sdl_free, uisdl2);
	ui->run = DELEGATE_AS0(void, ui_sdl_run, uisdl2);
	ui->set_state = DELEGATE_AS3(void, int, int, cvoidp, ui_sdl_set_state, uisdl2);

#ifdef HAVE_X11
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif

	sdl_keyboard_init(uisdl2);

	// Window geometry sensible defaults
	uisdl2->display_rect.w = 320;
	uisdl2->display_rect.h = 240;

	return ui;
}

static void ui_sdl_free(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	global_uisdl2 = NULL;
	free(uisdl2);
}

static void ui_sdl_set_state(void *sptr, int tag, int value, const void *data) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	(void)data;
	switch (tag) {
	case ui_tag_kbd_translate:
		uisdl2->keyboard.translate = value;
		break;
	default:
		break;
	}
}
