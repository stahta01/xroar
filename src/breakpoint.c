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
#include "debug_cpu.h"
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

_Bool bp_breakpoint_add(struct bp_breakpoint_set *bbs, uint32_t A,
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

struct bp_session_private {
	struct bp_session bps;
	struct slist *instruction_list;
	struct slist *iter_next;
	struct machine *machine;
	struct debug_cpu *debug_cpu;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct bp_session *bp_session_new(struct machine *m) {
	if (!m)
		return NULL;
	struct part *cpu = part_component_by_id_is_a(&m->part, "CPU", "DEBUG-CPU");
	if (!cpu)
		return NULL;

	struct bp_session_private *bpsp = xmalloc(sizeof(*bpsp));
	*bpsp = (struct bp_session_private){0};
	struct bp_session *bps = &bpsp->bps;
	bpsp->machine = m;
	bpsp->debug_cpu = (struct debug_cpu *)cpu;

	return bps;
}

void bp_session_free(struct bp_session *bps) {
	free(bps);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct breakpoint *trap_find(struct bp_session_private *bpsp,
				    struct slist *bp_list, unsigned addr, unsigned addr_end) {
	for (struct slist *iter = bp_list; iter; iter = iter->next) {
		struct breakpoint *bp = iter->data;
		if (bp->address == addr && bp->address_end == addr_end
		    && bp->handler.func == bpsp->bps.trap_handler.func)
			return bp;
	}
	return NULL;
}

typedef DELEGATE_S0(void) trap_handler;

static void do_wp_add_range(struct bp_session_private *bpsp,
		     struct slist **bp_list, unsigned addr, unsigned addr_end,
		     DELEGATE_T0(void) handler) {
	if (!handler.func) {
		LOG_MOD_WARN("breakpoint", "no trap handler; not setting breakpoint\n");
		return;
	}
	if (trap_find(bpsp, *bp_list, addr, addr_end))
		return;
	struct breakpoint *new = xmalloc(sizeof(*new));
	new->address = addr;
	new->address_end = addr_end;
	new->handler = handler;
	*bp_list = slist_prepend(*bp_list, new);
}

void bp_wp_add_range(struct bp_session *bps, unsigned type,
		     unsigned addr, unsigned addr_end, DELEGATE_T0(void) handler) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	switch (type) {
	case 2:
		do_wp_add_range(bpsp, &bpsp->bps.wp_write_list, addr, addr_end, handler);
		break;
	case 3:
		do_wp_add_range(bpsp, &bpsp->bps.wp_read_list, addr, addr_end, handler);
		break;
	case 4:
		do_wp_add_range(bpsp, &bpsp->bps.wp_write_list, addr, addr_end, handler);
		do_wp_add_range(bpsp, &bpsp->bps.wp_read_list, addr, addr_end, handler);
		break;
	default:
		break;
	}
}

static void do_wp_remove_range(struct bp_session_private *bpsp,
			struct slist **bp_list, unsigned addr, unsigned addr_end) {
	struct breakpoint *bp = trap_find(bpsp, *bp_list, addr, addr_end);
	if (bp) {
		if (bpsp->iter_next && bpsp->iter_next->data == bp)
			bpsp->iter_next = bpsp->iter_next->next;
		*bp_list = slist_remove(*bp_list, bp);
		free(bp);
	}
}

void bp_wp_remove_range(struct bp_session *bps, unsigned type,
			unsigned addr, unsigned addr_end) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	switch (type) {
	case 2:
		do_wp_remove_range(bpsp, &bpsp->bps.wp_write_list, addr, addr_end);
		break;
	case 3:
		do_wp_remove_range(bpsp, &bpsp->bps.wp_read_list, addr, addr_end);
		break;
	case 4:
		do_wp_remove_range(bpsp, &bpsp->bps.wp_write_list, addr, addr_end);
		do_wp_remove_range(bpsp, &bpsp->bps.wp_read_list, addr, addr_end);
		break;
	default:
		break;
	}
}

#ifdef WANT_GDB_TARGET

void bp_wp_add(struct bp_session *bps, unsigned type, unsigned addr, unsigned nbytes) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	bp_wp_add_range(bps, type, addr, addr + nbytes - 1, bpsp->bps.trap_handler);
}

void bp_wp_remove(struct bp_session *bps, unsigned type, unsigned addr, unsigned nbytes) {
	bp_wp_remove_range(bps, type, addr, addr + nbytes - 1);
}

#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Check the supplied list for any matching hooks.  These are temporarily
 * addded to a new list for dispatch, as the handler may call routines that
 * alter the original list. */

static void bp_hook(struct bp_session_private *bpsp, struct slist *bp_list, unsigned address) {
	for (struct slist *iter = bp_list; iter; iter = bpsp->iter_next) {
		bpsp->iter_next = iter->next;
		struct breakpoint *bp = iter->data;
		if (address < bp->address)
			continue;
		if (address > bp->address_end)
			continue;
		DELEGATE_CALL(bp->handler);
	}
	bpsp->iter_next = NULL;
}

static int compar_breakpoint_a(const void *aa, const void *bb) {
	const struct bp_breakpoint *a = aa;
	const struct bp_breakpoint *b = bb;
	if (a->A < b->A)
		return -1;
	return (a->A > b->A);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Instruction hook

void bp_instruction_hook_new(void *sptr, uint32_t A) {
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

#ifdef WANT_GDB_TARGET

void bp_wp_read_hook(struct bp_session *bps, unsigned address) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	bp_hook(bpsp, bpsp->bps.wp_read_list, address);
}

void bp_wp_write_hook(struct bp_session *bps, unsigned address) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	bp_hook(bpsp, bpsp->bps.wp_write_list, address);
}

#endif
