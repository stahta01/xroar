/** \file
 *
 *  \brief SDL3 user-interface module.
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "slist.h"
#include "xalloc.h"

#include "cart.h"
#include "events.h"
#include "logging.h"
#include "machine.h"
#include "module.h"
#include "ui.h"
#include "vo.h"
#include "wasm/wasm.h"
#include "xroar.h"
#include "sdl3/common.h"

// Initialise SDL video and allocate at least enough space for a struct
// ui_sdl3_interface.
//
// UI modules may use this to derive from the base SDL support and add to it.

struct ui_sdl3_interface *ui_sdl_allocate(size_t usize) {
	// Be sure we've not made more than one of these
	assert(global_uisdl3 == NULL);

	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
		LOG_MOD_ERROR("sdl", "failed to initialise video: %s\n", SDL_GetError());
		return NULL;
	}

	if (usize < sizeof(struct ui_sdl3_interface))
		usize = sizeof(struct ui_sdl3_interface);
	struct ui_sdl3_interface *uisdl3 = xmalloc(usize);

	return uisdl3;
}

// Populate with useful defaults.
//
// After this, it's just up to the caller to also call sdl_vo_init().  Not done
// here, as derived modules may need to set things up beforehand.

void ui_sdl_init(struct ui_sdl3_interface *uisdl3, struct ui_cfg *ui_cfg) {
	uisdl3->cfg = ui_cfg;

	// Defaults - may be overridden by platform-specific versions
	struct ui_interface *ui = &uisdl3->ui_interface;
	ui->free = DELEGATE_AS0(void, ui_sdl_free, uisdl3);
	ui->run = DELEGATE_AS0(void, ui_sdl_run, uisdl3);

	// Make available globally for other SDL3 code
	global_uisdl3 = uisdl3;

	// File requester.  TODO: move this to individual modules so they can
	// refer to their own data.
	struct module *fr_module = module_select_by_arg(default_filereq_module_list, ui_cfg->filereq);
	ui->filereq_interface = module_init(fr_module, "filereq", NULL);
}

void ui_sdl_free(void *sptr) {
	struct ui_sdl3_interface *uisdl3 = sptr;
	struct ui_interface *ui = &uisdl3->ui_interface;

	if (ui->filereq_interface) {
		DELEGATE_SAFE_CALL(ui->filereq_interface->free);
	}
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	global_uisdl3 = NULL;
	free(uisdl3);
}

void ui_sdl_run(void *sptr) {
	struct ui_sdl3_interface *uisdl3 = sptr;
	sdl_js_enable_events();
	for (;;) {
		run_sdl_event_loop(uisdl3);
		xroar_run(EVENT_MS(10));
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// The rest of this file defines the basic SDL UI module that will be used if
// no derived module with more features exists (or if explicitly enabled).

#ifdef WANT_UI_SDL

static void *ui_sdl_new(void *cfg);

struct ui_module ui_sdl_module = {
	.common = { .name = "sdl", .description = "SDL3 UI",
	            .new = ui_sdl_new,
	},
	.joystick_module_list = sdl_js_modlist,
};

static void *ui_sdl_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;

	struct ui_sdl3_interface *uisdl3 = ui_sdl_allocate(sizeof(*uisdl3));
	if (!uisdl3) {
		return NULL;
	}
	*uisdl3 = (struct ui_sdl3_interface){0};
	ui_sdl_init(uisdl3, ui_cfg);
	struct ui_interface *ui = &uisdl3->ui_interface;
	(void)ui;

	if (!sdl_vo_init(uisdl3)) {
		free(uisdl3);
		return NULL;
	}

#ifdef HAVE_X11
	SDL_SetX11EventHook(sdl_x11_event_hook, uisdl3);
#endif

	return uisdl3;
}

#endif
