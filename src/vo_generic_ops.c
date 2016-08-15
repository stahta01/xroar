/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This file contains generic scanline rendering routines.  It is included
 * into various video module source files and makes use of macros defined in
 * those files (eg, LOCK_SURFACE and XSTEP) */

#include <math.h>

#include "machine.h"
#include "module.h"
#include "ntsc.h"
#include "vdg_palette.h"
#include "mc6847/mc6847.h"

static Pixel *pixel;
static Pixel vdg_colour[12];
static Pixel artifact_5bit[2][32];
static Pixel artifact_simple[2][4];

/* Map VDG palette entry */
static Pixel map_palette_entry(int i) {
	float R, G, B;
	vdg_palette_RGB(xroar_vdg_palette, i, &R, &G, &B);
	R *= 255.0;
	G *= 255.0;
	B *= 255.0;
	return MAPCOLOUR((int)R, (int)G, (int)B);
}

/*

References:
    http://www.le.ac.uk/eg/fss1/FIRFILT.C

Low-pass filter, fs=28MHz, cutoff=4.2MHz, rectangular window, M=3.

Coefficients scaled for integer maths.  Result should be divided by 32768.

*/

#define N0 (8307)
#define N1 (7130)
#define N2 (4191)
#define N3 (907)

/* Scale factor */
#define NSHIFT (15)

/* Filter offset */
#define NOFF (3)

/* Gamma LUTs */
static uint8_t ntsc_ungamma[256];

/* Allocate colours */

static void alloc_colours(struct vo_interface *vo) {
	(void)vo;
#ifdef RESET_PALETTE
	RESET_PALETTE();
#endif
	for (int j = 0; j < 12; j++) {
		vdg_colour[j] = map_palette_entry(j);
	}

	// Populate NTSC inverse gamma LUT
	for (int j = 0; j < 256; j++) {
		float c = j / 255.0;
		if (c <= (0.018 * 4.5)) {
			c /= 4.5;
		} else {
			c = powf((c+0.099)/(1.+0.099), 2.2);
		}
		ntsc_ungamma[j] = (int)(c * 255.0);
	}

	// 2-bit LUT NTSC cross-colour
	artifact_simple[0][0] = MAPCOLOUR(0x00, 0x00, 0x00);
	artifact_simple[0][1] = MAPCOLOUR(0x00, 0x80, 0xff);
	artifact_simple[0][2] = MAPCOLOUR(0xff, 0x80, 0x00);
	artifact_simple[0][3] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_simple[1][0] = MAPCOLOUR(0x00, 0x00, 0x00);
	artifact_simple[1][1] = MAPCOLOUR(0xff, 0x80, 0x00);
	artifact_simple[1][2] = MAPCOLOUR(0x00, 0x80, 0xff);
	artifact_simple[1][3] = MAPCOLOUR(0xff, 0xff, 0xff);

	// 5-bit LUT NTSC cross-colour
	// TODO: generate this using available NTSC decoding
	artifact_5bit[0][0x00] = MAPCOLOUR(0x00, 0x00, 0x00);
	artifact_5bit[0][0x01] = MAPCOLOUR(0x00, 0x00, 0x00);
	artifact_5bit[0][0x02] = MAPCOLOUR(0x00, 0x32, 0x78);
	artifact_5bit[0][0x03] = MAPCOLOUR(0x00, 0x28, 0x00);
	artifact_5bit[0][0x04] = MAPCOLOUR(0xff, 0x8c, 0x64);
	artifact_5bit[0][0x05] = MAPCOLOUR(0xff, 0x8c, 0x64);
	artifact_5bit[0][0x06] = MAPCOLOUR(0xff, 0xd2, 0xff);
	artifact_5bit[0][0x07] = MAPCOLOUR(0xff, 0xf0, 0xc8);
	artifact_5bit[0][0x08] = MAPCOLOUR(0x00, 0x32, 0x78);
	artifact_5bit[0][0x09] = MAPCOLOUR(0x00, 0x00, 0x3c);
	artifact_5bit[0][0x0a] = MAPCOLOUR(0x00, 0x80, 0xff);
	artifact_5bit[0][0x0b] = MAPCOLOUR(0x00, 0x80, 0xff);
	artifact_5bit[0][0x0c] = MAPCOLOUR(0xd2, 0xff, 0xd2);
	artifact_5bit[0][0x0d] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_5bit[0][0x0e] = MAPCOLOUR(0x64, 0xf0, 0xff);
	artifact_5bit[0][0x0f] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_5bit[0][0x10] = MAPCOLOUR(0x3c, 0x00, 0x00);
	artifact_5bit[0][0x11] = MAPCOLOUR(0x3c, 0x00, 0x00);
	artifact_5bit[0][0x12] = MAPCOLOUR(0x00, 0x00, 0x00);
	artifact_5bit[0][0x13] = MAPCOLOUR(0x00, 0x28, 0x00);
	artifact_5bit[0][0x14] = MAPCOLOUR(0xff, 0x80, 0x00);
	artifact_5bit[0][0x15] = MAPCOLOUR(0xff, 0x80, 0x00);
	artifact_5bit[0][0x16] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_5bit[0][0x17] = MAPCOLOUR(0xff, 0xf0, 0xc8);
	artifact_5bit[0][0x18] = MAPCOLOUR(0x28, 0x00, 0x28);
	artifact_5bit[0][0x19] = MAPCOLOUR(0x28, 0x00, 0x28);
	artifact_5bit[0][0x1a] = MAPCOLOUR(0x00, 0x80, 0xff);
	artifact_5bit[0][0x1b] = MAPCOLOUR(0x00, 0x80, 0xff);
	artifact_5bit[0][0x1c] = MAPCOLOUR(0xff, 0xf0, 0xc8);
	artifact_5bit[0][0x1d] = MAPCOLOUR(0xff, 0xf0, 0xc8);
	artifact_5bit[0][0x1e] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_5bit[0][0x1f] = MAPCOLOUR(0xff, 0xff, 0xff);

	artifact_5bit[1][0x00] = MAPCOLOUR(0x00, 0x00, 0x00);
	artifact_5bit[1][0x01] = MAPCOLOUR(0x00, 0x00, 0x00);
	artifact_5bit[1][0x02] = MAPCOLOUR(0xb4, 0x3c, 0x1e);
	artifact_5bit[1][0x03] = MAPCOLOUR(0x28, 0x00, 0x28);
	artifact_5bit[1][0x04] = MAPCOLOUR(0x46, 0xc8, 0xff);
	artifact_5bit[1][0x05] = MAPCOLOUR(0x46, 0xc8, 0xff);
	artifact_5bit[1][0x06] = MAPCOLOUR(0xd2, 0xff, 0xd2);
	artifact_5bit[1][0x07] = MAPCOLOUR(0x64, 0xf0, 0xff);
	artifact_5bit[1][0x08] = MAPCOLOUR(0xb4, 0x3c, 0x1e);
	artifact_5bit[1][0x09] = MAPCOLOUR(0x3c, 0x00, 0x00);
	artifact_5bit[1][0x0a] = MAPCOLOUR(0xff, 0x80, 0x00);
	artifact_5bit[1][0x0b] = MAPCOLOUR(0xff, 0x80, 0x00);
	artifact_5bit[1][0x0c] = MAPCOLOUR(0xff, 0xd2, 0xff);
	artifact_5bit[1][0x0d] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_5bit[1][0x0e] = MAPCOLOUR(0xff, 0xf0, 0xc8);
	artifact_5bit[1][0x0f] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_5bit[1][0x10] = MAPCOLOUR(0x00, 0x00, 0x3c);
	artifact_5bit[1][0x11] = MAPCOLOUR(0x00, 0x00, 0x3c);
	artifact_5bit[1][0x12] = MAPCOLOUR(0x00, 0x00, 0x00);
	artifact_5bit[1][0x13] = MAPCOLOUR(0x28, 0x00, 0x28);
	artifact_5bit[1][0x14] = MAPCOLOUR(0x00, 0x80, 0xff);
	artifact_5bit[1][0x15] = MAPCOLOUR(0x00, 0x80, 0xff);
	artifact_5bit[1][0x16] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_5bit[1][0x17] = MAPCOLOUR(0x64, 0xf0, 0xff);
	artifact_5bit[1][0x18] = MAPCOLOUR(0x00, 0x28, 0x00);
	artifact_5bit[1][0x19] = MAPCOLOUR(0x00, 0x28, 0x00);
	artifact_5bit[1][0x1a] = MAPCOLOUR(0xff, 0x80, 0x00);
	artifact_5bit[1][0x1b] = MAPCOLOUR(0xff, 0x80, 0x00);
	artifact_5bit[1][0x1c] = MAPCOLOUR(0x64, 0xf0, 0xff);
	artifact_5bit[1][0x1d] = MAPCOLOUR(0x64, 0xf0, 0xff);
	artifact_5bit[1][0x1e] = MAPCOLOUR(0xff, 0xff, 0xff);
	artifact_5bit[1][0x1f] = MAPCOLOUR(0xff, 0xff, 0xff);
}

/* Render colour line using palette */

static void render_scanline(struct vo_interface *vo, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	(void)burst;
	(void)phase;
	if (vo->scanline >= vo->window_y &&
	    vo->scanline < (vo->window_y + vo->window_h)) {
		scanline_data += vo->window_x;
		LOCK_SURFACE;
		for (int i = vo->window_w >> 1; i; i--) {
			uint8_t c0 = *scanline_data;
			scanline_data += 2;
			*pixel = *(pixel+1) = vdg_colour[c0];
			pixel += 2*XSTEP;
		}
		UNLOCK_SURFACE;
		pixel += NEXTLINE;
	}
	vo->scanline++;
}

/* Render artifacted colours - simple 4-colour lookup */

static void render_ccr_simple(struct vo_interface *vo, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	(void)burst;
	unsigned p = (phase >> 2) & 1;
	if (vo->scanline >= vo->window_y &&
	    vo->scanline < (vo->window_y + vo->window_h)) {
		scanline_data += vo->window_x;
		LOCK_SURFACE;
		for (int i = vo->window_w / 4; i; i--) {
			uint8_t c0 = *scanline_data;
			uint8_t c1 = *(scanline_data+2);
			scanline_data += 4;
			if (c0 == VDG_BLACK || c0 == VDG_WHITE) {
				int aindex = ((c0 != VDG_BLACK) ? 2 : 0)
					     | ((c1 != VDG_BLACK) ? 1 : 0);
				*pixel = *(pixel+1) = *(pixel+2) = *(pixel+3) = artifact_simple[p][aindex];
			} else {
				*pixel = *(pixel+1) = vdg_colour[c0];
				*(pixel+2) = *(pixel+3) = vdg_colour[c1];
			}
			pixel += 4*XSTEP;
		}
		UNLOCK_SURFACE;
		pixel += NEXTLINE;
	}
	vo->scanline++;
}

/* Render artifacted colours - 5-bit lookup table */

static void render_ccr_5bit(struct vo_interface *vo, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	(void)burst;
	unsigned p = (phase >> 2) & 1;
	if (vo->scanline >= vo->window_y &&
	    vo->scanline < (vo->window_y + vo->window_h)) {
		unsigned aindex = 0;
		scanline_data += vo->window_x;
		aindex = (*(scanline_data - 6) != VDG_BLACK) ? 14 : 0;
		aindex |= (*(scanline_data - 2) != VDG_BLACK) ? 1 : 0;
		LOCK_SURFACE;
		for (int i = vo->window_w/2; i; i--) {
			aindex = (aindex << 1) & 31;
			if (*(scanline_data + 4) != VDG_BLACK)
				aindex |= 1;
			uint8_t c = *scanline_data;
			scanline_data += 2;
			if (c == VDG_BLACK || c == VDG_WHITE) {
				*pixel = *(pixel+1) = artifact_5bit[p][aindex];
			} else {
				*pixel = *(pixel+1) = vdg_colour[c];
			}
			pixel += 2*XSTEP;
			p ^= 1;
		}
		UNLOCK_SURFACE;
		pixel += NEXTLINE;
	}
	vo->scanline++;
}

/* NTSC composite video simulation */

static void render_ntsc(struct vo_interface *vo, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	if (vo->scanline < vo->window_y ||
	    vo->scanline >= (vo->window_y + vo->window_h)) {
		vo->scanline++;
		return;
	}
	vo->scanline++;
	const uint8_t *v = (scanline_data + vo->window_x) - NOFF;
	phase = ((phase + vo->window_x) + NOFF) & 3;
	LOCK_SURFACE;
	for (int j = vo->window_w; j; j--) {
		// Warning: for speed, no bounds checking is performed here.
		// If video data is out of range, this will read from invalid
		// addresses.
		const int *bursti = burst->byphase[(phase+1)&3];
		const int *burstq = burst->byphase[(phase+0)&3];

		int y, i, q;
		y = N3*v[NOFF-3] + N2*v[NOFF-2] + N1*v[NOFF-1] +
		    N0*v[NOFF] +
		    N1*v[NOFF+1] + N2*v[NOFF+2] + N3*v[NOFF+3];
		i = bursti[0]*v[NOFF-3] + bursti[1]*v[NOFF-2] +
		    bursti[2]*v[NOFF-1] +
		    bursti[3]*v[NOFF] +
		    bursti[4]*v[NOFF+1] +
		    bursti[5]*v[NOFF+2] + bursti[6]*v[NOFF+3];
		q = burstq[0]*v[NOFF-3] + burstq[1]*v[NOFF-2] +
		    burstq[2]*v[NOFF-1] +
		    burstq[3]*v[NOFF] +
		    burstq[4]*v[NOFF+1] +
		    burstq[5]*v[NOFF+2] + burstq[6]*v[NOFF+3];

		// Integer maths here adds another 7 bits to the result,
		// so divide by 2^22 rather than 2^15.
		int r = +128*y +122*i  +79*q;  // +1.0*y +0.956*i +0.621*q
		int g = +128*y  -35*i  -83*q;  // +1.0*y -0.272*i -0.647*q
		int b = +128*y -141*i +218*q;  // +1.0*y -1.105*i +1.702*q
		r /= (1 << 22);
		g /= (1 << 22);
		b /= (1 << 22);

		// 40 is a reasonable value for brightness
		// TODO: make this adjustable
		uint8_t R = ntsc_ungamma[clamp_uint8(r+40)];
		uint8_t G = ntsc_ungamma[clamp_uint8(g+40)];
		uint8_t B = ntsc_ungamma[clamp_uint8(b+40)];

		*pixel = MAPCOLOUR(R, G, B);
		pixel += XSTEP;
		phase = (phase+1) & 3;
		v++;
	}
	UNLOCK_SURFACE;
	pixel += NEXTLINE;
}

static void set_vo_cmp(struct vo_interface *vo, int mode) {
	switch (mode) {
	case VO_CMP_PALETTE:
		vo->render_scanline = render_scanline;
		break;
	case VO_CMP_2BIT:
		vo->render_scanline = render_ccr_simple;
		break;
	case VO_CMP_5BIT:
		vo->render_scanline = render_ccr_5bit;
		break;
	case VO_CMP_SIMULATED:
		vo->render_scanline = render_ntsc;
		break;
	}
}
