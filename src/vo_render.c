/** \file
 *
 *  \brief Video renderers.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
#include <stdlib.h>

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

#include "delegate.h"
#include "intfuncs.h"
#include "pl-endian.h"
#include "xalloc.h"

#include "colourspace.h"
#include "filter.h"
#include "messenger.h"
#include "ntsc.h"
#include "vo_render.h"
#include "xroar.h"

#define MAX_FILTER_ORDER (15)

// Pixel formats supported

struct xconfig_enum vo_pixel_fmt_list[] = {
	{ XC_ENUM_INT("rgba8", VO_RENDER_FMT_RGBA8, "RGBA 8-8-8-8, byte order") },
	{ XC_ENUM_INT("argb8", VO_RENDER_FMT_ARGB8, "ARGB 8-8-8-8, byte order") },
	{ XC_ENUM_INT("bgra8", VO_RENDER_FMT_BGRA8, "BGRA 8-8-8-8, byte order") },
	{ XC_ENUM_INT("abgr8", VO_RENDER_FMT_ABGR8, "ABGR 8-8-8-8, byte order") },
	{ XC_ENUM_INT("rgb565", VO_RENDER_FMT_RGB565, "RGB 5-6-5") },
	{ XC_ENUM_INT("rgba4", VO_RENDER_FMT_RGBA4, "RGBA 4-4-4") },
	{ XC_ENUM_INT("rgba32", VO_RENDER_FMT_RGBA32, "RGBA 8-8-8-8, host 32-bit packing") },
	{ XC_ENUM_INT("argb32", VO_RENDER_FMT_ARGB32, "ARGB 8-8-8-8, host 32-bit packing") },
	{ XC_ENUM_INT("bgra32", VO_RENDER_FMT_BGRA32, "BGRA 8-8-8-8, host 32-bit packing") },
	{ XC_ENUM_INT("abgr32", VO_RENDER_FMT_ABGR32, "ABGR 8-8-8-8, host 32-bit packing") },
	{ XC_ENUM_END() }
};

// Pixel rates - used as sampling frequency when filtering

struct xconfig_enum vo_render_fs_list[] = {
	{ XC_ENUM_INT("ntsc", VO_RENDER_FS_14_31818, "14.31818 MHz") },
	{ XC_ENUM_INT("pal-dragon", VO_RENDER_FS_14_218, "14.218 MHz") },
	{ XC_ENUM_INT("pal-coco", VO_RENDER_FS_14_23753, "14.23753 MHz") },
	{ XC_ENUM_END() }
};

// Colour subcarrier frequencies

struct xconfig_enum vo_render_fsc_list[] = {
	{ XC_ENUM_INT("pal", VO_RENDER_FSC_4_43361875, "4.43361875 MHz") },
	{ XC_ENUM_INT("ntsc", VO_RENDER_FSC_3_579545, "3.579545 MHz") },
	{ XC_ENUM_END() }
};

// Colour systems

struct xconfig_enum vo_render_system_list[] = {
	{ XC_ENUM_INT("pal-i", VO_RENDER_SYSTEM_PAL_I, "PAL-I") },
	{ XC_ENUM_INT("pal-m", VO_RENDER_SYSTEM_PAL_M, "PAL-M") },
	{ XC_ENUM_INT("ntsc", VO_RENDER_SYSTEM_NTSC, "NTSC") },
	{ XC_ENUM_END() }
};

// Map (Fs, Fsc) to sets of ratios used to generate burst tables

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

// 2-bit cross-colour LUT

static const uint8_t cmp_lut_2bit[2][4][3] = {
	{
		{ 0x00, 0x00, 0x00 }, { 0x00, 0x80, 0xff },
		{ 0xff, 0x80, 0x00 }, { 0xff, 0xff, 0xff },
	}, {
		{ 0x00, 0x00, 0x00 }, { 0xff, 0x80, 0x00 },
		{ 0x00, 0x80, 0xff }, { 0xff, 0xff, 0xff }
	}
};

// 5-bit cross-colour LUT

static const uint8_t cmp_lut_5bit[2][32][3] = {
	{
		{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
		{ 0x00, 0x32, 0x78 }, { 0x00, 0x28, 0x00 },
		{ 0xff, 0x8c, 0x64 }, { 0xff, 0x8c, 0x64 },
		{ 0xff, 0xd2, 0xff }, { 0xff, 0xf0, 0xc8 },
		{ 0x00, 0x32, 0x78 }, { 0x00, 0x00, 0x3c },
		{ 0x00, 0x80, 0xff }, { 0x00, 0x80, 0xff },
		{ 0xd2, 0xff, 0xd2 }, { 0xff, 0xff, 0xff },
		{ 0x64, 0xf0, 0xff }, { 0xff, 0xff, 0xff },
		{ 0x3c, 0x00, 0x00 }, { 0x3c, 0x00, 0x00 },
		{ 0x00, 0x00, 0x00 }, { 0x00, 0x28, 0x00 },
		{ 0xff, 0x80, 0x00 }, { 0xff, 0x80, 0x00 },
		{ 0xff, 0xff, 0xff }, { 0xff, 0xf0, 0xc8 },
		{ 0x28, 0x00, 0x28 }, { 0x28, 0x00, 0x28 },
		{ 0x00, 0x80, 0xff }, { 0x00, 0x80, 0xff },
		{ 0xff, 0xf0, 0xc8 }, { 0xff, 0xf0, 0xc8 },
		{ 0xff, 0xff, 0xff }, { 0xff, 0xff, 0xff },
	}, {
		{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
		{ 0xb4, 0x3c, 0x1e }, { 0x28, 0x00, 0x28 },
		{ 0x46, 0xc8, 0xff }, { 0x46, 0xc8, 0xff },
		{ 0xd2, 0xff, 0xd2 }, { 0x64, 0xf0, 0xff },
		{ 0xb4, 0x3c, 0x1e }, { 0x3c, 0x00, 0x00 },
		{ 0xff, 0x80, 0x00 }, { 0xff, 0x80, 0x00 },
		{ 0xff, 0xd2, 0xff }, { 0xff, 0xff, 0xff },
		{ 0xff, 0xf0, 0xc8 }, { 0xff, 0xff, 0xff },
		{ 0x00, 0x00, 0x3c }, { 0x00, 0x00, 0x3c },
		{ 0x00, 0x00, 0x00 }, { 0x28, 0x00, 0x28 },
		{ 0x00, 0x80, 0xff }, { 0x00, 0x80, 0xff },
		{ 0xff, 0xff, 0xff }, { 0x64, 0xf0, 0xff },
		{ 0x00, 0x28, 0x00 }, { 0x00, 0x28, 0x00 },
		{ 0xff, 0x80, 0x00 }, { 0xff, 0x80, 0x00 },
		{ 0x64, 0xf0, 0xff }, { 0x64, 0xf0, 0xff },
		{ 0xff, 0xff, 0xff }, { 0xff, 0xff, 0xff },
	}
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

static uint32_t map_rgba8(int R, int G, int B);
static uint32_t map_argb8(int R, int G, int B);
static uint32_t map_bgra8(int R, int G, int B);
static uint32_t map_abgr8(int R, int G, int B);
static uint16_t map_rgba4(int R, int G, int B);
static uint16_t map_rgb565(int R, int G, int B);

static int_xyz unmap_rgba8(uint32_t s);
static int_xyz unmap_argb8(uint32_t s);
static int_xyz unmap_bgra8(uint32_t s);
static int_xyz unmap_abgr8(uint32_t s);
static int_xyz unmap_rgba4(uint16_t s);
static int_xyz unmap_rgb565(uint16_t s);

static void render_rgba8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_argb8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_bgra8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_abgr8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_rgba4(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_rgb565(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void update_cmp_system(struct vo_render *vr);
static void vr_ui_set_cmp_fs(void *, int tag, void *smsg);
static void vr_ui_set_cmp_fsc(void *, int tag, void *smsg);
static void vr_ui_set_cmp_system(void *, int tag, void *smsg);
static void vr_ui_set_cmp_colour_killer(void *, int tag, void *smsg);
static void vr_ui_set_ntsc_scaling(void *, int tag, void *smsg);
static void vr_ui_set_brightness(void *, int tag, void *smsg);
static void vr_ui_set_contrast(void *, int tag, void *smsg);
static void vr_ui_set_saturation(void *, int tag, void *smsg);
static void vr_ui_set_hue(void *, int tag, void *smsg);
static void update_gamma_table(struct vo_render *vr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create a new renderer for the specified pixel format

struct vo_render *vo_render_new(int fmt) {
	struct vo_render *vr = NULL;
	switch (fmt) {
	case VO_RENDER_FMT_RGBA8:
		vr = renderer_new_uint32(map_rgba8, unmap_rgba8);
		vr->render_rgb = render_rgba8;
		break;

	case VO_RENDER_FMT_ARGB8:
		vr = renderer_new_uint32(map_argb8, unmap_argb8);
		vr->render_rgb = render_argb8;
		break;

	case VO_RENDER_FMT_BGRA8:
		vr = renderer_new_uint32(map_bgra8, unmap_bgra8);
		vr->render_rgb = render_bgra8;
		break;

	case VO_RENDER_FMT_ABGR8:
		vr = renderer_new_uint32(map_abgr8, unmap_abgr8);
		vr->render_rgb = render_abgr8;
		break;

	case VO_RENDER_FMT_RGBA4:
		vr = renderer_new_uint16(map_rgba4, unmap_rgba4);
		vr->render_rgb = render_rgba4;
		break;

	case VO_RENDER_FMT_RGB565:
		vr = renderer_new_uint16(map_rgb565, unmap_rgb565);
		vr->render_rgb = render_rgb565;
		break;

	default:
		break;
	}

	if (!vr)
		return NULL;

	vr->msgr_client_id = messenger_client_register();
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_cmp_fs, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_cmp_fs, vr));
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_cmp_fsc, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_cmp_fsc, vr));
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_cmp_system, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_cmp_system, vr));
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_cmp_colour_killer, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_cmp_colour_killer, vr));
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_ntsc_scaling, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_ntsc_scaling, vr));
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_brightness, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_brightness, vr));
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_contrast, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_contrast, vr));
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_saturation, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_saturation, vr));
	ui_messenger_preempt_group(vr->msgr_client_id, ui_tag_hue, MESSENGER_NOTIFY_DELEGATE(vr_ui_set_hue, vr));

	// Sensible defaults
	vr->cmp.fs = VO_RENDER_FS_14_31818;
	vr->cmp.fsc = VO_RENDER_FSC_4_43361875;
	vr->cmp.system = VO_RENDER_SYSTEM_PAL_I;
	update_cmp_system(vr);

	vr->cmp.cha_phase = M_PI/2.;  // default 90°

	// Sensible viewport defaults.  Will be updated to centre the display
	// in short order.  Default X offset is enough to apply two filters
	// without having to worry about underflow.
	vr->viewport.x = 2 * MAX_FILTER_ORDER;
	vr->viewport.w = 640;
	vr->viewport.h = 240;
	vo_render_set_active_area(vr, 254, 63, 512, 512);

	vr->t = 0;
	vr->tmax = 4;
	vr->brightness = 50;
	vr->contrast = 50;
	vr->hue = 0;
	vr->cmp.phase_offset = 0;

	// Populate LUTs
	for (unsigned i = 0; i < 2; ++i) {
		// 2-bit LUT NTSC cross-colour
		for (unsigned j = 0; j < 4; ++j) {
			vr->set_palette_entry(vr, VO_RENDER_PALETTE_CMP_2BIT, i*4+j,
					      cmp_lut_2bit[i][j][0],
					      cmp_lut_2bit[i][j][1],
					      cmp_lut_2bit[i][j][2]);
		}
		// 5-bit LUT NTSC cross-colour
		// TODO: generate this using available NTSC decoding
		for (unsigned j = 0; j < 32; ++j) {
			vr->set_palette_entry(vr, VO_RENDER_PALETTE_CMP_5BIT, i*32+j,
					      cmp_lut_5bit[i][j][0],
					      cmp_lut_5bit[i][j][1],
					      cmp_lut_5bit[i][j][2]);
		}
	}
	update_gamma_table(vr);

	return vr;
}

// Free renderer

void vo_render_free(struct vo_render *vr) {
	messenger_client_unregister(vr->msgr_client_id);
	free(vr->cmp.burst);
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

static uint32_t map_rgba8(int R, int G, int B) {
	return (R << 24) | (G << 16) | (B << 8) | 0xff;
}

static uint32_t map_argb8(int R, int G, int B) {
	return 0xff000000 | (R << 16) | (G << 8) | (B << 0);
}

static uint32_t map_bgra8(int R, int G, int B) {
	return (B << 24) | (G << 16) | (R << 8) | 0xff;
}

static uint32_t map_abgr8(int R, int G, int B) {
	return 0xff000000 | (B << 16) | (G << 8) | (R << 0);
}

static uint16_t map_rgba4(int R, int G, int B) {
	return ((R & 0xf0) << 8) | ((G & 0xf0) << 4) | (B & 0xf0) | 0x0f;
}

static uint16_t map_rgb565(int R, int G, int B) {
	return ((R & 0xf8) << 8) | ((G & 0xfc) << 3) | ((B & 0xf8) >> 3);
}

static int_xyz unmap_rgba8(uint32_t s) {
	return (int_xyz){.x = (s >> 24) & 0xff, .y = (s >> 16) & 0xff, .z = (s >> 8) & 0xff};
}

static int_xyz unmap_argb8(uint32_t s) {
	return (int_xyz){.x = (s >> 16) & 0xff, .y = (s >> 8) & 0xff, .z = s & 0xff};
}

static int_xyz unmap_bgra8(uint32_t s) {
	return (int_xyz){.x = (s >> 8) & 0xff, .y = (s >> 16) & 0xff, .z = (s >> 24) & 0xff};
}

static int_xyz unmap_abgr8(uint32_t s) {
	return (int_xyz){.x = s & 0xff, .y = (s >> 8) & 0xff, .z = (s >> 16) & 0xff};
}

static int_xyz unmap_rgba4(uint16_t s) {
	return (int_xyz){.x = (s >> 8) & 0xf0, .y = (s >> 4) & 0xf0, .z = s & 0xf0};
}

static int_xyz unmap_rgb565(uint16_t s) {
	return (int_xyz){.x = (s >> 8) & 0xf8, .y = (s >> 3) & 0xfc, .z = (s >> 3) & 0xf8};
}

// Render a line of RGB data into a particular pixel format.  The calls to
// map_* (defined above) should get inlined, as they are usually trivial.

static void render_rgba8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint32_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_rgba8(R, G, B);
	}
}

static void render_argb8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint32_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_argb8(R, G, B);
	}
}

static void render_bgra8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint32_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_bgra8(R, G, B);
	}
}

static void render_abgr8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint32_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_abgr8(R, G, B);
	}
}

static void render_rgba4(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint16_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_rgba4(R, G, B);
	}
}

static void render_rgb565(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint16_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_rgb565(R, G, B);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Update a composite palette entry, applying brightness & contrast

static void update_cmp_palette(struct vo_render *vr, uint8_t c) {
	double y = vr->cmp.colour[c].y;
	double b_y = vr->cmp.colour[c].pb;
	double r_y = vr->cmp.colour[c].pr;
	double nb_y, nr_y;

	// Update in partial NTSC palette
	ntsc_palette_set_ybr(vr, c);

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
	if (y > 0.85 && fabs(b_y) < 0.10 && fabs(r_y) < 0.10) {
		vr->cmp.is_black_or_white[c] = 3;
	} else if (y < 0.25 && fabs(b_y) < 0.10 && fabs(r_y) < 0.10) {
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

static void update_cmp_burst(struct vo_render *vr, unsigned burstn) {
	struct vo_render_burst *burst = &vr->cmp.burst[burstn];

	unsigned tmax = f_ratios[vr->cmp.fs][vr->cmp.fsc].tmax;
	unsigned ncycles = f_ratios[vr->cmp.fs][vr->cmp.fsc].ncycles;
	double wratio = 2. * M_PI * (double)ncycles / (double)tmax;

	int moff_i = vr->cmp.phase + vr->cmp.phase_offset;
	double moff = (2. * M_PI * (double)moff_i) / 360.;
	double boff = (2. * M_PI * (double)burst->phase_offset) / 360.;
	double hue = (2. * M_PI * (double)vr->hue) / 360.;

	for (unsigned t = 0; t < tmax; t++) {
		double a = wratio * (double)t + moff;
		if (vr->cmp.system == VO_RENDER_SYSTEM_NTSC) {
			burst->mod.u[t] =            512. * sin(a);
			burst->mod.v[0][t] =         512. * sin(a + vr->cmp.cha_phase);
			burst->mod.v[1][t] =         512. * sin(a + vr->cmp.cha_phase);
			burst->demod.u[t] =     2. * 512. * sin(a - boff + hue);
			burst->demod.v[0][t] =  2. * 512. * cos(a - boff + hue);
			burst->demod.v[1][t] =  2. * 512. * cos(a - boff + hue);
		} else {
			// PAL doesn't need a hue control, but to provide the
			// function anyway, we need to offset positively for V
			// on one scanline and negatively on the next.
			burst->mod.u[t] =            512. * sin(a);
			burst->mod.v[0][t] =         512. * sin(a + vr->cmp.cha_phase);
			burst->mod.v[1][t] =        -512. * sin(a + vr->cmp.cha_phase);
			burst->demod.u[t] =     2. * 512. * sin(a - boff + hue);
			burst->demod.v[0][t] =  2. * 512. * cos(a - boff + hue);
			burst->demod.v[1][t] = -2. * 512. * cos(a + boff - hue);
		}
	}

	ntsc_burst_set(vr, burstn);
}

static void update_phase_offset(struct vo_render *vr) {
	for (unsigned i = 0; i < 256; i++) {
		ntsc_palette_set_ybr(vr, i);
	}
	for (unsigned i = 0; i < vr->cmp.nbursts; i++) {
		update_cmp_burst(vr, i);
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

		set_lp_filter(&vr->cmp.mod.ufilter, 0.0, 0);
		set_lp_filter(&vr->cmp.mod.vfilter, 0.0, 0);
		set_lp_filter(&vr->cmp.demod.yfilter, 2.1/fs_mhz, 11);
		set_lp_filter(&vr->cmp.demod.ufilter, 1.3/fs_mhz, 8);
		set_lp_filter(&vr->cmp.demod.vfilter, 1.3/fs_mhz, 8);
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
		break;
	}

	switch (vr->cmp.system) {
	case VO_RENDER_SYSTEM_NTSC:
		vr->cmp.average_chroma = 0;
		break;
	default:
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
	vr->cmp.demod.morder = (vr->cmp.demod.corder > vr->cmp.demod.yfilter.order) ?
	                       vr->cmp.demod.corder : vr->cmp.demod.yfilter.order;

	for (unsigned i = 0; i < vr->cmp.nbursts; i++) {
		update_cmp_burst(vr, i);
	}
}

// Update viewport offset based on viewport dimensions and active area

static void update_viewport(struct vo_render *vr) {
	vr->viewport.new_x = vr->active_area.x - (vr->viewport.w - vr->active_area.w) / 2;
	vr->viewport.new_y = vr->active_area.y - (vr->viewport.h - vr->active_area.h) / 2;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by UI to adjust viewing parameters

// Set viewport dimensions
//     int w, h;  // dimensions

void vo_render_set_viewport(struct vo_render *vr, int w, int h) {
	vr->viewport.w = w;
	vr->viewport.h = h;
	update_viewport(vr);
}

// Set 60Hz scaling
//     value = enabled

static void vr_ui_set_ntsc_scaling(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_ntsc_scaling);

	vr->ntsc_scaling = ui_msg_adjust_value_range(uimsg, vr->ntsc_scaling, 0, 0, 1,
						     UI_ADJUST_FLAG_CYCLE);
}

// Set brightness
//     value = brightness (0-100)

static void vr_ui_set_brightness(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_brightness);

	vr->brightness = ui_msg_adjust_value_range(uimsg, vr->brightness, 50, 0, 100, 0);
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
		update_rgb_palette(vr, c);
	}
	update_gamma_table(vr);
}

// Set contrast
//     value = contrast (0-100)

static void vr_ui_set_contrast(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_contrast);

	vr->contrast = ui_msg_adjust_value_range(uimsg, vr->contrast, 50, 0, 100, 0);
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
		update_rgb_palette(vr, c);
	}
	update_gamma_table(vr);
}

// Set colour saturation
//     value = saturation (0-100)

static void vr_ui_set_saturation(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_saturation);

	vr->saturation = ui_msg_adjust_value_range(uimsg, vr->saturation, 50, 0, 100, 0);
	vr->cmp.demod.saturation = (int)(((double)vr->saturation * 512.) / 100.);
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
	}
}

// Set hue
//     value = hue (-179 to +180)

static void vr_ui_set_hue(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_hue);

	vr->hue = ui_msg_adjust_value_range(uimsg, vr->hue, 0, -179, 180,
					    UI_ADJUST_FLAG_CYCLE);
	for (unsigned c = 0; c < 256; c++) {
		update_cmp_palette(vr, c);
	}
	for (unsigned i = 0; i < vr->cmp.nbursts; i++) {
		update_cmp_burst(vr, i);
	}
}

// Set cross-colour phase
//     int phase;  // in degrees

void vo_render_set_cmp_phase(void *sptr, int value) {
	struct vo_render *vr = sptr;
	vr->cmp.phase = value;
	update_phase_offset(vr);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by machine to configure video output

// Configure active area (used to centre display)
//     int x, y;  // top-left of active area
//     int w, h;  // size of active area

void vo_render_set_active_area(void *sptr, int x, int y, int w, int h) {
	struct vo_render *vr = sptr;
	vr->active_area.x = x;
	vr->active_area.y = y;
	vr->active_area.w = w;
	vr->active_area.h = h;
	update_viewport(vr);
}

// Set sampling frequency (equal to pixel rate)
//     value = VO_RENDER_FS_*

static void vr_ui_set_cmp_fs(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_cmp_fs);

	vr->cmp.fs = ui_msg_adjust_value_range(uimsg, vr->cmp.fs, VO_RENDER_FS_14_31818,
					       VO_RENDER_FS_14_31818, VO_RENDER_FS_14_23753, 0);
	update_cmp_system(vr);
}

// Set chroma subcarrier frequency
//     value = VO_RENDER_FSC_*

static void vr_ui_set_cmp_fsc(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_cmp_fsc);

	vr->cmp.fsc = ui_msg_adjust_value_range(uimsg, vr->cmp.fsc, VO_RENDER_FSC_4_43361875,
						VO_RENDER_FSC_4_43361875,
						VO_RENDER_FSC_3_579545, 0);
	update_cmp_system(vr);
}

// Set colour system
//     value = VO_RENDER_SYSTEM_*

static void vr_ui_set_cmp_system(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_cmp_system);

	vr->cmp.system = ui_msg_adjust_value_range(uimsg, vr->cmp.system,
						   VO_RENDER_SYSTEM_PAL_I,
						   VO_RENDER_SYSTEM_PAL_I,
						   VO_RENDER_SYSTEM_NTSC, 0);
	update_cmp_system(vr);
}

// Set whether colour decoding disabled when no colourburst indicated
//     value = enabled

static void vr_ui_set_cmp_colour_killer(void *sptr, int tag, void *smsg) {
	struct vo_render *vr = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_cmp_colour_killer);

	vr->cmp.colour_killer = ui_msg_adjust_value_range(uimsg, vr->cmp.colour_killer, 0,
							  0, 1, UI_ADJUST_FLAG_CYCLE);
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
		unsigned nbursts = burstn + 1;
		vr->cmp.burst = xrealloc(vr->cmp.burst, nbursts * sizeof(*(vr->cmp.burst)));
		for (unsigned i = vr->cmp.nbursts; i < nbursts; i++) {
			vr->cmp.burst[i] = (struct vo_render_burst){0};
		}
		vr->cmp.nbursts = nbursts;
	}
	vr->cmp.burst[burstn].phase_offset = offset;
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

// Set machine pixel to burst phase offset
//     int phase;  // in degrees

void vo_render_set_cmp_phase_offset(void *sptr, int offset) {
	struct vo_render *vr = sptr;
	vr->cmp.phase_offset = offset;
	update_phase_offset(vr);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Used by machine to render video

// Vertical sync

void vo_render_vsync(void *sptr) {
	struct vo_render *vr = sptr;
	if (!vr)
		return;
	vr->pixel = vr->buffer;

	_Bool is_60hz = vr->ntsc_scaling && (vr->scanline < 288);
	if (is_60hz != vr->is_60hz) {
		vr->is_60hz = is_60hz;
		DELEGATE_SAFE_CALL(vr->notify_frame_rate, is_60hz);
	}

	vr->scanline = 0;
	vr->viewport.x = vr->viewport.new_x;
	vr->viewport.y = vr->viewport.new_y;
	vr->cmp.vswitch = !(vr->cmp.system == VO_RENDER_SYSTEM_NTSC || vr->cmp.phase == 0);
}

// NTSC partial composite video simulation
//
// Uses render_rgb(), so doesn't need to be duplicated per-type.
//
// Time 't' not kept accurate, as scanlines are all aligned to chroma.

void vo_render_cmp_partial(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct vo_render *vr = sptr;
	(void)npixels;

	if (!data ||
	    vr->scanline < vr->viewport.y ||
	    vr->scanline >= (vr->viewport.y + vr->viewport.h)) {
		vr->scanline++;
		return;
	}

	if (!burstn && !vr->cmp.colour_killer)
		burstn = 1;

	struct ntsc_burst *burst = &vr->cmp.burst[burstn].ntsc_burst;
	const unsigned tmax = NTSC_NPHASES;

	// Encode NTSC
	// Reuse a convenient buffer from other renderer
	uint8_t *ntsc_dest = (uint8_t *)vr->cmp.demod.fubuf[0];
	struct ntsc_palette *np = &vr->cmp.ntsc_palette;
	for (int i = vr->viewport.x - 3; i < (vr->viewport.x + vr->viewport.w + 3); i++) {
		uint8_t c = data[i];
		*(ntsc_dest++) = np->byphase[i % tmax][c];
	}

	// Decode into intermediate RGB buffer
	uint8_t const *src = (uint8_t *)vr->cmp.demod.fubuf[0];
	int_xyz rgb[912];
	int_xyz *idest = rgb;
	if (burstn) {
		for (int i = vr->viewport.x; i < (vr->viewport.x + vr->viewport.w); i++) {
			*(idest++) = ntsc_decode(burst, src++, i);
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

	if (!burstn && !vr->cmp.colour_killer)
		burstn = 1;

	struct vo_render_burst *burst = &vr->cmp.burst[burstn];
	unsigned tmax = vr->tmax;
	unsigned t = vr->t % tmax;

	int vswitch = vr->cmp.vswitch;
	if (vr->cmp.average_chroma)
		vr->cmp.vswitch = !vswitch;

	// Optionally apply lowpass filters to U and V.  Modulate results.
	for (unsigned i = vr->viewport.x - vr->cmp.demod.morder; i < (unsigned)(vr->viewport.x + vr->viewport.w + vr->cmp.demod.morder); i++) {
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

	for (unsigned i = vr->viewport.x; i < (unsigned)(vr->viewport.x + vr->viewport.w); i++) {
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
