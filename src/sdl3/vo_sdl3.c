/** \file
 *
 *  \brief SDL3 video output module.
 *
 *  \copyright Copyright 2015-2026 Ciaran Anscomb
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

/* TODO
 *
 * This is a naive translation from the SDL2 code, and needs review.
 *
 * There are various workarounds used in SDL2 (e.g. recreating renderers which
 * is needed on some platforms but causes issues for the WASM build) that may
 * not be required at all now.
 *
 * We can detect the Wayland driver and it may be possible to add HKBD support
 * for Wayland.
 *
 * SDL2 code asserted SDL_WINDOW_FULLSCREEN_DESKTOP which doesn't exist in
 * SDL3.  There are other ways to make it happen, but I've not implemented them
 * yet as it may not even be necessary.
 */

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "array.h"
#include "xalloc.h"

#include "hkbd.h"
#include "logging.h"
#include "mc6847/mc6847.h"
#include "module.h"
#include "vo.h"
#include "vo_render.h"
#include "xroar.h"

#include "sdl3/common.h"
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
	bool scale_60hz;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void vo_sdl_free(void *);
static void set_viewport(void *, int vp_w, int vp_h);
static void draw(void *);
static void resize(void *, unsigned int w, unsigned int h);
static void vosdl_ui_set_gl_filter(void *, int tag, void *smsg);
#ifndef HAVE_WASM
static void vosdl_ui_set_vsync(void *, int tag, void *smsg);
static void vosdl_ui_set_fullscreen(void *, int tag, void *smsg);
#endif
static void vosdl_ui_set_menubar(void *, int tag, void *smsg);

static void notify_frame_rate(void *, bool is_60hz);

static void recreate_renderer(struct ui_sdl3_interface *);
static void SDLCALL print_property(void *sptr, SDL_PropertiesID props, const char *name);

bool sdl_vo_init(struct ui_sdl3_interface *uisdl3) {
	struct vo_cfg *vo_cfg = &uisdl3->cfg->vo_cfg;

	struct vo_sdl_interface *vosdl = vo_interface_new(sizeof(*vosdl));
	*vosdl = (struct vo_sdl_interface){0};
	struct vo_interface *vo = &vosdl->vo_interface;
	uisdl3->ui_interface.vo_interface = vo;

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

	vo->free = DELEGATE_AS0(void, vo_sdl_free, uisdl3);

	vosdl->msgr_client_id = messenger_client_register();

	// Used by UI to adjust viewing parameters
	vo->set_viewport = DELEGATE_AS2(void, int, int, set_viewport, uisdl3);
	ui_messenger_join_group(vosdl->msgr_client_id, ui_tag_gl_filter, MESSENGER_NOTIFY_DELEGATE(vosdl_ui_set_gl_filter, uisdl3));
#ifndef HAVE_WASM
	ui_messenger_join_group(vosdl->msgr_client_id, ui_tag_vsync, MESSENGER_NOTIFY_DELEGATE(vosdl_ui_set_vsync, uisdl3));
	ui_messenger_join_group(vosdl->msgr_client_id, ui_tag_fullscreen, MESSENGER_NOTIFY_DELEGATE(vosdl_ui_set_fullscreen, uisdl3));
#endif
	ui_messenger_join_group(vosdl->msgr_client_id, ui_tag_menubar, MESSENGER_NOTIFY_DELEGATE(vosdl_ui_set_menubar, uisdl3));

	vr->notify_frame_rate = DELEGATE_AS1(void, bool, notify_frame_rate, uisdl3);

	// Used by machine to render video
	vo->draw = DELEGATE_AS0(void, draw, uisdl3);
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, uisdl3);

	vosdl->window_area.w = 640;
	vosdl->window_area.h = 480;
	uisdl3->viewport.w = 640;
	uisdl3->viewport.h = 240;
	if (vo_cfg->geometry) {
		struct vo_geometry geometry;
		vo_parse_geometry(vo_cfg->geometry, &geometry);
		if (geometry.flags & VO_GEOMETRY_W)
			vosdl->window_area.w = geometry.w;
		if (geometry.flags & VO_GEOMETRY_H)
			vosdl->window_area.h = geometry.h;
		uisdl3->user_specified_geometry = 1;
	}

	// Create window, setting fullscreen hint if appropriate
	Uint32 wflags = SDL_WINDOW_RESIZABLE;
	uisdl3->vo_window = SDL_CreateWindow("XRoar", vosdl->window_area.w, vosdl->window_area.h, wflags);
	SDL_SetWindowMinimumSize(uisdl3->vo_window, 160, 120);
	uisdl3->vo_window_id = SDL_GetWindowID(uisdl3->vo_window);

#ifdef HAVE_WASM
	SDL_SetEventFilter(filter_sdl_events, uisdl3);
#endif

	// Add menubar if the created window is not fullscreen
	vo->is_fullscreen = SDL_GetWindowFlags(uisdl3->vo_window) & SDL_WINDOW_FULLSCREEN;
	if (vo->is_fullscreen) {
		SDL_DisableScreenSaver();
	} else {
		SDL_EnableScreenSaver();
	}
	vo->show_menubar = !vo->is_fullscreen;
#ifdef WINDOWS32
	if (vo->show_menubar) {
		sdl_windows32_set_menu_visible(uisdl3, 1);
		SDL_SetWindowSize(uisdl3->vo_window, vosdl->window_area.w, vosdl->window_area.h);
	}
#endif
	{
		int w, h;
		SDL_GetWindowSize(uisdl3->vo_window, &w, &h);
		vo_set_draw_area(vo, 0, 0, w, h);
	}

	// Create renderer

	recreate_renderer(uisdl3);

	if (!vosdl->sdl_renderer) {
		LOG_MOD_SUB_ERROR("sdl", "vo", "failed to create renderer\n");
		return 0;
	}

	if (logging.level >= 3) {
		SDL_PropertiesID props = SDL_GetRendererProperties(vosdl->sdl_renderer);
		if (props != 0) {
			LOG_MOD_SUB_PRINT("sdl", "vo", "SDL_GetRendererProperties()\n");
			SDL_EnumerateProperties(props, print_property, "\t");
		}
	}

#ifdef WINDOWS32
	// Need an event handler to prevent events backing up while menus are
	// being used.
	sdl_windows32_set_events_window(uisdl3);
#endif

	// Per-OS keyboard initialisation
#if defined(SDL_PLATFORM_LINUX)
	if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
		Display *display = (Display *)SDL_GetPointerProperty(SDL_GetWindowProperties(uisdl3->vo_window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
		if (display) {
			hk_x11_set_display(display);
		}
	}
#endif

	// Global keyboard initialisation
	hk_init();

	return 1;
}

static void SDLCALL print_property(void *sptr, SDL_PropertiesID props, const char *name) {
	const char *pre = sptr;
	SDL_PropertyType type = SDL_GetPropertyType(props, name);
	switch (type) {
	case SDL_PROPERTY_TYPE_INVALID:
		break;
	case SDL_PROPERTY_TYPE_POINTER:
		LOG_PRINT("%s%s\n", pre, name);
		break;
	case SDL_PROPERTY_TYPE_STRING:
		LOG_PRINT("%s%s=\"%s\"\n", pre, name, SDL_GetStringProperty(props, name, ""));
		break;
	case SDL_PROPERTY_TYPE_NUMBER:
		{
			const char *fmt = "%s%s=" SDL_PRIs64 "\n";
			LOG_PRINT(fmt, pre, name, SDL_GetNumberProperty(props, name, 0));
		}
		break;
	case SDL_PROPERTY_TYPE_FLOAT:
		LOG_PRINT("%s%s=%.2f\n", pre, name, SDL_GetFloatProperty(props, name, 0.0));
		break;
	case SDL_PROPERTY_TYPE_BOOLEAN:
		LOG_PRINT("%s%s=%s\n", pre, name, SDL_GetBooleanProperty(props, name, 0) ? "true" : "false");
		break;
	}
}

static void recreate_renderer(struct ui_sdl3_interface *uisdl3) {
	struct ui_interface *ui = &uisdl3->ui_interface;

	struct vo_interface *vo = ui->vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	if (vosdl->sdl_renderer) {
		SDL_DestroyRenderer(vosdl->sdl_renderer);
		vosdl->sdl_renderer = NULL;
	}

	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, uisdl3->vo_window);
	SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, vo->vsync ? 1 : 0);
	vosdl->sdl_renderer = SDL_CreateRendererWithProperties(props);
	SDL_DestroyProperties(props);
}

// We need to recreate the texture whenever the viewport changes (it needs to
// be a different size) or the window size changes (texture scaling argument
// may change).

static void recreate_texture(struct ui_sdl3_interface *uisdl3) {
	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;
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
		SDL_SetTextureScaleMode(vosdl->texture.texture, SDL_SCALEMODE_NEAREST);
	} else {
		SDL_SetTextureScaleMode(vosdl->texture.texture, SDL_SCALEMODE_LINEAR);
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

static void update_viewport(struct ui_sdl3_interface *uisdl3) {
	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	struct vo_render *vr = vo->renderer;

	int vp_w = uisdl3->viewport.w;
	int vp_h = uisdl3->viewport.h;

	if (vosdl->scale_60hz) {
		vp_h = (vp_h * 5) / 6;
	}

	vo_render_set_viewport(vr, vp_w, vp_h);

	recreate_texture(uisdl3);
}

static void set_viewport(void *sptr, int vp_w, int vp_h) {
	struct ui_sdl3_interface *uisdl3 = sptr;

	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	bool is_exact_multiple = 0;
	int multiple = 1;
	int mw = uisdl3->viewport.w;
	int mh = uisdl3->viewport.h * 2;

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

	uisdl3->viewport.w = vp_w;
	uisdl3->viewport.h = vp_h;

	if (is_exact_multiple && !uisdl3->user_specified_geometry) {
		int new_w = multiple * vp_w;
		int new_h = multiple * vp_h * 2;
		SDL_SetWindowSize(uisdl3->vo_window, new_w, new_h);
	}
	update_viewport(uisdl3);
}

static void notify_frame_rate(void *sptr, bool is_60hz) {
	struct ui_sdl3_interface *uisdl3 = sptr;

	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	vosdl->scale_60hz = is_60hz;
	update_viewport(uisdl3);
}

void sdl_vo_notify_size_changed(struct ui_sdl3_interface *uisdl3, int w, int h) {
	struct ui_interface *ui = &uisdl3->ui_interface;

	struct vo_interface *vo = ui->vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	vosdl->window_area.w = w;
	vosdl->window_area.h = h;
	update_viewport(uisdl3);

	vo_set_draw_area(vo, 0, 0, w, h);
}

// https://github.com/libsdl-org/SDL/issues/9861
//
// "[...] when you get SDL_EVENT_RENDER_DEVICE_RESET, you should destroy the
// renderer [...] and then reinitialize everything."

void sdl_vo_notify_render_device_reset(struct ui_sdl3_interface *uisdl3) {
	recreate_renderer(uisdl3);
	update_viewport(uisdl3);
}

static void vosdl_ui_set_gl_filter(void *sptr, int tag, void *smsg) {
	(void)tag;
	(void)smsg;
	struct ui_sdl3_interface *uisdl3 = sptr;

	update_viewport(uisdl3);
}

#ifndef HAVE_WASM
static void vosdl_ui_set_vsync(void *sptr, int tag, void *smsg) {
	(void)tag;
	(void)smsg;
	struct ui_sdl3_interface *uisdl3 = sptr;

	sdl_vo_notify_render_device_reset(uisdl3);
}

static void vosdl_ui_set_fullscreen(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct ui_sdl3_interface *uisdl3 = sptr;
	struct ui_state_message *uimsg = smsg;
	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;

	bool want_fullscreen = uimsg->value;
	bool is_fullscreen = SDL_GetWindowFlags(uisdl3->vo_window) & SDL_WINDOW_FULLSCREEN;

	if (is_fullscreen == want_fullscreen) {
		return;
	}

	if (want_fullscreen && vo->show_menubar) {
#ifdef WINDOWS32
		sdl_windows32_set_menu_visible(uisdl3, 0);
#endif
		vo->show_menubar = 0;
	} else if (!want_fullscreen && !vo->show_menubar) {
#ifdef WINDOWS32
		sdl_windows32_set_menu_visible(uisdl3, 1);
#endif
		vo->show_menubar = 1;
	}

	vo->is_fullscreen = want_fullscreen;
	SDL_SetWindowFullscreen(uisdl3->vo_window, want_fullscreen ? SDL_WINDOW_FULLSCREEN: 0);
	if (want_fullscreen) {
		SDL_DisableScreenSaver();
	} else {
		SDL_EnableScreenSaver();
	}
}
#endif

static void vosdl_ui_set_menubar(void *sptr, int tag, void *smsg) {
	(void)tag;
	struct ui_sdl3_interface *uisdl3 = sptr;
	struct ui_state_message *uimsg = smsg;
	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	(void)vosdl;

	bool want_menubar = uimsg->value;

#ifdef WINDOWS32
	if (want_menubar && !vo->show_menubar) {
		sdl_windows32_set_menu_visible(uisdl3, 1);
	} else if (!want_menubar && vo->show_menubar) {
		sdl_windows32_set_menu_visible(uisdl3, 0);
	}
	if (!vo->is_fullscreen) {
		SDL_SetWindowSize(uisdl3->vo_window, vosdl->window_area.w, vosdl->window_area.h);
	} else {
		int w, h;
		SDL_GetWindowSize(uisdl3->vo_window, &w, &h);
		sdl_vo_notify_size_changed(uisdl3, w, h);
	}
#endif
	vo->show_menubar = want_menubar;
}

static void vo_sdl_free(void *sptr) {
	struct ui_sdl3_interface *uisdl3 = sptr;

	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	struct vo_render *vr = vo->renderer;

	messenger_client_unregister(vosdl->msgr_client_id);

	vo_render_free(vr);

	free(vosdl->texture.pixels);
	vosdl->texture.pixels = NULL;

	// TODO: I used to have a note here that destroying the renderer caused
	// a SEGV deep down in the video driver.  This doesn't seem to happen
	// in my current environment, but I need to test it in others.
	if (vosdl->sdl_renderer) {
		SDL_DestroyRenderer(vosdl->sdl_renderer);
		vosdl->sdl_renderer = NULL;
	}

	if (uisdl3->vo_window) {
		SDL_DestroyWindow(uisdl3->vo_window);
		uisdl3->vo_window = NULL;
	}

	free(vosdl);
}

static void draw(void *sptr) {
	struct ui_sdl3_interface *uisdl3 = sptr;

	struct vo_interface *vo = uisdl3->ui_interface.vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;
	struct vo_render *vr = vo->renderer;

	SDL_UpdateTexture(vosdl->texture.texture, NULL, vosdl->texture.pixels, vr->viewport.w * vosdl->texture.pixel_size);
	SDL_RenderClear(vosdl->sdl_renderer);
	SDL_FRect dstrect = {
		.x = vo->picture_area.x, .y = vo->picture_area.y,
		.w = vo->picture_area.w, .h = vo->picture_area.h
	};
	SDL_RenderTexture(vosdl->sdl_renderer, vosdl->texture.texture, NULL, &dstrect);
	SDL_RenderPresent(vosdl->sdl_renderer);
}

static void resize(void *sptr, unsigned int w, unsigned int h) {
	struct ui_sdl3_interface *uisdl3 = sptr;
	SDL_SetWindowSize(uisdl3->vo_window, w, h);
}
