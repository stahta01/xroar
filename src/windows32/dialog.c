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
 */

#include "top-config.h"

#include <windows.h>
#include <commctrl.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "delegate.h"
#include "xalloc.h"

#include "messenger.h"
#include "ui.h"

#include "windows32/common_windows32.h"
#include "windows32/dialog.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// List of known dialog windows

struct uiw32_dialog **dialogs = NULL;
int ndialogs = 0;

// UI message reception

static void dlg_ui_state_notify(void *, int tag, void *smsg);

// Signal handlers

static INT_PTR CALLBACK dlg_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create dialog window

void *uiw32_dialog_new_sized(size_t size, struct ui_windows32_interface *uiw32,
			     int resource_id, int ui_tag,
			     INT_PTR CALLBACK (*proc)(struct uiw32_dialog *,
						      UINT, WPARAM, LPARAM)) {
	// Create dialog from resource
	HWND hWnd = CreateDialog(NULL, MAKEINTRESOURCE(resource_id), windows32_main_hwnd, (DLGPROC)dlg_proc);
	assert(hWnd != NULL);

	if (size < sizeof(struct uiw32_dialog)) {
		size = sizeof(struct uiw32_dialog);
	}
	struct uiw32_dialog *dlg = xmalloc(size);
	// Zero everything
	memset(dlg, 0, size);
	// Properly initialise the part we know about
	*dlg = (struct uiw32_dialog){0};

	dlg->uiw32 = uiw32;
	dlg->hWnd = hWnd;
	dlg->dlg_proc = proc;
	dlg->ui_tag = ui_tag;

	// Add to the list of known dialogs
	int i = ndialogs++;
	dialogs = xrealloc(dialogs, ndialogs * sizeof(*dialogs));
	dialogs[i] = dlg;

	// Register dialog with messenger
	dlg->msgr_client_id = messenger_client_register();

	// Preempt the message group for the dialog's UI tag
	if (ui_tag >= 0) {
		ui_messenger_preempt_group(dlg->msgr_client_id, ui_tag, MESSENGER_NOTIFY_DELEGATE(dlg_ui_state_notify, dlg));
	}

	return dlg;
}

struct uiw32_dialog *uiw32_dialog_new(struct ui_windows32_interface *uiw32,
				      int resource_id, int ui_tag,
				      INT_PTR CALLBACK (*proc)(struct uiw32_dialog *,
							       UINT, WPARAM, LPARAM)) {
	struct uiw32_dialog *dlg = uiw32_dialog_new_sized(sizeof(*dlg), uiw32,
							  resource_id, ui_tag, proc);
	return dlg;
}

void uiw32_dialog_free(struct uiw32_dialog *dlg) {
	if (dlg) {
		DELEGATE_SAFE_CALL(dlg->free);
		if (dlg->msgr_client_id >= 0) {
			messenger_client_unregister(dlg->msgr_client_id);
		}
		if (dlg->hWnd) {
			DestroyWindow(dlg->hWnd);
		}
	}

	for (int i = 0; i < ndialogs; ++i) {
		if (dialogs[i] == dlg) {
			struct uiw32_dialog **dst = &dialogs[i];
			int nfollowing = ndialogs - i - 1;
			if (nfollowing > 0) {
				struct uiw32_dialog **src = &dialogs[i+1];
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

void uiw32_dialog_shutdown(void) {
	while (ndialogs > 0) {
		uiw32_dialog_free(dialogs[ndialogs-1]);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

// Intercepts the dialog-specific UI tag and shows/hides the dialog window
// accordingly.  Calls the ui_state_notify delegate for anything else.

static void dlg_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct uiw32_dialog *dlg = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == dlg->ui_tag);

	_Bool show;
	if (uimsg->value == UI_NEXT || uimsg->value == UI_PREV) {
		LONG style = GetWindowLongA(dlg->hWnd, GWL_STYLE);
		show = (style & WS_VISIBLE) ? 0 : 1;
	} else {
		show = uimsg->value;
	}
	ShowWindow(dlg->hWnd, show ? SW_SHOW : SW_HIDE);
	uimsg->value = show;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Signal handlers

static INT_PTR CALLBACK dlg_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// Find dialog
	struct uiw32_dialog *dlg = NULL;
	for (int i = 0; i < ndialogs; ++i) {
		if (dialogs[i]->hWnd == hWnd) {
			dlg = dialogs[i];
			break;
		}
	}
	if (!dlg) {
		return FALSE;
	}

	// Supplied dlg_proc can override anything we do here, though it's
	// probably best not to.

	if (dlg->dlg_proc) {
		if (dlg->dlg_proc(dlg, msg, wParam, lParam) == TRUE) {
			return TRUE;
		}
	}

	if (msg == WM_COMMAND) {
		if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);
			switch (id) {
			case IDOK:
			case IDCANCEL:
				// Close the dialog by sending UI message
				ui_update_state(-1, dlg->ui_tag, 0, NULL);
				return TRUE;

			default: break;
			}
		}
	}

	// "The dialog box procedure must return TRUE to direct the system to
	// further process the WM_INITDIALOG message."
	if (msg == WM_INITDIALOG) {
		return TRUE;
	}

	return FALSE;
}
