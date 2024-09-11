/** \file
 *
 *  \brief Command-line file requester.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "delegate.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "fs.h"
#include "logging.h"
#include "module.h"
#include "ui.h"

static void *filereq_cli_new(void *cfg);

struct module filereq_cli_module = {
	.name = "cli", .description = "Command-line file requester",
	.new = filereq_cli_new
};

struct cli_filereq_interface {
	struct filereq_interface public;

	sds cwd;
	sds path;
	_Bool exists;
};

static void filereq_cli_free(void *sptr);
static char *load_filename(void *sptr, char const *title);
static char *save_filename(void *sptr, char const *title);

static void *filereq_cli_new(void *cfg) {
	(void)cfg;
	struct cli_filereq_interface *frcli = xmalloc(sizeof(*frcli));
	*frcli = (struct cli_filereq_interface){0};
	frcli->public.free = DELEGATE_AS0(void, filereq_cli_free, frcli);
	frcli->public.load_filename = DELEGATE_AS1(charp, charcp, load_filename, frcli);
	frcli->public.save_filename = DELEGATE_AS1(charp, charcp, save_filename, frcli);
	return frcli;
}

static void filereq_cli_free(void *sptr) {
	struct cli_filereq_interface *frcli = sptr;
	sdsfree(frcli->cwd);
	sdsfree(frcli->path);
	free(frcli);
}

#ifdef WINDOWS32
#define SEPSTR "\\"
#define SEPSET "\\/"
#else
#define SEPSTR "/"
#define SEPSET "/"
#endif

static _Bool issep(char c) {
#ifdef WINDOWS32
	return (c == '/' || c == '\\');
#else
	return (c == '/');
#endif
}

static void printent(void *sptr, void *data) {
	(void)data;
	char *s = sptr;
	printf("%s\n", s);
}

static char *get_filename(struct cli_filereq_interface *frcli, const char *prompt) {
#ifdef WINDOWS32
	const char *home = getenv("USERPROFILE");
#else
	const char *home = getenv("HOME");
#endif

	if (!frcli->cwd) {
		char *cwd = fs_getcwd();
		if (cwd) {
			frcli->cwd = sdsnew(cwd);
			free(cwd);
		} else if (home) {
			frcli->cwd = sdsnew(home);
		} else {
			frcli->cwd = sdsnew(".");
		}
	}

	frcli->exists = 0;

	puts(
"Enter a directory name to change directory and list contents.\n"
"e.g., ""."" on its own to list the current directory.\n"
"Empty string to abort.");

	char buf[256];

	while (1) {
		printf("%s\n%s", frcli->cwd, prompt);
		fflush(stdout);
		char *in = fgets(buf, sizeof(buf), stdin);
		if (!in)
			return NULL;
		char *cr = strrchr(in, '\n');
		if (cr)
			*cr = 0;
		if (*in == 0)
			return NULL;

		sdsfree(frcli->path);
		if (*in == '~' && (*(in+1) == 0 || issep(*(in+1)))) {
			// is ~ or begins with ~/ - path relative to home
			frcli->path = sdsnew(home);
			in++;
			if (*in) {
				frcli->path = sdscat(frcli->path, in);
			}
		} else if (issep(*in)) {
			// begins with / - absolute path
			frcli->path = sdsnew(in);
		} else {
			// relative to cwd
			frcli->path = sdsdup(frcli->cwd);
			frcli->path = sdscat(frcli->path, SEPSTR);
			frcli->path = sdscat(frcli->path, in);
		}
		// remove any trailing dir separator
		frcli->path = sdsx_rtrim(frcli->path, SEPSET);

		struct stat statbuf;
		if (stat(frcli->path, &statbuf) == 0) {
			// if the new path is a directory, list it and loop
			// back to the prompt
			if (S_ISDIR(statbuf.st_mode)) {
				frcli->cwd = sdscpylen(frcli->cwd, frcli->path, sdslen(frcli->path));
				DIR *d = opendir(frcli->cwd);
				if (d) {
					struct slist *l = NULL;
					struct dirent *e;
					while ((e = readdir(d))) {
						l = slist_prepend(l, sdsnew(e->d_name));
					}
					closedir(d);
					l = slist_sort(l, (slist_cmp_func)strcmp);
					slist_foreach(l, (slist_iter_func)printent, NULL);
					slist_free_full(l, (slist_free_func)sdsfree);
				}
				continue;
			}
			frcli->exists = 1;
		}
		// if the new path either doesn't exist, or is not a directory,
		// return it.
		return frcli->path;
	}
}

static char *load_filename(void *sptr, char const *title) {
	struct cli_filereq_interface *frcli = sptr;
	sds prompt = sdsnew(title);
	prompt = sdscat(prompt, "? ");
	char *filename = get_filename(frcli, prompt);
	sdsfree(prompt);
	return filename;
}

static char *save_filename(void *sptr, char const *title) {
	struct cli_filereq_interface *frcli = sptr;
	sds prompt = sdsnew(title);
	prompt = sdscat(prompt, "? ");
	char *filename = get_filename(frcli, prompt);
	sdsfree(prompt);
	if (frcli->exists) {
		char buf[64];
		printf("File exists: overwrite (y/n)? ");
		fflush(stdout);
		char *in = fgets(buf, sizeof(buf), stdin);
		if (!in || (buf[0] != 'y' && buf[0] != 'Y')) {
			printf("Not overwriting.\n");
			fflush(stdout);
			return NULL;
		}
	}
	return filename;
}

