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

#ifndef XROAR_EVENT_H_
#define XROAR_EVENT_H_

#include <stdint.h>
#include <stdlib.h>

#include "delegate.h"

/* Maintains queues of events.  Each event has a tick number at which its
 * delegate is scheduled to run.  */

typedef uint32_t event_ticks;

/* Event tick frequency */
#define EVENT_TICK_RATE ((uintmax_t)14318180)

#define EVENT_S(s) (EVENT_TICK_RATE * (s))
#define EVENT_MS(ms) ((EVENT_TICK_RATE * (ms)) / 1000)
#define EVENT_US(us) ((EVENT_TICK_RATE * (us)) / 1000000)
#define EVENT_TICKS_14M31818(t) (t)

/* Current "time". */
extern event_ticks event_current_tick;

struct event;

struct event_list {
	struct event *events;
};

struct event {
	event_ticks at_tick;
	DELEGATE_T0(void) delegate;
	_Bool queued;
	_Bool autofree;
	struct event_list *list;
	struct event *next;
};

struct event_list *event_list_new(void);
void event_list_init(struct event_list *);

// An event is created to exist on a particular list

struct event *event_new(struct event_list *, DELEGATE_T0(void));
void event_init(struct event *event, struct event_list *, DELEGATE_T0(void));

/* event_queue() guarantees that events scheduled for the same time will run in
 * order of their being added to queue */

void event_free(struct event *event);
void event_queue(struct event *event);
void event_dequeue(struct event *event);

// Set event scheduling time relative to current time
#define event_set_dt(ev,dt) do { \
		(ev)->at_tick = event_current_tick + (dt); \
	} while (0)

// Set event scheduling time to absolute value
#define event_set_abs(ev,abst) do { \
		(ev)->at_tick = (abst); \
	} while (0)

// Queue event relative to current time
#define event_queue_dt(ev,dt) do { \
		event_set_dt((ev), (dt)); \
		event_queue(ev); \
	} while (0)

// Queue event using absolute time
#define event_queue_abs(ev,abst) do { \
		event_set_abs((ev), (abst)); \
		event_queue(ev); \
	} while (0)

// Allocate an event and queue it, flagged to autofree.  Event will be
// scheduled for current time + dt.
void event_queue_auto(struct event_list *list, DELEGATE_T0(void), int dt);

#define event_queued(e) ((e)->queued)

/* In theory, C99 6.5:7 combined with the fact that fixed width integers are
 * guaranteed 2s complement should make this safe.  Kinda hard to tell, though.
 */

inline int event_tick_delta(event_ticks t0, event_ticks t1) {
	uint32_t dt = t0 - t1;
	return *(int32_t *)&dt;
}

inline _Bool event_pending(struct event_list *list, event_ticks to_time) {
	return list->events && event_tick_delta(to_time, list->events->at_tick) >= 0;
}

inline void event_dispatch_next(struct event_list *list) {
	struct event *e = list->events;
	list->events = e->next;
	e->queued = 0;
	event_current_tick = e->at_tick;
	DELEGATE_CALL(e->delegate);
	if (e->autofree)
		free(e);
}

inline void event_run_queue(struct event_list *list, event_ticks dt) {
	event_ticks to_time = event_current_tick + dt;
	while (event_pending(list, to_time)) {
		event_dispatch_next(list);
	}
	event_current_tick = to_time;
}

#endif
