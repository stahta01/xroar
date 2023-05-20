/** \file
 *
 *  \brief NTSC encoding & decoding.
 *
 *  \copyright Copyright 2016-2023 Ciaran Anscomb
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

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

#include "intfuncs.h"
#include "xalloc.h"

#include "ntsc.h"
#include "vo_render.h"

// NTSC sync to white is 140 IRE = 1000mV, sync to peak is 160 IRE = 1143mV
//
// Video Demystified recommends 1305mV across 10 bits (0-1023)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ntsc_palette_set_ybr(struct vo_render *vr, unsigned c) {
	struct ntsc_palette *np = &vr->cmp.ntsc_palette;

	unsigned tmax = NTSC_NPHASES;
	unsigned ncycles = 1;
	double wratio = 2. * M_PI * (double)ncycles / (double)tmax;

	int moff_i = vr->cmp.phase + vr->cmp.phase_offset;
	double moff = (2. * M_PI * (double)moff_i) / 360.;

	double y = vr->cmp.colour[c].y;
	double b_y = vr->cmp.colour[c].pb;
	double r_y = vr->cmp.colour[c].pr;

	// Convert to U,V
	y *= 0.6812;
	double u = 0.594 * b_y;
	double v = 0.838 * r_y;

	for (unsigned t = 0; t < NTSC_NPHASES; t++) {
		double a = wratio * (double)t + moff;
		double uu = u * sin(a);
		double vv = v * sin(a + vr->cmp.cha_phase);
		np->byphase[t][c] = int_clamp_u8(255.*(y+uu+vv));
	}
}

void ntsc_burst_set(struct vo_render *vr, unsigned burstn) {
	struct vo_render_burst *burst = &vr->cmp.burst[burstn];
	struct ntsc_burst *nb = &burst->ntsc_burst;

	unsigned tmax = NTSC_NPHASES;
	unsigned ncycles = 1;
	double wratio = 2. * M_PI * (double)ncycles / (double)tmax;

	int moff_i = vr->cmp.phase + vr->cmp.phase_offset;
	double moff = (2. * M_PI * (double)moff_i) / 360.;
	double boff = (2. * M_PI * (double)burst->phase_offset) / 360.;
	double hue = (2. * M_PI * (double)vr->hue) / 360.;

	for (unsigned t = 0; t < tmax; t++) {
		double a0 = sin((wratio * (double)(t+0)) + moff - boff + hue);
		double a1 = sin((wratio * (double)(t+1)) + moff - boff + hue);
		double a2 = sin((wratio * (double)(t+2)) + moff - boff + hue);
		double a3 = sin((wratio * (double)(t+3)) + moff - boff + hue);
		nb->byphase[t][0] = NTSC_C3*a1;
		nb->byphase[t][1] = NTSC_C2*a2;
		nb->byphase[t][2] = NTSC_C1*a3;
		nb->byphase[t][3] = NTSC_C0*a0;
		nb->byphase[t][4] = NTSC_C1*a1;
		nb->byphase[t][5] = NTSC_C2*a2;
		nb->byphase[t][6] = NTSC_C3*a3;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern inline int_xyz ntsc_decode(const struct ntsc_burst *nb, const uint8_t *ntsc, unsigned t);
extern inline int_xyz ntsc_decode_mono(const uint8_t *ntsc);
