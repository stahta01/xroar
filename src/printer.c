/** \file
 *
 *  \brief Printing to file or pipe
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

// for popen, pclose
#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sds.h"
#include "xalloc.h"

#include "delegate.h"
#include "events.h"
#include "logging.h"
#include "part.h"
#include "path.h"
#include "printer.h"
#include "ui.h"
#include "xroar.h"

struct printer_interface_private {
	struct printer_interface public;

	// Messenger client id
	int msgr_client_id;

	int destination;
	sds filename;  // for PRINTER_DESTINATION_FILE
	sds pipe;      // for PRINTER_DESTINATION_PIPE

	FILE *stream;
	struct event ack_clear_event;
	_Bool strobe_state;
	_Bool busy;

	int chars_printed;
	struct event update_chars_printed_event;
};

static void printer_ui_set_print_destination(void *, int tag, void *smsg);
static void printer_ui_set_print_file(void *, int tag, void *smsg);
static void printer_ui_set_print_pipe(void *, int tag, void *smsg);

static void open_stream(struct printer_interface_private *pip);
static void close_stream(struct printer_interface_private *pip);
static void do_ack_clear(void *);
static void do_update_chars_printed(void *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct printer_interface *printer_interface_new(void) {
	struct printer_interface_private *pip = xmalloc(sizeof(*pip));
	*pip = (struct printer_interface_private){0};

	pip->msgr_client_id = messenger_client_register();
	ui_messenger_preempt_group(pip->msgr_client_id, ui_tag_print_destination, MESSENGER_NOTIFY_DELEGATE(printer_ui_set_print_destination, pip));
	ui_messenger_preempt_group(pip->msgr_client_id, ui_tag_print_file, MESSENGER_NOTIFY_DELEGATE(printer_ui_set_print_file, pip));
	ui_messenger_preempt_group(pip->msgr_client_id, ui_tag_print_pipe, MESSENGER_NOTIFY_DELEGATE(printer_ui_set_print_pipe, pip));

	pip->destination = PRINTER_DESTINATION_NONE;
	pip->filename = NULL;
	pip->pipe = NULL;
	pip->stream = NULL;
	event_init(&pip->ack_clear_event, DELEGATE_AS0(void, do_ack_clear, pip));
	event_init(&pip->update_chars_printed_event, DELEGATE_AS0(void, do_update_chars_printed, pip));
	pip->strobe_state = 1;
	pip->busy = 0;
	return &pip->public;
}

void printer_interface_free(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	messenger_client_unregister(pip->msgr_client_id);
	close_stream(pip);
	if (pip->filename)
		sdsfree(pip->filename);
	if (pip->pipe)
		sdsfree(pip->pipe);
	event_dequeue(&pip->ack_clear_event);
	free(pip);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void printer_reset(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	pip->strobe_state = 1;
}

static void printer_ui_set_print_destination(void *sptr, int tag, void *smsg) {
	struct printer_interface_private *pip = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_print_destination);

	int dest = uimsg->value;
	if (dest != PRINTER_DESTINATION_FILE && dest != PRINTER_DESTINATION_PIPE) {
		dest = PRINTER_DESTINATION_NONE;
	}
	if (dest != pip->destination) {
		printer_flush(&pip->public);
		pip->destination = dest;
		pip->busy = 0;
	}
	uimsg->value = dest;
}

static void printer_ui_set_print_file(void *sptr, int tag, void *smsg) {
	struct printer_interface_private *pip = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_print_file);

	const char *filename = uimsg->data;
	if (pip->destination == PRINTER_DESTINATION_FILE) {
		close_stream(pip);
	}
	if (pip->filename) {
		sdsfree(pip->filename);
		pip->filename = NULL;
	}
	if (filename) {
		pip->filename = path_interp(filename);
	}
	if (pip->destination == PRINTER_DESTINATION_FILE) {
		pip->busy = 0;
	}
}

static void printer_ui_set_print_pipe(void *sptr, int tag, void *smsg) {
	struct printer_interface_private *pip = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_print_pipe);

	const char *pipe = uimsg->data;
	if (pip->destination == PRINTER_DESTINATION_PIPE) {
		close_stream(pip);
	}
	if (pip->pipe) {
		sdsfree(pip->pipe);
		pip->pipe = NULL;
	}
	if (pipe) {
		pip->pipe = sdsnew(pipe);
	}
	if (pip->destination == PRINTER_DESTINATION_PIPE) {
		pip->busy = 0;
	}
}

/* close stream but leave stream_dest intact so it will be reopened */
void printer_flush(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	if (!pip->stream)
		return;
	if (pip->destination == PRINTER_DESTINATION_PIPE) {
#ifdef HAVE_POPEN
		pclose(pip->stream);
#endif
	}
	if (pip->destination == PRINTER_DESTINATION_FILE) {
		fclose(pip->stream);
	}
	pip->stream = NULL;
	pip->chars_printed = 0;
	if (!event_queued(&pip->update_chars_printed_event)) {
		pip->update_chars_printed_event.at_tick = event_current_tick + EVENT_MS(500);
		event_queue(&UI_EVENT_LIST, &pip->update_chars_printed_event);
	}
}

// Called when the PIA bus containing STROBE is changed

void printer_strobe(struct printer_interface *pi, _Bool strobe, int data) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	// Ignore if this is not a transition to high
	if (strobe == pip->strobe_state) return;
	pip->strobe_state = strobe;
	if (!pip->strobe_state) return;
	// Open stream for output if it's not already
	if (!pip->stream)
		open_stream(pip);
	// Print byte
	if (pip->stream) {
		fputc(data, pip->stream);
		// Schedule UI notify
		pip->chars_printed++;
		if (!event_queued(&pip->update_chars_printed_event)) {
			pip->update_chars_printed_event.at_tick = event_current_tick + EVENT_MS(500);
			event_queue(&UI_EVENT_LIST, &pip->update_chars_printed_event);
		}
	}
	// ACK, and schedule !ACK
	DELEGATE_SAFE_CALL(pi->signal_ack, 1);
	pip->ack_clear_event.at_tick = event_current_tick + EVENT_US(7);
	event_queue(&MACHINE_EVENT_LIST, &pip->ack_clear_event);
}

_Bool printer_busy(struct printer_interface *pi) {
	struct printer_interface_private *pip = (struct printer_interface_private *)pi;
	return pip->busy;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void open_stream(struct printer_interface_private *pip) {
	if (pip->destination == PRINTER_DESTINATION_PIPE && pip->pipe) {
#ifdef HAVE_POPEN
		pip->stream = popen(pip->pipe, "w");
#endif
	} else if (pip->destination == PRINTER_DESTINATION_FILE && pip->filename) {
		pip->stream = fopen(pip->filename, "ab");
	}
	if (pip->stream) {
		pip->busy = 0;
	}
}

static void close_stream(struct printer_interface_private *pip) {
	printer_flush(&pip->public);
	pip->busy = 1;
}

static void do_ack_clear(void *sptr) {
	struct printer_interface_private *pip = sptr;
	struct printer_interface *pi = &pip->public;
	DELEGATE_SAFE_CALL(pi->signal_ack, 0);
}

static void do_update_chars_printed(void *sptr) {
	struct printer_interface_private *pip = sptr;
	ui_update_state(-1, ui_tag_print_count, pip->chars_printed, NULL);
}
