/** \file
 *
 *  \brief Mac OS X+ user-interface common definitions.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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

#ifndef XROAR_MACOSX_UI_MACOSX_H_
#define XROAR_MACOSX_UI_MACOSX_H_

#include <stdint.h>

#define UIMAC_TAG(t) (((t) & 0x7f) << 8)
#define UIMAC_TAGV(t,v) (UIMAC_TAG(t) | ((v) & 0xff))
#define UIMAC_TAG_TYPE(t) (((t) >> 8) & 0x7f)
#define UIMAC_TAG_VALUE(t) ((int8_t)((t) & 0xff))

struct ui_macosx_interface {
	struct ui_sdl2_interface ui_sdl2_interface;

	// Top level messenger client id
	int msgr_client_id;

	// UI state tracking

	struct {
		int id;
		int keymap;
	} machine;

	struct {
		int id;
	} cart;

	struct {
		int playing;
		int fast;
		int pad_auto;
		int rewrite;
	} tape;

	struct {
		struct {
			int write_enable;
			int write_back;
		} drive[4];
	} disk;

	struct {
		int cmp_fs;
		int cmp_fsc;
		int cmp_system;
		int cmp_colour_killer;
		int ccr;
		int picture;
		int ntsc_scaling;
		int tv_input;
		int fullscreen;
		int invert_text;
	} vo;

	struct {
		int layout;
		int lang;
		int translate;
	} kbd;

	struct {
		int id[2];
	} joy;

	struct {
		int destination;
	} lp;

	struct {
		int ratelimit_latch;
	} misc;

	struct {
		int autosave;
	} config;
};

#endif
