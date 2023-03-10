/** \file
 *
 *  \brief Generic OpenGL support for video output modules.
 *
 *  \copyright Copyright 2012-2023 Ciaran Anscomb
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
 *
 *  OpenGL code is common to several video modules.  All the stuff that's not
 *  toolkit-specific goes in here.
 */

#include "top-config.h"

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

#define TEXTURE_PITCH (1024)
#define TEXTURE_WIDTH (640)

/*** ***/

/* Define stuff required for vo_generic_ops and include it */

typedef uint16_t Pixel;

struct vo_opengl_interface {
	struct vo_interface public;

	Pixel *texture_pixels;
	unsigned window_width, window_height;
	GLuint texnum;
	int vo_opengl_x, vo_opengl_y;
	unsigned vo_opengl_w, vo_opengl_h;
	int filter;

	GLfloat vertices[4][2];
};

#define VO_MODULE_INTERFACE struct vo_opengl_interface
#define MAPCOLOUR(vo,r,g,b) ( (((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | (((b) & 0xf8) >> 3) )
#define XSTEP 1
#define NEXTLINE 0
#define LOCK_SURFACE(vo)
#define UNLOCK_SURFACE(vo)

#include "vo_generic_ops.c"

/*** ***/

static const GLfloat tex_coords[][2] = {
	{ 0.0, 0.0 },
	{ 0.0, 0.9375 },
	{ 0.625, 0.0 },
	{ 0.625, 0.9375 },
};

static void vo_opengl_free(void *sptr);
static void vo_opengl_set_window_size(void *sptr, unsigned w, unsigned h);
static void vo_opengl_refresh(void *sptr);
static void vo_opengl_vsync(void *sptr);

struct vo_interface *vo_opengl_new(struct vo_cfg *vo_cfg) {
	struct vo_generic_interface *generic = xmalloc(sizeof(*generic));
	struct vo_opengl_interface *vogl = &generic->module;
	struct vo_interface *vo = &vogl->public;

	vo_generic_init(generic);

	vo->free = DELEGATE_AS0(void, vo_opengl_free, vo);

	// Used by UI to adjust viewing parameters
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, vo_opengl_set_window_size, vo);
	vo->set_viewport_xy = DELEGATE_AS2(void, unsigned, unsigned, set_viewport_xy, generic);
	vo->set_input = DELEGATE_AS1(void, int, set_input, generic);
	vo->set_cmp_ccr = DELEGATE_AS1(void, int, set_cmp_ccr, generic);
	vo->set_cmp_phase = DELEGATE_AS1(void, int, set_cmp_phase, generic);

	// Used by machine to configure video output
	vo->palette_set_ybr = DELEGATE_AS4(void, uint8, float, float, float, palette_set_ybr, generic);
	vo->palette_set_rgb = DELEGATE_AS4(void, uint8, float, float, float, palette_set_rgb, generic);
	vo->set_cmp_phase_offset = DELEGATE_AS1(void, int, set_cmp_phase_offset, generic);

	// Used by machine to render video
	vo->render_scanline = DELEGATE_AS2(void, uint8cp, ntscburst, render_palette, vo);
	vo->vsync = DELEGATE_AS0(void, vo_opengl_vsync, vo);
	vo->refresh = DELEGATE_AS0(void, vo_opengl_refresh, vo);

	vogl->texture_pixels = xmalloc(TEXTURE_WIDTH * 240 * sizeof(Pixel));
	vogl->window_width = 640;
	vogl->window_height = 480;
	vogl->vo_opengl_x = vogl->vo_opengl_y = 0;
	vogl->filter = vo_cfg->gl_filter;
	generic_vsync(generic);
	generic->pixel = vogl->texture_pixels;
	return vo;
}

void vo_opengl_get_display_rect(struct vo_interface *vo, struct vo_rect *disp) {
	struct vo_opengl_interface *vogl = (struct vo_opengl_interface *)vo;
	disp->x = vogl->vo_opengl_x;
	disp->y = vogl->vo_opengl_y;
	disp->w = vogl->vo_opengl_w;
	disp->h = vogl->vo_opengl_h;
}

static void vo_opengl_free(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	struct vo_opengl_interface *vogl = &generic->module;
	vo_generic_free(generic);
	glDeleteTextures(1, &vogl->texnum);
	free(vogl->texture_pixels);
	free(vogl);
}

static void vo_opengl_set_window_size(void *sptr, unsigned w, unsigned h) {
	struct vo_opengl_interface *vogl = sptr;
	vogl->window_width = w;
	vogl->window_height = h;

	if (((float)vogl->window_width/(float)vogl->window_height)>(4.0/3.0)) {
		vogl->vo_opengl_h = vogl->window_height;
		vogl->vo_opengl_w = (((float)vogl->vo_opengl_h/3.0)*4.0) + 0.5;
		vogl->vo_opengl_x = (vogl->window_width - vogl->vo_opengl_w) / 2;
		vogl->vo_opengl_y = 0;
	} else {
		vogl->vo_opengl_w = vogl->window_width;
		vogl->vo_opengl_h = (((float)vogl->vo_opengl_w/4.0)*3.0) + 0.5;
		vogl->vo_opengl_x = 0;
		vogl->vo_opengl_y = (vogl->window_height - vogl->vo_opengl_h)/2;
	}

	/* Configure OpenGL */
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glViewport(0, 0, vogl->window_width, vogl->window_height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, vogl->window_width, vogl->window_height , 0, -1.0, 1.0);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0f);

	glDeleteTextures(1, &vogl->texnum);
	glGenTextures(1, &vogl->texnum);
	glBindTexture(GL_TEXTURE_2D, vogl->texnum);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5, TEXTURE_PITCH, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	if (vogl->filter == UI_GL_FILTER_NEAREST
	    || (vogl->filter == UI_GL_FILTER_AUTO && (vogl->vo_opengl_w % 320) == 0 && (vogl->vo_opengl_h % 240) == 0)) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	/* Is there a better way of clearing the texture? */
	memset(vogl->texture_pixels, 0, TEXTURE_PITCH * sizeof(Pixel));
	glTexSubImage2D(GL_TEXTURE_2D, 0, TEXTURE_WIDTH,   0,    1, 256,
			GL_RGB, GL_UNSIGNED_SHORT_5_6_5, vogl->texture_pixels);
	glTexSubImage2D(GL_TEXTURE_2D, 0,   0, 240, TEXTURE_PITCH,   1,
			GL_RGB, GL_UNSIGNED_SHORT_5_6_5, vogl->texture_pixels);

	glColor4f(1.0, 1.0, 1.0, 1.0);

	vogl->vertices[0][0] = vogl->vo_opengl_x;
	vogl->vertices[0][1] = vogl->vo_opengl_y;
	vogl->vertices[1][0] = vogl->vo_opengl_x;
	vogl->vertices[1][1] = vogl->window_height - vogl->vo_opengl_y;
	vogl->vertices[2][0] = vogl->window_width - vogl->vo_opengl_x;
	vogl->vertices[2][1] = vogl->vo_opengl_y;
	vogl->vertices[3][0] = vogl->window_width - vogl->vo_opengl_x;
	vogl->vertices[3][1] = vogl->window_height - vogl->vo_opengl_y;

	/* The same vertex & texcoord lists will be used every draw,
	   so configure them here rather than in vsync() */
	glVertexPointer(2, GL_FLOAT, 0, vogl->vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, tex_coords);
}

static void vo_opengl_refresh(void *sptr) {
	struct vo_opengl_interface *vogl = sptr;
	glClear(GL_COLOR_BUFFER_BIT);
	/* Draw main window */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
			TEXTURE_WIDTH, 240, GL_RGB,
			GL_UNSIGNED_SHORT_5_6_5, vogl->texture_pixels);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	/* Video module should now do whatever's required to swap buffers */
}

static void vo_opengl_vsync(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	struct vo_opengl_interface *vogl = &generic->module;
	vo_opengl_refresh(vogl);
	generic->pixel = vogl->texture_pixels;
	generic_vsync(generic);
}
