/** \file
 *
 *  \brief GtkGLExt video output module.
 *
 *  \copyright Copyright 2010-2019 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop
#ifdef HAVE_X11
#include <gdk/gdkx.h>
#include <GL/glx.h>
#endif
#include <gtk/gtkgl.h>

#include "xalloc.h"

#include "logging.h"
#include "module.h"
#include "vo.h"
#include "vo_opengl.h"
#include "xroar.h"

#include "gtk2/common.h"

static void *new(void *cfg);

struct module vo_gtkgl_module = {
	.name = "gtkgl", .description = "GtkGLExt video",
	.new = new,
};

// - - -

struct vo_gtkgl_interface {
	struct vo_interface public;

	struct vo_interface *vogl;  // OpenGL generic interface
};

// - - -

static void vo_gtkgl_free(void *sptr);
static void refresh(void *sptr);
static void vsync(void *sptr);
static void resize(void *sptr, unsigned int w, unsigned int h);
static int set_fullscreen(void *sptr, _Bool fullscreen);
static void vo_gtkgl_set_vo_cmp(void *sptr, int mode);

static gboolean window_state(GtkWidget *, GdkEventWindowState *, gpointer);
static gboolean configure(GtkWidget *, GdkEventConfigure *, gpointer);
static void vo_gtkgl_set_vsync(int val);

static void *new(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	struct vo_cfg *vo_cfg = &uigtk2->cfg->vo_cfg;

	gtk_gl_init(NULL, NULL);

	if (gdk_gl_query_extension() != TRUE) {
		LOG_ERROR("OpenGL not available\n");
		return NULL;
	}

	struct vo_interface *vogl = vo_opengl_new(vo_cfg);
	if (!vogl) {
		LOG_ERROR("Failed to create OpenGL context\n");
		return NULL;
	}

	struct vo_gtkgl_interface *vogtkgl = xmalloc(sizeof(*vogtkgl));
	struct vo_interface *vo = &vogtkgl->public;
	*vogtkgl = (struct vo_gtkgl_interface){0};
	vogtkgl->vogl = vogl;

	vo->free = DELEGATE_AS0(void, vo_gtkgl_free, vo);
	vo->update_palette = vogl->update_palette;
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, vo);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vo);
	vo->refresh = DELEGATE_AS0(void, refresh, vo);
	vo->vsync = DELEGATE_AS0(void, vsync, vo);
	vo->set_vo_cmp = DELEGATE_AS1(void, int, vo_gtkgl_set_vo_cmp, vo);

	/* Configure drawing_area widget */
	gtk_widget_set_size_request(global_uigtk2->drawing_area, 640, 480);
	GdkGLConfig *glconfig = gdk_gl_config_new_by_mode(GDK_GL_MODE_RGB | GDK_GL_MODE_DOUBLE);
	if (!glconfig) {
		LOG_ERROR("Failed to create OpenGL config\n");
		vo_gtkgl_free(vo);
		return NULL;
	}
	if (!gtk_widget_set_gl_capability(global_uigtk2->drawing_area, glconfig, NULL, TRUE, GDK_GL_RGBA_TYPE)) {
		LOG_ERROR("Failed to add OpenGL support to GTK widget\n");
		g_object_unref(glconfig);
		vo_gtkgl_free(vo);
		return NULL;
	}
	g_object_unref(glconfig);

	g_signal_connect(global_uigtk2->top_window, "window-state-event", G_CALLBACK(window_state), vo);
	g_signal_connect(global_uigtk2->drawing_area, "configure-event", G_CALLBACK(configure), vo);

	/* Show top window first so that drawing area is realised to the
	 * right size even if we then fullscreen.  */
	gtk_widget_show(global_uigtk2->top_window);
	/* Set fullscreen. */
	set_fullscreen(vo, vo_cfg->fullscreen);

	vsync(vo);

	return vo;
}

static void vo_gtkgl_free(void *sptr) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_interface *vogl = vogtkgl->vogl;
	set_fullscreen(vogtkgl, 0);
	DELEGATE_CALL(vogl->free);
	free(vogtkgl);
}

static void resize(void *sptr, unsigned int w, unsigned int h) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_interface *vo = &vogtkgl->public;
	if (vo->is_fullscreen) {
		return;
	}
	if (w < 160 || h < 120) {
		return;
	}
	if (w > 1920 || h > 1440) {
		return;
	}
	/* You can't just set the widget size and expect GTK to adapt the
	 * containing window, or indeed ask it to.  This will hopefully work
	 * consistently.  It seems to be basically how GIMP "shrink wrap"s its
	 * windows.  */
	gint oldw = global_uigtk2->top_window->allocation.width;
	gint oldh = global_uigtk2->top_window->allocation.height;
	gint woff = oldw - global_uigtk2->drawing_area->allocation.width;
	gint hoff = oldh - global_uigtk2->drawing_area->allocation.height;
	w += woff;
	h += hoff;
	gtk_window_resize(GTK_WINDOW(global_uigtk2->top_window), w, h);
}

static int set_fullscreen(void *sptr, _Bool fullscreen) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_interface *vo = &vogtkgl->public;
	(void)fullscreen;
	if (fullscreen) {
		gtk_window_fullscreen(GTK_WINDOW(global_uigtk2->top_window));
	} else {
		gtk_window_unfullscreen(GTK_WINDOW(global_uigtk2->top_window));
	}
	vo->is_fullscreen = fullscreen;
	return 0;
}

static gboolean window_state(GtkWidget *tw, GdkEventWindowState *event, gpointer data) {
	struct vo_interface *vo = data;
	(void)tw;
	(void)data;
	if ((event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) && !vo->is_fullscreen) {
		gtk_widget_hide(global_uigtk2->menubar);
		vo->is_fullscreen = 1;
	}
	if (!(event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) && vo->is_fullscreen) {
		gtk_widget_show(global_uigtk2->menubar);
		vo->is_fullscreen = 0;
	}
	return 0;
}

static gboolean configure(GtkWidget *da, GdkEventConfigure *event, gpointer data) {
	struct vo_gtkgl_interface *vogtkgl = data;
	struct vo_interface *vogl = vogtkgl->vogl;
	(void)event;

	GdkGLContext *glcontext = gtk_widget_get_gl_context(da);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(da);

	if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
		g_assert_not_reached();
	}

	DELEGATE_CALL(vogl->resize, da->allocation.width, da->allocation.height);
	vo_opengl_get_display_rect(vogl, &global_uigtk2->display_rect);
	vo_gtkgl_set_vsync(1);

	gdk_gl_drawable_gl_end(gldrawable);

	return 0;
}

static void refresh(void *sptr) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_interface *vogl = vogtkgl->vogl;
	GdkGLContext *glcontext = gtk_widget_get_gl_context(global_uigtk2->drawing_area);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(global_uigtk2->drawing_area);

	if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
		g_assert_not_reached();
	}

	DELEGATE_CALL(vogl->refresh);

	gdk_gl_drawable_swap_buffers(gldrawable);
	gdk_gl_drawable_gl_end(gldrawable);
}

static void vsync(void *sptr) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_interface *vogl = vogtkgl->vogl;
	GdkGLContext *glcontext = gtk_widget_get_gl_context(global_uigtk2->drawing_area);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(global_uigtk2->drawing_area);

	if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
		g_assert_not_reached();
	}

	DELEGATE_CALL(vogl->vsync);

	gdk_gl_drawable_swap_buffers(gldrawable);
	gdk_gl_drawable_gl_end(gldrawable);
}

static void vo_gtkgl_set_vo_cmp(void *sptr, int mode) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_interface *vo = &vogtkgl->public;
	struct vo_interface *vogl = vogtkgl->vogl;
	DELEGATE_CALL(vogl->set_vo_cmp, mode);
	vo->render_scanline = vogl->render_scanline;
}

static void vo_gtkgl_set_vsync(int val) {
	(void)val;

#ifdef HAVE_X11

	PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddress((const GLubyte *)"glXSwapIntervalEXT");
	if (glXSwapIntervalEXT) {
		Display *dpy = gdk_x11_drawable_get_xdisplay(GTK_WIDGET(global_uigtk2->drawing_area)->window);
		Window win = gdk_x11_drawable_get_xid(GTK_WIDGET(global_uigtk2->drawing_area)->window);
		if (dpy && win) {
			LOG_DEBUG(3, "vo_gtkgl: glXSwapIntervalEXT(%p, %lu, %d)\n", dpy, win, val);
			glXSwapIntervalEXT(dpy, win, val);
			return;
		}
	}

	PFNGLXSWAPINTERVALMESAPROC glXSwapIntervalMESA = (PFNGLXSWAPINTERVALMESAPROC)glXGetProcAddress((const GLubyte *)"glXSwapIntervalMESA");
	if (glXSwapIntervalMESA) {
		LOG_DEBUG(3, "vo_gtkgl: glXSwapIntervalMESA(%d)\n", val);
		glXSwapIntervalMESA(val);
		return;
	}

	PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)glXGetProcAddress((const GLubyte *)"glXSwapIntervalSGI");
	if (glXSwapIntervalSGI) {
		LOG_DEBUG(3, "vo_gtkgl: glXSwapIntervalSGI(%d)\n", val);
		glXSwapIntervalSGI(val);
	}

#endif

	LOG_DEBUG(3, "vo_gtkgl: Found no way to set swap interval\n");
}
