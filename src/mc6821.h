/** \file
 *
 *  \brief Motorola MC6821 Peripheral Interface Adaptor.
 *
 *  \copyright Copyright 2003-2022 Ciaran Anscomb
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

#ifndef XROAR_MC6821_H_
#define XROAR_MC6821_H_

#include <stdint.h>

#include "delegate.h"

#include "events.h"
#include "part.h"

// A PIA consists of two "sides" (A & B), each with slightly different
// characteristics.
//
// The "A" side has internal pull-up resistors, and so is represented only as a
// combination of input and output sinks.  The "B" side actively sources or
// sinks current, so is represented by output and input source and sink values.
// PIA_VALUE_A() and PIA_VALUE_B() calculates the state seen "outside" the PIA
// accordingly.
//
// For now I'm assuming the Cx2 control line is similarly different between the
// ports.  Data sheet just says they have "slightly different loading
// characteristics" when used as output.  Similar macros for determining their
// external state are PIA_VALUE_CA2() and PIA_VALUE_CB2().
//
// Cx2 can also be configured as an input.  Use the mc6821_update_ca2_state()
// and mc6821_update_cb2_state() funcions to update internal state after
// adjusting Cx2 input source & sinks.
//
// Pointers to preread and postwrite hooks can be set for data & control
// registers.
//
// Work in progress: Cx2/IRQx2 behaviour.

struct MC6821_side {
	// Internal state
	uint8_t control_register;
	uint8_t direction_register;
	uint8_t output_register;

	_Bool cx1;  // input-only
	_Bool cx2;
	uint8_t irq1_received;
	uint8_t irq2_received;
	_Bool irq;

	// For calculating pin state
	uint8_t out_source;  // ignored for side A
	uint8_t out_sink;
	uint8_t in_source;  // ignored for side A
	uint8_t in_sink;

	// Same for Cx2
	_Bool cx2_out_source;  // ignored for side A
	_Bool cx2_out_sink;
	_Bool cx2_in_source;  // ignored for side A
	_Bool cx2_in_sink;

	// There is a propagation delay of about 1µs (independent of clock
	// rate) from an active transition causing the IRQ line to fall.
	// Scheduled with these events:
	struct event irq_event;

	// Read and write "strobes" bring Cx2 low until a read (A side) or
	// write (B side) of the output register.  They fall a short time after
	// a) the mode is configured, or b) Cx2 rises.  This event schedules
	// the falling edge:
	struct event strobe_event;

	// The Cx2 "strobe" can be restored by an E transition.  In that case,
	// schedule with this event:
	struct event restore_event;

	// Called after control reg is written to, or if Cx2 changes state
	DELEGATE_T0(void) control_postwrite;

	// Called before reading from a port to update input state
	DELEGATE_T0(void) data_preread;

	// Called after writing to a port
	DELEGATE_T0(void) data_postwrite;
};

struct MC6821 {
	struct part part;
	struct MC6821_side a, b;
};

/* Convenience macros to calculate the effective value of a port output, for
 * example as seen by a high impedance input. */

#define PIA_VALUE_A(p) ((p)->a.out_sink & (p)->a.in_sink)
#define PIA_VALUE_B(p) (((p)->b.out_source | (p)->b.in_source) & (p)->b.out_sink & (p)->b.in_sink)

#define PIA_VALUE_CA2(p) ((p)->a.cx2_out_sink & (p)->a.cx2_in_sink)
#define PIA_VALUE_CB2(p) (((p)->b.cx2_out_source | (p)->b.cx2_in_source) & (p)->b.cx2_out_sink & (p)->b.cx2_in_sink)

void mc6821_reset(struct MC6821 *pia);

// The CPU interface goes through these functions:

uint8_t mc6821_read(struct MC6821 *pia, uint16_t A);
void mc6821_write(struct MC6821 *pia, uint16_t A, uint8_t D);

// Cx1 is input-only, and acts as an interrupt trigger with configurable active
// edge.  This function sets the current level seen at the pin:

void mc6821_set_cx1(struct MC6821_side *side, _Bool level);

// After modifying the external sink/source of a port or the Cx2 control line,
// call these functions to update internal state.  For Cx2, this may trigger an
// interrupt.

void mc6821_update_a_state(struct MC6821 *pia);
void mc6821_update_b_state(struct MC6821 *pia);

void mc6821_update_ca2_state(struct MC6821 *pia);
void mc6821_update_cb2_state(struct MC6821 *pia);

#endif
