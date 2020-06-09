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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <stdlib.h>
#include <string.h>

#include "pl-string.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "joystick.h"
#include "logging.h"
#include "module.h"
#include "ui.h"
#include "xroar.h"

extern struct joystick_module linux_js_mod;
extern struct joystick_module sdl_js_mod_exported;
static struct joystick_module * const joystick_module_list[] = {
#ifdef HAVE_LINUX_JOYSTICK
	&linux_js_mod,
#endif
#ifdef HAVE_SDL
	&sdl_js_mod_exported,
#endif
	NULL
};

struct joystick_module * const *ui_joystick_module_list = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct slist *config_list = NULL;
static unsigned next_id = 0;

// Current configuration, per-port:
struct joystick_config const *joystick_port_config[JOYSTICK_NUM_PORTS];

static struct joystick_submodule *selected_interface = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct joystick {
	struct joystick_axis *axes[JOYSTICK_NUM_AXES];
	struct joystick_button *buttons[JOYSTICK_NUM_BUTTONS];
};

static struct joystick *joystick_port[JOYSTICK_NUM_PORTS];

// Support the swap/cycle shortcuts:
static struct joystick_config const *virtual_joystick_config;
static struct joystick const *virtual_joystick = NULL;
static struct joystick_config const *cycled_config = NULL;

static void joystick_config_free(struct joystick_config *jc);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void joystick_init(void) {
	for (unsigned p = 0; p < JOYSTICK_NUM_PORTS; p++) {
		joystick_port[p] = NULL;
	}
}

void joystick_shutdown(void) {
	for (unsigned p = 0; p < JOYSTICK_NUM_PORTS; p++) {
		joystick_unmap(p);
	}
	slist_free_full(config_list, (slist_free_func)joystick_config_free);
	config_list = NULL;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct joystick_config *joystick_config_new(void) {
	struct joystick_config *new = xmalloc(sizeof(*new));
	*new = (struct joystick_config){0};
	new->id = next_id;
	config_list = slist_append(config_list, new);
	next_id++;
	return new;
}

struct joystick_config *joystick_config_by_id(unsigned id) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct joystick_config *jc = l->data;
		if (jc->id == id)
			return jc;
	}
	return NULL;
}

struct joystick_config *joystick_config_by_name(const char *name) {
	if (!name) return NULL;
	for (struct slist *l = config_list; l; l = l->next) {
		struct joystick_config *jc = l->data;
		if (0 == strcmp(jc->name, name)) {
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
	if (jc->name)
		free(jc->name);
	if (jc->description)
		free(jc->description);
	for (int i = 0; i < JOYSTICK_NUM_AXES; i++) {
		if (jc->axis_specs[i]) {
			free(jc->axis_specs[i]);
			jc->axis_specs[i] = NULL;
		}
	}
	for (int i = 0; i < JOYSTICK_NUM_BUTTONS; i++) {
		if (jc->button_specs[i]) {
			free(jc->button_specs[i]);
			jc->button_specs[i] = NULL;
		}
	}
	free(jc);
}

_Bool joystick_config_remove(const char *name) {
	struct joystick_config *jc = joystick_config_by_name(name);
	if (!jc)
		return 0;
	config_list = slist_remove(config_list, jc);
	joystick_config_free(jc);
	return 1;
}

struct slist *joystick_config_list(void) {
	return config_list;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_submodule *find_if_in_mod(struct joystick_module *module, const char *if_name) {
	if (!module || !if_name)
		return NULL;
	for (unsigned i = 0; module->submodule_list[i]; i++) {
		if (0 == strcmp(module->submodule_list[i]->name, if_name))
			return module->submodule_list[i];
	}
	return NULL;
}

static struct joystick_submodule *find_if_in_modlist(struct joystick_module * const *list, const char *if_name) {
	if (!list || !if_name)
		return NULL;
	for (unsigned i = 0; list[i]; i++) {
		struct joystick_submodule *submod = find_if_in_mod(list[i], if_name);
		if (submod)
			return submod;
	}
	return NULL;
}

static struct joystick_submodule *find_if(const char *if_name) {
	struct joystick_submodule *submod;
	if ((submod = find_if_in_modlist(ui_joystick_module_list, if_name)))
		return submod;
	return find_if_in_modlist(joystick_module_list, if_name);
}

static void select_interface(char **spec) {
	char *mod_name = NULL;
	char *if_name = NULL;
	if (*spec && strchr(*spec, ':')) {
		if_name = strsep(spec, ":");
	}
	if (*spec && strchr(*spec, ':')) {
		mod_name = if_name;
		if_name = strsep(spec, ":");
	}
	if (mod_name) {
		struct joystick_module *m = (struct joystick_module *)module_select_by_arg((struct module * const *)ui_joystick_module_list, mod_name);
		if (!m) {
			m = (struct joystick_module *)module_select_by_arg((struct module * const *)joystick_module_list, mod_name);
		}
		selected_interface = find_if_in_mod(m, if_name);
	} else if (if_name) {
		selected_interface = find_if(if_name);
	} else if (!selected_interface) {
		selected_interface = find_if("physical");
	}
}

void joystick_map(struct joystick_config const *jc, unsigned port) {
	selected_interface = NULL;
	if (port >= JOYSTICK_NUM_PORTS)
		return;
	if (joystick_port_config[port] == jc)
		return;
	joystick_unmap(port);
	if (!jc)
		return;
	struct joystick *j = xmalloc(sizeof(*j));
	*j = (struct joystick){.axes={0}};
	_Bool valid_joystick = 0;
	for (unsigned i = 0; i < JOYSTICK_NUM_AXES; i++) {
		char *spec_copy = xstrdup(jc->axis_specs[i]);
		char *spec = spec_copy;
		select_interface(&spec);
		if (!selected_interface) {
			free(spec_copy);
			free(j);
			return;
		}
		struct joystick_axis *axis = selected_interface->configure_axis(spec, i);
		j->axes[i] = axis;
		if (axis) {
			axis->submod = selected_interface;
			valid_joystick = 1;
		}
		free(spec_copy);
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; i++) {
		char *spec_copy = xstrdup(jc->button_specs[i]);
		char *spec = spec_copy;
		select_interface(&spec);
		if (!selected_interface) {
			free(spec_copy);
			free(j);
			return;
		}
		struct joystick_button *button = selected_interface->configure_button(spec, i);
		j->buttons[i] = button;
		if (button) {
			button->submod = selected_interface;
			valid_joystick = 1;
		}
		free(spec_copy);
	}
	if (!valid_joystick) {
		free(j);
		return;
	}
	LOG_DEBUG(1, "Joystick port %u = %s [ ", port, jc->name);
	for (unsigned i = 0; i < JOYSTICK_NUM_AXES; i++) {
		if (j->axes[i])
			LOG_DEBUG(1, "%u=%s:", i, j->axes[i]->submod->name);
		LOG_DEBUG(1, ", ");
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; i++) {
		if (j->buttons[i])
			LOG_DEBUG(1, "%u=%s:", i, j->buttons[i]->submod->name);
		if ((i + 1) < JOYSTICK_NUM_BUTTONS)
			LOG_DEBUG(1, ", ");
	}
	LOG_DEBUG(1, " ]\n");
	joystick_port[port] = j;
	joystick_port_config[port] = jc;
}

void joystick_unmap(unsigned port) {
	if (port >= JOYSTICK_NUM_PORTS)
		return;
	struct joystick *j = joystick_port[port];
	joystick_port_config[port] = NULL;
	joystick_port[port] = NULL;
	if (!j)
		return;
	for (unsigned a = 0; a < JOYSTICK_NUM_AXES; a++) {
		struct joystick_axis *axis = j->axes[a];
		if (axis) {
			struct joystick_submodule *submod = axis->submod;
			if (submod->unmap_axis) {
				submod->unmap_axis(axis);
			} else {
				free(j->axes[a]);
				j->axes[a] = NULL;
			}
		}
	}
	for (unsigned b = 0; b < JOYSTICK_NUM_BUTTONS; b++) {
		struct joystick_button *button = j->buttons[b];
		if (button) {
			struct joystick_submodule *submod = button->submod;
			if (submod->unmap_button) {
				submod->unmap_button(button);
			} else {
				free(j->buttons[b]);
				j->buttons[b] = NULL;
			}
		}
	}
	free(j);
}

void joystick_set_virtual(struct joystick_config const *jc) {
	int remap_virtual_to = -1;
	if (virtual_joystick) {
		if (joystick_port[0] == virtual_joystick) {
			joystick_unmap(0);
			remap_virtual_to = 0;
		}
		if (joystick_port[1] == virtual_joystick) {
			joystick_unmap(1);
			remap_virtual_to = 1;
		}
	}
	virtual_joystick_config = jc;
	if (remap_virtual_to >= 0)
		joystick_map(jc, remap_virtual_to);
}

// Swap the right & left joysticks
void joystick_swap(void) {
	struct joystick_config const *tmp = joystick_port_config[0];
	joystick_map(joystick_port_config[1], 0);
	joystick_map(tmp, 1);
}

// Cycle the virtual joystick through right and left joystick ports
void joystick_cycle(void) {
	if (!virtual_joystick_config) {
		joystick_swap();
		return;
	}
	struct joystick_config const *tmp0 = joystick_port_config[0];
	struct joystick_config const *tmp1 = joystick_port_config[1];
	if (cycled_config == NULL &&
	    tmp0 != virtual_joystick_config && tmp1 != virtual_joystick_config) {
		cycled_config = virtual_joystick_config;
	}
	joystick_map(cycled_config, 0);
	joystick_map(tmp0, 1);
	cycled_config = tmp1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

int joystick_read_axis(int port, int axis) {
	struct joystick *j = joystick_port[port];
	if (j && j->axes[axis]) {
		return j->axes[axis]->read(j->axes[axis]->data);
	}
	return 32767;
}

int joystick_read_buttons(void) {
	int buttons = 0;
	if (joystick_port[0] && joystick_port[0]->buttons[0]) {
		if (joystick_port[0]->buttons[0]->read(joystick_port[0]->buttons[0]->data))
			buttons |= 1;
	}
	if (joystick_port[1] && joystick_port[1]->buttons[0]) {
		if (joystick_port[1]->buttons[0]->read(joystick_port[1]->buttons[0]->data))
			buttons |= 2;
	}
	return buttons;
}
