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

#ifdef WINDOWS32
#include "windows32/common_windows32.h"
#include <GL/glext.h>
#endif

#include "xalloc.h"

#include "vo.h"
#include "vo_opengl.h"
#include "vo_render.h"
#include "xroar.h"

// TEX_INT_FMT is the format OpenGL is asked to make the texture internally.
//
// TEX_BUF_FMT is the format used to transfer data to the texture; ie, the
// format we allocate memory for and manipulate.
//
// TEX_BUF_TYPE is the data type used for those transfers, therefore also
// linked to the data we manipulate.
//
// VO_RENDER_FMT is the renderer we request, and should match up to the above.

// These definitions may need to be configurable - probably any modern system
// can use ARGB8, but maybe not?

// Low precision definitions
#define TEX_INT_FMT GL_RGB5
#define TEX_BUF_FMT GL_RGB
#define TEX_BUF_TYPE GL_UNSIGNED_SHORT_5_6_5
#define VO_RENDER_FMT VO_RENDER_RGB565
typedef uint16_t Pixel;

/*
// Higher precision definitions
#define TEX_INT_FMT GL_RGB8
#define TEX_BUF_FMT GL_RGBA
#define TEX_BUF_TYPE GL_UNSIGNED_INT_8_8_8_8
#define VO_RENDER_FMT VO_RENDER_RGBA8
typedef uint32_t Pixel;
*/

// TEX_INT_PITCH is the pitch of the texture internally.  This used to be
// best kept as a power of 2 - no idea how necessary that still is, but might
// as well keep it that way.
//
// TEX_BUF_WIDTH is the width of the buffer transferred to the texture.

#define TEX_INT_PITCH (1024)
#define TEX_INT_HEIGHT (256)
#define TEX_BUF_WIDTH (640)
#define TEX_BUF_HEIGHT (240)

struct vo_opengl_interface {
	struct vo_interface public;

	Pixel *texture_pixels;
	unsigned window_width, window_height;
	GLuint texnum;
	int vo_opengl_x, vo_opengl_y;
	unsigned vo_opengl_w, vo_opengl_h;
	int filter;

	GLfloat vertices[4][2];
	GLfloat tex_coords[4][2];
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void vo_opengl_free(void *sptr);
static void vo_opengl_set_window_size(void *sptr, unsigned w, unsigned h);
static void vo_opengl_refresh(void *sptr);
static void vo_opengl_vsync(void *sptr);

struct vo_interface *vo_opengl_new(struct vo_cfg *vo_cfg) {
	struct vo_opengl_interface *vogl = xmalloc(sizeof(*vogl));
	struct vo_interface *vo = &vogl->public;

	struct vo_render *vr = vo_render_new(VO_RENDER_FMT);
	vr->buffer_pitch = TEX_BUF_WIDTH;
	vo_set_renderer(vo, vr);

	vo->free = DELEGATE_AS0(void, vo_opengl_free, vo);

	// Used by UI to adjust viewing parameters
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, vo_opengl_set_window_size, vo);
	vo->set_active_area = DELEGATE_AS4(void, int, int, int, int, vo_render_set_active_area, vr);

	// Used by machine to render video
	vo->vsync = DELEGATE_AS0(void, vo_opengl_vsync, vo);
	vo->refresh = DELEGATE_AS0(void, vo_opengl_refresh, vo);

	vogl->texture_pixels = xmalloc(TEX_BUF_WIDTH * TEX_BUF_HEIGHT * sizeof(Pixel));
	vr->buffer = vogl->texture_pixels;

	vogl->window_width = 640;
	vogl->window_height = 480;
	vogl->vo_opengl_x = vogl->vo_opengl_y = 0;
	vogl->filter = vo_cfg->gl_filter;
	vo_render_vsync(vo->renderer);
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
	struct vo_opengl_interface *vogl = sptr;
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

	// Configure OpenGL
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
	glTexImage2D(GL_TEXTURE_2D, 0, TEX_INT_FMT, TEX_INT_PITCH, TEX_INT_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
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
	glColor4f(1.0, 1.0, 1.0, 1.0);

	// OpenGL 4.4+ has glClearTexImage(), but for now let's just clear a
	// line just to the right and just below the area in the texture we'll
	// be updating.  This prevents weird fringing effects.
	unsigned nclear = (TEX_INT_PITCH > TEX_INT_HEIGHT) ? TEX_INT_PITCH : TEX_INT_HEIGHT;
	memset(vogl->texture_pixels, 0, nclear * sizeof(Pixel));
	if (TEX_INT_PITCH > TEX_BUF_WIDTH) {
		glTexSubImage2D(GL_TEXTURE_2D, 0,
				TEX_BUF_WIDTH, 0, 1, TEX_INT_HEIGHT,
				TEX_BUF_FMT, TEX_BUF_TYPE, vogl->texture_pixels);
	}
	if (TEX_INT_HEIGHT > TEX_BUF_HEIGHT) {
		glTexSubImage2D(GL_TEXTURE_2D, 0,
				0, TEX_BUF_HEIGHT, TEX_INT_PITCH, 1,
				TEX_BUF_FMT, TEX_BUF_TYPE, vogl->texture_pixels);
	}

	vogl->vertices[0][0] = vogl->vo_opengl_x;
	vogl->vertices[0][1] = vogl->vo_opengl_y;
	vogl->vertices[1][0] = vogl->vo_opengl_x;
	vogl->vertices[1][1] = vogl->window_height - vogl->vo_opengl_y;
	vogl->vertices[2][0] = vogl->window_width - vogl->vo_opengl_x;
	vogl->vertices[2][1] = vogl->vo_opengl_y;
	vogl->vertices[3][0] = vogl->window_width - vogl->vo_opengl_x;
	vogl->vertices[3][1] = vogl->window_height - vogl->vo_opengl_y;

	vogl->tex_coords[0][0] = 0.0;
	vogl->tex_coords[0][1] = 0.0;
	vogl->tex_coords[1][0] = 0.0;
	vogl->tex_coords[1][1] = (double)TEX_BUF_HEIGHT / (double)TEX_INT_HEIGHT;
	vogl->tex_coords[2][0] = (double)TEX_BUF_WIDTH / (double)TEX_INT_PITCH;
	vogl->tex_coords[2][1] = 0.0;
	vogl->tex_coords[3][0] = (double)TEX_BUF_WIDTH / (double)TEX_INT_PITCH;
	vogl->tex_coords[3][1] = (double)TEX_BUF_HEIGHT / (double)TEX_INT_HEIGHT;

	// The same vertex & texcoord lists will be used every draw,
	// so configure them here rather than in vsync()
	glVertexPointer(2, GL_FLOAT, 0, vogl->vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, vogl->tex_coords);
}

static void vo_opengl_refresh(void *sptr) {
	struct vo_opengl_interface *vogl = sptr;
	glClear(GL_COLOR_BUFFER_BIT);
	// Draw main window
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, 0, TEX_BUF_WIDTH, TEX_BUF_HEIGHT,
			TEX_BUF_FMT, TEX_BUF_TYPE, vogl->texture_pixels);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	// Video module should now do whatever's required to swap buffers
}

static void vo_opengl_vsync(void *sptr) {
	struct vo_opengl_interface *vogl = sptr;
	struct vo_interface *vo = &vogl->public;
	vo_opengl_refresh(vogl);
	vo_render_vsync(vo->renderer);
}
