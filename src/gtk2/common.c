/** \file
 *
 *  \brief GTK+ 2 user-interface common functions.
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

#include "top-config.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "slist.h"
#include "xalloc.h"

#include "logging.h"
#include "xconfig.h"

#include "gtk2/common.h"

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_gtk2 and make it available
// globally.
struct ui_gtk2_interface *global_uigtk2 = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI builder helpers

FUNC_ATTR_NORETURN static void do_g_abort(const gchar *format, GError *error) {
	(void)format;
	if (error) {
		g_message("gtk_builder_add_from_resource() failed: %s", error->message);
		g_error_free(error);
	}
	g_abort();
}

void uigtk2_add_from_resource(struct ui_gtk2_interface *uigtk2, const gchar *path) {
	GError *error = NULL;
	GBytes *resource = g_resources_lookup_data(path, 0, &error);
	if (!resource) {
		do_g_abort("g_resources_lookup_data() failed: %s", error);
	}

	gsize xml_size;
	const gchar *xml = g_bytes_get_data(resource, &xml_size);

	if (gtk_builder_add_from_string(uigtk2->builder, xml, xml_size, &error) == 0) {
		do_g_abort("gtk_builder_add_from_string() failed: %s", error);
	}

	g_bytes_unref(resource);
}

void do_uigtk2_signal_connect(struct ui_gtk2_interface *uigtk2, const gchar *o_name,
			      const gchar *detailed_signal,
			      GCallback c_handler,
			      gpointer data) {
	GObject *o = gtk_builder_get_object(uigtk2->builder, o_name);
	g_signal_connect(o, detailed_signal, c_handler, data);
}

// Notify-only menu manager update helpers.
//
// Blocks callback so that no further action is taken.

void uigtk2_notify_radio_menu_set_current_value(struct uigtk2_radio_menu *rm, gint v) {
	if (!rm)
		return;
	GList *list = gtk_action_group_list_actions(rm->action_group);
	if (!list)
		return;
	GtkRadioAction *ra = GTK_RADIO_ACTION(list->data);
	g_list_free(list);
	g_signal_handlers_block_by_func(ra, rm->callback, rm->uigtk2);
	gtk_radio_action_set_current_value(ra, v);
	g_signal_handlers_unblock_by_func(ra, rm->callback, rm->uigtk2);
}

void uigtk2_notify_toggle_action_set_active(struct ui_gtk2_interface *uigtk2,
					    const gchar *path, gboolean v, gpointer func) {
	GtkToggleAction *ta = GTK_TOGGLE_ACTION(gtk_ui_manager_get_action(uigtk2->menu_manager, path));
	g_signal_handlers_block_by_func(ta, G_CALLBACK(func), uigtk2);
	gtk_toggle_action_set_active(ta, v);
	g_signal_handlers_unblock_by_func(ta, G_CALLBACK(func), uigtk2);
}

// Menu manager helpers

gboolean uigtk2_toggle_action_get_active(struct ui_gtk2_interface *uigtk2, const gchar *path) {
	GtkToggleAction *ta = GTK_TOGGLE_ACTION(gtk_ui_manager_get_action(uigtk2->menu_manager, path));
	return gtk_toggle_action_get_active(ta);
}

void uigtk2_toggle_action_set_active(struct ui_gtk2_interface *uigtk2, const gchar *path,
				     gboolean v) {
	GtkToggleAction *ta = GTK_TOGGLE_ACTION(gtk_ui_manager_get_action(uigtk2->menu_manager, path));
	gtk_toggle_action_set_active(ta, v);
}

// UI helpers

void uigtk2_editable_set_editable(struct ui_gtk2_interface *uigtk2, const gchar *e_name,
				 gboolean is_editable) {
	GtkEditable *e = GTK_EDITABLE(gtk_builder_get_object(uigtk2->builder, e_name));
	gtk_editable_set_editable(e, is_editable);
}

void uigtk2_label_set_text(struct ui_gtk2_interface *uigtk2, const gchar *l_name,
			   const gchar *str) {
	GtkLabel *l = GTK_LABEL(gtk_builder_get_object(uigtk2->builder, l_name));
	gtk_label_set_text(l, str);
}

void uigtk2_spin_button_set_value(struct ui_gtk2_interface *uigtk2,
				  const gchar *sb_name, gdouble value) {
	GtkSpinButton *sb = GTK_SPIN_BUTTON(gtk_builder_get_object(uigtk2->builder, sb_name));
	gtk_spin_button_set_value(sb, value);
}

void uigtk2_toggle_button_set_active(struct ui_gtk2_interface *uigtk2, const gchar *tb_name,
				     gboolean v) {
	GtkToggleButton *tb = GTK_TOGGLE_BUTTON(gtk_builder_get_object(uigtk2->builder, tb_name));
	gtk_toggle_button_set_active(tb, v);
}

void uigtk2_widget_hide(struct ui_gtk2_interface *uigtk2, const gchar *w_name) {
	GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(uigtk2->builder, w_name));
	gtk_widget_hide(w);
}

void uigtk2_widget_set_sensitive(struct ui_gtk2_interface *uigtk2, const gchar *w_name,
				 gboolean sensitive) {
	GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(uigtk2->builder, w_name));
	gtk_widget_set_sensitive(w, sensitive);
}

void uigtk2_widget_show(struct ui_gtk2_interface *uigtk2, const gchar *w_name) {
	GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(uigtk2->builder, w_name));
	gtk_widget_show(w);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

_Bool uigtk2_update_adjustment(struct ui_gtk2_interface *uigtk2, const gchar *a_name,
			       gdouble min, gdouble max, gdouble pos) {
	GtkAdjustment *a = GTK_ADJUSTMENT(gtk_builder_get_object(uigtk2->builder, a_name));
	if (!a)
		return 0;
	_Bool changed = 0;
	gdouble old_min = gtk_adjustment_get_lower(a);
	gdouble old_max = gtk_adjustment_get_upper(a);
	gdouble old_pos = gtk_adjustment_get_value(a);
	if (old_min != min) {
		gtk_adjustment_set_lower(a, min);
		changed |= 1;
	}
	if (old_max != max) {
		gtk_adjustment_set_upper(a, max);
		changed |= 1;
	}
	if (old_pos != pos) {
		gtk_adjustment_set_value(a, pos);
		changed |= 1;
	}
	return changed;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create all the required bits & bobs for keeping a dynamically created radio
// menu updated.

struct uigtk2_radio_menu *uigtk2_radio_menu_new(struct ui_gtk2_interface *uigtk2,
						const char *path, GCallback callback) {
	struct uigtk2_radio_menu *rm = g_malloc0(sizeof(*rm));
	static unsigned id = 0;
	rm->uigtk2 = uigtk2;
	rm->path = g_strdup(path);
	rm->action_group_name = g_strdup_printf("rm%u", id++);
	rm->action_group = gtk_action_group_new(rm->action_group_name);
	gtk_ui_manager_insert_action_group(uigtk2->menu_manager, rm->action_group, -1);
	rm->merge_id = gtk_ui_manager_new_merge_id(uigtk2->menu_manager);
	rm->callback = callback;
	uigtk2->rm_list = slist_prepend(uigtk2->rm_list, rm);
	return rm;
}

void uigtk2_radio_menu_free_void(void *sptr) {
	uigtk2_radio_menu_free((struct uigtk2_radio_menu *)sptr);
}

void uigtk2_radio_menu_free(struct uigtk2_radio_menu *rm) {
	struct ui_gtk2_interface *uigtk2 = rm->uigtk2;
	gtk_ui_manager_remove_action_group(uigtk2->menu_manager, rm->action_group);
	g_object_unref(rm->action_group);
	g_free(rm->action_group_name);
	g_free(rm->path);
	g_free(rm);
}

void uigtk2_radio_menu_set_current_value(struct uigtk2_radio_menu *rm, gint v) {
	if (!rm)
		return;
	GList *list = gtk_action_group_list_actions(rm->action_group);
	if (!list)
		return;
	GtkRadioAction *ra = GTK_RADIO_ACTION(list->data);
	g_list_free(list);
	gtk_radio_action_set_current_value(ra, v);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// [Re-]build a menu from an xconfig_enum

void uigtk2_update_radio_menu_from_enum(struct uigtk2_radio_menu *rm,
					struct xconfig_enum *xc_list,
					const char *name_fmt, const char *label_fmt,
					int selected) {
	if (!rm || !xc_list)
		return;
	struct ui_gtk2_interface *uigtk2 = rm->uigtk2;
	if (!name_fmt)
		name_fmt = "%s";
	if (!label_fmt)
		label_fmt = "%s";

	// Remove old entries
	uigtk2_free_action_group(rm->action_group);
	gtk_ui_manager_remove_ui(uigtk2->menu_manager, rm->merge_id);

	// Count entries
	unsigned num_entries = 0;
	for (struct xconfig_enum *iter = xc_list; iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		++num_entries;
	}

	// Add entries
	GtkRadioActionEntry *entries = g_malloc0(num_entries * sizeof(*entries));
	unsigned i = 0;
	for (struct xconfig_enum *iter = xc_list; iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		entries[i].name = g_strdup_printf(name_fmt, iter->name);
		entries[i].label = g_strdup_printf(label_fmt, iter->description);
		entries[i].value = iter->value;
		gtk_ui_manager_add_ui(uigtk2->menu_manager, rm->merge_id, rm->path, entries[i].name, entries[i].name, GTK_UI_MANAGER_MENUITEM, FALSE);
		++i;
	}
	gtk_action_group_add_radio_actions(rm->action_group, entries, num_entries, selected, rm->callback, uigtk2);

	// Free everything
	for (i = 0; i < num_entries; i++) {
		g_free((gpointer)entries[i].name);
		g_free((gpointer)entries[i].label);
	}
	g_free(entries);
}

static void remove_action_from_group(gpointer data, gpointer user_data) {
	GtkAction *action = data;
	GtkActionGroup *action_group = user_data;
	gtk_action_group_remove_action(action_group, action);
}

void uigtk2_free_action_group(GtkActionGroup *action_group) {
	GList *list = gtk_action_group_list_actions(action_group);
	g_list_foreach(list, remove_action_from_group, action_group);
	g_list_free(list);
}

// ComboBoxText with Value

struct uigtk2_cbt_value *uigtk2_cbt_value_new(struct ui_gtk2_interface *uigtk2,
					      const char *name) {
	GtkComboBoxText *cbt = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(uigtk2->builder, name));
	if (!cbt) {
		return NULL;
	}
	struct uigtk2_cbt_value *cbtv = g_malloc(sizeof(*cbt));
	*cbtv = (struct uigtk2_cbt_value){0};
	cbtv->uigtk2 = uigtk2;
	cbtv->name = xstrdup(name);
	cbtv->cbt = cbt;
	cbtv->nvalues = 0;
	uigtk2->cbtv_list = slist_prepend(uigtk2->cbtv_list, cbtv);
	return cbtv;
}

void uigtk2_cbt_value_free_void(void *sptr) {
	uigtk2_cbt_value_free((struct uigtk2_cbt_value *)sptr);
}

void uigtk2_cbt_value_free(struct uigtk2_cbt_value *cbtv) {
	if (cbtv->values) {
		free(cbtv->values);
	}
	free(cbtv->name);
	free(cbtv);
}

struct uigtk2_cbt_value *uigtk2_cbt_value_by_name(struct ui_gtk2_interface *uigtk2,
						  const char *name) {
	for (struct slist *iter = uigtk2->cbtv_list; iter; iter = iter->next) {
		struct uigtk2_cbt_value *cbtv = iter->data;
		if (0 == strcmp(cbtv->name, name)) {
			return cbtv;
		}
	}
	return NULL;
}

struct uigtk2_cbt_value *uigtk2_cbt_value_from_enum(struct ui_gtk2_interface *uigtk2,
						    const char *name,
						    struct xconfig_enum *xc_list,
						    GCallback changed_handler) {
	// Count entries
	unsigned nvalues = 0;
	for (struct xconfig_enum *iter = xc_list; iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		++nvalues;
	}
	// Assuming an empty list might be possible here

	struct uigtk2_cbt_value *cbtv = uigtk2_cbt_value_new(uigtk2, name);
	if (!cbtv) {
		return NULL;
	}

	cbtv->nvalues = nvalues;
	cbtv->values = g_malloc(nvalues * sizeof(*cbtv->values));
	unsigned i = 0;
	for (struct xconfig_enum *iter = xc_list; i < nvalues && iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		gtk_combo_box_text_append_text(cbtv->cbt, iter->description);
		cbtv->values[i++] = (void *)(intptr_t)iter->value;
	}

	if (changed_handler) {
		g_signal_connect(cbtv->cbt, "changed", changed_handler, cbtv);
	}
	return cbtv;
}

void *uigtk2_cbt_value_get_value(struct uigtk2_cbt_value *cbtv) {
	if (!cbtv) {
		return NULL;
	}
	gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(cbtv->cbt));
	if (active < 0 || (unsigned)active >= cbtv->nvalues) {
		LOG_MOD_ERROR("gtk2", "combo box index '%d' out of bounds for '%s'\n", active, cbtv->name);
		return 0;
	}
	return cbtv->values[active];
}

void uigtk2_cbt_value_set_value(struct uigtk2_cbt_value *cbtv, void *value) {
	if (!cbtv) {
		return;
	}
	for (unsigned i = 0; i < cbtv->nvalues; i++) {
		if (cbtv->values[i] == value) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(cbtv->cbt), i);
			return;
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// General helpers

// Escape underscores in a string, making it suitable to use as a menu label.
// Allocates new space that should be freed with g_free().

char *uigtk2_escape_underscores(const char *str) {
	if (!str) return NULL;
	int len = strlen(str);
	const char *in;
	char *out;
	for (in = str; *in; in++) {
		if (*in == '_')
			len++;
	}
	char *ret_str = g_malloc(len + 1);
	for (in = str, out = ret_str; *in; in++) {
		*(out++) = *in;
		if (*in == '_') {
			*(out++) = '_';
		}
	}
	*out = 0;
	return ret_str;
}
