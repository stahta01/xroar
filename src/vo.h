/** \file
 *
 *  \brief Video ouput modules & interfaces.
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

#ifndef XROAR_VO_H_
#define XROAR_VO_H_

#include <stdint.h>

#include "delegate.h"

#include "vo_render.h"
#include "xconfig.h"

struct module;

// Monitor input signal

enum {
	VO_SIGNAL_SVIDEO,
	VO_SIGNAL_CMP,
	VO_SIGNAL_RGB,
	NUM_VO_SIGNAL
};

// Composite cross-colour renderer.

enum {
	VO_CMP_CCR_PALETTE,
	VO_CMP_CCR_2BIT,
	VO_CMP_CCR_5BIT,
	VO_CMP_CCR_SIMULATED,
	NUM_VO_CMP_CCR
};

// phase relationship for composite cross-colour

#define VO_CMP_PHASE_KBRW (0)
#define VO_CMP_PHASE_KRBW (2)

struct vo_cfg {
	char *geometry;
	int gl_filter;
	int pixel_fmt;
	_Bool fullscreen;
};

struct vo_rect {
	unsigned x, y;
	unsigned w, h;
};

struct vo_interface {
	_Bool is_fullscreen;
	_Bool show_menubar;

	// Renderer
	struct vo_render *renderer;

	// Selected input signal
	int signal;      // VO_SIGNAL_*

	// Selected cross-colour renderer
	int cmp_ccr;    // VO_CMP_CCR_*

	// Called by vo_free before freeing the struct to handle
	// module-specific allocations
	DELEGATE_T0(void) free;

	// Used by UI to adjust viewing parameters

	// Resize window
	//     unsigned w, h;  // dimensions in pixels
	DELEGATE_T2(void, unsigned, unsigned) resize;

	// Configure active area (used to centre display)
	//     int x, y;  // top-left of active area
	//     int w, h;  // size of active area
	DELEGATE_T4(void, int, int, int, int) set_active_area;

	// Set fullscreen mode on or off
	//     _Bool fullscreen;
	DELEGATE_T1(int, bool) set_fullscreen;

	// Set menubar on or off
	//     _Bool menubar;
	DELEGATE_T1(void, bool) set_menubar;

	// Set brightness
	//     int brightness;  // 0-100
	DELEGATE_T1(void, int) set_brightness;

	// Set contrast
	//     int contrast;  // 0-100
	DELEGATE_T1(void, int) set_contrast;

	// Set colour saturation
	//     int saturation;  // 0-100
	DELEGATE_T1(void, int) set_saturation;

	// Set hue
	//     int hue;  // -179 to +180
	DELEGATE_T1(void, int) set_hue;

	// Set cross-colour phase
	//     int phase;  // VO_CMP_PHASE_*
	DELEGATE_T1(void, int) set_cmp_phase;

	// Used by machine to configure video output

	// Set how the chroma components relate to each other (in degrees)
	//     float chb_phase;  // øB phase, default 0°
	//     float cha_phase;  // øA phase, default 90°
	DELEGATE_T2(void, float, float) set_cmp_lead_lag;

	// Add a colour to the palette using Y', Pb, Pr values
	//     uint8_t index;    // palette index
	//     float y, pb, pr;  // colour
	DELEGATE_T4(void, uint8, float, float, float) palette_set_ybr;

	// Add a colour to the palette usine RGB values
	//     uint8_t index;  // palette index
	//     float R, G, B;  // colour
	DELEGATE_T4(void, uint8, float, float, float) palette_set_rgb;

	// Set a burst phase
	//     unsigned burstn;  // burst index
	//     int phase;        // in degrees
	DELEGATE_T2(void, unsigned, int) set_cmp_burst;

	// Set machine default cross-colour phase
	//     int phase;  // VO_CMP_PHASE_*
	DELEGATE_T1(void, int) set_cmp_phase_offset;

	// Used by machine to render video

	// Currently selected line renderer
	//     unsigned burst;       // burst index for this line
	//     unsigned npixels;     // no. pixels in scanline
	//     const uint8_t *data;  // palettised data, NULL for dummy line
	DELEGATE_T3(void, unsigned, unsigned, uint8cp) render_line;

	// Draw the current buffer.  Called by vo_vsync() and vo_refresh().
	DELEGATE_T0(void) draw;
};

extern struct xconfig_enum vo_cmp_ccr_list[];
extern struct xconfig_enum vo_pixel_fmt_list[];

extern const uint8_t vo_cmp_lut_2bit[2][4][3];
extern const uint8_t vo_cmp_lut_5bit[2][32][3];

// Allocates at least enough space for (struct vo_interface)

void *vo_interface_new(size_t isize);

// Calls free() delegate then frees structure

void vo_free(void *);

// Set renderer and use its contents to prepopulate various delegates.  Call
// this before overriding any locally in video modules.

void vo_set_renderer(struct vo_interface *vo, struct vo_render *vr);

// Select input signal
//     int signal;  // VO_SIGNAL_*

void vo_set_signal(struct vo_interface *vo, int signal);

// Select cross-colour renderer
//     int ccr;  // VO_CMP_CCR_*

void vo_set_cmp_ccr(struct vo_interface *vo, int ccr);

// Vertical sync.  Calls any module-specific draw function, then
// vo_render_vsync().

inline void vo_vsync(struct vo_interface *vo) {
	DELEGATE_SAFE_CALL(vo->draw);
	vo_render_vsync(vo->renderer);
}

// Refresh the display by calling draw().  Useful while single-stepping, where
// the usual render functions won't be called.

inline void vo_refresh(struct vo_interface *vo) {
	DELEGATE_SAFE_CALL(vo->draw);
}

#endif
