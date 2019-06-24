/*

NTSC encoding & decoding

Copyright 2016-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

/*

References:
    https://www.arc.id.au/FilterDesign.html

Low-pass, Fs=14.318MHz, Fb=2.15MHz, Kaiser-Bessel windows, 21dB att, M=7 (Np=3).

Coefficients scaled for integer maths.  Result should be divided by 32768.

*/

#ifndef XROAR_NTSC_H_
#define XROAR_NTSC_H_

#include "machine.h"
#include "xroar.h"

#define NTSC_NPHASES (4)

#define NTSC_C0 (8316)
#define NTSC_C1 (7136)
#define NTSC_C2 (4189)
#define NTSC_C3 (899)

struct ntsc_palette {
	unsigned ncolours;
	int *byphase[NTSC_NPHASES];
};

struct ntsc_burst {
	int byphase[NTSC_NPHASES][7];
};

struct ntsc_xyz {
	int x, y, z;
};

extern unsigned ntsc_phase;

inline void ntsc_reset_phase(void) {
	if (xroar_machine_config->cross_colour_phase == VO_PHASE_KRBW) {
		ntsc_phase = 1;
	} else {
		ntsc_phase = 3;
	}
}

struct ntsc_palette *ntsc_palette_new(void);
void ntsc_palette_free(struct ntsc_palette *np);
void ntsc_palette_add_ybr(struct ntsc_palette *np, unsigned c,
			  double y, double b_y, double r_y);
void ntsc_palette_add_direct(struct ntsc_palette *np, unsigned c);

inline int ntsc_encode_from_palette(const struct ntsc_palette *np, unsigned c) {
	int r = np->byphase[ntsc_phase][c];
	ntsc_phase = (ntsc_phase + 1) & 3;
	return r;
}

struct ntsc_burst *ntsc_burst_new(int offset);
void ntsc_burst_free(struct ntsc_burst *nb);

inline struct ntsc_xyz ntsc_decode(const struct ntsc_burst *nb, const uint8_t *ntsc) {
	struct ntsc_xyz buf;
	const int *bursti = nb->byphase[(ntsc_phase+1)&3];
	const int *burstq = nb->byphase[(ntsc_phase+0)&3];
	int y = NTSC_C3*ntsc[0] + NTSC_C2*ntsc[1] + NTSC_C1*ntsc[2] +
		NTSC_C0*ntsc[3] +
		NTSC_C1*ntsc[4] + NTSC_C2*ntsc[5] + NTSC_C3*ntsc[6];
	int i = bursti[0]*ntsc[0] + bursti[1]*ntsc[1] + bursti[2]*ntsc[2] +
		bursti[3]*ntsc[3] +
		bursti[4]*ntsc[4] + bursti[5]*ntsc[5] + bursti[6]*ntsc[6];
	int q = burstq[0]*ntsc[0] + burstq[1]*ntsc[1] + burstq[2]*ntsc[2] +
		burstq[3]*ntsc[3] +
		burstq[4]*ntsc[4] + burstq[5]*ntsc[5] + burstq[6]*ntsc[6];
	// Integer maths here adds another 7 bits to the result,
	// so divide by 2^22 rather than 2^15.
	buf.x = (+128*y +122*i  +79*q) / (1 << 22);  // +1.0*y +0.956*i +0.621*q
	buf.y = (+128*y  -35*i  -83*q) / (1 << 22);  // +1.0*y -0.272*i -0.647*q
	buf.z = (+128*y -141*i +218*q) / (1 << 22);  // +1.0*y -1.105*i +1.702*q
	ntsc_phase = (ntsc_phase + 1) & 3;
	return buf;
}

#endif
