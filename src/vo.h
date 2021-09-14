/** \file
 *
 *  \brief Video ouput modules & interfaces.
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

// For palette manipulation, accepts colour index and three colour values
// (Y'PbPr or R'G'B').
typedef DELEGATE_S4(void, uint8_t, float, float, float) DELEGATE_T4(void, uint8, float, float, float);

struct vo_interface {
	_Bool is_fullscreen;

	DELEGATE_T0(void) free;

	DELEGATE_T2(void, unsigned, unsigned) resize;
	DELEGATE_T1(int, bool) set_fullscreen;
	DELEGATE_T2(void, uint8cp, ntscburst) render_scanline;
	DELEGATE_T0(void) vsync;
	DELEGATE_T0(void) refresh;
	DELEGATE_T2(void, unsigned, unsigned) set_viewport_xy;
	DELEGATE_T4(void, uint8, float, float, float) palette_set_ybr;  // Composite
	DELEGATE_T4(void, uint8, float, float, float) palette_set_rgb;  // RGB
	DELEGATE_T1(void, int) set_input;
	DELEGATE_T1(void, int) set_cmp_ccr;
	DELEGATE_T1(void, int) set_cmp_phase;  // set by user
	DELEGATE_T1(void, int) set_cmp_phase_offset;  // set per-machine
};

extern struct xconfig_enum vo_cmp_ccr_list[];

extern const uint8_t vo_cmp_lut_2bit[2][4][3];
extern const uint8_t vo_cmp_lut_5bit[2][32][3];

#endif
