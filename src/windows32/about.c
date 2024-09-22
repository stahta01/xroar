/** \file
 *
 *  \brief Windows "About" dialog.
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

#include "windows32/common_windows32.h"
#include "windows32/resources.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Dialog box procedure

static INT_PTR CALLBACK about_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create "About" dialog

void uiw32_create_about_window(struct ui_windows32_interface *uiw32) {
	(void)uiw32;
	if (!IsWindow(uiw32->about.window)) {
		uiw32->about.window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_ABOUT), windows32_main_hwnd, (DLGPROC)about_proc);
		if (uiw32->about.window) {
			ShowWindow(uiw32->about.window, SW_SHOW);
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// About - signal handlers

static INT_PTR CALLBACK about_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	(void)lParam;
	struct ui_windows32_interface *uiw32 = (struct ui_windows32_interface *)global_uisdl2;

	switch (msg) {

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);

			switch (id) {

			// Standard buttons

			case IDOK:
			case IDCANCEL:
				// Close and destroy the dialog
				DestroyWindow(hwnd);
				uiw32->about.window = NULL;
				return TRUE;

			default:
				break;
			}
		}
		break;

	default:
		break;
	}

	return FALSE;
}
