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
#include "vo_render.h"
#include "xroar.h"

#include "sdl2/common.h"

// TEX_BUF_WIDTH is the width of the buffer transferred to the texture.

#define TEX_BUF_WIDTH (640)

static void *new(void *cfg);

struct module vo_sdl_module = {
	.name = "sdl", .description = "SDL2 video",
	.new = new,
};

struct vo_sdl_interface {
	struct vo_interface public;

	struct {
		// Format SDL is asked to make the texture
		Uint32 format;

		// Texture handle
		SDL_Texture *texture;

		// Size of one pixel, in bytes
		unsigned pixel_size;

		// Pixel buffer
		void *pixels;
	} texture;

	SDL_Renderer *sdl_renderer;
	int filter;

	struct vo_window_area window_area;

#ifdef WINDOWS32
	_Bool showing_menu;
#endif
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static const Uint32 renderer_flags[] = {
	SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
	SDL_RENDERER_ACCELERATED,
	SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC,
	SDL_RENDERER_SOFTWARE
};

static void vo_sdl_free(void *sptr);
static void resize(void *sptr, unsigned int w, unsigned int h);
static void draw(void *sptr);
static int set_fullscreen(void *sptr, _Bool fullscreen);
static void set_menubar(void *sptr, _Bool show_menubar);

static void *new(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	struct vo_cfg *vo_cfg = &uisdl2->cfg->vo_cfg;

	struct vo_sdl_interface *vosdl = vo_interface_new(sizeof(*vosdl));
	*vosdl = (struct vo_sdl_interface){0};
	struct vo_interface *vo = &vosdl->public;

	switch (vo_cfg->pixel_fmt) {
	default:
		vo_cfg->pixel_fmt = VO_RENDER_FMT_RGBA8;
		// fall through

	case VO_RENDER_FMT_RGBA8:
		vosdl->texture.format = SDL_PIXELFORMAT_RGBA8888;
		vosdl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_BGRA8:
		vosdl->texture.format = SDL_PIXELFORMAT_BGRA8888;
		vosdl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_ARGB8:
		vosdl->texture.format = SDL_PIXELFORMAT_ARGB8888;
		vosdl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_ABGR8:
		vosdl->texture.format = SDL_PIXELFORMAT_ABGR8888;
		vosdl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_RGB565:
		vosdl->texture.format = SDL_PIXELFORMAT_RGB565;
		vosdl->texture.pixel_size = 2;
		break;

	case VO_RENDER_FMT_RGBA4:
		vosdl->texture.format = SDL_PIXELFORMAT_RGBA4444;
		vosdl->texture.pixel_size = 2;
		break;
	}

	struct vo_render *vr = vo_render_new(vo_cfg->pixel_fmt);
	vr->buffer_pitch = TEX_BUF_WIDTH;
	vr->cmp.colour_killer = vo_cfg->colour_killer;

	vo_set_renderer(vo, vr);

	vosdl->texture.pixels = xmalloc(TEX_BUF_WIDTH * 240 * vosdl->texture.pixel_size);
	vo_render_set_buffer(vr, vosdl->texture.pixels);
	memset(vosdl->texture.pixels, 0, TEX_BUF_WIDTH * 240 * vosdl->texture.pixel_size);

	vosdl->filter = vo_cfg->gl_filter;
	vosdl->window_area.w = 640;
	vosdl->window_area.h = 480;

	vo->free = DELEGATE_AS0(void, vo_sdl_free, vo);

	// Used by UI to adjust viewing parameters
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, vo);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vo);
	vo->set_menubar = DELEGATE_AS1(void, bool, set_menubar, vo);

	// Used by machine to render video
	vo->draw = DELEGATE_AS0(void, draw, vo);

	Uint32 wflags = SDL_WINDOW_RESIZABLE;
	if (vo_cfg->fullscreen) {
		wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	uisdl2->vo_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, wflags);
	SDL_SetWindowMinimumSize(uisdl2->vo_window, 160, 120);
	uisdl2->vo_window_id = SDL_GetWindowID(uisdl2->vo_window);
	vo->show_menubar = 1;

	// Create renderer
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(renderer_flags); i++) {
		vosdl->sdl_renderer = SDL_CreateRenderer(global_uisdl2->vo_window, -1, renderer_flags[i]);
		if (vosdl->sdl_renderer)
			break;
	}
	if (!vosdl->sdl_renderer) {
		LOG_ERROR("Failed to create renderer\n");
		return 0;
	}

	if (logging.level >= 3) {
		SDL_RendererInfo renderer_info;
		if (SDL_GetRendererInfo(vosdl->sdl_renderer, &renderer_info) == 0) {
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

	// The actual resolution specified here doesn't seem to matter, it's
	// effectively setting an aspect ratio, meaning SDL determines what the
	// Picture Area is for us.
	SDL_RenderSetLogicalSize(vosdl->sdl_renderer, 640, 480);

#ifdef WINDOWS32
	// Need an event handler to prevent events backing up while menus are
	// being used.
	sdl_windows32_set_events_window(uisdl2->vo_window);
#endif

	// Initialise keyboard
	sdl_os_keyboard_init(global_uisdl2->vo_window);

	// Initial resize (dimensions actually ignored and taken from window)
	// creates the texture.
	resize(vosdl, 640, 480);

	return vo;
}

static void resize(void *sptr, unsigned int w_ignored, unsigned int h_ignored) {
	struct vo_sdl_interface *vosdl = sptr;
	struct vo_interface *vo = &vosdl->public;
	(void)w_ignored;
	(void)h_ignored;

	if (vosdl->texture.texture) {
		SDL_DestroyTexture(vosdl->texture.texture);
		vosdl->texture.texture = NULL;
	}

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
		vosdl->window_area.w = w;
		vosdl->window_area.h = h;
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

	vosdl->texture.texture = SDL_CreateTexture(vosdl->sdl_renderer, vosdl->texture.format, SDL_TEXTUREACCESS_STREAMING, TEX_BUF_WIDTH, 240);
	if (!vosdl->texture.texture) {
		LOG_ERROR("Failed to create texture\n");
		abort();
	}

	SDL_RenderClear(vosdl->sdl_renderer);
	SDL_RenderPresent(vosdl->sdl_renderer);

	global_uisdl2->draw_area.x = global_uisdl2->draw_area.y = 0;
	global_uisdl2->draw_area.w = w;
	global_uisdl2->draw_area.h = h;
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
		SDL_SetWindowSize(global_uisdl2->vo_window, vosdl->window_area.w, vosdl->window_area.h);
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
		SDL_SetWindowSize(global_uisdl2->vo_window, vosdl->window_area.w, vosdl->window_area.h);
	}
#endif
}

static void vo_sdl_free(void *sptr) {
	struct vo_sdl_interface *vosdl = sptr;

	if (vosdl->texture.pixels) {
		free(vosdl->texture.pixels);
		vosdl->texture.pixels = NULL;
	}

	// TODO: I used to have a note here that destroying the renderer caused
	// a SEGV deep down in the video driver.  This doesn't seem to happen
	// in my current environment, but I need to test it in others.
	if (vosdl->sdl_renderer) {
		SDL_DestroyRenderer(vosdl->sdl_renderer);
		vosdl->sdl_renderer = NULL;
	}

	if (global_uisdl2->vo_window) {
		sdl_os_keyboard_free(global_uisdl2->vo_window);
		SDL_DestroyWindow(global_uisdl2->vo_window);
		global_uisdl2->vo_window = NULL;
	}

	free(vosdl);
}

static void draw(void *sptr) {
	struct vo_sdl_interface *vosdl = sptr;
	SDL_UpdateTexture(vosdl->texture.texture, NULL, vosdl->texture.pixels, TEX_BUF_WIDTH * vosdl->texture.pixel_size);
	SDL_RenderClear(vosdl->sdl_renderer);
	SDL_RenderCopy(vosdl->sdl_renderer, vosdl->texture.texture, NULL, NULL);
	SDL_RenderPresent(vosdl->sdl_renderer);
}
