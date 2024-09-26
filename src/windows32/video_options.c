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
#include "machine.h"
#include "messenger.h"
#include "sound.h"
#include "ui.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialog.h"
#include "windows32/resources.h"
#include "windows32/video_options.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void vo_ui_state_notify(void *sptr, int tag, void *smsg);

// Dialog box procedure

static INT_PTR CALLBACK tv_proc(struct uiw32_dialog *, UINT msg, WPARAM wParam, LPARAM lParam);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create TV controls dialog

struct uiw32_dialog *uiw32_tv_dialog_new(struct ui_windows32_interface *uiw32) {
	struct uiw32_dialog *dlg = uiw32_dialog_new(uiw32, IDD_DLG_TV_CONTROLS, ui_tag_tv_dialog, tv_proc);

	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_ccr, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, uiw32));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_picture, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, uiw32));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, uiw32));

	HWND vo_volume = GetDlgItem(dlg->hWnd, IDC_SPIN_VOLUME);
	SendMessage(vo_volume, UDM_SETRANGE, 0, MAKELPARAM(150, 0));
	SendMessage(vo_volume, UDM_SETPOS, 0, 70);

	HWND vo_brightness = GetDlgItem(dlg->hWnd, IDC_SPIN_BRIGHTNESS);
	SendMessage(vo_brightness, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_brightness, UDM_SETPOS, 0, 50);

	HWND vo_contrast = GetDlgItem(dlg->hWnd, IDC_SPIN_CONTRAST);
	SendMessage(vo_contrast, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_contrast, UDM_SETPOS, 0, 50);

	HWND vo_saturation = GetDlgItem(dlg->hWnd, IDC_SPIN_SATURATION);
	SendMessage(vo_saturation, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
	SendMessage(vo_saturation, UDM_SETPOS, 0, 0);

	HWND vo_hue = GetDlgItem(dlg->hWnd, IDC_SPIN_HUE);
	SendMessage(vo_hue, UDM_SETRANGE, 0, MAKELPARAM(180, -179));
	SendMessage(vo_hue, UDM_SETPOS, 0, 0);

	uiw32_combo_box_from_enum(dlg->hWnd, IDC_CB_TV_INPUT, machine_tv_input_list);
	uiw32_combo_box_from_enum(dlg->hWnd, IDC_CB_PICTURE, vo_viewport_list);
	uiw32_combo_box_from_enum(dlg->hWnd, IDC_CB_RENDERER, vo_cmp_ccr_list);
	uiw32_combo_box_from_enum(dlg->hWnd, IDC_CB_FS, vo_render_fs_list);
	uiw32_combo_box_from_enum(dlg->hWnd, IDC_CB_FSC, vo_render_fsc_list);
	uiw32_combo_box_from_enum(dlg->hWnd, IDC_CB_SYSTEM, vo_render_system_list);

	return dlg;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - update values in UI

void windows32_vo_update_state(struct ui_windows32_interface *uiw32,
			       int tag, int value, const void *data) {
	(void)data;

	switch (tag) {

	case ui_tag_gain:
		uiw32_send_message(uiw32->tv_dialog->hWnd, IDC_SPIN_VOLUME, UDM_SETPOS, 0, value);
		break;

	case ui_tag_brightness:
		uiw32_send_message(uiw32->tv_dialog->hWnd, IDC_SPIN_BRIGHTNESS, UDM_SETPOS, 0, value);
		break;

	case ui_tag_contrast:
		uiw32_send_message(uiw32->tv_dialog->hWnd, IDC_SPIN_CONTRAST, UDM_SETPOS, 0, value);
		break;

	case ui_tag_saturation:
		uiw32_send_message(uiw32->tv_dialog->hWnd, IDC_SPIN_SATURATION, UDM_SETPOS, 0, value);
		break;

	case ui_tag_hue:
		uiw32_send_message(uiw32->tv_dialog->hWnd, IDC_SPIN_HUE, UDM_SETPOS, 0, value);
		break;

	case ui_tag_ntsc_scaling:
		uiw32_send_message(uiw32->tv_dialog->hWnd, IDC_BN_NTSC_SCALING, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	case ui_tag_cmp_fs:
		uiw32_combo_box_select_by_data(uiw32->tv_dialog->hWnd, IDC_CB_FS, value);
		break;

	case ui_tag_cmp_fsc:
		uiw32_combo_box_select_by_data(uiw32->tv_dialog->hWnd, IDC_CB_FSC, value);
		break;

	case ui_tag_cmp_system:
		uiw32_combo_box_select_by_data(uiw32->tv_dialog->hWnd, IDC_CB_SYSTEM, value);
		break;

	case ui_tag_cmp_colour_killer:
		uiw32_send_message(uiw32->tv_dialog->hWnd, IDC_BN_COLOUR_KILLER, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	default:
		break;
	}
}

static void vo_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct uiw32_dialog *dlg = sptr;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	//const void *data = uimsg->data;

	switch (tag) {

	case ui_tag_tv_dialog:
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

	case ui_tag_ccr:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_RENDERER, value);
		break;

	case ui_tag_picture:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_PICTURE, value);
		break;

	case ui_tag_tv_input:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_TV_INPUT, value);
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - signal handlers

// Unlike checkboxes in menus, altering state in a dialog _does_ update what
// the UI displays without further action.  However, it doesn't hurt to receive
// the update message, so still using a client id of -1 when calling
// ui_update_state().

static INT_PTR CALLBACK tv_proc(struct uiw32_dialog *dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	// hwnd is the handle for the dialog window, i.e. uiw32->tv.window

	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->idFrom) {
		case IDC_SPIN_VOLUME:
			if (xroar.ao_interface) {
				HWND vo_volume = GetDlgItem(dlg->hWnd, IDC_SPIN_VOLUME);
				sound_set_volume(xroar.ao_interface->sound_interface, (int16_t)SendMessage(vo_volume, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_BRIGHTNESS:
			if (xroar.vo_interface) {
				HWND vo_brightness = GetDlgItem(dlg->hWnd, IDC_SPIN_BRIGHTNESS);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_brightness, (int16_t)SendMessage(vo_brightness, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_CONTRAST:
			if (xroar.vo_interface) {
				HWND vo_contrast = GetDlgItem(dlg->hWnd, IDC_SPIN_CONTRAST);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_contrast, (int16_t)SendMessage(vo_contrast, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_SATURATION:
			if (xroar.vo_interface) {
				HWND vo_saturation = GetDlgItem(dlg->hWnd, IDC_SPIN_SATURATION);
				DELEGATE_SAFE_CALL(xroar.vo_interface->set_saturation, (int16_t)SendMessage(vo_saturation, UDM_GETPOS, (WPARAM)0, (LPARAM)0));
			}
			break;

		case IDC_SPIN_HUE:
			if (xroar.vo_interface) {
				HWND vo_hue = GetDlgItem(dlg->hWnd, IDC_SPIN_HUE);
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
			case IDC_CB_TV_INPUT:
				ui_update_state(-1, ui_tag_tv_input, value, NULL);
				break;

			case IDC_CB_PICTURE:
				ui_update_state(-1, ui_tag_picture, value, NULL);
				break;

			case IDC_CB_RENDERER:
				ui_update_state(-1, ui_tag_ccr, value, NULL);
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
					HWND tb_ntsc_scaling = GetDlgItem(dlg->hWnd, IDC_BN_NTSC_SCALING);
					int value = !(SendMessage(tb_ntsc_scaling, BM_GETCHECK, 0, 0) == BST_CHECKED);
					vo_set_ntsc_scaling(xroar.vo_interface, 1, value);
				}
				return FALSE;

			case IDC_BN_COLOUR_KILLER:
				if (xroar.vo_interface) {
					HWND tb_cmp_colour_killer = GetDlgItem(dlg->hWnd, IDC_BN_COLOUR_KILLER);
					int value = !(SendMessage(tb_cmp_colour_killer, BM_GETCHECK, 0, 0) == BST_CHECKED);
					vo_set_cmp_colour_killer(xroar.vo_interface, 1, value);
				}
				return FALSE;

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
