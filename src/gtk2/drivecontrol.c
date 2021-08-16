/** \file
 *
 *  \brief GTK+2 drive control window.
 *
 *  \copyright Copyright 2011-2017 Ciaran Anscomb
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/drivecontrol.h"

/* Module callbacks */
static void update_drive_cyl_head(void *sptr, unsigned drive, unsigned cyl, unsigned head);

/* Drive control widgets */
static GtkWidget *dc_window = NULL;
static GtkWidget *dc_filename_drive[4] = { NULL, NULL, NULL, NULL };
static GtkToggleButton *dc_we_drive[4] = { NULL, NULL, NULL, NULL };
static GtkToggleButton *dc_wb_drive[4] = { NULL, NULL, NULL, NULL };
static GtkWidget *dc_drive_cyl_head = NULL;

static void hide_dc_window(GtkEntry *entry, gpointer user_data);
static void dc_insert(GtkButton *button, gpointer user_data);
static void dc_eject(GtkButton *button, gpointer user_data);

void gtk2_insert_disk(struct ui_gtk2_interface *uigtk2, int drive) {
	(void)uigtk2;
	static GtkFileChooser *file_dialog = NULL;
	static GtkComboBox *drive_combo = NULL;
	if (!file_dialog) {
		file_dialog = GTK_FILE_CHOOSER(
		    gtk_file_chooser_dialog_new("Insert Disk",
			GTK_WINDOW(uigtk2->top_window),
			GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL,
			GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN,
			GTK_RESPONSE_ACCEPT, NULL));
	}
	if (!drive_combo) {
		drive_combo = GTK_COMBO_BOX(gtk_combo_box_new_text());
		gtk_combo_box_append_text(drive_combo, "Drive 1");
		gtk_combo_box_append_text(drive_combo, "Drive 2");
		gtk_combo_box_append_text(drive_combo, "Drive 3");
		gtk_combo_box_append_text(drive_combo, "Drive 4");
		gtk_file_chooser_set_extra_widget(file_dialog, GTK_WIDGET(drive_combo));
		gtk_combo_box_set_active(drive_combo, 0);
	}
	if (drive >= 0 && drive <= 3) {
		gtk_combo_box_set_active(drive_combo, drive);
	}
	if (gtk_dialog_run(GTK_DIALOG(file_dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(file_dialog);
		drive = gtk_combo_box_get_active(drive_combo);
		if (drive < 0 || drive > 3)
			drive = 0;
		if (filename) {
			xroar_insert_disk_file(drive, filename);
			g_free(filename);
		}
	}
	gtk_widget_hide(GTK_WIDGET(file_dialog));
}

static void dc_toggled_we(GtkToggleButton *togglebutton, gpointer user_data);
static void dc_toggled_wb(GtkToggleButton *togglebutton, gpointer user_data);

void gtk2_create_dc_window(struct ui_gtk2_interface *uigtk2) {
	GtkBuilder *builder;
	GtkWidget *widget;
	GError *error = NULL;
	int i;
	builder = gtk_builder_new();

	GBytes *res_drivecontrol = g_resources_lookup_data("/uk/org/6809/xroar/ui_gtk2/drivecontrol.glade", 0, NULL);
	if (!gtk_builder_add_from_string(builder, g_bytes_get_data(res_drivecontrol, NULL), -1, &error)) {
		g_warning("Couldn't create UI: %s", error->message);
		g_error_free(error);
		return;
	}
	g_bytes_unref(res_drivecontrol);

	/* Extract UI elements modified elsewhere */
	dc_window = GTK_WIDGET(gtk_builder_get_object(builder, "dc_window"));
	dc_filename_drive[0] = GTK_WIDGET(gtk_builder_get_object(builder, "filename_drive1"));
	dc_filename_drive[1] = GTK_WIDGET(gtk_builder_get_object(builder, "filename_drive2"));
	dc_filename_drive[2] = GTK_WIDGET(gtk_builder_get_object(builder, "filename_drive3"));
	dc_filename_drive[3] = GTK_WIDGET(gtk_builder_get_object(builder, "filename_drive4"));
	dc_we_drive[0] = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "we_drive1"));
	dc_we_drive[1] = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "we_drive2"));
	dc_we_drive[2] = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "we_drive3"));
	dc_we_drive[3] = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "we_drive4"));
	dc_wb_drive[0] = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "wb_drive1"));
	dc_wb_drive[1] = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "wb_drive2"));
	dc_wb_drive[2] = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "wb_drive3"));
	dc_wb_drive[3] = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "wb_drive4"));
	dc_drive_cyl_head = GTK_WIDGET(gtk_builder_get_object(builder, "drive_cyl_head"));

	/* Connect signals */
	g_signal_connect(dc_window, "key-press-event", G_CALLBACK(gtk2_dummy_keypress), uigtk2);
	for (i = 0; i < 4; i++) {
		g_signal_connect(dc_we_drive[i], "toggled", G_CALLBACK(dc_toggled_we), (gpointer)(intptr_t)i);
		g_signal_connect(dc_wb_drive[i], "toggled", G_CALLBACK(dc_toggled_wb), (gpointer)(intptr_t)i);
	}
	g_signal_connect(dc_window, "delete-event", G_CALLBACK(hide_dc_window), uigtk2);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "eject_drive1"));
	g_signal_connect(widget, "clicked", G_CALLBACK(dc_eject), (gpointer)(intptr_t)0);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "eject_drive2"));
	g_signal_connect(widget, "clicked", G_CALLBACK(dc_eject), (gpointer)(intptr_t)1);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "eject_drive3"));
	g_signal_connect(widget, "clicked", G_CALLBACK(dc_eject), (gpointer)(intptr_t)2);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "eject_drive4"));
	g_signal_connect(widget, "clicked", G_CALLBACK(dc_eject), (gpointer)(intptr_t)3);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "insert_drive1"));
	g_signal_connect(widget, "clicked", G_CALLBACK(dc_insert), (gpointer)(intptr_t)0);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "insert_drive2"));
	g_signal_connect(widget, "clicked", G_CALLBACK(dc_insert), (gpointer)(intptr_t)1);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "insert_drive3"));
	g_signal_connect(widget, "clicked", G_CALLBACK(dc_insert), (gpointer)(intptr_t)2);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "insert_drive4"));
	g_signal_connect(widget, "clicked", G_CALLBACK(dc_insert), (gpointer)(intptr_t)3);

	/* In case any signals remain... */
	gtk_builder_connect_signals(builder, uigtk2);
	g_object_unref(builder);

	xroar_vdrive_interface->update_drive_cyl_head = DELEGATE_AS3(void, unsigned, unsigned, unsigned, update_drive_cyl_head, uigtk2);
}

/* Drive Control - Signal Handlers */

void gtk2_toggle_dc_window(GtkToggleAction *current, gpointer user_data) {
	gboolean val = gtk_toggle_action_get_active(current);
	(void)user_data;
	if (val) {
		gtk_widget_show(dc_window);
	} else {
		gtk_widget_hide(dc_window);
	}
}

static void hide_dc_window(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	GtkToggleAction *toggle = (GtkToggleAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ToolMenu/DriveControl");
	gtk_toggle_action_set_active(toggle, 0);
}

static void dc_insert(GtkButton *button, gpointer user_data) {
	int drive = (char *)user_data - (char *)0;
	(void)button;
	xroar_insert_disk(drive);
}

static void dc_eject(GtkButton *button, gpointer user_data) {
	int drive = (char *)user_data - (char *)0;
	(void)button;
	xroar_eject_disk(drive);
}

static void dc_toggled_we(GtkToggleButton *togglebutton, gpointer user_data) {
	int set = gtk_toggle_button_get_active(togglebutton) ? 1 : 0;
	int drive = (char *)user_data - (char *)0;
	xroar_set_write_enable(0, drive, set);
}

static void dc_toggled_wb(GtkToggleButton *togglebutton, gpointer user_data) {
	int set = gtk_toggle_button_get_active(togglebutton) ? 1 : 0;
	int drive = (char *)user_data - (char *)0;
	xroar_set_write_back(0, drive, set);
}

/* Drive Control - UI callbacks */

void gtk2_update_drive_write_enable(int drive, _Bool write_enable) {
	if (drive >= 0 && drive <= 3) {
		gtk_toggle_button_set_active(dc_we_drive[drive], write_enable ? TRUE : FALSE);
	}
}

void gtk2_update_drive_write_back(int drive, _Bool write_back) {
	if (drive >= 0 && drive <= 3) {
		gtk_toggle_button_set_active(dc_wb_drive[drive], write_back ? TRUE : FALSE);
	}
}

void gtk2_update_drive_disk(int drive, const struct vdisk *disk) {
	if (drive < 0 || drive > 3)
		return;
	char *filename = NULL;
	_Bool we = 0, wb = 0;
	if (disk) {
		filename = disk->filename;
		we = !disk->write_protect;
		wb = disk->write_back;
	}
	gtk_label_set_text(GTK_LABEL(dc_filename_drive[drive]), filename);
	gtk2_update_drive_write_enable(drive, we);
	gtk2_update_drive_write_back(drive, wb);
}

static void update_drive_cyl_head(void *sptr, unsigned drive, unsigned cyl, unsigned head) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	(void)uigtk2;
	char string[16];
	snprintf(string, sizeof(string), "Dr %01u Tr %02u He %01u", drive + 1, cyl, head);
	gtk_label_set_text(GTK_LABEL(dc_drive_cyl_head), string);
}
