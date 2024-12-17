/** \file
 *
 *  \brief ROM filename database.
 *
 *  \copyright Copyright 2012-2024 Ciaran Anscomb
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "c-strcase.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "path.h"
#include "romlist.h"
#include "xroar.h"

/* User defined rom lists */
struct romlist {
	char *name;
	struct slist *list;
	_Bool flag;
};

/* List containing all defined rom lists */
static struct slist *romlist_list = NULL;

#ifndef HAVE_WASM
static char const * const rom_extensions[] = {
	"", ".rom", ".ROM", ".dgn", ".DGN"
};
#endif

static int compare_entry(const void *a, const void *b) {
	const struct romlist *aa = a;
	const char *bb = b;
	return strcmp(aa->name, bb);
}

/**************************************************************************/

static struct romlist *new_romlist(const char *name) {
	struct romlist *new = xmalloc(sizeof(*new));
	new->name = xstrdup(name);
	new->list = NULL;
	new->flag = 0;
	return new;
}

static void free_romlist(struct romlist *romlist) {
	if (!romlist) return;
	struct slist *list = romlist->list;
	while (list) {
		void *data = list->data;
		list = slist_remove(list, data);
		free(data);
	}
	free(romlist->name);
	free(romlist);
}

static struct romlist *find_romlist(const char *name) {
	struct slist *entry = slist_find_custom(romlist_list, name, (slist_cmp_func)compare_entry);
	if (!entry) return NULL;
	return entry->data;
}

// Assign a romlist.  Overwrites any existing list with provided name.
void romlist_assign(const char *name, struct sdsx_list *values) {
	if (!name)
		return;

	// find if there's an old list with this name
	struct romlist *old_list = find_romlist(name);
	if (old_list) {
		// if so, remove its reference in romlist_list
		romlist_list = slist_remove(romlist_list, old_list);
	}

	// if supplied list is empty, we're done (deleted any old entry)
	if (!values || values->len == 0)
		return;

	struct romlist *new_list = new_romlist(name);

	for (unsigned i = 0; i < values->len; i++) {
		const char *value = values->elem[i];
		if (value[0] == '@' && 0 == strcmp(value+1, name)) {
			// reference to this list - append current contents
			if (old_list) {
				new_list->list = slist_concat(new_list->list, old_list->list);
				old_list->list = NULL;
			}
		} else {
			/* otherwise just add a new entry */
			new_list->list = slist_append(new_list->list, xstrdup(value));
		}
	}
	if (old_list) {
		free_romlist(old_list);
	}
	// add new list to romlist_list
	romlist_list = slist_append(romlist_list, new_list);
}

/* Find a ROM within ROMPATH */
#ifndef HAVE_WASM
static sds find_rom(const char *romname) {
	sds path = NULL;
	if (!romname) return NULL;
	sds filename = sdsnew(romname);
	size_t filename_len = sdslen(filename);
	const char *rompath = xroar.cfg.file.rompath ? xroar.cfg.file.rompath : "";
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(rom_extensions); i++) {
		sdssetlen(filename, filename_len);
		filename = sdscat(filename, rom_extensions[i]);
		path = find_in_path(rompath, filename);
		if (path) break;
	}
	sdsfree(filename);
	return path;
}
#endif

/* Attempt to find a ROM image.  If name starts with '@', search the named
 * list for the first accessible entry, otherwise search for a single entry. */
sds romlist_find(const char *name) {
	if (!name) return NULL;
	/* not prefixed with an '@'?  then it's not a list! */
	if (name[0] != '@') {
#ifdef HAVE_WASM
		// For WebAssembly we short-circuit the ROM list and only
		// consider the first non-list entry we encounter.  We also
		// ensure it ends with ".rom".
		sds filename = sdsnew(name);
		size_t filename_length = sdslen(filename);
		if (filename_length >= 4 &&
		    (0 == c_strcasecmp(".ccc", filename + filename_length - 4) ||
		     0 == c_strcasecmp(".rom", filename + filename_length - 4))) {
			return filename;
		}
		filename = sdscat(filename, ".rom");
		return filename;
#else
		return find_rom(name);
#endif
	}
	struct romlist *romlist = find_romlist(name+1);
	/* found an appropriate list?  flag it and start scanning it */
	if (!romlist)
		return NULL;
	struct slist *iter;
	if (romlist->flag)
		return NULL;
	romlist->flag = 1;
	sds path = NULL;
	for (iter = romlist->list; iter; iter = iter->next) {
		char *ent = iter->data;
		if (ent) {
			//if (ent[0] == '@') {
				if ((path = romlist_find(ent))) {
					break;
				}
			//} else if ((path = find_rom(ent))) {
			//	break;
			//}
		}
	}
	romlist->flag = 0;
	return path;
}

static void print_romlist_entry(struct romlist *list, void *user_data) {
	FILE *f = user_data;
	struct slist *jter;
	if (!user_data) {
		f = stdout;
	}
	if (user_data) {
		fprintf(f, "romlist %s=", list->name);
	} else {
		if (strlen(list->name) > 15) {
			fprintf(f, "\t%s\n\t%16s", list->name, "");
		} else {
			fprintf(f, "\t%-15s ", list->name);
		}
	}
	for (jter = list->list; jter; jter = jter->next) {
		char *str = jter->data;
		if (user_data) {
			sds out = sdsx_quote_str(str);
			if (out) {
				fprintf(f, "%s", out);
				sdsfree(out);
			}
		} else {
			fprintf(f, "%s", str);
		}
		if (jter->next) {
			fputc(',', f);
		}
	}
	fprintf(f, "\n");
}

/* Print a list of defined ROM lists to stdout */
void romlist_print_all(FILE *f) {
	slist_foreach(romlist_list, (slist_iter_func)print_romlist_entry, (void *)f);
}

/* Print list and exit */
void romlist_print(void) {
	printf("ROM lists:\n");
	slist_foreach(romlist_list, (slist_iter_func)print_romlist_entry, NULL);
	exit(EXIT_SUCCESS);
}

static void romlist_free(void *sptr) {
	struct romlist *list = sptr;
	if (list->name)
		free(list->name);
	if (list->list)
		slist_free_full(list->list, (slist_free_func)free);
	free(list);
}

/* Tidy up */
void romlist_shutdown(void) {
	slist_free_full(romlist_list, (slist_free_func)romlist_free);
	romlist_list = NULL;
}
