/** \file
 *
 *  \brief Windows printer control window.
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

#include <SDL.h>
#include <SDL_syswm.h>

#include "xalloc.h"

#include "printer.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/printercontrol.h"
#include "windows32/resources.h"

static INT_PTR CALLBACK pc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void windows32_pc_create_window(struct ui_windows32_interface *uiw32) {
	// Main dialog window handle
	uiw32->printer.window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_PRINTER_CONTROLS), windows32_main_hwnd, (DLGPROC)pc_proc);

	CheckRadioButton(uiw32->printer.window, IDC_RB_PRINTER_NONE, IDC_RB_PRINTER_FILE, IDC_RB_PRINTER_NONE);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Printer control - update values in UI

void windows32_pc_update_state(struct ui_windows32_interface *uiw32,
			       int tag, int value, const void *data) {
	switch (tag) {
	case ui_tag_print_dialog:
		ShowWindow(uiw32->printer.window, SW_SHOW);
		break;

	case ui_tag_print_destination:
		CheckRadioButton(uiw32->printer.window, IDC_RB_PRINTER_NONE, IDC_RB_PRINTER_FILE, IDC_RB_PRINTER_NONE + value);
		break;

	case ui_tag_print_file:
		windows32_send_message_dlg_item(uiw32->printer.window, IDC_STM_PRINT_FILENAME, WM_SETTEXT, 0, (LPARAM)data);
		break;

	case ui_tag_print_pipe:
		// Not showing this in Windows
		break;

	case ui_tag_print_count:
		{
			char buf[14];
			char *fmt = "%.0f%s";
			char *unit = "";
			double count = (double)value;
			if (count > 1000.) {
				fmt = "%.1f%s";
				count /= 1000.;
				unit = "k";
			}
			if (count > 1000.) {
				count /= 1000.;
				unit = "M";
			}
			if (count > 1000.) {
				count /= 1000.;
				unit = "G";
			}
			snprintf(buf, sizeof(buf), fmt, count, unit);
			windows32_send_message_dlg_item(uiw32->printer.window, IDC_STM_PRINT_CHARS, WM_SETTEXT, 0, (LPARAM)buf);
		}
		break;

	default:
		break;
	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Printer control - signal handlers

static INT_PTR CALLBACK pc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	struct ui_windows32_interface *uiw32 = (struct ui_windows32_interface *)global_uisdl2;
	struct ui_interface *ui = &uiw32->ui_sdl2_interface.ui_interface;
	// hwnd is the handle for the dialog window, i.e. printer.window

	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_HSCROLL:
		break;

	case WM_NOTIFY:
		return TRUE;

	case WM_DRAWITEM:
		switch (LOWORD(wParam)) {
		case IDC_STM_PRINT_FILENAME:
			{
				HWND ctl_hwnd = GetDlgItem(hwnd, IDC_STM_PRINT_FILENAME);
				LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
				windows32_drawtext_path(ctl_hwnd, pDIS);
			}
			return TRUE;

		default:
			break;
		}
		return FALSE;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);
			switch (id) {

			// Radio buttons

			case IDC_RB_PRINTER_NONE:
			case IDC_RB_PRINTER_FILE:
				xroar_set_printer_destination(1, id - IDC_RB_PRINTER_NONE);
				break;

			// Attach button
			case IDC_BN_PRINT_ATTACH:
				{
					char *filename = DELEGATE_CALL(ui->filereq_interface->save_filename, "Print to file");
					if (filename) {
						xroar_set_printer_file(1, filename);
					}
				}
				break;

			// Flush button
			case IDC_BN_PRINT_FLUSH:
				xroar_flush_printer();
				break;

			// Standard buttons

			case IDOK:
			case IDCANCEL:
				ShowWindow(hwnd, SW_HIDE);
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
