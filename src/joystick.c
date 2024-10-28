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

extern struct joystick_module joydev_js_mod;
extern struct joystick_module sdl_js_mod_exported;
static struct joystick_module * const joystick_module_list[] = {
#ifdef HAVE_JOYDEV
	&joydev_js_mod,
#endif
#ifdef HAVE_SDL2
	&sdl_js_mod_exported,
#endif
	NULL
};

struct joystick_module * const *ui_joystick_module_list = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct joystick {
	const struct joystick_config *config;
	struct joystick_axis *axes[JOYSTICK_NUM_AXES];
	struct joystick_button *buttons[JOYSTICK_NUM_BUTTONS];
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

// Initialisation & shutdown

void joystick_init(void) {
	for (unsigned i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
		joystick_port_config[i] = NULL;
	}
	init_submod("physical");
	init_submod("mouse");
	init_submod("keyboard");
	msgr_client_id = messenger_client_register();
	ui_messenger_preempt_group(msgr_client_id, ui_tag_joystick_port, MESSENGER_NOTIFY_DELEGATE(joystick_ui_set_joystick_port, NULL));
	ui_messenger_preempt_group(msgr_client_id, ui_tag_joystick_cycle, MESSENGER_NOTIFY_DELEGATE(joystick_ui_set_joystick_cycle, NULL));
}

void joystick_shutdown(void) {
	for (unsigned i = 0; i < JOYSTICK_NUM_PORTS; ++i) {
		joystick_unmap(i);
	}
	messenger_client_unregister(msgr_client_id);
	slist_free_full(config_list, (slist_free_func)joystick_config_free);
	config_list = NULL;
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
			LOG_DEBUG(1, "[joystick] port %u unplugged\n", i);
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

struct slist *joystick_config_list(void) {
	return config_list;
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
		LOG_WARN("Joystick port %d out of range\n", port);
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
		LOG_DEBUG(1, "[joystick] port %u = %s\n", port, description);
		joystick_port[port] = j;
		joystick_port_config[port] = jc;
	} else {
		LOG_DEBUG(1, "[joystick] port %u unplugged\n", port);
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
		LOG_DEBUG(1, "[joystick] virtual joystick = %s\n", description);
	} else {
		LOG_DEBUG(1, "[joystick] virtual joystick = None\n");
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
		struct joystick_axis *axis = submod->configure_axis(spec, i);
		j->axes[i] = axis;
		if (axis) {
			if (!DELEGATE_DEFINED(axis->as_control.read)) {
				axis->submod = submod;
			}
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
		struct joystick_button *button = submod->configure_button(spec, i);
		j->buttons[i] = button;
		if (button) {
			if (!DELEGATE_DEFINED(button->as_control.read)) {
				button->submod = submod;
			}
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
		struct joystick_axis *axis = j->axes[a];
		struct joystick_control *control = &axis->as_control;
		if (axis) {
			if (DELEGATE_DEFINED(control->read)) {
				DELEGATE_SAFE_CALL(control->free);
			} else {
				struct joystick_submodule *submod = axis->submod;
				if (submod->unmap_axis) {
					submod->unmap_axis(axis);
				} else {
					free(j->axes[a]);
					j->axes[a] = NULL;
				}
			}
		}
	}
	for (unsigned b = 0; b < JOYSTICK_NUM_BUTTONS; ++b) {
		struct joystick_button *button = j->buttons[b];
		struct joystick_control *control = &button->as_control;
		if (button) {
			if (DELEGATE_DEFINED(control->read)) {
				DELEGATE_SAFE_CALL(control->free);
			} else {
				struct joystick_submodule *submod = button->submod;
				if (submod->unmap_button) {
					submod->unmap_button(button);
				} else {
					free(j->buttons[b]);
					j->buttons[b] = NULL;
				}
			}
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
		struct joystick_axis *axis = j->axes[axis_index];
		struct joystick_control *control = &axis->as_control;
		if (DELEGATE_DEFINED(control->read)) {
			return DELEGATE_CALL(control->read);
		} else {
			return axis->read(axis->data);
		}
	}
	return 32767;
}

static inline int read_button(int port, int button_index) {
	struct joystick *j = joystick_port[port];
	if (j && j->buttons[button_index]) {
		struct joystick_button *button = j->buttons[button_index];
		struct joystick_control *control = &button->as_control;
		if (DELEGATE_DEFINED(control->read)) {
			return DELEGATE_CALL(control->read);
		} else {
			return button->read(button->data);
		}
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

struct joystick_axis *joystick_configure_mouse_axis(struct ui_interface *ui,
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

	return (struct joystick_axis *)&axis->joystick_control;
}

struct joystick_button *joystick_configure_mouse_button(struct ui_interface *ui,
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

	return (struct joystick_button *)&button->joystick_control;
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
