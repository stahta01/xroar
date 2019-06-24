/*

Hitach HD6309 CPU tracing

Copyright 2012-2017 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_HD6309_TRACE_H_
#define XROAR_HD6309_TRACE_H_

#include "hd6309.h"

struct hd6309_trace;

struct hd6309_trace *hd6309_trace_new(struct HD6309 *hcpu);
void hd6309_trace_free(struct hd6309_trace *tracer);

void hd6309_trace_reset(struct hd6309_trace *tracer);
void hd6309_trace_byte(struct hd6309_trace *tracer, uint8_t byte, uint16_t pc);
void hd6309_trace_irq(struct hd6309_trace *tracer, int vector);
void hd6309_trace_print(struct hd6309_trace *tracer);

#endif
