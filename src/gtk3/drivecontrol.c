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

#include <gtk/gtk.h>

#include "blockdev.h"
#include "messenger.h"
#include "ui.h"
#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

#include "gtk3/common.h"
#include "gtk3/dialog.h"
#include "gtk3/drivecontrol.h"
#include "gtk3/event_handlers.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Structure holding convenient pointers to ui struct and drive number.  Needed
// to pass to UI callbacks where we want both pieces of information.

static struct drive_info {
	struct ui_gtk3_interface *uigtk3;
	int drive;
} drive_info[4];

static struct hd_info {
	struct ui_gtk3_interface *uigtk3;
	int hd;
} hd_info[2];

// Convenient arrays of per-drive UI object names.  Could do this by
// snprintf()ing each in turn.

static const char *label_filename_drive[4] = {
	"filename_drive1", "filename_drive2", "filename_drive3", "filename_drive4"
};

static const char *tb_we_drive[4] = {
	"we_drive1", "we_drive2", "we_drive3", "we_drive4"
};

static const char *tb_wb_drive[4] = {
	"wb_drive1", "wb_drive2", "wb_drive3", "wb_drive4"
};

static const char *b_insert_drive[4] = {
	"insert_drive1", "insert_drive2", "insert_drive3", "insert_drive4"
};

static const char *b_new_drive[4] = {
	"new_drive1", "new_drive2", "new_drive3", "new_drive4"
};

static const char *b_eject_drive[4] = {
	"eject_drive1", "eject_drive2", "eject_drive3", "eject_drive4"
};

static const char *label_filename_hd[2] = {
	"filename_hd0", "filename_hd1"
};

static const char *b_attach_hd[2] = {
	"attach_hd0", "attach_hd1"
};

static const char *b_new_hd[2] = {
	"new_hd0", "new_hd1"
};

static const char *b_detach_hd[2] = {
	"detach_hd0", "detach_hd1"
};

// Callbacks

static void dc_insert(GtkButton *, gpointer user_data);
static void dc_new(GtkButton *, gpointer user_data);
static void dc_eject(GtkButton *, gpointer user_data);
static void dc_toggled_we(GtkToggleButton *, gpointer user_data);
static void dc_toggled_wb(GtkToggleButton *, gpointer user_data);

static void dc_hd_attach(GtkButton *, gpointer user_data);
static void dc_hd_new(GtkButton *, gpointer user_data);
static void dc_hd_detach(GtkButton *, gpointer user_data);

// UI message reception

static void dc_ui_state_notify(void *sptr, int tag, void *smsg);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create dialog window

struct uigtk3_dialog *gtk3_dc_dialog_new(struct ui_gtk3_interface *uigtk3) {
	struct uigtk3_dialog *dlg = uigtk3_dialog_new(uigtk3, "/uk/org/6809/xroar/gtk3/drivecontrol.ui", "dc_window", ui_tag_disk_dialog);

	// Join each UI group we're interested in
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_disk_data, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_disk_write_enable, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_disk_write_back, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_disk_drive_info, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_hd_filename, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, uigtk3));

	// Connect signals

	// Per-drive signals
	for (unsigned i = 0; i < 4; ++i) {
		drive_info[i].uigtk3 = uigtk3;
		drive_info[i].drive = i;
		uigtk3_signal_connect(uigtk3, tb_we_drive[i], "toggled", G_CALLBACK(dc_toggled_we), &drive_info[i]);
		uigtk3_signal_connect(uigtk3, tb_wb_drive[i], "toggled", G_CALLBACK(dc_toggled_wb), &drive_info[i]);
		uigtk3_signal_connect(uigtk3, b_insert_drive[i], "clicked", dc_insert, &drive_info[i]);
		uigtk3_signal_connect(uigtk3, b_new_drive[i], "clicked", dc_new, &drive_info[i]);
		uigtk3_signal_connect(uigtk3, b_eject_drive[i], "clicked", dc_eject, &drive_info[i]);
	}
	for (unsigned i = 0; i < 2; ++i) {
		hd_info[i].uigtk3 = uigtk3;
		hd_info[i].hd = i;
		uigtk3_signal_connect(uigtk3, b_attach_hd[i], "clicked", dc_hd_attach, &hd_info[i]);
		uigtk3_signal_connect(uigtk3, b_new_hd[i], "clicked", dc_hd_new, &hd_info[i]);
		uigtk3_signal_connect(uigtk3, b_detach_hd[i], "clicked", dc_hd_detach, &hd_info[i]);
	}

	return dlg;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void dc_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct ui_gtk3_interface *uigtk3 = sptr;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	const void *data = uimsg->data;

	switch (tag) {

	case ui_tag_disk_data:
		if (value >= 0 && value <= 3) {
			int drive = value;
			const struct vdisk *disk = data;
			const char *filename = disk ? disk->filename : NULL;
			uigtk3_label_set_text(uigtk3, label_filename_drive[drive], filename);
		}
		break;

	case ui_tag_disk_write_enable:
		{
			int drive = (intptr_t)data;
			if (drive >= 0 && drive <= 3) {
				uigtk3_toggle_button_set_active(uigtk3, tb_we_drive[drive], value);
			}
		}
		break;

	case ui_tag_disk_write_back:
		{
			int drive = (intptr_t)data;
			if (drive >= 0 && drive <= 3) {
				uigtk3_toggle_button_set_active(uigtk3, tb_wb_drive[drive], value);
			}
		}
		break;

	case ui_tag_disk_drive_info:
		{
			const struct vdrive_info *vi = data;
			unsigned d = vi->drive + 1;
			unsigned c = vi->cylinder;
			unsigned h = vi->head;
			char string[16];
			snprintf(string, sizeof(string), "Dr %01u Tr %02u He %01u", d, c, h);
			uigtk3_label_set_text(uigtk3, "drive_cyl_head", string);
		}
		break;

	case ui_tag_hd_filename:
		if (value >= 0 && value <= 1) {
			int hd = value;
			const char *filename = data;
			uigtk3_label_set_text(uigtk3, label_filename_hd[hd], filename);
		}
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Insert floppy disk

void gtk3_insert_disk(struct ui_gtk3_interface *uigtk3, int drive) {
	static GtkFileChooser *file_dialog = NULL;
	static GtkComboBox *drive_combo = NULL;
	if (!file_dialog) {
		file_dialog = GTK_FILE_CHOOSER(
		    gtk_file_chooser_dialog_new("Insert Disk",
			GTK_WINDOW(uigtk3->top_window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			"_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT,
			NULL));
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

// Create & attach hard disk

static void gtk3_new_hd(struct ui_gtk3_interface *uigtk3, int hd) {
	static GtkFileChooser *file_dialog = NULL;
	static GtkComboBox *cbt_hd_type = NULL;
	if (!file_dialog) {
		file_dialog = GTK_FILE_CHOOSER(
		    gtk_file_chooser_dialog_new("New Hard Disk image",
			GTK_WINDOW(uigtk3->top_window),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			"_Cancel", GTK_RESPONSE_CANCEL,
			"_Create", GTK_RESPONSE_ACCEPT,
			NULL));
		cbt_hd_type = (GtkComboBox *)gtk_combo_box_text_new();
		gtk_combo_box_text_append_text((GtkComboBoxText *)cbt_hd_type, "20MiB");
		gtk_combo_box_text_append_text((GtkComboBoxText *)cbt_hd_type, "40MiB");
		gtk_combo_box_text_append_text((GtkComboBoxText *)cbt_hd_type, "128MiB");
		gtk_combo_box_text_append_text((GtkComboBoxText *)cbt_hd_type, "256MiB");
		gtk_file_chooser_set_extra_widget(file_dialog, GTK_WIDGET(cbt_hd_type));
		gtk_combo_box_set_active(cbt_hd_type, 0);
	}
	if (gtk_dialog_run(GTK_DIALOG(file_dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(file_dialog);
		uint8_t hd_type;
		switch (gtk_combo_box_get_active(cbt_hd_type)) {
		case 0: default:
			hd_type = BD_ACME_NEMESIS;
			break;
		case 1:
			hd_type = BD_ACME_ULTRASONICUS;
			break;
		case 2:
			hd_type = BD_ACME_ACCELLERATTI;
			break;
		case 3:
			hd_type = BD_ACME_ZIPPIBUS;
			break;
		}
		if (filename) {
			if (bd_create(filename, hd_type)) {
				xroar_insert_hd_file(hd, filename);
			}
			g_free(filename);
		}
	}
	gtk_widget_hide(GTK_WIDGET(file_dialog));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Disk dialog - signal handlers

// Disk insert, create, eject use top level functions that prompt the user and
// then shuffle disks around intelligently.

static void dc_insert(GtkButton *button, gpointer user_data) {
	struct drive_info *di = user_data;
	int drive = di->drive;
	(void)button;
	xroar_insert_disk(drive);
}

static void dc_new(GtkButton *button, gpointer user_data) {
	struct drive_info *di = user_data;
	int drive = di->drive;
	(void)button;
	xroar_new_disk(drive);
}

static void dc_eject(GtkButton *button, gpointer user_data) {
	struct drive_info *di = user_data;
	int drive = di->drive;
	(void)button;
	xroar_eject_disk(drive);
}

// Checkbox toggles for write enable and write back.

static void dc_toggled_we(GtkToggleButton *togglebutton, gpointer user_data) {
	struct drive_info *di = user_data;
	int drive = di->drive;
	int value = gtk_toggle_button_get_active(togglebutton);
	ui_update_state(-1, ui_tag_disk_write_enable, value, (void *)(intptr_t)drive);
}

static void dc_toggled_wb(GtkToggleButton *togglebutton, gpointer user_data) {
	struct drive_info *di = user_data;
	int drive = di->drive;
	int value = gtk_toggle_button_get_active(togglebutton);
	ui_update_state(-1, ui_tag_disk_write_back, value, (void *)(intptr_t)drive);
}

static void dc_hd_attach(GtkButton *button, gpointer user_data) {
	struct hd_info *hi = user_data;
	int hd = hi->hd;
	(void)button;
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->load_filename, "Attach hard disk image");
	if (filename) {
		xroar_insert_hd_file(hd, filename);
	}
}

static void dc_hd_new(GtkButton *button, gpointer user_data) {
	struct hd_info *hi = user_data;
	int hd = hi->hd;
	(void)button;
	gtk3_new_hd(hi->uigtk3, hd);
}

static void dc_hd_detach(GtkButton *button, gpointer user_data) {
	struct hd_info *hi = user_data;
	int hd = hi->hd;
	(void)button;
	xroar_insert_hd_file(hd, NULL);
}
