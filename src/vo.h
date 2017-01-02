/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2017 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

Video output interface definition.

*/

#ifndef XROAR_VO_H_
#define XROAR_VO_H_

#include <stdint.h>

#include "delegate.h"

struct module;
struct ntsc_burst;

#define VO_CMP_PALETTE (0)
#define VO_CMP_2BIT (1)
#define VO_CMP_5BIT (2)
#define VO_CMP_SIMULATED (3)

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

inline int clamp_uint8(int v) {
	if (v < 0) return 0;
	if (v > 255) return 255;
	return v;
}

#endif
