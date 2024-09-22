/** \file
 *
 *  \brief Windows dialog window abstraction.
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

#ifndef XROAR_WINDOWS32_DIALOG_H_
#define XROAR_WINDOWS32_DIALOG_H_

#include "delegate.h"

struct ui_windows32_interface;

struct uiw32_dialog {
	struct ui_windows32_interface *uiw32;

	DELEGATE_T0(void) free;

	// Dialog window handle
	HWND hWnd;

	// Messenger client id
	int msgr_client_id;

	// UI tag for dialog open/close messages
	int ui_tag;

	// Dialog proc callback
	INT_PTR CALLBACK (*dlg_proc)(struct uiw32_dialog *, UINT msg, WPARAM wParam, LPARAM lParam);
};

void *uiw32_dialog_new_sized(size_t size, struct ui_windows32_interface *uiw32,
			     int resource_id, int ui_tag,
			     INT_PTR CALLBACK (*proc)(struct uiw32_dialog *,
						      UINT, WPARAM, LPARAM));

struct uiw32_dialog *uiw32_dialog_new(struct ui_windows32_interface *uiw32,
				      int resource_id, int ui_tag,
				      INT_PTR CALLBACK (*proc)(struct uiw32_dialog *,
							       UINT, WPARAM, LPARAM));

void uiw32_dialog_free(struct uiw32_dialog *);

// Free every dialog

void uiw32_dialog_shutdown(void);

#endif
