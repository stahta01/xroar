/** \file
 *
 *  \brief PAL interfacing to MC6847 VDG.
 *
 *  \copyright Copyright 2026 Ciaran Anscomb
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

#ifndef XROAR_VDG_PAL_H_
#define XROAR_VDG_PAL_H_

#include "delegate.h"

#include "events.h"

struct MC6847;

struct vdg_pal {
	struct MC6847 *VDG;
	DELEGATE_T1(void, bool) signal_hs;
	DELEGATE_T1(void, bool) signal_fs;
	struct event pal_hs_fall_event;
	struct event pal_hs_rise_event;
	uint8_t blank_line[912];
	_Bool pal_hs_inhibit;
	int pal_scanline;
	int pal_stop_0, pal_stop_1;
	int pal_delay_0, pal_delay_1;
	int pal_count;
};

// Overrides VDG's signal_hs and signal_fs with interposing delegates that call
// the originals first.
void vdg_pal_init(struct vdg_pal *v, struct MC6847 *VDG);

// Restore original delegates
void vdg_pal_deinit(struct vdg_pal *v);

#endif
