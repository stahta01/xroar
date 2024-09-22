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

#include <stdio.h>

#include "printer.h"
#include "xroar.h"

#include "windows32/common_windows32.h"
#include "windows32/dialog.h"
#include "windows32/printercontrol.h"
#include "windows32/resources.h"

// UI message reception

static void pc_ui_state_notify(void *sptr, int tag, void *smsg);

// Dialog box procedure

static INT_PTR CALLBACK pc_proc(struct uiw32_dialog *, UINT msg, WPARAM wParam, LPARAM lParam);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct uiw32_dialog *uiw32_pc_dialog_new(struct ui_windows32_interface *uiw32) {
	struct uiw32_dialog *dlg = uiw32_dialog_new(uiw32, IDD_DLG_PRINTER_CONTROLS, ui_tag_print_dialog, pc_proc);

	// Join each UI group we're interested in
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_print_destination, MESSENGER_NOTIFY_DELEGATE(pc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_print_file, MESSENGER_NOTIFY_DELEGATE(pc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_print_pipe, MESSENGER_NOTIFY_DELEGATE(pc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_print_count, MESSENGER_NOTIFY_DELEGATE(pc_ui_state_notify, dlg));

	CheckRadioButton(dlg->hWnd, IDC_RB_PRINTER_NONE, IDC_RB_PRINTER_FILE, IDC_RB_PRINTER_NONE);

	return dlg;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void pc_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct uiw32_dialog *dlg = sptr;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	const void *data = uimsg->data;

	switch (tag) {

	case ui_tag_print_dialog:
		{
			_Bool show;
			if (value == UI_NEXT || value == UI_PREV) {
				LONG style = GetWindowLongA(dlg->hWnd, GWL_STYLE);
				show = (style & WS_VISIBLE) ? 0 : 1;
			} else {
				show = value;
			}
			ShowWindow(dlg->hWnd, show ? SW_SHOW : SW_HIDE);
			uimsg->value = show;
		}
		break;

	case ui_tag_print_destination:
		CheckRadioButton(dlg->hWnd, IDC_RB_PRINTER_NONE, IDC_RB_PRINTER_FILE, IDC_RB_PRINTER_NONE + value);
		break;

	case ui_tag_print_file:
		windows32_send_message_dlg_item(dlg->hWnd, IDC_STM_PRINT_FILENAME, WM_SETTEXT, 0, (LPARAM)data);
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
			windows32_send_message_dlg_item(dlg->hWnd, IDC_STM_PRINT_CHARS, WM_SETTEXT, 0, (LPARAM)buf);
		}
		break;

	default:
		break;
	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Printer control - signal handlers

// WM_COMMAND is received for checkboxes, but Windows does not itself alter the
// control's state.  We query its current value and invert it.  The UI message
// we send will then later be received by dc_ui_state_notify() which will
// update the control.

// Some controls are marked with SS_OWNERDRAW (style=ownerdraw in .win file).
// We receive WM_DRAWITEM events for these when they need to be drawn, and we
// can use a custom style, e.g. using uiw32_drawtext_path() to draw with
// DT_PATH_ELLIPSIS.

static INT_PTR CALLBACK pc_proc(struct uiw32_dialog *dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	struct ui_windows32_interface *uiw32 = dlg->uiw32;
	struct ui_interface *ui = &uiw32->ui_sdl2_interface.ui_interface;

	switch (msg) {

	case WM_NOTIFY:
		return TRUE;

	case WM_DRAWITEM:
		{
			int id = LOWORD(wParam);
			switch (id) {
			case IDC_STM_PRINT_FILENAME:
				uiw32_drawtext_path(dlg->hWnd, id, (LPDRAWITEMSTRUCT)lParam);
				return TRUE;

			default:
				break;
			}
		}
		break;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);
			switch (id) {

			// Radio buttons

			case IDC_RB_PRINTER_NONE:
			case IDC_RB_PRINTER_FILE:
				ui_update_state(-1, ui_tag_print_destination, id - IDC_RB_PRINTER_NONE, NULL);
				break;

			// Attach button
			case IDC_BN_PRINT_ATTACH:
				{
					char *filename = DELEGATE_CALL(ui->filereq_interface->save_filename, "Print to file");
					if (filename) {
						ui_update_state(-1, ui_tag_print_file, 0, filename);
					}
				}
				break;

			// Flush button
			case IDC_BN_PRINT_FLUSH:
				xroar_flush_printer();
				break;

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
