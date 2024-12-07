/** \file
 *
 *  \brief WebAssembly virtual joystick.
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

#include "xalloc.h"

#include "joystick.h"
#include "logging.h"

struct wasm_js_device {
	int axis[2];
	unsigned buttons;
};

struct wasm_js_control {
	struct joystick_control joystick_control;
	union {
		unsigned axis;
		unsigned button_mask;
	};
};

static struct wasm_js_device wasm_js;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void wasm_js_init(void) {
	struct joystick_config *jc = joystick_config_new();
	jc->name = xstrdup("wasm");
	jc->alias = xstrdup("joy0");
	jc->description = xstrdup("Virtual joystick");
	for (unsigned i = 0; i < 2; ++i) {
		jc->axis_specs[i] = xstrdup("wasm:");
		jc->button_specs[i] = xstrdup("wasm:");
		wasm_js.axis[i] = 32256;
	}
	LOG_MOD_SUB_DEBUG(1, "wasm", "joystick", "added: %s (2 axes, 2 buttons)\n", jc->description);

	joystick_config_update_menus();
	joystick_reconnect();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void wasm_js_set_axis(int axis, int value) {
	if (axis >= 2) {
		return;
	}
	if (value < 0) {
		value = 0;
	} else if (value > 65535) {
		value = 65535;
	}
	wasm_js.axis[axis] = value;
}

void wasm_js_set_button(int button, int value) {
	if (button < 0 || button >= 2) {
		return;
	}
	if (value) {
		wasm_js.buttons |= (1 << button);
	} else {
		wasm_js.buttons &= ~(1 << button);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int wasm_js_axis_read(void *sptr) {
	struct wasm_js_control *c = sptr;
	return wasm_js.axis[c->axis];
}


static int wasm_js_button_read(void *sptr) {
	struct wasm_js_control *c = sptr;
	return !!(wasm_js.buttons & c->button_mask);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_control *configure_axis(char *spec, unsigned jaxis) {
	(void)spec;
	struct wasm_js_control *c = xmalloc(sizeof(*c));
	*c = (struct wasm_js_control){0};
	c->joystick_control.read = DELEGATE_AS0(int, wasm_js_axis_read, c);
	c->axis = jaxis;
	return &c->joystick_control;
}

static struct joystick_control *configure_button(char *spec, unsigned jbutton) {
	(void)spec;
	struct wasm_js_control *c = xmalloc(sizeof(*c));
	*c = (struct wasm_js_control){0};
	c->joystick_control.read = DELEGATE_AS0(int, wasm_js_button_read, c);
	c->button_mask = (1 << jbutton);
	return &c->joystick_control;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_submodule wasm_js_submod_wasm = {
	.name = "wasm",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
};

static struct joystick_submodule *wasm_js_submodlist[] = {
	&wasm_js_submod_wasm,
	NULL
};

struct joystick_module wasm_js_mod = {
	.common = { .name = "wasm", .description = "WebAssembly virtual joystick" },
	.submodule_list = wasm_js_submodlist,
};
