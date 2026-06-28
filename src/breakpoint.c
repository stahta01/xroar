/** \file
 *
 *  \brief Breakpoint tracking for debugging.
 *
 *  \copyright Copyright 2011-2026 Ciaran Anscomb
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
#define BP_DEBUG(...)

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "slist.h"
#include "xalloc.h"

#include "breakpoint.h"
#include "logging.h"
#include "machine.h"
#include "part.h"

#ifndef BP_DEBUG
#define BP_DEBUG(...) LOG_DEBUG(__VA_ARGS__)
#define BP_MOD_DEBUG(l,...) LOG_MOD_DEBUG(l, "breakpoint", __VA_ARGS__)
#else
#define BP_MOD_DEBUG(l,...)
#endif

static int compar_breakpoint_a(const void *aa, const void *bb);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool bp_breakpoint_add(struct bp_breakpoint_set *bbs, uint32_t A,
			DELEGATE_T2(void, bool, uint32) handler) {
	struct bp_breakpoint key = { .A = A };
	struct bp_breakpoint *ent = bbs->list ? bsearch(&key, bbs->list, bbs->nbreakpoints, sizeof(key), compar_breakpoint_a) : NULL;
	if (!ent) {
		++bbs->nbreakpoints;
		if (bbs->nbreakpoints > bbs->nallocated) {
			bbs->nallocated = bbs->nbreakpoints;
			bbs->list = xrealloc(bbs->list, bbs->nallocated * sizeof(*bbs->list));
		}
		ent = &bbs->list[bbs->nbreakpoints-1];
		*ent = (struct bp_breakpoint){0};
		ent->A = A;
		ent->handler[0] = handler;
		qsort(bbs->list, bbs->nbreakpoints, sizeof(*bbs->list), compar_breakpoint_a);
		bbs->modified = 1;
		BP_MOD_DEBUG(2, "added new breakpoint @ 0x%04x\n", A);
		return 1;
	}
	for (int i = 0; i < BP_HANDLERS_PER_ADDRESS; ++i) {
		if (DELEGATE_EQ(ent->handler[i], handler)) {
			BP_MOD_DEBUG(2, "breakpoint already exists with handler @ 0x%04x\n", A);
			return 1;
		}
	}
	for (int i = 0; i < BP_HANDLERS_PER_ADDRESS; ++i) {
		if (!DELEGATE_DEFINED(ent->handler[i])) {
			BP_MOD_DEBUG(2, "added handler to existing breakpoint @ 0x%04x\n", A);
			ent->handler[i] = handler;
			return 1;
		}
	}
	return 0;
}

void bp_breakpoint_remove(struct bp_breakpoint_set *bbs, int32_t A,
			  DELEGATE_T2(void, bool, uint32) handler) {
	for (size_t i = 0; i < bbs->nbreakpoints; ) {
		struct bp_breakpoint *ent = &bbs->list[i];
		// A < 0 implies match all addresses
		if (A < 0 || (uint32_t)A == ent->A) {
			int n = 0;
			for (int j = 0; j < BP_HANDLERS_PER_ADDRESS; ++j) {
				// Similarly, NULL handler func/sptr implies match any
				if (DELEGATE_DEFINED(ent->handler[j])) {
					if ((!handler.func || handler.func == ent->handler[j].func) &&
					    (!handler.sptr || handler.sptr == ent->handler[j].sptr)) {
						ent->handler[j].func = NULL;
						BP_MOD_DEBUG(2, "removed handler from breakpoint @ 0x%04x\n", ent->A);
						continue;
					}
					n++;
				}
			}
			if (n == 0) {
				BP_MOD_DEBUG(2, "removed breakpoint @ 0x%04x\n", ent->A);
				--bbs->nbreakpoints;
				size_t ncopy = bbs->nbreakpoints - i;
				if (ncopy > 0) {
					memmove(ent, ent + 1, ncopy * sizeof(*ent));
				}
				bbs->modified = 1;
				// continue at current index after moving
				continue;
			}
		}
		++i;
	}
	// If there's nothing left, free the list
	if (bbs->nbreakpoints == 0 && bbs->list) {
		bbs->nallocated = 0;
		free(bbs->list);
		bbs->list = NULL;
		BP_MOD_DEBUG(2, "breakpoint list empty: freed\n");
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool bp_watchpoint_add(struct bp_watchpoint_set *set, bool RnW, uint32_t Astart,
			uint32_t Aend, DELEGATE_T2(void, bool, uint32) handler) {
	struct bp_watchpoint **list = &set->list[RnW];
	// Does this watchpoint with this handler already exist?
	for (struct bp_watchpoint *iter = *list; iter; iter = iter->next) {
		if (iter->Astart == Astart && iter->Aend == Aend &&
		    DELEGATE_EQ(iter->handler, handler)) {
			BP_MOD_DEBUG(2, "watchpoint already exists with handler @ 0x%04x..0x%04x\n", Astart, Aend);
			return 0;
		}
	}
	struct bp_watchpoint *wp = xmalloc(sizeof(*wp));
	*wp = (struct bp_watchpoint){0};
	wp->next = *list;
	wp->Astart = Astart;
	wp->Aend = Aend;
	wp->handler = handler;
	*list = wp;
	BP_MOD_DEBUG(2, "added %s watchpoint @ 0x%04x..0x%04x\n", RnW ? "read" : "write", Astart, Aend);
	return 1;
}

void bp_watchpoint_remove(struct bp_watchpoint_set *set, int RnW, int32_t Astart,
			  uint32_t Aend, DELEGATE_T2(void, bool, uint32) handler) {
	if (RnW < 0) {
		bp_watchpoint_remove(set, 0, Astart, Aend, handler);
		bp_watchpoint_remove(set, 1, Astart, Aend, handler);
		return;
	}
	RnW = !!RnW;  // sanitise

	struct bp_watchpoint **list = &set->list[RnW];
	struct bp_watchpoint **nextp;
	for (struct bp_watchpoint **iterp = list; *iterp; iterp = nextp) {
		struct bp_watchpoint *wp = *iterp;
		nextp = &wp->next;
		if ((Astart < 0 || (wp->Astart == (uint32_t)Astart && wp->Aend == Aend)) &&
		    (handler.func == NULL || DELEGATE_EQ(wp->handler, handler))) {
			BP_MOD_DEBUG(2, "removed %s watchpoint @ 0x%04x..0x%04x\n", RnW ? "read" : "write", wp->Astart, wp->Aend);
			*iterp = wp->next;
			nextp = iterp;
			free(wp);
		}
	}
	if (*list == NULL) {
		BP_MOD_DEBUG(2, "%s watchpoint list empty\n", RnW ? "read" : "write");
	}
}

extern inline void bp_check_watchpoints(struct bp_watchpoint_set *set, bool RnW, uint32_t A);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int compar_breakpoint_a(const void *aa, const void *bb) {
	const struct bp_breakpoint *a = aa;
	const struct bp_breakpoint *b = bb;
	if (a->A < b->A)
		return -1;
	return (a->A > b->A);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Instruction hook

void bp_instruction_hook(void *sptr, uint32_t A) {
	struct bp_breakpoint_set *bbs = sptr;
	struct bp_breakpoint key = { .A = A };
	struct bp_breakpoint *ent = NULL;
	bbs->modified = 1;  // initial symbol fetch
	for (int i = 0; i < BP_HANDLERS_PER_ADDRESS; ++i) {
		if (bbs->modified) {
			// subsequent iterations won't refetch the symbol unless
			// an add/remove operation sets the modified flag
			bbs->modified = 0;
			if (!bbs->list)
				return;
			ent = bsearch(&key, bbs->list, bbs->nbreakpoints, sizeof(key), compar_breakpoint_a);
			if (!ent)
				return;
		}
		DELEGATE_SAFE_CALL(ent->handler[i], 1, A);
	}
}
