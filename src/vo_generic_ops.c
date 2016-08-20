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

struct vo_generic_interface {
	struct vo_interface public;

	// Palettes
	Pixel vdg_colour[12];
	Pixel artifact_5bit[2][32];
	Pixel artifact_simple[2][4];

	// Current render pointer
	Pixel *pixel;
	int scanline;

	// Gamma LUT
	uint8_t ntsc_ungamma[256];
};

/* Map VDG palette entry */
static Pixel map_palette_entry(struct vo_generic_interface *generic, int i) {
	(void)generic;
	float R, G, B;
	vdg_palette_RGB(xroar_vdg_palette, i, &R, &G, &B);
	R *= 255.0;
	G *= 255.0;
	B *= 255.0;
	return MAPCOLOUR(generic, (int)R, (int)G, (int)B);
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

/* Allocate colours */

static void alloc_colours(void *sptr) {
	struct vo_generic_interface *generic = sptr;
#ifdef RESET_PALETTE
	RESET_PALETTE();
#endif
	for (int j = 0; j < 12; j++) {
		generic->vdg_colour[j] = map_palette_entry(generic, j);
	}

	// Populate NTSC inverse gamma LUT
	for (int j = 0; j < 256; j++) {
		float c = j / 255.0;
		if (c <= (0.018 * 4.5)) {
			c /= 4.5;
		} else {
			c = powf((c+0.099)/(1.+0.099), 2.2);
		}
		generic->ntsc_ungamma[j] = (int)(c * 255.0);
	}

	// 2-bit LUT NTSC cross-colour
	generic->artifact_simple[0][0] = MAPCOLOUR(generic, 0x00, 0x00, 0x00);
	generic->artifact_simple[0][1] = MAPCOLOUR(generic, 0x00, 0x80, 0xff);
	generic->artifact_simple[0][2] = MAPCOLOUR(generic, 0xff, 0x80, 0x00);
	generic->artifact_simple[0][3] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_simple[1][0] = MAPCOLOUR(generic, 0x00, 0x00, 0x00);
	generic->artifact_simple[1][1] = MAPCOLOUR(generic, 0xff, 0x80, 0x00);
	generic->artifact_simple[1][2] = MAPCOLOUR(generic, 0x00, 0x80, 0xff);
	generic->artifact_simple[1][3] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);

	// 5-bit LUT NTSC cross-colour
	// TODO: generate this using available NTSC decoding
	generic->artifact_5bit[0][0x00] = MAPCOLOUR(generic, 0x00, 0x00, 0x00);
	generic->artifact_5bit[0][0x01] = MAPCOLOUR(generic, 0x00, 0x00, 0x00);
	generic->artifact_5bit[0][0x02] = MAPCOLOUR(generic, 0x00, 0x32, 0x78);
	generic->artifact_5bit[0][0x03] = MAPCOLOUR(generic, 0x00, 0x28, 0x00);
	generic->artifact_5bit[0][0x04] = MAPCOLOUR(generic, 0xff, 0x8c, 0x64);
	generic->artifact_5bit[0][0x05] = MAPCOLOUR(generic, 0xff, 0x8c, 0x64);
	generic->artifact_5bit[0][0x06] = MAPCOLOUR(generic, 0xff, 0xd2, 0xff);
	generic->artifact_5bit[0][0x07] = MAPCOLOUR(generic, 0xff, 0xf0, 0xc8);
	generic->artifact_5bit[0][0x08] = MAPCOLOUR(generic, 0x00, 0x32, 0x78);
	generic->artifact_5bit[0][0x09] = MAPCOLOUR(generic, 0x00, 0x00, 0x3c);
	generic->artifact_5bit[0][0x0a] = MAPCOLOUR(generic, 0x00, 0x80, 0xff);
	generic->artifact_5bit[0][0x0b] = MAPCOLOUR(generic, 0x00, 0x80, 0xff);
	generic->artifact_5bit[0][0x0c] = MAPCOLOUR(generic, 0xd2, 0xff, 0xd2);
	generic->artifact_5bit[0][0x0d] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_5bit[0][0x0e] = MAPCOLOUR(generic, 0x64, 0xf0, 0xff);
	generic->artifact_5bit[0][0x0f] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_5bit[0][0x10] = MAPCOLOUR(generic, 0x3c, 0x00, 0x00);
	generic->artifact_5bit[0][0x11] = MAPCOLOUR(generic, 0x3c, 0x00, 0x00);
	generic->artifact_5bit[0][0x12] = MAPCOLOUR(generic, 0x00, 0x00, 0x00);
	generic->artifact_5bit[0][0x13] = MAPCOLOUR(generic, 0x00, 0x28, 0x00);
	generic->artifact_5bit[0][0x14] = MAPCOLOUR(generic, 0xff, 0x80, 0x00);
	generic->artifact_5bit[0][0x15] = MAPCOLOUR(generic, 0xff, 0x80, 0x00);
	generic->artifact_5bit[0][0x16] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_5bit[0][0x17] = MAPCOLOUR(generic, 0xff, 0xf0, 0xc8);
	generic->artifact_5bit[0][0x18] = MAPCOLOUR(generic, 0x28, 0x00, 0x28);
	generic->artifact_5bit[0][0x19] = MAPCOLOUR(generic, 0x28, 0x00, 0x28);
	generic->artifact_5bit[0][0x1a] = MAPCOLOUR(generic, 0x00, 0x80, 0xff);
	generic->artifact_5bit[0][0x1b] = MAPCOLOUR(generic, 0x00, 0x80, 0xff);
	generic->artifact_5bit[0][0x1c] = MAPCOLOUR(generic, 0xff, 0xf0, 0xc8);
	generic->artifact_5bit[0][0x1d] = MAPCOLOUR(generic, 0xff, 0xf0, 0xc8);
	generic->artifact_5bit[0][0x1e] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_5bit[0][0x1f] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);

	generic->artifact_5bit[1][0x00] = MAPCOLOUR(generic, 0x00, 0x00, 0x00);
	generic->artifact_5bit[1][0x01] = MAPCOLOUR(generic, 0x00, 0x00, 0x00);
	generic->artifact_5bit[1][0x02] = MAPCOLOUR(generic, 0xb4, 0x3c, 0x1e);
	generic->artifact_5bit[1][0x03] = MAPCOLOUR(generic, 0x28, 0x00, 0x28);
	generic->artifact_5bit[1][0x04] = MAPCOLOUR(generic, 0x46, 0xc8, 0xff);
	generic->artifact_5bit[1][0x05] = MAPCOLOUR(generic, 0x46, 0xc8, 0xff);
	generic->artifact_5bit[1][0x06] = MAPCOLOUR(generic, 0xd2, 0xff, 0xd2);
	generic->artifact_5bit[1][0x07] = MAPCOLOUR(generic, 0x64, 0xf0, 0xff);
	generic->artifact_5bit[1][0x08] = MAPCOLOUR(generic, 0xb4, 0x3c, 0x1e);
	generic->artifact_5bit[1][0x09] = MAPCOLOUR(generic, 0x3c, 0x00, 0x00);
	generic->artifact_5bit[1][0x0a] = MAPCOLOUR(generic, 0xff, 0x80, 0x00);
	generic->artifact_5bit[1][0x0b] = MAPCOLOUR(generic, 0xff, 0x80, 0x00);
	generic->artifact_5bit[1][0x0c] = MAPCOLOUR(generic, 0xff, 0xd2, 0xff);
	generic->artifact_5bit[1][0x0d] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_5bit[1][0x0e] = MAPCOLOUR(generic, 0xff, 0xf0, 0xc8);
	generic->artifact_5bit[1][0x0f] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_5bit[1][0x10] = MAPCOLOUR(generic, 0x00, 0x00, 0x3c);
	generic->artifact_5bit[1][0x11] = MAPCOLOUR(generic, 0x00, 0x00, 0x3c);
	generic->artifact_5bit[1][0x12] = MAPCOLOUR(generic, 0x00, 0x00, 0x00);
	generic->artifact_5bit[1][0x13] = MAPCOLOUR(generic, 0x28, 0x00, 0x28);
	generic->artifact_5bit[1][0x14] = MAPCOLOUR(generic, 0x00, 0x80, 0xff);
	generic->artifact_5bit[1][0x15] = MAPCOLOUR(generic, 0x00, 0x80, 0xff);
	generic->artifact_5bit[1][0x16] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_5bit[1][0x17] = MAPCOLOUR(generic, 0x64, 0xf0, 0xff);
	generic->artifact_5bit[1][0x18] = MAPCOLOUR(generic, 0x00, 0x28, 0x00);
	generic->artifact_5bit[1][0x19] = MAPCOLOUR(generic, 0x00, 0x28, 0x00);
	generic->artifact_5bit[1][0x1a] = MAPCOLOUR(generic, 0xff, 0x80, 0x00);
	generic->artifact_5bit[1][0x1b] = MAPCOLOUR(generic, 0xff, 0x80, 0x00);
	generic->artifact_5bit[1][0x1c] = MAPCOLOUR(generic, 0x64, 0xf0, 0xff);
	generic->artifact_5bit[1][0x1d] = MAPCOLOUR(generic, 0x64, 0xf0, 0xff);
	generic->artifact_5bit[1][0x1e] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
	generic->artifact_5bit[1][0x1f] = MAPCOLOUR(generic, 0xff, 0xff, 0xff);
}

/* Render colour line using palette */

static void render_scanline(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	struct vo_interface *vo = &generic->public;
	(void)burst;
	(void)phase;
	if (generic->scanline >= vo->window_y &&
	    generic->scanline < (vo->window_y + vo->window_h)) {
		scanline_data += vo->window_x;
		LOCK_SURFACE(generic);
		for (int i = vo->window_w >> 1; i; i--) {
			uint8_t c0 = *scanline_data;
			scanline_data += 2;
			*(generic->pixel) = *(generic->pixel+1) = generic->vdg_colour[c0];
			generic->pixel += 2*XSTEP;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

/* Render artifacted colours - simple 4-colour lookup */

static void render_ccr_simple(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	struct vo_interface *vo = &generic->public;
	(void)burst;
	unsigned p = (phase >> 2) & 1;
	if (generic->scanline >= vo->window_y &&
	    generic->scanline < (vo->window_y + vo->window_h)) {
		scanline_data += vo->window_x;
		LOCK_SURFACE(generic);
		for (int i = vo->window_w / 4; i; i--) {
			uint8_t c0 = *scanline_data;
			uint8_t c1 = *(scanline_data+2);
			scanline_data += 4;
			if (c0 == VDG_BLACK || c0 == VDG_WHITE) {
				int aindex = ((c0 != VDG_BLACK) ? 2 : 0)
					     | ((c1 != VDG_BLACK) ? 1 : 0);
				*(generic->pixel) = *(generic->pixel+1) = *(generic->pixel+2) = *(generic->pixel+3) = generic->artifact_simple[p][aindex];
			} else {
				*(generic->pixel) = *(generic->pixel+1) = generic->vdg_colour[c0];
				*(generic->pixel+2) = *(generic->pixel+3) = generic->vdg_colour[c1];
			}
			generic->pixel += 4*XSTEP;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

/* Render artifacted colours - 5-bit lookup table */

static void render_ccr_5bit(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	struct vo_interface *vo = &generic->public;
	(void)burst;
	unsigned p = (phase >> 2) & 1;
	if (generic->scanline >= vo->window_y &&
	    generic->scanline < (vo->window_y + vo->window_h)) {
		unsigned aindex = 0;
		scanline_data += vo->window_x;
		aindex = (*(scanline_data - 6) != VDG_BLACK) ? 14 : 0;
		aindex |= (*(scanline_data - 2) != VDG_BLACK) ? 1 : 0;
		LOCK_SURFACE(generic);
		for (int i = vo->window_w/2; i; i--) {
			aindex = (aindex << 1) & 31;
			if (*(scanline_data + 4) != VDG_BLACK)
				aindex |= 1;
			uint8_t c = *scanline_data;
			scanline_data += 2;
			if (c == VDG_BLACK || c == VDG_WHITE) {
				*(generic->pixel) = *(generic->pixel+1) = generic->artifact_5bit[p][aindex];
			} else {
				*(generic->pixel) = *(generic->pixel+1) = generic->vdg_colour[c];
			}
			generic->pixel += 2*XSTEP;
			p ^= 1;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

/* NTSC composite video simulation */

static void render_ntsc(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	struct vo_interface *vo = &generic->public;
	if (generic->scanline < vo->window_y ||
	    generic->scanline >= (vo->window_y + vo->window_h)) {
		generic->scanline++;
		return;
	}
	generic->scanline++;
	const uint8_t *v = (scanline_data + vo->window_x) - NOFF;
	phase = ((phase + vo->window_x) + NOFF) & 3;
	LOCK_SURFACE(generic);
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
		uint8_t R = generic->ntsc_ungamma[clamp_uint8(r+40)];
		uint8_t G = generic->ntsc_ungamma[clamp_uint8(g+40)];
		uint8_t B = generic->ntsc_ungamma[clamp_uint8(b+40)];

		*(generic->pixel) = MAPCOLOUR(generic, R, G, B);
		generic->pixel += XSTEP;
		phase = (phase+1) & 3;
		v++;
	}
	UNLOCK_SURFACE(generic);
	generic->pixel += NEXTLINE;
}

static void set_vo_cmp(void *sptr, int mode) {
	struct vo_generic_interface *generic = sptr;
	struct vo_interface *vo = &generic->public;
	switch (mode) {
	case VO_CMP_PALETTE:
		vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_scanline, vo);
		break;
	case VO_CMP_2BIT:
		vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_ccr_simple, vo);
		break;
	case VO_CMP_5BIT:
		vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_ccr_5bit, vo);
		break;
	case VO_CMP_SIMULATED:
		vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_ntsc, vo);
		break;
	}
}

static void generic_vsync(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	generic->scanline = 0;
}
