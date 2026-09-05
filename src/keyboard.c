/* Dragon keyboard.
 *
 * Copyright 2003-2023 Ciaran Anscomb
 *
 * This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 * XRoar is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * See COPYING.GPL for redistribution conditions.
 */

#include "top-config.h"

#include <stdlib.h>

#include "delegate.h"
#include "xalloc.h"

#include "dkbd.h"
#include "keyboard.h"
#include "logging.h"
#include "xroar.h"

extern inline void keyboard_press_matrix(struct keyboard_interface *ki, int col, int row);
extern inline void keyboard_release_matrix(struct keyboard_interface *ki, int col, int row);
extern inline void keyboard_press(struct keyboard_interface *ki, int s);
extern inline void keyboard_release(struct keyboard_interface *ki, int s);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct keyboard_interface *keyboard_interface_new(void) {
	struct keyboard_interface *ki = xmalloc(sizeof(*ki));
	*ki = (struct keyboard_interface){0};
	for (int i = 0; i < 8; i++) {
		ki->keyboard_column[i] = ~0;
		ki->keyboard_row[i] = ~0;
	}
	return ki;
}

void keyboard_interface_free(struct keyboard_interface *ki) {
	free(ki);
}

void keyboard_set_keymap(struct keyboard_interface *ki, int map) {
	map %= dkbd_num_layouts;
	dkbd_map_init(&ki->keymap, map);
}

void keyboard_set_chord_mode(struct keyboard_interface *ki, enum keyboard_chord_mode mode) {
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
		KBD_MATRIX_PRESS(ki, DSCAN_SHIFT);
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_UNSHIFT)
		KBD_MATRIX_RELEASE(ki, DSCAN_SHIFT);
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_CLEAR)
		KBD_MATRIX_PRESS(ki, DSCAN_CLEAR);
	int s = ki->keymap.unicode_to_dkey[unicode].dk_key;
	KBD_MATRIX_PRESS(ki, s);
	DELEGATE_SAFE_CALL(ki->update);
}

void keyboard_unicode_release(struct keyboard_interface *ki, unsigned unicode) {
	if (unicode >= DKBD_U_TABLE_SIZE)
		return;
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_SHIFT)
		KBD_MATRIX_RELEASE(ki, DSCAN_SHIFT);
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_UNSHIFT)
		KBD_MATRIX_PRESS(ki, DSCAN_SHIFT);
	if (ki->keymap.unicode_to_dkey[unicode].dk_mod & DK_MOD_CLEAR)
		KBD_MATRIX_RELEASE(ki, DSCAN_CLEAR);
	int s = ki->keymap.unicode_to_dkey[unicode].dk_key;
	KBD_MATRIX_RELEASE(ki, s);
	DELEGATE_SAFE_CALL(ki->update);
}
