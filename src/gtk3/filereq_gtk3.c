/** \file
 *
 *  \brief GTK+ 3 file requester module.
 *
 *  \copyright Copyright 2014 Ciaran Anscomb
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

#include <gtk/gtk.h>

#include "xalloc.h"

#include "fs.h"
#include "logging.h"
#include "module.h"
#include "xroar.h"

#include "gtk3/common.h"

struct filereq_interface_gtk3 {
	struct filereq_interface public;

	GtkWidget *top_window;
	GtkWidget *load_dialog;
	GtkWidget *save_dialog;
	gchar *filename;
};

static void *filereq_gtk3_new(void *cfg);
static void filereq_gtk3_free(void *sptr);
static char *load_filename(void *sptr, char const *title);
static char *save_filename(void *sptr, char const *title);

struct module filereq_gtk3_module = {
	.name = "gtk3", .description = "GTK+ 3 file requester",
	.new = filereq_gtk3_new
};

static void *filereq_gtk3_new(void *sptr) {
	struct ui_gtk3_interface *ui_gtk3 = sptr;

	struct filereq_interface_gtk3 *frgtk3 = xmalloc(sizeof(*frgtk3));
	*frgtk3 = (struct filereq_interface_gtk3){0};
	frgtk3->public.free = DELEGATE_AS0(void, filereq_gtk3_free, frgtk3);
	frgtk3->public.load_filename = DELEGATE_AS1(charp, charcp, load_filename, frgtk3);
	frgtk3->public.save_filename = DELEGATE_AS1(charp, charcp, save_filename, frgtk3);

	// If running as part of the general GTK+ UI, fetch its top window
	// widget.  Otherwise, we need to initialise GTK+ here.
	if (ui_gtk3) {
		frgtk3->top_window = ui_gtk3->top_window;
	} else {
		gtk_init(NULL, NULL);
	}
	return frgtk3;
}

static void filereq_gtk3_free(void *sptr) {
	struct filereq_interface_gtk3 *frgtk3 = sptr;
	free(frgtk3->filename);
	frgtk3->filename = NULL;
	free(frgtk3);
}

static char *load_filename(void *sptr, char const *title) {
	struct filereq_interface_gtk3 *frgtk3 = sptr;
	if (frgtk3->filename) {
		g_free(frgtk3->filename);
		frgtk3->filename = NULL;
	}
	if (!frgtk3->load_dialog) {
		frgtk3->load_dialog = gtk_file_chooser_dialog_new(title,
		    GTK_WINDOW(frgtk3->top_window), GTK_FILE_CHOOSER_ACTION_OPEN,
		    "_Cancel", GTK_RESPONSE_CANCEL,
		    "_Open", GTK_RESPONSE_ACCEPT, NULL);
	} else {
		GdkWindow *w = gtk_widget_get_window(frgtk3->load_dialog);
		if (w) {
			gdk_window_set_title(w, title);
		}
	}
	if (gtk_dialog_run(GTK_DIALOG(frgtk3->load_dialog)) == GTK_RESPONSE_ACCEPT) {
		frgtk3->filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(frgtk3->load_dialog));
	}
	gtk_widget_hide(frgtk3->load_dialog);
	if (!frgtk3->top_window) {
		while (gtk_events_pending()) {
			gtk_main_iteration();
		}
	}
	return frgtk3->filename;
}

static char *save_filename(void *sptr, char const *title) {
	struct filereq_interface_gtk3 *frgtk3 = sptr;
	if (frgtk3->filename) {
		g_free(frgtk3->filename);
		frgtk3->filename = NULL;
	}
	if (!frgtk3->save_dialog) {
		frgtk3->save_dialog = gtk_file_chooser_dialog_new(title,
		    GTK_WINDOW(frgtk3->top_window), GTK_FILE_CHOOSER_ACTION_SAVE,
		    "_Cancel", GTK_RESPONSE_CANCEL,
		    "_Save", GTK_RESPONSE_ACCEPT, NULL);
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(frgtk3->save_dialog), TRUE);
	} else {
		GdkWindow *w = gtk_widget_get_window(frgtk3->save_dialog);
		if (w) {
			gdk_window_set_title(w, title);
		}
	}
	if (gtk_dialog_run(GTK_DIALOG(frgtk3->save_dialog)) == GTK_RESPONSE_ACCEPT) {
		frgtk3->filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(frgtk3->save_dialog));
	}
	gtk_widget_hide(frgtk3->save_dialog);
	if (!frgtk3->top_window) {
		while (gtk_events_pending()) {
			gtk_main_iteration();
		}
	}
	return frgtk3->filename;
}
