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

#include "top-config.h"

// Comment this out for debugging
#define SYM_DEBUG(...)

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "c-strcase.h"
#include "xalloc.h"

#include "logging.h"
#include "symtab.h"

#ifndef SYM_DEBUG
#define SYM_DEBUG(...) LOG_DEBUG(__VA_ARGS__)
#define SYM_MOD_DEBUG(l,...) LOG_MOD_DEBUG(l, "symtab", __VA_ARGS__)
#else
#define SYM_MOD_DEBUG(l,...)
#endif

struct sym_entry {
	const char *label;
	int32_t A;
};

enum {
	SYMTAB_INCLUDE,
	SYMTAB_SYM,
};

struct symtab_def_internal {
	int type;
	union {
		struct {
			const char *table;
		} include;
		struct {
			const char *label;
			int32_t A;
		} sym;
	};
};

#define INC(t)   { .type = SYMTAB_INCLUDE, .include = { .table = (t) } }
#define SYM(s,a) { .type = SYMTAB_SYM, .sym = { .label = (s), .A = (a) } }

struct symtab_def_internal symtab_basic69[] = {
	SYM("__POLCAT", 0xa000),
	SYM("__CHROUT", 0xa002),
	SYM("__CSRDON", 0xa004),
	SYM("__BLKIN",  0xa006),
	SYM("__BLKOUT", 0xa008),
	SYM("__JOYIN",  0xa00a),
	SYM("__WRTLDR", 0xa00c),
};

struct symtab_def_internal symtab_dragon[] = {
	INC("basic69"),

	SYM("_HWINIT",  0x8000),
	SYM("_SWINIT",  0x8003),
	SYM("_POLCAT",  0x8006),
	SYM("_CBLINK",  0x8009),
	SYM("_CHROUT",  0x800c),
	SYM("_LPOUT",   0x800f),
	SYM("_JOYIN",   0x8012),
	SYM("_CASON",   0x8015),
	SYM("_CASOFF",  0x8018),
	SYM("_WRTLDR",  0x801b),
	SYM("_CBOUT",   0x801e),
	SYM("_CSRDON",  0x8021),
	SYM("_CBIN",    0x8024),
	SYM("_BITIN",   0x8027),
	SYM("_SERIN",   0x802a),
	SYM("_SEROUT",  0x802d),
	SYM("_SERSET",  0x8030),

	SYM("POLCAT",   0xbbe5),
	SYM("CHROUT",   0xbcab),
	SYM("CSRDON",   0xbde7),
	SYM("BLKIN",    0xb93e),
	SYM("BLKOUT",   0xb999),
	SYM("JOYIN",    0xbd52),
	SYM("WRTLDR",   0xbe68),

	SYM("HWINIT",   0xbb40),
	SYM("SWINIT",   0xbb88),
	SYM("CBLINK",   0xbbb5),
	SYM("LPOUT",    0xbd1a),
	SYM("CASON",    0xbdcf),
	SYM("CASOFF",   0xbddc),
	SYM("CBOUT",    0xbe12),
	SYM("CBIN",     0xbdad),
	SYM("BITIN",    0xbda5),

	SYM("CkClBrak", 0x89a4),
	SYM("CkOpBrak", 0x89a7),
	SYM("CkComa",   0x89aa),
	SYM("CkChar",   0x89ac),
	SYM("GETVAR",   0x8a94),

	SYM("delay_1ms",        0xbbc5),

	SYM("kbd.scan_key",     0x851b),

	SYM("tape.pwcount",     0x0082),
	SYM("tape.bcount",      0x0083),
	SYM("tape.bphase",      0x0084),
	SYM("tape.minpw1200",   0x0093),
	SYM("tape.maxpw1200",   0x0094),
	SYM("tape.mincw1200",   0x0092),
	SYM("tape.motor_delay", 0x0095),

	SYM("tape.motor_on_delay",      0xbdd7),
	SYM("tape.tape_on",             0xbdeb),
	SYM("tape.sync_leader",         0xbded),
	SYM("tape.p0_wait_p1",          0xbd99),
	SYM("tape.post_sync",           0xb94d),
	SYM("tape.post_bitin",          0xbdac),
	SYM("tape.post_blkin",          0xb97e),
};

struct symtab_def_internal symtab_bas10[] = {
	INC("basic69"),

	SYM("POLCAT",   0xa1c1),
	SYM("CHROUT",   0xa282),
	SYM("CSRDON",   0xa77c),
	SYM("BLKIN",    0xa70b),
	SYM("BLKOUT",   0xa7f4),
	SYM("JOYIN",    0xa9de),
	SYM("WRTLDR",   0xa7d8),

	SYM("CASON",    0xa7ca),
	SYM("CASOFF",   0xa7eb),
	SYM("CBOUT",    0xa82a),
	SYM("CBIN",     0xa749),
	SYM("BITIN",    0xa755),

	SYM("delay_1ms",        0xa7d3),

	SYM("serial.printchr",  0xa2c1),

	SYM("tape.pwcount",     0x0083),
	SYM("tape.bcount",      0x0082),
	SYM("tape.bphase",      0x0084),
	SYM("tape.minpw1200",   0x0091),
	SYM("tape.maxpw1200",   0x0090),
	SYM("tape.mincw1200",   0x008f),
	SYM("tape.motor_delay", 0x008a),

	SYM("tape.tape_on",             0xa780),
	SYM("tape.sync_leader",         0xa782),
	SYM("tape.motor_on_delay",      0xa7d1),
	SYM("tape.p0_wait_p1",          0xa769),
	SYM("tape.post_sync",           0xa719),
	SYM("tape.post_bitin",          0xa75c),
	SYM("tape.post_blkin",          0xa746),
};

struct symtab_def_internal symtab_mc10[] = {
	SYM("__POLCAT", 0xffdc),
	SYM("__CHROUT", 0xffde),
	SYM("__CSRDON", 0xffe0),
	SYM("__BLKIN",  0xffe2),
	SYM("__BLKOUT", 0xffe4),
	SYM("__SOUND",  0xffe6),
	SYM("__WRTLDR", 0xffe8),

	SYM("POLCAT",   0xf883),
	SYM("CHROUT",   0xf9c6),
	SYM("CSRDON",   0xff4e),
	SYM("BLKIN",    0xfeb9),
	SYM("BLKOUT",   0xfcc0),
	SYM("SOUND",    0xffab),
	SYM("WRTLDR",   0xfcb7),

	SYM("CBIN",     0xff14),
	SYM("BITIN",    0xff22),

	SYM("delay_1ms",        0xf83f),

	SYM("serial.printchr",  0xf9d0),

	SYM("tape.pwcount",     0x427d),
	SYM("tape.bcount",      0x427c),
	SYM("tape.bphase",      0x427e),
	SYM("tape.minpw1200",   0x422e),
	SYM("tape.maxpw1200",   0x422d),
	SYM("tape.mincw1200",   0x422c),

	SYM("tape.tape_on",     0xff50),
	SYM("tape.sync_leader", 0xff53),
	SYM("tape.p0_wait_p1",  0xff38),
	SYM("tape.post_sync",   0xfecc),
	SYM("tape.post_bitin",  0xff2b),
	SYM("tape.post_blkin",  0xff10),
};

struct symtab_def_internal symtab_d32[] = {
	INC("dragon"),
	SYM("SERIN",    0xbe7b),
	SYM("SEROUT",   0xbe7c),
	SYM("SERSET",   0xbe7d),
};

struct symtab_def_internal symtab_d64_1[] = {
	INC("dragon"),
	SYM("SERIN",    0xbe7b),
	SYM("SEROUT",   0xbe98),
	SYM("SERSET",   0xbea6),
};

struct symtab_def_internal symtab_bas11[] = {
	INC("bas10"),
};

struct symtab_def_internal symtab_bas12[] = {
	INC("bas11"),
	SYM("POLCAT",   0xa1cb),
};

struct symtab_def_internal symtab_bas13[] = {
	INC("bas12"),
};

struct symtab_def_internal symtab_coco3[] = {
	INC("bas12"),
};

struct symtab_idx_internal {
	const char *name;
	struct symtab_def_internal *list;
	size_t nelems;
};

struct symtab_idx_internal symtab_idx[] = {
	{ "basic69", symtab_basic69, ARRAY_N_ELEMENTS(symtab_basic69) },
	{ "dragon", symtab_dragon, ARRAY_N_ELEMENTS(symtab_dragon) },
	{ "bas10", symtab_bas10, ARRAY_N_ELEMENTS(symtab_bas10) },
	{ "mc10", symtab_mc10, ARRAY_N_ELEMENTS(symtab_mc10) },
	{ "d32", symtab_d32, ARRAY_N_ELEMENTS(symtab_d32) },
	{ "d64_1", symtab_d64_1, ARRAY_N_ELEMENTS(symtab_d64_1) },
	{ "bas11", symtab_bas11, ARRAY_N_ELEMENTS(symtab_bas11) },
	{ "bas12", symtab_bas12, ARRAY_N_ELEMENTS(symtab_bas12) },
	{ "bas13", symtab_bas13, ARRAY_N_ELEMENTS(symtab_bas13) },
	{ "coco3", symtab_coco3, ARRAY_N_ELEMENTS(symtab_coco3) },
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct symtab *symtab_new(void) {
	struct symtab *ss = xmalloc(sizeof(*ss));
	symtab_init(ss);
	return ss;
}

void symtab_free(struct symtab *ss) {
	symtab_clear(ss);
	free(ss);
}

void symtab_init(struct symtab *ss) {
	*ss = (struct symtab){0};
}

void symtab_clear(struct symtab *ss) {
	for (size_t i = 0; i < ss->nelems; ++i) {
		//free(ss->list[i].label);
	}
	free(ss->list);
	ss->list = NULL;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Compare the label field of two symbol entries.  Used to sort the list, and
// when binary searching the list.

static int compar_sym_label(const void *aa, const void *bb) {
	const struct sym_entry *a = aa;
	const struct sym_entry *b = bb;
	return strcmp(a->label, b->label);
}

static void do_symtab_include(struct symtab *ss, const char *name, size_t ntotal) {
	struct symtab_def_internal *def = NULL;
	size_t nelems;
	for (size_t i = 0; i < ARRAY_N_ELEMENTS(symtab_idx); ++i) {
		if (0 == c_strcasecmp(name, symtab_idx[i].name)) {
			struct symtab_idx_internal *idx = &symtab_idx[i];
			nelems = idx->nelems;
			ntotal += nelems;
			if (ntotal > ss->nalloc) {
				ss->list = xrealloc(ss->list, ntotal * sizeof(*ss->list));
				ss->nalloc = ntotal;
			}
			def = idx->list;
			break;
		}
	}
	if (!def) {
		SYM_MOD_DEBUG(2, "symbol table '%s' not found\n", name);
		return;
	}
	SYM_MOD_DEBUG(2, "including symbol table '%s'\n", name);
	size_t old_nelems = ss->nelems;
	for (size_t i = 0; i < nelems; ++i) {
		if (def[i].type == SYMTAB_INCLUDE) {
			do_symtab_include(ss, def[i].include.table, ntotal);
			old_nelems = ss->nelems;
			continue;
		} else if (def[i].type == SYMTAB_SYM) {
			const char *label = def[i].sym.label;
			uint32_t A = def[i].sym.A;
			struct sym_entry key = { .label = /*(char *)*/label };
			struct sym_entry *ent = NULL;
			if (old_nelems) {
				ent = bsearch(&key, ss->list, old_nelems, sizeof(*ent), compar_sym_label);
			}
			if (ent) {
				SYM_MOD_DEBUG(2, "%s: replacing '%s': 0x%04x -> 0x%04x\n", name, label, ent->A, A);
			} else {
				SYM_MOD_DEBUG(2, "%s: adding    '%s': 0x%04x\n", name, label, A);
				ent = &ss->list[ss->nelems++];
			}
			ent->label = /*xstrdup(*/label/*)*/;
			ent->A = A;
		}
	}
	qsort(ss->list, ss->nelems, sizeof(*ss->list), compar_sym_label);
}

// Recursively include definition lists using do_symtab_include() then sort the
// results by label.

void symtab_include(struct symtab *ss, const char *name) {
	do_symtab_include(ss, name, ss->nelems);
}

// Find a symbol by label.

int32_t sym_find(struct symtab *ss, const char *label) {
	if (!ss || !ss->list)
		return -1;
	struct sym_entry key = { .label = /*(char *)*/label };
	struct sym_entry *ent = bsearch(&key, ss->list, ss->nelems, sizeof(*ent), compar_sym_label);
	if (ent) {
		return ent->A;
	}
	return -1;
}
