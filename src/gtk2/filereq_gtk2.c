/** \file
 *
 *  \brief GTK+ 2 file requester module.
 *
 *  \copyright Copyright 2008-2023 Ciaran Anscomb
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

#include <glib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "xalloc.h"

#include "fs.h"
#include "logging.h"
#include "module.h"
#include "xroar.h"

#include "gtk2/common.h"

struct filereq_interface_gtk2 {
	struct filereq_interface public;

	GtkWidget *top_window;
	GtkWidget *load_dialog;
	GtkWidget *save_dialog;
	gchar *filename;
};

static void *filereq_gtk2_new(void *cfg);
static void filereq_gtk2_free(void *sptr);
static char *load_filename(void *sptr, char const *title);
static char *save_filename(void *sptr, char const *title);

struct module filereq_gtk2_module = {
	.name = "gtk2", .description = "GTK+ 2 file requester",
	.new = filereq_gtk2_new
};

static void *filereq_gtk2_new(void *sptr) {
	struct ui_gtk2_interface *ui_gtk2 = sptr;

	struct filereq_interface_gtk2 *frgtk2 = xmalloc(sizeof(*frgtk2));
	*frgtk2 = (struct filereq_interface_gtk2){0};
	frgtk2->public.free = DELEGATE_AS0(void, filereq_gtk2_free, frgtk2);
	frgtk2->public.load_filename = DELEGATE_AS1(charp, charcp, load_filename, frgtk2);
	frgtk2->public.save_filename = DELEGATE_AS1(charp, charcp, save_filename, frgtk2);

	// If running as part of the general GTK+ UI, fetch its top window
	// widget.  Otherwise, we need to initialise GTK+ here.
	if (ui_gtk2) {
		frgtk2->top_window = ui_gtk2->top_window;
	} else {
		gtk_init(NULL, NULL);
	}
	return frgtk2;
}

static void filereq_gtk2_free(void *sptr) {
	struct filereq_interface_gtk2 *frgtk2 = sptr;
	if (frgtk2->filename) {
		free(frgtk2->filename);
		frgtk2->filename = NULL;
	}
	free(frgtk2);
}

static char *load_filename(void *sptr, char const *title) {
	struct filereq_interface_gtk2 *frgtk2 = sptr;
	if (frgtk2->filename) {
		g_free(frgtk2->filename);
		frgtk2->filename = NULL;
	}
	if (!frgtk2->load_dialog) {
		frgtk2->load_dialog = gtk_file_chooser_dialog_new(title,
		    GTK_WINDOW(frgtk2->top_window), GTK_FILE_CHOOSER_ACTION_OPEN,
		    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	} else {
		GdkWindow *w = gtk_widget_get_window(frgtk2->load_dialog);
		if (w) {
			gdk_window_set_title(w, title);
		}
	}
	if (gtk_dialog_run(GTK_DIALOG(frgtk2->load_dialog)) == GTK_RESPONSE_ACCEPT) {
		frgtk2->filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(frgtk2->load_dialog));
	}
	gtk_widget_hide(frgtk2->load_dialog);
	if (!frgtk2->top_window) {
		while (gtk_events_pending()) {
			gtk_main_iteration();
		}
	}
	return frgtk2->filename;
}

static char *save_filename(void *sptr, char const *title) {
	struct filereq_interface_gtk2 *frgtk2 = sptr;
	if (frgtk2->filename) {
		g_free(frgtk2->filename);
		frgtk2->filename = NULL;
	}
	if (!frgtk2->save_dialog) {
		frgtk2->save_dialog = gtk_file_chooser_dialog_new(title,
		    GTK_WINDOW(frgtk2->top_window), GTK_FILE_CHOOSER_ACTION_SAVE,
		    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		    GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(frgtk2->save_dialog), TRUE);
	} else {
		GdkWindow *w = gtk_widget_get_window(frgtk2->save_dialog);
		if (w) {
			gdk_window_set_title(w, title);
		}
	}
	if (gtk_dialog_run(GTK_DIALOG(frgtk2->save_dialog)) == GTK_RESPONSE_ACCEPT) {
		frgtk2->filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(frgtk2->save_dialog));
	}
	gtk_widget_hide(frgtk2->save_dialog);
	if (!frgtk2->top_window) {
		while (gtk_events_pending()) {
			gtk_main_iteration();
		}
	}
	return frgtk2->filename;
}
