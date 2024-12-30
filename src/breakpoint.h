/** \file
 *
 *  \brief Breakpoint tracking for debugging.
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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

#include "delegate.h"

struct machine;

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

void bp_add(struct bp_session *bps, struct breakpoint *bp);
void bp_remove(struct bp_session *bps, struct breakpoint *bp);

// Manipulate simple traps.

void bp_hbreak_add(struct bp_session *bps, unsigned addr);
void bp_hbreak_remove(struct bp_session *bps, unsigned addr);

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

#endif
