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

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtk/gtk.h>

#include "ao.h"
#include "sound.h"
#include "vo.h"
#include "xroar.h"

#include "gtk3/common.h"
#include "gtk3/video_options.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Callbacks
static void vo_change_gain(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_brightness(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_contrast(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_saturation(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_hue(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_picture(GtkComboBox *widget, gpointer user_data);
static void vo_change_ntsc_scaling(GtkToggleButton *widget, gpointer user_data);
static void vo_change_cmp_renderer(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_fs(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_fsc(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_system(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data);

// Signal handlers
static gboolean hide_vo_window(GtkWidget *widget, GdkEvent *event, gpointer user_data);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void gtk3_vo_create_window(struct ui_gtk3_interface *uigtk3) {
	uigtk3_add_from_resource(uigtk3, "/uk/org/6809/xroar/gtk3/video_options.ui");

	// Build lists
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_picture", vo_viewport_list, G_CALLBACK(vo_change_picture));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_cmp_renderer", vo_cmp_ccr_list, G_CALLBACK(vo_change_cmp_renderer));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_cmp_fs", vo_render_fs_list, G_CALLBACK(vo_change_cmp_fs));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_cmp_fsc", vo_render_fsc_list, G_CALLBACK(vo_change_cmp_fsc));
	uigtk3_cbt_value_from_enum(uigtk3, "cbt_cmp_system", vo_render_system_list, G_CALLBACK(vo_change_cmp_system));

	// Connect signals
	uigtk3_signal_connect(uigtk3, "vo_window", "delete-event", G_CALLBACK(hide_vo_window), uigtk3);
	uigtk3_signal_connect(uigtk3, "vo_window", "key-press-event", G_CALLBACK(gtk3_dummy_keypress), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_gain", "value-changed", G_CALLBACK(vo_change_gain), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_brightness", "value-changed", G_CALLBACK(vo_change_brightness), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_contrast", "value-changed", G_CALLBACK(vo_change_contrast), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_saturation", "value-changed", G_CALLBACK(vo_change_saturation), uigtk3);
	uigtk3_signal_connect(uigtk3, "sb_hue", "value-changed", G_CALLBACK(vo_change_hue), uigtk3);
	uigtk3_signal_connect(uigtk3, "tb_ntsc_scaling", "toggled", G_CALLBACK(vo_change_ntsc_scaling), uigtk3);
	uigtk3_signal_connect(uigtk3, "tb_cmp_colour_killer", "toggled", G_CALLBACK(vo_change_cmp_colour_killer), uigtk3);
}

void gtk3_vo_toggle_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	if (gtk_toggle_action_get_active(current)) {
		uigtk3_widget_show(uigtk3, "vo_window");
	} else {
		uigtk3_widget_hide(uigtk3, "vo_window");
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - update values in UI

void gtk3_vo_update_state(struct ui_gtk3_interface *uigtk3,
			  int tag, int value, const void *data) {
	(void)data;
	switch (tag) {
	case ui_tag_gain:
		uigtk3_notify_spin_button_set_value(uigtk3, "sb_gain", *(float *)data, vo_change_gain);
		break;

	case ui_tag_brightness:
		uigtk3_notify_spin_button_set_value(uigtk3, "sb_brightness", value, vo_change_brightness);
		break;

	case ui_tag_contrast:
		uigtk3_notify_spin_button_set_value(uigtk3, "sb_contrast", value, vo_change_contrast);
		break;

	case ui_tag_saturation:
		uigtk3_notify_spin_button_set_value(uigtk3, "sb_saturation", value, vo_change_saturation);
		break;

	case ui_tag_hue:
		uigtk3_notify_spin_button_set_value(uigtk3, "sb_hue", value, vo_change_hue);
		break;

	case ui_tag_picture:
		uigtk3_cbt_value_by_name_set_value(uigtk3, "cbt_picture", (void *)(intptr_t)value);
		break;

	case ui_tag_ntsc_scaling:
		uigtk3_notify_toggle_button_set_active(uigtk3, "tb_ntsc_scaling", value, vo_change_ntsc_scaling);
		break;

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
		uigtk3_notify_toggle_button_set_active(uigtk3, "tb_cmp_colour_killer", value, vo_change_cmp_colour_killer);
		break;

	default:
		break;
	}
}

void gtk3_vo_update_cmp_renderer(struct ui_gtk3_interface *uigtk3, int value) {
	uigtk3_cbt_value_by_name_set_value(uigtk3, "cbt_cmp_renderer", (void *)(intptr_t)value);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - signal handlers

static gboolean hide_vo_window(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk3_interface *uigtk3 = user_data;
	uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/ViewMenu/VideoOptions", 0);
	uigtk3_widget_hide(uigtk3, "vo_window");
	return TRUE;
}

static void vo_change_gain(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	float value = (float)gtk_spin_button_get_value(spin_button);
	if (value < -49.9)
		value = -999.;
	if (xroar.ao_interface) {
		sound_set_gain(xroar.ao_interface->sound_interface, value);
	}
}

static void vo_change_brightness(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_brightness, value);
	}
}

static void vo_change_contrast(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_contrast, value);
	}
}

static void vo_change_saturation(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_saturation, value);
	}
}

static void vo_change_hue(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_hue, value);
	}
}

static void vo_change_picture(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	xroar_set_picture(0, value);
}

static void vo_change_ntsc_scaling(GtkToggleButton *widget, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	int value = gtk_toggle_button_get_active(widget);
	vo_set_ntsc_scaling(xroar.vo_interface, 0, value);
}

static void vo_change_cmp_renderer(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	vo_set_cmp_ccr(xroar.vo_interface, 1, value);
}

static void vo_change_cmp_fs(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	vo_set_cmp_fs(xroar.vo_interface, 0, value);
}

static void vo_change_cmp_fsc(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	vo_set_cmp_fsc(xroar.vo_interface, 0, value);
}

static void vo_change_cmp_system(GtkComboBox *widget, gpointer user_data) {
	(void)widget;
	struct uigtk3_cbt_value *cbtv = user_data;
	int value = (intptr_t)uigtk3_cbt_value_get_value(cbtv);
	vo_set_cmp_system(xroar.vo_interface, 0, value);
}

static void vo_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	int value = gtk_toggle_button_get_active(widget);
	vo_set_cmp_colour_killer(xroar.vo_interface, 0, value);
}
