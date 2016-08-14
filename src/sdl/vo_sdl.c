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

#include "sdl/common.h"

static void *new(void);

struct module vo_sdl_module = {
	.name = "sdl", .description = "Minimal SDL video",
	.new = new,
};

static void vo_sdl_free(struct vo_interface *vo);
static void alloc_colours(struct vo_interface *vo);
static void vsync(struct vo_interface *vo);
static void render_scanline(struct vo_interface *vo, uint8_t const *data, struct ntsc_burst *burst, unsigned phase);
static int set_fullscreen(struct vo_interface *vo, _Bool fullscreen);
static void set_vo_cmp(struct vo_interface *vo, int mode);

typedef Uint8 Pixel;
#define RESET_PALETTE() reset_palette()
#define MAPCOLOUR(r,g,b) alloc_and_map(r, g, b)
#define VIDEO_SCREENBASE ((Pixel *)screen->pixels)
#define XSTEP 1
#define NEXTLINE 0
#define VIDEO_TOPLEFT (VIDEO_SCREENBASE)
#define VIDEO_VIEWPORT_YOFFSET (0)
#define LOCK_SURFACE SDL_LockSurface(screen)
#define UNLOCK_SURFACE SDL_UnlockSurface(screen)
#define VIDEO_MODULE_NAME vo_sdl_module

static SDL_Surface *screen;

static int palette_index = 0;

static void reset_palette(void) {
	palette_index = 0;
}

static Pixel alloc_and_map(int r, int g, int b) {
	SDL_Color c;
	c.r = r;
	c.g = g;
	c.b = b;
	SDL_SetPalette(screen, SDL_LOGPAL|SDL_PHYSPAL, &c, palette_index, 1);
	palette_index++;
	palette_index %= 256;
	return SDL_MapRGB(screen->format, r, g, b);
}

#include "vo_generic_ops.c"

static void *new(void) {
	// XXX
	// New video code assumes 640x240 layout scaled to 4x3.
	// This old directly-rendered code does not support this!
	if (1)
		return NULL;

	struct vo_interface *vo = xmalloc(sizeof(*vo));
	*vo = (struct vo_interface){0};

	vo->free = vo_sdl_free;
	vo->update_palette = alloc_colours;
	vo->vsync = vsync;
	vo->render_scanline = render_scanline;
	vo->set_fullscreen = set_fullscreen;
	vo->set_vo_cmp = set_vo_cmp;

	vo->is_fullscreen = !xroar_ui_cfg.fullscreen;
	if (set_fullscreen(vo, xroar_ui_cfg.fullscreen) != 0) {
		vo_sdl_free(vo);
		return NULL;
	}
	vsync(vo);
	return vo;
}

static void vo_sdl_free(struct vo_interface *vo) {
	set_fullscreen(vo, 0);
	/* Should not be freed by caller: SDL_FreeSurface(screen); */
	free(vo);
}

static int set_fullscreen(struct vo_interface *vo, _Bool fullscreen) {

#ifdef WINDOWS32
	/* Remove menubar if transitioning from windowed to fullscreen. */

	if (screen && !vo_sdl_module.is_fullscreen && fullscreen) {
		sdl_windows32_remove_menu(screen);
	}
#endif

	screen = SDL_SetVideoMode(320, 240, 8, SDL_SWSURFACE|(fullscreen?SDL_FULLSCREEN:0));
	if (screen == NULL) {
		LOG_ERROR("Failed to allocate SDL surface for display\n");
		return 1;
	}

#ifdef WINDOWS32
	sdl_windows32_set_events_window(screen);

	/* Add menubar if transitioning from fullscreen to windowed. */

	if (vo_sdl_module.is_fullscreen && !fullscreen) {
		sdl_windows32_add_menu(screen);

		/* Adding the menubar will resize the *client area*, i.e., the
		 * bit SDL wants to render into. A specified geometry in this
		 * case should apply to the client area, so we need to resize
		 * again to account for this. */

		screen = SDL_SetVideoMode(320, 240, 8, SDL_SWSURFACE);

		/* No need to purge resize events in SDL module - window is not
		 * resizable anyway, so we don't handle them. */
	}
#endif


	SDL_WM_SetCaption("XRoar", "XRoar");

	if (fullscreen)
		SDL_ShowCursor(SDL_DISABLE);
	else
		SDL_ShowCursor(SDL_ENABLE);

	vo->is_fullscreen = fullscreen;

	pixel = VIDEO_TOPLEFT + VIDEO_VIEWPORT_YOFFSET;
	vo->scanline = 0;
	vo->window_x = VDG_ACTIVE_LINE_START - 64;
	vo->window_y = VDG_TOP_BORDER_START + 1;
	vo->window_w = 640;
	vo->window_h = 240;
	sdl_window_x = sdl_window_y = 0;
	sdl_window_w = 320;
	sdl_window_h = 240;

	alloc_colours(vo);

	return 0;
}

static void vsync(struct vo_interface *vo) {
	SDL_UpdateRect(screen, 0, 0, 320, 240);
	pixel = VIDEO_TOPLEFT + VIDEO_VIEWPORT_YOFFSET;
	vo->scanline = 0;
}
