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

#include "module.h"

struct ntsc_burst;

#define VO_CMP_PALETTE (0)
#define VO_CMP_2BIT (1)
#define VO_CMP_5BIT (2)
#define VO_CMP_SIMULATED (3)

struct vo_module {
	struct module common;
	int scanline;
	int window_x, window_y;
	int window_w, window_h;
	void (* const update_palette)(void);
	void (* const resize)(unsigned int w, unsigned int h);
	int (* const set_fullscreen)(_Bool fullscreen);
	_Bool is_fullscreen;
	void (*render_scanline)(struct vo_module *vo, uint8_t const *scanline_data,
				struct ntsc_burst *burst, unsigned phase);
	void (* const vsync)(struct vo_module *vo);
	void (* const refresh)(void);
	void (* const set_vo_cmp)(struct vo_module *vo, int mode);
};

extern struct vo_module * const *vo_module_list;
extern struct vo_module *vo_module;

#endif
