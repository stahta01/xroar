/** \file
 *
 *  \brief AY-3-891x sound chip.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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
 *
 *  AY-3-891X is listed in the part database.  Create with:
 *
 *  struct part *p = part_create("AY891X", NULL);
 *
 *  No options are needed for this part.
 */

#ifndef XROAR_AY891X_H_
#define XROAR_AY891X_H_

#include "delegate.h"

#include "part.h"

struct AY891X {
	struct part part;

	struct {
		uint8_t out_sink;
		uint8_t in_sink;

		// Called before reading from a port in input mode to update
		// input state
		DELEGATE_T0(void) data_preread;

		// Called after writing to a port, or on changing port direction
		DELEGATE_T0(void) data_postwrite;
	} a, b;
};

#define AY891X_VALUE_A(p) ((p)->a.out_sink & (p)->a.in_sink)
#define AY891X_VALUE_B(p) ((p)->b.out_sink & (p)->b.in_sink)

// Configure sound chip.  refrate is the reference clock to the sound chip
// itself (e.g., 4000000).  framerate is the desired output rate to be written
// to supplied buffers.  tickrate is the "system" tick rate (e.g., 14318180).
// tick indicates time of creation.

void ay891x_configure(struct AY891X *csg, int refrate, int framerate, int tickrate,
                       uint32_t tick);

// Access cycle.  BDIR and BC1 determines the direction and function of the data:
//
//      BDIR    BC1
//      0       0       Inactive
//      0       1       Read
//      1       0       Write
//      1       1       Latch address

void ay891x_cycle(struct AY891X *csg, bool BDIR, bool BC1, uint8_t *D);

// Fill a buffer with (float, mono) audio at the desired frame rate.  Returned
// value is the audio output at the elapsed system time (which due to sample
// rate conversion may not be in the returned buffer).

float ay891x_get_audio(void *sptr, uint32_t tick, int nframes, float *buf);

#endif
