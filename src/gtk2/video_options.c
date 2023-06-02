/** \file
 *
 *  \brief GTK+ 2 video options window.
 *
 *  \copyright Copyright 2023 Ciaran Anscomb
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

#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "ao.h"
#include "sound.h"
#include "vo.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/video_options.h"

// Actions
static void vo_change_gain(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_brightness(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_contrast(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_saturation(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_hue(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_cmp_fs(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_fsc(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_system(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data);

// Video Options control widgets
static GtkWidget *vo_window = NULL;
static GtkSpinButton *vo_gain = NULL;
static GtkSpinButton *vo_brightness = NULL;
static GtkSpinButton *vo_contrast = NULL;
static GtkSpinButton *vo_saturation = NULL;
static GtkSpinButton *vo_hue = NULL;
static GtkComboBoxText *cbt_cmp_fs = NULL;
static GtkComboBoxText *cbt_cmp_fsc = NULL;
static GtkComboBoxText *cbt_cmp_system = NULL;
static GtkToggleButton *tb_cmp_colour_killer = NULL;

// Signal handlers
static gboolean hide_vo_window(GtkWidget *widget, GdkEvent *event, gpointer user_data);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void gtk2_vo_create_window(struct ui_gtk2_interface *uigtk2) {
	GError *error = NULL;
	GtkBuilder *builder = gtk_builder_new();

	GBytes *res_video_options = g_resources_lookup_data("/uk/org/6809/xroar/gtk2/video_options.ui", 0, NULL);
	if (!gtk_builder_add_from_string(builder, g_bytes_get_data(res_video_options, NULL), -1, &error)) {
		g_warning("Couldn't create UI: %s", error->message);
		g_error_free(error);
		return;
	}
	g_bytes_unref(res_video_options);

	// Extract UI elements modified elsewhere
	vo_window = GTK_WIDGET(gtk_builder_get_object(builder, "vo_window"));
	vo_gain = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_gain"));
	vo_brightness = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_brightness"));
	vo_contrast = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_contrast"));
	vo_saturation = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_saturation"));
	vo_hue = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_hue"));
	cbt_cmp_fs = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "cbt_cmp_fs"));
	cbt_cmp_fsc = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "cbt_cmp_fsc"));
	cbt_cmp_system = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder, "cbt_cmp_system"));
	tb_cmp_colour_killer = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "tb_cmp_colour_killer"));

	// Build lists
	for (unsigned i = 0; i < NUM_VO_RENDER_FS; i++) {
		gtk_combo_box_text_append_text(cbt_cmp_fs, vo_render_fs_name[i]);
	}
	for (unsigned i = 0; i < NUM_VO_RENDER_FSC; i++) {
		gtk_combo_box_text_append_text(cbt_cmp_fsc, vo_render_fsc_name[i]);
	}
	for (unsigned i = 0; i < NUM_VO_RENDER_SYSTEM; i++) {
		gtk_combo_box_text_append_text(cbt_cmp_system, vo_render_system_name[i]);
	}

	// Connect signals
	g_signal_connect(vo_window, "key-press-event", G_CALLBACK(gtk2_dummy_keypress), uigtk2);
	g_signal_connect(vo_window, "delete-event", G_CALLBACK(hide_vo_window), uigtk2);
	g_signal_connect(vo_gain, "value-changed", G_CALLBACK(vo_change_gain), uigtk2);
	g_signal_connect(vo_brightness, "value-changed", G_CALLBACK(vo_change_brightness), uigtk2);
	g_signal_connect(vo_contrast, "value-changed", G_CALLBACK(vo_change_contrast), uigtk2);
	g_signal_connect(vo_saturation, "value-changed", G_CALLBACK(vo_change_saturation), uigtk2);
	g_signal_connect(vo_hue, "value-changed", G_CALLBACK(vo_change_hue), uigtk2);
	g_signal_connect(cbt_cmp_fs, "changed", G_CALLBACK(vo_change_cmp_fs), uigtk2);
	g_signal_connect(cbt_cmp_fsc, "changed", G_CALLBACK(vo_change_cmp_fsc), uigtk2);
	g_signal_connect(cbt_cmp_system, "changed", G_CALLBACK(vo_change_cmp_system), uigtk2);
	g_signal_connect(tb_cmp_colour_killer, "toggled", G_CALLBACK(vo_change_cmp_colour_killer), uigtk2);

	// In case any signals remain...
	gtk_builder_connect_signals(builder, uigtk2);
	g_object_unref(builder);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - update values in UI

void gtk2_vo_update_gain(struct ui_gtk2_interface *uigtk2, float value) {
	(void)uigtk2;
	uigtk2_notify_spin_button_set(vo_gain, value, vo_change_gain, uigtk2);
}

void gtk2_vo_update_brightness(struct ui_gtk2_interface *uigtk2, int value) {
	(void)uigtk2;
	uigtk2_notify_spin_button_set(vo_brightness, value, vo_change_brightness, uigtk2);
}

void gtk2_vo_update_contrast(struct ui_gtk2_interface *uigtk2, int value) {
	(void)uigtk2;
	uigtk2_notify_spin_button_set(vo_contrast, value, vo_change_contrast, uigtk2);
}

void gtk2_vo_update_saturation(struct ui_gtk2_interface *uigtk2, int value) {
	(void)uigtk2;
	uigtk2_notify_spin_button_set(vo_saturation, value, vo_change_saturation, uigtk2);
}

void gtk2_vo_update_hue(struct ui_gtk2_interface *uigtk2, int value) {
	(void)uigtk2;
	uigtk2_notify_spin_button_set(vo_hue, value, vo_change_hue, uigtk2);
}

void gtk2_vo_update_cmp_fs(struct ui_gtk2_interface *uigtk2, int value) {
	(void)uigtk2;
	gtk_combo_box_set_active(GTK_COMBO_BOX(cbt_cmp_fs), value);
}

void gtk2_vo_update_cmp_fsc(struct ui_gtk2_interface *uigtk2, int value) {
	(void)uigtk2;
	gtk_combo_box_set_active(GTK_COMBO_BOX(cbt_cmp_fsc), value);
}

void gtk2_vo_update_cmp_system(struct ui_gtk2_interface *uigtk2, int value) {
	(void)uigtk2;
	gtk_combo_box_set_active(GTK_COMBO_BOX(cbt_cmp_system), value);
}

void gtk2_vo_update_cmp_colour_killer(struct ui_gtk2_interface *uigtk2, int value) {
	(void)uigtk2;
	uigtk2_notify_toggle_button_set(tb_cmp_colour_killer, value, vo_change_cmp_colour_killer, uigtk2);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - signal handlers

void gtk2_vo_toggle_window(GtkToggleAction *current, gpointer user_data) {
	gboolean val = gtk_toggle_action_get_active(current);
	(void)user_data;
	if (val) {
		gtk_widget_show(vo_window);
	} else {
		gtk_widget_hide(vo_window);
	}
}

static gboolean hide_vo_window(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk2_interface *uigtk2 = user_data;
	GtkToggleAction *toggle = (GtkToggleAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ViewMenu/VideoOptions");
	gtk_toggle_action_set_active(toggle, 0);
	gtk_widget_hide(vo_window);
	return TRUE;
}

static void vo_change_gain(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	float value = (float)gtk_spin_button_get_value(spin_button);
	if (value < -49.9)
		value = -999.;
	if (xroar_ao_interface) {
		sound_set_gain(xroar_ao_interface->sound_interface, value);
	}
}

static void vo_change_brightness(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar_vo_interface) {
		DELEGATE_SAFE_CALL(xroar_vo_interface->set_brightness, value);
	}
}

static void vo_change_contrast(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar_vo_interface) {
		DELEGATE_SAFE_CALL(xroar_vo_interface->set_contrast, value);
	}
}

static void vo_change_saturation(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar_vo_interface) {
		DELEGATE_SAFE_CALL(xroar_vo_interface->set_saturation, value);
	}
}

static void vo_change_hue(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar_vo_interface) {
		DELEGATE_SAFE_CALL(xroar_vo_interface->set_hue, value);
	}
}

static void vo_change_cmp_fs(GtkComboBox *widget, gpointer user_data) {
        struct ui_gtk2_interface *uigtk2 = user_data;
        (void)uigtk2;
        int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
        if (xroar_vo_interface) {
                vo_set_cmp_fs(xroar_vo_interface, 0, value);
        }
}

static void vo_change_cmp_fsc(GtkComboBox *widget, gpointer user_data) {
        struct ui_gtk2_interface *uigtk2 = user_data;
        (void)uigtk2;
        int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
        if (xroar_vo_interface) {
                vo_set_cmp_fsc(xroar_vo_interface, 0, value);
        }
}

static void vo_change_cmp_system(GtkComboBox *widget, gpointer user_data) {
        struct ui_gtk2_interface *uigtk2 = user_data;
        (void)uigtk2;
        int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
        if (xroar_vo_interface) {
                vo_set_cmp_system(xroar_vo_interface, 0, value);
        }
}

static void vo_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = gtk_toggle_button_get_active(widget);
	vo_set_cmp_colour_killer(xroar_vo_interface, 0, value);
}
