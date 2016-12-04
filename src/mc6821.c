/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "delegate.h"
#include "events.h"
#include "mc6821.h"
#include "xroar.h"

struct MC6821 *mc6821_new(void) {
	struct MC6821 *new = xmalloc(sizeof(*new));
	mc6821_init(new);
	return new;
}

static void do_irq(void *data) {
	struct MC6821_side *side = data;
	side->irq = 1;
}

void mc6821_init(struct MC6821 *pia) {
	memset(pia, 0, sizeof(*pia));
	pia->a.in_sink = 0xff;
	pia->b.in_sink = 0xff;
	event_init(&pia->a.irq_event, DELEGATE_AS0(void, do_irq, &pia->a));
	event_init(&pia->b.irq_event, DELEGATE_AS0(void, do_irq, &pia->b));
}

void mc6821_free(struct MC6821 *pia) {
	if (pia == NULL) return;
	event_dequeue(&pia->a.irq_event);
	event_dequeue(&pia->b.irq_event);
	free(pia);
}

#define INTERRUPT_ENABLED(p) ((p).control_register & 0x01)
#define ACTIVE_TRANSITION(p) ((p).control_register & 0x02)
#define DDR_SELECTED(p)      (!((p).control_register & 0x04))
#define PDR_SELECTED(p)      ((p).control_register & 0x04)

void mc6821_reset(struct MC6821 *pia) {
	if (pia == NULL) return;
	pia->a.control_register = 0;
	pia->a.direction_register = 0;
	pia->a.output_register = 0;
	pia->a.interrupt_received = 0;
	pia->a.cx1 = 0;
	pia->a.irq = 0;
	pia->b.control_register = 0;
	pia->b.direction_register = 0;
	pia->b.output_register = 0;
	pia->b.interrupt_received = 0;
	pia->b.cx1 = 0;
	pia->b.irq = 0;
	mc6821_update_state(pia);
}

#define PIA_INTERRUPT_ENABLED(s) ((s)->control_register & 0x01)
#define PIA_DDR_SELECTED(s)      (!((s)->control_register & 0x04))
#define PIA_PDR_SELECTED(s)      ((s)->control_register & 0x04)

void mc6821_set_cx1(struct MC6821_side *side, _Bool level) {
	if (level == side->cx1)
		return;
	side->cx1 = level;
	_Bool active_high = side->control_register & 2;
	if (active_high == level) {
		_Bool irq_enabled = side->control_register & 1;
		side->interrupt_received = 1;
		if (irq_enabled) {
			side->irq_event.at_tick = event_current_tick + EVENT_US(1);
			event_queue(&MACHINE_EVENT_LIST, &side->irq_event);
		} else {
			side->irq = 0;
		}
	}
}

#define UPDATE_OUTPUT_A(p) do { \
		(p).out_sink = ~(~(p).output_register & (p).direction_register); \
		DELEGATE_SAFE_CALL0((p).data_postwrite); \
	} while (0)

#define UPDATE_OUTPUT_B(p) do { \
		(p).out_source = (p).output_register & (p).direction_register; \
		(p).out_sink = (p).output_register | ~(p).direction_register; \
		DELEGATE_SAFE_CALL0((p).data_postwrite); \
	} while (0)

void mc6821_update_state(struct MC6821 *pia) {
	UPDATE_OUTPUT_A(pia->a);
	UPDATE_OUTPUT_B(pia->b);
	DELEGATE_SAFE_CALL0(pia->a.control_postwrite);
	DELEGATE_SAFE_CALL0(pia->b.control_postwrite);
}

#define READ_DR(p) do { \
		DELEGATE_SAFE_CALL0((p).data_preread); \
		(p).interrupt_received = 0; \
		(p).irq = 0; \
	} while (0)

#define READ_CR(p) do { \
		DELEGATE_SAFE_CALL0((p).control_preread); \
	} while (0)

uint8_t mc6821_read(struct MC6821 *pia, uint16_t A) {
	switch (A & 3) {
		default:
		case 0:
			if (DDR_SELECTED(pia->a))
				return pia->a.direction_register;
			READ_DR(pia->a);
			return pia->a.out_sink & pia->a.in_sink;
		case 1:
			READ_CR(pia->a);
			return (pia->a.control_register | (pia->a.interrupt_received ? 0x80 : 0));
		case 2:
			if (DDR_SELECTED(pia->b))
				return pia->b.direction_register;
			READ_DR(pia->b);
			return (pia->b.output_register & pia->b.direction_register) | ((pia->b.out_source | pia->b.in_source) & pia->b.out_sink & pia->b.in_sink & ~pia->b.direction_register);
		case 3:
			READ_CR(pia->b);
			return (pia->b.control_register | (pia->b.interrupt_received ? 0x80 : 0));
	}
}

#define WRITE_DR(p,v) do { \
		if (PDR_SELECTED(p)) { \
			(p).output_register = v; \
			v &= (p).direction_register; \
		} else { \
			(p).direction_register = v; \
			v &= (p).output_register; \
		} \
	} while (0)

#define WRITE_CR(p,v) do { \
		(p).control_register = v & 0x3f; \
		if (INTERRUPT_ENABLED(p)) { \
			if ((p).interrupt_received) \
				(p).irq = 1; \
		} else { \
			(p).irq = 0; \
		} \
		DELEGATE_SAFE_CALL0((p).control_postwrite); \
	} while (0)

void mc6821_write(struct MC6821 *pia, uint16_t A, uint8_t D) {
	switch (A & 3) {
		default:
		case 0:
			WRITE_DR(pia->a, D);
			UPDATE_OUTPUT_A(pia->a);
			break;
		case 1:
			WRITE_CR(pia->a, D);
			break;
		case 2:
			WRITE_DR(pia->b, D);
			UPDATE_OUTPUT_B(pia->b);
			break;
		case 3:
			WRITE_CR(pia->b, D);
			break;
	}
}
