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
#include "mc6847/mc6847.h"
#include "vo.h"
#include "xroar.h"

#include "sdl2/common.h"

static void *new(void);

struct module vo_sdl_module = {
	.name = "sdl", .description = "SDL2 video",
	.new = new,
};

/*** ***/

typedef uint16_t Pixel;
#define MAPCOLOUR(vo,r,g,b) ( 0xf000 | (((r) & 0xf0) << 4) | (((g) & 0xf0)) | (((b) & 0xf0) >> 4) )
#define XSTEP 1
#define NEXTLINE 0
#define LOCK_SURFACE(vo)
#define UNLOCK_SURFACE(vo)
#define VIDEO_MODULE_NAME vo_sdl_module

#include "vo_generic_ops.c"

/*** ***/

struct vo_sdl_interface {
	struct vo_generic_interface generic;

	SDL_Renderer *renderer;
	SDL_Texture *texture;
	uint16_t *texture_pixels;

	int window_w;
	int window_h;
};

static void vo_sdl_free(void *sptr);
static void vo_sdl_vsync(void *sptr);
static void resize(void *sptr, unsigned int w, unsigned int h);
static int set_fullscreen(void *sptr, _Bool fullscreen);

static int create_renderer(struct vo_sdl_interface *vosdl);
static void destroy_window(void);
static void destroy_renderer(struct vo_sdl_interface *vosdl);

static void *new(void) {
	struct vo_sdl_interface *vosdl = xmalloc(sizeof(*vosdl));
	*vosdl = (struct vo_sdl_interface){0};
	struct vo_generic_interface *generic = &vosdl->generic;
	struct vo_interface *vo = &generic->public;

	vosdl->texture_pixels = xmalloc(640 * 240 * sizeof(Pixel));
	for (int i = 0; i < 640 * 240; i++)
		vosdl->texture_pixels[i] = MAPCOLOUR(vosdl,0,0,0);
	vosdl->window_w = 640;
	vosdl->window_h = 480;

	vo->free = DELEGATE_AS0(void, vo_sdl_free, vo);
	vo->update_palette = DELEGATE_AS0(void, alloc_colours, vo);
	vo->vsync = DELEGATE_AS0(void, vo_sdl_vsync, vo);
	vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_scanline, vo);
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, vo);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vo);
	vo->set_vo_cmp = DELEGATE_AS1(void, int, set_vo_cmp, vo);

	vosdl->generic.public.is_fullscreen = !xroar_ui_cfg.fullscreen;
	if (set_fullscreen(vo, xroar_ui_cfg.fullscreen) != 0) {
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
	struct vo_sdl_interface *vosdl = sptr;
	if (vosdl->generic.public.is_fullscreen)
		return;
	vosdl->window_w = w;
	vosdl->window_h = h;
	create_renderer(vosdl);
}

static int set_fullscreen(void *sptr, _Bool fullscreen) {
	struct vo_sdl_interface *vosdl = sptr;
	int err;

#ifdef WINDOWS32
	/* Remove menubar if transitioning from windowed to fullscreen. */

	if (sdl_window && !vosdl->generic.public.is_fullscreen && fullscreen) {
		sdl_windows32_remove_menu(sdl_window);
	}
#endif

	destroy_renderer(vosdl);
	destroy_window();

	if (fullscreen) {
		sdl_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
	} else {
		sdl_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, vosdl->window_w, vosdl->window_h, SDL_WINDOW_RESIZABLE);
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

	if (vosdl->generic.public.is_fullscreen && !fullscreen) {
		sdl_windows32_add_menu(sdl_window);

		/* Adding the menubar will resize the *client area*, i.e., the
		 * bit SDL wants to render into. A specified geometry in this
		 * case should apply to the client area, so we need to resize
		 * again to account for this. */

		SDL_SetWindowSize(sdl_window, vosdl->window_w, vosdl->window_h);

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

	vosdl->generic.public.is_fullscreen = fullscreen;
	sdl_display.x = sdl_display.y = 0;

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

static int create_renderer(struct vo_sdl_interface *vosdl) {
	destroy_renderer(vosdl);
	int w, h;
	SDL_GetWindowSize(sdl_window, &w, &h);
	if (w % 320 == 0 && h % 240 == 0) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	} else {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}

	vosdl->renderer = SDL_CreateRenderer(sdl_window, -1, 0);
	if (!vosdl->renderer) {
		LOG_ERROR("Failed to create renderer\n");
		return -1;
	}

	vosdl->texture = SDL_CreateTexture(vosdl->renderer, SDL_PIXELFORMAT_ARGB4444, SDL_TEXTUREACCESS_STREAMING, 640, 240);
	if (!vosdl->texture) {
		LOG_ERROR("Failed to create texture\n");
		destroy_renderer(vosdl);
		return -1;
	}

	SDL_RenderSetLogicalSize(vosdl->renderer, 640, 480);

	SDL_RenderClear(vosdl->renderer);
	SDL_RenderPresent(vosdl->renderer);

	sdl_display.w = vosdl->window_w;
	sdl_display.h = vosdl->window_h;

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
	struct vo_sdl_interface *vosdl = sptr;
	SDL_UpdateTexture(vosdl->texture, NULL, vosdl->texture_pixels, 640 * sizeof(Pixel));
	SDL_RenderClear(vosdl->renderer);
	SDL_RenderCopy(vosdl->renderer, vosdl->texture, NULL, NULL);
	SDL_RenderPresent(vosdl->renderer);
	vosdl->generic.pixel = vosdl->texture_pixels;
	vosdl->generic.public.scanline = 0;
}
