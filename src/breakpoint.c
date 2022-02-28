/** \file
 *
 *  \brief Breakpoint tracking for debugging.
 *
 *  \copyright Copyright 2011-2022 Ciaran Anscomb
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
#include <stdlib.h>
#include <string.h>

#include "slist.h"
#include "xalloc.h"

#include "breakpoint.h"
#include "debug_cpu.h"
#include "logging.h"
#include "machine.h"
#include "part.h"

struct bp_session_private {
	struct bp_session bps;
	struct slist *instruction_list;
	struct slist *iter_next;
	struct machine *machine;
	struct debug_cpu *debug_cpu;
};

static void bp_instruction_hook(void *);

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

static _Bool is_in_list(struct slist *bp_list, struct breakpoint *bp) {
	for (struct slist *iter = bp_list; iter; iter = iter->next) {
		if (bp == iter->data)
			return 1;
	}
	return 0;
}

void bp_add(struct bp_session *bps, struct breakpoint *bp) {
	if (!bps)
		return;
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	if (is_in_list(bpsp->instruction_list, bp))
		return;
	bp->address_end = bp->address;
	bpsp->instruction_list = slist_prepend(bpsp->instruction_list, bp);
	bpsp->debug_cpu->instruction_hook = DELEGATE_AS0(void, bp_instruction_hook, bps);
}

void bp_remove(struct bp_session *bps, struct breakpoint *bp) {
	if (!bps)
		return;
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	if (bpsp->iter_next && bpsp->iter_next->data == bp)
		bpsp->iter_next = bpsp->iter_next->next;
	bpsp->instruction_list = slist_remove(bpsp->instruction_list, bp);
	if (!bpsp->instruction_list) {
		bpsp->debug_cpu->instruction_hook.func = NULL;
	}
}

static struct breakpoint *trap_find(struct bp_session_private *bpsp,
				    struct slist *bp_list, unsigned addr, unsigned addr_end,
				    unsigned cond_mask, unsigned cond) {
	for (struct slist *iter = bp_list; iter; iter = iter->next) {
		struct breakpoint *bp = iter->data;
		if (bp->address == addr && bp->address_end == addr_end
		    && bp->cond_mask == cond_mask
		    && bp->cond == cond
		    && bp->handler.func == bpsp->bps.trap_handler.func)
			return bp;
	}
	return NULL;
}

static void trap_add(struct bp_session_private *bpsp,
		     struct slist **bp_list, unsigned addr, unsigned addr_end,
		     unsigned cond_mask, unsigned cond) {
	if (!bpsp->bps.trap_handler.func) {
		LOG_WARN("Machine has not set trap handler: not setting breakpoint\n");
		return;
	}
	if (trap_find(bpsp, *bp_list, addr, addr_end, cond_mask, cond))
		return;
	struct breakpoint *new = xmalloc(sizeof(*new));
	new->cond_mask = cond_mask;
	new->cond = cond;
	new->address = addr;
	new->address_end = addr_end;
	new->handler = bpsp->bps.trap_handler;
	*bp_list = slist_prepend(*bp_list, new);
}

static void trap_remove(struct bp_session_private *bpsp,
			struct slist **bp_list, unsigned addr, unsigned addr_end,
			unsigned cond_mask, unsigned cond) {
	struct breakpoint *bp = trap_find(bpsp, *bp_list, addr, addr_end, cond_mask, cond);
	if (bp) {
		if (bpsp->iter_next && bpsp->iter_next->data == bp)
			bpsp->iter_next = bpsp->iter_next->next;
		*bp_list = slist_remove(*bp_list, bp);
		free(bp);
	}
}

void bp_hbreak_add(struct bp_session *bps, unsigned addr, unsigned cond_mask, unsigned cond) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	trap_add(bpsp, &bpsp->instruction_list, addr, addr, cond_mask, cond);
	if (bpsp->instruction_list) {
		bpsp->debug_cpu->instruction_hook = DELEGATE_AS0(void, bp_instruction_hook, bps);
	}
}

void bp_hbreak_remove(struct bp_session *bps, unsigned addr, unsigned cond_mask, unsigned cond) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	trap_remove(bpsp, &bpsp->instruction_list, addr, addr, cond_mask, cond);
	if (!bpsp->instruction_list) {
		bpsp->debug_cpu->instruction_hook.func = NULL;
	}
}

#ifdef WANT_GDB_TARGET

void bp_wp_add(struct bp_session *bps, unsigned type,
	       unsigned addr, unsigned nbytes, unsigned cond_mask, unsigned cond) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	switch (type) {
	case 2:
		trap_add(bpsp, &bpsp->bps.wp_write_list, addr, addr + nbytes - 1, cond_mask, cond);
		break;
	case 3:
		trap_add(bpsp, &bpsp->bps.wp_read_list, addr, addr + nbytes - 1, cond_mask, cond);
		break;
	case 4:
		trap_add(bpsp, &bpsp->bps.wp_write_list, addr, addr + nbytes - 1, cond_mask, cond);
		trap_add(bpsp, &bpsp->bps.wp_read_list, addr, addr + nbytes - 1, cond_mask, cond);
		break;
	default:
		break;
	}
}

void bp_wp_remove(struct bp_session *bps, unsigned type,
		  unsigned addr, unsigned nbytes, unsigned cond_mask, unsigned cond) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	switch (type) {
	case 2:
		trap_remove(bpsp, &bpsp->bps.wp_write_list, addr, addr + nbytes - 1, cond_mask, cond);
		break;
	case 3:
		trap_remove(bpsp, &bpsp->bps.wp_read_list, addr, addr + nbytes - 1, cond_mask, cond);
		break;
	case 4:
		trap_remove(bpsp, &bpsp->bps.wp_write_list, addr, addr + nbytes - 1, cond_mask, cond);
		trap_remove(bpsp, &bpsp->bps.wp_read_list, addr, addr + nbytes - 1, cond_mask, cond);
		break;
	default:
		break;
	}
}

#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Check the supplied list for any matching hooks.  These are temporarily
 * addded to a new list for dispatch, as the handler may call routines that
 * alter the original list. */

static void bp_hook(struct bp_session_private *bpsp, struct slist *bp_list, unsigned address) {
	struct bp_session *bps = &bpsp->bps;
	for (struct slist *iter = bp_list; iter; iter = bpsp->iter_next) {
		bpsp->iter_next = iter->next;
		struct breakpoint *bp = iter->data;
		if ((bps->cond & bp->cond_mask) != bp->cond)
			continue;
		if (address < bp->address)
			continue;
		if (address > bp->address_end)
			continue;
		DELEGATE_CALL(bp->handler);
	}
	bpsp->iter_next = NULL;
}

static void bp_instruction_hook(void *sptr) {
	struct bp_session_private *bpsp = sptr;
	uint16_t old_pc;
	do {
		old_pc = DELEGATE_CALL(bpsp->debug_cpu->get_pc);
		bp_hook(bpsp, bpsp->instruction_list, old_pc);
	} while (old_pc != DELEGATE_CALL(bpsp->debug_cpu->get_pc));
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
