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
#define VO_CMP_PHASE_KRBW (1)

// XXX old values for above - please REMOVE:

// Composite NTSC artifacting can be rendered to various degrees of accuracy.
// Additionally, VO_CMP_PALETTE and VO_CMP_SIMULATED values are used to
// distinguish between the palette-based and the arithmetic-based approaches.

#define VO_CMP_PALETTE (0)
#define VO_CMP_2BIT (1)
#define VO_CMP_5BIT (2)
#define VO_CMP_SIMULATED (3)

// NTSC cross-colour can either be switched off, or sychronised to one of two
// phases.

#define NUM_VO_PHASES (3)
#define VO_PHASE_OFF  (0)
#define VO_PHASE_KBRW (1)
#define VO_PHASE_KRBW (2)

struct vo_cfg {
	char *geometry;
	int gl_filter;
	_Bool fullscreen;
};

// For render_scanline(), accepts pointer to scanline data (uint8_t), an NTSC
// colourburst and phase indicator.
typedef DELEGATE_S3(void, uint8_t const *, struct ntsc_burst *, unsigned) DELEGATE_T3(void, uint8cp, ntscburst, unsigned);

// For palette manipulation, accepts colour index and three colour values
// (Y'PbPr or R'G'B').
typedef DELEGATE_S4(void, uint8_t, float, float, float) DELEGATE_T4(void, uint8, float, float, float);

struct vo_rect {
	int x, y;
	unsigned w, h;
};

struct vo_interface {
	struct vo_rect window;
	_Bool is_fullscreen;

	DELEGATE_T0(void) free;

	DELEGATE_T2(void, unsigned, unsigned) resize;
	DELEGATE_T1(int, bool) set_fullscreen;
	DELEGATE_T3(void, uint8cp, ntscburst, unsigned) render_scanline;
	DELEGATE_T0(void) vsync;
	DELEGATE_T0(void) refresh;
	DELEGATE_T4(void, uint8, float, float, float) palette_set_ybr;  // Composite
	DELEGATE_T4(void, uint8, float, float, float) palette_set_rgb;  // RGB
	DELEGATE_T1(void, int) set_vo_input;
	DELEGATE_T1(void, int) set_vo_cmp;
};

extern struct xconfig_enum vo_ntsc_phase_list[];

extern const uint8_t vo_cmp_lut_2bit[2][4][3];
extern const uint8_t vo_cmp_lut_5bit[2][32][3];

#endif
