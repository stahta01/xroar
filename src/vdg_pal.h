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

/* PAL machines typically have external circuitry counting scanline and
 * stopping the VDG at two points in each frame to insert padding lines.
 *
 * How to use:
 *
 * Include a struct vdg_pal in machine data.
 *
 * Call vdg_pal_init() if PAL interposing is required.  This will swap the VDG
 * delegates around to go via functions provided here.
 *
 * Before running the machine, configure the stop and delay values (see struct
 * comments), and optionally pal_hs_inhibit.
 *
 * User needs to account for these delays when they tell the video output
 * system about the active area.
 *
 * When done, call vdg_pal_deinit().  This is safe to do even if not in use if
 * you initialised the VDG field of the struct to NULL.
 */

#ifndef XROAR_VDG_PAL_H_
#define XROAR_VDG_PAL_H_

#include "delegate.h"

#include "events.h"

struct MC6847;

struct vdg_pal {
	// The two points at which the VDG is stopped to generate padding lines
	// and the number of delay lines generated in each case.  Delays will
	// typically sum to 50.
	int pal_stop_0, pal_delay_0;
	int pal_stop_1, pal_delay_1;

	// Set to inhibit HS during padding (e.g. Dragon 64)
	bool pal_hs_inhibit;

	// VDG being interposed
	struct MC6847 *VDG;

	// The original VDG signal handlers, called where appropriate
	DELEGATE_T1(void, bool) signal_hs;
	DELEGATE_T1(void, bool) signal_fs;

	// Internal counters
	int pal_scanline;
	int pal_count;
	// Internal events
	struct event pal_hs_fall_event;
	struct event pal_hs_rise_event;

	// Dummy line emitted during padding.  Initialised to VDG_BLACK, but we
	// could tweak this to a separate palette entry for "not quite black"
	// (or dynamically update it to "last VDG border colour").
	uint8_t blank_line[912];
};

// Overrides VDG's signal_hs and signal_fs with interposing delegates that call
// the originals where appropriate.
void vdg_pal_init(struct vdg_pal *v, struct MC6847 *VDG);

// Restore original delegates.
void vdg_pal_deinit(struct vdg_pal *v);

#endif
