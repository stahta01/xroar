/** \file
 *
 *  \brief GTK+ 3 tape control window.
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

#include "events.h"
#include "fs.h"
#include "tape.h"
#include "vdrive.h"
#include "xroar.h"

#include "gtk3/common.h"
#include "gtk3/dialog.h"
#include "gtk3/event_handlers.h"
#include "gtk3/tapecontrol.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Column indices within the input ListStore

enum {
	TC_FILENAME = 0,
	TC_POSITION,
	TC_FILE_POINTER,
	TC_MAX
};

// Callbacks

static void tc_input_file_selected(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data);
static gboolean tc_input_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);

static void tc_toggled_fast(GtkToggleButton *togglebutton, gpointer user_data);
static void tc_toggled_pad_auto(GtkToggleButton *togglebutton, gpointer user_data);
static void tc_toggled_rewrite(GtkToggleButton *togglebutton, gpointer user_data);

static void tc_play(GtkButton *button, gpointer user_data);
static void tc_pause(GtkButton *button, gpointer user_data);
static void tc_input_rewind(GtkButton *button, gpointer user_data);
static void tc_input_eject(GtkButton *button, gpointer user_data);
static void tc_input_insert(GtkButton *button, gpointer user_data);

static gboolean tc_output_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);

static void tc_output_rewind(GtkButton *button, gpointer user_data);
static void tc_output_eject(GtkButton *button, gpointer user_data);
static void tc_output_insert(GtkButton *button, gpointer user_data);

// UI message reception

static void tc_ui_state_notify(void *sptr, int tag, void *smsg);

// Tape counter update event

static struct event ev_update_tape_counters;
static void update_tape_counters(void *);

// Helper functions

static void clear_program_list(struct uigtk3_dialog *);
static void update_program_list(struct uigtk3_dialog *);
static void tc_seek(struct tape *tape, GtkScrollType scroll, gdouble value);
static gchar *ms_to_string(int ms);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create cassettes dialog window

struct uigtk3_dialog *gtk3_tc_dialog_new(struct ui_gtk3_interface *uigtk3) {
	struct uigtk3_dialog *dlg = uigtk3_dialog_new(uigtk3, "/uk/org/6809/xroar/gtk3/tapecontrol.ui", "tc_window", ui_tag_tape_dialog);

	// Join each UI group we're interested in
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_dialog, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_playing, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_flag_fast, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_flag_pad_auto, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_flag_rewrite, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_input_filename, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_output_filename, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, dlg));

	// Connect signals - input tab
	uigtk3_signal_connect(uigtk3, "input_file_list_view", "row-activated", tc_input_file_selected, dlg);
	uigtk3_signal_connect(uigtk3, "input_file_progress", "change-value", tc_input_progress_change, dlg);
	uigtk3_signal_connect(uigtk3, "fast", "toggled", tc_toggled_fast, dlg);
	uigtk3_signal_connect(uigtk3, "pad_auto", "toggled", tc_toggled_pad_auto, dlg);
	uigtk3_signal_connect(uigtk3, "rewrite", "toggled", tc_toggled_rewrite, dlg);
	uigtk3_signal_connect(uigtk3, "input_play", "clicked", tc_play, dlg);
	uigtk3_signal_connect(uigtk3, "input_pause", "clicked", tc_pause, dlg);
	uigtk3_signal_connect(uigtk3, "input_rewind", "clicked", tc_input_rewind, dlg);
	uigtk3_signal_connect(uigtk3, "input_eject", "clicked", tc_input_eject, dlg);
	uigtk3_signal_connect(uigtk3, "input_insert", "clicked", tc_input_insert, dlg);

	// Connect signals - output tab
	uigtk3_signal_connect(uigtk3, "output_file_progress", "change-value", tc_output_progress_change, dlg);
	uigtk3_signal_connect(uigtk3, "output_record", "clicked", tc_play, dlg);
	uigtk3_signal_connect(uigtk3, "output_pause", "clicked", tc_pause, dlg);
	uigtk3_signal_connect(uigtk3, "output_rewind", "clicked", tc_output_rewind, dlg);
	uigtk3_signal_connect(uigtk3, "output_eject", "clicked", tc_output_eject, dlg);
	uigtk3_signal_connect(uigtk3, "output_insert", "clicked", tc_output_insert, dlg);

	// While window displayed, an event triggers updating tape counters
	event_init(&ev_update_tape_counters, DELEGATE_AS0(void, update_tape_counters, dlg));

	return dlg;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void update_input_filename(struct uigtk3_dialog *, const char *filename);
static void update_tape_playing(struct uigtk3_dialog *, _Bool playing);

static void tc_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct uigtk3_dialog *dlg = sptr;
	struct ui_state_message *uimsg = smsg;
	struct ui_gtk3_interface *uigtk3 = dlg->uigtk3;
	int value = uimsg->value;
	const void *data = uimsg->data;

	switch (tag) {

	case ui_tag_tape_dialog:
		update_program_list(dlg);
		if (value) {
			ev_update_tape_counters.at_tick = event_current_tick + EVENT_MS(500);
			event_queue(&UI_EVENT_LIST, &ev_update_tape_counters);
		} else {
			event_dequeue(&ev_update_tape_counters);
		}
		break;

	case ui_tag_tape_input_filename:
		update_input_filename(dlg, (const char *)data);
		break;

	case ui_tag_tape_output_filename:
		uigtk3_label_set_text(uigtk3, "output_filename", (const char *)data);
		break;

	case ui_tag_tape_flag_fast:
		uigtk3_toggle_button_set_active(uigtk3, "fast", value ? TRUE : FALSE);
		break;

	case ui_tag_tape_flag_pad_auto:
		uigtk3_toggle_button_set_active(uigtk3, "pad_auto", value ? TRUE : FALSE);
		break;

	case ui_tag_tape_flag_rewrite:
		uigtk3_toggle_button_set_active(uigtk3, "rewrite", value ? TRUE : FALSE);
		break;

	case ui_tag_tape_playing:
		update_tape_playing(dlg, value);
		break;

	default:
		break;
	}
}

static void update_input_filename(struct uigtk3_dialog *dlg, const char *filename) {
	struct ui_gtk3_interface *uigtk3 = dlg->uigtk3;
	uigtk3_label_set_text(uigtk3, "input_filename", filename);
	clear_program_list(dlg);
	if (gtk_widget_is_visible(GTK_WIDGET(dlg->window))) {
		update_program_list(dlg);
	}
}

static void update_tape_playing(struct uigtk3_dialog *dlg, _Bool playing) {
	struct ui_gtk3_interface *uigtk3 = dlg->uigtk3;
	uigtk3_widget_set_sensitive(uigtk3, "input_play", !playing);
	uigtk3_widget_set_sensitive(uigtk3, "input_pause", playing);
	uigtk3_widget_set_sensitive(uigtk3, "output_record", !playing);
	uigtk3_widget_set_sensitive(uigtk3, "output_pause", playing);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Callbacks

// Input tab callbacks

static gboolean tc_input_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data) {
	(void)range;
	struct uigtk3_dialog *dlg = user_data;
	tc_seek(xroar.tape_interface->tape_input, scroll, value);
	update_tape_counters(dlg);
	return TRUE;
}

static void tc_input_file_selected(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
	(void)tree_view;
	(void)column;
	struct uigtk3_dialog *dlg = user_data;
	struct ui_gtk3_interface *uigtk3 = dlg->uigtk3;

	GtkListStore *ls = GTK_LIST_STORE(gtk_builder_get_object(uigtk3->builder, "input_file_list_store"));

	GtkTreeIter iter;
	struct tape_file *file;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(ls), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(ls), &iter, TC_FILE_POINTER, &file, -1);
	tape_seek_to_file(xroar.tape_interface->tape_input, file);
	update_tape_counters(dlg);
}

static void tc_toggled_fast(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int value = gtk_toggle_button_get_active(togglebutton);
	ui_update_state(-1, ui_tag_tape_flag_fast, value, NULL);
}

static void tc_toggled_pad_auto(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int value = gtk_toggle_button_get_active(togglebutton);
	ui_update_state(-1, ui_tag_tape_flag_pad_auto, value, NULL);
}

static void tc_toggled_rewrite(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int value = gtk_toggle_button_get_active(togglebutton);
	ui_update_state(-1, ui_tag_tape_flag_rewrite, value, NULL);
}

static void tc_play(GtkButton *button, gpointer user_data) {
	(void)button;
	struct uigtk3_dialog *dlg = user_data;
	update_tape_playing(dlg, 1);
	ui_update_state(-1, ui_tag_tape_playing, 1, NULL);
}

static void tc_pause(GtkButton *button, gpointer user_data) {
	(void)button;
	struct uigtk3_dialog *dlg = user_data;
	update_tape_playing(dlg, 0);
	ui_update_state(-1, ui_tag_tape_playing, 0, NULL);
}

static void tc_input_rewind(GtkButton *button, gpointer user_data) {
	(void)button;
	struct uigtk3_dialog *dlg = user_data;
	if (xroar.tape_interface->tape_input) {
		tape_seek(xroar.tape_interface->tape_input, 0, SEEK_SET);
		update_tape_counters(dlg);
	}
}

static void tc_input_eject(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_eject_input_tape();
}

static void tc_input_insert(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_insert_input_tape();
}

// Output tab callbacks

static gboolean tc_output_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data) {
	(void)range;
	(void)user_data;
	tc_seek(xroar.tape_interface->tape_output, scroll, value);
	return TRUE;
}

static void tc_output_rewind(GtkButton *button, gpointer user_data) {
	(void)button;
	struct uigtk3_dialog *dlg = user_data;
	if (xroar.tape_interface && xroar.tape_interface->tape_output) {
		tape_seek(xroar.tape_interface->tape_output, 0, SEEK_SET);
		update_tape_counters(dlg);
	}
}

static void tc_output_eject(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_eject_output_tape();
}

static void tc_output_insert(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_insert_output_tape();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape counter update event

static void update_tape_counters(void *sptr) {
	struct uigtk3_dialog *dlg = sptr;
	struct ui_gtk3_interface *uigtk3 = dlg->uigtk3;

	long imax = 0, ipos = 0;
	if (xroar.tape_interface->tape_input) {
		imax = tape_to_ms(xroar.tape_interface->tape_input, xroar.tape_interface->tape_input->size);
		ipos = tape_to_ms(xroar.tape_interface->tape_input, xroar.tape_interface->tape_input->offset);
	}
	if (uigtk3_update_adjustment(uigtk3, "input_file_adjustment", 0.0, (gdouble)imax, (gdouble)ipos)) {
		uigtk3_label_set_text(uigtk3, "input_file_time", ms_to_string(ipos));
	}

	long omax = 0, opos = 0;
	if (xroar.tape_interface->tape_output) {
		omax = tape_to_ms(xroar.tape_interface->tape_output, xroar.tape_interface->tape_output->size);
		opos = tape_to_ms(xroar.tape_interface->tape_output, xroar.tape_interface->tape_output->offset);
	}
	if (uigtk3_update_adjustment(uigtk3, "output_file_adjustment", 0.0, (gdouble)omax, (gdouble)opos)) {
		uigtk3_label_set_text(uigtk3, "output_file_time", ms_to_string(opos));
	}

	ev_update_tape_counters.at_tick = event_current_tick + EVENT_MS(500);
	event_queue(&UI_EVENT_LIST, &ev_update_tape_counters);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Helper functions

// Clear program list

static void clear_program_list(struct uigtk3_dialog *dlg) {
	struct ui_gtk3_interface *uigtk3 = dlg->uigtk3;
	GtkListStore *ls = GTK_LIST_STORE(gtk_builder_get_object(uigtk3->builder, "input_file_list_store"));

	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls), &iter)) {
		do {
			struct tape_file *file;
			gtk_tree_model_get(GTK_TREE_MODEL(ls), &iter, TC_FILE_POINTER, &file, -1);
			g_free(file);
		} while (gtk_tree_model_iter_next(GTK_TREE_MODEL(ls), &iter));
	}
	gtk_list_store_clear(ls);
}

// Populate program list

static void update_program_list(struct uigtk3_dialog *dlg) {
	struct ui_gtk3_interface *uigtk3 = dlg->uigtk3;
	GtkListStore *ls = GTK_LIST_STORE(gtk_builder_get_object(uigtk3->builder, "input_file_list_store"));

	if (!xroar.tape_interface || !xroar.tape_interface->tape_input) {
		clear_program_list(dlg);
		return;
	}

	// If there's anything in the tree already, don't scan it again
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls), &iter)) {
		return;
	}

	struct tape_file *file;
	long old_offset = tape_tell(xroar.tape_interface->tape_input);
	tape_rewind(xroar.tape_interface->tape_input);
	while ((file = tape_file_next(xroar.tape_interface->tape_input, 1))) {
		int ms = tape_to_ms(xroar.tape_interface->tape_input, file->offset);
		gchar *timestr = ms_to_string(ms);
		gtk_list_store_append(ls, &iter);
		gtk_list_store_set(ls, &iter,
				   TC_FILENAME, file->name,
				   TC_POSITION, timestr,
				   TC_FILE_POINTER, file,
				   -1);
		free(file);
	}
	tape_seek(xroar.tape_interface->tape_input, old_offset, SEEK_SET);
}

// Interpret scroll parameters

static void tc_seek(struct tape *tape, GtkScrollType scroll, gdouble value) {
	if (tape) {
		int seekms = 0;
		switch (scroll) {
			case GTK_SCROLL_STEP_BACKWARD:
				seekms = tape_to_ms(tape, tape->offset) - 1000;
				break;
			case GTK_SCROLL_STEP_FORWARD:
				seekms = tape_to_ms(tape, tape->offset) + 1000;
				break;
			case GTK_SCROLL_PAGE_BACKWARD:
				seekms = tape_to_ms(tape, tape->offset) - 5000;
				break;
			case GTK_SCROLL_PAGE_FORWARD:
				seekms = tape_to_ms(tape, tape->offset) + 5000;
				break;
			case GTK_SCROLL_JUMP:
				seekms = (int)value;
				break;
			default:
				return;
		}
		if (seekms < 0) return;
		long seek_to = tape_ms_to(tape, seekms);
		if (seek_to > tape->size) seek_to = tape->size;
		tape_seek(tape, seek_to, SEEK_SET);
	}
}

// Convert milliseconds to string of form mm:ss

static gchar *ms_to_string(int ms) {
	static gchar timestr[9];
	int min, sec;
	sec = ms / 1000;
	min = sec / 60;
	sec %= 60;
	min %= 60;
	snprintf(timestr, sizeof(timestr), "%02d:%02d", min, sec);
	return timestr;
}
