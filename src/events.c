/** \file
 *
 *  \brief Event scheduling & dispatch.
 *
 *  \copyright Copyright 2005-2025 Ciaran Anscomb
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

#include <stdlib.h>

#include "xalloc.h"

#include "events.h"
#include "logging.h"

extern inline int event_tick_delta(event_ticks t0, event_ticks t1);
extern inline _Bool event_pending(struct event_list *, event_ticks to_time);
extern inline void event_dispatch_next(struct event_list *);
extern inline void event_run_queue(struct event_list *, event_ticks dt);


event_ticks event_current_tick = 0;

struct event_list *event_list_new(void) {
	struct event_list *list = xmalloc(sizeof(*list));
	event_list_init(list);
	return list;
}

void event_list_init(struct event_list *list) {
	*list = (struct event_list){0};
}

struct event *event_new(struct event_list *list, DELEGATE_T0(void) delegate) {
	struct event *new = xmalloc(sizeof(*new));
	event_init(new, list, delegate);
	return new;
}

void event_init(struct event *event, struct event_list *list, DELEGATE_T0(void) delegate) {
	if (event == NULL) return;
	*event = (struct event){0};
	event->list = list;
	event->at_tick = event_current_tick;
	event->delegate = delegate;
}

void event_free(struct event *event) {
	event_dequeue(event);
	free(event);
}

void event_queue(struct event *event) {
	struct event **entry;
	if (event->queued)
		event_dequeue(event);
	event->queued = 1;
	for (entry = &event->list->events; *entry; entry = &((*entry)->next)) {
		if (event_tick_delta(event->at_tick, (*entry)->at_tick) < 0) {
			event->next = *entry;
			*entry = event;
			return;
		}
	}
	*entry = event;
	event->next = NULL;
}

void event_queue_auto(struct event_list *list, DELEGATE_T0(void) delegate, int dt) {
	struct event *e = event_new(list, delegate);
	e->at_tick += dt;
	e->autofree = 1;
	event_queue(e);
}

void event_dequeue(struct event *event) {
	if (!event->queued)
		return;
	event->queued = 0;
	struct event **list = &event->list->events;
	if (*list == event) {
		*list = event->next;
		return;
	}
	for (struct event **entry = list; *entry; entry = &((*entry)->next)) {
		if ((*entry)->next == event) {
			(*entry)->next = event->next;
			return;
		}
	}
}
