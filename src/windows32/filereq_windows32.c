/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

/* This Windows32 code is probably all wrong, but it does seem to work */

#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

/* Windows has a habit of making include order important: */
#include <windows.h>
#include <commdlg.h>

#include "fs.h"
#include "logging.h"
#include "module.h"
#include "vo.h"

#include "windows32/common_windows32.h"

static char *load_filename(char const * const *extensions);
static char *save_filename(char const * const *extensions);

FileReqModule filereq_windows32_module = {
	.common = { .name = "windows32",
	            .description = "Windows file requester" },
	.load_filename = load_filename,
	.save_filename = save_filename
};

static char *filename = NULL;

static const char *lpstrFilter =
	"All\0"             "*.*\0"
	"Binary files\0"    "*.BIN;*.HEX\0"
	"Cassette images\0" "*.ASC;*.BAS;*.CAS;*.WAV\0"
	"Cartridges\0"      "*.ROM;*.CCC\0"
	"Disk images\0"     "*.DMK;*.DSK;*.JVC;*.OS9;*.VDK\0"
	"Snapshots\0"       "*.SNA\0"
	;

static char *load_filename(char const * const *extensions) {
	OPENFILENAME ofn;
	char fn_buf[260];
	int was_fullscreen;

	(void)extensions;  /* unused */
	was_fullscreen = vo_module->is_fullscreen;
	if (vo_module->set_fullscreen && was_fullscreen)
		vo_module->set_fullscreen(0);

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = windows32_main_hwnd;
	ofn.lpstrFile = fn_buf;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(fn_buf);
	ofn.lpstrFilter = lpstrFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR
		| OFN_HIDEREADONLY;

	if (filename)
		free(filename);
	filename = NULL;
	if (GetOpenFileName(&ofn)==TRUE) {
		filename = xstrdup(ofn.lpstrFile);
	}
	if (vo_module->set_fullscreen && was_fullscreen)
		vo_module->set_fullscreen(1);
	return filename;
}

static char *save_filename(char const * const *extensions) {
	OPENFILENAME ofn;
	char fn_buf[260];
	int was_fullscreen;

	(void)extensions;  /* unused */
	was_fullscreen = vo_module->is_fullscreen;
	if (vo_module->set_fullscreen && was_fullscreen)
		vo_module->set_fullscreen(0);

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = windows32_main_hwnd;
	ofn.lpstrFile = fn_buf;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(fn_buf);
	ofn.lpstrFilter = lpstrFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY
		| OFN_OVERWRITEPROMPT;

	if (filename)
		free(filename);
	filename = NULL;
	if (GetSaveFileName(&ofn)==TRUE) {
		filename = xstrdup(ofn.lpstrFile);
	}
	if (vo_module->set_fullscreen && was_fullscreen)
		vo_module->set_fullscreen(1);
	return filename;
}
