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
	_Bool modified;  // an add or delete has occurred during list run
};

// Add a breakpoint.  Returns 1 if successful, else 0 (e.g. identical
// breakpoint already exists)
_Bool bp_breakpoint_add(struct bp_breakpoint_set *bbs, uint32_t A,
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
_Bool bp_watchpoint_add(struct bp_watchpoint_set *wps, _Bool Rnw,
			uint32_t Astart, uint32_t Aend,
			DELEGATE_T2(void, bool, uint32) handler);

// Remove specific watchpoint.
void bp_watchpoint_remove(struct bp_watchpoint_set *wps, int Rnw,
			  int32_t Astart, uint32_t Aend,
			  DELEGATE_T2(void, bool, uint32) handler);

// Inlined watchpoint match & dispatch
inline void bp_check_watchpoints(struct bp_watchpoint_set *set, _Bool RnW, uint32_t A) {
	struct bp_watchpoint *list = set->list[RnW];
	struct bp_watchpoint *next;
	for (struct bp_watchpoint *iter = list; iter; iter = next) {
		next = iter->next;
		if (A >= iter->Astart && A <= iter->Aend) {
			DELEGATE_CALL(iter->handler, RnW, A);
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Breakpoint support both for internal hooks and user-added traps (e.g. via
 * the GDB target).
 *
 * For internal hooks, an array of struct breakpoint is usually supplied using
 * bp_add_list, and the add_cond field determines whether each breakpoint is
 * relevant to the currently configured architecture.
 *
 * Once a breakpoint is added, match_mask determines the match bits that need
 * to match to trigger, and match_cond specifies what those values must be.
 */

// Breakpoint session

struct bp_session {
	DELEGATE_T0(void) trap_handler;

	struct slist *wp_read_list;
	struct slist *wp_write_list;
};

struct bp_session *bp_session_new(struct machine *m);
void bp_session_free(struct bp_session *);

struct breakpoint {
	// Breakpoint conditions
	unsigned address;
	unsigned address_end;
	// Handler
	DELEGATE_T0(void) handler;
};

// Chosen to match up to the GDB protcol watchpoint type minus 1.
#define WP_WRITE (1)
#define WP_READ  (2)
#define WP_BOTH  (3)

#define BP_WP_WRITE  (2)
#define BP_WP_READ   (3)
#define BP_WP_ACCESS (4)

void bp_wp_add_range(struct bp_session *bps, unsigned type,
		     unsigned addr, unsigned addr_end, DELEGATE_T0(void) handler);
void bp_wp_remove_range(struct bp_session *bps, unsigned type,
			unsigned addr, unsigned addr_end);

#ifdef WANT_GDB_TARGET
void bp_wp_add(struct bp_session *bps, unsigned type, unsigned addr, unsigned nbytes);
void bp_wp_remove(struct bp_session *bps, unsigned type, unsigned addr, unsigned nbytes);
#endif

void bp_wp_read_hook(struct bp_session *bps, unsigned address);
void bp_wp_write_hook(struct bp_session *bps, unsigned address);

void bp_instruction_hook_new(void *sptr, uint32_t A);

#endif
