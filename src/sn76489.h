/** \file
 *
 *  \brief TI SN76489 sound chip.
 *
 *  \copyright Copyright 2018-2021 Ciaran Anscomb
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

#ifndef XROAR_SN76489_H_
#define XROAR_SN76489_H_

#include "part.h"

// 76489 has two outputs: a READY line and the audio output itself.  Audio
// output buffers are populated with sn76489_get_audio(), so only READY
// remains.

struct SN76489 {
	struct part part;
	_Bool ready;
};

// Configure sound chip.  refrate is the reference clock to the sound chip
// itself (e.g., 4000000).  framerate is the desired output rate to be written
// to supplied buffers.  tickrate is the "system" tick rate (e.g., 14318180).
// tick indicates time of creation.

void sn76489_configure(struct SN76489 *csg, int refrate, int framerate, int tickrate,
                       uint32_t tick);

// Register write.  Current system time required to update 'ready' state.

void sn76489_write(struct SN76489 *csg, uint32_t tick, uint8_t D);

// Fill a buffer with (float, mono) audio at the desired frame rate.  Returned
// value is the audio output at the elapsed system time (which due to sample
// rate conversion may not be in the returned buffer).  Also updates 'ready'
// state.

float sn76489_get_audio(void *sptr, uint32_t tick, int nframes, float *buf);

#endif
