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

void bp_hbreak_add(struct bp_session *bps, unsigned addr) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	do_wp_add_range(bpsp, &bpsp->instruction_list, addr, addr, bpsp->bps.trap_handler);
	if (bpsp->instruction_list) {
		bpsp->debug_cpu->instruction_hook = DELEGATE_AS0(void, bp_instruction_hook, bps);
	}
}

void bp_hbreak_remove(struct bp_session *bps, unsigned addr) {
	struct bp_session_private *bpsp = (struct bp_session_private *)bps;
	do_wp_remove_range(bpsp, &bpsp->instruction_list, addr, addr);
	if (!bpsp->instruction_list) {
		bpsp->debug_cpu->instruction_hook.func = NULL;
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

static void bp_instruction_hook(void *sptr) {
	struct bp_session_private *bpsp = sptr;
	uint16_t cur_pc = DELEGATE_CALL(bpsp->debug_cpu->get_pc);
	uint16_t old_pc;
	do {
		bp_hook(bpsp, bpsp->instruction_list, cur_pc);
		old_pc = cur_pc;
		cur_pc = DELEGATE_CALL(bpsp->debug_cpu->get_pc);
	} while (old_pc != cur_pc);
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
