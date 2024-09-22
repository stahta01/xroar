/** \file
 *
 *  \brief User-interface modules & interfaces.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "logging.h"
#include "messenger.h"
#include "module.h"
#include "ui.h"
#include "xconfig.h"

// File requester modules
//
// Kept here for now, intention being to roll them into the UI

extern struct module filereq_cocoa_module;
extern struct module filereq_windows32_module;
extern struct module filereq_gtk3_module;
extern struct module filereq_gtk2_module;
extern struct module filereq_cli_module;
extern struct module filereq_null_module;
struct module * const default_filereq_module_list[] = {
#ifdef HAVE_COCOA
	&filereq_cocoa_module,
#endif
#ifdef WINDOWS32
	&filereq_windows32_module,
#endif
#ifdef HAVE_GTK3
	&filereq_gtk3_module,
#endif
#ifdef HAVE_GTK2
	&filereq_gtk2_module,
#endif
#ifdef HAVE_CLI
	&filereq_cli_module,
#endif
	&filereq_null_module,
	NULL
};

struct module * const *filereq_module_list = default_filereq_module_list;
struct module *filereq_module = NULL;

// UI modules

extern struct ui_module ui_gtk3_module;
extern struct ui_module ui_gtk2_module;
extern struct ui_module ui_null_module;
extern struct ui_module ui_windows32_module;
extern struct ui_module ui_wasm_module;
extern struct ui_module ui_cocoa_module;
extern struct ui_module ui_sdl_module;
static struct ui_module * const default_ui_module_list[] = {
#ifdef HAVE_GTK3
	&ui_gtk3_module,
#endif
#ifdef HAVE_GTK2
#ifdef HAVE_GTKGL
	&ui_gtk2_module,
#endif
#endif
#ifdef WINDOWS32
	&ui_windows32_module,
#endif
#ifdef HAVE_WASM
	&ui_wasm_module,
#endif
#ifdef HAVE_COCOA
	&ui_cocoa_module,
#endif
#ifdef WANT_UI_SDL
	&ui_sdl_module,
#endif
	&ui_null_module,
	NULL
};

struct ui_module * const *ui_module_list = default_ui_module_list;

// We want a message group per tag, as otherwise the blocking behaviour would
// prevent messages about one tag affecting others.  Note: not sure if that's
// actually needed...

int ui_tag_to_group_id[ui_num_tags];

// We make up a group name for each tag based on its tag value.  Nothing uses
// the names directly (we abstract group joins by id) so to they don't need to
// be human-readable.

static const char *ui_group_by_tag(int tag) {
	static char group[18];
	assert(tag >= 0 && tag < ui_num_tags);
	snprintf(group, sizeof(group), "uimsg-%d", tag);
	return group;
}

void ui_init(void) {
	for (unsigned i = 0; i < ui_num_tags; ++i) {
		ui_tag_to_group_id[i] = -1;
	}
	// Get an ID for each group.  No need to register as a client, we only
	// ever proxy messages.
	for (int tag = 0; tag < ui_num_tags; ++tag) {
		const char *group = ui_group_by_tag(tag);
		ui_tag_to_group_id[tag] = messenger_join_group(-1, group, MESSENGER_NO_NOTIFY_DELEGATE);
	}
}

int ui_messenger_join_group(int client_id, int tag, messenger_notify_delegate notify) {
	const char *group = ui_group_by_tag(tag);
	return messenger_join_group(client_id, group, notify);
}

int ui_messenger_preempt_group(int client_id, int tag, messenger_notify_delegate notify) {
	const char *group = ui_group_by_tag(tag);
	return messenger_preempt_group(client_id, group, notify);
}

extern inline void ui_update_state(int client_id, int tag, int value, const void *data);

int ui_msg_adjust_value_range(struct ui_state_message *uimsg, int cur, int dfl,
			      int min, int max, unsigned flags) {
	assert(uimsg != NULL);
	assert(min <= max);  // I could just swap them, I suppose...
	int value = uimsg->value;
	_Bool keep_auto = 0;
	if (min >= 0 && uimsg->value == UI_NEXT) {
		value = cur + 1;
		if ((flags & UI_ADJUST_FLAG_CYCLE) && value > max) {
			value = min;
		}
	} else if (min >= 0 && uimsg->value == UI_PREV) {
		value = cur - 1;
		if ((flags & UI_ADJUST_FLAG_CYCLE) && value < min) {
			value = max;
		}
	} else if (min >= 0 && uimsg->value == UI_AUTO) {
		keep_auto = flags & UI_ADJUST_FLAG_KEEP_AUTO;
		value = dfl;
	}
	if (value < min) {
		value = min;
	} else if (value > max) {
		value = max;
	}
	if (!keep_auto) {
		uimsg->value = value;
	}
	return value;
}
