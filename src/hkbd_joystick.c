/** \file
 *
 *  \brief Keyboard-based virtual joystick.
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

#include "top-config.h"

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pl-string.h"
#include "xalloc.h"

#include "hkbd.h"
#include "joystick.h"
#include "logging.h"
#include "module.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_control *configure_axis(char *, unsigned);
static struct joystick_control *configure_button(char *, unsigned);

struct joystick_submodule hkbd_js_keyboard = {
	.name = "keyboard",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct hkbd_js_control {
	struct joystick_control control;
	union {
		struct {
			uint8_t key0_code, key1_code;
			unsigned value;
		} axis;
		struct {
			uint8_t key_code;
			_Bool value;
		} button;
	};
};

#define MAX_AXES (4)
#define MAX_BUTTONS (4)

static struct hkbd_js_control *enabled_axis[MAX_AXES];
static struct hkbd_js_control *enabled_button[MAX_BUTTONS];
static void free_axis(void *);
static void free_button(void *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void hkbd_js_init(void) {
	// Clear the keystick mappings.
	for (unsigned i = 0; i < MAX_AXES; i++) {
		enabled_axis[i] = NULL;
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		enabled_button[i] = NULL;
	}
}

// Returns true if handled as a virtual joystick control

_Bool hkbd_js_keypress(uint8_t code) {
	_Bool handled = 0;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (enabled_axis[i]) {
			if (code == enabled_axis[i]->axis.key0_code) {
				enabled_axis[i]->axis.value = 0;
				handled = 1;
			}
			if (code == enabled_axis[i]->axis.key1_code) {
				enabled_axis[i]->axis.value = 65535;
				handled = 1;
			}
		}
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (enabled_button[i]) {
			if (code == enabled_button[i]->button.key_code) {
				enabled_button[i]->button.value = 1;
				handled = 1;
			}
		}
	}
	return handled;
}

_Bool hkbd_js_keyrelease(uint8_t code) {
	_Bool handled = 0;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (enabled_axis[i]) {
			if (code == enabled_axis[i]->axis.key0_code) {
				if (enabled_axis[i]->axis.value < 32768)
					enabled_axis[i]->axis.value = 32256;
				handled = 1;
			}
			if (code == enabled_axis[i]->axis.key1_code) {
				if (enabled_axis[i]->axis.value >= 32768)
					enabled_axis[i]->axis.value = 33280;
				handled = 1;
			}
		}
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (enabled_button[i]) {
			if (code == enabled_button[i]->button.key_code) {
				enabled_button[i]->button.value = 0;
				handled = 1;
			}
		}
	}
	return handled;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int hkbd_js_axis_read(void *);
static int hkbd_js_button_read(void *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_control *configure_axis(char *spec, unsigned jaxis) {
	uint8_t key0_code, key1_code;

	// sensible defaults
	if (jaxis == 0) {
		key0_code = hk_scan_Left;
		key1_code = hk_scan_Right;
	} else {
		key0_code = hk_scan_Up;
		key1_code = hk_scan_Down;
	}

	char *a0 = NULL;
	char *a1 = NULL;
	if (spec) {
		a0 = strsep(&spec, ",");
		a1 = spec;
	}
	if (a0 && *a0)
		key0_code = hk_scancode_from_name(a0);
	if (a1 && *a1)
		key1_code = hk_scancode_from_name(a1);

	struct hkbd_js_control *axis = xmalloc(sizeof(*axis));
	*axis = (struct hkbd_js_control){0};
	struct joystick_control *control = &axis->control;

	control->read = DELEGATE_AS0(int, hkbd_js_axis_read, axis);
	control->free = DELEGATE_AS0(void, free_axis, axis);

	axis->axis.key0_code = key0_code;
	axis->axis.key1_code = key1_code;
	axis->axis.value = 32256;

	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (!enabled_axis[i]) {
			enabled_axis[i] = axis;
			break;
		}
	}
	return control;
}

static struct joystick_control *configure_button(char *spec, unsigned jbutton) {
	// sensible defaults
	uint8_t key_code = (jbutton == 0) ? hk_scan_Alt_L : hk_scan_Super_L;

	if (spec && *spec)
		key_code = hk_scancode_from_name(spec);

	struct hkbd_js_control *button = xmalloc(sizeof(*button));
	*button = (struct hkbd_js_control){0};
	struct joystick_control *control = &button->control;

	control->read = DELEGATE_AS0(int, hkbd_js_button_read, button);
	control->free = DELEGATE_AS0(void, free_button, button);

	button->button.key_code = key_code;
	button->button.value = 0;

	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (!enabled_button[i]) {
			enabled_button[i] = button;
			break;
		}
	}
	return control;
}

static int hkbd_js_axis_read(void *sptr) {
	struct hkbd_js_control *a = sptr;
	return a->axis.value;
}

static int hkbd_js_button_read(void *sptr) {
	struct hkbd_js_control *b = sptr;
	return b->button.value;
}

static void free_axis(void *sptr) {
	struct hkbd_js_control *axis = sptr;
	if (!axis)
		return;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (axis == enabled_axis[i]) {
			enabled_axis[i] = NULL;
		}
	}
	free(axis);
}

static void free_button(void *sptr) {
	struct hkbd_js_control *button = sptr;
	if (!button)
		return;
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (button == enabled_button[i]) {
			enabled_button[i] = NULL;
		}
	}
	free(button);
}
