/** \file
 *
 *  \brief GTK+ 3 dialog window abstraction.
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
 *
 *  Handles all the boilerplate of creating a dialog:
 *
 *  - Creates the window from a resource.
 *
 *  - Registers with messenger.
 *
 *  - Handles open/close UI messages.
 *
 *  - Tracks dialogs so all can be shutdown at once.
 */

#ifndef XROAR_GTK3_DIALOG_H_
#define XROAR_GTK3_DIALOG_H_

struct ui_gtk3_interface;

struct uigtk3_dialog {
	struct ui_gtk3_interface *uigtk3;

	DELEGATE_T0(void) free;

	// Dialog window's name in the builder and widget handle
	char *name;
	GtkWindow *window;

	// Messenger client id
	int msgr_client_id;

	// UI tag for dialog open/close messages
	int ui_tag;
};

void *uigtk3_dialog_new_sized(size_t size, struct ui_gtk3_interface *uigtk3,
			      const char *resource_path, const char *dlg_name, int ui_tag);

struct uigtk3_dialog *uigtk3_dialog_new(struct ui_gtk3_interface *uigtk3,
					const char *resource_path, const char *dlg_name,
					int ui_tag);

void uigtk3_dialog_free(struct uigtk3_dialog *);

// Free every dialog

void uigtk3_dialog_shutdown(void);

#endif
