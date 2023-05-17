/** \file
 *
 *  \brief Video renderers.
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
 *  This file contains generic scanline rendering routines.  It defines a
 *  variety of renderers by type, and various functions that can be exposed
 *  through a video interface.
 */

#include "top-config.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

#include "delegate.h"
#include "intfuncs.h"
#include "pl-endian.h"

#include "colourspace.h"
#include "filter.h"
#include "ntsc.h"
#include "vo_render.h"
#include "xroar.h"

#define MAX_FILTER_ORDER (15)

static const struct {
	const int tmax;     // number of samples at F(s)
	const int ncycles;  // number of cycles at F(sc)
} f_ratios[NUM_VO_RENDER_FS][NUM_VO_RENDER_FSC] = {
	// F(s) = 14.31818 MHz (NTSC, early Dragons)
	{
		{ .ncycles = 61, .tmax = 197 },  // F(sc) = 4.43361875 MHz (PAL)
		{ .ncycles =  1, .tmax =   4 },  // F(sc) = 3.579545 MHz (NTSC)
	},
	// F(s) = 14.218 MHz (later Dragons)
	{
		{ .ncycles = 29, .tmax =  93 },  // F(sc) = 4.43361875 MHz (PAL)
		{ .ncycles = 36, .tmax = 143 },  // F(sc) = 3.579545 MHz (NTSC)
	},
	// F(s) = 14.23753 MHz (PAL CoCos)
	{
		{ .ncycles = 71, .tmax = 228 },  // F(sc) = 4.43361875 MHz (PAL)
		{ .ncycles = 44, .tmax = 174 },  // F(sc) = 3.579545 MHz (NTSC)
	},
};

// Used to calculate filters

static const double vo_render_fs_mhz[NUM_VO_RENDER_FS] = {
        14.31818, 14.218, 14.23753
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define VR_PTYPE uint8_t
#define VR_SUFFIX uint8
#include "vo_render_tmpl.c"
#undef VR_PTYPE
#undef VR_SUFFIX

#define VR_PTYPE uint16_t
#define VR_SUFFIX uint16
#include "vo_render_tmpl.c"
#undef VR_PTYPE
#undef VR_SUFFIX

#define VR_PTYPE uint32_t
#define VR_SUFFIX uint32
#include "vo_render_tmpl.c"
#undef VR_PTYPE
#undef VR_SUFFIX

static uint32_t map_rgba8(struct vo_render *vr, int R, int G, int B);
static uint32_t map_argb8(struct vo_render *vr, int R, int G, int B);
static uint32_t map_bgra8(struct vo_render *vr, int R, int G, int B);
static uint32_t map_abgr8(struct vo_render *vr, int R, int G, int B);
static uint16_t map_rgba4(struct vo_render *vr, int R, int G, int B);
static uint16_t map_rgb565(struct vo_render *vr, int R, int G, int B);

static void render_rgba8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_argb8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_bgra8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_abgr8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_rgba4(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_rgb565(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void update_gamma_table(struct vo_render *vr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create a new renderer for the specified pixel format

struct vo_render *vo_render_new(int fmt) {
	struct vo_render *vr = NULL;
	switch (fmt) {
	case VO_RENDER_FMT_RGBA8:
		vr = renderer_new_uint32(map_rgba8);
		vr->render_rgb = render_rgba8;
		break;

	case VO_RENDER_FMT_ARGB8:
		vr = renderer_new_uint32(map_argb8);
		vr->render_rgb = render_argb8;
		break;

	case VO_RENDER_FMT_BGRA8:
		vr = renderer_new_uint32(map_bgra8);
		vr->render_rgb = render_bgra8;
		break;

	case VO_RENDER_FMT_ABGR8:
		vr = renderer_new_uint32(map_abgr8);
		vr->render_rgb = render_abgr8;
		break;

	case VO_RENDER_FMT_RGBA4:
		vr = renderer_new_uint16(map_rgba4);
		vr->render_rgb = render_rgba4;
		break;

	case VO_RENDER_FMT_RGB565:
		vr = renderer_new_uint16(map_rgb565);
		vr->render_rgb = render_rgb565;
		break;

	default:
		break;
	}

	if (!vr)
		return NULL;

	// Sensible defaults
	vo_render_set_cmp_fs(vr, 1, VO_RENDER_FS_14_31818);
	vo_render_set_cmp_fsc(vr, 1, VO_RENDER_FSC_4_43361875);
	vo_render_set_cmp_system(vr, 1, VO_RENDER_SYSTEM_PAL_I);

	vr->cmp.ntsc_palette = ntsc_palette_new();
	vr->cmp.cha_phase = M_PI/2.;  // default 90°
	vr->viewport.new_x = 190;
	vr->viewport.new_y = 14;
	vr->viewport.x = 190;
	vr->viewport.y = 14;
	vr->viewport.w = 640;
	vr->viewport.h = 240;
	vr->t = 0;
	vr->tmax = 4;
	vr->brightness = 50;
	vr->contrast = 50;
	vr->hue = 0;
	vr->cmp.phase_offset = 2;

	// Populate LUTs
	for (int i = 0; i < 2; i++) {
		// 2-bit LUT NTSC cross-colour
		for (int j = 0; j < 4; j++) {
			vr->set_palette_entry(vr, VO_RENDER_PALETTE_CMP_2BIT, i*4+j, vo_cmp_lut_2bit[i][j][0], vo_cmp_lut_2bit[i][j][1], vo_cmp_lut_2bit[i][j][2]);
		}
		// 5-bit LUT NTSC cross-colour
		// TODO: generate this using available NTSC decoding
		for (int j = 0; j < 32; j++) {
			vr->set_palette_entry(vr, VO_RENDER_PALETTE_CMP_5BIT, i*32+j, vo_cmp_lut_5bit[i][j][0], vo_cmp_lut_5bit[i][j][1], vo_cmp_lut_5bit[i][j][2]);
		}
	}
	update_gamma_table(vr);

	return vr;
}

// Free renderer

void vo_render_free(struct vo_render *vr) {
	ntsc_palette_free(vr->cmp.ntsc_palette);
	for (unsigned i = 0; i < vr->cmp.nbursts; i++) {
		ntsc_burst_free(vr->cmp.ntsc_burst[i]);
	}
	free(vr->cmp.ntsc_burst);
	if (vr->cmp.mod.ufilter.coeff) {
		free(vr->cmp.mod.ufilter.coeff - MAX_FILTER_ORDER);
	}
	if (vr->cmp.mod.vfilter.coeff) {
		free(vr->cmp.mod.vfilter.coeff - MAX_FILTER_ORDER);
	}
	if (vr->cmp.demod.yfilter.coeff) {
		free(vr->cmp.demod.yfilter.coeff - MAX_FILTER_ORDER);
	}
	if (vr->cmp.demod.ufilter.coeff) {
		free(vr->cmp.demod.ufilter.coeff - MAX_FILTER_ORDER);
	}
	if (vr->cmp.demod.vfilter.coeff) {
		free(vr->cmp.demod.vfilter.coeff - MAX_FILTER_ORDER);
	}
	free(vr);
}

extern inline void vo_render_set_buffer(struct vo_render *vr, void *buffer);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Pack R,G,B into a particular pixel format

static uint32_t map_rgba8(struct vo_render *vr, int R, int G, int B) {
	(void)vr;
	return (R << 24) | (G << 16) | (B << 8) | 0xff;
}

static uint32_t map_argb8(struct vo_render *vr, int R, int G, int B) {
	(void)vr;
	return 0xff000000 | (R << 16) | (G << 8) | (B << 0);
}

static uint32_t map_bgra8(struct vo_render *vr, int R, int G, int B) {
	(void)vr;
	return (B << 24) | (G << 16) | (R << 8) | 0xff;
}

static uint32_t map_abgr8(struct vo_render *vr, int R, int G, int B) {
	(void)vr;
	return 0xff000000 | (B << 16) | (G << 8) | (R << 0);
}

static uint16_t map_rgba4(struct vo_render *vr, int R, int G, int B) {
	(void)vr;
	return ((R & 0xf0) << 8) | ((G & 0xf0) << 4) | (B & 0xf0) | 0x0f;
}

static uint16_t map_rgb565(struct vo_render *vr, int R, int G, int B) {
	(void)vr;
	return ((R & 0xf8) << 8) | ((G & 0xfc) << 3) | ((B & 0xf8) >> 3);
}

// Render a line of RGB data into a particular pixel format.  The calls to
// map_* (defined above) should get inlined, as they are usually trivial.

static void render_rgba8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint32_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_rgba8(vr, R, G, B);
	}
}

static void render_argb8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint32_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_argb8(vr, R, G, B);
	}
}

static void render_bgra8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint32_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_bgra8(vr, R, G, B);
	}
}

static void render_abgr8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint32_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_abgr8(vr, R, G, B);
	}
}

static void render_rgba4(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint16_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_rgba4(vr, R, G, B);
	}
}

static void render_rgb565(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint16_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_rgb565(vr, R, G, B);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Update a composite palette entry, applying brightness & contrast

static void update_cmp_palette(struct vo_render *vr, uint8_t c) {
	float y = vr->cmp.colour[c].y;
	float b_y = vr->cmp.colour[c].pb;
	float r_y = vr->cmp.colour[c].pr;
	float nb_y, nr_y;

	// Add to NTSC palette before we process it
	ntsc_palette_add_ybr(vr->cmp.ntsc_palette, c, y, b_y, r_y);

	double mu = vr->cmp.palette.uconv.umul * b_y + vr->cmp.palette.uconv.vmul * r_y;
	double mv = vr->cmp.palette.vconv.umul * b_y + vr->cmp.palette.vconv.vmul * r_y;
	vr->cmp.palette.y[c] = 656. * y * vr->cmp.palette.yconv;
	vr->cmp.palette.u[c] = 896. * mu;
	vr->cmp.palette.v[c] = 896. * mv;

	// Adjust according to chroma phase configuration
	nb_y = b_y - (r_y / tan(vr->cmp.cha_phase));
	nr_y = r_y / sin(vr->cmp.cha_phase);
	b_y = nb_y;
	r_y = nr_y;

	// Apply colour saturation
	float saturation = (float)vr->saturation / 50.;
	b_y *= saturation;
	r_y *= saturation;

	// Apply hue
	float hue = (2. * M_PI * (float)vr->hue) / 360.;
	nb_y = r_y * sin(hue) + b_y * cos(hue);
	nr_y = r_y * cos(hue) - b_y * sin(hue);
	b_y = nb_y;
	r_y = nr_y;

	// Convert to R'G'B'
	float r = y + r_y;
	float g = y - 0.114 * b_y - 0.299 * r_y;
	float b = y + b_y;

	// Apply brightness & contrast
	float brightness = (float)(vr->brightness - 50) / 50.;
	float contrast = (float)vr->contrast / 50.;
	r = (r * contrast) + brightness;
	g = (g * contrast) + brightness;
	b = (b * contrast) + brightness;

	// Convert to display colourspace
	float R, G, B;
	cs_mlaw(vr->cs, r, g, b, &R, &G, &B);
	cs_clamp(&R, &G, &B);

	// Track "black or white" for simple artefact renderers
	if (y > 0.85 && fabsf(b_y) < 0.10 && fabsf(r_y) < 0.10) {
		vr->cmp.is_black_or_white[c] = 3;
	} else if (y < 0.20 && fabsf(b_y) < 0.10 && fabsf(r_y) < 0.10) {
		vr->cmp.is_black_or_white[c] = 2;
	} else {
		vr->cmp.is_black_or_white[c] = 0;
	}

	// Update palette entry
	int Ri = (int)(R * 255.);
	int Gi = (int)(G * 255.);
	int Bi = (int)(B * 255.);
	vr->set_palette_entry(vr, VO_RENDER_PALETTE_CMP, c, Ri, Gi, Bi);
}

// Update an RGB palette entry, applying brightness & contrast

static void update_rgb_palette(struct vo_render *vr, uint8_t c) {
	float r = vr->rgb.colour[c].r;
	float g = vr->rgb.colour[c].g;
	float b = vr->rgb.colour[c].b;

	// Apply brightness & contrast
	float brightness = (float)(vr->brightness - 50) / 50.;
	float contrast = (float)vr->contrast / 50.;
	r = (r * contrast) + brightness;
	g = (g * contrast) + brightness;
	b = (b * contrast) + brightness;

	// Convert to display colourspace
	float R, G, B;
	cs_mlaw(vr->cs, r, g, b, &R, &G, &B);
	cs_clamp(&R, &G, &B);

	// Update palette entry
	int Ri = (int)(R * 255.);
	int Gi = (int)(G * 255.);
	int Bi = (int)(B * 255.);
	vr->set_palette_entry(vr, VO_RENDER_PALETTE_RGB, c, Ri, Gi, Bi);
}

// Update gamma LUT

static void update_gamma_table(struct vo_render *vr) {
	// Tweak default brightness/contrast a little
	float brightness = (float)(vr->brightness + 1 - 50) / 50.;
	float contrast = (float)(vr->contrast + 11) / 50.;
	for (int j = 0; j < 256; j++) {
		float c = j / 255.0;
		c *= contrast;
		c += brightness;
		float C = cs_mlaw_1(vr->cs, c);
		vr->ungamma[j] = int_clamp_u8((int)(C * 255.0));
	}
}

// Generate encode and decode tables for indexed burst phase offset
//
// Lead/lag is incorporated into the encode tables, hue control into the decode
// tables.
//
// PAL doesn't need a hue control, but to provide the function anyway, we need
// to offset positively for V on one scanline and negatively on the next.

static void update_cmp_burst(struct vo_render *vr, unsigned burstn) {
	struct vo_render_burst *burst = &vr->cmp.burst[burstn];
	unsigned tmax = f_ratios[vr->cmp.fs][vr->cmp.fsc].tmax;
	unsigned ncycles = f_ratios[vr->cmp.fs][vr->cmp.fsc].ncycles;
	double wratio = 2. * M_PI * (double)ncycles / (double)tmax;
	double hue = (2. * M_PI * (double)vr->hue) / 360.;
	const double uv45 = (2. * M_PI * 45.) / 360.;
	for (unsigned t = 0; t < tmax; t++) {
		double a = wratio * (double)t;
		if (vr->cmp.system == VO_RENDER_SYSTEM_NTSC) {
			burst->mod.u[t] =            512. * sin(a);
			burst->mod.v[0][t] =         512. * sin(a + vr->cmp.cha_phase);
			burst->mod.v[1][t] =         512. * sin(a + vr->cmp.cha_phase);
			burst->demod.u[t] =     2. * 512. * sin(a - burst->phase_offset + hue);
			burst->demod.v[0][t] =  2. * 512. * cos(a - burst->phase_offset + hue);
			burst->demod.v[1][t] =  2. * 512. * cos(a - burst->phase_offset + hue);
		} else {
			a -= uv45;
			burst->mod.u[t] =            512. * sin(a);
			burst->mod.v[0][t] =         512. * sin(a + vr->cmp.cha_phase);
			burst->mod.v[1][t] =        -512. * sin(a + vr->cmp.cha_phase);
			burst->demod.u[t] =     2. * 512. * sin(a - burst->phase_offset + hue);
			burst->demod.v[0][t] =  2. * 512. * cos(a - burst->phase_offset + hue);
			burst->demod.v[1][t] = -2. * 512. * cos(a - burst->phase_offset - hue);
		}
	}
}

static void set_lp_filter(struct vo_render_filter *f, double fc, int order) {
	if (order < 1) {
		// order=0 flags filter as "not used"
		f->order = 0;
		return;
	}
	if (!f->coeff) {
		int ntaps = (MAX_FILTER_ORDER * 2) + 1;
		int *coeff = xmalloc(ntaps * sizeof(int));
		f->coeff = coeff + MAX_FILTER_ORDER;
	}
	if (order > MAX_FILTER_ORDER) {
		order = MAX_FILTER_ORDER;
	}
	struct filter_fir *src = filter_fir_lp_create(FILTER_WINDOW_BLACKMAN, fc, order);
	for (int ft = -MAX_FILTER_ORDER; ft <= MAX_FILTER_ORDER; ft++) {
		if (ft >= -order && ft <= order) {
			f->coeff[ft] = 32768. * src->taps[ft+order];
		} else {
			f->coeff[ft] = 0;
		}
	}
	filter_fir_free(src);
	f->order = order;
}

static void update_cmp_system(struct vo_render *vr) {
	vr->tmax = f_ratios[vr->cmp.fs][vr->cmp.fsc].tmax;
	assert(vr->tmax <= VO_RENDER_MAX_T);  // sanity check
	vr->t = 0;

	double fs_mhz = vo_render_fs_mhz[vr->cmp.fs];

	switch (vr->cmp.system) {
	case VO_RENDER_SYSTEM_NTSC:
	case VO_RENDER_SYSTEM_PAL_M:
		vr->cmp.palette.yconv = 0.591;
		vr->cmp.palette.uconv.umul = 0.504; vr->cmp.palette.uconv.vmul = 0.000;
		vr->cmp.palette.vconv.umul = 0.000; vr->cmp.palette.vconv.vmul = 0.711;

		vr->cmp.demod.ulimit.lower = -244; vr->cmp.demod.ulimit.upper = 244;
		vr->cmp.demod.vlimit.lower = -319; vr->cmp.demod.vlimit.upper = 319;
		vr->cmp.demod.rconv.umul =  0.000*512.; vr->cmp.demod.rconv.vmul =  1.140*512.;
		vr->cmp.demod.gconv.umul = -0.396*512.; vr->cmp.demod.gconv.vmul = -0.581*512.;
		vr->cmp.demod.bconv.umul =  2.029*512.; vr->cmp.demod.bconv.vmul =  0.000*512.;

		set_lp_filter(&vr->cmp.mod.ufilter, 1.3, 6);
		set_lp_filter(&vr->cmp.mod.vfilter, 0.5, 6);
		set_lp_filter(&vr->cmp.demod.yfilter, 2.1/fs_mhz, 10);
		set_lp_filter(&vr->cmp.demod.ufilter, 1.3/fs_mhz, 6);
		set_lp_filter(&vr->cmp.demod.vfilter, 0.5/fs_mhz, 6);
		vr->cmp.average_chroma = 0;
		break;

	default:
		vr->cmp.palette.yconv = 0.625;
		vr->cmp.palette.uconv.umul = 0.533; vr->cmp.palette.uconv.vmul = 0.000;
		vr->cmp.palette.vconv.umul = 0.000; vr->cmp.palette.vconv.vmul = 0.752;

		vr->cmp.demod.ulimit.lower = -239; vr->cmp.demod.ulimit.upper = 239;
		vr->cmp.demod.vlimit.lower = -337; vr->cmp.demod.vlimit.upper = 337;
		vr->cmp.demod.rconv.umul =  0.000*512.; vr->cmp.demod.rconv.vmul =  1.140*512.;
		vr->cmp.demod.gconv.umul = -0.396*512.; vr->cmp.demod.gconv.vmul = -0.581*512.;
		vr->cmp.demod.bconv.umul =  2.029*512.; vr->cmp.demod.bconv.vmul =  0.000*512.;

		set_lp_filter(&vr->cmp.mod.ufilter, 1.3/fs_mhz, 6);
		set_lp_filter(&vr->cmp.mod.vfilter, 1.3/fs_mhz, 6);
		set_lp_filter(&vr->cmp.demod.yfilter, 3.0/fs_mhz, 10);
		set_lp_filter(&vr->cmp.demod.ufilter, 1.3/fs_mhz, 6);
		set_lp_filter(&vr->cmp.demod.vfilter, 1.3/fs_mhz, 6);
		vr->cmp.average_chroma = 1;
		break;
	}

	switch (vr->cmp.system) {
	case VO_RENDER_SYSTEM_PAL_I:
		vr->cs = cs_profile_by_name("pal");
		break;

	default:
		// PAL-M displays are closer to NTSC
		vr->cs = cs_profile_by_name("ntsc");
		break;
	}

	vr->cmp.mod.corder = (vr->cmp.mod.ufilter.order > vr->cmp.mod.vfilter.order) ?
	                     vr->cmp.mod.ufilter.order : vr->cmp.mod.vfilter.order;
	vr->cmp.demod.corder = (vr->cmp.demod.ufilter.order > vr->cmp.demod.vfilter.order) ?
	                       vr->cmp.demod.ufilter.order : vr->cmp.demod.vfilter.order;

	for (unsigned i = 0; i < vr->cmp.nbursts; i++) {
		update_cmp_burst(vr, i);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by UI to adjust viewing parameters

// Set brightness
//     int brightness;  // 0-100

void vo_render_set_brightness(void *sptr, int value) {
	struct vo_render *vr = sptr;
	if (value < 0) value = 0;
	if (value > 100) value = 100;
	vr->brightness = value;
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
		update_rgb_palette(vr, c);
	}
	update_gamma_table(vr);
	if (xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_brightness, value, NULL);
	}
}

// Set contrast
//     int contrast;  // 0-100

void vo_render_set_contrast(void *sptr, int value) {
	struct vo_render *vr = sptr;
	if (value < 0) value = 0;
	if (value > 100) value = 100;
	vr->contrast = value;
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
		update_rgb_palette(vr, c);
	}
	update_gamma_table(vr);
	if (xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_contrast, value, NULL);
	}
}

// Set colour saturation
//     int saturation;  // 0-100

void vo_render_set_saturation(void *sptr, int value) {
	struct vo_render *vr = sptr;
	if (value < 0) value = 0;
	if (value > 100) value = 100;
	vr->saturation = value;
	vr->cmp.demod.saturation = (int)(((double)vr->saturation * 512.) / 100.);
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
	}
	if (xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_saturation, value, NULL);
	}
}

// Set hue
//     int hue;  // -179 to +180

void vo_render_set_hue(void *sptr, int value) {
	struct vo_render *vr = sptr;
	value = ((value + 179) % 360) - 179;
	vr->hue = value;
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
	}
	for (unsigned i = 0; i < vr->cmp.nbursts; i++) {
		update_cmp_burst(vr, i);
	}
	if (xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_hue, value, NULL);
	}
}

// Set cross-colour phase
//     int phase;  // VO_CMP_PHASE_*

void vo_render_set_cmp_phase(void *sptr, int value) {
	struct vo_render *vr = sptr;
	vr->cmp.phase = value ^ vr->cmp.phase_offset;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by machine to configure video output

// Configure active area (used to centre display)
//     int x, y;  // top-left of active area
//     int w, h;  // size of active area

void vo_render_set_active_area(void *sptr, int x, int y, int w, int h) {
	struct vo_render *vr = sptr;
	int xoff = x - (640 - w) / 2;
	int yoff = y - (240 - h) / 2;
	vr->viewport.new_x = xoff;
	vr->viewport.new_y = yoff;
}

// Set sampling frequency (equal to pixel rate) to one of VO_RENDER_FS_*

void vo_render_set_cmp_fs(struct vo_render *vr, _Bool notify, int fs) {
	if (fs < 0 || fs >= NUM_VO_RENDER_FS) {
		fs = VO_RENDER_FS_14_31818;
	}
	vr->cmp.fs = fs;
	update_cmp_system(vr);
	if (notify && xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_cmp_fs, fs, NULL);
	}
}

// Set chroma subcarrier frequency to one of VO_RENDER_FSC_*

void vo_render_set_cmp_fsc(struct vo_render *vr, _Bool notify, int fsc) {
	if (fsc < 0 || fsc >= NUM_VO_RENDER_FSC) {
		fsc = VO_RENDER_FSC_4_43361875;
	}
	vr->cmp.fsc = fsc;
	update_cmp_system(vr);
	if (notify && xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_cmp_fsc, fsc, NULL);
	}
}

// Set colour system to one of VO_RENDER_SYSTEM_*

void vo_render_set_cmp_system(struct vo_render *vr, _Bool notify, int system) {
	if (system < 0 || system >= NUM_VO_RENDER_SYSTEM) {
		system = VO_RENDER_SYSTEM_PAL_I;
	}
	vr->cmp.system = system;
	update_cmp_system(vr);
	if (notify && xroar_ui_interface) {
		DELEGATE_CALL(xroar_ui_interface->update_state, ui_tag_cmp_system, system, NULL);
	}
}

// Set how the chroma components relate to each other (in degrees)
//     float chb_phase;  // ignored TODO: remove argument
//     float cha_phase;  // øA phase, default 90°

void vo_render_set_cmp_lead_lag(void *sptr, float chb_phase, float cha_phase) {
	struct vo_render *vr = sptr;
	(void)chb_phase;
	vr->cmp.cha_phase = (cha_phase * 2. * M_PI) / 360.;
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
	}
}

// Add palette entry to composite palette as Y', Pb, Pr

void vo_render_set_cmp_palette(void *sptr, uint8_t c, float y, float pb, float pr) {
	struct vo_render *vr = sptr;
	vr->cmp.colour[c].y = y;
	vr->cmp.colour[c].pb = pb;
	vr->cmp.colour[c].pr = pr;
	update_cmp_palette(vr, c);
}

// Add palette entry to RGB palette as R', G', B'

void vo_render_set_rgb_palette(void *sptr, uint8_t c, float r, float g, float b) {
	struct vo_render *vr = sptr;
        vr->rgb.colour[c].r = r;
        vr->rgb.colour[c].g = g;
        vr->rgb.colour[c].b = b;
        update_rgb_palette(vr, c);
}

// Set a burst phase
//     unsigned burstn;  // burst index
//     int offset;       // in degrees

void vo_render_set_cmp_burst(void *sptr, unsigned burstn, int offset) {
	struct vo_render *vr = sptr;
	if (burstn >= vr->cmp.nbursts) {
		vr->cmp.nbursts = burstn + 1;
		vr->cmp.burst = xrealloc(vr->cmp.burst, vr->cmp.nbursts * sizeof(*(vr->cmp.burst)));
		vr->cmp.ntsc_burst = xrealloc(vr->cmp.ntsc_burst, vr->cmp.nbursts * sizeof(*(vr->cmp.ntsc_burst)));
	} else if (vr->cmp.ntsc_burst[burstn]) {
		ntsc_burst_free(vr->cmp.ntsc_burst[burstn]);
	}
	vr->cmp.ntsc_burst[burstn] = ntsc_burst_new(offset);
	vr->cmp.burst[burstn].phase_offset = (2. * M_PI * (double)offset) / 360.;
	update_cmp_burst(vr, burstn);
}

// Same, but in terms of B'-Y' and R'-Y', ie the voltages present on a motherboard

void vo_render_set_cmp_burst_br(void *sptr, unsigned burstn, float b_y, float r_y) {
	struct vo_render *vr = sptr;

	// Adjust according to chroma phase configuration
	double mu = b_y - (r_y / tan(vr->cmp.cha_phase));
	double mv = r_y / sin(vr->cmp.cha_phase);

	double a = atan2(mv, mu) - M_PI;
	int offset = (int)(((a * 360.) / (2 * M_PI)) + 360.5) % 360;

	vo_render_set_cmp_burst(sptr, burstn, offset);
}

// Set machine default cross-colour phase
//     int phase;  // VO_CMP_PHASE_*

void vo_render_set_cmp_phase_offset(void *sptr, int phase) {
	struct vo_render *vr = sptr;
	int p = vr->cmp.phase ^ vr->cmp.phase_offset;
	vr->cmp.phase_offset = phase ^ 2;
	vo_render_set_cmp_phase(vr, p);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by machine to render video

// Vertical sync

void vo_render_vsync(void *sptr) {
	struct vo_render *vr = sptr;
	if (!vr)
		return;
	vr->pixel = vr->buffer;
	vr->scanline = 0;
	vr->viewport.x = vr->viewport.new_x;
	vr->viewport.y = vr->viewport.new_y;
	vr->cmp.vswitch = !(vr->cmp.system == VO_RENDER_SYSTEM_NTSC || vr->cmp.phase == 0);
}

// NTSC partial composite video simulation
//
// Uses render_rgb(), so doesn't need to be duplicated per-type

void vo_render_cmp_partial(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct vo_render *vr = sptr;
	(void)npixels;

	if (!data ||
	    vr->scanline < vr->viewport.y ||
	    vr->scanline >= (vr->viewport.y + vr->viewport.h)) {
		vr->t = (vr->t + npixels) % vr->tmax;
		vr->scanline++;
		return;
	}

	struct ntsc_burst *burst = vr->cmp.ntsc_burst[burstn];

	// Encode NTSC
	uint8_t const *src = data + vr->viewport.x - 3;
	// Reuse a convenient buffer from other renderer
	uint8_t *ntsc_dest = (uint8_t *)vr->cmp.demod.fubuf[0];
	ntsc_phase = (vr->cmp.phase + vr->viewport.x) & 3;
	for (int i = vr->viewport.w + 6; i; i--) {
		unsigned c = *(src++);
		*(ntsc_dest++) = ntsc_encode_from_palette(vr->cmp.ntsc_palette, c);
	}

	// Decode into intermediate RGB buffer
	src = (uint8_t *)vr->cmp.demod.fubuf[0];
	int_xyz rgb[912];
	int_xyz *idest = rgb;
	ntsc_phase = ((vr->cmp.phase + vr->viewport.x) + 3) & 3;
	if (burstn) {
		for (int i = vr->viewport.w; i; i--) {
			*(idest++) = ntsc_decode(burst, src++);
		}
	} else {
		for (int i = vr->viewport.w; i; i--) {
			*(idest++) = ntsc_decode_mono(src++);
		}
	}

	// Render from intermediate RGB buffer
	vr->render_rgb(vr, rgb, vr->pixel, vr->viewport.w);
	vr->next_line(vr, npixels);
}

// Fully simulated composite video
//
// Uses render_rgb(), so doesn't need to be duplicated per-type

void vo_render_cmp_simulated(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct vo_render *vr = sptr;

	if (!data ||
	    vr->scanline < vr->viewport.y ||
	    vr->scanline >= (vr->viewport.y + vr->viewport.h)) {
		vr->t = (vr->t + npixels) % vr->tmax;
		vr->scanline++;
		return;
	}

	// Temporary buffers
	int mbuf[1024];  // Y' + U sin(ωt) + V cos(ωt), U/V optionally lowpassed
	int ubuf[1024];  // mbuf * 2 sin(ωt) (lowpass to recover U)
	int vbuf[1024];  // mbuf * 2 cos(ωt) (lowpass to recover V)

	struct vo_render_burst *burst = &vr->cmp.burst[burstn];
	unsigned tmax = vr->tmax;
	unsigned t = (vr->t + vr->cmp.phase_offset) % tmax;

	int vswitch = vr->cmp.vswitch;
	if (vr->cmp.average_chroma)
		vr->cmp.vswitch = !vswitch;

	// Optionally apply lowpass filters to U and V.  Modulate results.
	for (unsigned i = MAX_FILTER_ORDER; i < npixels - MAX_FILTER_ORDER; i++) {
		int c = data[i];
		int py = vr->cmp.palette.y[c];

		int fu, fv;
		if (vr->cmp.mod.corder) {
			fu = fv = 0;
			for (int ft = -vr->cmp.mod.corder; ft <= vr->cmp.mod.corder; ft++) {
				int ct = data[i+ft];
				fu += vr->cmp.palette.u[ct] * vr->cmp.mod.ufilter.coeff[ft];
				fv += vr->cmp.palette.v[ct] * vr->cmp.mod.vfilter.coeff[ft];
			}
			fu >>= 15;
			fv >>= 15;
		} else {
			fu = vr->cmp.palette.u[c];
			fv = vr->cmp.palette.v[c];
		}

		int fu_sin_wt = (fu * burst->mod.u[(i+t) % tmax]) >> 9;
		int fv_cos_wt = (fv * burst->mod.v[vswitch][(i+t) % tmax]) >> 9;

		mbuf[i] = py + fu_sin_wt + fv_cos_wt;

		// Multiply results by 2sin(wt)/2cos(wt), preempting
		// demodulation:
		if (burstn) {
			ubuf[i] = (mbuf[i] * burst->demod.u[(i+t) % tmax]) >> 9;
			vbuf[i] = (mbuf[i] * burst->demod.v[vswitch][(i+t) % tmax]) >> 9;
		}
	}

	int *fubuf0 = vr->cmp.demod.fubuf[vswitch];
	int *fvbuf0 = vr->cmp.demod.fvbuf[vswitch];
	int *fubuf1 = vr->cmp.demod.fubuf[vr->cmp.vswitch];
	int *fvbuf1 = vr->cmp.demod.fvbuf[vr->cmp.vswitch];

	int_xyz rgb[1024];

	for (unsigned i = MAX_FILTER_ORDER; i < npixels - MAX_FILTER_ORDER; i++) {
		int fy = 0;
		int yorder = vr->cmp.demod.yfilter.order;
		for (int ft = -yorder; ft <= yorder; ft++) {
			fy += vr->cmp.demod.yfilter.coeff[ft] * mbuf[i+ft];
		}
		fy >>= (15-9);  // fy won't be multiplied by [rgb]_conv

		int fu0 = 0, fv0 = 0;
		if (burstn) {
			int corder = vr->cmp.demod.corder;
			for (int ft = -corder; ft <= corder; ft++) {
				fu0 += vr->cmp.demod.ufilter.coeff[ft] * ubuf[i+ft];
				fv0 += vr->cmp.demod.vfilter.coeff[ft] * vbuf[i+ft];
			}
			fu0 >>= 15;
			fv0 >>= 15;
		}
		fubuf0[i] = fu0;
		fvbuf0[i] = fv0;

		int fu1 = fubuf1[i];
		int fu = (fu0 + fu1) >> 1;
		int fv1 = fvbuf1[i];
		int fv = (fv0 + fv1) >> 1;

		// Apply saturation control
		int ru = (fu * vr->cmp.demod.saturation) >> 9;
                int rv = (fv * vr->cmp.demod.saturation) >> 9;

		// Limits on chroma values
		if (ru < vr->cmp.demod.ulimit.lower) ru = vr->cmp.demod.ulimit.lower;
		if (ru > vr->cmp.demod.ulimit.upper) ru = vr->cmp.demod.ulimit.upper;
		if (rv < vr->cmp.demod.vlimit.lower) rv = vr->cmp.demod.vlimit.lower;
		if (rv > vr->cmp.demod.vlimit.upper) rv = vr->cmp.demod.vlimit.upper;

		// Convert to R'G'B' in supplied output buffer
		rgb[i].x = (fy + ru*vr->cmp.demod.rconv.umul + rv*vr->cmp.demod.rconv.vmul) >> 10;
		rgb[i].y = (fy + ru*vr->cmp.demod.gconv.umul + rv*vr->cmp.demod.gconv.vmul) >> 10;
		rgb[i].z = (fy + ru*vr->cmp.demod.bconv.umul + rv*vr->cmp.demod.bconv.vmul) >> 10;
	}

	// Render from intermediate RGB buffer
	vr->render_rgb(vr, rgb + vr->viewport.x, vr->pixel, vr->viewport.w);
	vr->next_line(vr, npixels);
}
