/** \file
 *
 *  \brief Video output module generic operations.
 *
 *  \copyright Copyright 2003-2023 Ciaran Anscomb
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
#include "xroar.h"

#include "mc6847/mc6847.h"

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

struct vo_generic_interface {
	VO_MODULE_INTERFACE module;

	struct {
		// Record values for recalculation
		struct {
			float y, pb, pr;
		} colour[256];
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
		// Record values for recalculation
		struct {
			float r, g, b;
		} colour[256];
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

	// NTSC bursts
	unsigned nbursts;
	struct ntsc_burst **ntsc_burst;

	// Buffer for NTSC line encode.
	// XXX if window_w can change, this must too!
	uint8_t ntsc_buf[647];

	// Gamma LUT.
	uint8_t ntsc_ungamma[256];

	// Viewport
	struct vo_rect viewport;

	// Render configuration.
	int brightness;
	int contrast;
	int hue;
	int input;      // VO_TV_CMP or VO_TV_RGB
	int cmp_ccr;    // VO_CMP_CCR_NONE, _2BIT, _5BIT or _SIMULATED
	int cmp_phase;  // 0 or 2 are useful
	int cmp_phase_offset;  // likewise
};

static void update_gamma_table(struct vo_generic_interface *generic);
static void update_palette_ybr(struct vo_generic_interface *generic, uint8_t c);
static void update_palette_rgb(struct vo_generic_interface *generic, uint8_t c);
static void palette_set(Pixel *palette, uint8_t c, float R, float G, float B);
static void update_render_parameters(struct vo_generic_interface *generic);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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

	// Populate inverse gamma LUT
	generic->brightness = 50;
	generic->contrast = 50;
	generic->hue = 0;
	update_gamma_table(generic);

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
	for (unsigned i = 0; i < generic->nbursts; i++) {
		ntsc_burst_free(generic->ntsc_burst[i]);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by UI to adjust viewing parameters

// Configure viewport X and Y offset
//     unsigned x0, y0;  // offset to top-left displayed pixel

static void set_viewport_xy(void *sptr, unsigned x0, unsigned y0) {
	struct vo_generic_interface *generic = sptr;
	// XXX bounds checking?  Only really going to be needed if user ends up
	// able to move the viewport...
	generic->viewport.x = x0;
	generic->viewport.y = y0;
	generic->scanline = y0 + generic->viewport.h;
}

// Select TV "input"
//     int input;  // VO_TV_*

static void set_input(void *sptr, int input) {
	struct vo_generic_interface *generic = sptr;
	generic->input = input;
	update_render_parameters(generic);
}

// Set brightness
//     int brightness;  // 0-100

static void set_brightness(void *sptr, int brightness) {
	struct vo_generic_interface *generic = sptr;
	if (brightness < 0) brightness = 0;
	if (brightness > 100) brightness = 100;
	generic->brightness = brightness;
	for (unsigned i = 0; i < 256; i++) {
		update_palette_ybr(generic, i);
		update_palette_rgb(generic, i);
	}
	update_gamma_table(generic);
	if (xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_brightness, brightness, NULL);
	}
}

// Set contrast
//     int contrast;  // 0-100

static void set_contrast(void *sptr, int contrast) {
	struct vo_generic_interface *generic = sptr;
	if (contrast < 0) contrast = 0;
	if (contrast > 100) contrast = 100;
	generic->contrast = contrast;
	for (unsigned i = 0; i < 256; i++) {
		update_palette_ybr(generic, i);
		update_palette_rgb(generic, i);
	}
	update_gamma_table(generic);
	if (xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_contrast, contrast, NULL);
	}
}

// Set hue
//     int hue;  // -179 to +180

static void set_hue(void *sptr, int hue) {
	struct vo_generic_interface *generic = sptr;
	hue = ((hue + 179) % 360) - 179;
	generic->hue = hue;
	for (unsigned i = 0; i < 256; i++) {
		update_palette_ybr(generic, i);
	}
	if (xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_hue, hue, NULL);
	}
}

// Set cross-colour renderer
//     int ccr;  // VO_CMP_CCR_*

static void set_cmp_ccr(void *sptr, int ccr) {
	struct vo_generic_interface *generic = sptr;
	generic->cmp_ccr = ccr;
	update_render_parameters(generic);
}

// Set cross-colour phase
//     int phase;  // VO_CMP_PHASE_*

static void set_cmp_phase(void *sptr, int phase) {
	struct vo_generic_interface *generic = sptr;
	generic->cmp_phase = phase ^ generic->cmp_phase_offset;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by machine to configure video output

// Add palette entry to composite palette as Y', Pb, Pr

static void palette_set_ybr(void *sptr, uint8_t c, float y, float pb, float pr) {
	struct vo_generic_interface *generic = sptr;

	generic->cmp.colour[c].y = y;
	generic->cmp.colour[c].pb = pb;
	generic->cmp.colour[c].pr = pr;

	update_palette_ybr(generic, c);
}

// Add palette entry to RGB palette as R', G', B'

static void palette_set_rgb(void *sptr, uint8_t c, float r, float g, float b) {
	struct vo_generic_interface *generic = sptr;

	generic->rgb.colour[c].r = r;
	generic->rgb.colour[c].g = g;
	generic->rgb.colour[c].b = b;

	update_palette_rgb(generic, c);
}

// Set machine default cross-colour phase
//     int phase;  // VO_CMP_PHASE_*

static void set_cmp_phase_offset(void *sptr, int phase) {
	struct vo_generic_interface *generic = sptr;
	int p = generic->cmp_phase ^ generic->cmp_phase_offset;
	generic->cmp_phase_offset = phase ^ 2;
	set_cmp_phase(generic, p);
}

// Set a burst phase
//     unsigned burstn;  // burst index
//     int phase;        // in degrees

static void set_burst(void *sptr, unsigned burstn, int offset) {
	struct vo_generic_interface *generic = sptr;
	if (burstn >= generic->nbursts) {
		generic->nbursts = burstn + 1;
		generic->ntsc_burst = xrealloc(generic->ntsc_burst, generic->nbursts * sizeof(*(generic->ntsc_burst)));
	} else if (generic->ntsc_burst[burstn]) {
		ntsc_burst_free(generic->ntsc_burst[burstn]);
	}
	generic->ntsc_burst[burstn] = ntsc_burst_new(offset);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by machine to render video

// Submit a scanline for rendering
//     const uint8_t *data;       // palettised data
//     struct ntsc_burst *burst;  // colourburst for this line

static void render_palette(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data);
static void render_ccr_2bit(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data);
static void render_ccr_5bit(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data);
static void render_ntsc(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data);

// Vertical sync

static void generic_vsync(void *sptr) {
	struct vo_generic_interface *generic = sptr;
	generic->scanline = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Update gamma LUT

static void update_gamma_table(struct vo_generic_interface *generic) {
	float brightness = (float)(generic->brightness - 50) / 50.;
	float contrast = (float)generic->contrast / 50.;
	for (int j = 0; j < 256; j++) {
		float c = j / 255.0;
		c *= contrast;
		c += brightness;
		float C = cs_mlaw_1(generic->cs, c);
		generic->ntsc_ungamma[j] = int_clamp_u8((int)(C * 255.0));
	}
}

// Set a palette entry (called by palette_set_* after colour conversion)

static void palette_set(Pixel *palette, uint8_t c, float R, float G, float B) {
	cs_clamp(&R, &G, &B);
	R *= 255.0;
	G *= 255.0;
	B *= 255.0;
	palette[c] = MAPCOLOUR(generic, (int)R, (int)G, (int)B);
}

// Update a composite palette entry, applying brightness & contrast

static void update_palette_ybr(struct vo_generic_interface *generic, uint8_t c) {
	float y = generic->cmp.colour[c].y;
	float pb = generic->cmp.colour[c].pb;
	float pr = generic->cmp.colour[c].pr;

	// Scale Pb,Pr to valid B'-Y',R'-Y'
	float b_y = pb * 0.5/0.886;
	float r_y = pr * 0.5/0.701;

	// Apply hue
	float w = 2. * M_PI;
	float hue = (w * (float)generic->hue) / 360.;
	float nb_y = r_y * sin(hue) + b_y * cos(hue);
	float nr_y = r_y * cos(hue) - b_y * sin(hue);
	b_y = nb_y;
	r_y = nr_y;

	// Limit chroma extents
	if (b_y < -0.895) b_y = -0.895;
	if (b_y > 0.895) b_y = 0.895;
	if (r_y < -0.710) r_y = -0.710;
	if (r_y > 0.710) r_y = 0.710;

	// Convert to R'G'B'
	float r = y + r_y;
	float g = y - 0.1952 * b_y - 0.5095 * r_y;
	float b = y + b_y;

	// Apply brightness & contrast
	float brightness = (float)(generic->brightness - 50) / 50.;
	float contrast = (float)generic->contrast / 50.;
	r = (r * contrast) + brightness;
	g = (g * contrast) + brightness;
	b = (b * contrast) + brightness;

	float R, G, B;
	cs_mlaw(generic->cs, r, g, b, &R, &G, &B);

	palette_set(generic->cmp.palette, c, R, G, B);

	ntsc_palette_add_ybr(generic->cmp.ntsc_palette, c, y, pb, pr);

	if (r > 0.85 && g > 0.85 && b > 0.85) {
		generic->cmp.is_black_or_white[c] = 3;
	} else if (r < 0.20 && g < 0.20 && b < 0.20) {
		generic->cmp.is_black_or_white[c] = 2;
	} else {
		generic->cmp.is_black_or_white[c] = 0;
	}
}

// Update an RGB palette entry, applying brightness & contrast

static void update_palette_rgb(struct vo_generic_interface *generic, uint8_t c) {
	float r = generic->rgb.colour[c].r;
	float g = generic->rgb.colour[c].g;
	float b = generic->rgb.colour[c].b;

	// Apply brightness & contrast
	float brightness = (float)(generic->brightness - 50) / 50.;
	float contrast = (float)generic->contrast / 50.;
	r = (r * contrast) + brightness;
	g = (g * contrast) + brightness;
	b = (b * contrast) + brightness;

	float R, G, B;
	cs_mlaw(generic->cs, r, g, b, &R, &G, &B);

	palette_set(generic->rgb.palette, c, R, G, B);
}

// Housekeeping after selecting TV input

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
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, render_palette, vo);
		return;
	}

	// Composite video has more options
	switch (generic->cmp_ccr) {
	case VO_CMP_CCR_NONE:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, render_palette, vo);
		break;
	case VO_CMP_CCR_2BIT:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, render_ccr_2bit, vo);
		break;
	case VO_CMP_CCR_5BIT:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, render_ccr_5bit, vo);
		break;
	case VO_CMP_CCR_SIMULATED:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, render_ntsc, vo);
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Variants of render_line with different CPU/accuracy tradeoffs

// Render colour line using palette.  Used for RGB and palette-based CMP.

static void render_palette(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct vo_generic_interface *generic = sptr;
	(void)burstn;
	if (generic->scanline >= generic->viewport.y &&
	    generic->scanline < (generic->viewport.y + generic->viewport.h)) {
		data += generic->viewport.x;
		LOCK_SURFACE(generic);
		for (int i = generic->viewport.w >> 2; i; i--) {
			uint8_t c0 = *(data++);
			uint8_t c1 = *(data++);
			uint8_t c2 = *(data++);
			uint8_t c3 = *(data++);
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

static void render_ccr_2bit(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct vo_generic_interface *generic = sptr;
	(void)burstn;
	unsigned p = !(generic->cmp_phase & 2);
	if (generic->scanline >= generic->viewport.y &&
	    generic->scanline < (generic->viewport.y + generic->viewport.h)) {
		data += generic->viewport.x;
		LOCK_SURFACE(generic);
		for (int i = generic->viewport.w >> 2; i; i--) {
			Pixel p0, p1, p2, p3;
			uint8_t c0 = *data;
			uint8_t c2 = *(data + 2);
			if (generic->cmp.is_black_or_white[c0] && generic->cmp.is_black_or_white[c2]) {
				unsigned aindex = (generic->cmp.is_black_or_white[c0] << 1) | (generic->cmp.is_black_or_white[c2] & 1);
				p0 = p1 = p2 = p3 = generic->cmp.cc_2bit[p][aindex & 3];
			} else {
				uint8_t c1 = *(data+1);
				uint8_t c3 = *(data+3);
				p0 = generic->cmp.palette[c0];
				p1 = generic->cmp.palette[c1];
				p2 = generic->cmp.palette[c2];
				p3 = generic->cmp.palette[c3];
			}
			data += 4;
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

static void render_ccr_5bit(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct vo_generic_interface *generic = sptr;
	(void)burstn;
	unsigned p = !(generic->cmp_phase & 2);
	if (generic->scanline >= generic->viewport.y &&
	    generic->scanline < (generic->viewport.y + generic->viewport.h)) {
		unsigned ibwcount = 0;
		unsigned aindex = 0;
		data += generic->viewport.x;
		uint8_t ibw0 = generic->cmp.is_black_or_white[*(data-6)];
		uint8_t ibw1 = generic->cmp.is_black_or_white[*(data-2)];
		if (ibw0 && ibw1) {
			ibwcount = 7;
			aindex = (ibw0 & 1) ? 14 : 0;
			aindex |= (ibw1 & 1) ? 1 : 0;
		}
		LOCK_SURFACE(generic);
		for (int i = generic->viewport.w >> 2; i; i--) {
			Pixel p0, p1, p2, p3;

			uint8_t ibw2 = generic->cmp.is_black_or_white[*(data+2)];
			uint8_t ibw4 = generic->cmp.is_black_or_white[*(data+4)];
			uint8_t ibw6 = generic->cmp.is_black_or_white[*(data+6)];

			ibwcount = ((ibwcount << 1) | (ibw2 >> 1)) & 7;
			aindex = ((aindex << 1) | (ibw4 & 1));
			if (ibwcount == 7) {
				p0 = p1 = generic->cmp.cc_5bit[p][aindex & 31];
			} else {
				uint8_t c0 = *data;
				uint8_t c1 = *(data+1);
				p0 = generic->cmp.palette[c0];
				p1 = generic->cmp.palette[c1];
			}

			ibwcount = ((ibwcount << 1) | (ibw4 >> 1)) & 7;
			aindex = ((aindex << 1) | (ibw6 & 1));
			if (ibwcount == 7) {
				p2 = p3 = generic->cmp.cc_5bit[!p][aindex & 31];
			} else {
				uint8_t c2 = *(data+2);
				uint8_t c3 = *(data+3);
				p2 = generic->cmp.palette[c2];
				p3 = generic->cmp.palette[c3];
			}

			data += 4;
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

static void render_ntsc(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct vo_generic_interface *generic = sptr;
	if (generic->scanline < generic->viewport.y ||
	    generic->scanline >= (generic->viewport.y + generic->viewport.h)) {
		generic->scanline++;
		return;
	}
	generic->scanline++;

	struct ntsc_burst *burst = generic->ntsc_burst[burstn];

	// Encode NTSC
	const uint8_t *src = data + generic->viewport.x - 3;
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
		int_xyz rgb = ntsc_decode(burst, src++);
		// 40 is a reasonable value for brightness
		// TODO: make this adjustable
		int R = generic->ntsc_ungamma[int_clamp_u8(rgb.x)];
		int G = generic->ntsc_ungamma[int_clamp_u8(rgb.y)];
		int B = generic->ntsc_ungamma[int_clamp_u8(rgb.z)];
		*(generic->pixel) = MAPCOLOUR(generic, R, G, B);
		generic->pixel += XSTEP;
	}
	UNLOCK_SURFACE(generic);
	generic->pixel += NEXTLINE;
}
