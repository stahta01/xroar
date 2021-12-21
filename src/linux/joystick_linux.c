/** \file
 *
 *  \brief Linux joystick module.
 *
 *  \copyright Copyright 2010-2021 Ciaran Anscomb
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

#include <fcntl.h>
#include <glob.h>
#include <linux/joystick.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "pl-string.h"
#include "slist.h"
#include "xalloc.h"

#include "events.h"
#include "joystick.h"
#include "logging.h"
#include "module.h"
#include "xroar.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);
static void unmap_axis(struct joystick_axis *axis);
static void unmap_button(struct joystick_button *button);
static void linux_js_print_physical(void);

static struct joystick_submodule linux_js_submod_physical = {
	.name = "physical",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
	.unmap_axis = unmap_axis,
	.unmap_button = unmap_button,
	.print_list = linux_js_print_physical,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_submodule *js_submodlist[] = {
	&linux_js_submod_physical,
	NULL
};

struct joystick_module linux_js_mod = {
	.common = { .name = "linux", .description = "Linux joystick input" },
	.submodule_list = js_submodlist,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct device {
	int joystick_index;
	int fd;
	unsigned open_count;
	unsigned num_axes;
	unsigned num_buttons;
	unsigned *axis_value;
	_Bool *button_value;
};

static struct slist *device_list = NULL;

struct control {
	struct device *device;
	unsigned control;
	_Bool inverted;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// For sorting joystick device filenames

static unsigned linux_js_prefix_len = 0;

static int linux_js_compar(const void *ap, const void *bp) {
	long a = strtol(*(const char **)ap + linux_js_prefix_len, NULL, 10);
	long b = strtol(*(const char **)bp + linux_js_prefix_len, NULL, 10);
	if (a < b)
		return -1;
	if (a == b)
		return 0;
	return 1;
}

// For now all this does is print out a list of joysticks.  I think I'll need
// to switch to the event interface before a consistent gamepad experience is
// possible.

static void linux_js_print_physical(void) {
	glob_t globbuf;
	globbuf.gl_offs = 0;
	linux_js_prefix_len = 13;
	glob("/dev/input/js*", GLOB_ERR|GLOB_NOSORT, NULL, &globbuf);
	if (!globbuf.gl_pathc) {
		linux_js_prefix_len = 7;
		glob("/dev/js*", GLOB_ERR|GLOB_NOSORT, NULL, &globbuf);
	}
	// Sort the list so we can spot removed devices
	qsort(globbuf.gl_pathv, globbuf.gl_pathc, sizeof(char *), linux_js_compar);
	// Now iterate
	LOG_PRINT("%-3s %-31s %-7s %-7s\n", "Idx", "Description", "Axes", "Buttons");
	for (unsigned i = 0; i < globbuf.gl_pathc; i++) {
		if (strlen(globbuf.gl_pathv[i]) < linux_js_prefix_len)
			continue;
		const char *index = globbuf.gl_pathv[i] + linux_js_prefix_len;
		int fd  = open(globbuf.gl_pathv[i], O_RDONLY|O_NONBLOCK);
		if (fd < 0) {
			continue;
		}
		LOG_PRINT("%-3s ", index);
		char buf[32];
		if (ioctl(fd, JSIOCGNAME(sizeof(buf)), buf) >= 0) {
			buf[31] = 0;
			LOG_PRINT("%-31s ", buf);
		}
		if (ioctl(fd, JSIOCGAXES, buf) < 0)
			buf[0] = 0;
		LOG_PRINT("%-7d ", (int)buf[0]);
		if (ioctl(fd, JSIOCGBUTTONS, buf) < 0)
			buf[0] = 0;
		LOG_PRINT("%-7d\n", (int)buf[0]);
		close(fd);
	}
	globfree(&globbuf);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct device *open_device(int joystick_index) {
	// If the device is already open, just up its count and return it
	for (struct slist *iter = device_list; iter; iter = iter->next) {
		struct device *d = iter->data;
		if (d->joystick_index == joystick_index) {
			d->open_count++;
			return d;
		}
	}
	char buf[33];
	int fd;
	// Try /dev/input/jsN and /dev/jsN
	snprintf(buf, sizeof(buf), "/dev/input/js%d", joystick_index);
	fd = open(buf, O_NONBLOCK|O_RDONLY);
	if (fd < 0) {
		snprintf(buf, sizeof(buf), "/dev/js%d", joystick_index);
		fd = open(buf, O_NONBLOCK|O_RDONLY);
	}
	if (fd < 0)
		return NULL;
	struct device *d = xmalloc(sizeof(*d));
	d->joystick_index = joystick_index;
	d->fd = fd;
	char tmp;
	ioctl(fd, JSIOCGAXES, &tmp);
	d->num_axes = tmp;
	ioctl(fd, JSIOCGBUTTONS, &tmp);
	d->num_buttons = tmp;
	if (d->num_axes > 0) {
		d->axis_value = xmalloc(d->num_axes * sizeof(*d->axis_value));
		for (unsigned i = 0; i < d->num_axes; i++)
			d->axis_value[i] = 0;
	}
	if (d->num_buttons > 0) {
		d->button_value = xmalloc(d->num_buttons * sizeof(*d->button_value));
		for (unsigned i = 0; i < d->num_buttons; i++)
			d->button_value[i] = 0;
	}
	char namebuf[128];
	ioctl(fd, JSIOCGNAME(sizeof(namebuf)), namebuf);
	LOG_DEBUG(1, "Opened joystick %d: %s\n", joystick_index, namebuf);
	LOG_DEBUG(1, "\t%u axes, %u buttons\n", d->num_axes, d->num_buttons);
	d->open_count = 1;
	device_list = slist_prepend(device_list, d);
	return d;
}

static void close_device(struct device *d) {
	d->open_count--;
	if (d->open_count == 0) {
		close(d->fd);
		free(d->axis_value);
		free(d->button_value);
		device_list = slist_remove(device_list, d);
		free(d);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void poll_devices(void) {
	for (struct slist *iter = device_list; iter; iter = iter->next) {
		struct device *d = iter->data;
		struct js_event e;
		while (read(d->fd, &e, sizeof(e)) == sizeof(e)) {
			switch (e.type) {
			case JS_EVENT_AXIS:
				if (e.number < d->num_axes) {
					d->axis_value[e.number] = (e.value) + 32768;
				}
				break;
			case JS_EVENT_BUTTON:
				if (e.number < d->num_buttons) {
					d->button_value[e.number] = e.value;
				}
				break;
			default:
				break;
			}
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static unsigned read_axis(struct control *c) {
	poll_devices();
	unsigned ret = c->device->axis_value[c->control];
	if (c->inverted)
		ret ^= 0xffff;
	return ret;
}

static _Bool read_button(struct control *c) {
	poll_devices();
	return c->device->button_value[c->control];
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// axis & button specs are basically the same, just track a different
// "selected" variable.
static struct control *configure_control(char *spec, unsigned control) {
	unsigned joystick = 0;
	_Bool inverted = 0;
	char *tmp = NULL;
	if (spec)
		tmp = strsep(&spec, ",");
	if (tmp && *tmp) {
		control = strtol(tmp, NULL, 0);
	}
	if (spec && *spec) {
		joystick = control;
		if (*spec == '-') {
			inverted = 1;
			spec++;
		}
		if (*spec) {
			control = strtol(spec, NULL, 0);
		}
	}
	struct device *d = open_device(joystick);
	if (!d)
		return NULL;
	struct control *c = xmalloc(sizeof(*c));
	c->device = d;
	c->control = control;
	c->inverted = inverted;
	return c;
}

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
	struct control *c = configure_control(spec, jaxis);
	if (!c)
		return NULL;
	if (c->control >= c->device->num_axes) {
		close_device(c->device);
		free(c);
		return NULL;
	}
	struct joystick_axis *axis = xmalloc(sizeof(*axis));
	axis->read = (js_read_axis_func)read_axis;
	axis->data = c;
	return axis;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	struct control *c = configure_control(spec, jbutton);
	if (!c)
		return NULL;
	if (c->control >= c->device->num_buttons) {
		close_device(c->device);
		free(c);
		return NULL;
	}
	struct joystick_button *button = xmalloc(sizeof(*button));
	button->read = (js_read_button_func)read_button;
	button->data = c;
	return button;
}

static void unmap_axis(struct joystick_axis *axis) {
	if (!axis)
		return;
	struct control *c = axis->data;
	close_device(c->device);
	free(c);
	free(axis);
}

static void unmap_button(struct joystick_button *button) {
	if (!button)
		return;
	struct control *c = button->data;
	close_device(c->device);
	free(c);
	free(button);
}
