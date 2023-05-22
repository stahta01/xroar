/** \file
 *
 *  \brief Windows video options window.
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

#include "ao.h"
#include "sound.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialogs.h"

static INT_PTR CALLBACK tv_controls_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND vo_window = NULL;
static HWND vo_volume = NULL;
static HWND vo_brightness = NULL;
static HWND vo_contrast = NULL;
static HWND vo_saturation = NULL;
static HWND vo_hue = NULL;
static HWND cbt_cmp_fs = NULL;
static HWND cbt_cmp_fsc = NULL;
static HWND cbt_cmp_system = NULL;

void windows32_vo_create_window(struct ui_sdl2_interface *uisdl2) {
	(void)uisdl2;
	vo_window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_TV_CONTROLS), windows32_main_hwnd, (DLGPROC)tv_controls_proc);

	vo_volume = GetDlgItem(vo_window, IDC_SPIN_VOLUME);
	SendMessage(vo_volume, UDM_SETRANGE, 0, MAKELPARAM(150, 0));
	SendMessage(vo_volume, UDM_SETPOS, 0, 70);

	vo_brightness = GetDlgItem(vo_window, IDC_SPIN_BRIGHTNESS);
	SendMessage(vo_brightness, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_brightness, UDM_SETPOS, 0, 50);

	vo_contrast = GetDlgItem(vo_window, IDC_SPIN_CONTRAST);
	SendMessage(vo_contrast, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_contrast, UDM_SETPOS, 0, 50);

	vo_saturation = GetDlgItem(vo_window, IDC_SPIN_SATURATION);
	SendMessage(vo_saturation, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_saturation, UDM_SETPOS, 0, 0);

	vo_hue = GetDlgItem(vo_window, IDC_SPIN_HUE);
	SendMessage(vo_hue, UDM_SETRANGE, 0, MAKELPARAM(180, -179));
	SendMessage(vo_hue, UDM_SETPOS, 0, 0);

	cbt_cmp_fs = GetDlgItem(vo_window, IDC_CB_FS);
	for (unsigned i = 0; i < NUM_VO_RENDER_FS; i++) {
		SendMessage(cbt_cmp_fs, CB_ADDSTRING, 0, (LPARAM)vo_render_fs_name[i]);
	}

	cbt_cmp_fsc = GetDlgItem(vo_window, IDC_CB_FSC);
	for (unsigned i = 0; i < NUM_VO_RENDER_FSC; i++) {
		SendMessage(cbt_cmp_fsc, CB_ADDSTRING, 0, (LPARAM)vo_render_fsc_name[i]);
	}

	cbt_cmp_system = GetDlgItem(vo_window, IDC_CB_SYSTEM);
	for (unsigned i = 0; i < NUM_VO_RENDER_SYSTEM; i++) {
		SendMessage(cbt_cmp_system, CB_ADDSTRING, 0, (LPARAM)vo_render_system_name[i]);
	}
}

void windows32_vo_show_window(struct ui_sdl2_interface *uisdl2) {
	(void)uisdl2;
	ShowWindow(vo_window, SW_SHOW);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - update values in UI

void windows32_vo_update_volume(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	SendMessage(vo_volume, UDM_SETPOS, 0, value);
}

void windows32_vo_update_brightness(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	SendMessage(vo_brightness, UDM_SETPOS, 0, value);
}

void windows32_vo_update_contrast(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	SendMessage(vo_contrast, UDM_SETPOS, 0, value);
}

void windows32_vo_update_saturation(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	SendMessage(vo_saturation, UDM_SETPOS, 0, value);
}

void windows32_vo_update_hue(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	SendMessage(vo_hue, UDM_SETPOS, 0, value);
}

void windows32_vo_update_cmp_fs(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	SendMessage(cbt_cmp_fs, CB_SETCURSEL, value, 0);
}

void windows32_vo_update_cmp_fsc(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	SendMessage(cbt_cmp_fsc, CB_SETCURSEL, value, 0);
}

void windows32_vo_update_cmp_system(struct ui_sdl2_interface *uisdl2, int value) {
	(void)uisdl2;
	SendMessage(cbt_cmp_system, CB_SETCURSEL, value, 0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - signal handlers

static INT_PTR CALLBACK tv_controls_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	(void)hwnd;

	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_NOTIFY:
		if (xroar_vo_interface) {
			UINT id = ((LPNMHDR)lParam)->idFrom;
			switch (id) {
			case IDC_SPIN_VOLUME:
				if (xroar_ao_interface) {
					sound_set_volume(xroar_ao_interface->sound_interface, (int16_t)SendMessage(vo_volume, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
				}
				break;

			case IDC_SPIN_BRIGHTNESS:
				DELEGATE_SAFE_CALL(xroar_vo_interface->set_brightness, (int16_t)SendMessage(vo_brightness, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
				break;

			case IDC_SPIN_CONTRAST:
				DELEGATE_SAFE_CALL(xroar_vo_interface->set_contrast, (int16_t)SendMessage(vo_contrast, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
				break;

			case IDC_SPIN_SATURATION:
				DELEGATE_SAFE_CALL(xroar_vo_interface->set_saturation, (int16_t)SendMessage(vo_saturation, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
				break;

			case IDC_SPIN_HUE:
				DELEGATE_SAFE_CALL(xroar_vo_interface->set_hue, (int16_t)SendMessage(vo_hue, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
				break;

			default:
				break;
			}
		}
		return TRUE;

	case WM_COMMAND:
		if (HIWORD(wParam) == CBN_SELCHANGE) {
			int id = LOWORD(wParam);
			HWND cb = (HWND)lParam;
			int value = SendMessage(cb, CB_GETCURSEL, 0, 0);

			switch (id) {
			case IDC_CB_FS:
				if (xroar_vo_interface) {
					vo_set_cmp_fs(xroar_vo_interface, 0, value);
				}
				break;

			case IDC_CB_FSC:
				if (xroar_vo_interface) {
					vo_set_cmp_fsc(xroar_vo_interface, 0, value);
				}
				break;

			case IDC_CB_SYSTEM:
				if (xroar_vo_interface) {
					vo_set_cmp_system(xroar_vo_interface, 0, value);
				}
				break;

			default: break;
			}
		} else if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);

			switch (id) {
			case IDOK:
			case IDCANCEL:
				ShowWindow(vo_window, SW_HIDE);
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
