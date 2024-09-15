/** \file
 *
 *  \brief GTK+ 3 drive control window.
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

#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

#include "gtk3/common.h"
#include "gtk3/drivecontrol.h"
#include "gtk3/event_handlers.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

const char *label_filename_drive[4] = {
	"filename_drive1", "filename_drive2", "filename_drive3", "filename_drive4"
};

const char *tb_we_drive[4] = {
	"we_drive1", "we_drive2", "we_drive3", "we_drive4"
};

const char *tb_wb_drive[4] = {
	"wb_drive1", "wb_drive2", "wb_drive3", "wb_drive4"
};

// UI updates
static void gtk3_update_drive_disk(struct ui_gtk3_interface *, int drive, const struct vdisk *disk);
static void gtk3_update_drive_write_enable(struct ui_gtk3_interface *, int drive, _Bool write_enable);
static void gtk3_update_drive_write_back(struct ui_gtk3_interface *, int drive, _Bool write_back);
static void update_drive_cyl_head(void *, unsigned drive, unsigned cyl, unsigned head);

// Callbacks
static gboolean hide_dc_window(GtkWidget *, GdkEvent *event, gpointer user_data);
static void dc_insert(GtkButton *, gpointer user_data);
static void dc_new(GtkButton *, gpointer user_data);
static void dc_eject(GtkButton *, gpointer user_data);
static void dc_toggled_we(GtkToggleButton *, gpointer user_data);
static void dc_toggled_wb(GtkToggleButton *, gpointer user_data);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - create window

void gtk3_create_dc_window(struct ui_gtk3_interface *uigtk3) {
	uigtk3_add_from_resource(uigtk3, "/uk/org/6809/xroar/gtk3/drivecontrol.ui");

	// Connect signals
	uigtk3_signal_connect(uigtk3, "dc_window", "delete-event", hide_dc_window, uigtk3);
	uigtk3_signal_connect(uigtk3, "dc_window", "key-press-event", gtk3_dummy_keypress, uigtk3);
	for (int i = 0; i < 4; i++) {
		uigtk3_signal_connect(uigtk3, tb_we_drive[i], "toggled", G_CALLBACK(dc_toggled_we), (gpointer)(intptr_t)i);
		uigtk3_signal_connect(uigtk3, tb_wb_drive[i], "toggled", G_CALLBACK(dc_toggled_wb), (gpointer)(intptr_t)i);
	}
	uigtk3_signal_connect(uigtk3, "insert_drive1", "clicked", dc_insert, (gpointer)(intptr_t)0);
	uigtk3_signal_connect(uigtk3, "insert_drive2", "clicked", dc_insert, (gpointer)(intptr_t)1);
	uigtk3_signal_connect(uigtk3, "insert_drive3", "clicked", dc_insert, (gpointer)(intptr_t)2);
	uigtk3_signal_connect(uigtk3, "insert_drive4", "clicked", dc_insert, (gpointer)(intptr_t)3);
	uigtk3_signal_connect(uigtk3, "new_drive1", "clicked", dc_new, (gpointer)(intptr_t)0);
	uigtk3_signal_connect(uigtk3, "new_drive2", "clicked", dc_new, (gpointer)(intptr_t)1);
	uigtk3_signal_connect(uigtk3, "new_drive3", "clicked", dc_new, (gpointer)(intptr_t)2);
	uigtk3_signal_connect(uigtk3, "new_drive4", "clicked", dc_new, (gpointer)(intptr_t)3);
	uigtk3_signal_connect(uigtk3, "eject_drive1", "clicked", dc_eject, (gpointer)(intptr_t)0);
	uigtk3_signal_connect(uigtk3, "eject_drive2", "clicked", dc_eject, (gpointer)(intptr_t)1);
	uigtk3_signal_connect(uigtk3, "eject_drive3", "clicked", dc_eject, (gpointer)(intptr_t)2);
	uigtk3_signal_connect(uigtk3, "eject_drive4", "clicked", dc_eject, (gpointer)(intptr_t)3);

	xroar.vdrive_interface->update_drive_cyl_head = DELEGATE_AS3(void, unsigned, unsigned, unsigned, update_drive_cyl_head, uigtk3);
}

void gtk3_toggle_dc_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	if (gtk_toggle_action_get_active(current)) {
		uigtk3_widget_show(uigtk3, "dc_window");
	} else {
		uigtk3_widget_hide(uigtk3, "dc_window");
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - UI callbacks

void gtk3_dc_update_state(struct ui_gtk3_interface *uigtk3,
			  int tag, int value, const void *data) {
	switch (tag) {
	case ui_tag_disk_write_enable:
		gtk3_update_drive_write_enable(uigtk3, value, (intptr_t)data);
		break;

	case ui_tag_disk_write_back:
		gtk3_update_drive_write_back(uigtk3, value, (intptr_t)data);
		break;

	case ui_tag_disk_data:
		gtk3_update_drive_disk(uigtk3, value, (const struct vdisk *)data);
		break;

	default:
		break;
	}
}

static void gtk3_update_drive_write_enable(struct ui_gtk3_interface *uigtk3, int drive, _Bool write_enable) {
	if (drive >= 0 && drive <= 3) {
		uigtk3_toggle_button_set_active(uigtk3, tb_we_drive[drive], write_enable ? TRUE : FALSE);
	}
}

static void gtk3_update_drive_write_back(struct ui_gtk3_interface *uigtk3, int drive, _Bool write_back) {
	if (drive >= 0 && drive <= 3) {
		uigtk3_toggle_button_set_active(uigtk3, tb_wb_drive[drive], write_back ? TRUE : FALSE);
	}
}

static void gtk3_update_drive_disk(struct ui_gtk3_interface *uigtk3, int drive, const struct vdisk *disk) {
	if (drive < 0 || drive > 3)
		return;
	char *filename = NULL;
	_Bool we = 0, wb = 0;
	if (disk) {
		filename = disk->filename;
		we = !disk->write_protect;
		wb = disk->write_back;
	}

	uigtk3_label_set_text(uigtk3, label_filename_drive[drive], filename);
	gtk3_update_drive_write_enable(uigtk3, drive, we);
	gtk3_update_drive_write_back(uigtk3, drive, wb);
}

static void update_drive_cyl_head(void *sptr, unsigned drive, unsigned cyl, unsigned head) {
	struct ui_gtk3_interface *uigtk3 = sptr;
	char string[16];
	snprintf(string, sizeof(string), "Dr %01u Tr %02u He %01u", drive + 1, cyl, head);
	uigtk3_label_set_text(uigtk3, "drive_cyl_head", string);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - insert disk

void gtk3_insert_disk(struct ui_gtk3_interface *uigtk3, int drive) {
	static GtkFileChooser *file_dialog = NULL;
	static GtkComboBox *drive_combo = NULL;
	if (!file_dialog) {
		file_dialog = GTK_FILE_CHOOSER(
		    gtk_file_chooser_dialog_new("Insert Disk",
			GTK_WINDOW(uigtk3->top_window),
			GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
			GTK_RESPONSE_CANCEL, "_Open",
			GTK_RESPONSE_ACCEPT, NULL));
	}
	if (!drive_combo) {
		drive_combo = (GtkComboBox *)gtk_combo_box_text_new();
		gtk_combo_box_text_append_text((GtkComboBoxText *)drive_combo, "Drive 1");
		gtk_combo_box_text_append_text((GtkComboBoxText *)drive_combo, "Drive 2");
		gtk_combo_box_text_append_text((GtkComboBoxText *)drive_combo, "Drive 3");
		gtk_combo_box_text_append_text((GtkComboBoxText *)drive_combo, "Drive 4");
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - signal handlers

static gboolean hide_dc_window(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk3_interface *uigtk3 = user_data;
	uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/FileMenu/DriveControl", 0);
	uigtk3_widget_hide(uigtk3, "dc_window");
	return TRUE;
}

static void dc_insert(GtkButton *button, gpointer user_data) {
	int drive = (intptr_t)user_data;
	(void)button;
	xroar_insert_disk(drive);
}

static void dc_new(GtkButton *button, gpointer user_data) {
	int drive = (intptr_t)user_data;
	(void)button;
	xroar_new_disk(drive);
}

static void dc_eject(GtkButton *button, gpointer user_data) {
	int drive = (intptr_t)user_data;
	(void)button;
	xroar_eject_disk(drive);
}

static void dc_toggled_we(GtkToggleButton *togglebutton, gpointer user_data) {
	int set = gtk_toggle_button_get_active(togglebutton) ? 1 : 0;
	int drive = (intptr_t)user_data;
	xroar_set_write_enable(0, drive, set);
}

static void dc_toggled_wb(GtkToggleButton *togglebutton, gpointer user_data) {
	int set = gtk_toggle_button_get_active(togglebutton) ? 1 : 0;
	int drive = (intptr_t)user_data;
	xroar_set_write_back(0, drive, set);
}
