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

/* OpenGL code is common to several video modules.  All the stuff that's not
 * toolkit-specific goes in here. */

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if defined(__APPLE_CC__)
# include <OpenGL/gl.h>
#else
# include <GL/gl.h>
#endif

#include "xalloc.h"

#ifdef WINDOWS32
#include "windows32/common_windows32.h"
#include <GL/glext.h>
#endif

#include "mc6847/mc6847.h"
#include "vo.h"
#include "vo_opengl.h"
#include "xroar.h"

/*** ***/

/* Define stuff required for vo_generic_ops and include it */

typedef uint16_t Pixel;
#define MAPCOLOUR(r,g,b) ( (((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | (((b) & 0xf8) >> 3) )
#define XSTEP 1
#define NEXTLINE 0
#define LOCK_SURFACE
#define UNLOCK_SURFACE

#include "vo_generic_ops.c"

/*** ***/

static Pixel *texture_pixels;
static unsigned window_width, window_height;
static GLuint texnum = 0;
static int vo_opengl_x, vo_opengl_y;
static unsigned vo_opengl_w, vo_opengl_h;
static int filter;

static const GLfloat tex_coords[][2] = {
	{ 0.0, 0.0 },
	{ 0.0, 0.9375 },
	{ 0.625, 0.0 },
	{ 0.625, 0.9375 },
};

static GLfloat vertices[][2] = {
	{ 0., 0. },
	{ 0., 0. },
	{ 0., 0. },
	{ 0., 0. }
};

static void vo_opengl_free(void *sptr);
static void vo_opengl_set_window_size(void *sptr, unsigned w, unsigned h);
static void vo_opengl_refresh(void *sptr);
static void vo_opengl_vsync(void *sptr);
static void vo_opengl_set_vo_cmp(void *sptr, int mode);

struct vo_interface *vo_opengl_new(void) {
	struct vo_interface *vo = xmalloc(sizeof(*vo));
	*vo = (struct vo_interface){0};

	vo->free = DELEGATE_AS0(void, vo_opengl_free, vo);
	vo->update_palette = DELEGATE_AS0(void, alloc_colours, vo);
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, vo_opengl_set_window_size, vo);
	vo->vsync = DELEGATE_AS0(void, vo_opengl_vsync, vo);
	vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_scanline, vo);
	vo->refresh = DELEGATE_AS0(void, vo_opengl_refresh, vo);
	vo->set_vo_cmp = DELEGATE_AS1(void, int, vo_opengl_set_vo_cmp, vo);

	texture_pixels = xmalloc(640 * 240 * sizeof(Pixel));
	window_width = 640;
	window_height = 480;
	vo_opengl_x = vo_opengl_y = 0;
	filter = xroar_ui_cfg.gl_filter;
	alloc_colours(vo);
	vo->scanline = 0;
	vo->window_x = VDG_ACTIVE_LINE_START - 64;
	vo->window_y = VDG_TOP_BORDER_START + 1;
	vo->window_w = 640;
	vo->window_h = 240;
	pixel = texture_pixels;
	return vo;
}

void vo_opengl_get_display_rect(struct vo_interface *vo, struct vo_rect *disp) {
	(void)vo;
	disp->x = vo_opengl_x;
	disp->y = vo_opengl_y;
	disp->w = vo_opengl_w;
	disp->h = vo_opengl_h;
}

static void vo_opengl_free(void *sptr) {
	struct vo_interface *vo = sptr;
	glDeleteTextures(1, &texnum);
	free(texture_pixels);
	free(vo);
}

static void vo_opengl_set_window_size(void *sptr, unsigned w, unsigned h) {
	struct vo_interface *vo = sptr;
	(void)vo;
	window_width = w;
	window_height = h;

	if (((float)window_width/(float)window_height)>(4.0/3.0)) {
		vo_opengl_h = window_height;
		vo_opengl_w = (((float)vo_opengl_h/3.0)*4.0) + 0.5;
		vo_opengl_x = (window_width - vo_opengl_w) / 2;
		vo_opengl_y = 0;
	} else {
		vo_opengl_w = window_width;
		vo_opengl_h = (((float)vo_opengl_w/4.0)*3.0) + 0.5;
		vo_opengl_x = 0;
		vo_opengl_y = (window_height - vo_opengl_h)/2;
	}

	/* Configure OpenGL */
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glViewport(0, 0, window_width, window_height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, window_width, window_height , 0, -1.0, 1.0);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0f);

	glDeleteTextures(1, &texnum);
	glGenTextures(1, &texnum);
	glBindTexture(GL_TEXTURE_2D, texnum);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5, 1024, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	if (filter == UI_GL_FILTER_NEAREST
	    || (filter == UI_GL_FILTER_AUTO && (vo_opengl_w % 320) == 0 && (vo_opengl_h % 240) == 0)) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	/* Is there a better way of clearing the texture? */
	memset(texture_pixels, 0, 1024 * sizeof(Pixel));
	glTexSubImage2D(GL_TEXTURE_2D, 0, 640,   0,    1, 256,
			GL_RGB, GL_UNSIGNED_SHORT_5_6_5, texture_pixels);
	glTexSubImage2D(GL_TEXTURE_2D, 0,   0, 240, 1024,   1,
			GL_RGB, GL_UNSIGNED_SHORT_5_6_5, texture_pixels);

	glColor4f(1.0, 1.0, 1.0, 1.0);

	vertices[0][0] = vo_opengl_x;
	vertices[0][1] = vo_opengl_y;
	vertices[1][0] = vo_opengl_x;
	vertices[1][1] = window_height - vo_opengl_y;
	vertices[2][0] = window_width - vo_opengl_x;
	vertices[2][1] = vo_opengl_y;
	vertices[3][0] = window_width - vo_opengl_x;
	vertices[3][1] = window_height - vo_opengl_y;

	/* The same vertex & texcoord lists will be used every draw,
	   so configure them here rather than in vsync() */
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, tex_coords);
}

static void vo_opengl_refresh(void *sptr) {
	struct vo_interface *vo = sptr;
	(void)vo;
	glClear(GL_COLOR_BUFFER_BIT);
	/* Draw main window */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
			640, 240, GL_RGB,
			GL_UNSIGNED_SHORT_5_6_5, texture_pixels);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	/* Video module should now do whatever's required to swap buffers */
}

static void vo_opengl_vsync(void *sptr) {
	struct vo_interface *vo = sptr;
	vo_opengl_refresh(vo);
	pixel = texture_pixels;
	vo->scanline = 0;
}

static void vo_opengl_set_vo_cmp(void *sptr, int mode) {
	struct vo_interface *vo = sptr;
	set_vo_cmp(vo, mode);
}
