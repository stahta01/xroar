/*

SDL2 video output module

Copyright 2015-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "xalloc.h"

#include "logging.h"
#include "mc6847/mc6847.h"
#include "module.h"
#include "vo.h"
#include "xroar.h"

#include "sdl2/common.h"

#ifdef WANT_SIMULATED_NTSC
#define TEXTURE_WIDTH (640)
#else
#define TEXTURE_WIDTH (320)
#endif

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

static void vo_sdl_free(void *sptr);
static void vo_sdl_vsync(void *sptr);
static void resize(void *sptr, unsigned int w, unsigned int h);
static int set_fullscreen(void *sptr, _Bool fullscreen);

static int create_renderer(struct vo_sdl_interface *vosdl);
static void destroy_window(void);
static void destroy_renderer(struct vo_sdl_interface *vosdl);

static void *new(void *cfg) {
	struct vo_cfg *vo_cfg = cfg;
	struct vo_generic_interface *generic = xmalloc(sizeof(*generic));
	struct vo_sdl_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;
	*generic = (struct vo_generic_interface){0};

	vosdl->texture_pixels = xmalloc(TEXTURE_WIDTH * 240 * sizeof(Pixel));
	for (int i = 0; i < TEXTURE_WIDTH * 240; i++)
		vosdl->texture_pixels[i] = MAPCOLOUR(vosdl,0,0,0);

	vosdl->filter = vo_cfg->gl_filter;
	vosdl->window_w = 640;
	vosdl->window_h = 480;

	vo->free = DELEGATE_AS0(void, vo_sdl_free, vo);
	vo->update_palette = DELEGATE_AS0(void, alloc_colours, vo);
	vo->vsync = DELEGATE_AS0(void, vo_sdl_vsync, vo);
	vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_scanline, vo);
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, vo);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vo);
	vo->set_vo_cmp = DELEGATE_AS1(void, int, set_vo_cmp, vo);

	vo->is_fullscreen = !vo_cfg->fullscreen;
	if (set_fullscreen(vo, vo_cfg->fullscreen) != 0) {
		vo_sdl_free(vo);
		return NULL;
	}

	alloc_colours(vo);
	vo->window_x = VDG_ACTIVE_LINE_START - 64;
	vo->window_y = VDG_TOP_BORDER_START + 1;
	vo->window_w = 640;
	vo->window_h = 240;

	vo_sdl_vsync(vo);

	return vo;
}

static void resize(void *sptr, unsigned int w, unsigned int h) {
	struct vo_generic_interface *generic = sptr;
	struct vo_sdl_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;
	if (vo->is_fullscreen)
		return;
	vosdl->window_w = w;
	vosdl->window_h = h;
	create_renderer(vosdl);
}

static int set_fullscreen(void *sptr, _Bool fullscreen) {
	struct vo_generic_interface *generic = sptr;
	struct vo_sdl_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;
	int err;

#ifdef WINDOWS32
	/* Remove menubar if transitioning from windowed to fullscreen. */

	if (global_uisdl2->vo_window && !vo->is_fullscreen && fullscreen) {
		sdl_windows32_remove_menu(global_uisdl2->vo_window);
	}
#endif

	destroy_renderer(vosdl);
	destroy_window();

	if (fullscreen) {
		global_uisdl2->vo_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
	} else {
		global_uisdl2->vo_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, vosdl->window_w, vosdl->window_h, SDL_WINDOW_RESIZABLE);
	}
	if (!global_uisdl2->vo_window) {
		LOG_ERROR("Failed to create window\n");
		return -1;
	}
	global_uisdl2->vo_window_id = SDL_GetWindowID(global_uisdl2->vo_window);
	if (!fullscreen) {
		SDL_SetWindowMinimumSize(global_uisdl2->vo_window, 160, 120);
	}

#ifdef WINDOWS32
	sdl_windows32_set_events_window(global_uisdl2->vo_window);

	/* Add menubar if transitioning from fullscreen to windowed. */

	if (vo->is_fullscreen && !fullscreen) {
		sdl_windows32_add_menu(global_uisdl2->vo_window);

		/* Adding the menubar will resize the *client area*, i.e., the
		 * bit SDL wants to render into. A specified geometry in this
		 * case should apply to the client area, so we need to resize
		 * again to account for this. */

		SDL_SetWindowSize(global_uisdl2->vo_window, vosdl->window_w, vosdl->window_h);

		/* Now purge any resize events this all generated from the
		 * event queue. Don't want to end up in a resize loop! */

		SDL_FlushEvent(SDL_WINDOWEVENT);
	}
#endif

	if ((err = create_renderer(vosdl)) != 0) {
		destroy_window();
		return err;
	}

	if (fullscreen)
		SDL_ShowCursor(SDL_DISABLE);
	else
		SDL_ShowCursor(SDL_ENABLE);

	vo->is_fullscreen = fullscreen;
	global_uisdl2->display_rect.x = global_uisdl2->display_rect.y = 0;

	/* Initialise keyboard */
	sdl_os_keyboard_init(global_uisdl2->vo_window);

	/* Clear out any keydown events queued for the new window */
	SDL_PumpEvents();
	SDL_FlushEvent(SDL_KEYDOWN);

	return 0;
}

/* In Windows, the renderer and textures need recreating quite frequently */

static void destroy_window(void) {
	if (global_uisdl2->vo_window) {
		sdl_os_keyboard_free(global_uisdl2->vo_window);
		SDL_DestroyWindow(global_uisdl2->vo_window);
		global_uisdl2->vo_window = NULL;
	}
}

static int create_renderer(struct vo_sdl_interface *vosdl) {
	destroy_renderer(vosdl);
	int w, h;
	SDL_GetWindowSize(global_uisdl2->vo_window, &w, &h);
	if (vosdl->filter == UI_GL_FILTER_NEAREST
	    || (vosdl->filter == UI_GL_FILTER_AUTO && (w % 320 == 0 && h % 240 == 0))) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	} else {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}

	vosdl->renderer = SDL_CreateRenderer(global_uisdl2->vo_window, -1, SDL_RENDERER_PRESENTVSYNC);
	if (!vosdl->renderer) {
		LOG_ERROR("Failed to create renderer\n");
		return -1;
	}

	if (log_level >= 3) {
		SDL_RendererInfo renderer_info;
		if (SDL_GetRendererInfo(vosdl->renderer, &renderer_info) == 0) {
			LOG_PRINT("SDL_GetRendererInfo()\n");
			LOG_PRINT("\tname = %s\n", renderer_info.name);
			LOG_PRINT("\tflags = 0x%x\n", renderer_info.flags);
			for (unsigned i = 0; i < renderer_info.num_texture_formats; i++) {
				LOG_PRINT("\ttexture_formats[%u] = %s\n", i, SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
			}
			LOG_PRINT("\tmax_texture_width = %d\n", renderer_info.max_texture_width);
			LOG_PRINT("\tmax_texture_height = %d\n", renderer_info.max_texture_height);
		}
	}

	vosdl->texture = SDL_CreateTexture(vosdl->renderer, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING, TEXTURE_WIDTH, 240);
	if (!vosdl->texture) {
		LOG_ERROR("Failed to create texture\n");
		destroy_renderer(vosdl);
		return -1;
	}

	SDL_RenderSetLogicalSize(vosdl->renderer, 640, 480);

	SDL_RenderClear(vosdl->renderer);
	SDL_RenderPresent(vosdl->renderer);

	global_uisdl2->display_rect.w = vosdl->window_w;
	global_uisdl2->display_rect.h = vosdl->window_h;

	return 0;
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
	struct vo_sdl_interface *vosdl = sptr;
	if (vosdl->texture_pixels) {
		free(vosdl->texture_pixels);
		vosdl->texture_pixels = NULL;
	}
	destroy_renderer(vosdl);
	destroy_window();
	free(vosdl);
}

static void vo_sdl_vsync(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	struct vo_sdl_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;
	SDL_UpdateTexture(vosdl->texture, NULL, vosdl->texture_pixels, TEXTURE_WIDTH * sizeof(Pixel));
	SDL_RenderClear(vosdl->renderer);
	SDL_RenderCopy(vosdl->renderer, vosdl->texture, NULL, NULL);
	SDL_RenderPresent(vosdl->renderer);
	generic->pixel = vosdl->texture_pixels;
	generic_vsync(vo);
}
