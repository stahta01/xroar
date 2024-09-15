/** \file
 *
 *  \brief GTK+ 3 printer control window.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtk/gtk.h>

#include "array.h"
#include "delegate.h"
#include "xalloc.h"

#include "printer.h"
#include "xroar.h"

#include "gtk3/common.h"
#include "gtk3/event_handlers.h"
#include "gtk3/printercontrol.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Radio buttons
static struct {
	const char *id;
	int destination;
} rb_destinations[] = {
	{ "pc_rb_none", PRINTER_DESTINATION_NONE },
	{ "pc_rb_file", PRINTER_DESTINATION_FILE },
	{ "pc_rb_pipe", PRINTER_DESTINATION_PIPE },
};

// Callbacks
static gboolean pc_window_hide(GtkWidget *, GdkEvent *, gpointer);
static void pc_set_destination(GtkButton *, gpointer);
static void pc_file_attach(GtkButton *, gpointer);
static void pc_pipe_changed(GtkEditable *, gpointer);
static void pc_pipe_reset(GtkButton *, gpointer);
static void pc_pipe_apply(GtkWidget *, gpointer);
static void pc_flush(GtkButton *, gpointer);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - create window

void gtk3_create_pc_window(struct ui_gtk3_interface *uigtk3) {
	uigtk3_add_from_resource(uigtk3, "/uk/org/6809/xroar/gtk3/printercontrol.ui");

	// Connect signals
	uigtk3_signal_connect(uigtk3, "pc_window", "delete-event", pc_window_hide, uigtk3);
	uigtk3_signal_connect(uigtk3, "pc_window", "key-press-event", gtk3_dummy_keypress, uigtk3);
	uigtk3_signal_connect(uigtk3, "pc_b_file_attach", "clicked", pc_file_attach, uigtk3);
	uigtk3_signal_connect(uigtk3, "pc_e_pipe", "changed", pc_pipe_changed, uigtk3);
	uigtk3_signal_connect(uigtk3, "pc_b_pipe_reset", "clicked", pc_pipe_reset, uigtk3);
	uigtk3_signal_connect(uigtk3, "pc_e_pipe", "activate", pc_pipe_apply, uigtk3);
	uigtk3_signal_connect(uigtk3, "pc_b_pipe_apply", "clicked", pc_pipe_apply, uigtk3);
	uigtk3_signal_connect(uigtk3, "pc_b_flush", "clicked", pc_flush, uigtk3);

	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(rb_destinations); i++) {
		uigtk3_signal_connect(uigtk3, rb_destinations[i].id, "clicked", pc_set_destination, uigtk3);
	}

#ifndef HAVE_POPEN
	// Remove pipe from the UI if not supported.  Not that I can imagine
	// this ever being the case...
	uigtk3_widget_set_sensitive(uigtk3, "pc_rb_pipe", 0);
	uigtk3_widget_hide(uigtk3, "pc_rb_pipe");
	uigtk3_editable_set_editable(uigtk3, "pc_e_pipe", 0);
	uigtk3_widget_hide(uigtk3, "pc_e_pipe");
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - UI callbacks

void gtk3_pc_update_state(struct ui_gtk3_interface *uigtk3,
			  int tag, int value, const void *data) {
	switch (tag) {
	case ui_tag_print_destination:
		{
			const char *tb_name = "pc_rb_none";
			switch (value) {
			default:
				break;
			case 1:
				tb_name = "pc_rb_file";
				break;
			case 2:
				tb_name = "pc_rb_pipe";
				break;
			}
			uigtk3_notify_toggle_button_set_active(uigtk3, tb_name, 1, NULL);
		}
		break;

	case ui_tag_print_file:
		uigtk3_label_set_text(uigtk3, "pc_l_filename", data ? (const char *)data : "");
		break;

	case ui_tag_print_pipe:
		{
			if (uigtk3->printer.pipe)
				free(uigtk3->printer.pipe);
			uigtk3->printer.pipe = xstrdup(data ? (const char *)data : "");
			GtkEntry *e_pipe = GTK_ENTRY(gtk_builder_get_object(uigtk3->builder, "pc_e_pipe"));
			GtkEntryBuffer *eb_pipe = gtk_entry_get_buffer(e_pipe);
			gtk_entry_buffer_set_text(eb_pipe, uigtk3->printer.pipe, -1);
			uigtk3_widget_set_sensitive(uigtk3, "pc_b_pipe_reset", 0);
			uigtk3_widget_set_sensitive(uigtk3, "pc_b_pipe_apply", 0);
		}
		break;

	case ui_tag_print_count:
		{
			char buf[14];
			char *fmt = "%.0f%s";
			char *unit = "";
			double count = (double)value;
			if (count > 1000.) {
				fmt = "%.1f%s";
				count /= 1000.;
				unit = "k";
			}
			if (count > 1000.) {
				count /= 1000.;
				unit = "M";
			}
			if (count > 1000.) {
				count /= 1000.;
				unit = "G";
			}
			snprintf(buf, sizeof(buf), fmt, count, unit);
			uigtk3_label_set_text(uigtk3, "pc_l_chars", buf);
			uigtk3_widget_set_sensitive(uigtk3, "pc_b_flush", value != 0);
		}
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - signal handlers

void gtk3_toggle_pc_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	if (gtk_toggle_action_get_active(current)) {
		uigtk3_widget_show(uigtk3, "pc_window");
	} else {
		uigtk3_widget_hide(uigtk3, "pc_window");
	}
}

static gboolean pc_window_hide(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk3_interface *uigtk3 = user_data;
	uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/FileMenu/PrinterControl", 0);
	uigtk3_widget_hide(uigtk3, "pc_window");
	return TRUE;
}

static void pc_set_destination(GtkButton *button, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;

	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(rb_destinations); i++) {
		GtkButton *dest_button = GTK_BUTTON(gtk_builder_get_object(uigtk3->builder, rb_destinations[i].id));
		if (button == dest_button) {
			xroar_set_printer_destination(0, rb_destinations[i].destination);
			break;
		}
	}
}

static void pc_file_attach(GtkButton *button, gpointer user_data) {
	(void)button;
	struct ui_gtk3_interface *uigtk3 = user_data;
	struct ui_interface *ui = &uigtk3->public;

	char *filename = DELEGATE_CALL(ui->filereq_interface->save_filename, "Print to file");
	if (filename) {
		xroar_set_printer_file(0, filename);
		uigtk3_label_set_text(uigtk3, "pc_l_filename", filename);
	}
}

static void pc_pipe_changed(GtkEditable *e, gpointer user_data) {
	(void)e;
	struct ui_gtk3_interface *uigtk3 = user_data;
	uigtk3_widget_set_sensitive(uigtk3, "pc_b_pipe_reset", 1);
	uigtk3_widget_set_sensitive(uigtk3, "pc_b_pipe_apply", 1);
}

static void pc_pipe_reset(GtkButton *button, gpointer user_data) {
	(void)button;
	struct ui_gtk3_interface *uigtk3 = user_data;

	if (!uigtk3->printer.pipe)
		uigtk3->printer.pipe = xstrdup("");

	GtkEntry *e_pipe = GTK_ENTRY(gtk_builder_get_object(uigtk3->builder, "pc_e_pipe"));
	GtkEntryBuffer *eb_pipe = gtk_entry_get_buffer(e_pipe);
	gtk_entry_buffer_set_text(eb_pipe, uigtk3->printer.pipe, -1);
	uigtk3_widget_set_sensitive(uigtk3, "pc_b_pipe_reset", 0);
	uigtk3_widget_set_sensitive(uigtk3, "pc_b_pipe_apply", 0);
}

static void pc_pipe_apply(GtkWidget *w, gpointer user_data) {
	(void)w;
	struct ui_gtk3_interface *uigtk3 = user_data;

	GtkEntry *e_pipe = GTK_ENTRY(gtk_builder_get_object(uigtk3->builder, "pc_e_pipe"));
	GtkEntryBuffer *eb_pipe = gtk_entry_get_buffer(e_pipe);
	const gchar *eb_pipe_text = gtk_entry_buffer_get_text(eb_pipe);
	xroar_set_printer_pipe(0, eb_pipe_text);

	uigtk3_widget_set_sensitive(uigtk3, "pc_b_pipe_reset", 0);
	uigtk3_widget_set_sensitive(uigtk3, "pc_b_pipe_apply", 0);
}

static void pc_flush(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_flush_printer();
}
