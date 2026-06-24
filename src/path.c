/** \file
 *
 *  \brief File path searching.
 *
 *  \copyright Copyright 2009-2024 Ciaran Anscomb
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

#ifdef WINDOWS32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#endif

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "c-ctype.h"
#include "c-strcase.h"
#include "sds.h"
#include "sdsx.h"

#include "path.h"

#ifdef WINDOWS32
#define PSEPARATORS "/\\"
#define PSEP "\\"
#define HOMEDIR "USERPROFILE"
#else
#define PSEPARATORS "/"
#define PSEP "/"
#define HOMEDIR "HOME"
#endif

// Wrap SHGetKnownFolderPath(), allocating required space for path converted to
// UTF-8.  Caller should free() result.

#ifdef WINDOWS32
static char *get_known_folder_path_utf8(KNOWNFOLDERID const *rfid, DWORD dwFlags) {
	WCHAR *dir_w;
	if (SHGetKnownFolderPath(rfid, dwFlags, NULL, &dir_w) != S_OK)
		return NULL;
	char *dir = NULL;
	int len = WideCharToMultiByte(CP_UTF8, 0, dir_w, -1, NULL, 0, NULL, NULL);
	if (len > 0) {
		dir = malloc(len + 1);
		WideCharToMultiByte(CP_UTF8, 0, dir_w, -1, dir, len + 1, NULL, NULL);
		dir[len] = 0;
	}
	CoTaskMemFree(dir_w);
	return dir;
}
#endif

// Interpolate variables into a path element or filename (only considers
// leading "~/" for now).

sds path_interp_full(const char *path, uint32_t flags) {
	(void)flags;
	if (!path)
		return NULL;

	sds s = sdsempty();

	if (*path == '~' && strspn(path+1, PSEPARATORS) > 0) {
		const char *home = getenv(HOMEDIR);
		if (home && *home) {
			s = sdscat(s, home);
			s = sdscat(s, PSEP);
		}
		path++;
		path += strspn(path, PSEPARATORS);
#ifdef WINDOWS32
	} else if (*path == '%') {
		const char *next = strchr(path+1, '%');
		if (next) {
			const char *varname = path + 1;
			int length = next - varname;
			KNOWNFOLDERID const *rfid = NULL;
			if (length == 12 && c_strncasecmp(varname, "LOCALAPPDATA", length) == 0) {
				rfid = &FOLDERID_LocalAppData;
			} else if (length == 7 && c_strncasecmp(varname, "PROFILE", length) == 0) {
				rfid = &FOLDERID_Profile;
			} else if (length == 11 && c_strncasecmp(varname, "USERPROFILE", length) == 0) {
				rfid = &FOLDERID_Profile;
			}
			if (rfid) {
				DWORD dwFlags = 0;
				if (flags & PATH_FLAG_CREATE)
					dwFlags |= KF_FLAG_CREATE;
				char *dir = get_known_folder_path_utf8(rfid, dwFlags);
				if (dir) {
					s = sdscat(s, dir);
					free(dir);
				}
			} else {
				const char *dir = getenv(varname);
				if (dir && *dir) {
					s = sdscat(s, dir);
				}
			}
			path = next + 1;
		}
#endif
	}
	s = sdscat(s, path);

	// Create path as directory if it didn't exist already.  On Windows,
	// this is in addition to requesting the creation of any "known
	// directory" above.
	if (flags & PATH_FLAG_CREATE) {
		struct stat statbuf;
		if (stat(s, &statbuf) != 0) {
#ifdef WINDOWS32
			bool err = (_mkdir(s) != 0);
#else
			bool err = (mkdir(s, 0755) != 0);
#endif
			if (err) {
				sdsfree(s);
				return NULL;
			}
		}
	}

	return s;
}

sds path_interp(const char *path) {
	return path_interp_full(path, 0);
}

// Find file within supplied colon-separated path.  In path elements, "~/" at
// the start is expanded to "$HOME/".  (e.g., "\:" to stop a colon being seen
// as a path separator).
//
// Files are only considered if they are regular files (not sockets,
// directories, etc.) and are readable by the user.  This is not intended as a
// security check, just a convenience.

sds find_in_path(const char *path, const char *filename) {
	struct stat statbuf;

	if (!filename)
		return NULL;

	sds f = path_interp(filename);
	if (!f)
		return NULL;

	// If no path or filename contains a directory, just test file
	if (path == NULL || *path == 0 || strpbrk(f, PSEPARATORS)
#ifdef WINDOWS32
	    || (c_isalpha(*f) && *(f+1) == ':')
#endif
	    ) {
		// Only consider a file if user has read access.  This is NOT a
		// security check, it's purely for usability.
		if (stat(f, &statbuf) == 0) {
			if (S_ISREG(statbuf.st_mode)) {
				if (access(f, R_OK) == 0) {
					return f;
				}
			}
		}
		sdsfree(f);
		return NULL;
	}
	sdsfree(f);

	const char *p = path;
	size_t plen = strlen(p);
	sds s = sdsempty();

	while (p) {
		sdssetlen(s, 0);
		sds tok = sdsx_tok_str_len(&p, &plen, ":", 0);
		sds pathelem = path_interp(tok);
		sdsfree(tok);

		// Append a '/' if required, then the filename
		s = sdscatsds(s, pathelem);
		if (sdslen(s) == 0) {
			s = sdscat(s, "." PSEP);
		} else if (strspn(s + sdslen(s) - 1, PSEPARATORS) == 0) {
			s = sdscat(s, PSEP);
		}
		sdsfree(pathelem);
		s = sdscat(s, filename);

		// Return this one if file is valid
		if (stat(s, &statbuf) == 0) {
			if (S_ISREG(statbuf.st_mode)) {
				if (access(s, R_OK) == 0) {
					return s;
				}
			}
		}
	}
	sdsfree(s);
	return NULL;
}
