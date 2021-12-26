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

struct vo_generic_interface {
	VO_MODULE_INTERFACE module;

	struct {
		// Composite output palette.
		Pixel palette[256];
		// Cache testing if each colour is black or white.
		uint8_t is_black_or_white[256];
		// A 2-bit fast LUT for NTSC cross-colour.
		Pixel cc_2bit[2][4];
		// A 5-bit LUT for slightly better-looking NTSC cross-colour.
		Pixel cc_5bit[2][32];
		// And a full NTSC decode table.
		struct ntsc_palette *ntsc_palette;
	} cmp;

	struct {
		// RGB output palette.
		Pixel palette[256];
	} rgb;

	// Currently selected input.
	Pixel *input_palette;

	// Current render pointer
	Pixel *pixel;
	unsigned scanline;

	// Colourspace definition.
	struct cs_profile *cs;

	// Buffer for NTSC line encode.
	// XXX if window_w can change, this must too!
	uint8_t ntsc_buf[647];

	// Gamma LUT.
	uint8_t ntsc_ungamma[256];

	// Viewport
	struct vo_rect viewport;

	// Render configuration.
	int input;      // VO_TV_CMP or VO_TV_RGB
	int cmp_ccr;    // VO_CMP_CCR_NONE, _2BIT, _5BIT or _SIMULATED
	int cmp_phase;  // 0 or 2 are useful
	int cmp_phase_offset;  // likewise
};

// Must be called by encapsulating video module on startup.

static void vo_generic_init(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	*generic = (struct vo_generic_interface){0};
	generic->cmp.ntsc_palette = ntsc_palette_new();
	generic->input_palette = generic->cmp.palette;
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

	// Sensible defaults, should be overridden by call to set_viewport*()
	generic->viewport = (struct vo_rect){ .x = 190, .y = 14, .w = 640, .h = 240 };
	generic->cmp_phase_offset = 2;
}

// Must be called by encapsulating video module on exit.

static void vo_generic_free(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	ntsc_palette_free(generic->cmp.ntsc_palette);
}

// Set viewport geometry

static void set_viewport_xy(void *sptr, unsigned x, unsigned y) {
	struct vo_generic_interface *generic = sptr;
	// XXX bounds checking?  Only really going to be needed if user ends up
	// able to move the viewport...
	generic->viewport.x = x;
	generic->viewport.y = y;
	generic->scanline = y + generic->viewport.h;
}

// Set a palette entry.

static void palette_set(uint8_t c, float R, float G, float B, Pixel *palette) {
	cs_clamp(&R, &G, &B);
	R *= 255.0;
	G *= 255.0;
	B *= 255.0;
	palette[c] = MAPCOLOUR(generic, (int)R, (int)G, (int)B);
}

// Add palette entry to RGB palette as R', G', B'

static void palette_set_rgb(void *sptr, uint8_t c, float r, float g, float b) {
	struct vo_generic_interface *generic = sptr;

	float R, G, B;
	cs_mlaw(generic->cs, r, g, b, &R, &G, &B);

	palette_set(c, R, G, B, generic->rgb.palette);
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

	palette_set(c, R, G, B, generic->cmp.palette);

	ntsc_palette_add_ybr(generic->cmp.ntsc_palette, c, y, b_y, r_y);

	if (r > 0.85 && g > 0.85 && b > 0.85) {
		generic->cmp.is_black_or_white[c] = 3;
	} else if (r < 0.20 && g < 0.20 && b < 0.20) {
		generic->cmp.is_black_or_white[c] = 2;
	} else {
		generic->cmp.is_black_or_white[c] = 0;
	}
}

// Render colour line using palette.  Used for RGB and palette-based CMP.

static void render_palette(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst) {
	struct vo_generic_interface *generic = sptr;
	(void)burst;
	if (generic->scanline >= generic->viewport.y &&
	    generic->scanline < (generic->viewport.y + generic->viewport.h)) {
		scanline_data += generic->viewport.x;
		LOCK_SURFACE(generic);
		for (int i = generic->viewport.w >> 2; i; i--) {
			uint8_t c0 = *(scanline_data++);
			uint8_t c1 = *(scanline_data++);
			uint8_t c2 = *(scanline_data++);
			uint8_t c3 = *(scanline_data++);
			Pixel p0 = generic->input_palette[c0];
			Pixel p1 = generic->input_palette[c1];
			Pixel p2 = generic->input_palette[c2];
			Pixel p3 = generic->input_palette[c3];
			*(generic->pixel+0*XSTEP) = p0;
			*(generic->pixel+1*XSTEP) = p1;
			*(generic->pixel+2*XSTEP) = p2;
			*(generic->pixel+3*XSTEP) = p3;
			generic->pixel += 4*XSTEP;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

// Render artefact colours using simple 2-bit LUT.

static void render_ccr_2bit(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst) {
	struct vo_generic_interface *generic = sptr;
	(void)burst;
	unsigned p = !(generic->cmp_phase & 2);
	if (generic->scanline >= generic->viewport.y &&
	    generic->scanline < (generic->viewport.y + generic->viewport.h)) {
		scanline_data += generic->viewport.x;
		LOCK_SURFACE(generic);
		for (int i = generic->viewport.w >> 2; i; i--) {
			Pixel p0, p1, p2, p3;
			uint8_t c0 = *scanline_data;
			uint8_t c2 = *(scanline_data + 2);
			if (generic->cmp.is_black_or_white[c0] && generic->cmp.is_black_or_white[c2]) {
				unsigned aindex = (generic->cmp.is_black_or_white[c0] << 1) | (generic->cmp.is_black_or_white[c2] & 1);
				p0 = p1 = p2 = p3 = generic->cmp.cc_2bit[p][aindex & 3];
			} else {
				uint8_t c1 = *(scanline_data+1);
				uint8_t c3 = *(scanline_data+3);
				p0 = generic->cmp.palette[c0];
				p1 = generic->cmp.palette[c1];
				p2 = generic->cmp.palette[c2];
				p3 = generic->cmp.palette[c3];
			}
			scanline_data += 4;
			*(generic->pixel) = p0;
			*(generic->pixel+1*XSTEP) = p1;
			*(generic->pixel+2*XSTEP) = p2;
			*(generic->pixel+3*XSTEP) = p3;
			generic->pixel += 4*XSTEP;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

// Render artefact colours using 5-bit LUT.  Only explicitly black or white
// runs of pixels are considered to contribute to artefect colours, otherwise
// they are passed through from the palette.

static void render_ccr_5bit(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst) {
	struct vo_generic_interface *generic = sptr;
	(void)burst;
	unsigned p = !(generic->cmp_phase & 2);
	if (generic->scanline >= generic->viewport.y &&
	    generic->scanline < (generic->viewport.y + generic->viewport.h)) {
		unsigned ibwcount = 0;
		unsigned aindex = 0;
		scanline_data += generic->viewport.x;
		uint8_t ibw0 = generic->cmp.is_black_or_white[*(scanline_data-6)];
		uint8_t ibw1 = generic->cmp.is_black_or_white[*(scanline_data-2)];
		if (ibw0 && ibw1) {
			ibwcount = 7;
			aindex = (ibw0 & 1) ? 14 : 0;
			aindex |= (ibw1 & 1) ? 1 : 0;
		}
		LOCK_SURFACE(generic);
		for (int i = generic->viewport.w >> 2; i; i--) {
			Pixel p0, p1, p2, p3;

			uint8_t ibw2 = generic->cmp.is_black_or_white[*(scanline_data+2)];
			uint8_t ibw4 = generic->cmp.is_black_or_white[*(scanline_data+4)];
			uint8_t ibw6 = generic->cmp.is_black_or_white[*(scanline_data+6)];

			ibwcount = ((ibwcount << 1) | (ibw2 >> 1)) & 7;
			aindex = ((aindex << 1) | (ibw4 & 1));
			if (ibwcount == 7) {
				p0 = p1 = generic->cmp.cc_5bit[p][aindex & 31];
			} else {
				uint8_t c0 = *scanline_data;
				uint8_t c1 = *(scanline_data+1);
				p0 = generic->cmp.palette[c0];
				p1 = generic->cmp.palette[c1];
			}

			ibwcount = ((ibwcount << 1) | (ibw4 >> 1)) & 7;
			aindex = ((aindex << 1) | (ibw6 & 1));
			if (ibwcount == 7) {
				p2 = p3 = generic->cmp.cc_5bit[!p][aindex & 31];
			} else {
				uint8_t c2 = *(scanline_data+2);
				uint8_t c3 = *(scanline_data+3);
				p2 = generic->cmp.palette[c2];
				p3 = generic->cmp.palette[c3];
			}

			scanline_data += 4;
			*(generic->pixel) = p0;
			*(generic->pixel+1*XSTEP) = p1;
			*(generic->pixel+2*XSTEP) = p2;
			*(generic->pixel+3*XSTEP) = p3;
			generic->pixel += 4*XSTEP;
		}
		UNLOCK_SURFACE(generic);
		generic->pixel += NEXTLINE;
	}
	generic->scanline++;
}

// NTSC composite video simulation.

static void render_ntsc(void *sptr, uint8_t const *scanline_data, struct ntsc_burst *burst) {
	struct vo_generic_interface *generic = sptr;
	if (generic->scanline < generic->viewport.y ||
	    generic->scanline >= (generic->viewport.y + generic->viewport.h)) {
		generic->scanline++;
		return;
	}
	generic->scanline++;

	// Encode NTSC
	const uint8_t *src = scanline_data + generic->viewport.x - 3;
	uint8_t *dst = generic->ntsc_buf;
	ntsc_phase = (generic->cmp_phase + generic->viewport.x) & 3;
	for (int i = generic->viewport.w + 6; i; i--) {
		unsigned c = *(src++);
		*(dst++) = ntsc_encode_from_palette(generic->cmp.ntsc_palette, c);
	}

	// And now decode
	src = generic->ntsc_buf;
	ntsc_phase = ((generic->cmp_phase + generic->viewport.x) + 3) & 3;
	LOCK_SURFACE(generic);
	for (int j = generic->viewport.w; j; j--) {
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

static void update_render_parameters(struct vo_generic_interface *generic) {
	VO_MODULE_INTERFACE *vom = &generic->module;
	struct vo_interface *vo = &vom->public;

	switch (generic->input) {
	case VO_TV_CMP:
	default:
		generic->input_palette = generic->cmp.palette;
		break;
	case VO_TV_RGB:
		generic->input_palette = generic->rgb.palette;
		break;
	}

	// RGB is always palette-based
	if (generic->input == VO_TV_RGB) {
		vo->render_scanline = DELEGATE_AS2(void, uint8cp, ntscburst, render_palette, vo);
		return;
	}

	// Composite video has more options
	switch (generic->cmp_ccr) {
	case VO_CMP_CCR_NONE:
		vo->render_scanline = DELEGATE_AS2(void, uint8cp, ntscburst, render_palette, vo);
		break;
	case VO_CMP_CCR_2BIT:
		vo->render_scanline = DELEGATE_AS2(void, uint8cp, ntscburst, render_ccr_2bit, vo);
		break;
	case VO_CMP_CCR_5BIT:
		vo->render_scanline = DELEGATE_AS2(void, uint8cp, ntscburst, render_ccr_5bit, vo);
		break;
	case VO_CMP_CCR_SIMULATED:
		vo->render_scanline = DELEGATE_AS2(void, uint8cp, ntscburst, render_ntsc, vo);
		break;
	}
}

static void set_input(void *sptr, int input) {
	struct vo_generic_interface *generic = sptr;
	generic->input = input;
	update_render_parameters(generic);
}

static void set_cmp_ccr(void *sptr, int ccr) {
	struct vo_generic_interface *generic = sptr;
	generic->cmp_ccr = ccr;
	update_render_parameters(generic);
}

static void set_cmp_phase(void *sptr, int phase) {
	struct vo_generic_interface *generic = sptr;
	generic->cmp_phase = phase ^ generic->cmp_phase_offset;
}

static void set_cmp_phase_offset(void *sptr, int phase) {
	struct vo_generic_interface *generic = sptr;
	int p = generic->cmp_phase ^ generic->cmp_phase_offset;
	generic->cmp_phase_offset = phase ^ 2;
	set_cmp_phase(generic, p);
}

static void generic_vsync(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	generic->scanline = 0;
}
