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
#define UIW32_TAG_VALUE(t) ((int)((int8_t)((t) & 0xff)))

// Extend the UI tags for our own purposes

enum {
	uiw32_tag_joystick_right = ui_num_tags,
	uiw32_tag_joystick_left,
	uiw32_tag_config_save,
};

struct xconfig_enum;

struct ui_windows32_interface {
	struct ui_sdl2_interface ui_sdl2_interface;

	// Messenger client ID
	int msgr_client_id;

	HMENU top_menu;
	HMENU machine_menu;
	HMENU cartridge_menu;
	HMENU right_joystick_menu;
	HMENU left_joystick_menu;

	int max_machine_id;
	int max_cartridge_id;
	int max_joystick_id;

	// About dialog
	struct {
		HWND window;
	} about;
};

extern HWND windows32_main_hwnd;

/// Various initialisation required for Windows32.
int windows32_init(_Bool alloc_console);

/// Cleanup before exit.
void windows32_shutdown(void);

// Create "About" dialog
void uiw32_create_about_window(struct ui_windows32_interface *);

// Draw a control with DrawText()
void uiw32_drawtext(HWND hDlg, int nIDDlgItem, LPDRAWITEMSTRUCT pDIS, UINT format);

// Draw a control using DrawText(), using DT_PATH_ELLIPSIS format
#define uiw32_drawtext_path(d,i,s) uiw32_drawtext((d), (i), (s), DT_PATH_ELLIPSIS)

// Shortcut for finding handle of a control within a dialog and sending a
// message to it.
LRESULT uiw32_send_message(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam);

#define windows32_send_message_dlg_item(d,i,m,w,l) uiw32_send_message((d), (i), (m), (w), (l))

#define uiw32_bm_getcheck(d,i) (uiw32_send_message((d), (i), BM_GETCHECK, 0, 0) == BST_CHECKED)
#define uiw32_bm_setcheck(d,i,v) uiw32_send_message((d), (i), BM_SETCHECK, (v) ? BST_CHECKED : BST_UNCHECKED, 0)

// These only deal in 16-bit values, so cast appropriately
#define uiw32_udm_getpos(d,i) ((int16_t)uiw32_send_message((d), (i), UDM_GETPOS, 0, 0))
#define uiw32_udm_setpos(d,i,v) uiw32_send_message((d), (i), UDM_SETPOS, 0, (int16_t)(v))

void uiw32_update_radio_menu_from_enum(HMENU menu, struct xconfig_enum *xc_list, unsigned tag);

// Create combo box from enum

void uiw32_combo_box_from_enum(HWND hDlg, int nIDDlgItem, struct xconfig_enum *xc_list);

// Select combo box entry by comparing its data

void uiw32_combo_box_select_by_data(HWND hDlg, int nIDDlgItem, int value);

// Update scrollbar information and redraw.  Returns SCROLLINFO fMask field
// indicating which other fields needed to change.

UINT uiw32_update_scrollbar(HWND hDlg, int nIDDlgItem, int nMin, int nMax, int nPos);

#endif
