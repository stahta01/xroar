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

#ifndef XROAR_BREAKPOINT_H_
#define XROAR_BREAKPOINT_H_

#include <stdlib.h>

#include "delegate.h"

struct machine;
struct slist;

#define BP_HANDLERS_PER_ADDRESS (3)

/*
 * Breakpoints.  Trap on execution from address.
 */

struct bp_breakpoint {
	uint32_t A;
	DELEGATE_T2(void, bool, uint32) handler[BP_HANDLERS_PER_ADDRESS];
};

struct bp_breakpoint_set {
	size_t nbreakpoints;
	size_t nallocated;
	struct bp_breakpoint *list;
	bool modified;  // an add or delete has occurred during list run
};

// Add a breakpoint.  Returns 1 if successful, else 0 (e.g. identical
// breakpoint already exists)
bool bp_breakpoint_add(struct bp_breakpoint_set *bbs, uint32_t A,
			DELEGATE_T2(void, bool, uint32) handler);

// Remove specific breakpoint.
void bp_breakpoint_remove(struct bp_breakpoint_set *bbs, int32_t A,
			  DELEGATE_T2(void, bool, uint32) handler);

// Remove all breakpoints in a list matching supplied delegate data pointer.
void bp_breakpoint_remove_all(struct bp_breakpoint_set *bbs, void *sptr);

// Dispatch all breakpoint handlers for a given address.
void bp_instruction_hook(void *sptr, uint32_t A);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Watchpoints.  Trap on memory range reads and writes.  These are simple
 * linked lists, so less efficient than breakpoints, but we need to match on
 * ranges.
 */

struct bp_watchpoint {
	struct bp_watchpoint *next;
	uint32_t Astart;
	uint32_t Aend;
	DELEGATE_T2(void, bool, uint32) handler;
};

struct bp_watchpoint_set {
	struct bp_watchpoint *list[2];
};

// Add a watchpoint.  Returns 1 if successful, else 0 (e.g. identical
// watchpoint already exists)
bool bp_watchpoint_add(struct bp_watchpoint_set *wps, bool Rnw,
			uint32_t Astart, uint32_t Aend,
			DELEGATE_T2(void, bool, uint32) handler);

// Remove specific watchpoint.
void bp_watchpoint_remove(struct bp_watchpoint_set *wps, int Rnw,
			  int32_t Astart, uint32_t Aend,
			  DELEGATE_T2(void, bool, uint32) handler);

// Inlined watchpoint match & dispatch
inline void bp_check_watchpoints(struct bp_watchpoint_set *set, bool RnW, uint32_t A) {
	struct bp_watchpoint *list = set->list[RnW];
	struct bp_watchpoint *next;
	for (struct bp_watchpoint *iter = list; iter; iter = next) {
		next = iter->next;
		if (A >= iter->Astart && A <= iter->Aend) {
			DELEGATE_CALL(iter->handler, RnW, A);
		}
	}
}

#endif
