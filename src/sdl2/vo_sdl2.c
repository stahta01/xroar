/** \file
 *
 *  \brief SDL2 video output module.
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "array.h"
#include "xalloc.h"

#include "hkbd.h"
#include "logging.h"
#include "mc6847/mc6847.h"
#include "module.h"
#include "vo.h"
#include "vo_render.h"
#include "xroar.h"

#include "sdl2/common.h"
#ifdef HAVE_X11
#include "x11/hkbd_x11.h"
#endif

// MAX_VIEWPORT_* defines maximum viewport

#define MAX_VIEWPORT_WIDTH  (800)
#define MAX_VIEWPORT_HEIGHT (300)

struct vo_sdl_interface {
	struct vo_interface vo_interface;

	// Messenger client id
	int msgr_client_id;

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

	struct vo_window_area window_area;
	_Bool scale_60hz;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static const Uint32 renderer_flags[] = {
	SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
	SDL_RENDERER_ACCELERATED,
	SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC,
	SDL_RENDERER_SOFTWARE
};

static void vo_sdl_free(void *);
static void set_viewport(void *, int vp_w, int vp_h);
static void draw(void *);
static void resize(void *, unsigned int w, unsigned int h);
static void vosdl_ui_set_gl_filter(void *, int tag, void *smsg);
#ifndef HAVE_WASM
static void vosdl_ui_set_fullscreen(void *, int tag, void *smsg);
#endif
static void vosdl_ui_set_menubar(void *, int tag, void *smsg);

static void notify_frame_rate(void *, _Bool is_60hz);

static void recreate_renderer(struct ui_sdl2_interface *);

_Bool sdl_vo_init(struct ui_sdl2_interface *uisdl2) {
	struct vo_cfg *vo_cfg = &uisdl2->cfg->vo_cfg;

	struct vo_sdl_interface *vosdl = vo_interface_new(sizeof(*vosdl));
	*vosdl = (struct vo_sdl_interface){0};
	struct vo_interface *vo = &vosdl->vo_interface;
	uisdl2->ui_interface.vo_interface = vo;

	vo_interface_init(vo);

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

	vo_set_renderer(vo, vr);

	vosdl->texture.pixels = xmalloc(MAX_VIEWPORT_WIDTH * MAX_VIEWPORT_HEIGHT * vosdl->texture.pixel_size);
	vo_render_set_buffer(vr, vosdl->texture.pixels);
	memset(vosdl->texture.pixels, 0, MAX_VIEWPORT_WIDTH * MAX_VIEWPORT_HEIGHT * vosdl->texture.pixel_size);

	vo->free = DELEGATE_AS0(void, vo_sdl_free, uisdl2);

	vosdl->msgr_client_id = messenger_client_register();

	// Used by UI to adjust viewing parameters
	vo->set_viewport = DELEGATE_AS2(void, int, int, set_viewport, uisdl2);
	ui_messenger_join_group(vosdl->msgr_client_id, ui_tag_gl_filter, MESSENGER_NOTIFY_DELEGATE(vosdl_ui_set_gl_filter, uisdl2));
#ifndef HAVE_WASM
	ui_messenger_join_group(vosdl->msgr_client_id, ui_tag_fullscreen, MESSENGER_NOTIFY_DELEGATE(vosdl_ui_set_fullscreen, uisdl2));
#endif
	ui_messenger_join_group(vosdl->msgr_client_id, ui_tag_menubar, MESSENGER_NOTIFY_DELEGATE(vosdl_ui_set_menubar, uisdl2));

	vr->notify_frame_rate = DELEGATE_AS1(void, bool, notify_frame_rate, uisdl2);

	// Used by machine to render video
	vo->draw = DELEGATE_AS0(void, draw, uisdl2);
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, uisdl2);

	vosdl->window_area.w = 640;
	vosdl->window_area.h = 480;
	uisdl2->viewport.w = 640;
	uisdl2->viewport.h = 240;
	if (vo_cfg->geometry) {
		struct vo_geometry geometry;
		vo_parse_geometry(vo_cfg->geometry, &geometry);
		if (geometry.flags & VO_GEOMETRY_W)
			vosdl->window_area.w = geometry.w;
		if (geometry.flags & VO_GEOMETRY_H)
			vosdl->window_area.h = geometry.h;
		uisdl2->user_specified_geometry = 1;
	}

	// Create window, setting fullscreen hint if appropriate
	Uint32 wflags = SDL_WINDOW_RESIZABLE;
	uisdl2->vo_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, vosdl->window_area.w, vosdl->window_area.h, wflags);
	SDL_SetWindowMinimumSize(uisdl2->vo_window, 160, 120);
	uisdl2->vo_window_id = SDL_GetWindowID(uisdl2->vo_window);

#ifdef HAVE_WASM
	SDL_SetEventFilter(filter_sdl_events, uisdl2);
#endif

	// Add menubar if the created window is not fullscreen
	vo->is_fullscreen = SDL_GetWindowFlags(uisdl2->vo_window) & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP);
	vo->show_menubar = !vo->is_fullscreen;
#ifdef WINDOWS32
	if (vo->show_menubar) {
		sdl_windows32_set_menu_visible(uisdl2, 1);
		SDL_SetWindowSize(uisdl2->vo_window, vosdl->window_area.w, vosdl->window_area.h);
	}
#endif
	{
		int w, h;
		SDL_GetWindowSize(uisdl2->vo_window, &w, &h);
		vo_set_draw_area(vo, 0, 0, w, h);
	}

	// Create renderer

	recreate_renderer(uisdl2);

	if (!vosdl->sdl_renderer) {
		LOG_MOD_SUB_ERROR("sdl", "vo", "failed to create renderer\n");
		return 0;
	}

	if (logging.level >= 3) {
		SDL_RendererInfo renderer_info;
		if (SDL_GetRendererInfo(vosdl->sdl_renderer, &renderer_info) == 0) {
			LOG_MOD_SUB_PRINT("sdl", "vo", "SDL_GetRendererInfo()\n");
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

#ifdef WINDOWS32
	// Need an event handler to prevent events backing up while menus are
	// being used.
	sdl_windows32_set_events_window(uisdl2->vo_window);
#endif

	// Per-OS keyboard initialisation
#if defined(HAVE_X11) && defined(SDL_VIDEO_DRIVER_X11)
	{
		SDL_SysWMinfo sdlinfo;
		SDL_VERSION(&sdlinfo.version);
		SDL_GetWindowWMInfo(uisdl2->vo_window, &sdlinfo);
		if (sdlinfo.subsystem == SDL_SYSWM_X11) {
			Display *display = sdlinfo.info.x11.display;
			hk_x11_set_display(display);
		}
	}
#endif

	// Global keyboard initialisation
	hk_init();

	return 1;
}

static void recreate_renderer(struct ui_sdl2_interface *uisdl2) {
	struct ui_interface *ui = &uisdl2->ui_interface;

	struct vo_interface *vo = ui->vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	if (vosdl->sdl_renderer) {
		SDL_DestroyRenderer(vosdl->sdl_renderer);
		vosdl->sdl_renderer = NULL;
	}

	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(renderer_flags); ++i) {
		vosdl->sdl_renderer = SDL_CreateRenderer(uisdl2->vo_window, -1, renderer_flags[i]);
		if (vosdl->sdl_renderer)
			break;
	}
}

// We need to recreate the texture whenever the viewport changes (it needs to
// be a different size) or the window size changes (texture scaling argument
// may change).

static void recreate_texture(struct ui_sdl2_interface *uisdl2) {
	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	struct vo_render *vr = vo->renderer;

	// Destroy old
	if (vosdl->texture.texture) {
		SDL_DestroyTexture(vosdl->texture.texture);
		vosdl->texture.texture = NULL;
	}

	int vp_w = vr->viewport.w;
	int vp_h = vr->viewport.h;

	// Set scaling method according to options and window dimensions
	if (!vosdl->scale_60hz && (vo->gl_filter == VO_GL_FILTER_NEAREST ||
				   (vo->gl_filter == VO_GL_FILTER_AUTO &&
				    (vosdl->window_area.w % vp_w) == 0 &&
				    (vosdl->window_area.h % vp_h) == 0))) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	} else {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}

	// Create new
	vosdl->texture.texture = SDL_CreateTexture(vosdl->sdl_renderer, vosdl->texture.format, SDL_TEXTUREACCESS_STREAMING, vp_w, vp_h);
	if (!vosdl->texture.texture) {
		LOG_MOD_SUB_ERROR("sdl", "vo", "failed to create texture\n");
		abort();
	}

	vr->buffer_pitch = vr->viewport.w;
}

// Update viewport based on requested dimensions and 60Hz scaling.

static void update_viewport(struct ui_sdl2_interface *uisdl2) {
	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	struct vo_render *vr = vo->renderer;

	int vp_w = uisdl2->viewport.w;
	int vp_h = uisdl2->viewport.h;

	if (vosdl->scale_60hz) {
		vp_h = (vp_h * 5) / 6;
	}

	vo_render_set_viewport(vr, vp_w, vp_h);

	recreate_texture(uisdl2);
}

static void set_viewport(void *sptr, int vp_w, int vp_h) {
	struct ui_sdl2_interface *uisdl2 = sptr;

	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	_Bool is_exact_multiple = 0;
	int multiple = 1;
	int mw = uisdl2->viewport.w;
	int mh = uisdl2->viewport.h * 2;

	if (!vo->is_fullscreen && mw > 0 && mh > 0) {
		if ((vosdl->window_area.w % mw) == 0 &&
		    (vosdl->window_area.h % mh) == 0) {
			int wmul = vosdl->window_area.w / mw;
			int hmul = vosdl->window_area.h / mh;
			if (wmul == hmul && wmul > 0) {
				is_exact_multiple = 1;
				multiple = wmul;
			}
		}
	}

	if (vp_w < 16)
		vp_w = 16;
	if (vp_w > MAX_VIEWPORT_WIDTH)
		vp_w = MAX_VIEWPORT_WIDTH;
	if (vp_h < 6)
		vp_h = 6;
	if (vp_h > MAX_VIEWPORT_HEIGHT)
		vp_h = MAX_VIEWPORT_HEIGHT;

	uisdl2->viewport.w = vp_w;
	uisdl2->viewport.h = vp_h;

	if (is_exact_multiple && !uisdl2->user_specified_geometry) {
		int new_w = multiple * vp_w;
		int new_h = multiple * vp_h * 2;
		SDL_SetWindowSize(uisdl2->vo_window, new_w, new_h);
	}
	update_viewport(uisdl2);
}

static void notify_frame_rate(void *sptr, _Bool is_60hz) {
	struct ui_sdl2_interface *uisdl2 = sptr;

	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	vosdl->scale_60hz = is_60hz;
	update_viewport(uisdl2);
}

void sdl_vo_notify_size_changed(struct ui_sdl2_interface *uisdl2, int w, int h) {
	struct ui_interface *ui = &uisdl2->ui_interface;

	struct vo_interface *vo = ui->vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	vosdl->window_area.w = w;
	vosdl->window_area.h = h;
	update_viewport(uisdl2);

	vo_set_draw_area(vo, 0, 0, w, h);
}

// https://github.com/libsdl-org/SDL/issues/9861
//
// "[...] when you get SDL_EVENT_RENDER_DEVICE_RESET, you should destroy the
// renderer [...] and then reinitialize everything."

void sdl_vo_notify_render_device_reset(struct ui_sdl2_interface *uisdl2) {
	recreate_renderer(uisdl2);
	update_viewport(uisdl2);
}

static void vosdl_ui_set_gl_filter(void *sptr, int tag, void *smsg) {
	(void)tag;
	(void)smsg;
	struct ui_sdl2_interface *uisdl2 = sptr;

	update_viewport(uisdl2);
}

#ifndef HAVE_WASM
static void vosdl_ui_set_fullscreen(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct ui_sdl2_interface *uisdl2 = sptr;
	struct ui_state_message *uimsg = smsg;
	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;

	_Bool want_fullscreen = uimsg->value;
	_Bool is_fullscreen = SDL_GetWindowFlags(uisdl2->vo_window) & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP);

	if (is_fullscreen == want_fullscreen) {
		return;
	}

	if (want_fullscreen && vo->show_menubar) {
#ifdef WINDOWS32
		sdl_windows32_set_menu_visible(uisdl2, 0);
#endif
		vo->show_menubar = 0;
	} else if (!want_fullscreen && !vo->show_menubar) {
#ifdef WINDOWS32
		sdl_windows32_set_menu_visible(uisdl2, 1);
#endif
		vo->show_menubar = 1;
	}

	vo->is_fullscreen = want_fullscreen;
	SDL_SetWindowFullscreen(uisdl2->vo_window, want_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
#endif

static void vosdl_ui_set_menubar(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct ui_sdl2_interface *uisdl2 = sptr;
	struct ui_state_message *uimsg = smsg;
	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	(void)vosdl;

	_Bool want_menubar = uimsg->value;

#ifdef WINDOWS32
	if (want_menubar && !vo->show_menubar) {
		sdl_windows32_set_menu_visible(uisdl2, 1);
	} else if (!want_menubar && vo->show_menubar) {
		sdl_windows32_set_menu_visible(uisdl2, 0);
	}
	if (!vo->is_fullscreen) {
		SDL_SetWindowSize(uisdl2->vo_window, vosdl->window_area.w, vosdl->window_area.h);
	} else {
		int w, h;
		SDL_GetWindowSize(uisdl2->vo_window, &w, &h);
		sdl_vo_notify_size_changed(uisdl2, w, h);
	}
#endif
	vo->show_menubar = want_menubar;
}

static void vo_sdl_free(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;

	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	struct vo_render *vr = vo->renderer;

	messenger_client_unregister(vosdl->msgr_client_id);

	vo_render_free(vr);

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

	if (uisdl2->vo_window) {
		SDL_DestroyWindow(uisdl2->vo_window);
		uisdl2->vo_window = NULL;
	}

	free(vosdl);
}

static void draw(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;

	struct vo_interface *vo = uisdl2->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	struct vo_render *vr = vo->renderer;

	SDL_UpdateTexture(vosdl->texture.texture, NULL, vosdl->texture.pixels, vr->viewport.w * vosdl->texture.pixel_size);
	SDL_RenderClear(vosdl->sdl_renderer);
	SDL_Rect dstrect = {
		.x = vo->picture_area.x, .y = vo->picture_area.y,
		.w = vo->picture_area.w, .h = vo->picture_area.h
	};
	SDL_RenderCopy(vosdl->sdl_renderer, vosdl->texture.texture, NULL, &dstrect);
	SDL_RenderPresent(vosdl->sdl_renderer);
}

static void resize(void *sptr, unsigned int w, unsigned int h) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	SDL_SetWindowSize(uisdl2->vo_window, w, h);
}
