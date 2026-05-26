/** \file
 *
 *  \brief Symbol tables.
 *
 *  \copyright Copyright 2026 Ciaran Anscomb
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

#ifndef XROAR_SYMTAB_H_
#define XROAR_SYMTAB_H_

struct sym_entry;

struct symtab {
	size_t nelems;
	size_t nalloc;
	struct sym_entry *list;
};

struct symtab *symtab_new(void);
void symtab_free(struct symtab *ss);

void symtab_init(struct symtab *ss);
void symtab_clear(struct symtab *ss);

void symtab_include(struct symtab *ss, const char *name);

int32_t sym_find(struct symtab *ss, const char *label);

#endif
