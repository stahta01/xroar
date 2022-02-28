/** \file
 *
 *  \brief Printing to file or pipe
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

// for popen, pclose
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "breakpoint.h"
#include "debug_cpu.h"
#include "delegate.h"
#include "events.h"
#include "logging.h"
#include "mc6801/mc6801.h"
#include "mc6809/mc6809.h"
#include "machine.h"
#include "part.h"
#include "printer.h"
#include "xroar.h"

struct printer_interface_private {
	struct printer_interface public;

	struct machine *machine;
	struct debug_cpu *debug_cpu;
	_Bool is_6809;
	_Bool is_6803;

	FILE *stream;
	char *stream_dest;
	int is_pipe;
	struct event ack_clear_event;
	_Bool strobe_state;
	_Bool busy;
};

static void do_ack_clear(void *);
static void open_stream(struct printer_interface_private *pip);

static void coco_print_byte(void *);

static struct machine_bp coco_print_breakpoint[] = {
	BP_COCO_ROM(.address = 0xa2c1, .handler = DELEGATE_INIT(coco_print_byte, NULL) ),
	BP_COCO3_ROM(.address = 0xa2c1, .handler = DELEGATE_INIT(coco_print_byte, NULL) ),
	BP_MC10_ROM(.address = 0xf9d0, .handler = DELEGATE_INIT(coco_print_byte, NULL) ),
};

struct printer_interface *printer_interface_new(struct machine *m) {
	struct printer_interface_private *pip = xmalloc(sizeof(*pip));
	*pip = (struct printer_interface_private){0};
	pip->machine = m;
	pip->debug_cpu = (struct debug_cpu *)part_component_by_id_is_a(&m->part, "CPU", "DEBUG-CPU");
	pip->is_6809 = part_is_a(&pip->debug_cpu->part, "MC6809");
	pip->is_6803 = part_is_a(&pip->debug_cpu->part, "MC6803");
	pip->stream = NULL;
	pip->stream_dest = NULL;
	pip->is_pipe = 0;
	event_init(&pip->ack_clear_event, DELEGATE_AS0(void, do_ack_clear, pip));
	pip->strobe_state = 1;
	pip->busy = 0;
	return &pip->public;
}

void printer_interface_free(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	printer_close(pi);
	event_dequeue(&pip->ack_clear_event);
	free(pip);
}

void printer_reset(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	pip->strobe_state = 1;
	machine_bp_remove_list(pip->machine, coco_print_breakpoint);
	machine_bp_add_list(pip->machine, coco_print_breakpoint, pip);
}

/* "Open" routines don't directly open the stream.  This way, a file or pipe
 * can be specified in the config file, but we won't send anything unless
 * something is printed. */

void printer_open_file(struct printer_interface *pi, const char *filename) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	printer_close(pi);
	if (pip->stream_dest) free(pip->stream_dest);
	pip->stream_dest = xstrdup(filename);
	pip->is_pipe = 0;
	pip->busy = 0;
	machine_bp_add_list(pip->machine, coco_print_breakpoint, pip);
}

void printer_open_pipe(struct printer_interface *pi, const char *command) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	printer_close(pi);
	if (pip->stream_dest) free(pip->stream_dest);
	pip->stream_dest = xstrdup(command);
	pip->is_pipe = 1;
	pip->busy = 0;
	machine_bp_add_list(pip->machine, coco_print_breakpoint, pip);
}

void printer_close(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	/* flush stream, but destroy stream_dest so it won't be reopened */
	printer_flush(pi);
	if (pip->stream_dest) free(pip->stream_dest);
	pip->stream_dest = NULL;
	pip->is_pipe = 0;
	pip->busy = 1;
	machine_bp_remove_list(pip->machine, coco_print_breakpoint);
}

/* close stream but leave stream_dest intact so it will be reopened */
void printer_flush(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	if (!pip->stream) return;
	if (pip->is_pipe) {
#ifdef HAVE_POPEN
		pclose(pip->stream);
#endif
	} else {
		fclose(pip->stream);
	}
	pip->stream = NULL;
}

/* Called when the PIA bus containing STROBE is changed */
void printer_strobe(struct printer_interface *pi, _Bool strobe, int data) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	/* Ignore if this is not a transition to high */
	if (strobe == pip->strobe_state) return;
	pip->strobe_state = strobe;
	if (!pip->strobe_state) return;
	/* Open stream for output if it's not already */
	if (!pip->stream_dest) return;
	if (!pip->stream) open_stream(pip);
	/* Print byte */
	if (pip->stream) {
		fputc(data, pip->stream);
	}
	/* ACK, and schedule !ACK */
	DELEGATE_SAFE_CALL(pi->signal_ack, 1);
	pip->ack_clear_event.at_tick = event_current_tick + EVENT_US(7);
	event_queue(&MACHINE_EVENT_LIST, &pip->ack_clear_event);
}

static void coco_print_byte(void *sptr) {
	struct printer_interface_private *pip = sptr;
	int byte;
	/* Open stream for output if it's not already */
	if (!pip->stream_dest) return;
	if (!pip->stream) open_stream(pip);
	/* Print byte */
	if (pip->is_6809) {
		byte = MC6809_REG_A(((struct MC6809 *)pip->debug_cpu));
	} else {
		byte = MC6801_REG_A(((struct MC6801 *)pip->debug_cpu));
	}
	if (pip->stream) {
		fputc(byte, pip->stream);
	}
}

static void open_stream(struct printer_interface_private *pip) {
	struct printer_interface *pi = &pip->public;
	if (!pip->stream_dest) return;
	if (pip->is_pipe) {
#ifdef HAVE_POPEN
		pip->stream = popen(pip->stream_dest, "w");
#endif
	} else {
		pip->stream = fopen(pip->stream_dest, "ab");
	}
	if (pip->stream) {
		pip->busy = 0;
	} else {
		printer_close(pi);
	}
}

static void do_ack_clear(void *sptr) {
	struct printer_interface_private *pip = sptr;
	struct printer_interface *pi = &pip->public;
	DELEGATE_SAFE_CALL(pi->signal_ack, 0);
}

_Bool printer_busy(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	return pip->busy;
}
