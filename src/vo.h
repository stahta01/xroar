/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2016 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

Video output module definition.

*/

#ifndef XROAR_VO_H_
#define XROAR_VO_H_

#include <stdint.h>

#include "delegate.h"

#include "module.h"

struct ntsc_burst;

#define VO_CMP_PALETTE (0)
#define VO_CMP_2BIT (1)
#define VO_CMP_5BIT (2)
#define VO_CMP_SIMULATED (3)

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

	/*
	void (* update_palette)(struct vo_interface *vo);
	void (* resize)(struct vo_interface *vo, unsigned int w, unsigned int h);
	int (* set_fullscreen)(struct vo_interface *vo, _Bool fullscreen);
	void (*render_scanline)(struct vo_interface *vo, uint8_t const *scanline_data,
				struct ntsc_burst *burst, unsigned phase);
	void (* vsync)(struct vo_interface *vo);
	void (* refresh)(struct vo_interface *vo);
	void (* set_vo_cmp)(struct vo_interface *vo, int mode);
	*/

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
