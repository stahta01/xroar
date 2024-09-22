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

	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_fs, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_fsc, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_system, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_cmp_colour_killer, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_ccr, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_picture, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_ntsc_scaling, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_brightness, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_contrast, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_saturation, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_hue, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_gain, MESSENGER_NOTIFY_DELEGATE(vo_ui_state_notify, dlg));

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

// UI message reception

static void vo_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct uiw32_dialog *dlg = sptr;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	//const void *data = uimsg->data;

	switch (tag) {

	// Video

	case ui_tag_cmp_fs:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_FS, value);
		break;

	case ui_tag_cmp_fsc:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_FSC, value);
		break;

	case ui_tag_cmp_system:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_SYSTEM, value);
		break;

	case ui_tag_cmp_colour_killer:
		uiw32_send_message(dlg->hWnd, IDC_BN_COLOUR_KILLER, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	case ui_tag_ccr:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_RENDERER, value);
		break;

	case ui_tag_picture:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_PICTURE, value);
		break;

	case ui_tag_ntsc_scaling:
		uiw32_send_message(dlg->hWnd, IDC_BN_NTSC_SCALING, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	case ui_tag_tv_input:
		uiw32_combo_box_select_by_data(dlg->hWnd, IDC_CB_TV_INPUT, value);
		break;

	case ui_tag_brightness:
		uiw32_udm_setpos(dlg->hWnd, IDC_SPIN_BRIGHTNESS, value);
		break;

	case ui_tag_contrast:
		uiw32_udm_setpos(dlg->hWnd, IDC_SPIN_CONTRAST, value);
		break;

	case ui_tag_saturation:
		uiw32_udm_setpos(dlg->hWnd, IDC_SPIN_SATURATION, value);
		break;

	case ui_tag_hue:
		uiw32_udm_setpos(dlg->hWnd, IDC_SPIN_HUE, value);
		break;

	// Audio

	case ui_tag_gain:
		uiw32_udm_setpos(dlg->hWnd, IDC_SPIN_VOLUME, value);
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
			{
				int value = uiw32_udm_getpos(dlg->hWnd, IDC_SPIN_VOLUME);
				ui_update_state(-1, ui_tag_gain, value, NULL);
			}
			break;

		case IDC_SPIN_BRIGHTNESS:
			{
				int value = uiw32_udm_getpos(dlg->hWnd, IDC_SPIN_BRIGHTNESS);
				ui_update_state(-1, ui_tag_brightness, value, NULL);
			}
			break;

		case IDC_SPIN_CONTRAST:
			{
				int value = uiw32_udm_getpos(dlg->hWnd, IDC_SPIN_CONTRAST);
				ui_update_state(-1, ui_tag_contrast, value, NULL);
			}
			break;

		case IDC_SPIN_SATURATION:
			{
				int value = uiw32_udm_getpos(dlg->hWnd, IDC_SPIN_SATURATION);
				ui_update_state(-1, ui_tag_saturation, value, NULL);
			}
			break;

		case IDC_SPIN_HUE:
			{
				int value = uiw32_udm_getpos(dlg->hWnd, IDC_SPIN_HUE);
				ui_update_state(-1, ui_tag_hue, value, NULL);
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
				ui_update_state(-1, ui_tag_cmp_fs, old_value, NULL);
				break;

			case IDC_CB_FSC:
				ui_update_state(-1, ui_tag_cmp_fsc, old_value, NULL);
				break;

			case IDC_CB_SYSTEM:
				ui_update_state(-1, ui_tag_cmp_system, old_value, NULL);
				break;

			default: break;
			}
		} else if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);

			switch (id) {
			case IDC_BN_NTSC_SCALING:
				{
					int value = !uiw32_bm_getcheck(dlg->hWnd, IDC_BN_NTSC_SCALING);
					ui_update_state(-1, ui_tag_ntsc_scaling, value, NULL);
				}
				return FALSE;

			case IDC_BN_COLOUR_KILLER:
				{
					int value = !uiw32_bm_getcheck(dlg->hWnd, IDC_BN_COLOUR_KILLER);
					ui_update_state(-1, ui_tag_cmp_colour_killer, value, NULL);
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
