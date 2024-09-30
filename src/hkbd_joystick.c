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

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);
static void unmap_axis(struct joystick_axis *axis);
static void unmap_button(struct joystick_button *button);

struct joystick_submodule hkbd_js_keyboard = {
	.name = "keyboard",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
	.unmap_axis = unmap_axis,
	.unmap_button = unmap_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct axis {
	uint8_t key0_code, key1_code;
	unsigned value;
};

struct button {
	uint8_t key_code;
	_Bool value;
};

#define MAX_AXES (4)
#define MAX_BUTTONS (4)

static struct axis *enabled_axis[MAX_AXES];
static struct button *enabled_button[MAX_BUTTONS];

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
			if (code == enabled_axis[i]->key0_code) {
				enabled_axis[i]->value = 0;
				handled = 1;
			}
			if (code == enabled_axis[i]->key1_code) {
				enabled_axis[i]->value = 65535;
				handled = 1;
			}
		}
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (enabled_button[i]) {
			if (code == enabled_button[i]->key_code) {
				enabled_button[i]->value = 1;
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
			if (code == enabled_axis[i]->key0_code) {
				if (enabled_axis[i]->value < 32768)
					enabled_axis[i]->value = 32256;
				handled = 1;
			}
			if (code == enabled_axis[i]->key1_code) {
				if (enabled_axis[i]->value >= 32768)
					enabled_axis[i]->value = 33280;
				handled = 1;
			}
		}
	}
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (enabled_button[i]) {
			if (code == enabled_button[i]->key_code) {
				enabled_button[i]->value = 0;
				handled = 1;
			}
		}
	}
	return handled;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static unsigned read_axis(struct axis *a) {
	return a->value;
}

static _Bool read_button(struct button *b) {
	return b->value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
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

	struct axis *axis_data = xmalloc(sizeof(*axis_data));
	axis_data->key0_code = key0_code;
	axis_data->key1_code = key1_code;
	axis_data->value = 32256;

	struct joystick_axis *axis = xmalloc(sizeof(*axis));
	*axis = (struct joystick_axis){0};
	axis->read = (js_read_axis_func)read_axis;
	axis->data = axis_data;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (!enabled_axis[i]) {
			enabled_axis[i] = axis_data;
			break;
		}
	}
	return axis;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	// sensible defaults
	uint8_t key_code = (jbutton == 0) ? hk_scan_Alt_L : hk_scan_Super_L;

	if (spec && *spec)
		key_code = hk_scancode_from_name(spec);

	struct button *button_data = xmalloc(sizeof(*button_data));
	button_data->key_code = key_code;
	button_data->value = 0;

	struct joystick_button *button = xmalloc(sizeof(*button));
	*button = (struct joystick_button){0};
	button->read = (js_read_button_func)read_button;
	button->data = button_data;
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (!enabled_button[i]) {
			enabled_button[i] = button_data;
			break;
		}
	}
	return button;
}

static void unmap_axis(struct joystick_axis *axis) {
	if (!axis)
		return;
	for (unsigned i = 0; i < MAX_AXES; i++) {
		if (axis->data == enabled_axis[i]) {
			enabled_axis[i] = NULL;
		}
	}
	free(axis->data);
	free(axis);
}

static void unmap_button(struct joystick_button *button) {
	if (!button)
		return;
	for (unsigned i = 0; i < MAX_BUTTONS; i++) {
		if (button->data == enabled_button[i]) {
			enabled_button[i] = NULL;
		}
	}
	free(button->data);
	free(button);
}
