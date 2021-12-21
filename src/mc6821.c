/** \file
 *
 *  \brief Motorola MC6821 Peripheral Interface Adaptor.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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
#include <string.h>

#include "array.h"
#include "delegate.h"

#include "events.h"
#include "mc6821.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"
#include "xroar.h"

static const struct ser_struct ser_struct_mc6821_side[] = {
	SER_STRUCT_ELEM(struct MC6821_side, control_register, ser_type_uint8),  // 1
	SER_STRUCT_ELEM(struct MC6821_side, direction_register, ser_type_uint8),  // 2
	SER_STRUCT_ELEM(struct MC6821_side, output_register, ser_type_uint8),  // 3
	SER_STRUCT_ELEM(struct MC6821_side, cx1, ser_type_bool),  // 4
	SER_STRUCT_ELEM(struct MC6821_side, interrupt_received, ser_type_bool),  // 5
	SER_STRUCT_ELEM(struct MC6821_side, irq, ser_type_bool),  // 6
	SER_STRUCT_ELEM(struct MC6821_side, irq_event, ser_type_event),  // 7
	SER_STRUCT_ELEM(struct MC6821_side, out_source, ser_type_uint8),  // 8
	SER_STRUCT_ELEM(struct MC6821_side, out_sink, ser_type_uint8),  // 9
	SER_STRUCT_ELEM(struct MC6821_side, in_source, ser_type_uint8),  // 10
	SER_STRUCT_ELEM(struct MC6821_side, in_sink, ser_type_uint8),  // 11
};

static const struct ser_struct_data mc6821_side_ser_struct_data = {
	.elems = ser_struct_mc6821_side,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6821_side),
};

static const struct ser_struct ser_struct_mc6821[] = {
	SER_STRUCT_ELEM(struct MC6821, a, ser_type_unhandled),  // 1
	SER_STRUCT_ELEM(struct MC6821, b, ser_type_unhandled),  // 2
};

#define MC6821_SER_A (1)
#define MC6821_SER_B (2)

static _Bool mc6821_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool mc6821_write_elem(void *sptr, struct ser_handle *sh, int tag);

const struct ser_struct_data mc6821_ser_struct_data = {
	.elems = ser_struct_mc6821,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6821),
	.read_elem = mc6821_read_elem,
	.write_elem = mc6821_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void do_irq(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MC6821 PIA part creation

static struct part *mc6821_allocate(void);
static _Bool mc6821_finish(struct part *p);
static void mc6821_free(struct part *p);

static const struct partdb_entry_funcs mc6821_funcs = {
	.allocate = mc6821_allocate,
	.finish = mc6821_finish,
	.free = mc6821_free,

	.ser_struct_data = &mc6821_ser_struct_data,
};

const struct partdb_entry mc6821_part = { .name = "MC6821", .funcs = &mc6821_funcs };

static struct part *mc6821_allocate(void) {
	struct MC6821 *pia = part_new(sizeof(*pia));
	struct part *p = &pia->part;

	*pia = (struct MC6821){0};

	pia->a.in_sink = 0xff;
	pia->b.in_sink = 0xff;
	event_init(&pia->a.irq_event, DELEGATE_AS0(void, do_irq, &pia->a));
	event_init(&pia->b.irq_event, DELEGATE_AS0(void, do_irq, &pia->b));

	return p;
}

static _Bool mc6821_finish(struct part *p) {
	struct MC6821 *pia = (struct MC6821 *)p;
	if (pia->a.irq_event.next == &pia->a.irq_event) {
		event_queue(&MACHINE_EVENT_LIST, &pia->a.irq_event);
	}
	if (pia->b.irq_event.next == &pia->b.irq_event) {
		event_queue(&MACHINE_EVENT_LIST, &pia->b.irq_event);
	}
	return 1;
}

static void mc6821_free(struct part *p) {
	struct MC6821 *pia = (struct MC6821 *)p;
	event_dequeue(&pia->a.irq_event);
	event_dequeue(&pia->b.irq_event);
}

static _Bool mc6821_read_elem(void *sptr, struct ser_handle *sh, int tag) {
        struct MC6821 *pia = sptr;
	switch (tag) {
	case MC6821_SER_A:
		ser_read_struct_data(sh, &mc6821_side_ser_struct_data, &pia->a);
		break;
	case MC6821_SER_B:
		ser_read_struct_data(sh, &mc6821_side_ser_struct_data, &pia->b);
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool mc6821_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct MC6821 *pia = sptr;
	switch (tag) {
	case MC6821_SER_A:
		ser_write_open_string(sh, tag, "A");
		ser_write_struct_data(sh, &mc6821_side_ser_struct_data, &pia->a);
		break;
	case MC6821_SER_B:
		ser_write_open_string(sh, tag, "B");
		ser_write_struct_data(sh, &mc6821_side_ser_struct_data, &pia->b);
		break;
	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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
		DELEGATE_SAFE_CALL((p).data_postwrite); \
	} while (0)

#define UPDATE_OUTPUT_B(p) do { \
		(p).out_source = (p).output_register & (p).direction_register; \
		(p).out_sink = (p).output_register | ~(p).direction_register; \
		DELEGATE_SAFE_CALL((p).data_postwrite); \
	} while (0)

void mc6821_update_state(struct MC6821 *pia) {
	UPDATE_OUTPUT_A(pia->a);
	UPDATE_OUTPUT_B(pia->b);
	DELEGATE_SAFE_CALL(pia->a.control_postwrite);
	DELEGATE_SAFE_CALL(pia->b.control_postwrite);
}

#define READ_DR(p) do { \
		DELEGATE_SAFE_CALL((p).data_preread); \
		(p).interrupt_received = 0; \
		(p).irq = 0; \
	} while (0)

#define READ_CR(p) do { \
		DELEGATE_SAFE_CALL((p).control_preread); \
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
		DELEGATE_SAFE_CALL((p).control_postwrite); \
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

static void do_irq(void *sptr) {
	struct MC6821_side *side = sptr;
	side->irq = 1;
}
