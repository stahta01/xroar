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

#include "vo.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/video_options.h"

// Actions
static void vo_change_brightness(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_contrast(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_saturation(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_hue(GtkSpinButton *spin_button, gpointer user_data);

// Video Options control widgets
static GtkWidget *vo_window = NULL;
static GtkSpinButton *vo_brightness = NULL;
static GtkSpinButton *vo_contrast = NULL;
static GtkSpinButton *vo_saturation = NULL;
static GtkSpinButton *vo_hue = NULL;

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
	vo_brightness = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_brightness"));
	vo_contrast = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_contrast"));
	vo_saturation = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_saturation"));
	vo_hue = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sb_hue"));

	// Connect signals
	g_signal_connect(vo_window, "key-press-event", G_CALLBACK(gtk2_dummy_keypress), uigtk2);
	g_signal_connect(vo_window, "delete-event", G_CALLBACK(hide_vo_window), uigtk2);
	g_signal_connect(vo_brightness, "value-changed", G_CALLBACK(vo_change_brightness), uigtk2);
	g_signal_connect(vo_contrast, "value-changed", G_CALLBACK(vo_change_contrast), uigtk2);
	g_signal_connect(vo_saturation, "value-changed", G_CALLBACK(vo_change_saturation), uigtk2);
	g_signal_connect(vo_hue, "value-changed", G_CALLBACK(vo_change_hue), uigtk2);

	// In case any signals remain...
	gtk_builder_connect_signals(builder, uigtk2);
	g_object_unref(builder);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - update values in UI

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
