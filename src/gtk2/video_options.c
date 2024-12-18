/** \file
 *
 *  \brief GTK+ 2 video options window.
 *
 *  \copyright Copyright 2023-2024 Ciaran Anscomb
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "ao.h"
#include "machine.h"
#include "messenger.h"
#include "sound.h"
#include "ui.h"
#include "vo.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/dialog.h"
#include "gtk2/event_handlers.h"
#include "gtk2/video_options.h"

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

struct uigtk2_dialog *gtk2_tv_dialog_new(struct ui_gtk2_interface *uigtk2) {
	struct uigtk2_dialog *dlg = uigtk2_dialog_new(uigtk2, "/uk/org/6809/xroar/gtk2/video_options.ui", "vo_window", ui_tag_tv_dialog);

	// Join each UI group we're interested in

	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_fs, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_fsc, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_system, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_colour_killer, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_ccr, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_picture, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_ntsc_scaling, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_brightness, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_contrast, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_saturation, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_hue, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_gain, MESSENGER_NOTIFY_DELEGATE(tv_ui_state_notify, uigtk2));

	// Build lists

	uigtk2_cbt_value_from_enum(uigtk2, "cbt_tv_input", machine_tv_input_list, G_CALLBACK(tv_change_tv_input));
	uigtk2_cbt_value_from_enum(uigtk2, "cbt_picture", vo_viewport_list, G_CALLBACK(tv_change_picture));
	uigtk2_cbt_value_from_enum(uigtk2, "cbt_cmp_renderer", vo_cmp_ccr_list, G_CALLBACK(tv_change_cmp_renderer));
	uigtk2_cbt_value_from_enum(uigtk2, "cbt_cmp_fs", vo_render_fs_list, G_CALLBACK(tv_change_cmp_fs));
	uigtk2_cbt_value_from_enum(uigtk2, "cbt_cmp_fsc", vo_render_fsc_list, G_CALLBACK(tv_change_cmp_fsc));
	uigtk2_cbt_value_from_enum(uigtk2, "cbt_cmp_system", vo_render_system_list, G_CALLBACK(tv_change_cmp_system));

	// Connect signals

	uigtk2_signal_connect(uigtk2, "sb_gain", "value-changed", G_CALLBACK(tv_change_gain), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_brightness", "value-changed", G_CALLBACK(tv_change_brightness), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_contrast", "value-changed", G_CALLBACK(tv_change_contrast), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_saturation", "value-changed", G_CALLBACK(tv_change_saturation), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_hue", "value-changed", G_CALLBACK(tv_change_hue), uigtk2);
	uigtk2_signal_connect(uigtk2, "tb_ntsc_scaling", "toggled", G_CALLBACK(tv_change_ntsc_scaling), uigtk2);
	uigtk2_signal_connect(uigtk2, "tb_cmp_colour_killer", "toggled", G_CALLBACK(tv_change_cmp_colour_killer), uigtk2);

	return dlg;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void tv_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	const void *data = uimsg->data;

	switch (tag) {

	// Video

	case ui_tag_cmp_fs:
		uigtk2_cbt_value_by_name_set_value(uigtk2, "cbt_cmp_fs", (void *)(intptr_t)value);
		break;

	case ui_tag_cmp_fsc:
		uigtk2_cbt_value_by_name_set_value(uigtk2, "cbt_cmp_fsc", (void *)(intptr_t)value);
		break;

	case ui_tag_cmp_system:
		uigtk2_cbt_value_by_name_set_value(uigtk2, "cbt_cmp_system", (void *)(intptr_t)value);
		break;

	case ui_tag_cmp_colour_killer:
		uigtk2_toggle_button_set_active(uigtk2, "tb_cmp_colour_killer", value);
		break;

	case ui_tag_ccr:
		uigtk2_cbt_value_by_name_set_value(uigtk2, "cbt_cmp_renderer", (void *)(intptr_t)value);
		break;

	case ui_tag_picture:
		uigtk2_cbt_value_by_name_set_value(uigtk2, "cbt_picture", (void *)(intptr_t)value);
		break;

	case ui_tag_ntsc_scaling:
		uigtk2_toggle_button_set_active(uigtk2, "tb_ntsc_scaling", value);
		break;

	case ui_tag_tv_input:
		uigtk2_cbt_value_by_name_set_value(uigtk2, "cbt_tv_input", (void *)(intptr_t)value);
		break;

	case ui_tag_brightness:
		uigtk2_spin_button_set_value(uigtk2, "sb_brightness", value);
		break;

	case ui_tag_contrast:
		uigtk2_spin_button_set_value(uigtk2, "sb_contrast", value);
		break;

	case ui_tag_saturation:
		uigtk2_spin_button_set_value(uigtk2, "sb_saturation", value);
		break;

	case ui_tag_hue:
		uigtk2_spin_button_set_value(uigtk2, "sb_hue", value);
		break;

	// Audio

	case ui_tag_gain:
		uigtk2_spin_button_set_value(uigtk2, "sb_gain", *(float *)data);
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
	struct uigtk2_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk2_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_tv_input, value, NULL);
}

static void tv_change_picture(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk2_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk2_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_picture, value, NULL);
}

static void tv_change_ntsc_scaling(GtkToggleButton *widget, gpointer user_data) {
	(void)user_data;
	int value = gtk_toggle_button_get_active(widget);
	ui_update_state(-1, ui_tag_ntsc_scaling, value, NULL);
}

static void tv_change_cmp_renderer(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk2_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk2_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_ccr, value, NULL);
}

static void tv_change_cmp_fs(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk2_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk2_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_cmp_fs, value, NULL);
}

static void tv_change_cmp_fsc(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk2_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk2_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_cmp_fsc, value, NULL);
}

static void tv_change_cmp_system(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk2_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk2_cbt_value_get_value(cbtv);
	ui_update_state(-1, ui_tag_cmp_system, value, NULL);
}

static void tv_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data) {
	(void)user_data;
	int value = gtk_toggle_button_get_active(widget);
	ui_update_state(-1, ui_tag_cmp_colour_killer, value, NULL);
}
