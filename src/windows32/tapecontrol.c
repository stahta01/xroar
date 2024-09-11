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
#include "messenger.h"
#include "tape.h"
#include "ui.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialog.h"
#include "windows32/resources.h"
#include "windows32/tapecontrol.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct uiw32_tc_dialog {
	struct uiw32_dialog dialog;

	int num_programs;
	struct {
		struct tape_file *file;
		char *filename;
		char *position;
	} *programs;
};

static void uiw32_tc_dialog_free(void *);

// UI message reception

static void tc_ui_state_notify(void *sptr, int tag, void *smsg);

// Dialog box procedure

static INT_PTR CALLBACK tc_proc(struct uiw32_dialog *, UINT msg, WPARAM wParam, LPARAM lParam);

// Tape counter update event

static struct event ev_update_tape_counters;
static void update_tape_counters(void *);

// Helper functions

static void clear_programlist(struct uiw32_tc_dialog *);
static void update_programlist(struct uiw32_tc_dialog *);
static void tc_seek(struct tape *tape, int scroll, int value);
static char *ms_to_string(int ms);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create cassettes dialog window

struct uiw32_dialog *uiw32_tc_dialog_new(struct ui_windows32_interface *uiw32) {
	struct uiw32_tc_dialog *tc_dlg = uiw32_dialog_new_sized(sizeof(*tc_dlg), uiw32,
								IDD_DLG_TAPE_CONTROLS,
								ui_tag_tape_dialog, tc_proc);
	struct uiw32_dialog *dlg = &tc_dlg->dialog;
	dlg->free = DELEGATE_AS0(void, uiw32_tc_dialog_free, tc_dlg);

	// Join each UI group we're interested in
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_playing, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, tc_dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_flag_fast, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, tc_dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_flag_pad_auto, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, tc_dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_flag_rewrite, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, tc_dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_input_filename, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, tc_dlg));
	ui_messenger_join_group(dlg->msgr_client_id, ui_tag_tape_output_filename, MESSENGER_NOTIFY_DELEGATE(tc_ui_state_notify, tc_dlg));

	// Initialise program list dialog
	HWND tc_lvs_input_programlist = GetDlgItem(dlg->hWnd, IDC_LVS_INPUT_PROGRAMLIST);
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
	event_init(&ev_update_tape_counters, DELEGATE_AS0(void, update_tape_counters, tc_dlg));

	return dlg;
}

static void uiw32_tc_dialog_free(void *sptr) {
	struct uiw32_tc_dialog *tc_dlg = sptr;
	clear_programlist(tc_dlg);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void update_input_filename(struct uiw32_tc_dialog *, const char *filename);
static void update_tape_playing(struct uiw32_tc_dialog *, _Bool playing);

static void tc_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct uiw32_tc_dialog *tc_dlg = sptr;
	struct uiw32_dialog *dlg = &tc_dlg->dialog;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	const void *data = uimsg->data;

	switch (tag) {

	case ui_tag_tape_input_filename:
		update_input_filename(tc_dlg, (const char *)data);
		break;

	case ui_tag_tape_output_filename:
		uiw32_send_message(dlg->hWnd, IDC_STM_OUTPUT_FILENAME, WM_SETTEXT, 0, (LPARAM)data);
		break;

	case ui_tag_tape_flag_fast:
		uiw32_send_message(dlg->hWnd, IDC_BN_TAPE_FAST, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	case ui_tag_tape_flag_pad_auto:
		uiw32_send_message(dlg->hWnd, IDC_BN_TAPE_PAD_AUTO, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	case ui_tag_tape_flag_rewrite:
		uiw32_send_message(dlg->hWnd, IDC_BN_TAPE_REWRITE, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
		break;

	case ui_tag_tape_playing:
		update_tape_playing(tc_dlg, value);
		break;

	default:
		break;
	}
}

static void update_input_filename(struct uiw32_tc_dialog *tc_dlg, const char *filename) {
	struct uiw32_dialog *dlg = &tc_dlg->dialog;
	uiw32_send_message(dlg->hWnd, IDC_STM_INPUT_FILENAME, WM_SETTEXT, 0, (LPARAM)filename);
	clear_programlist(tc_dlg);
	if (IsWindowVisible(dlg->hWnd)) {
		update_programlist(tc_dlg);
	}
}

static void update_tape_playing(struct uiw32_tc_dialog *tc_dlg, _Bool playing) {
	struct uiw32_dialog *dlg = &tc_dlg->dialog;
	HWND tc_bn_input_play = GetDlgItem(dlg->hWnd, IDC_BN_INPUT_PLAY);
	HWND tc_bn_input_pause = GetDlgItem(dlg->hWnd, IDC_BN_INPUT_PAUSE);
	HWND tc_bn_output_record = GetDlgItem(dlg->hWnd, IDC_BN_OUTPUT_RECORD);
	HWND tc_bn_output_pause = GetDlgItem(dlg->hWnd, IDC_BN_OUTPUT_PAUSE);
	EnableWindow(tc_bn_input_play, !playing ? TRUE : FALSE);
	EnableWindow(tc_bn_input_pause, playing ? TRUE : FALSE);
	EnableWindow(tc_bn_output_record, !playing ? TRUE : FALSE);
	EnableWindow(tc_bn_output_pause, playing ? TRUE : FALSE);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape control - signal handlers

// WM_SHOWWINDOW is sent when the dialog window is about to be shown or hidden.

// Some controls are marked with SS_OWNERDRAW (style=ownerdraw in .win file).
// We receive WM_DRAWITEM events for these when they need to be drawn, and we
// can use a custom style, e.g. using uiw32_drawtext_path() to draw with
// DT_PATH_ELLIPSIS.

// WM_NOTIFY/LVN_GETDISPINFO is Windows asking for information to fill in a
// list-view control.

// WM_COMMAND is received for checkboxes, but Windows does not itself alter the
// control's state.  We query its current value and invert it.  The UI message
// we send will then later be received by dc_ui_state_notify() which will
// update the control.

// WM_HSCROLL is received when the user drags a scrollbar control.

static INT_PTR CALLBACK tc_proc(struct uiw32_dialog *dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	struct uiw32_tc_dialog *tc_dlg = (struct uiw32_tc_dialog *)dlg;

	switch (msg) {

	case WM_SHOWWINDOW:
		if (wParam) {
			// If the window is about to be shown, update the
			// program list and queue the tape counter update
			// event.
			update_programlist(tc_dlg);
			update_tape_counters(tc_dlg);
		} else {
			// Otherwise, dequeue that event.
			event_dequeue(&ev_update_tape_counters);
		}
		return FALSE;

	case WM_DRAWITEM:
		{
			int id = LOWORD(wParam);
			switch (id) {
			case IDC_STM_INPUT_FILENAME:
			case IDC_STM_OUTPUT_FILENAME:
				uiw32_drawtext_path(dlg->hWnd, id, (LPDRAWITEMSTRUCT)lParam);
				return TRUE;

			default:
				break;
			}
		}
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case LVN_GETDISPINFO:
			{
				NMLVDISPINFO *plvdi = (NMLVDISPINFO *)lParam;
				int id = GetDlgCtrlID(plvdi->hdr.hwndFrom);
				int item = plvdi->item.iItem;
				int subitem = plvdi->item.iSubItem;
				UINT mask = plvdi->item.mask;
				if (id == IDC_LVS_INPUT_PROGRAMLIST) {
					if (item < 0 || item >= tc_dlg->num_programs) {
						return TRUE;
					}
					switch (subitem) {
					case 0:
						if (mask & LVIF_TEXT) {
							plvdi->item.pszText = tc_dlg->programs[item].filename;
						}
						break;
					case 1:
						if (mask & LVIF_TEXT) {
							plvdi->item.pszText = tc_dlg->programs[item].position;
						}
						break;
					default:
						break;
					}
					return TRUE;
				}
			}
			break;

		case NM_DBLCLK:
			{
				LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE)lParam;
				int id = GetDlgCtrlID(lpnmitem->hdr.hwndFrom);
				int iItem = lpnmitem->iItem;
				if (id == IDC_LVS_INPUT_PROGRAMLIST) {
					tape_seek_to_file(xroar.tape_interface->tape_input, tc_dlg->programs[iItem].file);
					update_tape_counters(tc_dlg);
				}
				return TRUE;
			}
			break;

		default:
			break;
		}
		break;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			int id = LOWORD(wParam);

			switch (id) {

			// Checkbox toggles

			case IDC_BN_TAPE_FAST:
				ui_update_state(-1, ui_tag_tape_flag_fast, !uiw32_bm_getcheck(dlg->hWnd, id), NULL);  // toggle
				break;

			case IDC_BN_TAPE_PAD_AUTO:
				ui_update_state(-1, ui_tag_tape_flag_pad_auto, !uiw32_bm_getcheck(dlg->hWnd, id), NULL);  // toggle
				break;

			case IDC_BN_TAPE_REWRITE:
				ui_update_state(-1, ui_tag_tape_flag_rewrite, !uiw32_bm_getcheck(dlg->hWnd, id), NULL);  // toggle
				break;

			// Input tape buttons

			case IDC_BN_INPUT_PLAY:
				ui_update_state(-1, ui_tag_tape_playing, 1, NULL);
				break;

			case IDC_BN_INPUT_PAUSE:
				ui_update_state(-1, ui_tag_tape_playing, 0, NULL);
				break;

			case IDC_BN_INPUT_REWIND:
				if (xroar.tape_interface->tape_input) {
					tape_seek(xroar.tape_interface->tape_input, 0, SEEK_SET);
					update_tape_counters(tc_dlg);
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
				ui_update_state(-1, ui_tag_tape_playing, 1, NULL);
				break;

			case IDC_BN_OUTPUT_PAUSE:
				ui_update_state(-1, ui_tag_tape_playing, 0, NULL);
				break;

			case IDC_BN_OUTPUT_REWIND:
				if (xroar.tape_interface && xroar.tape_interface->tape_output) {
					tape_seek(xroar.tape_interface->tape_output, 0, SEEK_SET);
					update_tape_counters(tc_dlg);
				}
				break;

			case IDC_BN_OUTPUT_EJECT:
				xroar_eject_output_tape();
				break;

			case IDC_BN_OUTPUT_INSERT:
				xroar_insert_output_tape();
				break;

			default:
				break;
			}
		}
		break;

	case WM_HSCROLL:
		{
			HWND hDlg = (HWND)lParam;
			int id = GetDlgCtrlID(hDlg);
			SCROLLINFO si = {
				.cbSize = sizeof(SCROLLINFO),
				.fMask = SIF_TRACKPOS
			};
			GetScrollInfo(hDlg, SB_CTL, &si);
			int pos = si.nTrackPos;
			if (id == IDC_SBM_INPUT_POSITION) {
				tc_seek(xroar.tape_interface->tape_input, LOWORD(wParam), pos);
				update_tape_counters(tc_dlg);
			} else if (id == IDC_SBM_OUTPUT_POSITION) {
				tc_seek(xroar.tape_interface->tape_output, LOWORD(wParam), pos);
				update_tape_counters(tc_dlg);
			}
		}
		break;

	default:
		break;
	}
	return FALSE;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape counter update event

// An event is set up before the dialog is shown (WM_INITDIALOG) to poll the
// counters.  It's dequeued when closing the dialog.  This event handler
// requeues itself to update each half second.  This function is also called
// after seeking to immediately update the UI.

static void update_tape_counters(void *sptr) {
	struct uiw32_tc_dialog *tc_dlg = sptr;
	struct uiw32_dialog *dlg = &tc_dlg->dialog;
	HWND tc_stm_input_position = GetDlgItem(dlg->hWnd, IDC_STM_INPUT_POSITION);
	HWND tc_stm_output_position = GetDlgItem(dlg->hWnd, IDC_STM_OUTPUT_POSITION);

	long new_imax = 0, new_ipos = 0;
	if (xroar.tape_interface->tape_input) {
		new_imax = tape_to_ms(xroar.tape_interface->tape_input, xroar.tape_interface->tape_input->size);
		new_ipos = tape_to_ms(xroar.tape_interface->tape_input, xroar.tape_interface->tape_input->offset);
	}
	UINT fMask = uiw32_update_scrollbar(dlg->hWnd, IDC_SBM_INPUT_POSITION,
					    0, new_imax, new_ipos);
	if (fMask & SIF_POS) {
		SendMessage(tc_stm_input_position, WM_SETTEXT, 0, (LPARAM)ms_to_string(new_ipos));
	}

	long new_omax = 0, new_opos = 0;
	if (xroar.tape_interface->tape_output) {
		new_omax = tape_to_ms(xroar.tape_interface->tape_output, xroar.tape_interface->tape_output->size);
		new_opos = tape_to_ms(xroar.tape_interface->tape_output, xroar.tape_interface->tape_output->offset);
	}
	fMask = uiw32_update_scrollbar(dlg->hWnd, IDC_SBM_OUTPUT_POSITION,
					    0, new_omax, new_opos);
	if (fMask & SIF_POS) {
		SendMessage(tc_stm_output_position, WM_SETTEXT, 0, (LPARAM)ms_to_string(new_opos));
	}

	ev_update_tape_counters.at_tick = event_current_tick + EVENT_MS(500);
	event_queue(&UI_EVENT_LIST, &ev_update_tape_counters);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Helper functions

// Clear program list

static void clear_programlist(struct uiw32_tc_dialog *tc_dlg) {
	struct uiw32_dialog *dlg = &tc_dlg->dialog;
	uiw32_send_message(dlg->hWnd, IDC_LVS_INPUT_PROGRAMLIST, LVM_DELETEALLITEMS, 0, 0);
	for (int i = 0; i < tc_dlg->num_programs; i++) {
		free(tc_dlg->programs[i].filename);
		free(tc_dlg->programs[i].position);
		free(tc_dlg->programs[i].file);
	}
	tc_dlg->num_programs = 0;
}

// Populate program list

static void update_programlist(struct uiw32_tc_dialog *tc_dlg) {
	struct uiw32_dialog *dlg = &tc_dlg->dialog;
	HWND tc_lvs_input_programlist = GetDlgItem(dlg->hWnd, IDC_LVS_INPUT_PROGRAMLIST);
	if (!xroar.tape_interface || !xroar.tape_interface->tape_input) {
		clear_programlist(tc_dlg);
		return;
	}
	if (ListView_GetItemCount(tc_lvs_input_programlist) > 0) {
		return;
	}
	struct tape_file *file;
	long old_offset = tape_tell(xroar.tape_interface->tape_input);
	tape_rewind(xroar.tape_interface->tape_input);
	int nprograms = 0;
	while ((file = tape_file_next(xroar.tape_interface->tape_input, 1))) {
		int ms = tape_to_ms(xroar.tape_interface->tape_input, file->offset);
		tc_dlg->programs = xrealloc(tc_dlg->programs, (nprograms + 1) * sizeof(*tc_dlg->programs));
		tc_dlg->programs[nprograms].file = file;
		tc_dlg->programs[nprograms].filename = xstrdup(file->name);
		tc_dlg->programs[nprograms].position = xstrdup(ms_to_string(ms));
		LVITEMA item = {
			.mask = LVIF_TEXT,
			.iItem = nprograms,
			.iSubItem = 0,
			.pszText = LPSTR_TEXTCALLBACK,
		};
		SendMessage(tc_lvs_input_programlist, LVM_INSERTITEM, 0, (LPARAM)&item);
		nprograms++;
	}
	tc_dlg->num_programs = nprograms;
	tape_seek(xroar.tape_interface->tape_input, old_offset, SEEK_SET);
}

// Interpret scroll parameters

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

// Convert milliseconds to string of form mm:ss

static char *ms_to_string(int ms) {
	static char timestr[9];
	int min, sec;
	sec = ms / 1000;
	min = sec / 60;
	sec %= 60;
	min %= 60;
	snprintf(timestr, sizeof(timestr), "%02d:%02d", min, sec);
	return timestr;

// WM_HSCROLL is received when the user drags a scrollbar control.
}
