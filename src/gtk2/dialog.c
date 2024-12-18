/** \file
 *
 *  \brief GTK+ 2 dialog window abstraction.
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "delegate.h"
#include "xalloc.h"

#include "messenger.h"
#include "ui.h"

#include "gtk2/common.h"
#include "gtk2/dialog.h"
#include "gtk2/event_handlers.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// List of known dialog windows

struct uigtk2_dialog **dialogs = NULL;
int ndialogs = 0;

// UI message reception

static void dlg_ui_state_notify(void *, int tag, void *smsg);

// Signal handlers

static gboolean handle_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create dialog window

void *uigtk2_dialog_new_sized(size_t size, struct ui_gtk2_interface *uigtk2,
			      const char *resource_path, const char *dlg_name, int ui_tag) {
	// Create dialog from XML stored as a resource
	uigtk2_add_from_resource(uigtk2, resource_path);
	GtkWindow *window = GTK_WINDOW(gtk_builder_get_object(uigtk2->builder, dlg_name));
	assert(window != NULL);

	if (size < sizeof(struct uigtk2_dialog)) {
		size = sizeof(struct uigtk2_dialog);
	}
	struct uigtk2_dialog *dlg = xmalloc(size);
	// Zero everything
	memset(dlg, 0, size);
	// Properly initialise the part we know about
	*dlg = (struct uigtk2_dialog){0};

	dlg->uigtk2 = uigtk2;
	dlg->name = xstrdup(dlg_name);
	dlg->window = window;
	dlg->ui_tag = ui_tag;

	// Add to the list of known dialogs
	int i = ndialogs++;
	dialogs = xrealloc(dialogs, ndialogs * sizeof(*dialogs));
	dialogs[i] = dlg;

	// Handle window closure by sending a UI message.
	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(handle_delete_event), dlg);

	// Ensures accelerator keys are seen as in the main window
	g_signal_connect(G_OBJECT(window), "key-press-event", G_CALLBACK(gtk2_dummy_keypress), uigtk2);

	// Register dialog with messenger
	dlg->msgr_client_id = messenger_client_register();

	// Preempt the message group for the dialog's UI tag
	if (ui_tag >= 0) {
		ui_messenger_preempt_group(dlg->msgr_client_id, ui_tag, MESSENGER_NOTIFY_DELEGATE(dlg_ui_state_notify, dlg));
	}

	return dlg;
}

struct uigtk2_dialog *uigtk2_dialog_new(struct ui_gtk2_interface *uigtk2,
					const char *resource_path, const char *dlg_name,
					int ui_tag) {
	struct uigtk2_dialog *dlg = uigtk2_dialog_new_sized(sizeof(*dlg), uigtk2,
							    resource_path, dlg_name, ui_tag);
	return dlg;
}

void uigtk2_dialog_free(struct uigtk2_dialog *dlg) {
	if (dlg) {
		DELEGATE_SAFE_CALL(dlg->free);
		if (dlg->msgr_client_id >= 0) {
			messenger_client_unregister(dlg->msgr_client_id);
		}
		if (dlg->window) {
			gtk_widget_destroy(GTK_WIDGET(dlg->window));
		}
		if (dlg->name) {
			free(dlg->name);
		}
	}

	for (int i = 0; i < ndialogs; ++i) {
		if (dialogs[i] == dlg) {
			struct uigtk2_dialog **dst = &dialogs[i];
			int nfollowing = ndialogs - i - 1;
			if (nfollowing > 0) {
				struct uigtk2_dialog **src = &dialogs[i+1];
				memmove(dst, src, nfollowing * sizeof(*dst));
			}
			--ndialogs;
			dialogs[ndialogs] = NULL;
			break;
		}
	}

	if (dlg) {
		free(dlg);
	}
	if (ndialogs == 0) {
		free(dialogs);
		dialogs = NULL;
	}
}

void uigtk2_dialog_shutdown(void) {
	while (ndialogs > 0) {
		uigtk2_dialog_free(dialogs[ndialogs-1]);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

// Intercepts the dialog-specific UI tag and shows/hides the dialog window
// accordingly.  Calls the ui_state_notify delegate for anything else.

static void dlg_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct uigtk2_dialog *dlg = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == dlg->ui_tag);

	_Bool show;
	if (uimsg->value == UI_NEXT || uimsg->value == UI_PREV) {
		show = !gtk_widget_get_visible(GTK_WIDGET(dlg->window));
	} else {
		show = uimsg->value;
	}
	if (show) {
		gtk_widget_show(GTK_WIDGET(dlg->window));
	} else {
		gtk_widget_hide(GTK_WIDGET(dlg->window));
	}
	uimsg->value = show;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Signal handlers

// Handle "delete-event", sent when the dialog is closed.  Closes the dialog by
// sending a UI message so that the whole message group knows about it.

static gboolean handle_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct uigtk2_dialog *dlg = user_data;
	ui_update_state(-1, dlg->ui_tag, 0, NULL);
	return TRUE;
}
