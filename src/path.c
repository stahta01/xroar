/*

File path searching

Copyright 2009-2020 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

/* Find file within supplied colon-separated path.  In path elements, "~/" at
 * the start is expanded to "$HOME/".
 * (e.g., "\:" to stop a colon being seen as a path separator).
 *
 * Files are only considered if they are regular files (not sockets,
 * directories, etc.) and are readable by the user.  This is not intended as a
 * security check, just a convenience. */

sds find_in_path(const char *path, const char *filename) {
	struct stat statbuf;
	const char *home;

	if (filename == NULL)
		return NULL;
	// If no path or filename contains a directory, just test file
	if (path == NULL || *path == 0 || strpbrk(filename, PSEPARATORS)) {
		// Only consider a file if user has read access.  This is NOT a
		// security check, it's purely for usability.
		if (stat(filename, &statbuf) == 0) {
			if (S_ISREG(statbuf.st_mode)) {
				if (access(filename, R_OK) == 0) {
					return sdsnew(filename);
				}
			}
		}
		return NULL;
	}

	home = getenv(HOMEDIR);
	if (*home == 0)
		home = NULL;

	const char *p = path;
	size_t plen = strlen(p);
	sds s = sdsempty();

	while (p) {
		sdssetlen(s, 0);
		sds pathelem = sdsx_tok_str_len(&p, &plen, ":", 0);

		// Prefix $HOME if path elem starts "~/"
		if (home && *pathelem == '~' && strspn(pathelem+1, PSEPARATORS) > 0) {
			s = sdscat(s, home);
			pathelem = sdsx_replace_substr(pathelem, 2, -1);
			if (strspn(s + sdslen(s) - 1, PSEPARATORS) == 0) {
				s = sdscat(s, PSEP);
			}
		}

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
