/** \file
 *
 *  \brief Windows tape control window.
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
#include "tape.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialogs.h"
#include "windows32/tapecontrol.h"

static INT_PTR CALLBACK tc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND tc_window = NULL;
static HWND tc_stm_input_filename = NULL;
static HWND tc_lvs_input_programlist = NULL;
static HWND tc_sbm_input_position = NULL;
static HWND tc_stm_input_position = NULL;
static HWND tc_bn_tape_fast = NULL;
static HWND tc_bn_tape_pad_auto = NULL;
static HWND tc_bn_tape_rewrite = NULL;
static HWND tc_bn_input_play = NULL;
static HWND tc_bn_input_pause = NULL;
static HWND tc_stm_output_filename = NULL;
static HWND tc_sbm_output_position = NULL;
static HWND tc_stm_output_position = NULL;
static HWND tc_bn_output_record = NULL;
static HWND tc_bn_output_pause = NULL;

struct tc_program {
	struct tape_file *file;
	char *filename;
	char *position;
};

static _Bool have_programlist = 0;
static int num_programs = 0;
static struct tc_program *programs = NULL;
static void update_programlist(struct ui_sdl2_interface *uisdl2);

static struct event ev_update_tape_counters;
static void update_tape_counters(void *);

static void tc_seek(struct tape *tape, int scroll, int value);

void windows32_tc_create_window(struct ui_sdl2_interface *uisdl2) {
	// Main dialog window handle
	tc_window = CreateDialog(NULL, MAKEINTRESOURCE(IDD_DLG_TAPE_CONTROLS), windows32_main_hwnd, (DLGPROC)tc_proc);

	// Control handles
	tc_stm_input_filename = GetDlgItem(tc_window, IDC_STM_INPUT_FILENAME);
	tc_lvs_input_programlist = GetDlgItem(tc_window, IDC_LVS_INPUT_PROGRAMLIST);
	tc_sbm_input_position = GetDlgItem(tc_window, IDC_SBM_INPUT_POSITION);
	tc_stm_input_position = GetDlgItem(tc_window, IDC_STM_INPUT_POSITION);
	tc_bn_tape_fast = GetDlgItem(tc_window, IDC_BN_TAPE_FAST);
	tc_bn_tape_pad_auto = GetDlgItem(tc_window, IDC_BN_TAPE_PAD_AUTO);
	tc_bn_tape_rewrite = GetDlgItem(tc_window, IDC_BN_TAPE_REWRITE);
	tc_bn_input_play = GetDlgItem(tc_window, IDC_BN_INPUT_PLAY);
	tc_bn_input_pause = GetDlgItem(tc_window, IDC_BN_INPUT_PAUSE);
	tc_stm_output_filename = GetDlgItem(tc_window, IDC_STM_OUTPUT_FILENAME);
	tc_sbm_output_position = GetDlgItem(tc_window, IDC_SBM_OUTPUT_POSITION);
	tc_stm_output_position = GetDlgItem(tc_window, IDC_STM_OUTPUT_POSITION);
	tc_bn_output_record = GetDlgItem(tc_window, IDC_BN_OUTPUT_RECORD);
	tc_bn_output_pause = GetDlgItem(tc_window, IDC_BN_OUTPUT_PAUSE);

	// Initialise program list dialog
	LVCOLUMNA col = {
		.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT,
		.fmt = LVCFMT_LEFT,
		.cx = 160,
		.pszText = "Filename",
	};
	SendMessage(tc_lvs_input_programlist, LVM_INSERTCOLUMN, 0, (LPARAM)&col);
	col.cx = 80;
	col.pszText = "Position";
	SendMessage(tc_lvs_input_programlist, LVM_INSERTCOLUMN, 1, (LPARAM)&col);

	// While window displayed, an event triggers updating tape counters
	event_init(&ev_update_tape_counters, DELEGATE_AS0(void, update_tape_counters, uisdl2));
}

void windows32_tc_show_window(struct ui_sdl2_interface *uisdl2) {
	ShowWindow(tc_window, SW_SHOW);
	update_programlist(uisdl2);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape control - update values in UI

void windows32_tc_update_tape_state(struct ui_sdl2_interface *uisdl2, int flags) {
	(void)uisdl2;
	SendMessage(tc_bn_tape_fast, BM_SETCHECK, (flags & TAPE_FAST) ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessage(tc_bn_tape_pad_auto, BM_SETCHECK, (flags & TAPE_PAD_AUTO) ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessage(tc_bn_tape_rewrite, BM_SETCHECK, (flags & TAPE_REWRITE) ? BST_CHECKED : BST_UNCHECKED, 0);
}

void windows32_tc_update_input_filename(struct ui_sdl2_interface *uisdl2, const char *filename) {
	SendMessage(tc_stm_input_filename, WM_SETTEXT, 0, (LPARAM)filename);
	SendMessage(tc_lvs_input_programlist, LVM_DELETEALLITEMS, 0, 0);
	for (int i = 0; i < num_programs; i++) {
		free(programs[i].filename);
		free(programs[i].position);
		free(programs[i].file);
	}
	num_programs = 0;
	have_programlist = 0;
	if (IsWindowVisible(tc_window)) {
		update_programlist(uisdl2);
	}
}

void windows32_tc_update_output_filename(struct ui_sdl2_interface *uisdl2, const char *filename) {
	(void)uisdl2;
	SendMessage(tc_stm_output_filename, WM_SETTEXT, 0, (LPARAM)filename);
}

void windows32_tc_update_tape_playing(struct ui_sdl2_interface *uisdl2, int playing) {
	(void)uisdl2;
	EnableWindow(tc_bn_input_play, !playing ? TRUE : FALSE);
	EnableWindow(tc_bn_input_pause, playing ? TRUE : FALSE);
	EnableWindow(tc_bn_output_record, !playing ? TRUE : FALSE);
	EnableWindow(tc_bn_output_pause, playing ? TRUE : FALSE);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape control - signal handlers

static INT_PTR CALLBACK tc_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	(void)hwnd;
	switch (msg) {

	case WM_INITDIALOG:
		ev_update_tape_counters.at_tick = event_current_tick + EVENT_MS(500);
		event_queue(&UI_EVENT_LIST, &ev_update_tape_counters);
		return TRUE;

	case WM_HSCROLL:
		if ((HWND)lParam == tc_sbm_input_position) {
			tc_seek(xroar_tape_interface->tape_input, LOWORD(wParam), HIWORD(wParam));
		} else if ((HWND)lParam == tc_sbm_output_position) {
			tc_seek(xroar_tape_interface->tape_output, LOWORD(wParam), HIWORD(wParam));
		}
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case LVN_GETDISPINFO:
			{
				NMLVDISPINFO *plvdi = (NMLVDISPINFO *)lParam;
				int item = plvdi->item.iItem;
				if (!have_programlist || item >= num_programs) {
					return TRUE;
				}
				switch (plvdi->item.iSubItem) {
				case 0:
					plvdi->item.pszText = programs[item].filename;
					break;
				case 1:
					plvdi->item.pszText = programs[item].position;
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
				tape_seek_to_file(xroar_tape_interface->tape_input, programs[iItem].file);
			}
			break;

		default:
			break;
		}
		return TRUE;

	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			switch (LOWORD(wParam)) {

			// Checkbox toggles

			case IDC_BN_TAPE_FAST:
				{
					int set = (SendMessage(tc_bn_tape_fast, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : TAPE_FAST;
					int flags = (tape_get_state(xroar_tape_interface) & ~TAPE_FAST) | set;
					tape_select_state(xroar_tape_interface, flags);
				}
				break;

			case IDC_BN_TAPE_PAD_AUTO:
				{
					int set = (SendMessage(tc_bn_tape_pad_auto, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : TAPE_PAD_AUTO;
					int flags = (tape_get_state(xroar_tape_interface) & ~TAPE_PAD_AUTO) | set;
					tape_select_state(xroar_tape_interface, flags);
				}
				break;

			case IDC_BN_TAPE_REWRITE:
				{
					int set = (SendMessage(tc_bn_tape_rewrite, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : TAPE_REWRITE;
					int flags = (tape_get_state(xroar_tape_interface) & ~TAPE_REWRITE) | set;
					tape_select_state(xroar_tape_interface, flags);
				}
				break;

			// Input tape buttons

			case IDC_BN_INPUT_PLAY:
				tape_set_playing(xroar_tape_interface, 1, 1);
				break;

			case IDC_BN_INPUT_PAUSE:
				tape_set_playing(xroar_tape_interface, 0, 1);
				break;

			case IDC_BN_INPUT_REWIND:
				if (xroar_tape_interface->tape_input) {
					tape_seek(xroar_tape_interface->tape_input, 0, SEEK_SET);
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
				tape_set_playing(xroar_tape_interface, 1, 1);
				break;

			case IDC_BN_OUTPUT_PAUSE:
				tape_set_playing(xroar_tape_interface, 0, 1);
				break;

			case IDC_BN_OUTPUT_REWIND:
				if (xroar_tape_interface && xroar_tape_interface->tape_output) {
					tape_seek(xroar_tape_interface->tape_output, 0, SEEK_SET);
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
				ShowWindow(tc_window, SW_HIDE);
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

static void update_programlist(struct ui_sdl2_interface *uisdl2) {
	(void)uisdl2;
	if (have_programlist)
		return;
	if (!xroar_tape_interface || !xroar_tape_interface->tape_input)
		return;
	struct tape_file *file;
	long old_offset = tape_tell(xroar_tape_interface->tape_input);
	tape_rewind(xroar_tape_interface->tape_input);
	num_programs = 0;
	while ((file = tape_file_next(xroar_tape_interface->tape_input, 1))) {
		int ms = tape_to_ms(xroar_tape_interface->tape_input, file->offset);
		programs = xrealloc(programs, (num_programs + 1) * sizeof(struct tc_program));
		programs[num_programs].file = file;
		programs[num_programs].filename = xstrdup(file->name);
		programs[num_programs].position = xstrdup(ms_to_string(ms));
		LVITEMA item = {
			.mask = LVIF_TEXT,
			.iItem = num_programs,
			.iSubItem = 0,
			.pszText = LPSTR_TEXTCALLBACK,
		};
		SendMessage(tc_lvs_input_programlist, LVM_INSERTITEM, 0, (LPARAM)&item);
		num_programs++;
	}
	tape_seek(xroar_tape_interface->tape_input, old_offset, SEEK_SET);
	have_programlist = 1;
}

static void update_tape_counters(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	(void)uisdl2;

	static long imax = -1, ipos = -1;
	long new_imax = 0, new_ipos = 0;
	if (xroar_tape_interface->tape_input) {
		new_imax = tape_to_ms(xroar_tape_interface->tape_input, xroar_tape_interface->tape_input->size);
		new_ipos = tape_to_ms(xroar_tape_interface->tape_input, xroar_tape_interface->tape_input->offset);
	}
	SCROLLINFO si = {
		.cbSize = sizeof(SCROLLINFO),
		.fMask = 0,
		.nMin = 0,
	};
	if (imax != new_imax) {
		imax = new_imax;
		si.fMask |= SIF_RANGE;
		si.nMax = imax / 16;
	}
	if (ipos != new_ipos) {
		ipos = new_ipos;
		si.fMask |= SIF_POS;
		si.nPos = ipos / 16;
		SendMessage(tc_stm_input_position, WM_SETTEXT, 0, (LPARAM)ms_to_string(new_ipos));
	}
	if (si.fMask) {
		SendMessage(tc_sbm_input_position, SBM_SETSCROLLINFO, (WPARAM)TRUE, (LPARAM)&si);
	}

	static long omax = -1, opos = -1;
	long new_omax = 0, new_opos = 0;
	if (xroar_tape_interface->tape_output) {
		new_omax = tape_to_ms(xroar_tape_interface->tape_output, xroar_tape_interface->tape_output->size);
		new_opos = tape_to_ms(xroar_tape_interface->tape_output, xroar_tape_interface->tape_output->offset);
	}
	si.fMask = 0;
	if (omax != new_omax) {
		omax = new_omax;
		si.fMask |= SIF_RANGE;
		si.nMax = omax / 16;
	}
	if (opos != new_opos) {
		opos = new_opos;
		si.fMask |= SIF_POS;
		si.nPos = opos / 16;
		SendMessage(tc_stm_output_position, WM_SETTEXT, 0, (LPARAM)ms_to_string(new_opos));
	}
	if (si.fMask) {
		SendMessage(tc_sbm_output_position, SBM_SETSCROLLINFO, (WPARAM)TRUE, (LPARAM)&si);
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
		seekms = value * 16;
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
