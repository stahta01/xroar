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

#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include "xalloc.h"

#include "logging.h"
#include "vo.h"
#include "vo_opengl.h"
#include "xroar.h"

#include "sdl/common.h"

static void *new(void);

struct module vo_sdlgl_module = {
	.name = "sdlgl", .description = "SDL OpenGL video",
	.new = new,
};

static void vo_sdlgl_free(struct vo_interface *vo);
static void refresh(struct vo_interface *vo);
static void vsync(struct vo_interface *vo);
static void resize(struct vo_interface *vo, unsigned int w, unsigned int h);
static int set_fullscreen(struct vo_interface *vo, _Bool fullscreen);

static SDL_Surface *screen;
static unsigned int screen_width, screen_height;
static unsigned int window_width, window_height;

static void *new(void) {
	const SDL_VideoInfo *video_info;

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE,   5);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,  5);
	SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 16);

	struct vo_interface *vo = xmalloc(sizeof(*vo));
	*vo = (struct vo_interface){0};

	vo->free = vo_sdlgl_free;
	vo->update_palette = vo_opengl_alloc_colours;
	vo->refresh = refresh;
	vo->vsync = vsync;
	vo->render_scanline = vo_opengl_render_scanline;
	vo->resize = resize;
	vo->set_fullscreen = set_fullscreen;
	vo->set_vo_cmp = vo_opengl_set_vo_cmp;

	vo_opengl_init(vo);

	video_info = SDL_GetVideoInfo();
	screen_width = video_info->current_w;
	screen_height = video_info->current_h;
	window_width = 640;
	window_height = 480;
	vo->is_fullscreen = !xroar_ui_cfg.fullscreen;

	if (set_fullscreen(vo, xroar_ui_cfg.fullscreen) != 0) {
		vo_sdlgl_free(vo);
		return NULL;
	}

	vsync(vo);
	return vo;
}

static void vo_sdlgl_free(struct vo_interface *vo) {
	set_fullscreen(vo, 0);
	vo_opengl_shutdown(vo);
	/* Should not be freed by caller: SDL_FreeSurface(screen); */
	free(vo);
}

static void resize(struct vo_interface *vo, unsigned int w, unsigned int h) {
	window_width = w;
	window_height = h;
	set_fullscreen(vo, vo->is_fullscreen);
}

static int set_fullscreen(struct vo_interface *vo, _Bool fullscreen) {
	unsigned int want_width, want_height;

#ifdef WINDOWS32
	/* Remove menubar if transitioning from windowed to fullscreen. */

	if (screen && !vo->is_fullscreen && fullscreen) {
		sdl_windows32_remove_menu(screen);
	}
#endif

	if (fullscreen) {
		want_width = screen_width;
		want_height = screen_height;
	} else {
		want_width = window_width;
		want_height = window_height;
	}
	if (want_width < 320) want_width = 320;
	if (want_height < 240) want_height = 240;

	screen = SDL_SetVideoMode(want_width, want_height, 0, SDL_OPENGL|(fullscreen?SDL_FULLSCREEN:SDL_RESIZABLE));
	if (screen == NULL) {
		LOG_ERROR("Failed to initialise display\n");
		return 1;
	}

#ifdef WINDOWS32
	sdl_windows32_set_events_window(screen);

	/* Add menubar if transitioning from fullscreen to windowed. */

	if (vo->is_fullscreen && !fullscreen) {
		sdl_windows32_add_menu(screen);

		/* Adding the menubar will resize the *client area*, i.e., the
		 * bit SDL wants to render into. A specified geometry in this
		 * case should apply to the client area, so we need to resize
		 * again to account for this. */

		screen = SDL_SetVideoMode(want_width, want_height, 0, SDL_OPENGL|SDL_RESIZABLE);

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

	vo_opengl_set_window_size(vo, want_width, want_height);
	sdl_window_x = vo_opengl_x;
	sdl_window_y = vo_opengl_y;
	sdl_window_w = vo_opengl_w;
	sdl_window_h = vo_opengl_h;

	return 0;
}

static void refresh(struct vo_interface *vo) {
	vo_opengl_refresh(vo);
	SDL_GL_SwapBuffers();
}

static void vsync(struct vo_interface *vo) {
	vo_opengl_vsync(vo);
	SDL_GL_SwapBuffers();
}
