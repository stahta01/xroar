/*

Video ouput modules & interfaces

Copyright 2003-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_VO_H_
#define XROAR_VO_H_

#include <stdint.h>

#include "delegate.h"

#include "xconfig.h"

struct module;
struct ntsc_burst;

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

typedef DELEGATE_S3(void, uint8_t const *, struct ntsc_burst *, unsigned) DELEGATE_T3(void, uint8cp, ntscburst, unsigned);

struct vo_rect {
	int x, y;
	unsigned w, h;
};

struct vo_interface {
	int window_x, window_y;
	int window_w, window_h;
	_Bool is_fullscreen;

	DELEGATE_T0(void) free;

	DELEGATE_T0(void) update_palette;
	DELEGATE_T2(void, unsigned, unsigned) resize;
	DELEGATE_T1(int, bool) set_fullscreen;
	DELEGATE_T3(void, uint8cp, ntscburst, unsigned) render_scanline;
	DELEGATE_T0(void) vsync;
	DELEGATE_T0(void) refresh;
	DELEGATE_T1(void, int) set_vo_cmp;
};

extern struct module * const *vo_module_list;

extern struct xconfig_enum vo_ntsc_phase_list[];

inline int clamp_uint8(int v) {
	if (v < 0) return 0;
	if (v > 255) return 255;
	return v;
}

#endif
