/** \file
 *
 *  \brief Joysticks.
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

#ifndef XROAR_JOYSTICK_H_
#define XROAR_JOYSTICK_H_

#include <stdio.h>

#include "delegate.h"

#include "module.h"

struct slist;
struct ui_interface;

typedef DELEGATE_S0(int) DELEGATE_T0(int);

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
	char *alias;  // "joyN", "joyN/l" or "joyN/r"
	char *description;
	int id;
	char *axis_specs[JOYSTICK_NUM_AXES];
	char *button_specs[JOYSTICK_NUM_BUTTONS];
};

extern struct joystick_module * const *ui_joystick_module_list;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct joystick_control {
	DELEGATE_T0(int) read;
	DELEGATE_T0(void) free;
};

struct joystick_submodule {
	const char *name;
	void (* const init)(void);
	struct joystick_control *(* const configure_axis)(char *spec, unsigned jaxis);
	struct joystick_control *(* const configure_button)(char *spec, unsigned jbutton);
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Physical joysticks & gamepads

enum js_control {
	JS_CONTROL_INVALID = 0,

	JS_AXIS_MIN = 1,
	JS_AXIS_LEFTX = JS_AXIS_MIN,
	JS_AXIS_LEFTY,
	JS_AXIS_RIGHTX,
	JS_AXIS_RIGHTY,
	JS_AXIS_LEFTTRIGGER,
	JS_AXIS_RIGHTTRIGGER,
	JS_AXIS_MAX = JS_AXIS_RIGHTTRIGGER,

	JS_BUTTON_MIN,
	JS_BUTTON_A = JS_BUTTON_MIN,
	JS_BUTTON_B,
	JS_BUTTON_X,
	JS_BUTTON_Y,
	JS_BUTTON_BACK,
	JS_BUTTON_GUIDE,
	JS_BUTTON_START,
	JS_BUTTON_LEFTSTICK,
	JS_BUTTON_RIGHTSTICK,
	JS_BUTTON_LEFTSHOULDER,
	JS_BUTTON_RIGHTSHOULDER,
	JS_BUTTON_MAX = JS_BUTTON_RIGHTSHOULDER,

	JS_DP_MIN,
	JS_DP_LEFT = JS_DP_MIN,
	JS_DP_RIGHT,
	JS_DP_UP,
	JS_DP_DOWN,
	JS_DP_MAX = JS_DP_DOWN,
};

#define JS_NUM_NAMED_AXES (6)
#define JS_NUM_NAMED_BUTTONS (11)

// The states interpreted for an input
enum {
	// Input value is used directly
	JS_INPUT_STATE_DIRECT = 0,
	// Input value is compared against a low threshold (axis inputs only)
	JS_INPUT_STATE_LOW,
	// Input value is compared against a high threshold (axis inputs only)
	JS_INPUT_STATE_HIGH,
	NUM_JS_INPUT_STATES
};

// The actions that the inputs perform on controls
enum {
	// Control mapped from input directly
	JS_CONTROL_ACTION_DIRECT = 0,
	// Control set low when input active (axis controls only)
	JS_CONTROL_ACTION_LOW,
	// Control set high when input active (axis controls only)
	JS_CONTROL_ACTION_HIGH,
	JS_NUM_CONTROL_ACTIONS
};

struct js_db_control {
	// Input index.  In the order in which they report themselves.
	int index;
	// A control/action pair for each input state.
	struct {
		int control;
		int action;
	} input_state[NUM_JS_INPUT_STATES];
};

struct js_db_entry {
	// GUID is stored with CRC and version portions zeroed, but we maintain
	// that data separately.  This lets us choose to do a strict or loose
	// match.
	uint8_t guid[16];
	uint16_t crc;
	uint16_t version;

	// For display purposes, the name stored in the mapping DB.
	char *name;

	// We store axis and button mappings in linked lists.  An allocated
	// mapping table would get blown out of proportion as some DB entries
	// suggest buttons into the hundreds.
	struct slist *axes;
	struct slist *buttons;

	// Hats are just specially-named axes.
	struct slist *hats;
};

#define JS_DB_FALLBACK_NONE     (-1)
#define JS_DB_FALLBACK_JOYSTICK  (0)
#define JS_DB_FALLBACK_GAMEPAD   (1)

void js_read_db_file(const char *filename);
struct js_db_entry *js_find_db_entry(const char *guid_str, int fallback);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Initialisation & shutdown

void joystick_init(void);
void joystick_shutdown(void);

// Configuration profile management

struct joystick_config *joystick_config_new(void);
struct joystick_config *joystick_config_by_id(int jsid);
struct joystick_config *joystick_config_by_name(const char *name);
void joystick_config_print_all(FILE *f, _Bool all);
void joystick_config_remove(struct joystick_config *);
void joystick_config_remove_by_id(int jsid);
void joystick_config_remove_by_name(const char *name);
void joystick_config_update_menus(void);
struct slist *joystick_config_list(void);

// Port mapping

void joystick_set_virtual(struct joystick_config const *);

// Reconnect joysticks

void joystick_reconnect(void);

// Joystick reading

int joystick_read_axis(int port, int axis);
int joystick_read_buttons(void);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Mouse based virtual joystick

struct joystick_control *joystick_configure_mouse_axis(struct ui_interface *,
						       char *spec, unsigned jaxis);
struct joystick_control *joystick_configure_mouse_button(struct ui_interface *,
							 char *spec, unsigned jbutton);

#endif
