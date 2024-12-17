/** \file
 *
 *  \brief ROM CRC database.
 *
 *  \copyright Copyright 2012-2020 Ciaran Anscomb
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "crclist.h"
#include "xroar.h"

/* User defined CRC lists */
struct crclist {
	char *name;
	struct slist *list;
	_Bool flag;
};

/* List containing all defined CRC lists */
static struct slist *crclist_list = NULL;

static int compare_entry(const void *a, const void *b) {
	const struct crclist *aa = a;
	const char *bb = b;
	return strcmp(aa->name, bb);
}

/**************************************************************************/

static struct crclist *new_crclist(const char *name) {
	struct crclist *new = xmalloc(sizeof(*new));
	new->name = xstrdup(name);
	new->list = NULL;
	new->flag = 0;
	return new;
}

static void free_crclist(struct crclist *crclist) {
	if (!crclist) return;
	struct slist *list = crclist->list;
	while (list) {
		void *data = list->data;
		list = slist_remove(list, data);
		free(data);
	}
	free(crclist->name);
	free(crclist);
}

static struct crclist *find_crclist(const char *name) {
	struct slist *entry = slist_find_custom(crclist_list, name, (slist_cmp_func)compare_entry);
	if (!entry) return NULL;
	return entry->data;
}

// Assign a crclist.  Overwrites any existing list with provided name.
void crclist_assign(const char *name, struct sdsx_list *values) {
	if (!name) {
		return;
	}

	// find if there's an old list with this name
	struct crclist *old_list = find_crclist(name);
	if (old_list) {
		// if so, remove its reference in crclist_list
		crclist_list = slist_remove(crclist_list, old_list);
	}

	struct crclist *new_list = new_crclist(name);

	for (unsigned i = 0; i < values->len; i++) {
		const char *value = values->elem[i];
		if (value[0] == '@' && 0 == strcmp(value+1, name)) {
			// reference to this list - append current contents
			if (old_list) {
				new_list->list = slist_concat(new_list->list, old_list->list);
				old_list->list = NULL;
			}
		} else {
			// otherwise just add a new entry
			new_list->list = slist_append(new_list->list, xstrdup(value));
		}
	}
	if (old_list) {
		free_crclist(old_list);
	}
	// add new list to crclist_list
	crclist_list = slist_append(crclist_list, new_list);
}

/* convert a string to integer and compare against CRC */
static int crc_match(const char *crc_string, uint32_t crc) {
	long long check = strtoll(crc_string, NULL, 16);
	return (uint32_t)(check & 0xffffffff) == crc;
}

/* Match a provided CRC with values in a list.  Returns 1 if found. */
int crclist_match(const char *name, uint32_t crc) {
	if (!name) return 0;
	/* not prefixed with an '@'?  then it's not a list! */
	if (name[0] != '@') {
		return crc_match(name, crc);
	}
	struct crclist *crclist = find_crclist(name+1);
	/* found an appropriate list?  flag it and start scanning it */
	if (!crclist)
		return 0;
	struct slist *iter;
	if (crclist->flag)
		return 0;
	crclist->flag = 1;
	int match = 0;
	for (iter = crclist->list; iter; iter = iter->next) {
		char *ent = iter->data;
		if (ent) {
			if (ent[0] == '@') {
				if ((match = crclist_match(ent, crc)))
					break;
			} else if ((match = crc_match(ent, crc))) {
				break;
			}
		}
	}
	crclist->flag = 0;
	return match;
}

static void print_crclist_entry(struct crclist *list, void *user_data) {
	FILE *f = user_data;
	struct slist *jter;
	if (!user_data) {
		f = stdout;
	}
	if (user_data) {
		fprintf(f, "crclist %s=", list->name);
	} else {
		if (strlen(list->name) > 15) {
			fprintf(f, "\t%s\n\t%16s", list->name, "");
		} else {
			fprintf(f, "\t%-15s ", list->name);
		}
	}
	for (jter = list->list; jter; jter = jter->next) {
		fprintf(f, "%s", (char *)jter->data);
		if (jter->next) {
			fputc(',', f);
		}
	}
	fprintf(f, "\n");
}

/* Print a list of defined ROM lists to stdout */
void crclist_print_all(FILE *f) {
	slist_foreach(crclist_list, (slist_iter_func)print_crclist_entry, (void *)f);
}

/* Print list and exit */
void crclist_print(void) {
	printf("CRC lists:\n");
	slist_foreach(crclist_list, (slist_iter_func)print_crclist_entry, NULL);
	exit(EXIT_SUCCESS);
}

static void crclist_free(void *sptr) {
	struct crclist *list = sptr;
	if (list->name)
		free(list->name);
	if (list->list)
		slist_free_full(list->list, (slist_free_func)free);
	free(list);
}

/* Tidy up */
void crclist_shutdown(void) {
	slist_free_full(crclist_list, (slist_free_func)crclist_free);
	crclist_list = NULL;
}
