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

#include "top-config.h"

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_SDL2
#include <SDL.h>
#endif

#include "array.h"
#include "c-strcase.h"
#include "pl-string.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "joystick.h"
#include "logging.h"
#include "messenger.h"
#include "module.h"
#include "ui.h"
#include "vo.h"
#include "xroar.h"

#if defined(WINDOWS32)
#define JS_PLATFORM "Windows"
#elif defined(HAVE_COCOA)
#define JS_PLATFORM "Mac OS X"
#else
#define JS_PLATFORM "Linux"
#endif

extern struct joystick_module evdev_js_mod;
extern struct joystick_module joydev_js_mod;
extern struct joystick_module sdl_js_mod_exported;
extern struct joystick_module wasm_js_mod;
static struct joystick_module * const joystick_module_list[] = {
#ifdef HAVE_EVDEV
	&evdev_js_mod,
#endif
#ifdef HAVE_JOYDEV
	&joydev_js_mod,
#endif
#ifdef HAVE_SDL2
	&sdl_js_mod_exported,
#endif
#ifdef HAVE_WASM
	&wasm_js_mod,
#endif
	NULL
};

struct joystick_module * const *ui_joystick_module_list = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct joystick {
	const struct joystick_config *config;
	struct joystick_control *axes[JOYSTICK_NUM_AXES];
	struct joystick_control *buttons[JOYSTICK_NUM_BUTTONS];
};

// Messenger client ID
static int msgr_client_id = -1;

// Defined configurations
static struct slist *config_list = NULL;
static int next_id = 1;  // 0 is reserved to mean "no joystick"

// Current configuration assigned to each port
static struct joystick_config const *joystick_port_config[JOYSTICK_NUM_PORTS];

// Old config name for each port
static char *joystick_port_config_name[JOYSTICK_NUM_PORTS];

// Current joystick created for each port
static struct joystick *joystick_port[JOYSTICK_NUM_PORTS];

// Support the swap/cycle shortcuts:
static struct joystick_config const *virtual_joystick_config = NULL;
static struct joystick const *virtual_joystick = NULL;
static struct joystick_config const *cycled_config = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void joystick_config_free(struct joystick_config *jc);

static void joystick_ui_set_joystick_port(void *, int tag, void *smsg);
static void joystick_ui_set_joystick_cycle(void *, int tag, void *smsg);
static void joystick_map(const struct joystick_config *, unsigned port);
static void joystick_unmap(unsigned port);

static struct joystick *joystick_new_from_config(const struct joystick_config *);
static void joystick_free(struct joystick *);

static void init_submod(const char *submod_name);
static struct joystick_submodule *submod_by_name(const char *submod_name);
static struct joystick_submodule *select_submod(struct joystick_submodule *submod,
						char **spec);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void js_db_free(void);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Initialisation & shutdown

void joystick_init(void) {
	for (unsigned i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
		joystick_port_config[i] = NULL;
	}
	joystick_port_config_name[0] = xstrdup("joy0");
	joystick_port_config_name[1] = xstrdup("joy1");
	init_submod("physical");
	init_submod("mouse");
	init_submod("keyboard");
	msgr_client_id = messenger_client_register();
	ui_messenger_preempt_group(msgr_client_id, ui_tag_joystick_port, MESSENGER_NOTIFY_DELEGATE(joystick_ui_set_joystick_port, NULL));
	ui_messenger_preempt_group(msgr_client_id, ui_tag_joystick_cycle, MESSENGER_NOTIFY_DELEGATE(joystick_ui_set_joystick_cycle, NULL));
}

static void joystick_config_free_void(void *sptr) {
	joystick_config_free((struct joystick_config *)sptr);
}

void joystick_shutdown(void) {
	for (unsigned i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
		joystick_unmap(i);
	}
	messenger_client_unregister(msgr_client_id);
	slist_free_full(config_list, (slist_free_func)joystick_config_free_void);
	config_list = NULL;
	js_db_free();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Configuration profile management

struct joystick_config *joystick_config_new(void) {
	struct joystick_config *new = xmalloc(sizeof(*new));
	*new = (struct joystick_config){0};
	new->id = next_id++;
	config_list = slist_append(config_list, new);
	return new;
}

struct joystick_config *joystick_config_by_id(int id) {
	if (id == 0) {
		return NULL;
	}
	for (struct slist *l = config_list; l; l = l->next) {
		struct joystick_config *jc = l->data;
		if (jc->id == id) {
			return jc;
		}
	}
	return NULL;
}

struct joystick_config *joystick_config_by_name(const char *name) {
	if (!name) {
		return NULL;
	}
	for (struct slist *l = config_list; l; l = l->next) {
		struct joystick_config *jc = l->data;
		if (0 == strcmp(jc->name, name)) {
			return jc;
		}
	}
	for (struct slist *l = config_list; l; l = l->next) {
		struct joystick_config *jc = l->data;
		if (jc->alias && 0 == strcmp(jc->alias, name)) {
			return jc;
		}
	}
	return NULL;
}

void joystick_config_print_all(FILE *f, _Bool all) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct joystick_config *jc = l->data;
		fprintf(f, "joy %s\n", jc->name);
		xroar_cfg_print_inc_indent();
		xroar_cfg_print_string(f, all, "joy-desc", jc->description, NULL);
		for (int i = 0 ; i < JOYSTICK_NUM_AXES; i++) {
			if (jc->axis_specs[i]) {
				xroar_cfg_print_indent(f);
				sds str = sdsx_quote_str(jc->axis_specs[i]);
				fprintf(f, "joy-axis %d=%s\n", i, str);
				sdsfree(str);
			}
		}
		for (int i = 0 ; i < JOYSTICK_NUM_BUTTONS; i++) {
			if (jc->button_specs[i]) {
				xroar_cfg_print_indent(f);
				sds str = sdsx_quote_str(jc->button_specs[i]);
				fprintf(f, "joy-button %d=%s\n", i, str);
				sdsfree(str);
			}
		}
		xroar_cfg_print_dec_indent();
		fprintf(f, "\n");
	}
}

static void joystick_config_free(struct joystick_config *jc) {
	if (!jc) {
		return;
	}
	if (jc->name) {
		free(jc->name);
	}
	if (jc->alias) {
		free(jc->alias);
	}
	if (jc->description) {
		free(jc->description);
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_AXES; ++i) {
		if (jc->axis_specs[i]) {
			free(jc->axis_specs[i]);
		}
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; ++i) {
		if (jc->button_specs[i]) {
			free(jc->button_specs[i]);
		}
	}
	free(jc);
}

void joystick_config_remove(struct joystick_config *jc) {
	if (!jc) {
		return;
	}

	// Unmap it from any ports
	for (unsigned i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
		if (joystick_port_config[i] == jc) {
			joystick_unmap(i);
			LOG_MOD_DEBUG(1, "joystick", "port %u unplugged\n", i);
		}
	}

	// Remove config from list and free
	config_list = slist_remove(config_list, jc);
	joystick_config_free(jc);
}

void joystick_config_remove_by_id(int jsid) {
	joystick_config_remove(joystick_config_by_id(jsid));
}

void joystick_config_remove_by_name(const char *name) {
	joystick_config_remove(joystick_config_by_name(name));
}

void joystick_config_update_menus(void) {
	if (xroar.ui_interface) {
		DELEGATE_SAFE_CALL(xroar.ui_interface->update_joystick_menus);
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
		const struct joystick_config *jc = joystick_port_config[i];
		int id = jc ? jc->id : 0;
		ui_update_state(msgr_client_id, ui_tag_joystick_port, i, (void *)(intptr_t)id);
	}
}

struct slist *joystick_config_list(void) {
	return config_list;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Physical joysticks & gamepads

static struct {
	const char *name;
	int control;
} named_controls[] = {
	{ "leftx",      JS_AXIS_LEFTX },
	{ "lx",         JS_AXIS_LEFTX },
	{ "lefty",      JS_AXIS_LEFTY },
	{ "ly",         JS_AXIS_LEFTY },
	{ "rightx",     JS_AXIS_RIGHTX },
	{ "rx",         JS_AXIS_RIGHTX },
	{ "righty",     JS_AXIS_RIGHTY },
	{ "ry",         JS_AXIS_RIGHTY },
	{ "lefttrigger", JS_AXIS_LEFTTRIGGER },
	{ "lt",         JS_AXIS_LEFTTRIGGER },
	{ "righttrigger", JS_AXIS_RIGHTTRIGGER },
	{ "rt",         JS_AXIS_RIGHTTRIGGER },

	{ "a",          JS_BUTTON_A },
	{ "b",          JS_BUTTON_B },
	{ "x",          JS_BUTTON_X },
	{ "y",          JS_BUTTON_Y },
	{ "back",       JS_BUTTON_BACK },
	{ "guide",      JS_BUTTON_GUIDE },
	{ "start",      JS_BUTTON_START },
	{ "leftstick",  JS_BUTTON_LEFTSTICK },
	{ "lb",         JS_BUTTON_LEFTSTICK },
	{ "rightstick", JS_BUTTON_RIGHTSTICK },
	{ "rb",         JS_BUTTON_RIGHTSTICK },
	{ "leftshoulder", JS_BUTTON_LEFTSHOULDER },
	{ "ls",         JS_BUTTON_LEFTSHOULDER },
	{ "rightshoulder", JS_BUTTON_RIGHTSHOULDER },
	{ "rs",         JS_BUTTON_RIGHTSHOULDER },

	{ "dpup",       JS_DP_UP },
	{ "du",         JS_DP_UP },
	{ "dpdown",     JS_DP_DOWN },
	{ "dd",         JS_DP_DOWN },
	{ "dpleft",     JS_DP_LEFT },
	{ "dl",         JS_DP_LEFT },
	{ "dpright",    JS_DP_RIGHT },
	{ "dr",         JS_DP_RIGHT },
};

static int nmappings = 0;
static struct js_db_entry **mappings = NULL;
static struct js_db_entry *fallback_gamepad_mapping = NULL;
static struct js_db_entry *fallback_joystick_mapping = NULL;

static int hexdigit(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return (c - 'a') + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return (c - 'A') + 10;
	}
	return -1;
}

static _Bool string_to_guid(const char *str, uint8_t *guid) {
	for (int i = 0; i < 16; ++i) {
		if (!*str || !*(str+1)) {
			return 0;
		}
		int n0 = hexdigit(*(str++));
		int n1 = hexdigit(*(str++));
		if (n0 < 0 || n1 < 0) {
			return 0;
		}
		*(guid++) = (n0 << 4) | n1;
	}
	return 1;
}

static int cmp_control(const void *a, const void *b) {
	const struct js_db_control *aa = a;
	const struct js_db_control *bb = b;
	if (aa->index < bb->index) {
		return -1;
	}
	return (aa->index > bb->index);
}

static struct js_db_control *control_by_index(struct slist **list, int index) {
	assert(list != NULL);

	for (struct slist *iter = *list; iter; iter = iter->next) {
		struct js_db_control *control = iter->data;
		if (control->index == index) {
			return control;
		}
	}
	struct js_db_control *control = xmalloc(sizeof(*control));
	*control = (struct js_db_control){0};
	control->index = index;
	*list = slist_prepend(*list, control);
	return control;
}

static void js_db_map_free(struct js_db_entry *map);

static struct js_db_entry *js_parse_db_entry(const char *db_string) {
	char *dbcopy = xstrdup(db_string);
	char *dbnext = dbcopy;
	const char *guid_str = strsep(&dbnext, ",");
	const char *map_name = strsep(&dbnext, ",");

	struct js_db_entry *map = xmalloc(sizeof(*map));
	*map = (struct js_db_entry){0};

	if (!string_to_guid(guid_str, map->guid)) {
		memset(map->guid, 0, sizeof(map->guid));
		guid_str = "*";
	}
	map->crc = (map->guid[3] << 8) | map->guid[2];
	map->version = (map->guid[13] << 8) | map->guid[12];
	map->guid[2] = map->guid[3] = map->guid[12] = map->guid[13] = 0;
	map->name = xstrdup(map_name);

	char *dbctrl;
	while ((dbctrl = strsep(&dbnext, ","))) {
		char *input = dbctrl;
		char *ctl_name = strsep(&input, ":");
		if (!ctl_name || !*ctl_name || !input || !*input) {
			continue;
		}
		// Special cases
		if (0 == c_strcasecmp(ctl_name, "platform")) {
			if (0 != c_strcasecmp(input, JS_PLATFORM)) {
				js_db_map_free(map);
				free(dbcopy);
				return NULL;
			}
		} else if (0 == c_strcasecmp(ctl_name, "crc")) {
			map->crc = strtol(input, NULL, 16);
		}
		// Is this mapping direct, or to set a control low/high?
		int caction = JS_CONTROL_ACTION_DIRECT;
		while (*ctl_name) {
			if (*ctl_name == '-') {
				caction = JS_CONTROL_ACTION_LOW;
				++ctl_name;
			} else if (*ctl_name == '+') {
				caction = JS_CONTROL_ACTION_HIGH;
				++ctl_name;
			} else {
				break;
			}
		}
		for (unsigned i = 0; i < ARRAY_N_ELEMENTS(named_controls); ++i) {
			if (0 == c_strcasecmp(ctl_name, named_controls[i].name)) {
				int ctl = named_controls[i].control;
				int istate = JS_INPUT_STATE_DIRECT;
				while (*input) {
					if (*input == '-') {
						istate = JS_INPUT_STATE_LOW;
						++input;
					} else if (*input == '+') {
						istate = JS_INPUT_STATE_HIGH;
						++input;
					} else {
						break;
					}
				}

				struct js_db_control *ctl_map = NULL;
				char *subindex_str = NULL;
				int index = strtol(input + 1, &subindex_str, 10);
				int subindex = -1;
				if (subindex_str && *subindex_str == '.') {
					subindex = strtol(subindex_str + 1, NULL, 10);
				}
				if (*input == 'a') {
					ctl_map = control_by_index(&map->axes, index);
				} else if (*input == 'b') {
					ctl_map = control_by_index(&map->buttons, index);
				} else if (*input == 'h' && subindex >= 0) {
					index *= 2;
					switch (subindex) {
					default:
						continue;
					case 8:
						istate = JS_INPUT_STATE_LOW;
						break;
					case 2:
						istate = JS_INPUT_STATE_HIGH;
						break;
					case 1:
						++index;
						istate = JS_INPUT_STATE_LOW;
						break;
					case 4:
						++index;
						istate = JS_INPUT_STATE_HIGH;
						break;
					}
					ctl_map = control_by_index(&map->hats, index);
				}

				if (ctl_map) {
					ctl_map->input_state[istate].control = ctl;
					ctl_map->input_state[istate].action = caction;
				}
			}
		}
	}
	free(dbcopy);
	map->axes = slist_sort(map->axes, cmp_control);
	map->buttons = slist_sort(map->buttons, cmp_control);
	map->hats = slist_sort(map->hats, cmp_control);
	return map;
}

static struct js_db_entry *js_find_db_entry_by_guid(const uint8_t guid[16]) {
	uint8_t match_guid[16];
	memcpy(match_guid, guid, 16);
	uint16_t crc = (match_guid[3] << 8) | match_guid[2];
	uint16_t version = (match_guid[13] << 8) | match_guid[12];
	match_guid[2] = match_guid[3] = match_guid[12] = match_guid[13] = 0;

	int best_match_score = -1;
	struct js_db_entry *best_match = NULL;

	for (int i = 0; i < nmappings; ++i) {
		struct js_db_entry *map = mappings[i];
		if (0 == memcmp(map->guid, match_guid, 16)) {
			int match_score = (map->crc == crc) + (map->version == version);
			if (match_score > best_match_score) {
				best_match = map;
				best_match_score = match_score;
				if (match_score == 2) {
					// can't beat this
					break;
				}
			}
		}
	}
	return best_match;
}

static _Bool js_add_db_entry(const char *db_string) {
	struct js_db_entry *map = js_parse_db_entry(db_string);
	if (!map) {
		return 0;
	}

	_Bool count = 0;
	(void)count;  // avoid warning

#ifdef HAVE_SDL2
	// It's apparently safe to call this before SDL is initialised, so by
	// doing it here, we get a (more) consistent experience using the
	// command line option, rather than having to tell the user to set
	// environment variables.
	SDL_GameControllerAddMapping(db_string);
	count = 1;
#endif

#if !defined(HAVE_EVDEV) && !defined(HAVE_JOYDEV)
	// No point maintaining the list of mappings in memory if no module is
	// going to use it.
	js_db_map_free(map);
	return count;
#else
	// If this exactly matches an existing entry, overwrite it
	for (int i = 0; i < nmappings; ++i) {
		if (0 == memcmp(mappings[i]->guid, map->guid, 16) &&
		    mappings[i]->crc == map->crc &&
		    mappings[i]->version == map->version) {
			js_db_map_free(mappings[i]);
			mappings[i] = map;
			return 1;
		}
	}

	// Otherwise add to the end
	int next = nmappings++;
	mappings = xrealloc(mappings, nmappings * sizeof(*mappings));
	mappings[next] = map;
	return 1;
#endif
}

static void ensure_fallbacks_exist(void) {
	if (!fallback_gamepad_mapping) {
		fallback_gamepad_mapping = js_parse_db_entry("*,Fallback gamepad,a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,rightshoulder:b5,back:b6,start:b7,guide:b8,leftstick:b9,rightstick:b10,leftx:a0,lefty:a1,lefttrigger:a2,rightx:a3,righty:a4,righttrigger:a5,dpleft:h0.8,dpright:h0.2,dpup:h0.1,dpdown:h0.4,");
	}
	if (!fallback_joystick_mapping) {
		fallback_joystick_mapping = js_parse_db_entry("*,Fallback joystick,");
	}
}

void js_read_db_file(const char *filename) {
	ensure_fallbacks_exist();
	FILE *fd = fopen(filename, "r");
	if (!fd) {
		return;
	}
	sds line;
	int count = 0;
	while ((line = sdsx_fgets(fd))) {
		sds trimline = sdsx_trim_qe(sdsnew(line), NULL);
		sdsfree(line);
		if (!*trimline || *trimline == '#') {
			sdsfree(trimline);
			continue;
		}
		if (js_add_db_entry(trimline)) {
			++count;
		}
		sdsfree(trimline);
	}
	fclose(fd);
	LOG_MOD_DEBUG(1, "joystick", "loaded %d mapping%s from %s\n", count, (count == 1) ? "" : "s", filename);
}

struct js_db_entry *js_find_db_entry(const char *guid_str, int fallback) {
	struct js_db_entry *ent = NULL;
	uint8_t guid[16];
	if (string_to_guid(guid_str, guid)) {
		ent = js_find_db_entry_by_guid(guid);
	}
	if (ent) {
		return ent;
	}

	ensure_fallbacks_exist();

	switch (fallback) {
	case JS_DB_FALLBACK_GAMEPAD:
		return fallback_gamepad_mapping;
	case JS_DB_FALLBACK_JOYSTICK:
		return fallback_joystick_mapping;
	default:
		break;
	}
	return NULL;
}

static void js_db_map_free(struct js_db_entry *map) {
	if (map->name) {
		free(map->name);
	}
	slist_free_full(map->axes, free);
	slist_free_full(map->buttons, free);
	slist_free_full(map->hats, free);
	free(map);
}

static void js_db_free(void) {
	for (int i = 0; i < nmappings; ++i) {
		js_db_map_free(mappings[i]);
	}
	free(mappings);
	mappings = NULL;
	nmappings = 0;
	if (fallback_gamepad_mapping) {
		js_db_map_free(fallback_gamepad_mapping);
		fallback_gamepad_mapping = NULL;
	}
	if (fallback_joystick_mapping) {
		js_db_map_free(fallback_joystick_mapping);
		fallback_joystick_mapping = NULL;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Port mapping

static void joystick_ui_set_joystick_port(void *sptr, int tag, void *smsg) {
	(void)sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_joystick_port);
	int port = uimsg->value;
	int jsid = (intptr_t)uimsg->data;
	if (port < 0 || (unsigned)port >= JOYSTICK_NUM_PORTS) {
		LOG_MOD_WARN("joystick", "port %d out of range\n", port);
		uimsg->value = -1;
		return;
	}
	struct joystick_config *jc = joystick_config_by_id(jsid);
	joystick_map(jc, port);
	if (!jc) {
		uimsg->data = (void *)(intptr_t)0;
	}
}

static void joystick_ui_set_joystick_cycle(void *sptr, int tag, void *smsg) {
	(void)sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_joystick_cycle);

	// 0 means do-nothing.
	if (uimsg->value == 0) {
		return;
	}

	struct joystick_config const *tmp0 = joystick_port_config[0];
	struct joystick_config const *tmp1 = joystick_port_config[1];
	if (cycled_config == NULL &&
	    tmp0 != virtual_joystick_config && tmp1 != virtual_joystick_config) {
		cycled_config = virtual_joystick_config;
	}
	int port0_id = tmp0 ? tmp0->id : 0;
	int port1_id = tmp1 ? tmp1->id : 0;
	int cycled_id = cycled_config ? cycled_config->id : 0;

	if (uimsg->value == UI_NEXT) {
		// Cycle virtual joystick right, left, off
		ui_update_state(-1, ui_tag_joystick_port, 0, (void *)(intptr_t)cycled_id);
		ui_update_state(-1, ui_tag_joystick_port, 1, (void *)(intptr_t)port0_id);
		cycled_config = tmp1;
		return;
	} else if (uimsg->value == UI_PREV) {
		// Cycle virtual joystick left, right, off
		ui_update_state(-1, ui_tag_joystick_port, 0, (void *)(intptr_t)port1_id);
		ui_update_state(-1, ui_tag_joystick_port, 1, (void *)(intptr_t)cycled_id);
		cycled_config = tmp0;
		return;
	}

	// Any other value means swap joysticks
	ui_update_state(-1, ui_tag_joystick_port, 0, (void *)(intptr_t)port1_id);
	ui_update_state(-1, ui_tag_joystick_port, 1, (void *)(intptr_t)port0_id);
}

static void joystick_map(const struct joystick_config *jc, unsigned port) {
	if (port >= JOYSTICK_NUM_PORTS)
		return;
	if (joystick_port_config[port] == jc)
		return;
	if (joystick_port_config_name[port]) {
		free(joystick_port_config_name[port]);
		joystick_port_config_name[port] = NULL;
	}
	joystick_unmap(port);
	struct joystick *j = NULL;
	if (jc) {
		j = joystick_new_from_config(jc);
		joystick_port_config_name[port] = xstrdup(jc->name);
	}
	if (j) {
		const char *description = jc->description ? jc->description : jc->name;
		LOG_MOD_DEBUG(1, "joystick", "port %u = %s\n", port, description);
		joystick_port[port] = j;
		joystick_port_config[port] = jc;
	} else {
		LOG_MOD_DEBUG(1, "joystick", "port %u unplugged\n", port);
	}
}

static void joystick_unmap(unsigned port) {
	if (port >= JOYSTICK_NUM_PORTS)
		return;
	struct joystick *j = joystick_port[port];
	joystick_port_config[port] = NULL;
	joystick_port[port] = NULL;
	joystick_free(j);
}

void joystick_set_virtual(struct joystick_config const *jc) {
	unsigned remap_virtual = 0;
	if (virtual_joystick) {
		for (unsigned i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
			if (joystick_port[i] == virtual_joystick) {
				joystick_unmap(i);
				remap_virtual |= (1 << i);
			}
		}
	}
	virtual_joystick_config = jc;
	if (jc) {
		const char *description = jc->description ? jc->description : jc->name;
		LOG_MOD_DEBUG(1, "joystick", "virtual joystick = %s\n", description);
	} else {
		LOG_MOD_DEBUG(1, "joystick", "virtual joystick = None\n");
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
		if (remap_virtual & (1 << i)) {
			joystick_map(jc, i);
		}
	}
}

void joystick_reconnect(void) {
	for (int i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
		if (!joystick_port_config[i] && joystick_port_config_name[i]) {
			struct joystick_config *jc = joystick_config_by_name(joystick_port_config_name[i]);
			if (jc) {
				ui_update_state(-1, ui_tag_joystick_port, i, (void *)(intptr_t)jc->id);
			}
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Joystick creation

static struct joystick *joystick_new_from_config(const struct joystick_config *jc) {
	if (!jc) {
		return NULL;
	}
	struct joystick *j = xmalloc(sizeof(*j));
	*j = (struct joystick){0};
	j->config = jc;

	// We parse joystick specs here, so a config could still be invalid
	_Bool valid_joystick = 0;
	struct joystick_submodule *submod = NULL;
	for (unsigned i = 0; i < JOYSTICK_NUM_AXES; ++i) {
		if (!jc->axis_specs[i]) {
			continue;
		}
		char *spec_copy = xstrdup(jc->axis_specs[i]);
		char *spec = spec_copy;
		submod = select_submod(submod, &spec);
		if (!submod) {
			free(spec_copy);
			free(j);
			return NULL;
		}
		struct joystick_control *axis = submod->configure_axis(spec, i);
		j->axes[i] = axis;
		if (axis) {
			valid_joystick = 1;
		}
		free(spec_copy);
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; ++i) {
		if (!jc->button_specs[i])
			continue;
		char *spec_copy = xstrdup(jc->button_specs[i]);
		char *spec = spec_copy;
		submod = select_submod(submod, &spec);
		if (!submod) {
			free(spec_copy);
			free(j);
			return NULL;
		}
		struct joystick_control *button = submod->configure_button(spec, i);
		j->buttons[i] = button;
		if (button) {
			valid_joystick = 1;
		}
		free(spec_copy);
	}
	if (!valid_joystick) {
		free(j);
		return NULL;
	}

	return j;
}

static void joystick_free(struct joystick *j) {
	if (!j) {
		return;
	}
	for (unsigned a = 0; a < JOYSTICK_NUM_AXES; ++a) {
		struct joystick_control *axis = j->axes[a];
		if (axis) {
			DELEGATE_SAFE_CALL(axis->free);
		}
	}
	for (unsigned b = 0; b < JOYSTICK_NUM_BUTTONS; ++b) {
		struct joystick_control *button = j->buttons[b];
		if (button) {
			DELEGATE_SAFE_CALL(button->free);
		}
	}
	free(j);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Submodule handling

static void init_submod(const char *submod_name) {
	struct joystick_submodule *submod = submod_by_name(submod_name);
	if (submod && submod->init) {
		submod->init();
	}
}

static struct joystick_submodule *submod_by_name_in_modlist(struct joystick_module * const *list, const char *submod_name) {
	if (!list || !submod_name) {
		return NULL;
	}
	for (unsigned j = 0; list[j]; ++j) {
		struct joystick_module *module = list[j];
		for (unsigned i = 0; module->submodule_list[i]; ++i) {
			if (strcmp(module->submodule_list[i]->name, submod_name) == 0) {
				return module->submodule_list[i];
			}
		}
	}
	return NULL;
}

static struct joystick_submodule *submod_by_name(const char *submod_name) {
	struct joystick_submodule *submod;
	if ((submod = submod_by_name_in_modlist(ui_joystick_module_list, submod_name))) {
		return submod;
	}
	return submod_by_name_in_modlist(joystick_module_list, submod_name);
}

static struct joystick_submodule *select_submod(struct joystick_submodule *submod,
						char **spec) {
	char *submod_name = NULL;
	if (spec && *spec && strchr(*spec, ':')) {
		submod_name = strsep(spec, ":");
	}
	if (submod_name) {
		submod = submod_by_name(submod_name);
	} else if (!submod) {
		submod = submod_by_name("physical");
	}
	return submod;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Joystick reading

int joystick_read_axis(int port, int axis_index) {
	struct joystick *j = joystick_port[port];
	if (j && j->axes[axis_index]) {
		struct joystick_control *axis = j->axes[axis_index];
		return DELEGATE_CALL(axis->read);
	}
	return 32767;
}

static inline int read_button(int port, int button_index) {
	struct joystick *j = joystick_port[port];
	if (j && j->buttons[button_index]) {
		struct joystick_control *button = j->buttons[button_index];
		return DELEGATE_CALL(button->read);
	}
	return 0;
}

// Reads up to four buttons (one from each joystick).  The returned value is
// formatted to be easy to use with code for the Dragon/Coco1/2 (1 button per
// stick) or Coco3 (2 buttons per stick).

int joystick_read_buttons(void) {
	int buttons = 0;
	if (read_button(0, 0))
		buttons |= 1;
	if (read_button(0, 1))
		buttons |= 4;
	if (read_button(1, 0))
		buttons |= 2;
	if (read_button(1, 1))
		buttons |= 8;
	return buttons;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Mouse based virtual joystick

struct joystick_mouse_axis {
	struct joystick_control joystick_control;
	struct ui_interface *ui;
	int axis;
	_Bool active_area_relative;
	double offset;
	double scale;
};

struct joystick_mouse_button {
	struct joystick_control joystick_control;
	struct ui_interface *ui;
	int button;
};

static int joystick_read_mouse_axis(void *);
static int joystick_read_mouse_button(void *);

struct joystick_control *joystick_configure_mouse_axis(struct ui_interface *ui,
						       char *spec, unsigned jaxis) {
	if (jaxis >= 2)
		return NULL;

	struct joystick_mouse_axis *axis = xmalloc(sizeof(*axis));
	*axis = (struct joystick_mouse_axis){0};

	axis->ui = ui;
	axis->axis = jaxis;

	double aa_dim = (jaxis == 0) ? 256.0 : 192.0;

	double off0 = (jaxis == 0) ? 2.0 : 1.5;
	double off1 = (jaxis == 0) ? 254.0 : 190.5;

	if (spec) {
		char *next = NULL;
		double tmp = strtod(spec, &next);
		if (next != spec) {
			off0 = tmp;
			if (*next == ',') {
				++next;
			}
			spec = next;
			next = NULL;
			tmp = strtod(spec, &next);
			if (next != spec) {
				off1 = tmp;
			}
		}
	}

	// Avoid divide-by-zero
	if (fabs(off1 - off0) <= 1e-10) {
		off0 = 0.0;
		off1 = aa_dim;
	}

	axis->offset = off0 / aa_dim;
	axis->scale = aa_dim / (off1 - off0);

	axis->joystick_control.read = DELEGATE_AS0(int, joystick_read_mouse_axis, axis);
	axis->joystick_control.free = DELEGATE_AS0(void, free, axis);

	return &axis->joystick_control;
}

struct joystick_control *joystick_configure_mouse_button(struct ui_interface *ui,
							 char *spec, unsigned jbutton) {
	if (spec && *spec)
		jbutton = strtol(spec, NULL, 0) - 1;

	if (jbutton >= 3)
		return NULL;

	struct joystick_mouse_button *button = xmalloc(sizeof(*button));
	*button = (struct joystick_mouse_button){0};

	button->ui = ui;
	button->button = jbutton;

	button->joystick_control.read = DELEGATE_AS0(int, joystick_read_mouse_button, button);
	button->joystick_control.free = DELEGATE_AS0(void, free, button);

	return &button->joystick_control;
}

static int joystick_read_mouse_axis(void *sptr) {
	struct joystick_mouse_axis *axis = sptr;
	struct ui_interface *ui = axis->ui;
	struct vo_interface *vo = ui->vo_interface;
	struct vo_render *vr = vo->renderer;

	double pa_off, pa_dim;  // Picture offset, dimension
	double vp_dim;          // Viewport dimension
	double aa_off, aa_dim;  // Active area offset, dimension

	if (axis->axis == 0) {
		pa_off = vo->picture_area.x;
		pa_dim = vo->picture_area.w;
		vp_dim = vr->viewport.w;
		aa_dim = vr->active_area.w;
	} else {
		pa_off = vo->picture_area.y;
		pa_dim = vo->picture_area.h;
		vp_dim = vr->viewport.h;
		aa_dim = vr->active_area.h;
	}
	// Need to calculate active area offset
	aa_off = (vp_dim - aa_dim) / 2.;

	// Pointer's position within the picture area
	double pointer_par = (double)vo->mouse.axis[axis->axis] - pa_off;

	// Convert to viewport coordinates
	double pointer_vpr = (pointer_par * vp_dim) / (pa_dim - 1.);

	// Scale relative to active area
	double pointer_aar = (pointer_vpr - aa_off) / aa_dim;

	// Scale and offset according to axis configuration
	double v = (pointer_aar - axis->offset) * axis->scale;

	if (v < 0.0F) v = 0.0F;
	if (v > 1.0F) v = 1.0F;
	return (int)(v * 65535.);
}

static int joystick_read_mouse_button(void *sptr) {
	struct joystick_mouse_button *button = sptr;
	struct ui_interface *ui = button->ui;
	struct vo_interface *vo = ui->vo_interface;
	return vo->mouse.button[button->button];
}
