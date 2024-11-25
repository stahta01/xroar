/** \file
 *
 *  \brief Generic OpenGL support for video output modules.
 *
 *  \copyright Copyright 2012-2024 Ciaran Anscomb
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
 * OpenGL code may be common to multiple video modules.  Anything not specific
 * to a toolkit goes here.
 */

#ifndef XROAR_VO_OPENGL_H_
#define XROAR_VO_OPENGL_H_

#include <stdint.h>

#if defined(__APPLE_CC__)
# include <OpenGL/gl.h>
#else
# include <GL/gl.h>
#endif

#ifdef WINDOWS32
#include <GL/glext.h>
#endif

#include "vo.h"

// Not a standalone video interface.  Intended for video modules to "subclass".

struct vo_opengl_interface {
	struct vo_interface vo;

	struct {
		// Format OpenGL is asked to make the texture internally
		GLint internal_format;

		// Texture ID
		GLuint num;

		// Format used to transfer data to the texture; ie, the format
		// we allocate memory for and manipulate
		GLenum buf_format;

		// Data type used for those transfers, therefore also
		GLenum buf_type;

		// Size of one pixel, in bytes
		unsigned pixel_size;

		// Pixel buffer
		void *pixels;
	} texture;

	struct vo_viewport viewport;

	_Bool scale_60hz;

	GLuint blit_fbo;
	GLenum blit_filter;
};

// Allocate new opengl interface (potentially with room for extra data)

void *vo_opengl_new(size_t isize);

// Free any allocated structures

void vo_opengl_free(void *sptr);

// Configure parameters.  This finishes setting things up, including creating a
// renderer.  If required functions are not found, returns false.

_Bool vo_opengl_configure(struct vo_opengl_interface *, struct vo_cfg *cfg);

// Set up OpenGL context for rendering

void vo_opengl_setup_context(struct vo_opengl_interface *);

// Change viewport.

void vo_opengl_update_gl_filter(struct vo_opengl_interface *vogl);
void vo_opengl_set_viewport(struct vo_opengl_interface *, int w, int h);
void vo_opengl_set_frame_rate(struct vo_opengl_interface *, _Bool is_60hz);

// Update texture and draw it

void vo_opengl_draw(void *);

#endif
