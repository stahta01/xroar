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
#include <GL/glext.h>
#endif

#include "xalloc.h"

#include "vo.h"
#include "vo_opengl.h"
#include "vo_render.h"
#include "xroar.h"

// TEX_INT_PITCH is the pitch of the texture internally.  This used to be
// best kept as a power of 2 - no idea how necessary that still is, but might
// as well keep it that way.
//
// TEX_BUF_WIDTH is the width of the buffer transferred to the texture.

#define TEX_INT_PITCH (1024)
#define TEX_INT_HEIGHT (256)
#define TEX_BUF_WIDTH (640)
#define TEX_BUF_HEIGHT (240)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void *vo_opengl_new(size_t isize) {
	if (isize < sizeof(struct vo_opengl_interface))
		isize = sizeof(struct vo_opengl_interface);
	struct vo_opengl_interface *vogl = xmalloc(isize);
	*vogl = (struct vo_opengl_interface){0};
	return vogl;
}

void vo_opengl_free(void *sptr) {
	struct vo_opengl_interface *vogl = sptr;
	glDeleteTextures(1, &vogl->texture.num);
	free(vogl->texture.pixels);
}

void vo_opengl_configure(struct vo_opengl_interface *vogl, struct vo_cfg *cfg) {
	struct vo_interface *vo = &vogl->vo;

	vogl->texture.buf_format = GL_RGBA;

	switch (cfg->pixel_fmt) {
	default:
		cfg->pixel_fmt = VO_RENDER_FMT_RGBA8;
		// fall through

	case VO_RENDER_FMT_RGBA8:
		vogl->texture.internal_format = GL_RGB8;
		vogl->texture.buf_format = GL_RGBA;
		vogl->texture.buf_type = GL_UNSIGNED_INT_8_8_8_8;
		vogl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_BGRA8:
		vogl->texture.internal_format = GL_RGB8;
		vogl->texture.buf_format = GL_BGRA;
		vogl->texture.buf_type = GL_UNSIGNED_INT_8_8_8_8;
		vogl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_ARGB8:
		vogl->texture.internal_format = GL_RGB8;
		vogl->texture.buf_format = GL_BGRA;
		vogl->texture.buf_type = GL_UNSIGNED_INT_8_8_8_8_REV;
		vogl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_ABGR8:
		vogl->texture.internal_format = GL_RGB8;
		vogl->texture.buf_format = GL_RGBA;
		vogl->texture.buf_type = GL_UNSIGNED_INT_8_8_8_8_REV;
		vogl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_RGB565:
		vogl->texture.internal_format = GL_RGB565;
		vogl->texture.buf_format = GL_RGB;
		vogl->texture.buf_type = GL_UNSIGNED_SHORT_5_6_5;
		vogl->texture.pixel_size = 2;
		break;

	case VO_RENDER_FMT_RGBA4:
		vogl->texture.internal_format = GL_RGB4;
		vogl->texture.buf_format = GL_RGBA;
		vogl->texture.buf_type = GL_UNSIGNED_SHORT_4_4_4_4;
		vogl->texture.pixel_size = 2;
		break;
	}

	struct vo_render *vr = vo_render_new(cfg->pixel_fmt);
	vr->buffer_pitch = TEX_BUF_WIDTH;
	vo_set_renderer(vo, vr);

	vo->free = DELEGATE_AS0(void, vo_opengl_free, vo);
	vo->draw = DELEGATE_AS0(void, vo_opengl_draw, vogl);

	vogl->texture.pixels = xmalloc(TEX_BUF_WIDTH * TEX_BUF_HEIGHT * vogl->texture.pixel_size);
	vo_render_set_buffer(vr, vogl->texture.pixels);

	vogl->viewport.x = vogl->viewport.y = 0;
	vogl->filter = cfg->gl_filter;
	vo_render_vsync(vo->renderer);
}

void vo_opengl_setup_context(struct vo_opengl_interface *vogl, int w, int h) {
	// Set up viewport
	if (((float)w/(float)h)>(4.0/3.0)) {
		vogl->viewport.h = h;
		vogl->viewport.w = (((float)vogl->viewport.h/3.0)*4.0) + 0.5;
		vogl->viewport.x = (w - vogl->viewport.w) / 2;
		vogl->viewport.y = 0;
	} else {
		vogl->viewport.w = w;
		vogl->viewport.h = (((float)vogl->viewport.w/4.0)*3.0) + 0.5;
		vogl->viewport.x = 0;
		vogl->viewport.y = (h - vogl->viewport.h)/2;
	}

	// Configure OpenGL
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, w, h , 0, -1.0, 1.0);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0f);

	glDeleteTextures(1, &vogl->texture.num);
	glGenTextures(1, &vogl->texture.num);
	glBindTexture(GL_TEXTURE_2D, vogl->texture.num);
	glTexImage2D(GL_TEXTURE_2D, 0, vogl->texture.internal_format, TEX_INT_PITCH, TEX_INT_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	if (vogl->filter == UI_GL_FILTER_NEAREST
	    || (vogl->filter == UI_GL_FILTER_AUTO && (vogl->viewport.w % 320) == 0 && (vogl->viewport.h % 240) == 0)) {
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
	memset(vogl->texture.pixels, 0, nclear * vogl->texture.pixel_size);
	if (TEX_INT_PITCH > TEX_BUF_WIDTH) {
		glTexSubImage2D(GL_TEXTURE_2D, 0,
				TEX_BUF_WIDTH, 0, 1, TEX_INT_HEIGHT,
				vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);
	}
	if (TEX_INT_HEIGHT > TEX_BUF_HEIGHT) {
		glTexSubImage2D(GL_TEXTURE_2D, 0,
				0, TEX_BUF_HEIGHT, TEX_INT_PITCH, 1,
				vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);
	}

	// The same vertex & texcoord lists will be used every draw,
	// so configure them here rather than in vsync()

	// Texture coordinates select a subset of the texture to update
	vogl->tex_coords[0][0] = 0.0;
	vogl->tex_coords[0][1] = 0.0;
	vogl->tex_coords[1][0] = 0.0;
	vogl->tex_coords[1][1] = (double)TEX_BUF_HEIGHT / (double)TEX_INT_HEIGHT;
	vogl->tex_coords[2][0] = (double)TEX_BUF_WIDTH / (double)TEX_INT_PITCH;
	vogl->tex_coords[2][1] = 0.0;
	vogl->tex_coords[3][0] = (double)TEX_BUF_WIDTH / (double)TEX_INT_PITCH;
	vogl->tex_coords[3][1] = (double)TEX_BUF_HEIGHT / (double)TEX_INT_HEIGHT;
	glTexCoordPointer(2, GL_FLOAT, 0, vogl->tex_coords);

	// Vertex array defines where in the window the texture will be rendered
	vogl->vertices[0][0] = vogl->viewport.x;
	vogl->vertices[0][1] = vogl->viewport.y;
	vogl->vertices[1][0] = vogl->viewport.x;
	vogl->vertices[1][1] = h - vogl->viewport.y;
	vogl->vertices[2][0] = w - vogl->viewport.x;
	vogl->vertices[2][1] = vogl->viewport.y;
	vogl->vertices[3][0] = w - vogl->viewport.x;
	vogl->vertices[3][1] = h - vogl->viewport.y;
	glVertexPointer(2, GL_FLOAT, 0, vogl->vertices);
}

void vo_opengl_draw(void *sptr) {
	struct vo_opengl_interface *vogl = sptr;
	glClear(GL_COLOR_BUFFER_BIT);
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, 0, TEX_BUF_WIDTH, TEX_BUF_HEIGHT,
			vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
