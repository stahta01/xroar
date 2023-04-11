/** \file
 *
 *  \brief SDL2 video output module.
 *
 *  \copyright Copyright 2015-2023 Ciaran Anscomb
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "array.h"
#include "xalloc.h"

#include "logging.h"
#include "mc6847/mc6847.h"
#include "module.h"
#include "vo.h"
#include "xroar.h"

#include "sdl2/common.h"

#define TEXTURE_WIDTH (640)

static void *new(void *cfg);

struct module vo_sdl_module = {
	.name = "sdl", .description = "SDL2 video",
	.new = new,
};

/*** ***/

typedef uint16_t Pixel;

struct vo_sdl_interface {
	struct vo_interface public;

	SDL_Renderer *renderer;
	SDL_Texture *texture;
	Pixel *texture_pixels;
	int filter;

	int window_w;
	int window_h;

#ifdef WINDOWS32
	_Bool showing_menu;
#endif
};

#define VO_MODULE_INTERFACE struct vo_sdl_interface
#define MAPCOLOUR(vo,r,g,b) ( 0xf000 | (((r) & 0xf0) << 4) | (((g) & 0xf0)) | (((b) & 0xf0) >> 4) )
#define XSTEP 1
#define NEXTLINE 0
#define LOCK_SURFACE(generic)
#define UNLOCK_SURFACE(generic)
#define VIDEO_MODULE_NAME vo_sdl_module

#include "vo_generic_ops.c"

/*** ***/

static const Uint32 renderer_flags[] = {
	SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
	SDL_RENDERER_ACCELERATED,
	SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC,
	SDL_RENDERER_SOFTWARE
};

static void vo_sdl_free(void *sptr);
static void vo_sdl_refresh(void *sptr);
static void vo_sdl_vsync(void *sptr);
static void resize(void *sptr, unsigned int w, unsigned int h);
static int set_fullscreen(void *sptr, _Bool fullscreen);
static void set_menubar(void *sptr, _Bool show_menubar);

static _Bool create_renderer(struct vo_sdl_interface *vosdl);
static void destroy_window(void);
static void destroy_renderer(struct vo_sdl_interface *vosdl);

static void *new(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	struct vo_cfg *vo_cfg = &uisdl2->cfg->vo_cfg;

	struct vo_generic_interface *generic = xmalloc(sizeof(*generic));
	struct vo_sdl_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;

	vo_generic_init(generic);

	vosdl->texture_pixels = xmalloc(TEXTURE_WIDTH * 240 * sizeof(Pixel));
	for (int i = 0; i < TEXTURE_WIDTH * 240; i++)
		vosdl->texture_pixels[i] = MAPCOLOUR(vosdl,0,0,0);

	vosdl->filter = vo_cfg->gl_filter;
	vosdl->window_w = 640;
	vosdl->window_h = 480;

	vo->free = DELEGATE_AS0(void, vo_sdl_free, vo);

	// Used by UI to adjust viewing parameters
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, vo);
	vo->set_active_area = DELEGATE_AS4(void, int, int, int, int, set_active_area, generic);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vo);
	vo->set_menubar = DELEGATE_AS1(void, bool, set_menubar, vo);
	vo->set_input = DELEGATE_AS1(void, int, set_input, generic);
	vo->set_brightness = DELEGATE_AS1(void, int, set_brightness, generic);
	vo->set_contrast = DELEGATE_AS1(void, int, set_contrast, generic);
	vo->set_saturation = DELEGATE_AS1(void, int, set_saturation, generic);
	vo->set_hue = DELEGATE_AS1(void, int, set_hue, generic);
	vo->set_cmp_ccr = DELEGATE_AS1(void, int, set_cmp_ccr, generic);
	vo->set_cmp_phase = DELEGATE_AS1(void, int, set_cmp_phase, generic);

	// Used by machine to configure video output
	vo->palette_set_ybr = DELEGATE_AS4(void, uint8, float, float, float, palette_set_ybr, generic);
	vo->palette_set_rgb = DELEGATE_AS4(void, uint8, float, float, float, palette_set_rgb, generic);
	vo->set_burst = DELEGATE_AS2(void, unsigned, int, set_burst, generic);
	vo->set_cmp_phase_offset = DELEGATE_AS1(void, int, set_cmp_phase_offset, generic);

	// Used by machine to render video
	vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, render_palette, vo);
	vo->vsync = DELEGATE_AS0(void, vo_sdl_vsync, vo);
	vo->refresh = DELEGATE_AS0(void, vo_sdl_refresh, vosdl);

	Uint32 wflags = SDL_WINDOW_RESIZABLE;
	if (vo_cfg->fullscreen) {
		wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	uisdl2->vo_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, wflags);
	SDL_SetWindowMinimumSize(uisdl2->vo_window, 160, 120);
	uisdl2->vo_window_id = SDL_GetWindowID(uisdl2->vo_window);
	vo->show_menubar = 1;
	if (!create_renderer(vosdl)) {
		vo_sdl_free(vo);
		return NULL;
	}

#ifdef WINDOWS32
	// Need an event handler to prevent events backing up while menus are
	// being used.
	sdl_windows32_set_events_window(uisdl2->vo_window);
#endif

	// Initialise keyboard
	sdl_os_keyboard_init(global_uisdl2->vo_window);

	vo_sdl_vsync(vo);

	return vo;
}

static void resize(void *sptr, unsigned int w, unsigned int h) {
	struct vo_sdl_interface *vosdl = sptr;
	(void)w;
	(void)h;
	create_renderer(vosdl);
}

static int set_fullscreen(void *sptr, _Bool fullscreen) {
	struct vo_sdl_interface *vosdl = sptr;

#ifdef HAVE_WASM
	// Until WebAssembly fullscreen interaction becomes a little more
	// predictable, we just don't support it.
	return 0;
#endif

	_Bool is_fullscreen = SDL_GetWindowFlags(global_uisdl2->vo_window) & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP);

	if (is_fullscreen == fullscreen) {
		return 0;
	}

	SDL_SetWindowFullscreen(global_uisdl2->vo_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

	if (!fullscreen) {
		// Testing under Wine, returning from fullscreen doesn't
		// _always_ set it back to the original geometry.  I have no
		// idea why, so force it:
		SDL_SetWindowSize(global_uisdl2->vo_window, vosdl->window_w, vosdl->window_h);
	}

	return 0;
}

static void set_menubar(void *sptr, _Bool show_menubar) {
	struct vo_sdl_interface *vosdl = sptr;
	struct vo_interface *vo = &vosdl->public;

	vo->show_menubar = show_menubar;
#ifdef WINDOWS32
	if (show_menubar && !vosdl->showing_menu) {
		sdl_windows32_add_menu(global_uisdl2->vo_window);
	} else if (!show_menubar && vosdl->showing_menu) {
		sdl_windows32_remove_menu(global_uisdl2->vo_window);
	}
	if (!vo->is_fullscreen) {
		SDL_SetWindowSize(global_uisdl2->vo_window, vosdl->window_w, vosdl->window_h);
	}
#endif
}

static void destroy_window(void) {
	if (global_uisdl2->vo_window) {
		sdl_os_keyboard_free(global_uisdl2->vo_window);
		SDL_DestroyWindow(global_uisdl2->vo_window);
		global_uisdl2->vo_window = NULL;
	}
}

// Whenever the window size changes, we recreate the renderer and texture.

static _Bool create_renderer(struct vo_sdl_interface *vosdl) {
	struct vo_interface *vo = &vosdl->public;

	// XXX 2020-02-23
	//
	// There currently seems to be a bug in the Emscripten GL support,
	// manifesting in SDL2:
	//
	// https://github.com/emscripten-ports/SDL2/issues/92
	//
	// But probably due to a more low-level bug:
	//
	// https://github.com/emscripten-core/emscripten/pull/9803
	//
	// Until this is fixed, we do NOT destroy the renderer in Wasm builds.
	// We do recreate the texture though, as that seems to still work and
	// then the new scale hints are respected.
	//
	// Extra bug points: this doesn't actually seem to fix mousemotion
	// events in Chromium!  Though button presses are getting through.

#ifndef HAVE_WASM
	// Remove old renderer & texture, if they exist
	destroy_renderer(vosdl);
#else
	if (vosdl->texture) {
		SDL_DestroyTexture(vosdl->texture);
		vosdl->texture = NULL;
	}
#endif

	int w, h;
	SDL_GetWindowSize(global_uisdl2->vo_window, &w, &h);

	_Bool is_fullscreen = SDL_GetWindowFlags(global_uisdl2->vo_window) & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP);

	if (is_fullscreen != vo->is_fullscreen) {
		vo->is_fullscreen = is_fullscreen;
		vo->show_menubar = !is_fullscreen;
	}

	_Bool resize_again = 0;

#ifdef WINDOWS32
	// Also take the opportunity to add (windowed) or remove (fullscreen) a
	// menubar under windows.
	if (!vosdl->showing_menu && vo->show_menubar) {
		sdl_windows32_add_menu(global_uisdl2->vo_window);
		vosdl->showing_menu = 1;
		// Adding menubar steals space from client area, so reset size
		// to get that back.
		resize_again = 1;
	} else if (vosdl->showing_menu && !vo->show_menubar) {
		sdl_windows32_remove_menu(global_uisdl2->vo_window);
		vosdl->showing_menu = 0;
	}
#endif

	if (!vo->is_fullscreen) {
		if (w < 160 || h < 120) {
			w = 160;
			h = 120;
			resize_again = 1;
		}
		vosdl->window_w = w;
		vosdl->window_h = h;
	}

	if (resize_again) {
		SDL_SetWindowSize(global_uisdl2->vo_window, w, h);
	}

	// Set scaling method according to options and window dimensions
	if (vosdl->filter == UI_GL_FILTER_NEAREST
	    || (vosdl->filter == UI_GL_FILTER_AUTO && (w % 320 == 0 && h % 240 == 0))) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	} else {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}

#ifdef WINDOWS32
	// from https://github.com/libsdl-org/SDL/issues/5099
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif

#ifdef HAVE_WASM
	// XXX see above
	if (!vosdl->renderer) {
#endif

	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(renderer_flags); i++) {
		vosdl->renderer = SDL_CreateRenderer(global_uisdl2->vo_window, -1, renderer_flags[i]);
		if (vosdl->renderer)
			break;
	}
	if (!vosdl->renderer) {
		LOG_ERROR("Failed to create renderer\n");
		return 0;
	}

#ifdef HAVE_WASM
	}
#endif

	if (logging.level >= 3) {
		SDL_RendererInfo renderer_info;
		if (SDL_GetRendererInfo(vosdl->renderer, &renderer_info) == 0) {
			LOG_PRINT("SDL_GetRendererInfo()\n");
			LOG_PRINT("\tname = %s\n", renderer_info.name);
			LOG_PRINT("\tflags = 0x%x\n", renderer_info.flags);
			if (renderer_info.flags & SDL_RENDERER_SOFTWARE)
				LOG_PRINT("\t\tSDL_RENDERER_SOFTWARE\n");
			if (renderer_info.flags & SDL_RENDERER_ACCELERATED)
				LOG_PRINT("\t\tSDL_RENDERER_ACCELERATED\n");
			if (renderer_info.flags & SDL_RENDERER_PRESENTVSYNC)
				LOG_PRINT("\t\tSDL_RENDERER_PRESENTVSYNC\n");
			if (renderer_info.flags & SDL_RENDERER_TARGETTEXTURE)
				LOG_PRINT("\t\tSDL_RENDERER_TARGETTEXTURE\n");
			for (unsigned i = 0; i < renderer_info.num_texture_formats; i++) {
				LOG_PRINT("\ttexture_formats[%u] = %s\n", i, SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
			}
			LOG_PRINT("\tmax_texture_width = %d\n", renderer_info.max_texture_width);
			LOG_PRINT("\tmax_texture_height = %d\n", renderer_info.max_texture_height);
		}
	}

#ifdef HAVE_WASM
	// XXX see above
	if (!vosdl->texture)
#endif
	vosdl->texture = SDL_CreateTexture(vosdl->renderer, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING, TEXTURE_WIDTH, 240);
	if (!vosdl->texture) {
		LOG_ERROR("Failed to create texture\n");
		destroy_renderer(vosdl);
		return 0;
	}

	SDL_RenderSetLogicalSize(vosdl->renderer, 640, 480);

	SDL_RenderClear(vosdl->renderer);
	SDL_RenderPresent(vosdl->renderer);

	global_uisdl2->display_rect.x = global_uisdl2->display_rect.y = 0;
	global_uisdl2->display_rect.w = w;
	global_uisdl2->display_rect.h = h;

	return 1;
}

static void destroy_renderer(struct vo_sdl_interface *vosdl) {
	if (vosdl->texture) {
		SDL_DestroyTexture(vosdl->texture);
		vosdl->texture = NULL;
	}
	if (vosdl->renderer) {
		SDL_DestroyRenderer(vosdl->renderer);
		vosdl->renderer = NULL;
	}
}

static void vo_sdl_free(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	struct vo_sdl_interface *vosdl = &generic->module;
	vo_generic_free(generic);
	if (vosdl->texture_pixels) {
		free(vosdl->texture_pixels);
		vosdl->texture_pixels = NULL;
	}
	// XXX even though destroy_renderer() is called every time the window
	// resizes with no issues, for some reason calling it here (before or
	// after freeing the texture pixels) causes a SEGV deep down in the
	// video driver.  So just don't.
	//destroy_renderer(vosdl);
	destroy_window();
	free(vosdl);
}

static void vo_sdl_refresh(void *sptr) {
	struct vo_sdl_interface *vosdl = sptr;
	SDL_UpdateTexture(vosdl->texture, NULL, vosdl->texture_pixels, TEXTURE_WIDTH * sizeof(Pixel));
	SDL_RenderClear(vosdl->renderer);
	SDL_RenderCopy(vosdl->renderer, vosdl->texture, NULL, NULL);
	SDL_RenderPresent(vosdl->renderer);
}

static void vo_sdl_vsync(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	struct vo_sdl_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;
	vo_sdl_refresh(vosdl);
	generic->pixel = vosdl->texture_pixels;
	generic_vsync(vo);
}
