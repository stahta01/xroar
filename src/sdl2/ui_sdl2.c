/*

SDL2 user-interface module

Copyright 2015-2019 Ciaran Anscomb

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

#ifdef WINDOWS32
#include <windows.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_WASM
#include <emscripten.h>
#endif

#include <SDL.h>
#include <SDL_syswm.h>

#include "slist.h"
#include "xalloc.h"

#include "cart.h"
#include "events.h"
#include "logging.h"
#include "machine.h"
#include "module.h"
#include "sam.h"
#include "ui.h"
#include "vo.h"
#include "wasm/wasm.h"
#include "xroar.h"
#include "sdl2/common.h"

/* Note: prefer the default order for sound and joystick modules, which
 * will include the SDL options. */

static void *ui_sdl_new(void *cfg);
static void ui_sdl_free(void *sptr);
static void ui_sdl_set_state(void *sptr, int tag, int value, const void *data);

extern struct module vo_sdl_module;
extern struct module vo_null_module;
struct module * const sdl2_vo_module_list[] = {
	&vo_sdl_module,
	&vo_null_module,
	NULL
};

struct ui_module ui_sdl_module = {
	.common = { .name = "sdl", .description = "SDL2 UI",
	            .new = ui_sdl_new,
	},
	.vo_module_list = sdl2_vo_module_list,
	.joystick_module_list = sdl_js_modlist,
};

#ifdef HAVE_WASM
static void sdl2_wasm_update_machine_menu(struct ui_sdl2_interface *uisdl2);
static void sdl2_wasm_update_cartridge_menu(struct ui_sdl2_interface *uisdl2);
#endif

static void *ui_sdl_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;

	// Be sure we've not made more than one of these
	assert(global_uisdl2 == NULL);

#ifdef HAVE_COCOA
	cocoa_register_app();
#endif

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
	uisdl2->cfg = ui_cfg;

	// defaults - may be overridden by platform-specific versions below
	ui->free = DELEGATE_AS0(void, ui_sdl_free, uisdl2);
	ui->run = DELEGATE_AS0(void, ui_sdl_run, uisdl2);
	ui->set_state = DELEGATE_AS3(void, int, int, cvoidp, ui_sdl_set_state, uisdl2);

#ifdef HAVE_X11
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif

#ifdef HAVE_COCOA
	ui->set_state = DELEGATE_AS3(void, int, int, cvoidp, cocoa_ui_set_state, uisdl2);
	ui->update_machine_menu = DELEGATE_AS0(void, cocoa_update_machine_menu, uisdl2);
	ui->update_cartridge_menu = DELEGATE_AS0(void, cocoa_update_cartridge_menu, uisdl2);
	cocoa_update_machine_menu(uisdl2);
	cocoa_update_cartridge_menu(uisdl2);
#endif

#ifdef WINDOWS32
	ui->set_state = DELEGATE_AS3(void, int, int, cvoidp, windows32_ui_set_state, uisdl2);
	windows32_create_menus(uisdl2);
#endif

#ifdef HAVE_WASM
	ui->set_state = DELEGATE_AS3(void, int, int, cvoidp, wasm_ui_set_state, uisdl2);
	ui->run = DELEGATE_AS0(void, wasm_ui_run, uisdl2);
#endif

	// Window geometry sensible defaults
	uisdl2->display_rect.w = 320;
	uisdl2->display_rect.h = 240;

	struct module *vo_mod = (struct module *)module_select_by_arg((struct module * const *)sdl2_vo_module_list, uisdl2->cfg->vo);
	if (!(uisdl2->public.vo_interface = module_init(vo_mod, uisdl2))) {
		return NULL;
	}

	sdl_keyboard_init(uisdl2);

#ifdef HAVE_WASM
	sdl2_wasm_update_machine_menu(uisdl2);
	sdl2_wasm_update_cartridge_menu(uisdl2);
#endif

	return ui;
}

static void ui_sdl_free(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
#ifdef WINDOWS32
	windows32_destroy_menus(uisdl2);
#endif
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#ifdef HAVE_WASM
static void sdl2_wasm_update_machine_menu(struct ui_sdl2_interface *uisdl2) {
	for (struct slist *iter = machine_config_list(); iter; iter = iter->next) {
		struct machine_config *mc = iter->data;
		EM_ASM_({ ui_add_machine($0, $1); }, mc->id, mc->description);
	}
	if (xroar_machine_config) {
		EM_ASM_({ ui_update_machine($0); }, xroar_machine_config->id);
	}
}

static void sdl2_wasm_update_cartridge_menu(struct ui_sdl2_interface *uisdl2) {
	for (struct slist *iter = cart_config_list(); iter; iter = iter->next) {
		struct cart_config *cc = iter->data;
		EM_ASM_({ ui_add_cart($0, $1); }, cc->id, cc->description);
	}
}
#endif
