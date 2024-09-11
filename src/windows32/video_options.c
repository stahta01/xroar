/** \file
 *
 *  \brief Windows video options window.
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

#include "ao.h"
#include "sound.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialogs.h"

static INT_PTR CALLBACK tv_controls_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND vo_window = NULL;

void windows32_vo_create_window(struct ui_sdl2_interface *uisdl2) {
	(void)uisdl2;
	vo_window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_TV_CONTROLS), windows32_main_hwnd, (DLGPROC)tv_controls_proc);

	HWND vo_volume = GetDlgItem(vo_window, IDC_SPIN_VOLUME);
	SendMessage(vo_volume, UDM_SETRANGE, 0, MAKELPARAM(150, 0));
	SendMessage(vo_volume, UDM_SETPOS, 0, 70);

	HWND vo_brightness = GetDlgItem(vo_window, IDC_SPIN_BRIGHTNESS);
	SendMessage(vo_brightness, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_brightness, UDM_SETPOS, 0, 50);

	HWND vo_contrast = GetDlgItem(vo_window, IDC_SPIN_CONTRAST);
	SendMessage(vo_contrast, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_contrast, UDM_SETPOS, 0, 50);

	HWND vo_saturation = GetDlgItem(vo_window, IDC_SPIN_SATURATION);
	SendMessage(vo_saturation, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_saturation, UDM_SETPOS, 0, 0);

	HWND vo_hue = GetDlgItem(vo_window, IDC_SPIN_HUE);
	SendMessage(vo_hue, UDM_SETRANGE, 0, MAKELPARAM(180, -179));
	SendMessage(vo_hue, UDM_SETPOS, 0, 0);

	uiw32_combo_box_from_enum(vo_window, IDC_CB_PICTURE, vo_viewport_list);
	uiw32_combo_box_from_enum(vo_window, IDC_CB_RENDERER, vo_cmp_ccr_list);
	uiw32_combo_box_from_enum(vo_window, IDC_CB_FS, vo_render_fs_list);
	uiw32_combo_box_from_enum(vo_window, IDC_CB_FSC, vo_render_fsc_list);
	uiw32_combo_box_from_enum(vo_window, IDC_CB_SYSTEM, vo_render_system_list);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - update values in UI

void windows32_vo_update_state(struct ui_windows32_interface *uiw32,
			       int tag, int value, const void *data) {
	(void)uiw32;
	(void)data;

	switch (tag) {
	case ui_tag_tv_dialog:
		ShowWindow(vo_window, SW_SHOW);
		break;

	case ui_tag_gain:
		windows32_send_message_dlg_item(vo_window, IDC_SPIN_VOLUME, UDM_SETPOS, 0, value);
		break;

	case ui_tag_brightness:
		windows32_send_message_dlg_item(vo_window, IDC_SPIN_BRIGHTNESS, UDM_SETPOS, 0, value);
		break;

	case ui_tag_contrast:
		windows32_send_message_dlg_item(vo_window, IDC_SPIN_CONTRAST, UDM_SETPOS, 0, value);
		break;

	case ui_tag_saturation:
		windows32_send_message_dlg_item(vo_window, IDC_SPIN_SATURATION, UDM_SETPOS, 0, value);
		break;

	case ui_tag_hue:
		windows32_send_message_dlg_item(vo_window, IDC_SPIN_HUE, UDM_SETPOS, 0, value);
		break;

	case ui_tag_picture:
		uiw32_combo_box_select_by_data(vo_window, IDC_CB_PICTURE, value);
		break;

	case ui_tag_ntsc_scaling:
		windows32_send_message_dlg_item(vo_window, IDC_BN_NTSC_SCALING, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	case ui_tag_ccr:
		uiw32_combo_box_select_by_data(vo_window, IDC_CB_RENDERER, value);
		break;

	case ui_tag_cmp_fs:
		uiw32_combo_box_select_by_data(vo_window, IDC_CB_FS, value);
		break;

	case ui_tag_cmp_fsc:
		uiw32_combo_box_select_by_data(vo_window, IDC_CB_FSC, value);
		break;

	case ui_tag_cmp_system:
		uiw32_combo_box_select_by_data(vo_window, IDC_CB_SYSTEM, value);
		break;

	case ui_tag_cmp_colour_killer:
		windows32_send_message_dlg_item(vo_window, IDC_BN_COLOUR_KILLER, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - signal handlers

static INT_PTR CALLBACK tv_controls_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// hwnd is the handle for the dialog window, i.e. vo_window
	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->idFrom) {
		case IDC_SPIN_VOLUME:
			if (xroar.ao_interface) {
				HWND vo_volume = GetDlgItem(hwnd, IDC_SPIN_VOLUME);
				sound_set_volume(xroar.ao_interface->sound_interface, (int16_t)SendMessage(vo_volume, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_BRIGHTNESS:
			if (xroar.vo_interface) {
				HWND vo_brightness = GetDlgItem(hwnd, IDC_SPIN_BRIGHTNESS);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_brightness, (int16_t)SendMessage(vo_brightness, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_CONTRAST:
			if (xroar.vo_interface) {
				HWND vo_contrast = GetDlgItem(hwnd, IDC_SPIN_CONTRAST);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_contrast, (int16_t)SendMessage(vo_contrast, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_SATURATION:
			if (xroar.vo_interface) {
				HWND vo_saturation = GetDlgItem(hwnd, IDC_SPIN_SATURATION);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_saturation, (int16_t)SendMessage(vo_saturation, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_HUE:
			if (xroar.vo_interface) {
				HWND vo_hue = GetDlgItem(hwnd, IDC_SPIN_HUE);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_hue, (int16_t)SendMessage(vo_hue, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		default:
			break;
		}
		return TRUE;

	case WM_COMMAND:
		if (HIWORD(wParam) == CBN_SELCHANGE) {
			int id = LOWORD(wParam);
			HWND cb = (HWND)lParam;
			int idx = SendMessage(cb, CB_GETCURSEL, 0, 0);
			int old_value = idx;
			int value = SendMessage(cb, CB_GETITEMDATA, idx, 0);

			switch (id) {
			case IDC_CB_PICTURE:
				xroar_set_picture(0, value);
				break;

			case IDC_CB_RENDERER:
				if (xroar.vo_interface) {
					vo_set_cmp_ccr(xroar.vo_interface, 1, value);
				}
				break;

			case IDC_CB_FS:
				if (xroar.vo_interface) {
					vo_set_cmp_fs(xroar.vo_interface, 0, old_value);
				}
				break;

			case IDC_CB_FSC:
				if (xroar.vo_interface) {
					vo_set_cmp_fsc(xroar.vo_interface, 0, old_value);
				}
				break;

			case IDC_CB_SYSTEM:
				if (xroar.vo_interface) {
					vo_set_cmp_system(xroar.vo_interface, 0, old_value);
				}
				break;

			default: break;
			}
		} else if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);

			switch (id) {
			case IDC_BN_NTSC_SCALING:
				if (xroar.vo_interface) {
					HWND tb_ntsc_scaling = GetDlgItem(hwnd, IDC_BN_NTSC_SCALING);
					int value = !(SendMessage(tb_ntsc_scaling, BM_GETCHECK, 0, 0) == BST_CHECKED);
					vo_set_ntsc_scaling(xroar.vo_interface, 1, value);
				}
				return FALSE;

			case IDC_BN_COLOUR_KILLER:
				if (xroar.vo_interface) {
					HWND tb_cmp_colour_killer = GetDlgItem(hwnd, IDC_BN_COLOUR_KILLER);
					int value = !(SendMessage(tb_cmp_colour_killer, BM_GETCHECK, 0, 0) == BST_CHECKED);
					vo_set_cmp_colour_killer(xroar.vo_interface, 1, value);
				}
				return FALSE;
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
