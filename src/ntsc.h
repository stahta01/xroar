/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2016 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

NTSC encoding & decoding.

*/

#ifndef XROAR_NTSC_H_
#define XROAR_NTSC_H_

#include "machine.h"
#include "xroar.h"

#define NTSC_NPHASES (4)

/*

References:
    http://www.le.ac.uk/eg/fss1/FIRFILT.C

Low-pass filter, fs=28MHz, cutoff=4.2MHz, rectangular window, M=3.

Coefficients scaled for integer maths.  Result should be divided by 32768.

*/

#define NTSC_C0 (8307)
#define NTSC_C1 (7130)
#define NTSC_C2 (4191)
#define NTSC_C3 (907)

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
	if (xroar_machine_config->cross_colour_phase == CROSS_COLOUR_KRBW) {
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

struct ntsc_xyz ntsc_decode(struct ntsc_burst *nb, const uint8_t *ntsc);

#endif
