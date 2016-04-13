/*  Copyright 2003-2016 Ciaran Anscomb
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "xalloc.h"

#include "logging.h"
#include "mc6847.h"
#include "vo.h"
#include "xroar.h"

#include "sdl2/common.h"

static _Bool init(void);
static void shutdown(void);
static void alloc_colours(void);
static void vsync(void);
static void render_scanline(uint8_t const *scanline_data);
static void resize(unsigned int w, unsigned int h);
static int set_fullscreen(_Bool fullscreen);
static void update_cross_colour_phase(void);

struct vo_module vo_sdl_module = {
	.common = { .name = "sdl", .description = "SDL2 video",
	            .init = init, .shutdown = shutdown },
	.update_palette = alloc_colours,
	.vsync = vsync,
	.render_scanline = render_scanline,
	.resize = resize,
	.set_fullscreen = set_fullscreen,
	.update_cross_colour_phase = update_cross_colour_phase,
};

typedef uint16_t Pixel;
#define RESET_PALETTE()
#define MAPCOLOUR(r,g,b) ( 0xf000 | (((r) & 0xf0) << 4) | (((g) & 0xf0)) | (((b) & 0xf0) >> 4) )
#define VIDEO_SCREENBASE (pixels)
#define XSTEP 1
#define NEXTLINE 0
#define VIDEO_TOPLEFT (VIDEO_SCREENBASE)
#define VIDEO_VIEWPORT_YOFFSET (0)
#define LOCK_SURFACE
#define UNLOCK_SURFACE
#define VIDEO_MODULE_NAME vo_sdl_module

SDL_Window *sdl_window = NULL;
Uint32 sdl_windowID = 0;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static uint16_t *pixels = NULL;

#include "vo_generic_ops.c"

static int create_renderer(void);
static void destroy_window(void);
static void destroy_renderer(void);

static _Bool init(void) {
	pixels = xmalloc(320 * 240 * sizeof(Pixel));
	for (int i = 0; i < 320 * 240; i++)
		pixels[i] = MAPCOLOUR(0,0,0);

	vo_sdl_module.is_fullscreen = !xroar_ui_cfg.fullscreen;
	if (set_fullscreen(xroar_ui_cfg.fullscreen) != 0) {
		return 0;
	}

	alloc_colours();
	vo_sdl_module.window_x = VDG_ACTIVE_LINE_START - 32;
	vo_sdl_module.window_y = VDG_TOP_BORDER_START + 1;
	vo_sdl_module.window_w = 320;
	vo_sdl_module.window_h = 240;

	vsync();

	return 1;
}

static int window_w = 640;
static int window_h = 480;

static void resize(unsigned int w, unsigned int h) {
	if (vo_sdl_module.is_fullscreen)
		return;
	window_w = w;
	window_h = h;
	create_renderer();
}

static int set_fullscreen(_Bool fullscreen) {
	int err;

#ifdef WINDOWS32
	/* Remove menubar if transitioning from windowed to fullscreen. */

	if (sdl_window && !vo_sdl_module.is_fullscreen && fullscreen) {
		sdl_windows32_remove_menu(sdl_window);
	}
#endif

	destroy_renderer();
	destroy_window();

	if (fullscreen) {
		sdl_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
	} else {
		sdl_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_w, window_h, SDL_WINDOW_RESIZABLE);
	}
	if (!sdl_window) {
		LOG_ERROR("Failed to create window\n");
		return -1;
	}
	sdl_windowID = SDL_GetWindowID(sdl_window);
	if (!fullscreen) {
		SDL_SetWindowMinimumSize(sdl_window, 160, 120);
	}

#ifdef WINDOWS32
	sdl_windows32_set_events_window(sdl_window);

	/* Add menubar if transitioning from fullscreen to windowed. */

	if (vo_sdl_module.is_fullscreen && !fullscreen) {
		sdl_windows32_add_menu(sdl_window);

		/* Adding the menubar will resize the *client area*, i.e., the
		 * bit SDL wants to render into. A specified geometry in this
		 * case should apply to the client area, so we need to resize
		 * again to account for this. */

		SDL_SetWindowSize(sdl_window, window_w, window_h);

		/* Now purge any resize events this all generated from the
		 * event queue. Don't want to end up in a resize loop! */

		SDL_FlushEvent(SDL_WINDOWEVENT);
	}
#endif

	if ((err = create_renderer()) != 0) {
		destroy_window();
		return err;
	}

	if (fullscreen)
		SDL_ShowCursor(SDL_DISABLE);
	else
		SDL_ShowCursor(SDL_ENABLE);

	vo_sdl_module.is_fullscreen = fullscreen;
	sdl_window_x = sdl_window_y = 0;

	/* Initialise keyboard */
	sdl_os_keyboard_init(sdl_window);

	/* Clear out any keydown events queued for the new window */
	SDL_PumpEvents();
	SDL_FlushEvent(SDL_KEYDOWN);

	return 0;
}

/* In Windows, the renderer and textures need recreating quite frequently */

static void destroy_window(void) {
	if (sdl_window) {
		sdl_os_keyboard_free(sdl_window);
		SDL_DestroyWindow(sdl_window);
		sdl_window = NULL;
	}
}

static int create_renderer(void) {
	destroy_renderer();
	int w, h;
	SDL_GetWindowSize(sdl_window, &w, &h);
	if (w % 320 == 0 && h % 240 == 0) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	} else {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}

	renderer = SDL_CreateRenderer(sdl_window, -1, 0);
	if (!renderer) {
		LOG_ERROR("Failed to create renderer\n");
		return -1;
	}

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING, 320, 240);
	if (!texture) {
		LOG_ERROR("Failed to create texture\n");
		destroy_renderer();
		return -1;
	}

	SDL_RenderSetLogicalSize(renderer, 320, 240);

	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	sdl_window_w = window_w;
	sdl_window_h = window_h;

	return 0;
}

static void destroy_renderer(void) {
	if (texture) {
		SDL_DestroyTexture(texture);
		texture = NULL;
	}
	if (renderer) {
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}
}

static void shutdown(void) {
	if (pixels) {
		free(pixels);
		pixels = NULL;
	}
	destroy_renderer();
	destroy_window();
}

static void vsync(void) {
	SDL_UpdateTexture(texture, NULL, pixels, 320 * sizeof(Pixel));
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
	pixel = VIDEO_TOPLEFT + VIDEO_VIEWPORT_YOFFSET;
	vo_sdl_module.scanline = 0;
}
