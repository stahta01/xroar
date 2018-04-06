/*

GTK+2 file requester module

Copyright 2008-2017 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "fs.h"
#include "logging.h"
#include "module.h"
#include "xroar.h"

#include "gtk2/ui_gtk2.h"

static _Bool init(void *cfg);
static char *load_filename(char const * const *extensions);
static char *save_filename(char const * const *extensions);

FileReqModule filereq_gtk2_module = {
	.common = { .name = "gtk2", .description = "GTK+-2 file requester",
	            .init = init },
	.load_filename = load_filename,
	.save_filename = save_filename
};

static _Bool init(void *cfg) {
	(void)cfg;
	/* Only initialise if not running as part of general GTK+ interface */
	if (gtk2_top_window == NULL) {
		gtk_init(NULL, NULL);
	}
	return 1;
}

static GtkWidget *load_dialog = NULL;
static GtkWidget *save_dialog = NULL;
static gchar *filename = NULL;

static char *load_filename(char const * const *extensions) {
	(void)extensions;  /* unused */
	if (filename) {
		g_free(filename);
		filename = NULL;
	}
	if (!load_dialog) {
		load_dialog = gtk_file_chooser_dialog_new("Load file",
		    GTK_WINDOW(gtk2_top_window), GTK_FILE_CHOOSER_ACTION_OPEN,
		    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	}
	if (gtk_dialog_run(GTK_DIALOG(load_dialog)) == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(load_dialog));
	}
	gtk_widget_hide(load_dialog);
	if (!gtk2_top_window) {
		while (gtk_events_pending()) {
			gtk_main_iteration();
		}
	}
	return filename;
}

static char *save_filename(char const * const *extensions) {
	(void)extensions;  /* unused */
	if (filename) {
		g_free(filename);
		filename = NULL;
	}
	if (!save_dialog) {
		save_dialog = gtk_file_chooser_dialog_new("Save file",
		    GTK_WINDOW(gtk2_top_window), GTK_FILE_CHOOSER_ACTION_SAVE,
		    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		    GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(save_dialog), TRUE);
	}
	if (gtk_dialog_run(GTK_DIALOG(save_dialog)) == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(save_dialog));
	}
	gtk_widget_hide(save_dialog);
	if (!gtk2_top_window) {
		while (gtk_events_pending()) {
			gtk_main_iteration();
		}
	}
	return filename;
}
