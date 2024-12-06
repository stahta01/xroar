/** \file
 *
 *  \brief Linux evdev joystick module.
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

#include <assert.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libevdev/libevdev.h>

#include "array.h"
#include "pl-string.h"
#include "sds.h"
#include "slist.h"
#include "xalloc.h"

#include "crc16.h"
#include "events.h"
#include "joystick.h"
#include "logging.h"
#include "module.h"
#include "xroar.h"

// https://www.kernel.org/doc/html/latest/input/gamepad.html

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void evdev_js_physical_init(void);
static struct joystick_control *configure_axis(char *, unsigned);
static struct joystick_control *configure_button(char *, unsigned);

static struct joystick_submodule evdev_js_submod_physical = {
	.name = "physical",
	.init = evdev_js_physical_init,
	.configure_axis = configure_axis,
	.configure_button = configure_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_submodule *js_submodlist[] = {
	&evdev_js_submod_physical,
	NULL
};

struct joystick_module evdev_js_mod = {
	.common = { .name = "evdev", .description = "Linux joystick input (evdev)" },
	.submodule_list = js_submodlist,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// We watch /dev/input using inotify for input devices being added or removed.
// This is the maximum size of an inotify event.

#define INOTIFY_EVENT_SIZE (sizeof(struct inotify_event) + NAME_MAX + 1)

#define CTL_TYPE_AXIS   (0)
#define CTL_TYPE_BUTTON (1)

struct evdev_js_device;

struct evdev_js_control {
	struct joystick_control joystick_control;

	struct evdev_js_device *device;
	union {
		int axis;
		unsigned button_mask;
	} control;
	_Bool inverted;
};

struct code_to_control {
	struct {
		int type;     // CTL_TYPE_*
		int control;  // indexed from 0
		int action;   // JS_CONTROL_ACTION_*
		_Bool state;  // previous threshold state, only for LOW/HIGH
	} input_state[NUM_JS_INPUT_STATES];

	struct {
		int offset;
		int scale;
	} abs;
};

struct evdev_js_device {
	struct evdev_js_context *ctx;

	int index;
	int evid;
	int fd;
	struct libevdev *levd;
	unsigned open_count;

	uint8_t guid[16];

	// Joystick config names auto-created against this device
	char *jc_names[3];

	// We only recognise D-pad key events as controlling axes if this is a
	// gamepad:
	_Bool is_gamepad;

	// Indexed from ABS_X, maps ABS_* evdev code to a control
	int nabs_codes;
	struct code_to_control *abs_code_to_control;

	// Either BTN_JOYSTICK or BTN_GAMEPAD
	int key_code_base;

	// Indexed from key_code_base, maps BTN_* evdev code to a control
	int nkey_codes;
	struct code_to_control *key_code_to_control;

	// Mapped-to axis controls
	int naxes;
	struct {
		int value_centre;
		int value;
	} *axis_controls;

	// Simple bitmap for mapped-to button controls
	int nbuttons;
	unsigned button_controls;
};

struct evdev_js_context {
	int inotify_fd;
	int watch_d;

	// Known input devices, one for each of /dev/input/event*.  Not
	// necessarily all joysticks under our control.
	int ndevices;
	struct evdev_js_device *devices;

	// State update event
	struct event ev_update;
};

// A special context kept on the stack by evdev_js_device_add() to track
// mapping state.

struct device_map_context {
	struct evdev_js_device *device;
	struct libevdev *levd;
	_Bool is_gamepad;

	// Arrays of flags indicating which controls are mapped to
	int naxis_flags;
	_Bool *axis_flags;
	int nbutton_flags;
	_Bool *button_flags;

	// Track the "next" input number for axes, hats, buttons.
	int anum;
	int hnum;
	int bnum;

	// Copy of the lists of mappings
	struct slist *mapped_axes;
	struct slist *mapped_hats;
	struct slist *mapped_buttons;

	// Temporary lists of inputs that are not in the mapping.  These lists
	// are iterated over to fill the gaps in control numbers found by
	// checking the above arrays.
	struct slist *extra_axes;
	struct slist *extra_hats;
	struct slist *extra_buttons;
};

static const char hexdigit[16] = "0123456789abcdef";

static struct evdev_js_context *global_evdev_js_context = NULL;

static void evdev_js_device_add(struct evdev_js_context *ctx, int evid);

static void evdev_js_update(void *);

static struct evdev_js_device *open_device(struct evdev_js_context *, int index);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Check that a device index is accessible by the user.

static _Bool device_access_ok(int evid) {
	sds path = sdscatprintf(sdsempty(), "/dev/input/event%d", evid);
	struct stat statbuf;
	if (stat(path, &statbuf) != 0) {
		sdsfree(path);
		return 0;
	}
	_Bool r = (access(path, R_OK) == 0);
	sdsfree(path);
	return r;
}

// For sorting joystick device filenames.

static int compar_device_path(const void *ap, const void *bp) {
	const char *ac = ap;
	const char *bc = bp;
	while (*ac && *bc && *ac == *bc) {
		ac++;
		bc++;
	}
	long a = strtol(ac, NULL, 10);
	long b = strtol(bc, NULL, 10);
	if (a < b)
		return -1;
	if (a == b)
		return 0;
	return 1;
}

// Initialise evdev physical joystick device support.  Sets up an inotify watch
// to spot device addition & removal, then iterates over current devices,
// adding any that look like a gamepad.

static void evdev_js_physical_init(void) {
	if (global_evdev_js_context)
		return;

	struct evdev_js_context *ctx = xmalloc(sizeof(*ctx));
	*ctx = (struct evdev_js_context){0};
	global_evdev_js_context = ctx;
	ctx->inotify_fd = -1;
	ctx->watch_d = -1;

	// Set up inotify to watch /dev/input
	ctx->inotify_fd = inotify_init1(IN_NONBLOCK);
	if (ctx->inotify_fd >= 0) {
		ctx->watch_d = inotify_add_watch(ctx->inotify_fd, "/dev/input",
						  IN_CREATE | IN_ATTRIB | IN_DELETE);
	}

	// The inotify watch and device events are polled each time a control
	// is accessed, but in case that doesn't happen for a while, we queue
	// an event to do so.  Each poll will requeue this event to fire in
	// 100ms.
	event_init(&ctx->ev_update, DELEGATE_AS0(void, evdev_js_update, ctx));
	ctx->ev_update.at_tick = event_current_tick + EVENT_MS(100);
	event_queue(&UI_EVENT_LIST, &ctx->ev_update);

	glob_t globbuf;
	globbuf.gl_offs = 0;
	const long unsigned prefix_len = 16;
	glob("/dev/input/event*", GLOB_ERR|GLOB_NOSORT, NULL, &globbuf);

	if (globbuf.gl_pathc > 0) {
		// Sort the list
		qsort(globbuf.gl_pathv, globbuf.gl_pathc, sizeof(char *), compar_device_path);

		// Now iterate
		for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
			if (strlen(globbuf.gl_pathv[i]) < prefix_len) {
				continue;
			}
			const char *index_str = globbuf.gl_pathv[i] + prefix_len;
			long evid = strtol(index_str, NULL, 10);
			if (evid < 0 || evid > INT_MAX) {
				continue;
			}
			if (device_access_ok(evid)) {
				evdev_js_device_add(ctx, evid);
			}
		}
	}
	globfree(&globbuf);

	if (ctx->ndevices == 0) {
		LOG_MOD_SUB_DEBUG(1, "evdev", "joystick", "no devices found\n");
	}
}

static int device_by_evid(const struct evdev_js_context *ctx, int evid) {
	for (int i = 0; i < ctx->ndevices; ++i) {
		if (ctx->devices[i].evid == evid) {
			return i;
		}
	}
	return -1;
}

static struct code_to_control *code_map_by_index(int *ncodes, struct code_to_control **codes,
						 int code) {
	if (code >= *ncodes) {
		*codes = xrealloc(*codes, (code + 1) * sizeof(**codes));
		while (*ncodes <= code) {
			struct code_to_control *ccmap = &((*codes)[*ncodes]);
			*ccmap = (struct code_to_control){0};
			for (int istate = 0; istate < NUM_JS_INPUT_STATES; ++istate) {
				ccmap->input_state[istate].control = -1;
			}
			++*ncodes;
		}
	}
	return &((*codes)[code]);
}

// Flag that a control (for the specified type) is mapped to.  Used so that
// unmapped inputs can "fill in the gaps".

static void set_control_flag(struct device_map_context *dmctx, int ctl_type, int ctl) {
	int *nelems = (ctl_type == CTL_TYPE_AXIS) ? &dmctx->naxis_flags : &dmctx->nbutton_flags;
	_Bool **elems = (ctl_type == CTL_TYPE_AXIS) ? &dmctx->axis_flags : &dmctx->button_flags;
	if (ctl >= *nelems) {
		*elems = xrealloc(*elems, (ctl + 1) * sizeof(**elems));
		while (*nelems <= ctl) {
			(*elems)[*nelems] = 0;
			++*nelems;
		}
	}
	(*elems)[ctl] = 1;
}

// Test whether a control (for the specified type) has been mapped to.

static _Bool have_control_flag(struct device_map_context *dmctx, int ctl_type, int ctl) {
	if (ctl_type == CTL_TYPE_AXIS) {
		return (ctl >= 0) && (ctl < dmctx->naxis_flags) && dmctx->axis_flags[ctl];
	}
	return (ctl >= 0) && (ctl < dmctx->nbutton_flags) && dmctx->button_flags[ctl];
}

// Test if a particular control (by type) is mapped to (without needing the
// mapping context, which only exists during initial device add).

static _Bool have_control(struct evdev_js_device *d, int ctl_type, int ctl) {
	if (ctl < 0) {
		return 0;
	}
	int nmaps = 0;
	struct code_to_control *maps = NULL;
	if (ctl_type == CTL_TYPE_AXIS) {
		nmaps = d->nabs_codes;
		maps = d->abs_code_to_control;
	} else if (ctl_type == CTL_TYPE_BUTTON) {
		nmaps = d->nkey_codes;
		maps = d->key_code_to_control;
	} else {
		return 0;
	}
	for (int i = 0; i < nmaps; ++i) {
		const struct code_to_control *ccmap = &maps[i];
		for (int j = 0; j < NUM_JS_INPUT_STATES; ++j) {
			if (ccmap->input_state[j].control == ctl) {
				return 1;
			}
		}
	}
	return 0;
}

// Verbosely log the actions of an input-to-control mapping.  Might be a bit
// _too_ verbose, but it's been quite helpful debugging the mapping process.

static void report_control(int ev_type, int ev_code, struct code_to_control *ccmap) {
	if (logging.level < 2) {
		return;
	}

	int typ = ccmap->input_state[JS_INPUT_STATE_DIRECT].type;
	int ctl = ccmap->input_state[JS_INPUT_STATE_DIRECT].control;
	int act = ccmap->input_state[JS_INPUT_STATE_DIRECT].action;
	int typ_low = ccmap->input_state[JS_INPUT_STATE_LOW].type;
	int ctl_low = ccmap->input_state[JS_INPUT_STATE_LOW].control;
	int act_low = ccmap->input_state[JS_INPUT_STATE_LOW].action;
	int typ_high = ccmap->input_state[JS_INPUT_STATE_HIGH].type;
	int ctl_high = ccmap->input_state[JS_INPUT_STATE_HIGH].control;
	int act_high = ccmap->input_state[JS_INPUT_STATE_HIGH].action;

	if (ev_type == EV_KEY) {
		printf("\tpressing button code 0x%03x ", ev_code);
		if (ctl < 0) {
			printf("has no effect\n");
			return;
		}
		if (typ == CTL_TYPE_AXIS) {
			if (act == JS_CONTROL_ACTION_LOW) {
				printf("pushes axis control %d low\n", ctl);
			} else if (act == JS_CONTROL_ACTION_HIGH) {
				printf("pushes axis control %d high\n", ctl);
			} else {
				printf("has no effect on axis control %d\n", ctl);
			}
		} else if (typ == CTL_TYPE_BUTTON) {
			printf("pushes button control %d\n", ctl);
		}
		return;
	}

	if (ev_code >= ABS_HAT0X && ev_code <= ABS_HAT3Y) {
		if (ctl >= 0) {
			printf("\tmoving hat code 0x%03x ", ev_code);
			if (typ == CTL_TYPE_AXIS) {
				printf("directly maps to axis control %d\n", ctl);
			} else if (typ == CTL_TYPE_BUTTON) {
				printf("has no effect on button control %d\n", ctl);
			}
		}

		if (ctl_low >= 0) {
			printf("\tpressing hat code 0x%03x low ", ev_code);
			if (typ_low == CTL_TYPE_AXIS) {
				if (act_low == JS_CONTROL_ACTION_LOW) {
					printf("pushes axis control %d low\n", ctl_low);
				} else if (act_low == JS_CONTROL_ACTION_HIGH) {
					printf("pushes axis control %d high\n", ctl_low);
				} else {
					printf("pushes axis control %d low\n", ctl_low);
				}
			} else if (typ_low == CTL_TYPE_BUTTON) {
				printf("pushes button control %d\n", ctl_low);
			}
		}

		if (ctl_high >= 0) {
			printf("\tpressing hat code 0x%03x high ", ev_code);
			if (typ_high == CTL_TYPE_AXIS) {
				if (act_high == JS_CONTROL_ACTION_LOW) {
					printf("pushes axis control %d low\n", ctl_high);
				} else if (act_high == JS_CONTROL_ACTION_HIGH) {
					printf("pushes axis control %d high\n", ctl_high);
				} else {
					printf("pushes axis control %d high\n", ctl_high);
				}
			} else if (typ_high == CTL_TYPE_BUTTON) {
				printf("pushes button control %d\n", ctl_high);
			}
		}

		if (ctl < 0 && ctl_low < 0 && ctl_high < 0) {
			printf("\tpressing hat code 0x%03x has no effect\n", ev_code);
		}

		return;
	}

	if (ctl >= 0) {
		printf("\tmoving axis code 0x%03x ", ev_code);
		if (typ == CTL_TYPE_AXIS) {
			printf("directly maps to axis control %d\n", ctl);
		} else if (typ == CTL_TYPE_BUTTON) {
			printf("has no effect on button control %d\n", ctl);
		}
	}

	if (ctl_low >= 0) {
		printf("\tmoving axis code 0x%03x low ", ev_code);
		if (typ_low == CTL_TYPE_AXIS) {
			if (act_low == JS_CONTROL_ACTION_LOW) {
				printf("pushes axis control %d low\n", ctl_low);
			} else if (act_low == JS_CONTROL_ACTION_HIGH) {
				printf("pushes axis control %d high\n", ctl_low);
			} else {
				printf("pushes axis control %d low\n", ctl_low);
			}
		} else if (typ_low == CTL_TYPE_BUTTON) {
			printf("pushes button control %d\n", ctl_low);
		}
	}

	if (ctl_high >= 0) {
		printf("\tmoving axis code 0x%03x high ", ev_code);
		if (typ_high == CTL_TYPE_AXIS) {
			if (act_high == JS_CONTROL_ACTION_LOW) {
				printf("pushes axis control %d low\n", ctl_high);
			} else if (act_high == JS_CONTROL_ACTION_HIGH) {
				printf("pushes axis control %d high\n", ctl_high);
			} else {
				printf("pushes axis control %d high\n", ctl_high);
			}
		} else if (typ_high == CTL_TYPE_BUTTON) {
			printf("pushes button control %d\n", ctl_high);
		}
	}

	if (ctl < 0 && ctl_low < 0 && ctl_high < 0) {
		printf("\tmoving axis code 0x%03x has no effect\n", ev_code);
	}
}

// Loops through a range of input codes expected for a specified event type
// (EV_ABS or EV_KEY) and applies mappings.  Remember dmctx is on the stack
// during evdev_js_device_add().

static void process_map(struct device_map_context *dmctx, int ev_type, int ev_code_min, int ev_code_max) {
	// Pick out useful mapping context data
	struct evdev_js_device *d = dmctx->device;
	struct libevdev *levd = dmctx->levd;

	// Determing which input code to control mapping we're updating based
	// on event type
	int *ninput_codes = (ev_type == EV_ABS) ? &d->nabs_codes : &d->nkey_codes;
	struct code_to_control **input_codes = (ev_type == EV_ABS) ?  &d->abs_code_to_control : &d->key_code_to_control;

	// Look through event input codes for this event type
	for (int ev_code = ev_code_min; ev_code <= ev_code_max; ++ev_code) {
		// Only process this input code if it is actually present
		if (!libevdev_has_event_code(levd, ev_type, ev_code)) {
			// Code not present; next input code
			continue;
		}

		// Determine input type from event type and code
		_Bool is_hat = (ev_type == EV_ABS) && (ev_code >= ABS_HAT0X && ev_code <= ABS_HAT3Y);
		_Bool is_axis = (ev_type == EV_ABS) && !is_hat;

		// Choose which mapping metadata we're going to update based on
		// this input type:

		// The mapping list copied from the mapping database
		struct slist **map_list = is_axis ? &dmctx->mapped_axes : (is_hat ? &dmctx->mapped_hats : &dmctx->mapped_buttons);
		// Temporary storage of input codes that didn't map to anything
		struct slist **extra_list = is_axis ? &dmctx->extra_axes : (is_hat ? &dmctx->extra_hats : &dmctx->extra_buttons);
		// Track the number of this type of input we've found.  We use
		// this number to determine if this input is mentioned in the
		// mapping.
		int *cnum = is_axis ? &dmctx->anum : (is_hat ? &dmctx->hnum : &dmctx->bnum);

		// If the next mapping in the (sorted) list the one for this input number,
		// take it off the list.
		struct js_db_control *input_map = NULL;
		if (*map_list) {
			struct js_db_control *tmp_map = (*map_list)->data;
			if (tmp_map->index == *cnum) {
				input_map = tmp_map;
				*map_list = slist_remove(*map_list, tmp_map);
			}
		}

		// If it wasn't (or the list is empty), stash this input code
		// on the extras list to be added later.
		if (!input_map) {
			*extra_list = slist_append(*extra_list, (void *)(intptr_t)ev_code);
			++(*cnum);
			continue;
		}

		// Create or update the input code to control mapping
		struct code_to_control *ccmap = code_map_by_index(ninput_codes, input_codes, ev_code - ev_code_min);

		// A mapping may specify the behaviour of an input in one of
		// three states:  directly mapped, active when low, or active
		// when high.
		for (int j = 0; j < NUM_JS_INPUT_STATES; ++j) {
			int ctl = input_map->input_state[j].control;
			if (!ctl) {
				// No mapping; next input state
				continue;
			}

			// Record that this axis or button control number
			// exists (so that we can fill in gaps in the mapping
			// with any extras later).
			if (ctl >= JS_AXIS_MIN && ctl <= JS_AXIS_MAX) {
				set_control_flag(dmctx, CTL_TYPE_AXIS, ctl - JS_AXIS_MIN);
			} else if (ctl >= JS_BUTTON_MIN && ctl <= JS_BUTTON_MAX) {
				set_control_flag(dmctx, CTL_TYPE_BUTTON, ctl - JS_BUTTON_MIN);
			}

			// Record non-dpad mappings
			if (ctl < JS_DP_MIN || ctl > JS_DP_MAX) {
				int typ = (ctl >= JS_AXIS_MIN && ctl <= JS_AXIS_MAX) ? CTL_TYPE_AXIS : CTL_TYPE_BUTTON;
				int zctl = ctl - ((typ == CTL_TYPE_BUTTON) ? JS_BUTTON_MIN : JS_AXIS_MIN);
				ccmap->input_state[j].type = typ;
				ccmap->input_state[j].control = zctl;
				ccmap->input_state[j].action = input_map->input_state[j].action;
				// Next input state
				continue;
			}

			// D-pads: for non-gamepads, any axis input mapped to
			// the d-pad will be added as a simple axis control
			// (later).  For gamepads we'll instead make the d-pad
			// another way of moving the left X & Y axes.

			if (!dmctx->is_gamepad && ev_type == EV_ABS) {
				dmctx->extra_axes = slist_append(dmctx->extra_axes, (void *)(intptr_t)ev_code);
			} else switch (ctl) {
			default:
			case JS_DP_LEFT:
				set_control_flag(dmctx, CTL_TYPE_AXIS, JS_AXIS_LEFTX - JS_AXIS_MIN);
				ccmap->input_state[j].type = CTL_TYPE_AXIS;
				ccmap->input_state[j].control = JS_AXIS_LEFTX - JS_AXIS_MIN;
				ccmap->input_state[j].action = JS_CONTROL_ACTION_LOW;
				break;
			case JS_DP_RIGHT:
				set_control_flag(dmctx, CTL_TYPE_AXIS, JS_AXIS_LEFTX - JS_AXIS_MIN);
				ccmap->input_state[j].type = CTL_TYPE_AXIS;
				ccmap->input_state[j].control = JS_AXIS_LEFTX - JS_AXIS_MIN;
				ccmap->input_state[j].action = JS_CONTROL_ACTION_HIGH;
				break;
			case JS_DP_UP:
				set_control_flag(dmctx, CTL_TYPE_AXIS, JS_AXIS_LEFTY - JS_AXIS_MIN);
				ccmap->input_state[j].type = CTL_TYPE_AXIS;
				ccmap->input_state[j].control = JS_AXIS_LEFTY - JS_AXIS_MIN;
				ccmap->input_state[j].action = JS_CONTROL_ACTION_LOW;
				break;
			case JS_DP_DOWN:
				set_control_flag(dmctx, CTL_TYPE_AXIS, JS_AXIS_LEFTY - JS_AXIS_MIN);
				ccmap->input_state[j].type = CTL_TYPE_AXIS;
				ccmap->input_state[j].control = JS_AXIS_LEFTY - JS_AXIS_MIN;
				ccmap->input_state[j].action = JS_CONTROL_ACTION_HIGH;
				break;
			}
		}
		// ABS inputs need offset and scale applied; record the values
		if (ev_type == EV_ABS) {
			ccmap->abs.offset = -libevdev_get_abs_minimum(levd, ev_code);
			ccmap->abs.scale = ccmap->abs.offset + libevdev_get_abs_maximum(levd, ev_code);
		}
		// Log mapping
		report_control(ev_type, ev_code, ccmap);
		// Next control number
		++(*cnum);
	}
}

static void evdev_js_device_add(struct evdev_js_context *ctx, int evid) {
	assert(evid >= 0);

	if (device_by_evid(ctx, evid) >= 0) {
		return;
	}

	// Open the device file
	sds path = sdscatprintf(sdsempty(), "/dev/input/event%d", evid);
	int fd = open(path, O_NONBLOCK|O_RDONLY);
	sdsfree(path);
	if (fd < 0) {
		return;
	}

	// Initialise evdev with device file
	struct libevdev *levd = NULL;
	if (libevdev_new_from_fd(fd, &levd) < 0) {
		close(fd);
		return;
	}

	_Bool is_gamepad = libevdev_has_event_code(levd, EV_KEY, BTN_GAMEPAD);
	_Bool is_joystick = !is_gamepad && libevdev_has_event_code(levd, EV_KEY, BTN_JOYSTICK);

	// Neither gamepad nor joystick: discuss.
	if (!is_gamepad && !is_joystick) {
		libevdev_free(levd);
		close(fd);
		return;
	}

	// Fetch metadata
	int bustype = libevdev_get_id_bustype(levd);
	int vendor = libevdev_get_id_vendor(levd);
	int product = libevdev_get_id_product(levd);
	int version = libevdev_get_id_version(levd);
	const char *joy_name = libevdev_get_name(levd);
	uint16_t crc = 0;
	if (joy_name && *joy_name) {
		crc = crc16_block(crc, (const uint8_t *)joy_name, strlen(joy_name));
	} else {
		joy_name = "Joystick";
	}

	// Construct SDL-like GUID
	uint8_t guid[16];
	memset(guid, 0, sizeof(guid));
	guid[0] = bustype & 0xff;
	guid[1] = (bustype >> 8) & 0xff;
	guid[2] = crc & 0xff;
	guid[3] = (crc >> 8) & 0xff;
	guid[4] = vendor & 0xff;
	guid[5] = (vendor >> 8) & 0xff;
	guid[8] = product & 0xff;
	guid[9] = (product >> 8) & 0xff;
	guid[12] = version & 0xff;
	guid[13] = (version >> 8) & 0xff;

	int index = -1;

	// Search closed devices for one that matches and reuse its index
	for (int i = 0; i < ctx->ndevices; ++i) {
		const struct evdev_js_device *d = &ctx->devices[i];
		if (d->fd >= 0) {
			continue;
		}
		if (memcmp(guid, d->guid, sizeof(guid)) == 0) {
			index = i;
			break;
		}
	}

	// If nothing to reuse, add a new one
	if (index < 0) {
		index = ctx->ndevices++;
		ctx->devices = xrealloc(ctx->devices, ctx->ndevices * sizeof(*ctx->devices));
		struct evdev_js_device *d = &ctx->devices[index];
		*d = (struct evdev_js_device){0};
		d->fd = -1;
		memcpy(d->guid, guid, sizeof(guid));
	}

	assert(index >= 0);

	char guid_str[33];
	{
		uint8_t *s = guid;
		char *p = guid_str;
		for (int i = 0; i < 16; ++i) {
			*(p++) = hexdigit[(*s >> 4) & 0x0f];
			*(p++) = hexdigit[(*s++) & 0x0f];
		}
		*p = 0;
	}

	struct evdev_js_device *d = &ctx->devices[index];

	d->ctx = ctx;
	d->index = index;
	d->evid = evid;

	struct js_db_entry *map = js_find_db_entry(guid_str, is_gamepad);

	// Hold a mapping context on the stack
	struct device_map_context dmctx = (struct device_map_context){0};
	dmctx.device = d;
	dmctx.levd = levd;
	dmctx.is_gamepad = is_gamepad;

	// Copy the lists of mappings into the context
	dmctx.mapped_axes = slist_copy(map->axes);
	dmctx.mapped_hats = slist_copy(map->hats);
	dmctx.mapped_buttons = slist_copy(map->buttons);

	// Gamepad button codes from BTN_GAMEPAD, else BTN_JOYSTICK
	d->key_code_base = is_gamepad ? BTN_GAMEPAD : BTN_JOYSTICK;

	if (logging.level >= 2) {
		LOG_MOD_SUB_PRINT("evdev", "joystick", "adding new device at index %d:\n", index);
		LOG_PRINT("\tGUID: %s\n", guid_str);
		if (vendor) {
			LOG_PRINT("\tVendor ID: 0x%04x\n", vendor);
		}
		if (product) {
			LOG_PRINT("\tProduct ID: 0x%04x\n", product);
		}
		if (version) {
			LOG_PRINT("\tProduct version: 0x%04x\n", version);
		}
		LOG_MOD_SUB_PRINT("evdev", "joystick", "processing mapping for '%s'\n", map->name);
		LOG_MOD_SUB_PRINT("evdev", "joystick", "finding axes (EV_ABS from code 0x%03x):\n", ABS_X);
	}
	process_map(&dmctx, EV_ABS, ABS_X, ABS_MAX - 1);

	// Tack extra_hats onto the end of extra_axes
	dmctx.extra_axes = slist_concat(dmctx.extra_axes, dmctx.extra_hats);
	dmctx.extra_hats = NULL;

	// Add extra axes (and hats), filling in any gaps in the mapping
	{
		int ctl = 0;
		while (dmctx.extra_axes) {
			while (have_control_flag(&dmctx, CTL_TYPE_AXIS, ctl)) {
				++ctl;
			}
			int ev_code = (intptr_t)dmctx.extra_axes->data;
			struct code_to_control *ccmap = code_map_by_index(&d->nabs_codes, &d->abs_code_to_control, ev_code);
			if (ev_code >= ABS_HAT0X && ev_code <= ABS_HAT3Y) {
				ccmap->input_state[JS_INPUT_STATE_LOW].type = CTL_TYPE_AXIS;
				ccmap->input_state[JS_INPUT_STATE_LOW].control = ctl;
				ccmap->input_state[JS_INPUT_STATE_LOW].action = JS_CONTROL_ACTION_LOW;
				ccmap->input_state[JS_INPUT_STATE_HIGH].type = CTL_TYPE_AXIS;
				ccmap->input_state[JS_INPUT_STATE_HIGH].control = ctl;
				ccmap->input_state[JS_INPUT_STATE_HIGH].action = JS_CONTROL_ACTION_HIGH;
			} else {
				ccmap->input_state[JS_INPUT_STATE_DIRECT].type = CTL_TYPE_AXIS;
				ccmap->input_state[JS_INPUT_STATE_DIRECT].control = ctl;
				ccmap->input_state[JS_INPUT_STATE_DIRECT].action = JS_CONTROL_ACTION_DIRECT;
			}
			ccmap->abs.offset = -libevdev_get_abs_minimum(levd, ev_code);
			ccmap->abs.scale = ccmap->abs.offset + libevdev_get_abs_maximum(levd, ev_code);
			set_control_flag(&dmctx, CTL_TYPE_AXIS, ctl);
			report_control(EV_ABS, ev_code, ccmap);
			dmctx.extra_axes = slist_remove(dmctx.extra_axes, dmctx.extra_axes->data);
		}
	}

	LOG_MOD_SUB_DEBUG(2, "evdev", "joystick", "finding buttons (EV_KEY from code 0x%03x):\n", d->key_code_base);
	process_map(&dmctx, EV_KEY, d->key_code_base, KEY_MAX - 1);

	// Add extra buttons, filling in any gaps in the mapping
	{
		int ctl = 0;
		while (dmctx.extra_buttons) {
			while (have_control_flag(&dmctx, CTL_TYPE_BUTTON, ctl)) {
				++ctl;
			}
			int ev_code = (intptr_t)dmctx.extra_buttons->data;
			struct code_to_control *ccmap = code_map_by_index(&d->nkey_codes, &d->key_code_to_control, ev_code - d->key_code_base);
			ccmap->input_state[JS_INPUT_STATE_DIRECT].type = CTL_TYPE_BUTTON;
			ccmap->input_state[JS_INPUT_STATE_DIRECT].control = ctl;
			ccmap->input_state[JS_INPUT_STATE_DIRECT].action = JS_CONTROL_ACTION_DIRECT;
			set_control_flag(&dmctx, CTL_TYPE_BUTTON, ctl);
			report_control(EV_KEY, ev_code, ccmap);
			dmctx.extra_buttons = slist_remove(dmctx.extra_buttons, dmctx.extra_buttons->data);
		}
	}

	// Count the actual number of axis controls present (for reporting),
	// and initialise them.
	int actual_naxes = 0;
	d->naxes = dmctx.naxis_flags;
	d->axis_controls = xmalloc(dmctx.naxis_flags * sizeof(*d->axis_controls));
	for (int ctl = 0; ctl < dmctx.naxis_flags; ++ctl) {
		if (have_control_flag(&dmctx, CTL_TYPE_AXIS, ctl)) {
			d->axis_controls[ctl].value = d->axis_controls[ctl].value_centre = 32256;
			++actual_naxes;
		}
	}
	if (dmctx.axis_flags) {
		free(dmctx.axis_flags);
	}

	// Count the actual number of button controls present (for reporting),
	// and initialise them.
	int actual_nbuttons = 0;
	d->nbuttons = dmctx.nbutton_flags;
	d->button_controls = 0;
	for (int ctl = 0; ctl < dmctx.nbutton_flags; ++ctl) {
		if (have_control_flag(&dmctx, CTL_TYPE_BUTTON, ctl)) {
			++actual_nbuttons;
		}
	}
	if (dmctx.button_flags) {
		free(dmctx.button_flags);
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
		LOG_MOD_SUB_DEBUG(1, "evdev", "joystick", "added: %s ", jc->description);
		LOG_DEBUG(1, "(%d %s, ", actual_naxes, (actual_naxes == 1) ? "axis" : "axes");
		LOG_DEBUG(1, "%d button%s)\n", actual_nbuttons, (actual_nbuttons == 1) ? "" : "s");
	}

	// Add separate left and right stick configs for gamepads if controls
	// exist for the right stick.
	if (have_control(d, CTL_TYPE_AXIS, 2) && have_control(d, CTL_TYPE_AXIS, 3)) {
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
		tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, 0);
		jc0->axis_specs[0] = xstrdup(tmp);
		sdsfree(tmp);
		tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, 1);
		jc0->axis_specs[1] = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "physical:%d,%%%d", index, (1 << 0) | (1 << 9));
		jc0->button_specs[0] = xstrdup(tmp);
		sdsfree(tmp);
		tmp = sdscatprintf(sdsempty(), "physical:%d,%%%d", index, (1 << 1));
		jc0->button_specs[1] = xstrdup(tmp);
		sdsfree(tmp);
		//
		LOG_MOD_SUB_DEBUG(1, "evdev", "joystick", "added: %s\n", jc0->description);

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
		tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, 2);
		jc1->axis_specs[0] = xstrdup(tmp);
		sdsfree(tmp);
		tmp = sdscatprintf(sdsempty(), "physical:%d,%d", index, 3);
		jc1->axis_specs[1] = xstrdup(tmp);
		sdsfree(tmp);
		//
		tmp = sdscatprintf(sdsempty(), "physical:%d,%%%d", index, (1 << 2) | (1 << 10));
		jc1->button_specs[0] = xstrdup(tmp);
		sdsfree(tmp);
		tmp = sdscatprintf(sdsempty(), "physical:%d,%%%d", index, (1 << 3));
		jc1->button_specs[1] = xstrdup(tmp);
		sdsfree(tmp);
		//
		LOG_MOD_SUB_DEBUG(1, "evdev", "joystick", "added: %s\n", jc1->description);
	}

	joystick_config_update_menus();

	close(fd);
	libevdev_free(levd);

	// This may reconnect joysticks that used this device previously, if
	// something else wasn't mapped to the port in the meantime.
	joystick_reconnect();
}

// Removing a device closes its file descriptor and flags it as invalid.  It
// does not free up its entry in the devices list, as if the same device
// reappears, we want it to occupy the same index.

static void evdev_js_device_remove(struct evdev_js_context *ctx, int evid) {
	int index = device_by_evid(ctx, evid);
	if (index < 0) {
		return;
	}
	struct evdev_js_device *d = &ctx->devices[index];

	for (int i = 0; i < 3; ++i) {
		if (d->jc_names[i]) {
			struct joystick_config *jc = joystick_config_by_name(d->jc_names[i]);
			LOG_MOD_SUB_DEBUG(1, "evdev", "joystick", "removing: %s\n", jc->description);
			joystick_config_remove(jc);
			free(d->jc_names[i]);
			d->jc_names[i] = NULL;
		}
	}
	joystick_config_update_menus();
	if (d->axis_controls) {
		free(d->axis_controls);
		d->axis_controls = NULL;
	}
	d->naxes = 0;

	d->evid = -1;
	if (d->levd) {
		libevdev_free(d->levd);
		close(d->fd);
		d->levd = NULL;
		d->fd = -1;
	}
}

void digital_action(struct evdev_js_device *d, int typ, int ctl, int act, _Bool v) {
	if (typ == CTL_TYPE_BUTTON) {
		if (v) {
			d->button_controls |= (1 << ctl);
		} else {
			d->button_controls &= ~(1 << ctl);
		}
	} else if (typ == CTL_TYPE_AXIS) {
		if (act == JS_CONTROL_ACTION_LOW) {
			if (v) {
				d->axis_controls[ctl].value = 0;
				d->axis_controls[ctl].value_centre = 32256;
			} else {
				d->axis_controls[ctl].value = d->axis_controls[ctl].value_centre;
			}
		} else if (act == JS_CONTROL_ACTION_HIGH) {
			if (v) {
				d->axis_controls[ctl].value = 65535;
				d->axis_controls[ctl].value_centre = 33280;
			} else {
				d->axis_controls[ctl].value = d->axis_controls[ctl].value_centre;
			}
		}
	}
}

static void evdev_js_device_update(struct evdev_js_device *d) {
	struct input_event event;
	if (!d->levd) {
		return;
	}
	_Bool report = 0;
	while (libevdev_has_event_pending(d->levd)) {
		if (libevdev_next_event(d->levd, LIBEVDEV_READ_FLAG_NORMAL, &event) != 0) {
			continue;
		}
		report = 1;

		if (event.type == EV_ABS) {
			int code = event.code - ABS_X;
			if (code >= 0 && code < d->nabs_codes) {
				struct code_to_control *ccmap = &d->abs_code_to_control[code];
				double vf = (double)(event.value + ccmap->abs.offset) / (double)ccmap->abs.scale;
				int v = vf * 65535.;
				int typ = ccmap->input_state[JS_INPUT_STATE_DIRECT].type;
				int ctl = ccmap->input_state[JS_INPUT_STATE_DIRECT].control;
				int typ_low = ccmap->input_state[JS_INPUT_STATE_LOW].type;
				int ctl_low = ccmap->input_state[JS_INPUT_STATE_LOW].control;
				int act_low = ccmap->input_state[JS_INPUT_STATE_LOW].action;
				int typ_high = ccmap->input_state[JS_INPUT_STATE_HIGH].type;
				int ctl_high = ccmap->input_state[JS_INPUT_STATE_HIGH].control;
				int act_high = ccmap->input_state[JS_INPUT_STATE_HIGH].action;
				if (ctl_low >= 0) {
					_Bool new_state = (v < 16384);
					if (ccmap->input_state[JS_INPUT_STATE_LOW].state != new_state) {
						digital_action(d, typ_low, ctl_low, act_low, new_state);
						ccmap->input_state[JS_INPUT_STATE_LOW].state = new_state;
					}
				}
				if (ctl_high >= 0) {
					_Bool new_state = (v >= 49152);
					if (ccmap->input_state[JS_INPUT_STATE_HIGH].state != new_state) {
						digital_action(d, typ_high, ctl_high, act_high, new_state);
						ccmap->input_state[JS_INPUT_STATE_HIGH].state = new_state;
					}
				}
				if (ctl >= 0 && typ == CTL_TYPE_AXIS) {
					d->axis_controls[ctl].value = v;
				}
			}
		} else {
			int code = event.code - d->key_code_base;
			if (code >= 0 && code < d->nkey_codes) {
				struct code_to_control *ccmap = &d->key_code_to_control[code];
				int typ = ccmap->input_state[JS_INPUT_STATE_DIRECT].type;
				int ctl = ccmap->input_state[JS_INPUT_STATE_DIRECT].control;
				int act = ccmap->input_state[JS_INPUT_STATE_DIRECT].action;
				if (ctl >= 0) {
					_Bool new_state = event.value;
					if (ccmap->input_state[JS_INPUT_STATE_DIRECT].state != new_state) {
						digital_action(d, typ, ctl, act, new_state);
						ccmap->input_state[JS_INPUT_STATE_DIRECT].state = new_state;
					}
				}
			}
		}
	}

	if (report && (logging.debug_ui & LOG_UI_JS_MOTION)) {
		LOG_PRINT("JS%2d:", d->index);
		for (int ctl = 0; ctl < d->naxes; ++ctl) {
			LOG_PRINT(" a%d: %5d", ctl, d->axis_controls[ctl].value);
		}
		LOG_PRINT(" b: ");
		for (int ctl = 0; ctl < d->nbuttons; ++ctl) {
			LOG_PRINT("%c", (d->button_controls & (1 << ctl)) ? '1' : '0');
		}
		LOG_PRINT("\n");
	}
}

// Check inotify for devices being added or removed, then read any pending
// events from each open device.

static void evdev_js_update(void *sptr) {
	struct evdev_js_context *ctx = sptr;
	(void)sptr;

	// We provide a buffer large enough for the maximum single event size,
	// but likely as not we'll end up getting multiple (smaller) events at
	// once, so we need to iterate.

	// Alignment attribute straight from the inotify() manpage.
	char buf[INOTIFY_EVENT_SIZE] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t len;
	while ((len = read(ctx->inotify_fd, buf, INOTIFY_EVENT_SIZE)) > 0) {
		char *ptr = buf;
		while (ptr < (buf + len)) {
			const struct inotify_event *event = (void *)ptr;
			ptr += sizeof(struct inotify_event) + event->len;
			if (0 != strncmp(event->name, "event", 5)) {
				continue;
			}
			int evid = (int)strtol(event->name + 5, NULL, 10);
			if (event->mask & IN_DELETE) {
				evdev_js_device_remove(ctx, evid);
			} else if (event->mask & IN_CREATE) {
				if (device_access_ok(evid)) {
					evdev_js_device_add(ctx, evid);
				}
			} else if (event->mask & IN_ATTRIB) {
				if (device_access_ok(evid)) {
					evdev_js_device_add(ctx, evid);
				} else {
					evdev_js_device_remove(ctx, evid);
				}
			}
		}
	}

	// Update state of each open device
	for (int i = 0; i < ctx->ndevices; ++i) {
		evdev_js_device_update(&ctx->devices[i]);
	}

	// Reschedule this to run again soon
	ctx->ev_update.at_tick = event_current_tick + EVENT_MS(100);
	event_queue(&UI_EVENT_LIST, &ctx->ev_update);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct evdev_js_device *open_device(struct evdev_js_context *ctx, int index) {
	if (index < 0 || index >= ctx->ndevices) {
		return NULL;
	}
	struct evdev_js_device *d = &ctx->devices[index];

	// If the device is already open, just up its count and return it
	if (d->fd >= 0) {
		++d->open_count;
		return d;
	}

	// Open the device file
	sds path = sdscatprintf(sdsempty(), "/dev/input/event%d", d->evid);
	int fd = open(path, O_NONBLOCK|O_RDONLY);
	sdsfree(path);
	if (fd < 0) {
		return NULL;
	}

	// Initialise evdev with device file
	struct libevdev *levd = NULL;
	if (libevdev_new_from_fd(fd, &levd) < 0) {
		close(fd);
		return NULL;
	}

	_Bool is_gamepad = libevdev_has_event_code(levd, EV_KEY, BTN_GAMEPAD);
	_Bool is_joystick = !is_gamepad && libevdev_has_event_code(levd, EV_KEY, BTN_JOYSTICK);

	// Neither gamepad nor joystick: discuss.
	if (!is_gamepad && !is_joystick) {
		libevdev_free(levd);
		close(fd);
		return NULL;
	}

	d->is_gamepad = is_gamepad;
	d->fd = fd;
	d->levd = levd;
	// Only reset open_count if it wasn't already "open"
	if (d->open_count == 0) {
		d->open_count = 1;
	}
	return d;
}

static void close_device(struct evdev_js_device *d) {
	assert(d->open_count > 0);
	d->open_count--;
	if (d->open_count == 0 && d->fd >= 0) {
		libevdev_free(d->levd);
		d->levd = NULL;
		close(d->fd);
		d->fd = -1;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int evdev_js_axis_read(void *);
static int evdev_js_button_read(void *);
static void evdev_js_control_free(void *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// axis & button specs are basically the same, just track a different
// "selected" variable.
static struct evdev_js_control *configure_control(char *spec, unsigned control, _Bool buttons) {
	unsigned joystick = 0;
	_Bool inverted = 0;
	_Bool is_mask = 0;
	const char *tmp = NULL;
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

	struct evdev_js_device *d = open_device(global_evdev_js_context, joystick);
	if (!d) {
		return NULL;
	}

	struct evdev_js_control *c = xmalloc(sizeof(*c));
	*c = (struct evdev_js_control){0};
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

static struct joystick_control *configure_axis(char *spec, unsigned jaxis) {
	struct evdev_js_control *c = configure_control(spec, jaxis, 0);
	if (!c) {
		return NULL;
	}
	if (!have_control(c->device, CTL_TYPE_AXIS, c->control.axis)) {
		close_device(c->device);
		free(c);
		return NULL;
	}

	struct joystick_control *axis = &c->joystick_control;
	axis->read = DELEGATE_AS0(int, evdev_js_axis_read, c);
	axis->free = DELEGATE_AS0(void, evdev_js_control_free, c);

	return axis;
}

static struct joystick_control *configure_button(char *spec, unsigned jbutton) {
	struct evdev_js_control *c = configure_control(spec, jbutton, 1);
	if (!c) {
		return NULL;
	}
	if (c->control.button_mask == 0) {
		close_device(c->device);
		free(c);
		return NULL;
	}
	struct joystick_control *button = &c->joystick_control;
	button->read = DELEGATE_AS0(int, evdev_js_button_read, c);
	button->free = DELEGATE_AS0(void, evdev_js_control_free, c);

	return button;
}

static int evdev_js_axis_read(void *sptr) {
	struct evdev_js_control *c = sptr;
	struct evdev_js_device *d = c->device;
	evdev_js_update(d->ctx);
	// device may now have been removed, so sanity check
	if (!d->levd) {
		return 32767;
	}
	unsigned ret = d->axis_controls[c->control.axis].value;
	if (c->inverted) {
		ret ^= 0xffff;
	}
	return (int)ret;
}

static int evdev_js_button_read(void *sptr) {
	struct evdev_js_control *c = sptr;
	struct evdev_js_device *d = c->device;
	evdev_js_update(d->ctx);
	// device may now have been removed, so sanity check
	if (!d->levd) {
		return 0;
	}
	return d->button_controls & c->control.button_mask;
}

static void evdev_js_control_free(void *sptr) {
	struct evdev_js_control *c = sptr;
	close_device(c->device);
	free(c);
}
