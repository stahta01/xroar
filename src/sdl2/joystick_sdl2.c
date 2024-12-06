/** \file
 *
 *  \brief SDL2 joystick module.
 *
 *  \copyright Copyright 2015-2024 Ciaran Anscomb
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "pl-string.h"
#include "sds.h"
#include "xalloc.h"

#include "events.h"
#include "joystick.h"
#include "logging.h"
#include "module.h"
#include "xroar.h"

#include "sdl2/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void sdl_js_physical_init(void);
static struct joystick_control *configure_physical_axis(char *, unsigned);
static struct joystick_control *configure_physical_button(char *, unsigned);

struct joystick_submodule sdl_js_physical = {
	.name = "physical",
	.init = sdl_js_physical_init,
	.configure_axis = configure_physical_axis,
	.configure_button = configure_physical_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_submodule *js_submodlist[] = {
	&sdl_js_physical,
	NULL
};

struct joystick_module sdl_js_mod_exported = {
	.common = { .name = "sdl", .description = "SDL2 joystick input" },
	.submodule_list = js_submodlist,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Wrap SDL_Joystick up in struct sdl_js_device.  close_device() will only close the
// underlying joystick once open_count reaches 0.  Also preserves a link to the
// created joystick config so that it can be deactivated on removal by setting
// its description to NULL.

struct sdl_js_device {
	_Bool valid;

	_Bool is_gamecontroller;
	int joystick_index;
	union {
		SDL_Joystick *joystick;
		SDL_GameController *gamecontroller;
	} handle;
	event_ticks last_query;

	// Joystick config names auto-created against this device
	char *jc_names[3];

	unsigned open_count;
	int num_axes;
	int num_buttons;
	unsigned *debug_axes;
	unsigned debug_buttons;
};

static int num_devices = 0;
static _Bool events_enabled = 0;
static struct sdl_js_device *devices = NULL;

struct sdl_js_control {
	struct joystick_control joystick_control;

	struct sdl_js_device *device;
	union {
		int axis;
		unsigned button_mask;
	} control;
	_Bool inverted;
};

// Add a joystick.  Called during initialisation, and when running the SDL UI,
// on SDL_*DEVICEADDED events.  Non-SDL UIs using this module won't get the
// dynamic add/remove behaviour.

void sdl_js_device_added(int index) {
	SDL_GameController *gamecontroller = NULL;
	SDL_Joystick *joystick = NULL;

	if (SDL_IsGameController(index)) {
		gamecontroller = SDL_GameControllerOpen(index);
		if (!gamecontroller)
			return;
	} else {
		joystick = SDL_JoystickOpen(index);
		if (!joystick)
			return;
	}

	// Does the devices array need to grow?
	if (index >= num_devices) {
		// I have no idea if this paranoia is justified
		int new_num_devices = SDL_NumJoysticks();
		if (index >= new_num_devices) {
			if (gamecontroller) {
				SDL_GameControllerClose(gamecontroller);
			} else {
				SDL_JoystickClose(joystick);
			}
			return;
		}
		// Reallocate the devices array and initialise new additions
		devices = xrealloc(devices, new_num_devices * sizeof(*devices));
		for (int i = num_devices ; i < new_num_devices; ++i) {
			devices[num_devices] = (struct sdl_js_device){0};
		}
		num_devices = new_num_devices;
	}

	struct sdl_js_device *d = &devices[index];
	if (d->valid) {
		return;
	}
	d->valid = 1;
	d->is_gamecontroller = (gamecontroller != NULL);

	if (d->is_gamecontroller) {
		d->num_axes = SDL_CONTROLLER_AXIS_MAX;
		d->num_buttons = SDL_CONTROLLER_BUTTON_MAX;
	} else {
		d->num_axes = SDL_JoystickNumAxes(joystick);
		d->num_buttons = SDL_JoystickNumButtons(joystick);
	}

	SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(index);
	char guid_str[33];
	SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));

	const char *joy_name = NULL;
	if (gamecontroller) {
		joy_name = SDL_GameControllerName(gamecontroller);
	} else {
		joy_name = SDL_JoystickName(joystick);
	}
	if (!joy_name) {
		joy_name = "Joystick";
	}

	// Always add as a standalone joystick.  First two axes and buttons.
	{
		sds tmp;

		struct joystick_config *jc = joystick_config_new();
		jc->name = xstrdup(guid_str);
		d->jc_names[0] = xstrdup(jc->name);
		//
		tmp = sdscatprintf(sdsempty(), "joy%d", index);
		jc->alias = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "%d: %s", index, joy_name);
		jc->description = xstrdup(tmp);
		sdsfree(tmp);
		//
		for (int i = 0; i < JOYSTICK_NUM_AXES; ++i) {
			tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, i);
			jc->axis_specs[i] = xstrdup(tmp);
			sdsfree(tmp);
		}
		//
		for (int i = 0; i < JOYSTICK_NUM_BUTTONS; ++i) {
			tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, i);
			jc->button_specs[i] = xstrdup(tmp);
			sdsfree(tmp);
		}
		//
		LOG_MOD_SUB_DEBUG(1, "sdl", "joystick", "added: %s ", jc->description);
		if (d->is_gamecontroller) {
			LOG_DEBUG(1, "(gamepad)\n");
		} else {
			LOG_DEBUG(1, "(%d %s, ", d->num_axes, (d->num_axes == 1) ? "axis" : "axes");
			LOG_DEBUG(1, "%d button%s)\n", d->num_buttons, (d->num_buttons == 1) ? "" : "s");
		}
	}

	if (logging.level > 1) {
		unsigned vendor_id = 0;
		unsigned product_id = 0;
		unsigned product_version = 0;
		if (d->is_gamecontroller) {
			vendor_id = SDL_GameControllerGetVendor(gamecontroller);
			product_id = SDL_GameControllerGetProduct(gamecontroller);
			product_version = SDL_GameControllerGetProductVersion(gamecontroller);
		} else {
			vendor_id = SDL_JoystickGetVendor(joystick);
			product_id = SDL_JoystickGetProduct(joystick);
			product_version = SDL_JoystickGetProductVersion(joystick);
		}
		LOG_PRINT("\tGUID: %s\n", guid_str);
		if (vendor_id) {
			LOG_PRINT("\tVendor ID: 0x%04x\n", vendor_id);
		}
		if (product_id) {
			LOG_PRINT("\tProduct ID: 0x%04x\n", product_id);
		}
		if (product_version) {
			LOG_PRINT("\tProduct version: 0x%04x\n", product_version);
		}
	}

	// If it's a game controller, add left and right stick configs
	if (d->is_gamecontroller) {
		sds tmp;

		struct joystick_config *jc0 = joystick_config_new();
		tmp = sdscatprintf(sdsempty(), "%s/l", guid_str);
		jc0->name = xstrdup(tmp);
		d->jc_names[1] = xstrdup(jc0->name);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "joy%d/l", index);
		jc0->alias = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "%d: %s (L)", index, joy_name);
		jc0->description = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, SDL_CONTROLLER_AXIS_LEFTX);
		jc0->axis_specs[0] = xstrdup(tmp);
		sdsfree(tmp);
		tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, SDL_CONTROLLER_AXIS_LEFTY);
		jc0->axis_specs[1] = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "physical:%d,%%%d", index, (1 << SDL_CONTROLLER_BUTTON_A) | (1 << SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
		jc0->button_specs[0] = xstrdup(tmp);
		sdsfree(tmp);
		tmp = sdscatprintf(sdsempty(), "physical:%d,%%%d", index, (1 << SDL_CONTROLLER_BUTTON_B));
		jc0->button_specs[1] = xstrdup(tmp);
		sdsfree(tmp);
		//
		LOG_MOD_SUB_DEBUG(1, "sdl", "joystick", "added: %s\n", jc0->description);

		struct joystick_config *jc1 = joystick_config_new();
		tmp = sdscatprintf(sdsempty(), "%s/r", guid_str);
		jc1->name = xstrdup(tmp);
		d->jc_names[2] = xstrdup(jc1->name);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "joy%d/r", index);
		jc1->alias = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "%d: %s (R)", index, joy_name);
		jc1->description = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, SDL_CONTROLLER_AXIS_RIGHTX);
		jc1->axis_specs[0] = xstrdup(tmp);
		sdsfree(tmp);
		tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, SDL_CONTROLLER_AXIS_RIGHTY);
		jc1->axis_specs[1] = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "physical:%d,%%%d", index, (1 << SDL_CONTROLLER_BUTTON_X) | (1 << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
		jc1->button_specs[0] = xstrdup(tmp);
		sdsfree(tmp);
		tmp = sdscatprintf(sdsempty(), "physical:%d,%%%d", index, (1 << SDL_CONTROLLER_BUTTON_Y));
		jc1->button_specs[1] = xstrdup(tmp);
		sdsfree(tmp);
		//
		LOG_MOD_SUB_DEBUG(1, "sdl", "joystick", "added: %s\n", jc1->description);
	}

	joystick_config_update_menus();

	// Close device - it will be opened when actually configured for use.
	if (gamecontroller) {
		SDL_GameControllerClose(gamecontroller);
	} else {
		SDL_JoystickClose(joystick);
	}

	// This may reconnect joysticks that used this device previously, if
	// something else wasn't mapped to the port in the meantime.
	joystick_reconnect();
}

// Remove a joystick.  Called during shutdown, and when running the SDL UI, on
// SDL_*DEVICEREMOVED events.  Non-SDL UIs using this module won't get the
// dynamic add/remove behaviour.

void sdl_js_device_removed(int index) {
	if (index < 0 || index >= num_devices) {
		return;
	}
	struct sdl_js_device *d = &devices[index];
	if (!d->valid) {
		return;
	}

	// Close device and mark invalid
	if (d->is_gamecontroller) {
		SDL_GameControllerClose(d->handle.gamecontroller);
		d->handle.gamecontroller = NULL;
	} else {
		SDL_JoystickClose(d->handle.joystick);
		d->handle.joystick = NULL;
	}
	d->valid = 0;

	for (int i = 0; i < 3; ++i) {
		if (d->jc_names[i]) {
			struct joystick_config *jc = joystick_config_by_name(d->jc_names[i]);
			LOG_MOD_SUB_DEBUG(1, "sdl", "joystick", "removing: %s\n", jc->description);
			joystick_config_remove(jc);
			free(d->jc_names[i]);
			d->jc_names[i] = NULL;
		}
	}
	joystick_config_update_menus();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void sdl_js_physical_init(void) {
	// Initialising GAMECONTROLLER also initialises JOYSTICK.  We disable
	// events by default because, if used as a standalone module outside
	// SDL, nothing works.  I could have sworn it used to, but it's
	// possible I haven't tested this since SDL 1.2!  Instead we manually
	// call SDL_*Update() before polling, or the SDL UI can call
	// sdl_js_enable_events() to turn the events back on.

	SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
	SDL_GameControllerEventState(SDL_DISABLE);
	SDL_JoystickEventState(SDL_DISABLE);
	events_enabled = 0;

	num_devices = SDL_NumJoysticks();

	if (SDL_NumJoysticks() < 1) {
		LOG_MOD_SUB_DEBUG(1, "sdl", "joystick", "no devices found\n");
		return;
	}

	// Preallocate the device pointer array
	devices = xrealloc(devices, num_devices * sizeof(*devices));
	for (int i = 0; i < num_devices; ++i) {
		devices[i] = (struct sdl_js_device){0};
	}

	// Add all current devices
	for (int i = 0; i < num_devices; ++i) {
		sdl_js_device_added(i);
	}
}

void sdl_js_enable_events(void) {
	SDL_GameControllerEventState(SDL_ENABLE);
	SDL_JoystickEventState(SDL_ENABLE);
	events_enabled = 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct sdl_js_device *open_device(int joystick_index) {
	if (joystick_index >= num_devices) {
		return NULL;
	}

	struct sdl_js_device *d = &devices[joystick_index];

	// If the device is already open, just up its count and return it
	if (d->open_count) {
		++d->open_count;
		return d;
	}

	// Open as a controller?
	if (d->is_gamecontroller) {
		d->handle.gamecontroller = SDL_GameControllerOpen(joystick_index);
		if (d->handle.gamecontroller) {
			d->valid = 1;
			d->joystick_index = joystick_index;
			d->num_axes = SDL_CONTROLLER_AXIS_MAX;
			d->num_buttons = SDL_CONTROLLER_BUTTON_MAX;
			d->open_count = 1;
			return d;
		}
	}

	// If that failed, open as a joystick
	d->handle.joystick = SDL_JoystickOpen(joystick_index);
	if (!d->handle.joystick)
		return NULL;
	d->valid = 1;
	d->is_gamecontroller = 0;
	d->joystick_index = joystick_index;
	d->open_count = 1;
	return d;
}

static void close_device(struct sdl_js_device *d) {
	if (d->open_count == 0)
		return;
	d->open_count--;
	if (d->open_count == 0) {
		if (d->is_gamecontroller) {
			SDL_GameControllerClose(d->handle.gamecontroller);
		} else {
			SDL_JoystickClose(d->handle.joystick);
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void debug_controls(struct sdl_js_device *d) {
	if (!d->debug_axes) {
		d->debug_axes = xmalloc(d->num_axes * sizeof(*d->debug_axes));
		for (int i = 0; i < d->num_axes; ++i) {
			d->debug_axes[i] = 32768;
		}
	}
	_Bool report = 0;
	for (int i = 0; i < d->num_axes; ++i) {
		unsigned v;
		if (d->is_gamecontroller) {
			v = SDL_GameControllerGetAxis(d->handle.gamecontroller, i) + 32768;
		} else {
			v = SDL_JoystickGetAxis(d->handle.joystick, i) + 32768;
		}
		if (d->debug_axes[i] != v) {
			report = 1;
			d->debug_axes[i] = v;
		}
	}
	{
		unsigned v = 0;
		for (int i = 0; i < d->num_buttons; ++i) {
			_Bool b;
			if (d->is_gamecontroller) {
				b = SDL_GameControllerGetButton(d->handle.gamecontroller, i);
			} else {
				b = SDL_JoystickGetButton(d->handle.joystick, i);
			}
			v |= (b << i);
		}
		if (d->debug_buttons != v) {
			report = 1;
			d->debug_buttons = v;
		}
	}
	if (report) {
		LOG_PRINT("JS%2d:", d->joystick_index);
		for (int i = 0; i < d->num_axes; ++i) {
			LOG_PRINT(" a%d: %5u", i, d->debug_axes[i]);
		}
		LOG_PRINT(" b: ");
		for (int i = 0; i < d->num_buttons; ++i) {
			LOG_PRINT("%c", (d->debug_buttons & (1 << i)) ? '1' : '0');
		}
		LOG_PRINT("\n");
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int sdl_js_axis_read(void *);
static int sdl_js_button_read(void *);
static void sdl_js_control_free(void *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Axis & button specs are basically the same, just track a different control
// index.  Buttons can be specified as a bitmask of available buttons with '%'
// - not very user-friendly; I'm only using that for auto-configured joystick
// profiles at the moment.
static struct sdl_js_control *configure_control(char *spec, unsigned control, _Bool buttons) {
	unsigned joystick = 0;
	_Bool inverted = 0;
	_Bool is_mask = 0;
	char *tmp = NULL;
	if (spec) {
		tmp = strsep(&spec, ",");
	}
	if (tmp && *tmp) {
		control = strtol(tmp, NULL, 0);
	}
	if (spec && *spec) {
		joystick = control;
		while (*spec == '-' || *spec == '%') {
			if (*spec == '-') {
				inverted = 1;
				++spec;
			}
			if (*spec == '%') {
				is_mask = 1;
				++spec;
			}
		}
		if (*spec) {
			control = strtol(spec, NULL, 0);
		}
	}

	struct sdl_js_device *d = open_device(joystick);
	if (!d) {
		return NULL;
	}

	struct sdl_js_control *c = xmalloc(sizeof(*c));
	*c = (struct sdl_js_control){0};
	c->device = d;
	c->inverted = inverted;
	if (buttons) {
		if (is_mask) {
			c->control.button_mask = control;
		} else {
			c->control.button_mask = (1 << control);
		}
	} else {
		c->control.axis = control;
	}
	return c;
}

static struct joystick_control *configure_physical_axis(char *spec, unsigned jaxis) {
	struct sdl_js_control *c = configure_control(spec, jaxis, 0);
	if (!c) {
		return NULL;
	}
	if (c->control.axis >= c->device->num_axes) {
		close_device(c->device);
		free(c);
		return NULL;
	}

	struct joystick_control *axis = &c->joystick_control;
	axis->read = DELEGATE_AS0(int, sdl_js_axis_read, axis);
	axis->free = DELEGATE_AS0(void, sdl_js_control_free, axis);

	return axis;
}

static struct joystick_control *configure_physical_button(char *spec, unsigned jbutton) {
	struct sdl_js_control *c = configure_control(spec, jbutton, 1);
	if (!c) {
		return NULL;
	}
	if (c->control.button_mask == 0) {
		close_device(c->device);
		free(c);
		return NULL;
	}

	struct joystick_control *button = &c->joystick_control;
	button->read = DELEGATE_AS0(int, sdl_js_button_read, button);
	button->free = DELEGATE_AS0(void, sdl_js_control_free, button);

	return button;
}

static int sdl_js_axis_read(void *sptr) {
	struct sdl_js_control *c = sptr;
	if (!c->device->valid) {
		return 32768;
	}
	if (!events_enabled && c->device->last_query != event_current_tick) {
		if (c->device->is_gamecontroller) {
			SDL_GameControllerUpdate();
		} else {
			SDL_JoystickUpdate();
		}
		c->device->last_query = event_current_tick;
	}
	if (logging.debug_ui & LOG_UI_JS_MOTION) {
		debug_controls(c->device);
	}
	unsigned ret;
	if (c->device->is_gamecontroller) {
		ret = SDL_GameControllerGetAxis(c->device->handle.gamecontroller, c->control.axis) + 32768;
	} else {
		ret = SDL_JoystickGetAxis(c->device->handle.joystick, c->control.axis) + 32768;
	}
	if (c->inverted)
		ret ^= 0xffff;
	return ret;
}

static int sdl_js_button_read(void *sptr) {
	struct sdl_js_control *c = sptr;
	if (!c->device->valid) {
		return 0;
	}
	if (!events_enabled && c->device->last_query != event_current_tick) {
		if (c->device->is_gamecontroller) {
			SDL_GameControllerUpdate();
		} else {
			SDL_JoystickUpdate();
		}
		c->device->last_query = event_current_tick;
	}
	unsigned v = 0;
	for (int i = 0; i < c->device->num_buttons; ++i) {
		_Bool b;
		if (c->device->is_gamecontroller) {
			b = SDL_GameControllerGetButton(c->device->handle.gamecontroller, i);
		} else {
			b = SDL_JoystickGetButton(c->device->handle.joystick, i);
		}
		v |= (b << i);
	}

	if (logging.debug_ui & LOG_UI_JS_MOTION) {
		debug_controls(c->device);
	}

	return v & c->control.button_mask;
}

static void sdl_js_control_free(void *sptr) {
	struct sdl_js_control *c = sptr;
	close_device(c->device);
	free(c);
}
