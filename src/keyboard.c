/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "delegate.h"
#include "slist.h"
#include "xalloc.h"

#include "breakpoint.h"
#include "dkbd.h"
#include "events.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "xroar.h"

extern inline void keyboard_press_matrix(struct keyboard_interface *ki, int col, int row);
extern inline void keyboard_release_matrix(struct keyboard_interface *ki, int col, int row);
extern inline void keyboard_press(struct keyboard_interface *ki, int s);
extern inline void keyboard_release(struct keyboard_interface *ki, int s);

/* Current chording mode - only affects how backslash is typed: */
static enum keyboard_chord_mode chord_mode = keyboard_chord_mode_dragon_32k_basic;

struct keyboard_interface_private {
	struct keyboard_interface public;

	struct machine *machine;
	struct MC6809 *cpu;

	struct slist *basic_command_list;
	const char *basic_command;
};

static void type_command(void *);

static struct machine_bp basic_command_breakpoint[] = {
	BP_DRAGON_ROM(.address = 0xbbe5, .handler = DELEGATE_INIT(type_command, NULL) ),
	BP_COCO_BAS10_ROM(.address = 0xa1c1, .handler = DELEGATE_INIT(type_command, NULL) ),
	BP_COCO_BAS11_ROM(.address = 0xa1c1, .handler = DELEGATE_INIT(type_command, NULL) ),
	BP_COCO_BAS12_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(type_command, NULL) ),
	BP_COCO_BAS13_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(type_command, NULL) ),
	BP_MX1600_BAS_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(type_command, NULL) ),
};

struct keyboard_interface *keyboard_interface_new(struct machine *m) {
	struct keyboard_interface_private *kip = xmalloc(sizeof(*kip));
	*kip = (struct keyboard_interface_private){0};
	struct keyboard_interface *ki = &kip->public;
	kip->machine = m;
	kip->cpu = m->get_component(m, "CPU0");
	for (int i = 0; i < 8; i++) {
		ki->keyboard_column[i] = ~0;
		ki->keyboard_row[i] = ~0;
	}
	return ki;
}

void keyboard_interface_free(struct keyboard_interface *ki) {
	struct keyboard_interface_private *kip = (struct keyboard_interface_private *)ki;
	machine_bp_remove_list(kip->machine, basic_command_breakpoint);
	slist_free_full(kip->basic_command_list, (slist_free_func)free);
	free(kip);
}

void keyboard_set_keymap(struct keyboard_interface *ki, int map) {
	map %= NUM_KEYMAPS;
	xroar_machine_config->keymap = map;
	dkbd_map_init(&ki->keymap, map);
}

void keyboard_set_chord_mode(struct keyboard_interface *ki, enum keyboard_chord_mode mode) {
	chord_mode = mode;
	if (ki->keymap.layout == dkbd_layout_dragon) {
		if (mode == keyboard_chord_mode_dragon_32k_basic) {
			ki->keymap.unicode_to_dkey['\\'].dk_key = DSCAN_COMMA;
		} else {
			ki->keymap.unicode_to_dkey['\\'].dk_key = DSCAN_INVALID;
		}
	}
}

/* Compute sources & sinks based on inputs to the matrix and the current state
 * of depressed keys. */

void keyboard_read_matrix(struct keyboard_interface *ki, struct keyboard_state *state) {
	/* Ghosting: combine columns that share any pressed rows.  Repeat until
	 * no change in the row mask. */
	unsigned old;
	do {
		old = state->row_sink;
		for (int i = 0; i < 8; i++) {
			if (~state->row_sink & ~ki->keyboard_column[i]) {
				state->col_sink &= ~(1 << i);
				state->row_sink &= ki->keyboard_column[i];
			}
		}
	} while (old != state->row_sink);
	/* Likewise combining rows. */
	do {
		old = state->col_sink;
		for (int i = 0; i < 7; i++) {
			if (~state->col_sink & ~ki->keyboard_row[i]) {
				state->row_sink &= ~(1 << i);
				state->col_sink &= ki->keyboard_row[i];
			}
		}
	} while (old != state->col_sink);

	/* Sink & source any directly connected rows & columns */
	for (int i = 0; i < 8; i++) {
		if (!(state->col_sink & (1 << i))) {
			state->row_sink &= ki->keyboard_column[i];
		}
		if (state->col_source & (1 << i)) {
			state->row_source |= ~ki->keyboard_column[i];
		}
	}
	for (int i = 0; i < 7; i++) {
		if (!(state->row_sink & (1 << i))) {
			state->col_sink &= ki->keyboard_row[i];
		}
		if (state->row_source & (1 << i)) {
			state->col_source |= ~ki->keyboard_row[i];
		}
	}
}

void keyboard_unicode_press(struct keyboard_interface *ki, unsigned unicode) {
	if (unicode >= DKBD_U_TABLE_SIZE)
		return;
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_SHIFT)
		KEYBOARD_PRESS_SHIFT(ki);
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_UNSHIFT)
		KEYBOARD_RELEASE_SHIFT(ki);
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_CLEAR)
		KEYBOARD_PRESS_CLEAR(ki);
	keyboard_press(ki, ki->keymap.unicode_to_dkey[unicode].dk_key);
	return;
}

void keyboard_unicode_release(struct keyboard_interface *ki, unsigned unicode) {
	if (unicode >= DKBD_U_TABLE_SIZE)
		return;
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_SHIFT)
		KEYBOARD_RELEASE_SHIFT(ki);
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_UNSHIFT)
		KEYBOARD_PRESS_SHIFT(ki);
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_CLEAR)
		KEYBOARD_RELEASE_CLEAR(ki);
	keyboard_release(ki, ki->keymap.unicode_to_dkey[unicode].dk_key);
	return;
}

static void type_command(void *sptr) {
	struct keyboard_interface_private *kip = sptr;
	struct keyboard_interface *ki = &kip->public;
	struct MC6809 *cpu = kip->cpu;
	if (kip->basic_command) {
		int chr = *(kip->basic_command++);
		if (chr == '\\') {
			chr = *(kip->basic_command++);
			switch (chr) {
				case '0': chr = '\0'; break;
				case 'e': chr = '\003'; break;
				case 'f': chr = '\f'; break;
				case 'n':
				case 'r': chr = '\r'; break;
				default: break;
			}
		}
		if (ki->keymap.layout == dkbd_layout_dragon200e) {
			switch (chr) {
			case '[': chr = 0x00; break;
			case ']': chr = 0x01; break;
			case '\\': chr = 0x0b; break;
			case 0xc2:
				chr = *(kip->basic_command++);
				switch (chr) {
				case 0xa1: chr = 0x5b; break; // ¡
				case 0xa7: chr = 0x13; break; // §
				case 0xba: chr = 0x14; break; // º
				case 0xbf: chr = 0x5d; break; // ¿
				default: chr = -1; break;
				}
				break;
			case 0xc3:
				chr = *(kip->basic_command++);
				switch (chr) {
				case 0x80: case 0xa0: chr = 0x1b; break; // à
				case 0x81: case 0xa1: chr = 0x16; break; // á
				case 0x82: case 0xa2: chr = 0x0e; break; // â
				case 0x83: case 0xa3: chr = 0x0a; break; // ã
				case 0x84: case 0xa4: chr = 0x05; break; // ä
				case 0x87: case 0xa7: chr = 0x7d; break; // ç
				case 0x88: case 0xa8: chr = 0x1c; break; // è
				case 0x89: case 0xa9: chr = 0x17; break; // é
				case 0x8a: case 0xaa: chr = 0x0f; break; // ê
				case 0x8b: case 0xab: chr = 0x06; break; // ë
				case 0x8c: case 0xac: chr = 0x1d; break; // ì
				case 0x8d: case 0xad: chr = 0x18; break; // í
				case 0x8e: case 0xae: chr = 0x10; break; // î
				case 0x8f: case 0xaf: chr = 0x09; break; // ï
				case 0x91: chr = 0x5c; break; // Ñ
				case 0x92: case 0xb2: chr = 0x1e; break; // ò
				case 0x93: case 0xb3: chr = 0x19; break; // ó
				case 0x94: case 0xb4: chr = 0x11; break; // ô
				case 0x96: case 0xb6: chr = 0x07; break; // ö
				case 0x99: case 0xb9: chr = 0x1f; break; // ù
				case 0x9a: case 0xba: chr = 0x1a; break; // ú
				case 0x9b: case 0xbb: chr = 0x12; break; // û
				case 0x9c: chr = 0x7f; break; // Ü
				case 0x9f: chr = 0x02; break; // ß (also β)
				case 0xb1: chr = 0x7c; break; // ñ
				case 0xbc: chr = 0x7b; break; // ü
				default: chr = -1; break;
				}
				break;
			case 0xce:
				chr = *(kip->basic_command++);
				switch (chr) {
				case 0xb1: case 0x91: chr = 0x04; break; // α
				case 0xb2: case 0x92: chr = 0x02; break; // β (also ß)
				default: chr = -1; break;
				}
				break;
			}
		}
		if (chr >= 0) {
			MC6809_REG_A(cpu) = chr;
			cpu->reg_cc &= ~4;
		}
		if (*kip->basic_command == 0)
			kip->basic_command = NULL;
	}
	if (!kip->basic_command) {
		if (kip->basic_command_list) {
			void *data = kip->basic_command_list->data;
			kip->basic_command_list = slist_remove(kip->basic_command_list, data);
			free(data);
		}
		if (kip->basic_command_list) {
			kip->basic_command = kip->basic_command_list->data;
		} else {
			machine_bp_remove_list(kip->machine, basic_command_breakpoint);
		}
	}
	/* Use CPU read routine to pull return address back off stack */
	kip->machine->op_rts(kip->machine);
}

void keyboard_queue_basic(struct keyboard_interface *ki, const char *s) {
	struct keyboard_interface_private *kip = (struct keyboard_interface_private *)ki;
	char *data = NULL;
	machine_bp_remove_list(kip->machine, basic_command_breakpoint);
	if (s) {
		data = xstrdup(s);
		kip->basic_command_list = slist_append(kip->basic_command_list, data);
	}
	if (!kip->basic_command) {
		kip->basic_command = data;
	}
	if (kip->basic_command) {
		machine_bp_add_list(kip->machine, basic_command_breakpoint, kip);
	}
}
