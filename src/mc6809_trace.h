/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2018  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_MC6809_TRACE_H_
#define XROAR_MC6809_TRACE_H_

#include "mc6809.h"

struct mc6809_trace;

struct mc6809_trace *mc6809_trace_new(struct MC6809 *cpu);
void mc6809_trace_free(struct mc6809_trace *tracer);

void mc6809_trace_reset(struct mc6809_trace *tracer);
void mc6809_trace_byte(struct mc6809_trace *tracer, uint8_t byte, uint16_t pc);
void mc6809_trace_irq(struct mc6809_trace *tracer, int vector);
void mc6809_trace_print(struct mc6809_trace *tracer);

#endif  /* XROAR_MC6809_TRACE_H_ */
