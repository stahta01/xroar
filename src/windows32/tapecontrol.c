/** \file
 *
 *  \brief Windows tape control window.
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
#include "tape.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialogs.h"
#include "windows32/tapecontrol.h"

static INT_PTR CALLBACK tc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void update_programlist(struct ui_windows32_interface *);

static struct event ev_update_tape_counters;
static void update_tape_counters(void *);

static void tc_seek(struct tape *tape, int scroll, int value);

void windows32_tc_create_window(struct ui_windows32_interface *uiw32) {
	// Main dialog window handle
	uiw32->tape.window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_TAPE_CONTROLS), windows32_main_hwnd, (DLGPROC)tc_proc);

	// Initialise program list dialog
	HWND tc_lvs_input_programlist = GetDlgItem(uiw32->tape.window, IDC_LVS_INPUT_PROGRAMLIST);
	LVCOLUMNA col = {
		.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT,
		.fmt = LVCFMT_LEFT,
		.cx = 160,
		.pszText = "Filename",
	};
	SendMessage(tc_lvs_input_programlist, LVM_INSERTCOLUMN, 0, (LPARAM)&col);
	col.cx = 92;
	col.pszText = "Position";
	SendMessage(tc_lvs_input_programlist, LVM_INSERTCOLUMN, 1, (LPARAM)&col);

	// While window displayed, an event triggers updating tape counters
	event_init(&ev_update_tape_counters, DELEGATE_AS0(void, update_tape_counters, uiw32));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape control - update values in UI

static void windows32_tc_update_input_filename(struct ui_windows32_interface *, const char *filename);
static void windows32_tc_update_tape_playing(struct ui_windows32_interface *, int playing);

void windows32_tc_update_state(struct ui_windows32_interface *uiw32,
			       int tag, int value, const void *data) {
	switch (tag) {
	case ui_tag_tape_dialog:
		ShowWindow(uiw32->tape.window, SW_SHOW);
		update_programlist(uiw32);
		break;

	case ui_tag_tape_flags:
		windows32_send_message_dlg_item(uiw32->tape.window, IDC_BN_TAPE_FAST, BM_SETCHECK, (value & TAPE_FAST) ? BST_CHECKED : BST_UNCHECKED, 0);
		windows32_send_message_dlg_item(uiw32->tape.window, IDC_BN_TAPE_PAD_AUTO, BM_SETCHECK, (value & TAPE_PAD_AUTO) ? BST_CHECKED : BST_UNCHECKED, 0);
		windows32_send_message_dlg_item(uiw32->tape.window, IDC_BN_TAPE_REWRITE, BM_SETCHECK, (value & TAPE_REWRITE) ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	case ui_tag_tape_input_filename:
		windows32_tc_update_input_filename(uiw32, (const char *)data);
		break;

	case ui_tag_tape_output_filename:
		windows32_send_message_dlg_item(uiw32->tape.window, IDC_STM_OUTPUT_FILENAME, WM_SETTEXT, 0, (LPARAM)data);
		break;

	case ui_tag_tape_playing:
		windows32_tc_update_tape_playing(uiw32, value);
		break;

	default:
		break;
	}
}

static void windows32_tc_update_input_filename(struct ui_windows32_interface *uiw32, const char *filename) {
	windows32_send_message_dlg_item(uiw32->tape.window, IDC_STM_INPUT_FILENAME, WM_SETTEXT, 0, (LPARAM)filename);
	windows32_send_message_dlg_item(uiw32->tape.window, IDC_LVS_INPUT_PROGRAMLIST, LVM_DELETEALLITEMS, 0, 0);
	for (int i = 0; i < uiw32->tape.num_programs; i++) {
		free(uiw32->tape.programs[i].filename);
		free(uiw32->tape.programs[i].position);
		free(uiw32->tape.programs[i].file);
	}
	uiw32->tape.num_programs = 0;
	if (IsWindowVisible(uiw32->tape.window)) {
		update_programlist(uiw32);
	}
}

static void windows32_tc_update_tape_playing(struct ui_windows32_interface *uiw32, int playing) {
	HWND tc_bn_input_play = GetDlgItem(uiw32->tape.window, IDC_BN_INPUT_PLAY);
	HWND tc_bn_input_pause = GetDlgItem(uiw32->tape.window, IDC_BN_INPUT_PAUSE);
	HWND tc_bn_output_record = GetDlgItem(uiw32->tape.window, IDC_BN_OUTPUT_RECORD);
	HWND tc_bn_output_pause = GetDlgItem(uiw32->tape.window, IDC_BN_OUTPUT_PAUSE);
	EnableWindow(tc_bn_input_play, !playing ? TRUE : FALSE);
	EnableWindow(tc_bn_input_pause, playing ? TRUE : FALSE);
	EnableWindow(tc_bn_output_record, !playing ? TRUE : FALSE);
	EnableWindow(tc_bn_output_pause, playing ? TRUE : FALSE);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape control - signal handlers

static INT_PTR CALLBACK tc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	struct ui_windows32_interface *uiw32 = (struct ui_windows32_interface *)global_uisdl2;
	// hwnd is the handle for the dialog window, i.e. tc_window

	switch (msg) {

	case WM_INITDIALOG:
		ev_update_tape_counters.at_tick = event_current_tick + EVENT_MS(500);
		event_queue(&UI_EVENT_LIST, &ev_update_tape_counters);
		return TRUE;

	case WM_HSCROLL:
		{
			HWND hDlg = (HWND)lParam;
			int nIDDlgItem = GetDlgCtrlID(hDlg);
			SCROLLINFO si = {
				.cbSize = sizeof(SCROLLINFO),
				.fMask = SIF_TRACKPOS
			};
			GetScrollInfo(hDlg, SB_CTL, &si);
			int pos = si.nTrackPos;
			if (nIDDlgItem == IDC_SBM_INPUT_POSITION) {
				tc_seek(xroar.tape_interface->tape_input, LOWORD(wParam), pos);
			} else if (nIDDlgItem == IDC_SBM_OUTPUT_POSITION) {
				tc_seek(xroar.tape_interface->tape_output, LOWORD(wParam), pos);
			}
		}
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case LVN_GETDISPINFO:
			{
				NMLVDISPINFO *plvdi = (NMLVDISPINFO *)lParam;
				int item = plvdi->item.iItem;
				if (item >= uiw32->tape.num_programs) {
					return TRUE;
				}
				switch (plvdi->item.iSubItem) {
				case 0:
					plvdi->item.pszText = uiw32->tape.programs[item].filename;
					break;
				case 1:
					plvdi->item.pszText = uiw32->tape.programs[item].position;
					break;
				default:
					break;
				}
			}
			break;

		case NM_DBLCLK:
			{
				LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE)lParam;
				int iItem = lpnmitem->iItem;
				tape_seek_to_file(xroar.tape_interface->tape_input, uiw32->tape.programs[iItem].file);
			}
			break;

		default:
			break;
		}
		return TRUE;

	case WM_DRAWITEM:
		switch (LOWORD(wParam)) {
		case IDC_STM_INPUT_FILENAME:
			{
				HWND tc_stm_input_filename = GetDlgItem(hwnd, IDC_STM_INPUT_FILENAME);
				LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
				windows32_drawtext_path(tc_stm_input_filename, pDIS);
			}
			return TRUE;

		case IDC_STM_OUTPUT_FILENAME:
			{
				HWND tc_stm_output_filename = GetDlgItem(hwnd, IDC_STM_OUTPUT_FILENAME);
				LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
				windows32_drawtext_path(tc_stm_output_filename, pDIS);
			}
			return TRUE;

		default:
			break;
		}
		return FALSE;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			switch (LOWORD(wParam)) {

			// Checkbox toggles

			case IDC_BN_TAPE_FAST:
				{
					HWND tc_bn_tape_fast = GetDlgItem(hwnd, IDC_BN_TAPE_FAST);
					int set = (SendMessage(tc_bn_tape_fast, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : TAPE_FAST;
					int flags = (tape_get_state(xroar.tape_interface) & ~TAPE_FAST) | set;
					tape_select_state(xroar.tape_interface, flags);
				}
				break;

			case IDC_BN_TAPE_PAD_AUTO:
				{
					HWND tc_bn_tape_pad_auto = GetDlgItem(hwnd, IDC_BN_TAPE_PAD_AUTO);
					int set = (SendMessage(tc_bn_tape_pad_auto, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : TAPE_PAD_AUTO;
					int flags = (tape_get_state(xroar.tape_interface) & ~TAPE_PAD_AUTO) | set;
					tape_select_state(xroar.tape_interface, flags);
				}
				break;

			case IDC_BN_TAPE_REWRITE:
				{
					HWND tc_bn_tape_rewrite = GetDlgItem(hwnd, IDC_BN_TAPE_REWRITE);
					int set = (SendMessage(tc_bn_tape_rewrite, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : TAPE_REWRITE;
					int flags = (tape_get_state(xroar.tape_interface) & ~TAPE_REWRITE) | set;
					tape_select_state(xroar.tape_interface, flags);
				}
				break;

			// Input tape buttons

			case IDC_BN_INPUT_PLAY:
				tape_set_playing(xroar.tape_interface, 1, 1);
				break;

			case IDC_BN_INPUT_PAUSE:
				tape_set_playing(xroar.tape_interface, 0, 1);
				break;

			case IDC_BN_INPUT_REWIND:
				if (xroar.tape_interface->tape_input) {
					tape_seek(xroar.tape_interface->tape_input, 0, SEEK_SET);
				}
				break;

			case IDC_BN_INPUT_EJECT:
				xroar_eject_input_tape();
				break;

			case IDC_BN_INPUT_INSERT:
				xroar_insert_input_tape();
				break;

			// Output tape buttons

			case IDC_BN_OUTPUT_RECORD:
				tape_set_playing(xroar.tape_interface, 1, 1);
				break;

			case IDC_BN_OUTPUT_PAUSE:
				tape_set_playing(xroar.tape_interface, 0, 1);
				break;

			case IDC_BN_OUTPUT_REWIND:
				if (xroar.tape_interface && xroar.tape_interface->tape_output) {
					tape_seek(xroar.tape_interface->tape_output, 0, SEEK_SET);
				}
				break;

			case IDC_BN_OUTPUT_EJECT:
				xroar_eject_output_tape();
				break;

			case IDC_BN_OUTPUT_INSERT:
				xroar_insert_output_tape();
				break;

			// Standard buttons

			case IDOK:
			case IDCANCEL:
				ShowWindow(hwnd, SW_HIDE);
				event_dequeue(&ev_update_tape_counters);
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape control - helper functions

static char *ms_to_string(int ms) {
	static char timestr[9];
	int min, sec;
	sec = ms / 1000;
	min = sec / 60;
	sec %= 60;
	min %= 60;
	snprintf(timestr, sizeof(timestr), "%02d:%02d", min, sec);
	return timestr;
}

static void update_programlist(struct ui_windows32_interface *uiw32) {
	HWND tc_lvs_input_programlist = GetDlgItem(uiw32->tape.window, IDC_LVS_INPUT_PROGRAMLIST);
	if (ListView_GetItemCount(tc_lvs_input_programlist) > 0) {
		return;
	}
	if (!xroar.tape_interface || !xroar.tape_interface->tape_input)
		return;
	struct tape_file *file;
	long old_offset = tape_tell(xroar.tape_interface->tape_input);
	tape_rewind(xroar.tape_interface->tape_input);
	int nprograms = 0;
	while ((file = tape_file_next(xroar.tape_interface->tape_input, 1))) {
		int ms = tape_to_ms(xroar.tape_interface->tape_input, file->offset);
		uiw32->tape.programs = xrealloc(uiw32->tape.programs, (nprograms + 1) * sizeof(*uiw32->tape.programs));
		uiw32->tape.programs[nprograms].file = file;
		uiw32->tape.programs[nprograms].filename = xstrdup(file->name);
		uiw32->tape.programs[nprograms].position = xstrdup(ms_to_string(ms));
		LVITEMA item = {
			.mask = LVIF_TEXT,
			.iItem = nprograms,
			.iSubItem = 0,
			.pszText = LPSTR_TEXTCALLBACK,
		};
		SendMessage(tc_lvs_input_programlist, LVM_INSERTITEM, 0, (LPARAM)&item);
		nprograms++;
	}
	uiw32->tape.num_programs = nprograms;
	tape_seek(xroar.tape_interface->tape_input, old_offset, SEEK_SET);
}

static void update_tape_counters(void *sptr) {
	struct ui_windows32_interface *uiw32 = sptr;
	HWND tc_stm_input_position = GetDlgItem(uiw32->tape.window, IDC_STM_INPUT_POSITION);
	HWND tc_stm_output_position = GetDlgItem(uiw32->tape.window, IDC_STM_OUTPUT_POSITION);

	long new_imax = 0, new_ipos = 0;
	if (xroar.tape_interface->tape_input) {
		new_imax = tape_to_ms(xroar.tape_interface->tape_input, xroar.tape_interface->tape_input->size);
		new_ipos = tape_to_ms(xroar.tape_interface->tape_input, xroar.tape_interface->tape_input->offset);
	}
	UINT fMask = uiw32_update_scrollbar(uiw32->tape.window, IDC_SBM_INPUT_POSITION,
					    0, new_imax, new_ipos);
	if (fMask & SIF_POS) {
		SendMessage(tc_stm_input_position, WM_SETTEXT, 0, (LPARAM)ms_to_string(new_ipos));
	}

	long new_omax = 0, new_opos = 0;
	if (xroar.tape_interface->tape_output) {
		new_omax = tape_to_ms(xroar.tape_interface->tape_output, xroar.tape_interface->tape_output->size);
		new_opos = tape_to_ms(xroar.tape_interface->tape_output, xroar.tape_interface->tape_output->offset);
	}
	fMask = uiw32_update_scrollbar(uiw32->tape.window, IDC_SBM_OUTPUT_POSITION,
					    0, new_omax, new_opos);
	if (fMask & SIF_POS) {
		SendMessage(tc_stm_output_position, WM_SETTEXT, 0, (LPARAM)ms_to_string(new_opos));
	}

	ev_update_tape_counters.at_tick += EVENT_MS(500);
	event_queue(&UI_EVENT_LIST, &ev_update_tape_counters);
}

static void tc_seek(struct tape *tape, int scroll, int value) {
	if (!tape)
		return;
	int seekms = 0;
	switch (scroll) {

	case SB_LINELEFT:
		seekms = tape_to_ms(tape, tape->offset) - 1000;
		break;
	case SB_LINERIGHT:
		seekms = tape_to_ms(tape, tape->offset) + 1000;
		break;
	case SB_PAGELEFT:
		seekms = tape_to_ms(tape, tape->offset) - 5000;
		break;
	case SB_PAGERIGHT:
		seekms = tape_to_ms(tape, tape->offset) + 5000;
		break;

	case SB_THUMBPOSITION:
	case SB_THUMBTRACK:
		seekms = value;
		break;

	default:
		return;
	}

	if (seekms < 0)
		return;
	long seek_to = tape_ms_to(tape, seekms);
	if (seek_to > tape->size)
		seek_to = tape->size;
	tape_seek(tape, seek_to, SEEK_SET);
}
