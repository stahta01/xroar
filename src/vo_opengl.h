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

	void *texture_pixels;
	GLuint texnum;

	struct {
		int w, h;
	} draw_area;

	struct {
		int x, y;
		int w, h;
	} viewport;

	int filter;

	GLfloat vertices[4][2];
	GLfloat tex_coords[4][2];
};

// Allocate new opengl interface (potentially with room for extra data)

void *vo_opengl_new(size_t isize);

// Free any allocated structures

void vo_opengl_free(void *sptr);

// Configure parameters.  This finishes setting things up, including creating a
// renderer.

void vo_opengl_configure(struct vo_opengl_interface *, struct vo_cfg *cfg);

// Set up OpenGL context for rendering
//
//     int w, h;  // dimensions of window to draw into

void vo_opengl_setup_context(struct vo_opengl_interface *, int w, int h);

// Update texture and draw it

void vo_opengl_draw(void *);

#endif
