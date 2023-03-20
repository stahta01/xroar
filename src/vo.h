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

#include "xconfig.h"

struct module;
struct ntsc_burst;

// Select monitor input.
#define VO_TV_CMP (0)
#define VO_TV_RGB (1)

// Select composite cross-colour renderer.
#define VO_CMP_CCR_NONE (0)
#define VO_CMP_CCR_2BIT (1)
#define VO_CMP_CCR_5BIT (2)
#define VO_CMP_CCR_SIMULATED (3)

// Select phase relationship for composite cross-colour.
#define VO_CMP_PHASE_KBRW (0)
#define VO_CMP_PHASE_KRBW (2)

struct vo_cfg {
	char *geometry;
	int gl_filter;
	_Bool fullscreen;
};

struct vo_rect {
	unsigned x, y;
	unsigned w, h;
};

// For render_scanline(), accepts pointer to scanline data (uint8_t), and an NTSC
// colourburst.
typedef DELEGATE_S2(void, uint8_t const *, struct ntsc_burst *) DELEGATE_T2(void, uint8cp, ntscburst);

struct vo_interface {
	_Bool is_fullscreen;
	_Bool show_menubar;

	DELEGATE_T0(void) free;

	// Used by UI to adjust viewing parameters

	// Resize window
	//     unsigned w, h;  // dimensions in pixels
	DELEGATE_T2(void, unsigned, unsigned) resize;

	// Configure viewport X and Y offset
	//     unsigned x0, y0;  // offset to top-left displayed pixel
	DELEGATE_T2(void, unsigned, unsigned) set_viewport_xy;

	// Set fullscreen mode on or off
	//     _Bool fullscreen;
	DELEGATE_T1(int, bool) set_fullscreen;

	// Set menubar on or off
	//     _Bool menubar;
	DELEGATE_T1(void, bool) set_menubar;

	// Select TV "input"
	//     int input;  // VO_TV_*
	DELEGATE_T1(void, int) set_input;

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

	// Set cross-colour renderer
	//     int ccr;  // VO_CMP_CCR_*
	DELEGATE_T1(void, int) set_cmp_ccr;

	// Set cross-colour phase
	//     int phase;  // VO_CMP_PHASE_*
	DELEGATE_T1(void, int) set_cmp_phase;

	// Used by machine to configure video output

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
	DELEGATE_T2(void, unsigned, int) set_burst;

	// Set machine default cross-colour phase
	//     int phase;  // VO_CMP_PHASE_*
	DELEGATE_T1(void, int) set_cmp_phase_offset;

	// Used by machine to render video

	// Submit a scanline for rendering
	//     unsigned burst;       // burst index for this line
	//     unsigned npixels;     // no. pixels in scanline
	//     const uint8_t *data;  // palettised data, NULL for dummy line
	DELEGATE_T3(void, unsigned, unsigned, uint8cp) render_line;

	// Vertical sync
	DELEGATE_T0(void) vsync;

	// Refresh the display (useful while single-stepping, where the usual
	// render functions won't be called)
	DELEGATE_T0(void) refresh;
};

extern struct xconfig_enum vo_cmp_ccr_list[];

extern const uint8_t vo_cmp_lut_2bit[2][4][3];
extern const uint8_t vo_cmp_lut_5bit[2][32][3];

#endif
