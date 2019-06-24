/*

Video output module generic operations

Copyright 2003-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

This file contains generic scanline rendering routines.  It is included
into various video module source files and makes use of macros defined
in those files (eg, LOCK_SURFACE and XSTEP)

*/

#include <math.h>

#include "machine.h"
#include "module.h"
#include "ntsc.h"
#include "vdg_palette.h"
#include "mc6847/mc6847.h"

// - - - - - - -

#ifdef WANT_SIMULATED_NTSC

#define SCALE_PIXELS (1)
#define PIXEL_DENSITY (2)

#else

// Trades off speed for accuracy by halving the video data rate.
#define SCALE_PIXELS (2)
#define PIXEL_DENSITY (1)

#endif

// - - - - - - -

struct vo_generic_interface {
	VO_MODULE_INTERFACE module;

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
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
	(void)burst;
	(void)phase;
	if (generic->scanline >= vo->window_y &&
	    generic->scanline < (vo->window_y + vo->window_h)) {
		scanline_data += vo->window_x/SCALE_PIXELS;
		LOCK_SURFACE(generic);
		for (int i = vo->window_w >> 1; i; i--) {
			uint8_t c0 = *scanline_data;
			scanline_data += PIXEL_DENSITY;
#ifdef WANT_SIMULATED_NTSC
			*(generic->pixel) = *(generic->pixel+1) = generic->vdg_colour[c0];
#else
			*(generic->pixel) = generic->vdg_colour[c0];
#endif
			generic->pixel += PIXEL_DENSITY*XSTEP;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

/* Render artifacted colours - simple 4-colour lookup */

static void render_ccr_simple(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
	(void)burst;
	unsigned p = (phase >> 2) & 1;
	if (generic->scanline >= vo->window_y &&
	    generic->scanline < (vo->window_y + vo->window_h)) {
		scanline_data += vo->window_x/SCALE_PIXELS;
		LOCK_SURFACE(generic);
		for (int i = vo->window_w / 4; i; i--) {
			uint8_t c0 = *scanline_data;
			uint8_t c1 = *(scanline_data+PIXEL_DENSITY);
			scanline_data += 2*PIXEL_DENSITY;
			if (c0 == VDG_BLACK || c0 == VDG_WHITE) {
				int aindex = ((c0 != VDG_BLACK) ? 2 : 0)
					     | ((c1 != VDG_BLACK) ? 1 : 0);
#ifdef WANT_SIMULATED_NTSC
				*(generic->pixel) = *(generic->pixel+1) = *(generic->pixel+2) = *(generic->pixel+3) = generic->artifact_simple[p][aindex];
#else
				*(generic->pixel) = *(generic->pixel+1) = generic->artifact_simple[p][aindex];
#endif
			} else {
#ifdef WANT_SIMULATED_NTSC
				*(generic->pixel) = *(generic->pixel+1) = generic->vdg_colour[c0];
				*(generic->pixel+2) = *(generic->pixel+3) = generic->vdg_colour[c1];
#else
				*(generic->pixel) = generic->vdg_colour[c0];
				*(generic->pixel+1) = generic->vdg_colour[c1];
#endif
			}
			generic->pixel += 2*PIXEL_DENSITY*XSTEP;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

/* Render artifacted colours - 5-bit lookup table */

static void render_ccr_5bit(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
	(void)burst;
	unsigned p = (phase >> 2) & 1;
	if (generic->scanline >= vo->window_y &&
	    generic->scanline < (vo->window_y + vo->window_h)) {
		unsigned aindex = 0;
		scanline_data += vo->window_x/SCALE_PIXELS;
		aindex = (*(scanline_data - 3*PIXEL_DENSITY) != VDG_BLACK) ? 14 : 0;
		aindex |= (*(scanline_data - 1*PIXEL_DENSITY) != VDG_BLACK) ? 1 : 0;
		LOCK_SURFACE(generic);
		for (int i = vo->window_w/2; i; i--) {
			aindex = (aindex << 1) & 31;
			if (*(scanline_data + 2*PIXEL_DENSITY) != VDG_BLACK)
				aindex |= 1;
			uint8_t c = *scanline_data;
			scanline_data += PIXEL_DENSITY;
			if (c == VDG_BLACK || c == VDG_WHITE) {
#ifdef WANT_SIMULATED_NTSC
				*(generic->pixel) = *(generic->pixel+1) = generic->artifact_5bit[p][aindex];
#else
				*(generic->pixel) = generic->artifact_5bit[p][aindex];
#endif
			} else {
#ifdef WANT_SIMULATED_NTSC
				*(generic->pixel) = *(generic->pixel+1) = generic->vdg_colour[c];
#else
				*(generic->pixel) = generic->vdg_colour[c];
#endif
			}
			generic->pixel += 1*PIXEL_DENSITY*XSTEP;
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
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
	if (generic->scanline < vo->window_y ||
	    generic->scanline >= (vo->window_y + vo->window_h)) {
		generic->scanline++;
		return;
	}
	generic->scanline++;
	const uint8_t *v = (scanline_data + vo->window_x) - 3;
	ntsc_phase = ((phase + vo->window_x) + 3) & 3;
	LOCK_SURFACE(generic);
	for (int j = vo->window_w; j; j--) {
		struct ntsc_xyz rgb = ntsc_decode(burst, v++);
		// 40 is a reasonable value for brightness
		// TODO: make this adjustable
		int R = generic->ntsc_ungamma[clamp_uint8(rgb.x+40)];
		int G = generic->ntsc_ungamma[clamp_uint8(rgb.y+40)];
		int B = generic->ntsc_ungamma[clamp_uint8(rgb.z+40)];
		*(generic->pixel) = MAPCOLOUR(generic, R, G, B);
		generic->pixel += XSTEP;
	}
	UNLOCK_SURFACE(generic);
	generic->pixel += NEXTLINE;
}

static void set_vo_cmp(void *sptr, int mode) {
	struct vo_generic_interface *generic = sptr;
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
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
