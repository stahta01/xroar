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

#include <math.h>
#include <stdint.h>

#include "intfuncs.h"
#include "pl-endian.h"

#include "colourspace.h"
#include "ntsc.h"
#include "vo_render.h"
#include "xroar.h"

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
static uint16_t map_argb4(struct vo_render *vr, int R, int G, int B);
static uint16_t map_rgb565(struct vo_render *vr, int R, int G, int B);

static void render_rgba8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_argb8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_bgra8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_abgr8(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_argb4(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);
static void render_rgb565(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void update_gamma_table(struct vo_render *vr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create a new renderer for the specified pixel format

struct vo_render *vo_render_new(int fmt) {
	struct vo_render *vr = NULL;
	switch (fmt) {
	case VO_RENDER_RGBA8:
		vr = renderer_new_uint32(map_rgba8);
		vr->render_rgb = render_rgba8;
		break;

	case VO_RENDER_ARGB8:
		vr = renderer_new_uint32(map_argb8);
		vr->render_rgb = render_argb8;
		break;

	case VO_RENDER_BGRA8:
		vr = renderer_new_uint32(map_bgra8);
		vr->render_rgb = render_bgra8;
		break;

	case VO_RENDER_ABGR8:
		vr = renderer_new_uint32(map_abgr8);
		vr->render_rgb = render_abgr8;
		break;

	case VO_RENDER_ARGB4:
		vr = renderer_new_uint16(map_argb4);
		vr->render_rgb = render_argb4;
		break;

	case VO_RENDER_RGB565:
		vr = renderer_new_uint16(map_rgb565);
		vr->render_rgb = render_rgb565;
		break;

	default:
		break;
	}

	if (!vr)
		return NULL;

	// Sensible defaults
	vr->cs = cs_profile_by_name("ntsc");
	vr->cmp.ntsc_palette = ntsc_palette_new();
	vr->viewport.new_x = 190;
	vr->viewport.new_y = 14;
	vr->viewport.x = 190;
	vr->viewport.y = 14;
	vr->viewport.w = 640;
	vr->viewport.h = 240;
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

static uint16_t map_argb4(struct vo_render *vr, int R, int G, int B) {
	(void)vr;
	return 0xf000 | ((R & 0xf0) << 4) | (G & 0xf0) | ((B & 0xf0) >> 4);
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

static void render_argb4(struct vo_render *vr, int_xyz *src, void *dest, unsigned npixels) {
	uint16_t *d = dest;
	for (; npixels; src++, npixels--) {
		int R = vr->ungamma[int_clamp_u8(src->x)];
		int G = vr->ungamma[int_clamp_u8(src->y)];
		int B = vr->ungamma[int_clamp_u8(src->z)];
		*(d++) = map_argb4(vr, R, G, B);
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

	// Add to NTSC palette before we process it
	ntsc_palette_add_ybr(vr->cmp.ntsc_palette, c, y, b_y, r_y);

	// Apply colour saturation
	float saturation = (float)vr->saturation / 50.;
	b_y *= saturation;
	r_y *= saturation;

	// Apply hue
	float w = 2. * M_PI;
	float hue = (w * (float)vr->hue) / 360.;
	float nb_y = r_y * sin(hue) + b_y * cos(hue);
	float nr_y = r_y * cos(hue) - b_y * sin(hue);
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
	if (r > 0.85 && g > 0.85 && b > 0.85) {
		vr->cmp.is_black_or_white[c] = 3;
	} else if (r < 0.20 && g < 0.20 && b < 0.20) {
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
//     int phase;        // in degrees

void vo_render_set_burst(void *sptr, unsigned burstn, int offset) {
	struct vo_render *vr = sptr;
	if (burstn >= vr->cmp.nbursts) {
		vr->cmp.nbursts = burstn + 1;
		vr->cmp.ntsc_burst = xrealloc(vr->cmp.ntsc_burst, vr->cmp.nbursts * sizeof(*(vr->cmp.ntsc_burst)));
	} else if (vr->cmp.ntsc_burst[burstn]) {
		ntsc_burst_free(vr->cmp.ntsc_burst[burstn]);
	}
	vr->cmp.ntsc_burst[burstn] = ntsc_burst_new(offset);
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
}

// NTSC composite video simulation
//
// Uses render_rgb(), so doesn't need to be duplicated per-type

void vo_render_cmp_ntsc(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct vo_render *vr = sptr;
	(void)npixels;

	if (!data ||
	    vr->scanline < vr->viewport.y ||
	    vr->scanline >= (vr->viewport.y + vr->viewport.h)) {
		vr->scanline++;
		return;
	}

	struct ntsc_burst *burst = vr->cmp.ntsc_burst[burstn];

	// Encode NTSC
	uint8_t const *src = data + vr->viewport.x - 3;
	uint8_t *ntsc_dest = vr->cmp.ntsc_buf;
	ntsc_phase = (vr->cmp.phase + vr->viewport.x) & 3;
	for (int i = vr->viewport.w + 6; i; i--) {
		unsigned c = *(src++);
		*(ntsc_dest++) = ntsc_encode_from_palette(vr->cmp.ntsc_palette, c);
	}

	// Decode into intermediate RGB buffer
	src = vr->cmp.ntsc_buf;
	int_xyz rgb_buf[912];
	int_xyz *idest = rgb_buf;
	ntsc_phase = ((vr->cmp.phase + vr->viewport.x) + 3) & 3;
	for (int i = vr->viewport.w; i; i--) {
		*(idest++) = ntsc_decode(burst, src++);
	}

	// Render from intermediate RGB buffer
	vr->render_rgb(vr, rgb_buf, vr->pixel, vr->viewport.w);
	vr->next_line(vr);
}
