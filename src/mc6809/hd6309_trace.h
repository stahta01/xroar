/** \file
 *
 *  \brief Hitach HD6309 CPU tracing.
 *
 *  \copyright Copyright 2012-2025 Ciaran Anscomb
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

#ifndef XROAR_MC6809_HD6309_TRACE_H_
#define XROAR_MC6809_HD6309_TRACE_H_

#include "hd6309.h"

struct hd6309_trace;

struct hd6309_trace *hd6309_trace_new(struct HD6309 *hcpu);
void hd6309_trace_free(struct hd6309_trace *);

void hd6309_trace_vector(struct hd6309_trace *, uint16_t vec,
			 unsigned nbytes, uint8_t *bytes);
void hd6309_trace_instruction(struct hd6309_trace *, uint16_t pc,
			      unsigned nbytes, uint8_t *bytes);

#endif
