/** \file
 *
 *  \brief GtkGLExt video output module.
 *
 *  \copyright Copyright 2010-2023 Ciaran Anscomb
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

#include "top-config.h"

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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct vo_gtkgl_interface {
	struct vo_opengl_interface vogl;

	int woff, hoff;  // geometry offsets introduced by menubar
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void vo_gtkgl_free(void *sptr);
static void draw(void *sptr);
static void resize(void *sptr, unsigned int w, unsigned int h);
static int set_fullscreen(void *sptr, _Bool fullscreen);
static void set_menubar(void *sptr, _Bool show_menubar);

static gboolean window_state(GtkWidget *, GdkEventWindowState *, gpointer);
static gboolean configure(GtkWidget *, GdkEventConfigure *, gpointer);
static void vo_gtkgl_set_vsync(int val);

static void *new(void *sptr) {
	(void)sptr;

	gtk_gl_init(NULL, NULL);

	if (gdk_gl_query_extension() != TRUE) {
		LOG_ERROR("OpenGL not available\n");
		return NULL;
	}

	struct vo_gtkgl_interface *vogtkgl = vo_opengl_new(sizeof(*vogtkgl));
	*vogtkgl = (struct vo_gtkgl_interface){0};
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;

	struct vo_cfg *vo_cfg = &global_uigtk2->cfg->vo_cfg;
	vo_opengl_configure(vogl, vo_cfg);

	vo->free = DELEGATE_AS0(void, vo_gtkgl_free, vogtkgl);
	vo->draw = DELEGATE_AS0(void, draw, vogl);

	// Used by UI to adjust viewing parameters
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, vo);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vo);
	vo->set_menubar = DELEGATE_AS1(void, bool, set_menubar, vo);

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
	vo->show_menubar = 1;
	gtk_widget_show(global_uigtk2->top_window);
	/* Set fullscreen. */
	set_fullscreen(vo, vo_cfg->fullscreen);

	return vo;
}

static void vo_gtkgl_free(void *sptr) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	set_fullscreen(vogtkgl, 0);
	vo_opengl_free(vogl);
}

static void resize(void *sptr, unsigned int w, unsigned int h) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;

	if (vo->is_fullscreen) {
		return;
	}
	GdkScreen *s = gtk_window_get_screen(GTK_WINDOW(global_uigtk2->top_window));
	unsigned sw = 1024, sh = 768;
	if (s) {
		sw = gdk_screen_get_width(s);
		sh = gdk_screen_get_height(s);
	}
	if (w < 160 || h < 120) {
		return;
	}
	if (w > sw || h > sh) {
		return;
	}
	/* You can't just set the widget size and expect GTK to adapt the
	 * containing window, or indeed ask it to.  This will hopefully work
	 * consistently.  It seems to be basically how GIMP "shrink wrap"s its
	 * windows.  */
	GtkAllocation top_allocation, draw_allocation;
	gtk_widget_get_allocation(global_uigtk2->top_window, &top_allocation);
	gtk_widget_get_allocation(global_uigtk2->drawing_area, &draw_allocation);
	gint oldw = top_allocation.width;
	gint oldh = top_allocation.height;
	gint woff = oldw - draw_allocation.width;
	gint hoff = oldh - draw_allocation.height;
	vogtkgl->woff = woff;
	vogtkgl->hoff = hoff;
	gtk_window_resize(GTK_WINDOW(global_uigtk2->top_window), w + woff, h + hoff);
}

static int set_fullscreen(void *sptr, _Bool fullscreen) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;

	vo->is_fullscreen = fullscreen;
	vo->show_menubar = !fullscreen;
	if (fullscreen) {
		gtk_window_fullscreen(GTK_WINDOW(global_uigtk2->top_window));
	} else {
		gtk_window_unfullscreen(GTK_WINDOW(global_uigtk2->top_window));
	}
	return 0;
}

static void set_menubar(void *sptr, _Bool show_menubar) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;

	GtkAllocation allocation;
	gtk_widget_get_allocation(global_uigtk2->drawing_area, &allocation);
	int w = allocation.width;
	int h = allocation.height;
	if (show_menubar) {
		w += vogtkgl->woff;
		h += vogtkgl->hoff;
	}
	vo->show_menubar = show_menubar;
	if (show_menubar) {
		gtk_widget_show(global_uigtk2->menubar);
	} else {
		gtk_widget_hide(global_uigtk2->menubar);
	}
	gtk_window_resize(GTK_WINDOW(global_uigtk2->top_window), w, h);
}

static gboolean window_state(GtkWidget *tw, GdkEventWindowState *event, gpointer data) {
	struct vo_interface *vo = data;
	(void)tw;
	(void)data;
	if ((event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) && !vo->is_fullscreen) {
		gtk_widget_hide(global_uigtk2->menubar);
		vo->is_fullscreen = 1;
		vo->show_menubar = 0;
	}
	if (!(event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) && vo->is_fullscreen) {
		gtk_widget_show(global_uigtk2->menubar);
		vo->is_fullscreen = 0;
		vo->show_menubar = 1;
	}
	return 0;
}

static gboolean configure(GtkWidget *da, GdkEventConfigure *event, gpointer data) {
	struct vo_gtkgl_interface *vogtkgl = data;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;
	(void)event;

	GdkGLContext *glcontext = gtk_widget_get_gl_context(da);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(da);

	if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
		g_assert_not_reached();
	}

	GtkAllocation allocation;

	// Preserve geometry offsets introduced by menubar
	if (vo->show_menubar) {
		gtk_widget_get_allocation(global_uigtk2->top_window, &allocation);
		gint oldw = allocation.width;
		gint oldh = allocation.height;
		vogtkgl->woff = oldw - allocation.width;
		vogtkgl->hoff = oldh - allocation.height;
	}

	gtk_widget_get_allocation(da, &allocation);
	vo_opengl_setup_context(vogl, allocation.width, allocation.height);
	global_uigtk2->draw_area.x = vogl->draw_area.x;
	global_uigtk2->draw_area.y = vogl->draw_area.y;
	global_uigtk2->draw_area.w = vogl->draw_area.w;
	global_uigtk2->draw_area.h = vogl->draw_area.h;
	vo_gtkgl_set_vsync(-1);

	gdk_gl_drawable_gl_end(gldrawable);

	return 0;
}

static void draw(void *sptr) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;

	GdkGLContext *glcontext = gtk_widget_get_gl_context(global_uigtk2->drawing_area);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(global_uigtk2->drawing_area);

	if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
		g_assert_not_reached();
	}

	vo_opengl_draw(vogl);

	gdk_gl_drawable_swap_buffers(gldrawable);
	gdk_gl_drawable_gl_end(gldrawable);
}

#ifdef HAVE_X11

// Test glX extensions string for presence of a particular extension.

static _Bool opengl_has_extension(Display *display, const char *extension) {
        const char *(*glXQueryExtensionsStringFunc)(Display *, int) = (const char *(*)(Display *, int))glXGetProcAddress((const GLubyte *)"glXQueryExtensionsString");
        if (!glXQueryExtensionsStringFunc)
                return 0;

        int screen = DefaultScreen(display);

        const char *extensions = glXQueryExtensionsStringFunc(display, screen);
        if (!extensions)
                return 0;

        LOG_DEBUG(3, "gtkgl: extensions: %s\n", extensions);

        const char *start;
        const char *where, *terminator;

        // It takes a bit of care to be fool-proof about parsing the OpenGL
        // extensions string. Don't be fooled by sub-strings, etc.

        start = extensions;

        for (;;) {
                where = strstr(start, extension);
                if (!where)
                        break;

                terminator = where + strlen(extension);
                if (where == start || *(where - 1) == ' ')
                        if (*terminator == ' ' || *terminator == '\0')
                                return 1;

                start = terminator;
        }
        return 0;
}

#endif

// Set "swap interval" - that is, how many vsyncs should be waited for on
// buffer swap.  Usually this should be 1.  However, a negative value here
// tries to use GLX_EXT_swap_control_tear, which allows unsynchronised buffer
// swaps if a vsync was already missed.  If that particular extension is not
// found, just uses the absolute value.

static void vo_gtkgl_set_vsync(int val) {
	(void)val;

#ifdef HAVE_X11

	PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddress((const GLubyte *)"glXSwapIntervalEXT");
	if (glXSwapIntervalEXT) {
		Display *dpy = gdk_x11_drawable_get_xdisplay(gtk_widget_get_window(global_uigtk2->drawing_area));
		Window win = gdk_x11_drawable_get_xid(gtk_widget_get_window(global_uigtk2->drawing_area));
		if (!opengl_has_extension(dpy, "GLX_EXT_swap_control_tear")) {
			val = abs(val);
		}
		if (dpy && win) {
			LOG_DEBUG(3, "vo_gtkgl: glXSwapIntervalEXT(%p, %lu, %d)\n", dpy, win, val);
			glXSwapIntervalEXT(dpy, win, val);
			return;
		}
	}

	val = abs(val);

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
		return;
	}

#endif

	LOG_DEBUG(3, "vo_gtkgl: Found no way to set swap interval\n");
}
