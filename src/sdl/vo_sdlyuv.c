/*

SDL YUV video output module

Copyright 2003-2016 Ciaran Anscomb

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

#include "sdl/common.h"

static void *new(void *cfg);

struct module vo_sdlyuv_module = {
	.name = "sdlyuv", .description = "SDL YUV overlay video",
	.new = new,
};

/*** ***/

static Uint32 map_colour(void *sptr, int r, int g, int b);
static void lock_surface(void *sptr);
static void unlock_surface(void *sptr);

typedef Uint32 Pixel;

struct vo_sdlyuv_interface {
	struct vo_interface public;

	SDL_Surface *screen;
	SDL_Overlay *overlay;
	Uint32 overlay_format;
	unsigned screen_width, screen_height;
	unsigned window_width, window_height;
	SDL_Rect dstrect;
};

#define VO_MODULE_INTERFACE struct vo_sdlyuv_interface
#define MAPCOLOUR(vo,r,g,b) map_colour((vo), (r), (g), (b))
#define XSTEP 1
#define NEXTLINE 0
#define LOCK_SURFACE(vo) lock_surface(vo)
#define UNLOCK_SURFACE(vo) unlock_surface(vo)
#define VIDEO_MODULE_NAME vo_sdlyuv_module

#include "vo_generic_ops.c"

/*** ***/

static void vo_sdlyuv_free(void *sptr);
static void vo_sdlyuv_vsync(void *sptr);
static void resize(void *sptr, unsigned w, unsigned h);
static int set_fullscreen(void *sptr, _Bool fullscreen);

/* The packed modes supported by SDL: */
static const Uint32 try_overlay_format[] = {
	SDL_YUY2_OVERLAY,
	SDL_UYVY_OVERLAY,
	SDL_YVYU_OVERLAY,
};
#define NUM_OVERLAY_FORMATS ((int)(sizeof(try_overlay_format)/sizeof(Uint32)))

static void *new(void *cfg) {
	struct vo_cfg *vo_cfg = cfg;
	const SDL_VideoInfo *video_info;

	struct vo_generic_interface *generic = xmalloc(sizeof(*generic));
	struct vo_sdlyuv_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;
	*generic = (struct vo_generic_interface){0};

	vo->free = DELEGATE_AS0(void, vo_sdlyuv_free, vo);
	vo->update_palette = DELEGATE_AS0(void, alloc_colours, vo);
	vo->vsync = DELEGATE_AS0(void, vo_sdlyuv_vsync, vo);
	vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_scanline, vo);
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, vo);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vo);
	vo->set_vo_cmp = DELEGATE_AS1(void, int, set_vo_cmp, vo);

	video_info = SDL_GetVideoInfo();
	vosdl->screen_width = video_info->current_w;
	vosdl->screen_height = video_info->current_h;
	vosdl->window_width = 640;
	vosdl->window_height = 480;
	vo->is_fullscreen = !vo_cfg->fullscreen;

	if (set_fullscreen(vo, vo_cfg->fullscreen) != 0) {
		vo_sdlyuv_free(vo);
		return NULL;
	}

	vosdl->overlay = NULL;
	Uint32 first_successful_format = 0;
	for (int i = 0; i < NUM_OVERLAY_FORMATS; i++) {
		vosdl->overlay_format = try_overlay_format[i];
		vosdl->overlay = SDL_CreateYUVOverlay(1280, 240, vosdl->overlay_format, vosdl->screen);
		if (!vosdl->overlay) {
			continue;
		}
		if (first_successful_format == 0) {
			first_successful_format = vosdl->overlay_format;
		}
		if (vosdl->overlay->hw_overlay == 1) {
			break;
		}
		SDL_FreeYUVOverlay(vosdl->overlay);
		vosdl->overlay = NULL;
	}
	if (!vosdl->overlay && first_successful_format != 0) {
		/* Fall back to the first successful one, unaccelerated */
		vosdl->overlay_format = first_successful_format;
		vosdl->overlay = SDL_CreateYUVOverlay(1280, 240, vosdl->overlay_format, vosdl->screen);
	}
	if (!vosdl->overlay) {
		LOG_ERROR("Failed to create SDL overlay for display: %s\n", SDL_GetError());
		vo_sdlyuv_free(vo);
		return NULL;
	}
	if (vosdl->overlay->hw_overlay != 1) {
		LOG_WARN("Warning: SDL overlay is not hardware accelerated\n");
	}

	alloc_colours(vo);
	vo->window_x = VDG_ACTIVE_LINE_START - 64;
	vo->window_y = VDG_TOP_BORDER_START + 1;
	vo->window_w = 640;
	vo->window_h = 240;

	vo_sdlyuv_vsync(vo);

	return vo;
}

static void vo_sdlyuv_free(void *sptr) {
	struct vo_sdlyuv_interface *vosdl = sptr;
	set_fullscreen(vosdl, 0);
	SDL_FreeYUVOverlay(vosdl->overlay);
	/* Should not be freed by caller: SDL_FreeSurface(screen); */
	free(vosdl);
}

static Uint32 map_colour(void *sptr, int r, int g, int b) {
	struct vo_sdlyuv_interface *vosdl = sptr;
	Uint32 colour;
	uint8_t *d = (uint8_t *)&colour;
	uint8_t y = 0.299*r + 0.587*g + 0.114*b;
	uint8_t u = (b-y)*0.565 + 128;
	uint8_t v = (r-y)*0.713 + 128;
	switch (vosdl->overlay_format) {
	default:
	case SDL_YUY2_OVERLAY:
		d[0] = d[2] = y;
		d[1] = u;
		d[3] = v;
		break;
	case SDL_UYVY_OVERLAY:
		d[1] = d[3] = y;
		d[0] = u;
		d[2] = v;
		break;
	case SDL_YVYU_OVERLAY:
		d[0] = d[2] = y;
		d[3] = u;
		d[1] = v;
		break;
	}
	return colour;
}

static void lock_surface(void *sptr) {
	struct vo_sdlyuv_interface *vosdl = sptr;
	SDL_LockYUVOverlay(vosdl->overlay);
}

static void unlock_surface(void *sptr) {
	struct vo_sdlyuv_interface *vosdl = sptr;
	SDL_UnlockYUVOverlay(vosdl->overlay);
}

static void resize(void *sptr, unsigned w, unsigned h) {
	struct vo_generic_interface *generic = sptr;
	struct vo_sdlyuv_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;

	vosdl->window_width = w;
	vosdl->window_height = h;
	set_fullscreen(vosdl, vo->is_fullscreen);
}

static int set_fullscreen(void *sptr, _Bool fullscreen) {
	struct vo_generic_interface *generic = sptr;
	struct vo_sdlyuv_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;

	unsigned want_width, want_height;

#ifdef WINDOWS32
	/* Remove menubar if transitioning from windowed to fullscreen. */

	if (vosdl->screen && !vosdl->generic.public.is_fullscreen && fullscreen) {
		sdl_windows32_remove_menu(vosdl->screen);
	}
#endif

	if (fullscreen) {
		want_width = vosdl->screen_width;
		want_height = vosdl->screen_height;
	} else {
		want_width = vosdl->window_width;
		want_height = vosdl->window_height;
	}
	if (want_width < 320) want_width = 320;
	if (want_height < 240) want_height = 240;

	vosdl->screen = SDL_SetVideoMode(want_width, want_height, 0, SDL_HWSURFACE|SDL_ANYFORMAT|(fullscreen?SDL_FULLSCREEN:SDL_RESIZABLE));
	if (vosdl->screen == NULL) {
		LOG_ERROR("Failed to allocate SDL surface for display\n");
		return 1;
	}

#ifdef WINDOWS32
	sdl_windows32_set_events_window(vosdl->screen);

	/* Add menubar if transitioning from fullscreen to windowed. */

	if (vosdl->generic.public.is_fullscreen && !fullscreen) {
		sdl_windows32_add_menu(vosdl->screen);

		/* Adding the menubar will resize the *client area*, i.e., the
		 * bit SDL wants to render into. A specified geometry in this
		 * case should apply to the client area, so we need to resize
		 * again to account for this. */

		vosdl->screen = SDL_SetVideoMode(want_width, want_height, 0, SDL_HWSURFACE|SDL_ANYFORMAT|SDL_RESIZABLE);

		/* Now purge any resize events this all generated from the
		 * event queue. Don't want to end up in a resize loop! */

		SDL_PumpEvents();
		SDL_Event dummy;
		while (SDL_PeepEvents(&dummy, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_VIDEORESIZE)) > 0);
	}
#endif

	SDL_WM_SetCaption("XRoar", "XRoar");

	if (fullscreen)
		SDL_ShowCursor(SDL_DISABLE);
	else
		SDL_ShowCursor(SDL_ENABLE);

	vo->is_fullscreen = fullscreen;

	memcpy(&vosdl->dstrect, &vosdl->screen->clip_rect, sizeof(SDL_Rect));
	if (((float)vosdl->screen->w/(float)vosdl->screen->h)>(4.0/3.0)) {
		vosdl->dstrect.w = (((float)vosdl->screen->h/3.0)*4.0) + 0.5;
		vosdl->dstrect.h = vosdl->screen->h;
		vosdl->dstrect.x = (vosdl->screen->w - vosdl->dstrect.w)/2;
		vosdl->dstrect.y = 0;
	} else {
		vosdl->dstrect.w = vosdl->screen->w;
		vosdl->dstrect.h = (((float)vosdl->screen->w/4.0)*3.0) + 0.5;
		vosdl->dstrect.x = 0;
		vosdl->dstrect.y = (vosdl->screen->h - vosdl->dstrect.h)/2;
	}
	sdl_display.x = vosdl->dstrect.x;
	sdl_display.y = vosdl->dstrect.y;
	sdl_display.w = vosdl->dstrect.w;
	sdl_display.h = vosdl->dstrect.h;

	return 0;
}

static void vo_sdlyuv_vsync(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	struct vo_sdlyuv_interface *vosdl = &generic->module;
	struct vo_interface *vo = &vosdl->public;

	SDL_DisplayYUVOverlay(vosdl->overlay, &vosdl->dstrect);
	generic->pixel = (Pixel *)vosdl->overlay->pixels[0];
	generic_vsync(vo);
}
