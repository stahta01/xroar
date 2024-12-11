/** \file
 *
 *  \brief Windows drive control window.
 *
 *  \copyright Copyright 2023-2024 Ciaran Anscomb
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

#include "blockdev.h"
#include "messenger.h"
#include "ui.h"
#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialog.h"
#include "windows32/drivecontrol.h"
#include "windows32/resources.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void dc_ui_state_notify(void *sptr, int tag, void *smsg);

// Dialog box procedure

static INT_PTR CALLBACK dc_proc(struct uiw32_dialog *, UINT msg, WPARAM wParam, LPARAM lParam);

static void dc_new_hd(struct uiw32_dialog *dlg, int hd);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create floppy disks dialog window

struct uiw32_dialog *uiw32_dc_dialog_new(struct ui_windows32_interface *uiw32) {
        struct uiw32_dialog *dlg = uiw32_dialog_new(uiw32, IDD_DLG_DRIVE_CONTROLS, ui_tag_disk_dialog, dc_proc);

	// Join each UI group we're interested in
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_disk_data, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_disk_write_enable, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_disk_write_back, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_disk_drive_info, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_hd_filename, MESSENGER_NOTIFY_DELEGATE(dc_ui_state_notify, dlg));

	return dlg;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void dc_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct uiw32_dialog *dlg = sptr;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	const void *data = uimsg->data;

	switch (tag) {

	case ui_tag_disk_data:
		if (value >= 0 && value <= 3) {
			int drive = value;
			const struct vdisk *disk = data;
			const char *filename = disk ? disk->filename : NULL;
			uiw32_send_message(dlg->hWnd, IDC_STM_DRIVE1_FILENAME + drive, WM_SETTEXT, 0, (LPARAM)filename);
		}
		break;

	case ui_tag_disk_write_enable:
		{
			int drive = (intptr_t)data;
			if (drive >= 0 && drive <= 3) {
				uiw32_send_message(dlg->hWnd, IDC_BN_DRIVE1_WE + drive, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
			}
		}
		break;

	case ui_tag_disk_write_back:
		{
			int drive = (intptr_t)data;
			if (drive >= 0 && drive <= 3) {
				uiw32_send_message(dlg->hWnd, IDC_BN_DRIVE1_WB + drive, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
			}
		}
		break;

	case ui_tag_disk_drive_info:
		{
			const struct vdrive_info *vi = data;
			unsigned d = vi->drive + 1;
			unsigned c = vi->cylinder;
			unsigned h = vi->head;
			char string[16];
			snprintf(string, sizeof(string), "Dr %01u Tr %02u He %01u", d, c, h);
			HWND hWnd = GetDlgItem(dlg->hWnd, IDC_STM_DRIVE_CYL_HEAD);
			SendMessage(hWnd, WM_SETTEXT, 0, (LPARAM)string);
		}
		break;

	case ui_tag_hd_filename:
		if (value >= 0 && value <= 1) {
			int hd = value;
			const char *filename = data;
			uiw32_send_message(dlg->hWnd, IDC_STM_HD0_FILENAME + hd, WM_SETTEXT, 0, (LPARAM)filename);
		}
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Drive control - signal handlers

// WM_COMMAND is received for checkboxes, but Windows does not itself alter the
// control's state.  We query its current value and invert it.  The UI message
// we send will then later be received by dc_ui_state_notify() which will
// update the control.

// Some controls are marked with SS_OWNERDRAW (style=ownerdraw in .win file).
// We receive WM_DRAWITEM events for these when they need to be drawn, and we
// can use a custom style, e.g. using uiw32_drawtext_path() to draw with
// DT_PATH_ELLIPSIS.

static INT_PTR CALLBACK dc_proc(struct uiw32_dialog *dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {

	case WM_NOTIFY:
		return TRUE;

	case WM_DRAWITEM:
		{
			int id = LOWORD(wParam);
			if (id >= IDC_STM_DRIVE1_FILENAME && id <= IDC_STM_DRIVE4_FILENAME) {
				uiw32_drawtext_path(dlg->hWnd, id, (LPDRAWITEMSTRUCT)lParam);
				return TRUE;

			} else if (id >= IDC_STM_HD0_FILENAME && id <= IDC_STM_HD1_FILENAME) {
				uiw32_drawtext_path(dlg->hWnd, id, (LPDRAWITEMSTRUCT)lParam);
				return TRUE;

			}
		}
		return FALSE;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);

			// Per-drive checkbox toggles & buttons

			if (id >= IDC_BN_DRIVE1_WE && id <= IDC_BN_DRIVE4_WE) {
				// Write enable checkbox
				int drive = id - IDC_BN_DRIVE1_WE;
				int value = !uiw32_bm_getcheck(dlg->hWnd, id);  // toggle
				ui_update_state(-1, ui_tag_disk_write_enable, value, (void *)(intptr_t)drive);

			} else if (id >= IDC_BN_DRIVE1_WB && id <= IDC_BN_DRIVE4_WB) {
				// Write back checkbox
				int drive = id - IDC_BN_DRIVE1_WB;
				int value = !uiw32_bm_getcheck(dlg->hWnd, id);  // toggle
				ui_update_state(-1, ui_tag_disk_write_back, value, (void *)(intptr_t)drive);

			} else if (id >= IDC_BN_DRIVE1_INSERT && id <= IDC_BN_DRIVE4_INSERT) {
				// Insert button
				int drive = id - IDC_BN_DRIVE1_INSERT;
				xroar_insert_disk(drive);

			} else if (id >= IDC_BN_DRIVE1_NEW && id <= IDC_BN_DRIVE4_NEW) {
				// New button
				int drive = id - IDC_BN_DRIVE1_NEW;
				xroar_new_disk(drive);

			} else if (id >= IDC_BN_DRIVE1_EJECT && id <= IDC_BN_DRIVE4_EJECT) {
				// Eject button
				int drive = id - IDC_BN_DRIVE1_EJECT;
				xroar_eject_disk(drive);

			} else if (id >= IDC_BN_HD0_ATTACH && id <= IDC_BN_HD1_ATTACH) {
				// HD attach button
				int hd = id - IDC_BN_HD0_ATTACH;
				char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->load_filename, "Attach hard disk image");
				if (filename) {
					xroar_insert_hd_file(hd, filename);
				}

			} else if (id >= IDC_BN_HD0_NEW && id <= IDC_BN_HD1_NEW) {
				// HD detach button
				int hd = id - IDC_BN_HD0_NEW;
				dc_new_hd(dlg, hd);

			} else if (id >= IDC_BN_HD0_DETACH && id <= IDC_BN_HD1_DETACH) {
				// HD detach button
				int hd = id - IDC_BN_HD0_DETACH;
				xroar_insert_hd_file(hd, NULL);

			} else switch (id) {

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

// TODO: adapt dialog abstraction to allow running standalone modal dialogs

static int hd_type = -1;
static const int hd_type_map[4] = {
	BD_ACME_NEMESIS,
	BD_ACME_ULTRASONICUS,
	BD_ACME_ACCELLERATTI,
	BD_ACME_ZIPPIBUS
};

static INT_PTR CALLBACK dc_new_hd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	(void)lParam;
	switch (msg) {

	case WM_INITDIALOG:
		CheckRadioButton(hWnd, IDC_RB_HD_SIZE_0, IDC_RB_HD_SIZE_3, IDC_RB_HD_SIZE_0);
		hd_type = 0;
		return TRUE;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);

			if (id >= IDC_RB_HD_SIZE_0 && id <= IDC_RB_HD_SIZE_3) {
				hd_type = id - IDC_RB_HD_SIZE_0;
				CheckRadioButton(hWnd, IDC_RB_HD_SIZE_0, IDC_RB_HD_SIZE_3, id);
				return TRUE;

			} else switch (id) {
			case IDOK:
			case IDCANCEL:
				EndDialog(hWnd, wParam);
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

static void dc_new_hd(struct uiw32_dialog *dlg, int hd) {
	int r = DialogBox(NULL, MAKEINTRESOURCE(IDD_DLG_HD_SIZE), dlg->hWnd, (DLGPROC)dc_new_hd_proc);
	if (r != IDOK || hd_type < 0 || hd_type > 3) {
		return;
	}
	char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->save_filename, "Create hard disk image");
	if (filename) {
		if (bd_create(filename, hd_type_map[hd_type])) {
			xroar_insert_hd_file(hd, filename);
		}
	}
}
