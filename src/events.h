/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_EVENT_H_
#define XROAR_EVENT_H_

#include <limits.h>

#include "delegate.h"

/* Maintains queues of events.  Each event has a tick number at which its
 * delegate is scheduled to run.  */

typedef unsigned event_ticks;
#define EVENT_TICK_MAX (UINT_MAX)

/* Event tick frequency */
#define EVENT_TICK_RATE ((uintmax_t)14318180)

#define EVENT_S(s) (EVENT_TICK_RATE * (s))
#define EVENT_MS(ms) ((EVENT_TICK_RATE * (ms)) / 1000)
#define EVENT_US(us) ((EVENT_TICK_RATE * (us)) / 1000000)

/* Current "time". */
extern event_ticks event_current_tick;

struct event {
	event_ticks at_tick;
	DELEGATE_T0(void) delegate;
	_Bool queued;
	struct event **list;
	struct event *next;
};

struct event *event_new(DELEGATE_T0(void));
void event_init(struct event *event, DELEGATE_T0(void));

/* event_queue() guarantees that events scheduled for the same time will run in
 * order of their being added to queue */

void event_free(struct event *event);
void event_queue(struct event **list, struct event *event);
void event_dequeue(struct event *event);

inline _Bool event_pending(struct event **list) {
	return *list && (event_current_tick - (*list)->at_tick) <= (EVENT_TICK_MAX/2);
}

inline void event_dispatch_next(struct event **list) {
	struct event *e = *list;
	*list = e->next;
	e->queued = 0;
	DELEGATE_CALL0(e->delegate);
}

inline void event_run_queue(struct event **list) {
	while (event_pending(list))
		event_dispatch_next(list);
}

#endif  /* XROAR_EVENT_H_ */
