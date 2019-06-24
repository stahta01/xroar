/*

Dragon joysticks

Copyright 2003-2017 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_JOYSTICK_H_
#define XROAR_JOYSTICK_H_

#include <stdio.h>

#include "module.h"

struct slist;

// Each joystick module contains a list of submodules, with standard names:
//
// physical - reads from a real joystick (may be standalone)
// keyboard - keypresses simulate joystick (provided by UI)
// mouse    - mouse position maps to joystick position (provided by UI)

// Unlike other types of module, the joystick module list in a UI definition
// does not override the default, it supplements it.  Submodules are searched
// for in both lists.  This allows both modules that can exist standalone and
// modules that require a specific active UI to be available.

struct joystick_submodule;

struct joystick_module {
	struct module common;
	struct joystick_submodule **submodule_list;
};

// Specs are of the form [[MODULE:]INTERFACE:]CONTROL-SPEC.

// The CONTROL-SPEC will vary by submodule:
//
// Interface    Axis spec                       Button spec
// physical     DEVICE-NUMBER,AXIS-NUMBER       DEVICE-NUMER,BUTTON-NUMBER
// keyboard     KEY-NAME0,KEY-NAME1             KEY-NAME
// mouse        SCREEN0,SCREEN1                 BUTTON-NUMBER
//
// DEVICE-NUMBER - a physical joystick index, order will depend on the OS
// AXIS-NUMBER, BUTTON-NUMBER - index of relevant control on device
// 0,1 - Push (left,up) or (right,down)
// KEY-NAME - will (currently) depend on the underlying toolkit
// SCREEN - coordinates define bounding box for mouse-to-joystick mapping

#define JOYSTICK_NUM_PORTS (2)
#define JOYSTICK_NUM_AXES (2)
#define JOYSTICK_NUM_BUTTONS (2)

struct joystick_config {
	char *name;
	char *description;
	unsigned id;
	char *axis_specs[JOYSTICK_NUM_AXES];
	char *button_specs[JOYSTICK_NUM_BUTTONS];
};

extern struct joystick_config const *joystick_port_config[JOYSTICK_NUM_PORTS];
extern struct joystick_module * const *ui_joystick_module_list;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Interfaces will define one of these for reading each control, plus
// associated context data:
typedef unsigned (*js_read_axis_func)(void *);
typedef _Bool (*js_read_button_func)(void *);

struct joystick_axis {
	js_read_axis_func read;
	void *data;
	struct joystick_submodule *submod;
};

struct joystick_button {
	js_read_button_func read;
	void *data;
	struct joystick_submodule *submod;
};

struct joystick_submodule {
	const char *name;
	struct joystick_axis *(* const configure_axis)(char *spec, unsigned jaxis);
	struct joystick_button *(* const configure_button)(char *spec, unsigned jbutton);
	void (* const unmap_axis)(struct joystick_axis *);
	void (* const unmap_button)(struct joystick_button *);
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct joystick_config *joystick_config_new(void);
struct joystick_config *joystick_config_by_id(unsigned i);
struct joystick_config *joystick_config_by_name(const char *name);
void joystick_config_print_all(FILE *f, _Bool all);
_Bool joystick_config_remove(const char *name);
struct slist *joystick_config_list(void);

void joystick_init(void);
void joystick_shutdown(void);

void joystick_map(struct joystick_config const *, unsigned port);
void joystick_unmap(unsigned port);
void joystick_set_virtual(struct joystick_config const *);
void joystick_swap(void);
void joystick_cycle(void);

int joystick_read_axis(int port, int axis);
int joystick_read_buttons(void);

#endif
