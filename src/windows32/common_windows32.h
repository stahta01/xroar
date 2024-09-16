/** \file
 *
 *  \brief Windows user-interface common functions.
 *
 *  \copyright Copyright 2006-2024 Ciaran Anscomb
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

#ifndef XROAR_COMMON_WINDOWS32_H_
#define XROAR_COMMON_WINDOWS32_H_

#include <windows.h>

#include "sdl2/common.h"

#define UIW32_TAG(t) (((t) & 0x7f) << 8)
#define UIW32_TAGV(t,v) (UIW32_TAG(t) | ((v) & 0xff))
#define UIW32_TAG_TYPE(t) (((t) >> 8) & 0x7f)
#define UIW32_TAG_VALUE(t) ((t) & 0xff)

struct xconfig_enum;

struct ui_windows32_interface {
	struct ui_sdl2_interface ui_sdl2_interface;

	HMENU top_menu;
	HMENU machine_menu;
	HMENU cartridge_menu;
	HMENU right_joystick_menu;
	HMENU left_joystick_menu;

	// Cassette tapes dialog
	struct {
		HWND window;
		int num_programs;
		struct {
			struct tape_file *file;
			char *filename;
			char *position;
		} *programs;
	} tape;

	// Floppy disks dialog
	struct {
		HWND window;
	} disk;

	// Printer control dialog
	struct {
		HWND window;
	} printer;
};

extern HWND windows32_main_hwnd;

/// Various initialisation required for Windows32.
int windows32_init(_Bool alloc_console);

/// Cleanup before exit.
void windows32_shutdown(void);

// Draw a control using DrawText() with DT_PATH_ELLIPSIS
void windows32_drawtext_path(HWND hWnd, LPDRAWITEMSTRUCT pDIS);

// Shortcut for finding handle of a control within a dialog and sending a
// message to it.
LRESULT windows32_send_message_dlg_item(HWND hDlg, int nIDDlgItem, UINT Msg,
					WPARAM wParam, LPARAM lParam);

void uiw32_update_radio_menu_from_enum(HMENU menu, struct xconfig_enum *xc_list, unsigned tag);

// Create combo box from enum

void uiw32_combo_box_from_enum(HWND hDlg, int nIDDlgItem, struct xconfig_enum *xc_list);

// Select combo box entry by comparing its data

void uiw32_combo_box_select_by_data(HWND hDlg, int nIDDlgItem, int value);

// Update scrollbar information and redraw.  Returns SCROLLINFO fMask field
// indicating which other fields needed to change.

UINT uiw32_update_scrollbar(HWND hDlg, int nIDDlgItem, int nMin, int nMax, int nPos);

#endif
