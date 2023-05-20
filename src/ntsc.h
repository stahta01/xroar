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
 *
 *  \par Sources
 *
 *  - https://www.arc.id.au/FilterDesign.html
 *
 *  Low-pass, Fs=14.318MHz, Fb=2.15MHz, Kaiser-Bessel windows, 21dB att, M=7 (Np=3).
 *
 *  Coefficients scaled for integer maths.  Result should be divided by 32768.
 */

#ifndef XROAR_NTSC_H_
#define XROAR_NTSC_H_

#include "intfuncs.h"

#define NTSC_NPHASES (4)

#define NTSC_C0 (8316)
#define NTSC_C1 (7136)
#define NTSC_C2 (4189)
#define NTSC_C3 (899)

struct vo_render;

struct ntsc_palette {
	int byphase[NTSC_NPHASES][256];
};

struct ntsc_burst {
	int byphase[NTSC_NPHASES][7];
};

void ntsc_palette_set_ybr(struct vo_render *vr, unsigned c);

void ntsc_burst_set(struct vo_render *vr, unsigned burstn);

inline int_xyz ntsc_decode(const struct ntsc_burst *nb, const uint8_t *ntsc, unsigned t) {
	int_xyz buf;
	const int *burstu = nb->byphase[(t+0) % NTSC_NPHASES];
	const int *burstv = nb->byphase[(t+1) % NTSC_NPHASES];
	int y = NTSC_C3*ntsc[0] + NTSC_C2*ntsc[1] + NTSC_C1*ntsc[2] +
		NTSC_C0*ntsc[3] +
		NTSC_C1*ntsc[4] + NTSC_C2*ntsc[5] + NTSC_C3*ntsc[6];
	int u = burstu[0]*ntsc[0] + burstu[1]*ntsc[1] + burstu[2]*ntsc[2] +
		burstu[3]*ntsc[3] +
		burstu[4]*ntsc[4] + burstu[5]*ntsc[5] + burstu[6]*ntsc[6];
	int v = burstv[0]*ntsc[0] + burstv[1]*ntsc[1] + burstv[2]*ntsc[2] +
		burstv[3]*ntsc[3] +
		burstv[4]*ntsc[4] + burstv[5]*ntsc[5] + burstv[6]*ntsc[6];
	// Integer maths here adds another 7 bits to the result,
	// so divide by 2^22 rather than 2^15.
	buf.x = (+155*y   +0*u +177*v) >> 22;  // +1.691*y          +1.928*v
	buf.y = (+155*y  -61*u  -90*v) >> 22;  // +1.691*y -0.667*u -0.982*v
	buf.z = (+155*y +315*u   +0*v) >> 22;  // +1.691*y +3.436*u
	return buf;
}

inline int_xyz ntsc_decode_mono(const uint8_t *ntsc) {
	int_xyz buf;
	int y = NTSC_C3*ntsc[0] + NTSC_C2*ntsc[1] + NTSC_C1*ntsc[2] +
		NTSC_C0*ntsc[3] +
		NTSC_C1*ntsc[4] + NTSC_C2*ntsc[5] + NTSC_C3*ntsc[6];
	buf.x = buf.y = buf.z = (155 * y) >> 22;
	return buf;
}

#endif
