/** \file
 *
 *  \brief Motorola MC6821 Peripheral Interface Adaptor.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
	SER_ID_STRUCT_ELEM(1, struct MC6821_side, control_register),
	SER_ID_STRUCT_ELEM(2, struct MC6821_side, direction_register),
	SER_ID_STRUCT_ELEM(3, struct MC6821_side, output_register),

	SER_ID_STRUCT_ELEM(4, struct MC6821_side, cx1),
	SER_ID_STRUCT_ELEM(12, struct MC6821_side, cx2),
	SER_ID_STRUCT_ELEM(5, struct MC6821_side, irq1_received),
	SER_ID_STRUCT_ELEM(13, struct MC6821_side, irq2_received),
	SER_ID_STRUCT_ELEM(6, struct MC6821_side, irq),

	SER_ID_STRUCT_TYPE(7, ser_type_event, struct MC6821_side, irq_event),
	SER_ID_STRUCT_TYPE(14, ser_type_event, struct MC6821_side, strobe_event),
	SER_ID_STRUCT_TYPE(15, ser_type_event, struct MC6821_side, restore_event),

	SER_ID_STRUCT_ELEM(8, struct MC6821_side, out_source),
	SER_ID_STRUCT_ELEM(9, struct MC6821_side, out_sink),
	SER_ID_STRUCT_ELEM(10, struct MC6821_side, in_source),
	SER_ID_STRUCT_ELEM(11, struct MC6821_side, in_sink),

	SER_ID_STRUCT_ELEM(16, struct MC6821_side, cx2_out_source),
	SER_ID_STRUCT_ELEM(17, struct MC6821_side, cx2_out_sink),
	SER_ID_STRUCT_ELEM(18, struct MC6821_side, cx2_in_source),
	SER_ID_STRUCT_ELEM(19, struct MC6821_side, cx2_in_sink),
};

static const struct ser_struct_data mc6821_side_ser_struct_data = {
	.elems = ser_struct_mc6821_side,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6821_side),
};

static const struct ser_struct ser_struct_mc6821[] = {
	SER_ID_STRUCT_SUBSTRUCT(1, struct MC6821, a, &mc6821_side_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(2, struct MC6821, b, &mc6821_side_ser_struct_data),
};

static const struct ser_struct_data mc6821_ser_struct_data = {
	.elems = ser_struct_mc6821,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6821),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void do_irq(void *sptr);
static void do_strobe_cx2(void *sptr);
static void do_restore_cx2(void *sptr);
static void mc6821_update_cx2_state(struct MC6821_side *side, _Bool level);

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

const struct partdb_entry mc6821_part = { .name = "MC6821", .description = "Motorola | MC6821 PIA", .funcs = &mc6821_funcs };

static struct part *mc6821_allocate(void) {
	struct MC6821 *pia = part_new(sizeof(*pia));
	struct part *p = &pia->part;

	*pia = (struct MC6821){0};

	pia->a.in_sink = 0xff;
	pia->a.cx2_in_sink = 1;
	pia->b.in_sink = 0xff;
	pia->b.cx2_in_sink = 1;
	event_init(&pia->a.irq_event, DELEGATE_AS0(void, do_irq, &pia->a));
	event_init(&pia->a.strobe_event, DELEGATE_AS0(void, do_strobe_cx2, &pia->a));
	event_init(&pia->a.restore_event, DELEGATE_AS0(void, do_restore_cx2, &pia->a));
	event_init(&pia->b.irq_event, DELEGATE_AS0(void, do_irq, &pia->b));
	event_init(&pia->b.strobe_event, DELEGATE_AS0(void, do_strobe_cx2, &pia->b));
	event_init(&pia->b.restore_event, DELEGATE_AS0(void, do_restore_cx2, &pia->b));

	return p;
}

static _Bool mc6821_finish(struct part *p) {
	struct MC6821 *pia = (struct MC6821 *)p;

	if (pia->a.irq_event.next == &pia->a.irq_event) {
		event_queue(&MACHINE_EVENT_LIST, &pia->a.irq_event);
	}
	if (pia->a.strobe_event.next == &pia->a.strobe_event) {
		event_queue(&MACHINE_EVENT_LIST, &pia->a.strobe_event);
	}
	if (pia->a.restore_event.next == &pia->a.restore_event) {
		event_queue(&MACHINE_EVENT_LIST, &pia->a.restore_event);
	}
	if (pia->b.irq_event.next == &pia->b.irq_event) {
		event_queue(&MACHINE_EVENT_LIST, &pia->b.irq_event);
	}
	if (pia->b.strobe_event.next == &pia->b.strobe_event) {
		event_queue(&MACHINE_EVENT_LIST, &pia->b.strobe_event);
	}
	if (pia->b.restore_event.next == &pia->b.restore_event) {
		event_queue(&MACHINE_EVENT_LIST, &pia->b.restore_event);
	}

	// Old snapshots:
	if (pia->a.irq1_received)
		pia->a.irq1_received = 0x80;
	if (pia->b.irq1_received)
		pia->b.irq1_received = 0x80;
	return 1;
}

static void mc6821_free(struct part *p) {
	struct MC6821 *pia = (struct MC6821 *)p;
	event_dequeue(&pia->a.irq_event);
	event_dequeue(&pia->a.strobe_event);
	event_dequeue(&pia->a.restore_event);
	event_dequeue(&pia->b.irq_event);
	event_dequeue(&pia->b.strobe_event);
	event_dequeue(&pia->b.restore_event);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define IRQ1_ENABLED(s) ((s)->control_register & 0x01)
#define IRQ2_ENABLED(s) (((s)->control_register & 0x28) == 0x08)
#define DDR_SELECTED(s) (!((s).control_register & 0x04))
#define PR_SELECTED(s) ((s).control_register & 0x04)

void mc6821_reset(struct MC6821 *pia) {
	pia->a.control_register = 0;
	pia->a.direction_register = 0;
	pia->a.output_register = 0;
	pia->a.cx1 = 0;
	pia->a.cx2_out_sink = 1;
	pia->a.cx2 = 0;
	pia->a.irq = 0;
	mc6821_update_a_state(pia);
	pia->b.control_register = 0;
	pia->b.direction_register = 0;
	pia->b.output_register = 0;
	pia->b.cx1 = 0;
	pia->b.cx2_out_source = 0;
	pia->b.cx2_out_sink = 1;
	pia->b.cx2 = 0;
	pia->b.irq = 0;
	mc6821_update_b_state(pia);
}

void mc6821_set_cx1(struct MC6821_side *side, _Bool level) {
	if (level == side->cx1)
		return;
	side->cx1 = level;
	_Bool active_high = side->control_register & 0x02;
	if (active_high == level) {
		if ((side->control_register & 0x38) == 0x28) {
			// Read/Write Strobe with Cx1 Restore
			side->cx2_out_source = side->cx2_out_sink = 1;
			DELEGATE_SAFE_CALL(side->control_postwrite);
		}
		_Bool irq1_enabled = side->control_register & 0x01;
		side->irq1_received = 0x80;
		if (irq1_enabled) {
			// Figure 13, tRS3 = 1µs
			if (!event_queued(&side->irq_event)) {
				side->irq_event.at_tick = event_current_tick + EVENT_US(1);
				event_queue(&MACHINE_EVENT_LIST, &side->irq_event);
			}
		} else {
			side->irq = side->control_register & 0x40;
		}
	}
}

void mc6821_update_a_state(struct MC6821 *pia) {
	pia->a.out_sink = ~(~pia->a.output_register & pia->a.direction_register);
	DELEGATE_SAFE_CALL(pia->a.data_postwrite);
}

void mc6821_update_b_state(struct MC6821 *pia) {
	pia->b.out_source = pia->b.output_register & pia->b.direction_register;
	pia->b.out_sink = pia->b.output_register | ~pia->b.direction_register;
	DELEGATE_SAFE_CALL(pia->b.data_postwrite);
}

void mc6821_update_ca2_state(struct MC6821 *pia) {
	mc6821_update_cx2_state(&pia->a, PIA_VALUE_CA2(pia));
}

void mc6821_update_cb2_state(struct MC6821 *pia) {
	mc6821_update_cx2_state(&pia->b, PIA_VALUE_CB2(pia));
}

uint8_t mc6821_read(struct MC6821 *pia, uint16_t A) {
	switch (A & 3) {
		default:
		case 0:
			if (DDR_SELECTED(pia->a)) {
				// Read DDRA
				return pia->a.direction_register;
			}

			// Read PRA.  This may trigger a read strobe to CA2.
			DELEGATE_SAFE_CALL(pia->a.data_preread);
			pia->a.irq1_received = pia->a.irq2_received = 0;
			pia->a.irq = 0;

			if ((pia->a.control_register & 0x30) == 0x20) {
				// Read Strobe
				pia->a.strobe_event.at_tick = event_current_tick + 8;
				event_queue(&MACHINE_EVENT_LIST, &pia->a.strobe_event);
				if (!(pia->a.control_register & 0x08)) {
					// Read Strobe with CA1 Restore
					event_dequeue(&pia->a.restore_event);
				} else {
					// Read Strobe with E Restore
					pia->a.restore_event.at_tick = event_current_tick + 24;
					event_queue(&MACHINE_EVENT_LIST, &pia->a.restore_event);
				}
			}

			return pia->a.out_sink & pia->a.in_sink;

		case 1:
			return pia->a.control_register | pia->a.irq1_received | pia->a.irq2_received;

		case 2:
			if (DDR_SELECTED(pia->b)) {
				// Read DDRB
				return pia->b.direction_register;
			}

			// Read PRB
			DELEGATE_SAFE_CALL(pia->b.data_preread);
			pia->b.irq1_received = pia->b.irq2_received = 0;
			pia->b.irq = 0;

			return (pia->b.output_register & pia->b.direction_register) | (PIA_VALUE_B(pia) & ~pia->b.direction_register);

		case 3:
			return pia->b.control_register | pia->b.irq1_received | pia->b.irq2_received;
	}
}

static void write_cr(struct MC6821_side *side, uint8_t D) {
	side->control_register = D & 0x3f;
	if (D & 0x20) {
		side->irq2_received = 0;
	}
	if (IRQ1_ENABLED(side)) {
		side->irq |= side->irq1_received;
	} else if (IRQ2_ENABLED(side)) {
		side->irq |= side->irq2_received;
	} else {
		side->irq = 0;
	}
}

void mc6821_write(struct MC6821 *pia, uint16_t A, uint8_t D) {
	switch (A & 3) {
		default:

		case 0:
			if (DDR_SELECTED(pia->a)) {
				// Write DDRA
				pia->a.direction_register = D;
			} else {
				// Write PRA
				pia->a.output_register = D;
			}

			mc6821_update_a_state(pia);
			break;

		case 1:
			write_cr(&pia->a, D);
			if (D & 0x20) {
				// CA2 as output
				if (D & 0x10) {
					// Set/Reset CA2
					pia->a.cx2_out_sink = D & 8;
				} else {
					pia->a.cx2_out_sink = 1;
				}
			} else {
				// CA2 as input
				pia->a.cx2_out_sink = 1;
				mc6821_update_ca2_state(pia);
			}
			DELEGATE_SAFE_CALL(pia->a.control_postwrite);
			break;

		case 2:
			if (DDR_SELECTED(pia->b)) {
				// Write DDRB
				pia->b.direction_register = D;
			} else {
				// Write PRB.  This may trigger write strobe of CA2.
				pia->b.output_register = D;

				if ((pia->b.control_register & 0x30) == 0x20) {
					// Write Strobe
					pia->b.strobe_event.at_tick = event_current_tick + 16;
					event_queue(&MACHINE_EVENT_LIST, &pia->b.strobe_event);
					if (!(pia->b.control_register & 0x08)) {
						// Write Strobe with CB1 Restore
						event_dequeue(&pia->b.restore_event);
					} else {
						// Write Strobe with E Restore
						pia->b.restore_event.at_tick = event_current_tick + 48;
						event_queue(&MACHINE_EVENT_LIST, &pia->b.restore_event);
					}
				}

			}

			mc6821_update_b_state(pia);
			break;

		case 3:
			write_cr(&pia->b, D);
			if (D & 0x20) {
				// CB2 as output
				if (D & 0x10) {
					// Set/Reset CB2
					pia->b.cx2_out_source = D & 8;
					pia->b.cx2_out_sink = D & 8;
				} else {
					pia->b.cx2_out_source = 1;
					pia->b.cx2_out_sink = 1;
				}
			} else {
				// CA2 as input
				pia->b.cx2_out_source = 0;
				pia->b.cx2_out_sink = 1;
				mc6821_update_cb2_state(pia);
			}
			DELEGATE_SAFE_CALL(pia->b.control_postwrite);
			break;
	}
}

static void do_irq(void *sptr) {
	struct MC6821_side *side = sptr;
	side->irq = 1;
}

static void do_strobe_cx2(void *sptr) {
	struct MC6821_side *side = sptr;
	side->cx2_out_source = side->cx2_out_sink = 0;
	DELEGATE_SAFE_CALL(side->control_postwrite);
}

static void do_restore_cx2(void *sptr) {
	struct MC6821_side *side = sptr;
	side->cx2_out_source = side->cx2_out_sink = 1;
	DELEGATE_SAFE_CALL(side->control_postwrite);
}

static void mc6821_update_cx2_state(struct MC6821_side *side, _Bool level) {
	// Bit 5 set configures Cx2 as output
	if (side->control_register & 0x20) {
		side->irq2_received = 0;
		return;
	}
	if (level == side->cx2)
		return;
	side->cx2 = level;
	_Bool active_high = side->control_register & 0x10;
	if (active_high == level) {
		_Bool irq2_enabled = side->control_register & 0x08;
		side->irq2_received = 0x40;
		if (irq2_enabled) {
			// Figure 13, tRS3 = 1µs
			if (!event_queued(&side->irq_event)) {
				side->irq_event.at_tick = event_current_tick + EVENT_US(1);
				event_queue(&MACHINE_EVENT_LIST, &side->irq_event);
			}
		} else {
			side->irq = side->control_register & 0x80;
		}
	}
}
