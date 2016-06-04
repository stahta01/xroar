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
#include "vdg_palette.h"

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

/* For now, duplicate data from vdg_palette.c.  Should be associated with the
 * video chip really, with this code being fed YUV or RGB. */

#define VDG_BLACK_LEVEL (0.00)
#define VDG_Y_SYNC (1.00)
#define VDG_Y_BLACK (0.72)
#define VDG_Y_BLANK (0.77)
#define VDG_Y_WHITE (0.42)
#define VDG_Y_SCALE (1.00 / (VDG_Y_BLANK - VDG_Y_WHITE))
#define VDG_CHB (1.50)
#define NUM_VDG_COLOURS (12)

struct palette {
	float y;
	float b;
	float a;
};

static const struct palette vdg_colours[NUM_VDG_COLOURS] = {
	{ .y = 0.540, .b = 1.00, .a = 1.00 },
	{ .y = 0.420, .b = 1.00, .a = 1.50 },
	{ .y = 0.650, .b = 2.00, .a = 1.50 },
	{ .y = 0.650, .b = 1.50, .a = 2.00 },
	{ .y = 0.420, .b = 1.50, .a = 1.50 },
	{ .y = 0.540, .b = 1.50, .a = 1.00 },
	{ .y = 0.540, .b = 2.00, .a = 2.00 },
	{ .y = 0.540, .b = 1.00, .a = 2.00 },
	{ .y = 0.720, .b = 1.50, .a = 1.50 },
	{ .y = 0.720, .b = 1.00, .a = 1.00 },
	{ .y = 0.720, .b = 1.00, .a = 2.00 },
	{ .y = 0.420, .b = 1.00, .a = 2.00 },
};

/* Composite encoding per-colour * 4 phases */
static int vdg_ntsc[4][NUM_VDG_COLOURS];

/* Gamma LUTs */
static float ntsc_gamma[256];
static uint8_t ntsc_ungamma[256];

static int clamp_uint8(int v) {
	if (v < 0) return 0;
	if (v > 255) return 255;
	return v;
}

/* Allocate colours */

static void alloc_colours(void) {
#ifdef RESET_PALETTE
	RESET_PALETTE();
#endif
	for (int j = 0; j < 12; j++) {
		vdg_colour[j] = map_palette_entry(j);
	}

	// Populate NTSC gamma LUT
	for (int j = 0; j < 256; j++) {
		float c = j / 255.0;
		if (c <= 0.018) {
			c *= 4.5;
		} else {
			c = ((1.+0.099)*powf(c, 1./2.2)) - 0.099;
		}
		ntsc_gamma[j] = c;
	}

	// Populate NTSC inverse gamma LUT
	for (int j = 0; j < 256; j++) {
		float c = j / 255.0;
		if (c <= (0.018 * 4.5)) {
			c /= 4.5;
		} else {
			c = powf((c+0.099)/(1.+0.099), 2.4);
		}
		ntsc_ungamma[j] = (int)(c * 255.0);
	}

	// Populate NTSC encodings
	for (int j = 0; j < NUM_VDG_COLOURS; j++) {
		// Y, B-Y, R-Y from VDG voltage tables
		float y = vdg_colours[j].y;
		float b_y = vdg_colours[j].b - VDG_CHB;
		float r_y = vdg_colours[j].a - VDG_CHB;
		// Scale Y
		y = VDG_BLACK_LEVEL + (VDG_Y_BLANK - y) * VDG_Y_SCALE;
		// Gamma correct Y
		// XXX not sure why this is necessary, maybe with right
		// brightness/contrast/hue controls it won't be
		y = ntsc_gamma[(int)(y * 255.0)];
		// Compute I,Q from Y, B-Y, R-Y
		float i = -0.27 * b_y + 0.74 * r_y;
		float q =  0.41 * b_y + 0.48 * r_y;
		// Composite signal, four phases
		vdg_ntsc[0][j] = clamp_uint8(255.0*(y + i));
		vdg_ntsc[1][j] = clamp_uint8(255.0*(y + q));
		vdg_ntsc[2][j] = clamp_uint8(255.0*(y - i));
		vdg_ntsc[3][j] = clamp_uint8(255.0*(y - q));
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

static void render_scanline(uint8_t const *scanline_data) {
	if (vo_module->scanline >= vo_module->window_y &&
	    vo_module->scanline < (vo_module->window_y + vo_module->window_h)) {
		scanline_data += vo_module->window_x;
		LOCK_SURFACE;
		for (int i = vo_module->window_w; i; i--) {
			*pixel = vdg_colour[*(scanline_data++)];
			pixel += XSTEP;
		}
		UNLOCK_SURFACE;
		pixel += NEXTLINE;
	}
	vo_module->scanline++;
}

/* Render artifacted colours - simple 4-colour lookup */

static void render_ccr_simple(uint8_t const *scanline_data) {
	if (vo_module->scanline >= vo_module->window_y &&
	    vo_module->scanline < (vo_module->window_y + vo_module->window_h)) {
		int phase = xroar_machine_config->cross_colour_phase - 1;
		scanline_data += vo_module->window_x;
		LOCK_SURFACE;
		for (int i = vo_module->window_w >> 1; i; i--) {
			uint8_t c0 = *scanline_data;
			uint8_t c1 = *(scanline_data+2);
			scanline_data += 4;
			if (c0 == VDG_BLACK || c0 == VDG_WHITE) {
				int aindex = ((c0 != VDG_BLACK) ? 2 : 0)
					     | ((c1 != VDG_BLACK) ? 1 : 0);
				*pixel = *(pixel+1) = *(pixel+2) = *(pixel+3) = artifact_simple[phase][aindex];
			} else {
				*pixel = *(pixel+1) = vdg_colour[c0];
				*(pixel+1) = *(pixel+2) = *(pixel+3) = vdg_colour[c1];
			}
			pixel += 4*XSTEP;
		}
		UNLOCK_SURFACE;
		pixel += NEXTLINE;
	}
	vo_module->scanline++;
}

/* Render artifacted colours - 5-bit lookup table */

static void render_ccr_5bit(uint8_t const *scanline_data) {
	if (vo_module->scanline >= vo_module->window_y &&
	    vo_module->scanline < (vo_module->window_y + vo_module->window_h)) {
		int phase = xroar_machine_config->cross_colour_phase - 1;
		unsigned aindex = 0;
		scanline_data += vo_module->window_x;
		aindex = (*(scanline_data - 6) != VDG_BLACK) ? 14 : 0;
		aindex |= (*(scanline_data - 2) != VDG_BLACK) ? 1 : 0;
		LOCK_SURFACE;
		for (int i = vo_module->window_w/2; i; i--) {
			aindex = (aindex << 1) & 31;
			if (*(scanline_data + 4) != VDG_BLACK)
				aindex |= 1;
			uint8_t c = *scanline_data;
			scanline_data += 2;
			if (c == VDG_BLACK || c == VDG_WHITE) {
				*pixel = *(pixel+1) = artifact_5bit[phase][aindex];
			} else {
				*pixel = *(pixel+1) = vdg_colour[c];
			}
			pixel += 2*XSTEP;
			phase ^= 1;
		}
		UNLOCK_SURFACE;
		pixel += NEXTLINE;
	}
	vo_module->scanline++;
}

/*

References:
    http://www.le.ac.uk/eg/fss1/FIRFILT.C

Low-pass filter, fs=28MHz, cutoff=4.2MHz, rectangular window, M=3.

*/

#define N0 (8307)
#define N1 (7130)
#define N2 (4191)
#define N3 (907)

/* Scale factor */
#define NSHIFT (15)

/* Filter offset */
#define NOFF (3)

/* NTSC composite video simulation */

static void render_ntsc(uint8_t const *scanline_data) {
	if (vo_module->scanline < vo_module->window_y ||
	    vo_module->scanline >= (vo_module->window_y + vo_module->window_h)) {
		vo_module->scanline++;
		return;
	}
	vo_module->scanline++;
	const uint8_t *v = (scanline_data + vo_module->window_x) - NOFF;
	int phase = (vo_module->window_x + 3 + 2*xroar_machine_config->cross_colour_phase) % 4;
	LOCK_SURFACE;
	for (int j = vo_module->window_w; j; j--) {
		// Warning: for speed, no bounds checking is performed here.
		// If video data is out of range, this will read from invalid
		// addresses.
		const int *ntsc0 = vdg_ntsc[(phase+0)&3];
		const int *ntsc1 = vdg_ntsc[(phase+1)&3];
		const int *ntsc2 = vdg_ntsc[(phase+2)&3];
		const int *ntsc3 = vdg_ntsc[(phase+3)&3];
		int y, i, q;
		y = N3*ntsc1[v[NOFF-3]] + N2*ntsc2[v[NOFF-2]] + N1*ntsc3[v[NOFF-1]] +
		    N0*ntsc0[v[NOFF]] +
		    N1*ntsc1[v[NOFF+1]] + N2*ntsc2[v[NOFF+2]] + N3*ntsc3[v[NOFF+3]];
		switch (phase) {
		case 0: default:
			i = - N2*ntsc2[v[NOFF-2]] + N0*ntsc0[v[NOFF]] - N2*ntsc2[v[NOFF+2]];
			q = + N3*ntsc1[v[NOFF-3]] - N1*ntsc3[v[NOFF-1]] + N1*ntsc1[v[NOFF+1]] - N3*ntsc3[v[NOFF+3]];
			break;
		case 1:
			q = - N2*ntsc2[v[NOFF-2]] + N0*ntsc0[v[NOFF]] - N2*ntsc2[v[NOFF+2]];
			i = - N3*ntsc1[v[NOFF-3]] + N1*ntsc3[v[NOFF-1]] - N1*ntsc1[v[NOFF+1]] + N3*ntsc3[v[NOFF+3]];
			break;
		case 2:
			i = + N2*ntsc2[v[NOFF-2]] - N0*ntsc0[v[NOFF]] + N2*ntsc2[v[NOFF+2]];
			q = - N3*ntsc1[v[NOFF-3]] + N1*ntsc3[v[NOFF-1]] - N1*ntsc1[v[NOFF+1]] + N3*ntsc3[v[NOFF+3]];
			break;
		case 3:
			q = + N2*ntsc2[v[NOFF-2]] - N0*ntsc0[v[NOFF]] + N2*ntsc2[v[NOFF+2]];
			i = + N3*ntsc1[v[NOFF-3]] - N1*ntsc3[v[NOFF-1]] + N1*ntsc1[v[NOFF+1]] - N3*ntsc3[v[NOFF+3]];
			break;
		}

		int r = 1.0 * y + 0.956 * i + 0.621 * q;
		int g = 1.0 * y - 0.272 * i - 0.647 * q;
		int b = 1.0 * y - 1.105 * i + 1.702 * q;
		r >>= NSHIFT;
		g >>= NSHIFT;
		b >>= NSHIFT;
		uint8_t R = ntsc_ungamma[clamp_uint8(r)];
		uint8_t G = ntsc_ungamma[clamp_uint8(g)];
		uint8_t B = ntsc_ungamma[clamp_uint8(b)];
		*pixel = MAPCOLOUR(R, G, B);
		pixel += XSTEP;
		phase = (phase+1) & 3;
		v++;
	}
	UNLOCK_SURFACE;
	pixel += NEXTLINE;
}

static void update_cross_colour_phase(void) {
	if (xroar_machine_config->cross_colour_phase == CROSS_COLOUR_OFF) {
		vo_module->render_scanline = render_scanline;
	} else {
		switch (xroar_ui_cfg.ccr) {
		case UI_CCR_SIMPLE:
			vo_module->render_scanline = render_ccr_simple;
			break;
		case UI_CCR_5BIT: default:
			vo_module->render_scanline = render_ccr_5bit;
			break;
		case UI_CCR_NTSC:
			vo_module->render_scanline = render_ntsc;
			break;
		}
	}
}
