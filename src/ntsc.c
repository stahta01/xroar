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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

#include "xalloc.h"

#include "ntsc.h"
#include "vo.h"

unsigned ntsc_phase = 0;

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
		if (np->byphase[p]) {
			free(np->byphase[p]);
			np->byphase[p] = NULL;
		}
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

extern inline struct ntsc_xyz ntsc_decode(const struct ntsc_burst *nb, const uint8_t *ntsc);
