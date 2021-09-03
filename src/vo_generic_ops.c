/** \file
 *
 *  \brief Video output module generic operations.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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
 *  This file contains generic scanline rendering routines.  It is included
 *  into various video module source files and makes use of macros defined in
 *  those files (eg, LOCK_SURFACE and XSTEP)
 */

#include <math.h>

#include "intfuncs.h"

#include "colourspace.h"
#include "machine.h"
#include "module.h"
#include "ntsc.h"
#include "vdg_palette.h"
#include "mc6847/mc6847.h"

// - - - - - - -

// Palettes consist of a mask limiting number of entries, and an allocated list
// of pixel values.
struct vo_palette {
	uint8_t mask;
	Pixel *values;
};

struct vo_generic_interface {
	VO_MODULE_INTERFACE module;

	struct {
		// Composite output palette.
		struct vo_palette palette;
		// Cache testing if each colour is black or white.
		unsigned *is_black_or_white;
		// A 2-bit fast LUT for NTSC cross-colour.
		Pixel cc_2bit[2][4];
		// A 5-bit LUT for slightly better-looking NTSC cross-colour.
		Pixel cc_5bit[2][32];
		// And a full NTSC decode table.
		struct ntsc_palette *ntsc_palette;
	} cmp;

	struct {
		// RGB output palette.
		struct vo_palette palette;
	} rgb;

	// Currently selected input.
	struct vo_palette *input_palette;

	// Current render pointer
	Pixel *pixel;
	int scanline;

	// Colourspace definition.
	struct cs_profile *cs;

	// Buffer for NTSC line encode.
	// XXX if window_w can change, this must too!
	uint8_t ntsc_buf[647];

	// Gamma LUT.
	uint8_t ntsc_ungamma[256];
};

// Must be called by encapsulating video module on startup.

static void vo_generic_init(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	*generic = (struct vo_generic_interface){0};
	generic->cmp.palette.values = xmalloc(sizeof(Pixel));
	generic->cmp.is_black_or_white = xmalloc(sizeof(unsigned));
	generic->cmp.ntsc_palette = ntsc_palette_new();
	generic->rgb.palette.values = xmalloc(sizeof(Pixel));
	generic->input_palette = &generic->cmp.palette;
	generic->cs = cs_profile_by_name("ntsc");

#ifdef RESET_PALETTE
	RESET_PALETTE();
#endif

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

	for (int i = 0; i < 2; i++) {
		// 2-bit LUT NTSC cross-colour
		for (int j = 0; j < 4; j++) {
			generic->cmp.cc_2bit[i][j] = MAPCOLOUR(generic, vo_cmp_lut_2bit[i][j][0], vo_cmp_lut_2bit[i][j][1], vo_cmp_lut_2bit[i][j][2]);
		}
		// 5-bit LUT NTSC cross-colour
		// TODO: generate this using available NTSC decoding
		for (int j = 0; j < 32; j++) {
			generic->cmp.cc_5bit[i][j] = MAPCOLOUR(generic, vo_cmp_lut_5bit[i][j][0], vo_cmp_lut_5bit[i][j][1], vo_cmp_lut_5bit[i][j][2]);
		}
	}
}

// Must be called by encapsulating video module on exit.

static void vo_generic_free(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	ntsc_palette_free(generic->cmp.ntsc_palette);
	free(generic->rgb.palette.values);
	free(generic->cmp.is_black_or_white);
	free(generic->cmp.palette.values);
}

// Set a palette entry, adjusting mask and reallocating memory if needed.  This
// way, palette is only as large as it needs to be while maintaining a simple
// mask for bounds limiting.

static void palette_set(uint8_t c, float R, float G, float B, struct vo_palette *palette) {
	uint8_t oldmask = palette->mask;
	while (c > palette->mask) {
		palette->mask = (palette->mask << 1) | 1;
	}
	if (!palette->values || palette->mask != oldmask) {
		palette->values = xrealloc(palette->values, (palette->mask+1) * sizeof(Pixel));
	}
	cs_clamp(&R, &G, &B);
	R *= 255.0;
	G *= 255.0;
	B *= 255.0;
	(palette->values)[c] = MAPCOLOUR(generic, (int)R, (int)G, (int)B);
}

// Add palette entry to RGB palette as R', G', B'

static void palette_set_rgb(void *sptr, uint8_t c, float r, float g, float b) {
	struct vo_generic_interface *generic = sptr;

	float R, G, B;
	cs_mlaw(generic->cs, r, g, b, &R, &G, &B);

	palette_set(c, R, G, B, &generic->rgb.palette);
}

// Add palette entry to composite palette as Y', B'-Y', R'-Y'

static void palette_set_ybr(void *sptr, uint8_t c, float y, float b_y, float r_y) {
	struct vo_generic_interface *generic = sptr;

	float u = 0.493 * b_y;
	float v = 0.877 * r_y;
	float r = 1.0 * y + 0.000 * u + 1.140 * v;
	float g = 1.0 * y - 0.396 * u - 0.581 * v;
	float b = 1.0 * y + 2.029 * u + 0.000 * v;

	/* These values directly relate to voltages fed to a modulator which,
	 * I'm assuming, does nothing further to correct for the non-linearity
	 * of the display device.  Therefore, these can be considered "gamma
	 * corrected" values, and to work with them in linear RGB, we need to
	 * undo the assumed characteristics of the display.  NTSC was
	 * originally defined differently, but most SD televisions that people
	 * will have used any time recently are probably close to Rec. 601, so
	 * use that transfer function:
	 *
	 * L = V/4.5                        for V <  0.081
	 * L = ((V + 0.099) / 1.099) ^ 2.2  for V >= 0.081
	 *
	 * Note: the same transfer function is specified for Rec. 709.
	 */

	float R, G, B;
	cs_mlaw(generic->cs, r, g, b, &R, &G, &B);

	palette_set(c, R, G, B, &generic->cmp.palette);

	ntsc_palette_add_ybr(generic->cmp.ntsc_palette, c, y, b_y, r_y);

	generic->cmp.is_black_or_white = xrealloc(generic->cmp.is_black_or_white, (generic->cmp.palette.mask+1) * sizeof(unsigned));
	if (r > 0.85 && g > 0.85 && b > 0.85) {
		generic->cmp.is_black_or_white[c] = 2;
	} else if (r < 0.20 && g < 0.20 && b < 0.20) {
		generic->cmp.is_black_or_white[c] = 1;
	} else {
		generic->cmp.is_black_or_white[c] = 0;
	}
}

// Render colour line using palette.  Used for RGB and palette-based CMP.

static void render_palette(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
	(void)burst;
	(void)phase;
	if (generic->scanline >= vo->window.y &&
	    generic->scanline < (vo->window.y + vo->window.h)) {
		scanline_data += vo->window.x;
		uint8_t mask = generic->input_palette->mask;
		LOCK_SURFACE(generic);
		for (int i = vo->window.w; i; i--) {
			uint8_t c0 = *scanline_data & mask;
			scanline_data++;
			Pixel p0 = generic->input_palette->values[c0];
			*(generic->pixel) = p0;
			generic->pixel += XSTEP;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

// Render artefact colours using simple 2-bit LUT.

static void render_ccr_2bit(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
	(void)burst;
	unsigned p = (phase >> 2) & 1;
	if (generic->scanline >= vo->window.y &&
	    generic->scanline < (vo->window.y + vo->window.h)) {
		scanline_data += vo->window.x;
		uint8_t mask = generic->input_palette->mask;
		LOCK_SURFACE(generic);
		for (int i = vo->window.w / 4; i; i--) {
			uint8_t c0 = *scanline_data & mask;
			uint8_t c1 = *(scanline_data + 2) & mask;
			if (generic->cmp.is_black_or_white[c0] && generic->cmp.is_black_or_white[c1]) {
				scanline_data += 4;
				unsigned aindex = (generic->cmp.is_black_or_white[c0] & 2) | (generic->cmp.is_black_or_white[c1] >> 1);
				Pixel pout = generic->cmp.cc_2bit[p][aindex];
				*(generic->pixel) = pout;
				*(generic->pixel+1*XSTEP) = pout;
				*(generic->pixel+2*XSTEP) = pout;
				*(generic->pixel+3*XSTEP) = pout;
				generic->pixel += 4*XSTEP;
			} else {
				for (int j = 4; j; j--) {
					c0 = *scanline_data & mask;
					scanline_data++;
					Pixel p0 = generic->cmp.palette.values[c0];
					*(generic->pixel) = p0;
					generic->pixel += XSTEP;
				}
			}
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

// Render artefact colours using 5-bit LUT.

static void render_ccr_5bit(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
	(void)burst;
	unsigned p = (phase >> 2) & 1;
	if (generic->scanline >= vo->window.y &&
	    generic->scanline < (vo->window.y + vo->window.h)) {
		unsigned aindex = 0;
		scanline_data += vo->window.x;
		uint8_t mask = generic->input_palette->mask;
		aindex = (generic->cmp.is_black_or_white[*(scanline_data-6)] != 1) ? 14 : 0;
		aindex |= (generic->cmp.is_black_or_white[*(scanline_data-2)] != 1) ? 1 : 0;
		LOCK_SURFACE(generic);
		for (int i = vo->window.w / 2; i; i--) {
			aindex = (aindex << 1) & 31;
			if (generic->cmp.is_black_or_white[*(scanline_data+4)] != 1)
				aindex |= 1;
			uint8_t c0 = *scanline_data & mask;
			if (generic->cmp.is_black_or_white[c0]) {
				scanline_data += 2;
				Pixel pout = generic->cmp.cc_5bit[p][aindex];
				*(generic->pixel) = pout;
				*(generic->pixel+1*XSTEP) = pout;
				generic->pixel += 2*XSTEP;
			} else {
				for (int j = 2; j; j--) {
					c0 = *scanline_data & mask;
					scanline_data++;
					Pixel p0 = generic->cmp.palette.values[c0];
					*(generic->pixel) = p0;
					generic->pixel += XSTEP;
				}
			}
			p ^= 1;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

// NTSC composite video simulation.

static void render_ntsc(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst, unsigned phase) {
	struct vo_generic_interface *generic = sptr;
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;
	if (generic->scanline < vo->window.y ||
	    generic->scanline >= (vo->window.y + vo->window.h)) {
		generic->scanline++;
		return;
	}
	generic->scanline++;

	// Encode NTSC
	const uint8_t *src = scanline_data + vo->window.x - 3;
	uint8_t *dst = generic->ntsc_buf;
	ntsc_phase = (phase + vo->window.x) & 3;
	for (int i = vo->window.w + 6; i; i--) {
		unsigned c = *(src++);
		*(dst++) = ntsc_encode_from_palette(generic->cmp.ntsc_palette, c);
	}

	// And now decode
	src = generic->ntsc_buf;
	ntsc_phase = ((phase + vo->window.x) + 3) & 3;
	LOCK_SURFACE(generic);
	for (int j = vo->window.w; j; j--) {
		struct ntsc_xyz rgb = ntsc_decode(burst, src++);
		// 40 is a reasonable value for brightness
		// TODO: make this adjustable
		int R = generic->ntsc_ungamma[int_clamp_u8(rgb.x+40)];
		int G = generic->ntsc_ungamma[int_clamp_u8(rgb.y+40)];
		int B = generic->ntsc_ungamma[int_clamp_u8(rgb.z+40)];
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
		vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_palette, vo);
		break;
	case VO_CMP_2BIT:
		vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, render_ccr_2bit, vo);
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
