/** \file
 *
 *  \brief GTK+ 3 user-interface common functions.
 *
 *  \copyright Copyright 2014-2024 Ciaran Anscomb
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

#ifndef XROAR_GTK3_COMMON_H_
#define XROAR_GTK3_COMMON_H_

#include <stdint.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "events.h"
#include "ui.h"
#include "vo.h"

struct slist;
struct uigtk3_dialog;
struct xconfig_enum;

#define GTK_KBD_JS_MAX_AXES (4)
#define GTK_KBD_JS_MAX_BUTTONS (4)

struct gtk_kbd_js_axis;
struct gtk_kbd_js_button;

struct ui_gtk3_interface;

// The various bits needed when constructing one-of-many dynamic menus
struct uigtk3_radio_menu {
	struct ui_gtk3_interface *uigtk3;
	char *path;
	char *action_group_name;
	GtkActionGroup *action_group;
	guint merge_id;
	GCallback callback;
};

// Combo boxes can either be over-complicated ComboBox (requiring ListStores,
// CellRenderers and who knows what else) or ComboBoxText which doesn't have
// _any_ data other than index and label.  Bridge the gap here by storing
// values for a ComboBoxText.

struct uigtk3_cbt_value {
	struct ui_gtk3_interface *uigtk3;
	char *name;
	GtkComboBoxText *cbt;
	unsigned nvalues;
	void **values;
};

// All the data associated with the GTK+ UI.

struct ui_gtk3_interface {
	struct ui_interface public;

	struct ui_cfg *cfg;

	// UI builders
	GtkBuilder *builder;
	GtkUIManager *menu_manager;

	// Top window
	GtkWidget *top_window;
	GdkDisplay *display;  // Display for top window
	GtkWidget *drawing_area;

	// Menubar
	GtkWidget *menubar;

	// Dynamic menus
	struct uigtk3_radio_menu *tv_input_radio_menu;
	struct uigtk3_radio_menu *ccr_radio_menu;
	struct uigtk3_radio_menu *machine_radio_menu;
	struct uigtk3_radio_menu *cart_radio_menu;
	struct uigtk3_radio_menu *keymap_radio_menu;
	struct uigtk3_radio_menu *joy_right_radio_menu;
	struct uigtk3_radio_menu *joy_left_radio_menu;
	struct uigtk3_radio_menu *hkbd_layout_radio_menu;
	struct uigtk3_radio_menu *hkbd_lang_radio_menu;

	// Track radio menus to free
	struct slist *rm_list;

	// Track Combo Boxes with Values to free
	struct slist *cbtv_list;

	// Top level messenger client id
	int msgr_client_id;

	// Window geometry
	_Bool user_specified_geometry;

	// Printer state
	struct {
		char *pipe;
	} printer;

	// Keyboard state
	struct {
		// Is a non-preempted control key pressed?
		_Bool control;
		struct gtk_kbd_js_axis *enabled_axis[GTK_KBD_JS_MAX_AXES];
		struct gtk_kbd_js_button *enabled_button[GTK_KBD_JS_MAX_BUTTONS];
	} keyboard;

	// Cursor hiding
	_Bool cursor_hidden;
	GdkCursor *old_cursor;
	GdkCursor *blank_cursor;

};

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_gtk3 and make it available
// globally.
extern struct ui_gtk3_interface *global_uigtk3;

void gtk3_create_about_window(struct ui_gtk3_interface *);

_Bool gtk3_vo_init(struct ui_gtk3_interface *);

extern struct joystick_module *gtk3_js_modlist[];

#ifndef GLIB_VERSION_2_50
#define g_abort() abort()
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI builder helpers

void uigtk3_add_from_resource(struct ui_gtk3_interface *uigtk3, const gchar *path);

void do_uigtk3_signal_connect(struct ui_gtk3_interface *uigtk3, const gchar *o_name,
			      const gchar *detailed_signal,
			      GCallback c_handler,
			      gpointer data);

#define uigtk3_signal_connect(uigtk3, o_name, detailed_signal, c_handler, data) \
	do_uigtk3_signal_connect((uigtk3), (o_name), \
				 (detailed_signal), G_CALLBACK(c_handler), (data))

// Notify-only menu manager update helpers.
//
// Blocks callback so that no further action is taken.

void uigtk3_notify_radio_menu_set_current_value(struct uigtk3_radio_menu *rm, gint v);

void uigtk3_notify_toggle_action_set_active(struct ui_gtk3_interface *uigtk3,
					    const gchar *path, gboolean v, gpointer func);

// Menu manager update helpers

gboolean uigtk3_toggle_action_get_active(struct ui_gtk3_interface *uigtk3, const gchar *path);

void uigtk3_toggle_action_set_active(struct ui_gtk3_interface *uigtk3, const gchar *path,
				     gboolean v);

// UI update helpers

void uigtk3_editable_set_editable(struct ui_gtk3_interface *uigtk3, const gchar *e_name,
				  gboolean is_editable);

void uigtk3_label_set_text(struct ui_gtk3_interface *uigtk3, const gchar *l_name,
			   const gchar *str);

void uigtk3_spin_button_set_value(struct ui_gtk3_interface *,
				  const gchar *sb_name, gdouble value);

void uigtk3_toggle_button_set_active(struct ui_gtk3_interface *uigtk3, const gchar *tb_name,
                                     gboolean v);

void uigtk3_widget_hide(struct ui_gtk3_interface *uigtk3, const gchar *w_name);

void uigtk3_widget_set_sensitive(struct ui_gtk3_interface *uigtk3, const gchar *w_name,
				 gboolean sensitive);

void uigtk3_widget_show(struct ui_gtk3_interface *uigtk3, const gchar *w_name);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Update an adjustment.  Returns true if any parameter changed.

_Bool uigtk3_update_adjustment(struct ui_gtk3_interface *uigtk3, const gchar *a_name,
			       gdouble min, gdouble max, gdouble pos);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// [Re-]build a menu from an xconfig_enum

struct uigtk3_radio_menu *uigtk3_radio_menu_new(struct ui_gtk3_interface *uigtk3,
						const char *path, GCallback callback);

void uigtk3_radio_menu_free_void(void *);
void uigtk3_radio_menu_free(struct uigtk3_radio_menu *);

void uigtk3_radio_menu_set_current_value(struct uigtk3_radio_menu *rm, gint v);

void uigtk3_update_radio_menu_from_enum(struct uigtk3_radio_menu *rm,
					struct xconfig_enum *xc_enum,
					const char *name_fmt, const char *label_fmt,
					int selected);

// Create ComboBoxText with Value store.  Added to UI list for freeing.

struct uigtk3_cbt_value *uigtk3_cbt_value_new(struct ui_gtk3_interface *,
					      const char *name);

// Create ComboBoxText with Value store from xconfig_enum

struct uigtk3_cbt_value *uigtk3_cbt_value_from_enum(struct ui_gtk3_interface *,
						    const char *name,
						    struct xconfig_enum *xc_list,
						    GCallback changed_handler);

struct uigtk3_cbt_value *uigtk3_cbt_value_by_name(struct ui_gtk3_interface *uigtk3,
						  const char *name);

// Free ComboBoxText with Value.  Doesn't remove it from the UI list, that's
// done all in one go within ui_gtk3_free().

void uigtk3_cbt_value_free_void(void *);
void uigtk3_cbt_value_free(struct uigtk3_cbt_value *);

void *uigtk3_cbt_value_get_value(struct uigtk3_cbt_value *);

void uigtk3_cbt_value_set_value(struct uigtk3_cbt_value *, void *value);

#define uigtk3_cbt_value_by_name_get_value(u,n) uigtk3_cbt_value_get_value(uigtk3_cbt_value_by_name((u), (n)))

#define uigtk3_cbt_value_by_name_set_value(u,n,v) uigtk3_cbt_value_set_value(uigtk3_cbt_value_by_name((u), (n)), (v))

void uigtk3_free_action_group(GtkActionGroup *action_group);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// General helpers

// Escape underscores in a string, making it suitable to use as a menu label.
// Allocates new space that should be freed with g_free().

char *uigtk3_escape_underscores(const char *);

#endif
