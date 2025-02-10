/** \file
 *
 *  \brief Motorola MC6801/6803 CPU tracing.
 *
 *  \copyright Copyright 2021-2025 Ciaran Anscomb
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

#ifndef XROAR_MC6801_MC6801_TRACE_H_
#define XROAR_MC6801_MC6801_TRACE_H_

#include "mc6801.h"

struct mc6801_trace;

struct mc6801_trace *mc6801_trace_new(struct MC6801 *cpu);
void mc6801_trace_free(struct mc6801_trace *tracer);

void mc6801_trace_vector(struct mc6801_trace *, uint16_t vec,
			 unsigned nbytes, uint8_t *bytes);
void mc6801_trace_instruction(struct mc6801_trace *, uint16_t pc,
			      unsigned nbytes, uint8_t *bytes);

#endif
