/** \file
 *
 *  \brief Motorola MC6809 CPU tracing.
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

#ifndef XROAR_MC6809_MC6809_TRACE_H_
#define XROAR_MC6809_MC6809_TRACE_H_

#include "mc6809.h"

struct mc6809_trace;

struct mc6809_trace *mc6809_trace_new(struct MC6809 *cpu);
void mc6809_trace_free(struct mc6809_trace *);

void mc6809_trace_vector(struct mc6809_trace *, uint16_t vec,
			 unsigned nbytes, uint8_t *bytes);
void mc6809_trace_instruction(struct mc6809_trace *, uint16_t pc,
			      unsigned nbytes, uint8_t *bytes);

#endif
