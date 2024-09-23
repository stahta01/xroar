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

#include "events.h"
#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/drivecontrol.h"
#include "windows32/resources.h"

static INT_PTR CALLBACK dc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void update_drive_cyl_head(void *sptr, unsigned drive, unsigned cyl, unsigned head);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void windows32_dc_create_window(struct ui_windows32_interface *uiw32) {
	// Main dialog window handle
	uiw32->disk.window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_DRIVE_CONTROLS), windows32_main_hwnd, (DLGPROC)dc_proc);

	xroar.vdrive_interface->update_drive_cyl_head = DELEGATE_AS3(void, unsigned, unsigned, unsigned, update_drive_cyl_head, uiw32);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Drive control - update values in UI

static void windows32_dc_update_drive_disk(struct ui_windows32_interface *,
					   int drive, const struct vdisk *disk);
static void windows32_dc_update_drive_write_enable(struct ui_windows32_interface *,
						   int drive, _Bool write_enable);
static void windows32_dc_update_drive_write_back(struct ui_windows32_interface *,
						 int drive, _Bool write_back);

void windows32_dc_update_state(struct ui_windows32_interface *uiw32,
			       int tag, int value, const void *data) {
	switch (tag) {
	case ui_tag_disk_dialog:
		ShowWindow(uiw32->disk.window, SW_SHOW);
		break;

	case ui_tag_disk_data:
		windows32_dc_update_drive_disk(uiw32, value, (const struct vdisk *)data);
		break;

	case ui_tag_disk_write_enable:
		windows32_dc_update_drive_write_enable(uiw32, value, (intptr_t)data);
		break;

	case ui_tag_disk_write_back:
		windows32_dc_update_drive_write_back(uiw32, value, (intptr_t)data);
		break;

	default:
		break;
	}
}

static void windows32_dc_update_drive_disk(struct ui_windows32_interface *uiw32,
					   int drive, const struct vdisk *disk) {
	if (drive < 0 || drive > 3)
		return;
	char *filename = NULL;
	_Bool we = 0, wb = 0;
	if (disk) {
		filename = disk->filename;
		we = !disk->write_protect;
		wb = disk->write_back;
	}
	HWND dc_stm_drive_filename = GetDlgItem(uiw32->disk.window, IDC_STM_DRIVE1_FILENAME + drive);
	HWND dc_bn_drive_we = GetDlgItem(uiw32->disk.window, IDC_BN_DRIVE1_WE + drive);
	HWND dc_bn_drive_wb = GetDlgItem(uiw32->disk.window, IDC_BN_DRIVE1_WB + drive);
	SendMessage(dc_stm_drive_filename, WM_SETTEXT, 0, (LPARAM)filename);
	SendMessage(dc_bn_drive_we, BM_SETCHECK, we ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessage(dc_bn_drive_wb, BM_SETCHECK, wb ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void windows32_dc_update_drive_write_enable(struct ui_windows32_interface *uiw32,
						   int drive, _Bool write_enable) {
	if (drive >= 0 && drive <= 3) {
		HWND dc_bn_drive_we = GetDlgItem(uiw32->disk.window, IDC_BN_DRIVE1_WE + drive);
		SendMessage(dc_bn_drive_we, BM_SETCHECK, write_enable ? BST_CHECKED : BST_UNCHECKED, 0);
	}
}

static void windows32_dc_update_drive_write_back(struct ui_windows32_interface *uiw32,
						 int drive, _Bool write_back) {
	if (drive >= 0 && drive <= 3) {
		HWND dc_bn_drive_wb = GetDlgItem(uiw32->disk.window, IDC_BN_DRIVE1_WB + drive);
		SendMessage(dc_bn_drive_wb, BM_SETCHECK, write_back ? BST_CHECKED : BST_UNCHECKED, 0);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Drive control - signal handlers

static INT_PTR CALLBACK dc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// hwnd is the handle for the dialog window, i.e. dc_window
	(void)lParam;

	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_HSCROLL:
		break;

	case WM_NOTIFY:
		return TRUE;

	case WM_DRAWITEM:
		{
			int id = LOWORD(wParam);
			if (id >= IDC_STM_DRIVE1_FILENAME && id <= IDC_STM_DRIVE4_FILENAME) {
				uiw32_drawtext_path(hwnd, id, (LPDRAWITEMSTRUCT)lParam);
				return TRUE;
			}
		}
		return FALSE;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			// Per-drive checkbox toggles & buttons

			int id = LOWORD(wParam);
			if (id >= IDC_BN_DRIVE1_WE && id <= IDC_BN_DRIVE4_WE) {
				int drive = id - IDC_BN_DRIVE1_WE;
				HWND dc_bn_drive_we = GetDlgItem(hwnd, IDC_BN_DRIVE1_WE + drive);
				int set = (SendMessage(dc_bn_drive_we, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : 1;
				xroar_set_write_enable(1, drive, set);

			} else if (id >= IDC_BN_DRIVE1_WB && id <= IDC_BN_DRIVE4_WB) {
				int drive = id - IDC_BN_DRIVE1_WB;
				HWND dc_bn_drive_wb = GetDlgItem(hwnd, IDC_BN_DRIVE1_WB + drive);
				int set = (SendMessage(dc_bn_drive_wb, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : 1;
				xroar_set_write_back(1, drive, set);

			} else if (id >= IDC_BN_DRIVE1_INSERT && id <= IDC_BN_DRIVE4_INSERT) {
				int drive = id - IDC_BN_DRIVE1_INSERT;
				xroar_insert_disk(drive);

			} else if (id >= IDC_BN_DRIVE1_NEW && id <= IDC_BN_DRIVE4_NEW) {
				int drive = id - IDC_BN_DRIVE1_NEW;
				xroar_new_disk(drive);

			} else if (id >= IDC_BN_DRIVE1_EJECT && id <= IDC_BN_DRIVE4_EJECT) {
				int drive = id - IDC_BN_DRIVE1_EJECT;
				xroar_eject_disk(drive);

			} else switch (id) {

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

static void update_drive_cyl_head(void *sptr, unsigned drive, unsigned cyl, unsigned head) {
	struct ui_windows32_interface *uiw32 = sptr;
	char string[16];
	snprintf(string, sizeof(string), "Dr %01u Tr %02u He %01u", drive + 1, cyl, head);
	HWND dc_stm_drive_cyl_head = GetDlgItem(uiw32->disk.window, IDC_STM_DRIVE_CYL_HEAD);
	SendMessage(dc_stm_drive_cyl_head, WM_SETTEXT, 0, (LPARAM)string);
}
