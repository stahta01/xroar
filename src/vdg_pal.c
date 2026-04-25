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

#include "top-config.h"

#include <assert.h>
#include <string.h>

#include "delegate.h"

#include "events.h"
#include "mc6847/mc6847.h"
#include "vdg_pal.h"
#include "xroar.h"

static void vdg_pal_signal_hs(void *sptr, _Bool level);
static void vdg_pal_signal_fs(void *sptr, _Bool level);
static void vdg_pal_hs_fall(void *sptr);
static void vdg_pal_hs_rise(void *sptr);

void vdg_pal_init(struct vdg_pal *v, struct MC6847 *VDG) {
	// Sanity check
	assert(VDG->signal_hs.func != vdg_pal_signal_hs);
	assert(VDG->signal_fs.func != vdg_pal_signal_fs);

	*v = (struct vdg_pal){0};
	v->VDG = VDG;
	memset(&v->blank_line, VDG_BLACK, sizeof(v->blank_line));
	v->signal_hs = VDG->signal_hs;
	VDG->signal_hs = DELEGATE_AS1(void, bool, vdg_pal_signal_hs, v);
	v->signal_fs = VDG->signal_fs;
	VDG->signal_fs = DELEGATE_AS1(void, bool, vdg_pal_signal_fs, v);
	event_init(&v->pal_hs_fall_event, MACHINE_EVENT_LIST, DELEGATE_AS0(void, vdg_pal_hs_fall, v));
	event_init(&v->pal_hs_rise_event, MACHINE_EVENT_LIST, DELEGATE_AS0(void, vdg_pal_hs_rise, v));
}

void vdg_pal_deinit(struct vdg_pal *v) {
	event_dequeue(&v->pal_hs_fall_event);
	event_dequeue(&v->pal_hs_rise_event);
	v->VDG->signal_hs = v->signal_hs;
	v->VDG->signal_fs = v->signal_fs;
}

static void vdg_pal_signal_hs(void *sptr, _Bool level) {
	struct vdg_pal *v = sptr;

	DELEGATE_CALL(v->signal_hs, level);
	if (level)
		return;

	if (!v->pal_delay_0)
		return;

	v->pal_scanline++;

	if (v->pal_scanline == v->pal_stop_0) {
		v->pal_count = v->pal_delay_0;
	} else if (v->pal_scanline == v->pal_stop_1) {
		v->pal_count = v->pal_delay_1;
	}
	if (v->pal_count) {
		mc6847_pause(v->VDG);
		v->pal_hs_rise_event.at_tick = event_current_tick + 64;
		event_queue(&v->pal_hs_rise_event);
		v->pal_hs_fall_event.at_tick = event_current_tick + 912;
		event_queue(&v->pal_hs_fall_event);
	}
}

static void vdg_pal_signal_fs(void *sptr, _Bool level) {
	struct vdg_pal *v = sptr;

	DELEGATE_CALL(v->signal_fs, level);
	if (level) {
		v->pal_scanline = 0;
	}
}

static void vdg_pal_hs_fall(void *sptr) {
	struct vdg_pal *v = sptr;

	DELEGATE_CALL(v->VDG->render_line, 0, 912, v->blank_line);

	if (!v->pal_hs_inhibit) {
		DELEGATE_CALL(v->signal_hs, 0);
	}

	v->pal_count--;
	if (v->pal_count == 0) {
		mc6847_unpause(v->VDG);
		return;
	}

	v->pal_hs_rise_event.at_tick = event_current_tick + 64;
	event_queue(&v->pal_hs_rise_event);
	v->pal_hs_fall_event.at_tick = event_current_tick + 912;
	event_queue(&v->pal_hs_fall_event);
}

static void vdg_pal_hs_rise(void *sptr) {
	struct vdg_pal *v = sptr;

	DELEGATE_CALL(v->signal_hs, 1);
}
