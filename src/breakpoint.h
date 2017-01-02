/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

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
	unsigned cond;  // matched against breakpoint's cond ANDed with cond_mask
	DELEGATE_T0(void) trap_handler;
};

struct bp_session *bp_session_new(struct machine *m);
void bp_session_free(struct bp_session *);

struct breakpoint {
	// Breakpoint conditions
	unsigned cond_mask;
	unsigned cond;
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

void bp_hbreak_add(struct bp_session *bps,
		   unsigned addr,
		   unsigned match_mask, unsigned match_cond);
void bp_hbreak_remove(struct bp_session *bps,
		      unsigned addr,
		      unsigned match_mask, unsigned match_cond);

void bp_wp_add(struct bp_session *bps,
	       unsigned type,
	       unsigned addr, unsigned nbytes,
	       unsigned match_mask, unsigned match_cond);
void bp_wp_remove(struct bp_session *bps,
		  unsigned type,
		  unsigned addr, unsigned nbytes,
		  unsigned match_mask, unsigned match_cond);

void bp_wp_read_hook(struct bp_session *bps, unsigned address);
void bp_wp_write_hook(struct bp_session *bps, unsigned address);

#endif  /* XROAR_BREAKPOINT_H_ */
