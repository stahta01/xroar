/** \file
 *
 *  \brief GTK+ 3 video options window.
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

#include "ao.h"
#include "machine.h"
#include "messenger.h"
#include "sound.h"
#include "ui.h"
#include "vo.h"
#include "xroar.h"

#include "gtk3/common.h"
#include "gtk3/dialog.h"
#include "gtk3/event_handlers.h"
#include "gtk3/video_options.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Callbacks

static void tv_change_gain(GtkSpinButton *spin_button, gpointer user_data);
static void tv_change_brightness(GtkSpinButton *spin_button, gpointer user_data);
static void tv_change_contrast(GtkSpinButton *spin_button, gpointer user_data);
static void tv_change_saturation(GtkSpinButton *spin_button, gpointer user_data);
static void tv_change_hue(GtkSpinButton *spin_button, gpointer user_data);
static void tv_change_tv_input(GtkComboBox *widget, gpointer user_data);
static void tv_change_picture(GtkComboBox *widget, gpointer user_data);
static void tv_change_ntsc_scaling(GtkToggleButton *widget, gpointer user_data);
static void tv_change_cmp_renderer(GtkComboBox *widget, gpointer user_data);
static void tv_change_cmp_fs(GtkComboBox *widget, gpointer user_data);
static void tv_change_cmp_fsc(GtkComboBox *widget, gpointer user_data);
static void tv_change_cmp_system(GtkComboBox *widget, gpointer user_data);
static void tv_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data);

// UI message reception

static void tv_ui_state_notify(void *sptr, int tag, void *smsg);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create dialog window

struct uigtk3_dialog *gtk3_tv_dialog_new(struct ui_gtk3_interface *uigtk3) {
	struct uigtk3_dialog *dlg = uigtk3_dialog_new(uigtk3, "/uk/org/6809/xroar/gtk3/video_options.ui", "vo_window", ui_tag_tv_dialog);

	// Join each UI group we're interested in

	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_fs, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_fsc, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_system, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_colour_killer, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_ccr, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_picture, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_ntsc_scaling, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_brightness, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_contrast, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_saturation, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_hue, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_gain, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk3));

	// Build lists

	uigtk3_cbt_value_from_enum(uigtk3, "cbt_tv_input", machine_tv_input_list, G_CALLBACK(tv_change_tv_input));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_picture", vo_viewport_list, G_CALLBACK(tv_change_picture));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_cmp_renderer", vo_cmp_ccr_list, G_CALLBACK(tv_change_cmp_renderer));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_cmp_fs", vo_render_fs_list, G_CALLBACK(tv_change_cmp_fs));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_cmp_fsc", vo_render_fsc_list, G_CALLBACK(tv_change_cmp_fsc));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_cmp_system", vo_render_system_list, G_CALLBACK(tv_change_cmp_system));

	// Connect signals

	uigtk3_signal_connect(uigtk3, "sb_gain", "value-changed", G_CALLBACK(tv_change_gain), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_brightness", "value-changed", G_CALLBACK(tv_change_brightness), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_contrast", "value-changed", G_CALLBACK(tv_change_contrast), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_saturation", "value-changed", G_CALLBACK(tv_change_saturation), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_hue", "value-changed", G_CALLBACK(tv_change_hue), uigtk3);
	uigtk3_signal_connect(uigtk3, "tb_ntsc_scaling", "toggled", G_CALLBACK(tv_change_ntsc_scaling), uigtk3);
	uigtk3_signal_connect(uigtk3, "tb_cmp_colour_killer", "toggled", G_CALLBACK(tv_change_cmp_colour_killer), uigtk3);

	return dlg;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void tv_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct ui_gtk3_interface *uigtk3 = sptr;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	const void *data = uimsg->data;

	switch (tag) {

	// Video

	case ui_tag_cmp_fs:
		uigtk3_cbt_value_by_name_set_value(uigtk3, "cbt_cmp_fs", (void *)(intptr_t)value);
		break;

	case ui_tag_cmp_fsc:
		uigtk3_cbt_value_by_name_set_value(uigtk3, "cbt_cmp_fsc", (void *)(intptr_t)value);
		break;

	case ui_tag_cmp_system:
		uigtk3_cbt_value_by_name_set_value(uigtk3, "cbt_cmp_system", (void *)(intptr_t)value);
		break;

	case ui_tag_cmp_colour_killer:
		uigtk3_toggle_button_set_active(uigtk3, "tb_cmp_colour_killer", value);
		break;

	case ui_tag_ccr:
		uigtk3_cbt_value_by_name_set_value(uigtk3, "cbt_cmp_renderer", (void *)(intptr_t)value);
		break;

	case ui_tag_picture:
		uigtk3_cbt_value_by_name_set_value(uigtk3, "cbt_picture", (void *)(intptr_t)value);
		break;

	case ui_tag_ntsc_scaling:
		uigtk3_toggle_button_set_active(uigtk3, "tb_ntsc_scaling", value);
		break;

	case ui_tag_tv_input:
		uigtk3_cbt_value_by_name_set_value(uigtk3, "cbt_tv_input", (void *)(intptr_t)value);
		break;

	case ui_tag_brightness:
		uigtk3_spin_button_set_value(uigtk3, "sb_brightness", value);
		break;

	case ui_tag_contrast:
		uigtk3_spin_button_set_value(uigtk3, "sb_contrast", value);
		break;

	case ui_tag_saturation:
		uigtk3_spin_button_set_value(uigtk3, "sb_saturation", value);
		break;

	case ui_tag_hue:
		uigtk3_spin_button_set_value(uigtk3, "sb_hue", value);
		break;

	// Audio

	case ui_tag_gain:
		uigtk3_spin_button_set_value(uigtk3, "sb_gain", *(float *)data);
		break;

	default:
		break;
	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Callbacks

static void tv_change_gain(GtkSpinButton *spin_button, gpointer user_data) {
	(void)user_data;
	float value = (float)gtk_spin_button_get_value(spin_button);
	ui_update_state(-1, ui_tag_gain, 0, &value);
}

static void tv_change_brightness(GtkSpinButton *spin_button, gpointer user_data) {
	(void)user_data;
	int value = (int)gtk_spin_button_get_value(spin_button);
	ui_update_state(-1, ui_tag_brightness, value, NULL);
}

static void tv_change_contrast(GtkSpinButton *spin_button, gpointer user_data) {
	(void)user_data;
	int value = (int)gtk_spin_button_get_value(spin_button);
	ui_update_state(-1, ui_tag_contrast, value, NULL);
}

static void tv_change_saturation(GtkSpinButton *spin_button, gpointer user_data) {
	(void)user_data;
	int value = (int)gtk_spin_button_get_value(spin_button);
	ui_update_state(-1, ui_tag_saturation, value, NULL);
}

static void tv_change_hue(GtkSpinButton *spin_button, gpointer user_data) {
	(void)user_data;
	int value = (int)gtk_spin_button_get_value(spin_button);
	ui_update_state(-1, ui_tag_hue, value, NULL);
}

static void tv_change_tv_input(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_tv_input, value, NULL);
}

static void tv_change_picture(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_picture, value, NULL);
}

static void tv_change_ntsc_scaling(GtkToggleButton *widget, gpointer user_data) {
	(void)user_data;
	int value = gtk_toggle_button_get_active(widget);
	ui_update_state(-1, ui_tag_ntsc_scaling, value, NULL);
}

static void tv_change_cmp_renderer(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_ccr, value, NULL);
}

static void tv_change_cmp_fs(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_cmp_fs, value, NULL);
}

static void tv_change_cmp_fsc(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_cmp_fsc, value, NULL);
}

static void tv_change_cmp_system(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_cmp_system, value, NULL);
}

static void tv_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data) {
	(void)user_data;
	int value = gtk_toggle_button_get_active(widget);
	ui_update_state(-1, ui_tag_cmp_colour_killer, value, NULL);
}
