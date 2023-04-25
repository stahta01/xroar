/** \file
 *
 *  \brief Video renderer.
 *
 *  \copyright Copyright 2023 Ciaran Anscomb
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
 *  Successfully initialising a video module returns a (struct vo_interface),
 *  which is used by various parts of XRoar to do different things:
 *
 *  - The UI may ask it to resize, toggle menubar, etc.
 *
 *  - Selecting a machine may define colour palettes and select how things are
 *    to be rendered.
 *
 *  - While running, the emulated machine will use it to render scanlines,
 *    indicate vertical sync, or just ask to refresh the screen.
 *
 *  Palette entries are specified either as YPbPr (Y scaled 0-1, Pb and Pr
 *  scaled ±0.5) or as RGB (each scaled 0-1).
 */

#ifndef XROAR_VO_RENDER_H_
#define XROAR_VO_RENDER_H_

#include <stdint.h>

#include "intfuncs.h"

// Pixel formats supported.  Note that the primary names here relate to how the
// values are logically packed into their underlying data type.  The
// VO_RENDER_xxxx32 aliases instead indicate the in-memory byte order, and
// differ between right- and wrong-endian platforms (this distinction borrowed
// from SDL).

enum {
	VO_RENDER_FMT_RGBA8,
	VO_RENDER_FMT_ARGB8,
	VO_RENDER_FMT_BGRA8,
	VO_RENDER_FMT_ABGR8,
	VO_RENDER_FMT_RGBA4,
	VO_RENDER_FMT_RGB565,

#if __BYTE_ORDER == __BIG_ENDIAN
	VO_RENDER_FMT_RGBA32 = VO_RENDER_FMT_RGBA8,
	VO_RENDER_FMT_ARGB32 = VO_RENDER_FMT_ARGB8,
	VO_RENDER_FMT_BGRA32 = VO_RENDER_FMT_BGRA8,
	VO_RENDER_FMT_ABGR32 = VO_RENDER_FMT_ABGR8,
#else
	VO_RENDER_FMT_RGBA32 = VO_RENDER_FMT_ABGR8,
	VO_RENDER_FMT_ARGB32 = VO_RENDER_FMT_BGRA8,
	VO_RENDER_FMT_BGRA32 = VO_RENDER_FMT_ARGB8,
	VO_RENDER_FMT_ABGR32 = VO_RENDER_FMT_RGBA8,
#endif
};

// For configuring per-renderer colour palette entries
enum {
	VO_RENDER_PALETTE_CMP,
	VO_RENDER_PALETTE_CMP_2BIT,
	VO_RENDER_PALETTE_CMP_5BIT,
	VO_RENDER_PALETTE_RGB,
};

struct vo_render {
	struct {
		// Record values for recalculation
		struct {
			float y, pb, pr;
		} colour[256];

		// Cache testing if each colour is black or white
		uint8_t is_black_or_white[256];

		// Lead/lag of chroma components
		float chb_phase;  // default 0°
		float cha_phase;  // default 90° = π/2

		// And a full NTSC decode table
		struct ntsc_palette *ntsc_palette;

		// NTSC bursts
		unsigned nbursts;
		struct ntsc_burst **ntsc_burst;

		// Buffer for NTSC line encode
		uint8_t ntsc_buf[912];

		// Machine defined default cross-colour phase
		int phase_offset;

		// User configured cross-colour phase (modifies above)
		int phase;
	} cmp;

	struct {
		// Record values for recalculation
		struct {
			float r, g, b;
		} colour[256];
	} rgb;

	struct {
		int new_x, new_y;
		int x, y;
		int w, h;
	} viewport;

	// Current time, measured in pixels
	unsigned t;

	// Maximum time 't', ie number of pixels that span an exact
	// multiple of chroma cycles
	unsigned tmax;

	// Colourspace definition
	struct cs_profile *cs;

	// Gamma LUT
	uint8_t ungamma[256];

	// Current scanline - compared against viewport
	int scanline;

	// Top-left of output buffer; where vo_render_vsync() will return pixel to
	void *buffer;

	// Current pixel pointer
	void *pixel;

	// Amount to advance pixel pointer each line
	int buffer_pitch;

	// Display adjustments
	int brightness;
	int contrast;
	int saturation;
	int hue;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Populated by type-specific renderer's init code, used internally.

	// Set type-specific renderer palette entry
	void (*set_palette_entry)(void *, int, int, int, int, int);

	// Alternatives for the vo module render_line delegate
	void (*render_cmp_palette)(void *, unsigned, unsigned, uint8_t const *);
	void (*render_rgb_palette)(void *, unsigned, unsigned, uint8_t const *);
	void (*render_cmp_2bit)(void *, unsigned, unsigned, uint8_t const *);
	void (*render_cmp_5bit)(void *, unsigned, unsigned, uint8_t const *);

	// Helper for render_line implementations that generate an intermediate
	// array of RGB values
	void (*render_rgb)(struct vo_render *, int_xyz *, void *, unsigned);

	// Advance to next line
	//     unsigned npixels;  // elapsed time in pixels
	void (*next_line)(struct vo_render *, unsigned);
};

// Create a new renderer for the specified pixel format

struct vo_render *vo_render_new(int fmt);

// Free renderer

void vo_render_free(struct vo_render *vr);

// Set buffer to render into
inline void vo_render_set_buffer(struct vo_render *vr, void *buffer) {
	vr->buffer = buffer;
}

// Used by UI to adjust viewing parameters

void vo_render_set_brightness(void *, int value);
void vo_render_set_contrast(void *, int value);
void vo_render_set_saturation(void *, int value);
void vo_render_set_hue(void *, int value);
void vo_render_set_cmp_phase(void *, int phase);

// Used by machine to configure video output

void vo_render_set_active_area(void *, int x, int y, int w, int h);
void vo_render_set_cmp_lead_lag(void *, float cha_phase, float chb_phase);
void vo_render_set_cmp_palette(void *, uint8_t c, float y, float pb, float pr);
void vo_render_set_rgb_palette(void *, uint8_t c, float r, float g, float b);
void vo_render_set_burst(void *, unsigned burstn, int offset);
void vo_render_set_cmp_phase_offset(void *sptr, int phase);

// Used by machine to render video

void vo_render_vsync(void *);
void vo_render_cmp_ntsc(void *, unsigned burstn, unsigned npixels, uint8_t const *data);

#endif
