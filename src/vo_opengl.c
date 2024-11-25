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
 *  OpenGL code is common to several video modules.  All the stuff that's not
 *  toolkit-specific goes in here.
 *
 *  This code now uses OpenGL 3+ Framebuffer Objects (FBO), which simplifies
 *  things a lot, but may make it harder to run on old machines.
 */

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

// Defining GL_GLEXT_PROTOTYPES isn't the recommended way of getting access to
// glGenFramebuffers() etc., but it's maybe[*] safe under Linux, which is the
// only place this code has been tested anyway.
//
// [*] Debian at least is installing libopengl, a "Vendor neutral GL dispatch
// library", which to me sounds like we shouldn't have to care about this, at
// least under Linux/Unix.
//
// The "correct" way is to use arch-specific functions to get function
// addresses by name, potentially having to dlopen() an external library and
// retry on failure.
//
// And that isn't even all of it.  You end up getting valid-looking function
// pointers back even if they aren't supported!  The _real_ "correct" way is to
// do string parsing first and make sure the extension you're looking for is
// contained in the result of some query.  Mad.
//
// You kinda understand why anything-but-OpenGL took off, really...

#define GL_GLEXT_PROTOTYPES

// Despite the note above about only testing under Linux, I'll leave this
// macosx+ conditional here for those that want to mess about on that platform.
// For reasons best known to themselves, Apple put OpenGL headers in a
// completely non-standard directory.

#if defined(__APPLE_CC__)
# include <OpenGL/gl.h>
#else
# include <GL/gl.h>
#endif

// Rants over for now.

#include "xalloc.h"

#include "vo.h"
#include "vo_opengl.h"
#include "vo_render.h"
#include "xroar.h"

// MAX_VIEWPORT_* defines maximum viewport

#define MAX_VIEWPORT_WIDTH  (800)
#define MAX_VIEWPORT_HEIGHT (300)

// TEX_INT_PITCH is the pitch of the texture internally.  This used to be
// best kept as a power of 2 - no idea how necessary that still is, but might
// as well keep it that way.

#define TEX_INT_PITCH (1024)
#define TEX_INT_HEIGHT (384)

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
	struct vo_interface *vo = &vogl->vo;
	struct vo_render *vr = vo->renderer;
	vo_render_free(vr);
	glDeleteTextures(1, &vogl->texture.num);
	glDeleteFramebuffers(1, &vogl->blit_fbo);
	free(vogl->texture.pixels);
	free(vogl);
}

_Bool vo_opengl_configure(struct vo_opengl_interface *vogl, struct vo_cfg *cfg) {
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
#ifdef GL_RGB565
		vogl->texture.internal_format = GL_RGB565;
#else
		vogl->texture.internal_format = GL_RGB5;
#endif
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
	vo_set_renderer(vo, vr);

	vo->free = DELEGATE_AS0(void, vo_opengl_free, vo);
	vo->draw = DELEGATE_AS0(void, vo_opengl_draw, vogl);

	vogl->texture.pixels = xmalloc(MAX_VIEWPORT_WIDTH * MAX_VIEWPORT_HEIGHT * vogl->texture.pixel_size);
	memset(vogl->texture.pixels, 0, MAX_VIEWPORT_WIDTH * MAX_VIEWPORT_HEIGHT * vogl->texture.pixel_size);
	vo_render_set_buffer(vr, vogl->texture.pixels);

	vo->picture_area.x = vo->picture_area.y = 0;
	vogl->viewport.w = 640;
	vogl->viewport.h = 240;

	return 1;
}

static void update_viewport(struct vo_opengl_interface *vogl) {
	struct vo_interface *vo = &vogl->vo;
	struct vo_render *vr = vo->renderer;

	int vp_w = vogl->viewport.w;
	int vp_h = vogl->viewport.h;

	if (vogl->scale_60hz) {
		vp_h = (vp_h * 5) / 6;
	}

	vo_render_set_viewport(vr, vp_w, vp_h);

	int hw = vp_w / 2;
	int hh = vp_h;

	glDeleteTextures(1, &vogl->texture.num);
	glGenTextures(1, &vogl->texture.num);
	glBindTexture(GL_TEXTURE_2D, vogl->texture.num);
	glTexImage2D(GL_TEXTURE_2D, 0, vogl->texture.internal_format, TEX_INT_PITCH, TEX_INT_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glDeleteFramebuffers(1, &vogl->blit_fbo);
	glGenFramebuffers(1, &vogl->blit_fbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, vogl->blit_fbo);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, vogl->texture.num, 0);

	// Set scaling method according to options and window dimensions
	if (!vogl->scale_60hz && (vo->gl_filter == VO_GL_FILTER_NEAREST ||
				  (vo->gl_filter == VO_GL_FILTER_AUTO &&
				   (vo->picture_area.w % hw) == 0 &&
				   (vo->picture_area.h % hh) == 0))) {
		vogl->blit_filter = GL_NEAREST;
	} else {
		vogl->blit_filter = GL_LINEAR;
	}

	// We still need to clear to the right and underneath the bit of the
	// texture we'll use, else GL_LINEAR will interpolate against junk.
	memset(vogl->texture.pixels, 0, TEX_INT_PITCH * vogl->texture.pixel_size);
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			vp_w, 0, 1, TEX_INT_HEIGHT,
			vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, vp_h, TEX_INT_PITCH, 1,
			vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);

	vr->buffer_pitch = vp_w;
}

void vo_opengl_update_gl_filter(struct vo_opengl_interface *vogl) {
	update_viewport(vogl);
}

void vo_opengl_set_viewport(struct vo_opengl_interface *vogl, int vp_w, int vp_h) {
	vogl->viewport.w = vp_w;
	vogl->viewport.h = vp_h;
	update_viewport(vogl);
}

void vo_opengl_set_frame_rate(struct vo_opengl_interface *vogl, _Bool is_60hz) {
	vogl->scale_60hz = is_60hz;
	update_viewport(vogl);
}

void vo_opengl_setup_context(struct vo_opengl_interface *vogl) {
	// Create textures, etc.
	update_viewport(vogl);
}

void vo_opengl_draw(void *sptr) {
	struct vo_opengl_interface *vogl = sptr;
	struct vo_interface *vo = &vogl->vo;
	struct vo_render *vr = vogl->vo.renderer;

	glClear(GL_COLOR_BUFFER_BIT);

	glBindTexture(GL_TEXTURE_2D, vogl->texture.num);
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, 0, vr->viewport.w, vr->viewport.h,
			vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, vogl->blit_fbo);
	glBlitFramebuffer(0, vr->viewport.h, vr->viewport.w, 0,
			  vo->picture_area.x, vo->picture_area.y,
			  vo->picture_area.w + vo->picture_area.x,
			  vo->picture_area.h + vo->picture_area.y,
			  GL_COLOR_BUFFER_BIT, vogl->blit_filter);
}
