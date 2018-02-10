/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2018  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

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

#endif  /* XROAR_HD6309_TRACE_H_ */
