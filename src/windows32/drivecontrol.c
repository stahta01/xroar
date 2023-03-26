/** \file
 *
 *  \brief Windows drive control window.
 *
 *  \copyright Copyright 2023 Ciaran Anscomb
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
#include "windows32/dialogs.h"
#include "windows32/drivecontrol.h"

static INT_PTR CALLBACK dc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND dc_window = NULL;

static HWND dc_stm_drive_filename[VDRIVE_MAX_DRIVES];
static HWND dc_bn_drive_we[VDRIVE_MAX_DRIVES];
static HWND dc_bn_drive_wb[VDRIVE_MAX_DRIVES];
static HWND dc_stm_drive_cyl_head = NULL;

static void update_drive_cyl_head(void *sptr, unsigned drive, unsigned cyl, unsigned head);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void windows32_dc_create_window(struct ui_sdl2_interface *uisdl2) {
	// Main dialog window handle
	dc_window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_DRIVE_CONTROLS), windows32_main_hwnd, (DLGPROC)dc_proc);

	// Control handles
	for (unsigned i = 0; i < VDRIVE_MAX_DRIVES; i++) {
		dc_stm_drive_filename[i] = GetDlgItem(dc_window, IDC_STM_DRIVE1_FILENAME + i);
		dc_bn_drive_we[i] = GetDlgItem(dc_window, IDC_BN_DRIVE1_WE + i);
		dc_bn_drive_wb[i] = GetDlgItem(dc_window, IDC_BN_DRIVE1_WB + i);
	}
	dc_stm_drive_cyl_head = GetDlgItem(dc_window, IDC_STM_DRIVE_CYL_HEAD);

	xroar_vdrive_interface->update_drive_cyl_head = DELEGATE_AS3(void, unsigned, unsigned, unsigned, update_drive_cyl_head, uisdl2);
}

void windows32_dc_show_window(struct ui_sdl2_interface *uisdl2) {
	(void)uisdl2;
	ShowWindow(dc_window, SW_SHOW);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Drive control - update values in UI

void windows32_dc_update_drive_disk(struct ui_sdl2_interface *uisdl2,
				    int drive, const struct vdisk *disk) {
	(void)uisdl2;
	if (drive < 0 || drive > 3)
		return;
	char *filename = NULL;
	_Bool we = 0, wb = 0;
	if (disk) {
		filename = disk->filename;
		we = !disk->write_protect;
		wb = disk->write_back;
	}
	SendMessage(dc_stm_drive_filename[drive], WM_SETTEXT, 0, (LPARAM)filename);
	SendMessage(dc_bn_drive_we[drive], BM_SETCHECK, we ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessage(dc_bn_drive_wb[drive], BM_SETCHECK, wb ? BST_CHECKED : BST_UNCHECKED, 0);
}

void windows32_dc_update_drive_write_enable(struct ui_sdl2_interface *uisdl2,
					    int drive, _Bool write_enable) {
	(void)uisdl2;
	if (drive >= 0 && drive <= 3) {
		SendMessage(dc_bn_drive_we[drive], BM_SETCHECK, write_enable ? BST_CHECKED : BST_UNCHECKED, 0);
	}
}

void windows32_dc_update_drive_write_back(struct ui_sdl2_interface *uisdl2,
					  int drive, _Bool write_back) {
	(void)uisdl2;
	if (drive >= 0 && drive <= 3) {
		SendMessage(dc_bn_drive_wb[drive], BM_SETCHECK, write_back ? BST_CHECKED : BST_UNCHECKED, 0);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Drive control - signal handlers

static INT_PTR CALLBACK dc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	(void)hwnd;
	(void)lParam;
	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_HSCROLL:
		break;

	case WM_NOTIFY:
		return TRUE;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			// Per-drive checkbox toggles & buttons

			int id = LOWORD(wParam);
			if (id >= IDC_BN_DRIVE1_WE && id <= IDC_BN_DRIVE4_WE) {
				int drive = id - IDC_BN_DRIVE1_WE;
				int set = (SendMessage(dc_bn_drive_we[drive], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : 1;
				xroar_set_write_enable(1, drive, set);

			} else if (id >= IDC_BN_DRIVE1_WB && id <= IDC_BN_DRIVE4_WB) {
				int drive = id - IDC_BN_DRIVE1_WB;
				int set = (SendMessage(dc_bn_drive_wb[drive], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : 1;
				xroar_set_write_back(1, drive, set);

			} else if (id >= IDC_BN_DRIVE1_EJECT && id <= IDC_BN_DRIVE4_EJECT) {
				int drive = id - IDC_BN_DRIVE1_EJECT;
				xroar_eject_disk(drive);

			} else if (id >= IDC_BN_DRIVE1_INSERT && id <= IDC_BN_DRIVE4_INSERT) {
				int drive = id - IDC_BN_DRIVE1_INSERT;
				xroar_insert_disk(drive);

			} else switch (id) {

			// Standard buttons

			case IDOK:
			case IDCANCEL:
				ShowWindow(dc_window, SW_HIDE);
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
	struct ui_sdl2_interface *uisdl2 = sptr;
	(void)uisdl2;
	char string[16];
	snprintf(string, sizeof(string), "Dr %01u Tr %02u He %01u", drive + 1, cyl, head);
	SendMessage(dc_stm_drive_cyl_head, WM_SETTEXT, 0, (LPARAM)string);
}
