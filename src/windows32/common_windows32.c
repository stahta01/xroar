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

#include "top-config.h"

/* Windows has a habit of making include order important: */
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <commctrl.h>

#include "xalloc.h"

#include "logging.h"
#include "xconfig.h"

#include "windows32/common_windows32.h"
#include "windows32/guicon.h"

HWND windows32_main_hwnd = NULL;

/** A console window is created if requested, thus this should be called _after_
 * processing options that may call for a console, but _before_ generating any
 * output that should go to that console.
 *
 * Performs various incantations that seem to be required to make networking
 * code work.
 */

int windows32_init(_Bool alloc_console) {
	if (alloc_console) {
		redirect_io_to_console(1024);
	}
	// Windows needs this to do networking
	WORD wVersionRequested;
	WSADATA wsaData;
	wVersionRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVersionRequested, &wsaData) != 0) {
		LOG_MOD_WARN("windows32", "WSAStartup failed\n");
		return -1;
	}
	return 0;
}

void windows32_shutdown(void) {
	WSACleanup();
}

void uiw32_drawtext(HWND hDlg, int nIDDlgItem, LPDRAWITEMSTRUCT pDIS, UINT format) {
	HWND hWnd = GetDlgItem(hDlg, nIDDlgItem);
	int length = SendMessage(hWnd, WM_GETTEXTLENGTH, 0, 0) + 1;
	char *text = xmalloc(length);
	length = SendMessage(hWnd, WM_GETTEXT, length, (LPARAM)text);
	FillRect(pDIS->hDC, &pDIS->rcItem, (HBRUSH)(COLOR_WINDOW+1));
	DrawText(pDIS->hDC, text, length, &pDIS->rcItem, format);
	free(text);
}

LRESULT uiw32_send_message(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam) {
	HWND hWnd = GetDlgItem(hDlg, nIDDlgItem);
	return SendMessage(hWnd, Msg, wParam, lParam);
}

static char *escape_string(const char *str) {
	const char *s = str;
	unsigned size = 1;
	while (*s) {
		++size;
		if (*s == '&')
			++size;
		++s;
	}
	char *r = xmalloc(size);
	s = str;
	char *d = r;
	while (*s) {
		if (*s == '&') {
			*(d++) = *s;
			--size;
			if (size == 0)
				break;
		}
		*(d++) = *(s++);
		--size;
		if (size == 0)
			break;
	}
	*d = 0;
	return r;
}

void uiw32_update_radio_menu_from_enum(HMENU menu, struct xconfig_enum *xc_list, unsigned tag) {
	// Remove old entries
	while (DeleteMenu(menu, 0, MF_BYPOSITION))
		;

	// Add entries
	for (struct xconfig_enum *iter = xc_list; iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		char *label = escape_string(iter->description);
		AppendMenu(menu, MF_STRING, UIW32_TAGV(tag, iter->value), label);
		free(label);
	}
}

void uiw32_combo_box_from_enum(HWND hDlg, int nIDDlgItem, struct xconfig_enum *xc_list) {
	HWND cb_hWnd = GetDlgItem(hDlg, nIDDlgItem);
	if (cb_hWnd == NULL) {
		return;
	}
	for (struct xconfig_enum *iter = xc_list; iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		LRESULT idx = SendMessage(cb_hWnd, CB_ADDSTRING, 0, (LPARAM)iter->description);
		SendMessage(cb_hWnd, CB_SETITEMDATA, idx, iter->value);
	}
}

void uiw32_combo_box_select_by_data(HWND hDlg, int nIDDlgItem, int value) {
	HWND cb_hWnd = GetDlgItem(hDlg, nIDDlgItem);
	LRESULT nitems = SendMessage(cb_hWnd, CB_GETCOUNT, 0, 0);
	if (nitems == CB_ERR || nitems < 1) {
		return;
	}
	for (size_t i = 0; i < (size_t)nitems; ++i) {
		LRESULT data = SendMessage(cb_hWnd, CB_GETITEMDATA, i, 0);
		if (data == value) {
			SendMessage(cb_hWnd, CB_SETCURSEL, i, 0);
			return;
		}
	}
}

UINT uiw32_update_scrollbar(HWND hDlg, int nIDDlgItem, int nMin, int nMax, int nPos) {
	HWND hWnd = GetDlgItem(hDlg, nIDDlgItem);
	SCROLLINFO si = {
		.cbSize = sizeof(SCROLLINFO),
		.fMask = SIF_POS | SIF_RANGE
	};
	GetScrollInfo(hWnd, SB_CTL, &si);
	UINT fMask = 0;
	if (si.nPos != nPos) {
		si.nPos = nPos;
		fMask |= SIF_POS;
	}
	if (si.nMin != nMin || si.nMax != nMax) {
		si.nMin = nMin;
		si.nMax = nMax;
		fMask |= SIF_RANGE;
	}
	si.fMask = fMask;
	SetScrollInfo(hWnd, SB_CTL, &si, TRUE);
	return fMask;
}
