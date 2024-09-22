/** \file
 *
 *  \brief Video renderers.
 *
 *  \copyright Copyright 2023-2024 Ciaran Anscomb
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

#include "delegate.h"
#include "intfuncs.h"

#include "ntsc.h"
#include "xconfig.h"

// Window Area, Draw Area and Picture Area defined in vo.h.

// Viewport defines the region of the emulated video output that is to be
// mapped into the Picture Area, defined horizontally in terms of emulated
// pixels at F(s) from HSync fall, and vertically in scanlines from VSync rise.
//
// Offset is calculated from Active Area into (new_x,new_y), updated into (x,y)
// on vertical sync.
//
// Note that any system currently emulated sends a progressive signal to an
// interlaced display, so the vertical resolution will appear to be halved.

struct vo_viewport {
	int new_x, new_y;
	int x, y;
	int w, h;
};

// Active Area is updated by the emulated system on mode change to indicate
// where useful picture resides (excluding borders).  It is only used to update
// the Viewport offset, and has the same units.

struct vo_active_area {
	int x, y;
	int w, h;
};

// For speed we maintain tables for the modulation/demodulation of composite
// video that can be indexed be an incrementing integer time 't', modulo
// 'tmax'.  'tmax' is chosen such that a (near-enough) integer number of
// samples at F(s) corresponds to a (near-enough) integer number of cycles at
// F(sc).
//
// For NTSC machines with F(s) = 14.31818 MHz, this is very trivial: four
// samples at F(s) exactly covers one cycle at 3.579545 MHz.  For other
// combinations, 'tmax' will encompass more than one chroma cycle.

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

extern struct xconfig_enum vo_pixel_fmt_list[];

// For configuring per-renderer colour palette entries

enum {
	VO_RENDER_PALETTE_CMP,
	VO_RENDER_PALETTE_CMP_2BIT,
	VO_RENDER_PALETTE_CMP_5BIT,
	VO_RENDER_PALETTE_RGB,
};

// Pixel rates - used as sampling frequency when filtering

enum {
	VO_RENDER_FS_14_31818,
	VO_RENDER_FS_14_218,
	VO_RENDER_FS_14_23753,
	NUM_VO_RENDER_FS
};

extern struct xconfig_enum vo_render_fs_list[];

// Colour subcarrier frequencies

enum {
	VO_RENDER_FSC_4_43361875,
	VO_RENDER_FSC_3_579545,
	NUM_VO_RENDER_FSC
};

extern struct xconfig_enum vo_render_fsc_list[];

// Colour systems

enum {
	VO_RENDER_SYSTEM_PAL_I,
	VO_RENDER_SYSTEM_PAL_M,
	VO_RENDER_SYSTEM_NTSC,
	NUM_VO_RENDER_SYSTEM
};

extern struct xconfig_enum vo_render_system_list[];

// Largest value of 'tmax' (and thus 't')
#define VO_RENDER_MAX_T (228)

// Composite Video simulation
//
// The supported signals are defined as:
//
// NTSC = Y' + U sin ωt + V cos ωt, burst 180° (-U)
//
// PAL  = Y' + U sin ωt ± V cos ωt, burst 180° ± 45°
//
// The normal burst phase isn't terribly important, because a decoder may
// operate by synchronising to it, making colour always relative to it.
// However, we definitely care when the phase is modified, as that changes the
// relative phase of the colour information.
//
// Burst index 0 is reserved for indicating "no burst" - ie that a display may
// choose not to decode any colour information.  Burst index 1 is typically
// used with a phase offset of 0; ie, "normal" colour.  Extra bursts are used
// in the cases where the initial burst phase is modified, but the scanline
// colour information maintains its usual phase.
//
// We store demodulation tables here too, as a demodulator would synchronise
// with the colourburst it received.

struct vo_render_burst {
	// Offset from "normal" phase
	int phase_offset;

	// Values to multiply U and V at time 't' when modulating
	struct {
		int u[VO_RENDER_MAX_T];     // typically  sin ωt
		int v[2][VO_RENDER_MAX_T];  // typically ±cos ωt
	} mod;

	// Multiplied against signal and then low-pass filtered to
	// extract U and V
	struct {
		int u[VO_RENDER_MAX_T];     // typically  2 sin ωt
		int v[2][VO_RENDER_MAX_T];  // typically ±2 cos ωt
	} demod;

	// Data for the 'partial' renderer
	struct ntsc_burst ntsc_burst;
};

// Filter definition.  'coeff' actually points to the centre value, so can be
// indexed from -order to +order.

struct vo_render_filter {
	int order;
	int *coeff;
};

struct vo_render {
	struct {
		// Record values for recalculation
		struct {
			float y, pb, pr;
		} colour[256];

		// Precalculated values for composite renderer
		struct {
			// Multipliers to get from Y',R'-Y',B'-Y' to Y'UV
			double yconv;
			struct {
				double umul;
				double vmul;
			} uconv, vconv;

			int y[256];
			int u[256];
			int v[256];
		} palette;

		// Cache testing if each colour is black or white
		uint8_t is_black_or_white[256];

		// F(s); pixel rate
		int fs;

		// F(sc); chroma subcarrier
		int fsc;

		// Colour system
		int system;

		// Lead/lag of chroma components
		float cha_phase;  // default 90° = π/2

		// Whether to chroma average successive lines (eg PAL)
		_Bool average_chroma;

		// Whether colour-killer is enabled for no colourburst (burstn=0)
		_Bool colour_killer;

		// PAL v-switch
		int vswitch;

		struct {
			// Chroma low pass filters
			int corder;  // max of ufilter.order, vfilter.order
			struct vo_render_filter ufilter;
			struct vo_render_filter vfilter;
		} mod;

		struct {
			// Luma low pass filter
			struct vo_render_filter yfilter;

			// Chroma low pass filters
			int corder;  // max of ufilter.order, vfilter.order
			struct vo_render_filter ufilter;
			struct vo_render_filter vfilter;

			int morder;  // max of corder, yfilter.order

			// Filter chroma line delay.  Used in PAL averaging.
			int fubuf[2][1024];
			int fvbuf[2][1024];

			// Saturation converted to integer
			int saturation;

			// Upper & lower limits of decoded U/V values
			struct {
				int lower;
				int upper;
			} ulimit, vlimit;

			// Multipliers to get from U/V to R'G'B' (Y' assumed)
			struct {
				int umul;
				int vmul;
			} rconv, gconv, bconv;
		} demod;

		// And a full NTSC decode table
		struct ntsc_palette ntsc_palette;

		// NTSC bursts
		unsigned nbursts;
		struct vo_render_burst *burst;

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

	// Messenger client id
	int msgr_client_id;

	struct vo_viewport viewport;
	struct vo_active_area active_area;

	// Whether 60Hz scaling is enabled
	_Bool ntsc_scaling;

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

	// Current frame rate - used for notifying video module of change
	_Bool is_60hz;

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

	// Populated by video module

	// Notify video module if frame rate changes, based on scanline count.
	// Default assumption should be 50Hz.
	//
	//     _Bool is_60hz;  // false = 50Hz, true = 60Hz

	DELEGATE_T1(void, bool) notify_frame_rate;

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

	// Convert line into RGB (uint8s in that order) for screenshots
	void (*line_to_rgb)(struct vo_render *, int, uint8_t *);
};

// Create a new renderer for the specified pixel format

struct vo_render *vo_render_new(int fmt);

// Free renderer

void vo_render_free(struct vo_render *vr);

// Set buffer to render into
inline void vo_render_set_buffer(struct vo_render *vr, void *buffer) {
	vr->pixel = vr->buffer = buffer;
}

// Used by UI to adjust viewing parameters

void vo_render_set_viewport(struct vo_render *, int w, int h);
void vo_render_set_cmp_phase(void *, int phase);

// Used by machine to configure video output

void vo_render_set_active_area(void *, int x, int y, int w, int h);
void vo_render_set_cmp_lead_lag(void *, float chb_phase, float cha_phase);
void vo_render_set_cmp_palette(void *, uint8_t c, float y, float pb, float pr);
void vo_render_set_rgb_palette(void *, uint8_t c, float r, float g, float b);
void vo_render_set_cmp_burst(void *, unsigned burstn, int offset);
void vo_render_set_cmp_burst_br(void *sptr, unsigned burstn, float b_y, float r_y);
void vo_render_set_cmp_phase_offset(void *sptr, int phase);

// Used by machine to render video

void vo_render_vsync(void *);
void vo_render_cmp_partial(void *, unsigned burstn, unsigned npixels, uint8_t const *data);
void vo_render_cmp_simulated(void *, unsigned burstn, unsigned npixels, uint8_t const *data);

#endif
