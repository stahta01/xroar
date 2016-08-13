/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2016 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

NTSC encoding & decoding.

*/

#include "config.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

#include "xalloc.h"

#include "ntsc.h"

unsigned ntsc_phase = 0;

static int clamp_uint8(int v);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern inline void ntsc_reset_phase(void);

struct ntsc_palette *ntsc_palette_new(void) {
	struct ntsc_palette *np = xmalloc(sizeof(*np));
	*np = (struct ntsc_palette){0};
	return np;
}

void ntsc_palette_free(struct ntsc_palette *np) {
	if (!np)
		return;
	for (unsigned p = 0; p < NTSC_NPHASES; p++) {
		free(np->byphase[p]);
	}
	free(np);
}

void ntsc_palette_add_ybr(struct ntsc_palette *np, unsigned c,
			  double y, double b_y, double r_y) {
	assert(np != NULL);
	assert(c < 256);
	if (c >= np->ncolours) {
		np->ncolours = c+1;
		for (unsigned p = 0; p < NTSC_NPHASES; p++) {
			np->byphase[p] = xrealloc(np->byphase[p], np->ncolours*sizeof(int));
		}
	}
	float i = -0.27 * b_y + 0.74 * r_y;
	float q =  0.41 * b_y + 0.48 * r_y;
	np->byphase[0][c] = clamp_uint8(255.*(y+i));
	np->byphase[1][c] = clamp_uint8(255.*(y+q));
	np->byphase[2][c] = clamp_uint8(255.*(y-i));
	np->byphase[3][c] = clamp_uint8(255.*(y-q));
}

void ntsc_palette_add_direct(struct ntsc_palette *np, unsigned c) {
	assert(np != NULL);
	assert(c < 256);
	if (c >= np->ncolours) {
		np->ncolours = c+1;
		for (unsigned p = 0; p < NTSC_NPHASES; p++) {
			np->byphase[p] = xrealloc(np->byphase[p], np->ncolours*sizeof(int));
		}
	}
	np->byphase[0][c] = c;
	np->byphase[1][c] = c;
	np->byphase[2][c] = c;
	np->byphase[3][c] = c;
}

extern inline int ntsc_encode_from_palette(const struct ntsc_palette *np, unsigned c);

struct ntsc_burst *ntsc_burst_new(int offset) {
	struct ntsc_burst *nb = xmalloc(sizeof(*nb));
	*nb = (struct ntsc_burst){0};
	while (offset < 0)
		offset += 360;
	offset %= 360;
	float hue = (2.0 * M_PI * (float)offset) / 360.0;
	for (int p = 0; p < 4; p++) {
		double p0 = sin(hue+((2.*M_PI)*(double)(p+0))/4.);
		double p1 = sin(hue+((2.*M_PI)*(double)(p+1))/4.);
		double p2 = sin(hue+((2.*M_PI)*(double)(p+2))/4.);
		double p3 = sin(hue+((2.*M_PI)*(double)(p+3))/4.);
		nb->byphase[p][0] = NTSC_C3*p1;
		nb->byphase[p][1] = NTSC_C2*p2;
		nb->byphase[p][2] = NTSC_C1*p3;
		nb->byphase[p][3] = NTSC_C0*p0;
		nb->byphase[p][4] = NTSC_C1*p1;
		nb->byphase[p][5] = NTSC_C2*p2;
		nb->byphase[p][6] = NTSC_C3*p3;
	}
	return nb;
}

void ntsc_burst_free(struct ntsc_burst *nb) {
	if (!nb)
		return;
	free(nb);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct ntsc_xyz ntsc_decode(struct ntsc_burst *nb, const uint8_t *ntsc) {
	static struct ntsc_xyz buf;
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int clamp_uint8(int v) {
	if (v < 0) return 0;
	if (v > 255) return 255;
	return v;
}
