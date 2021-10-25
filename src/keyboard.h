/** \file
 *
 *  \brief Dragon keyboard.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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

#ifndef XROAR_KEYBOARD_H_
#define XROAR_KEYBOARD_H_

#include "delegate.h"
#include "sds.h"

#include "dkbd.h"

struct machine;

struct keyboard_state {
	unsigned row_source;
	unsigned row_sink;
	unsigned col_source;
	unsigned col_sink;
};

struct keyboard_interface {
	struct dkbd_map keymap;

	// These contain masks to be applied when the corresponding row/column
	// is held low.  eg, if row 1 is outputting a 0 , keyboard_column[1]
	// will be applied on column reads.

	unsigned keyboard_column[9];
	unsigned keyboard_row[9];

	// As the keyboard state is likely updated directly by keyboard
	// modules, machines may wish to be notified of changes.

	DELEGATE_T0(void) update;
};

/* Press or release a key at the the matrix position (col,row). */

inline void keyboard_press_matrix(struct keyboard_interface *ki, int col, int row) {
	ki->keyboard_column[col] &= ~(1<<(row));
	ki->keyboard_row[row] &= ~(1<<(col));
}

inline void keyboard_release_matrix(struct keyboard_interface *ki, int col, int row) {
	ki->keyboard_column[col] |= 1<<(row);
	ki->keyboard_row[row] |= 1<<(col);
}

/* Press or release a key from the current keymap. */

inline void keyboard_press(struct keyboard_interface *ki, int s) {
	keyboard_press_matrix(ki, ki->keymap.point[s].col, ki->keymap.point[s].row);
	DELEGATE_SAFE_CALL(ki->update);
}

inline void keyboard_release(struct keyboard_interface *ki, int s) {
	keyboard_release_matrix(ki, ki->keymap.point[s].col, ki->keymap.point[s].row);
	DELEGATE_SAFE_CALL(ki->update);
}

/* Shift and clear keys are at the same matrix point in both Dragon & CoCo
 * keymaps; indirection through the keymap can be bypassed. */

#define KEYBOARD_PRESS_CLEAR(ki) keyboard_press_matrix((ki),1,6)
#define KEYBOARD_RELEASE_CLEAR(ki) keyboard_release_matrix((ki),1,6)
#define KEYBOARD_PRESS_SHIFT(ki) keyboard_press_matrix((ki),7,6)
#define KEYBOARD_RELEASE_SHIFT(ki) keyboard_release_matrix((ki),7,6)

/* Chord mode affects how special characters are typed (specifically, the
 * backslash character when in translation mode). */
enum keyboard_chord_mode {
	keyboard_chord_mode_dragon_32k_basic,
	keyboard_chord_mode_dragon_64k_basic,
	keyboard_chord_mode_coco_basic
};

struct keyboard_interface *keyboard_interface_new(struct machine *m);
void keyboard_interface_free(struct keyboard_interface *ki);

void keyboard_set_keymap(struct keyboard_interface *ki, int map);

void keyboard_set_chord_mode(struct keyboard_interface *ki, enum keyboard_chord_mode mode);

void keyboard_read_matrix(struct keyboard_interface *ki, struct keyboard_state *);
void keyboard_unicode_press(struct keyboard_interface *ki, unsigned unicode);
void keyboard_unicode_release(struct keyboard_interface *ki, unsigned unicode);

// If supplied as an SDS, the string is assumed to be pre-parsed for escape sequences, etc.
void keyboard_queue_basic_sds(struct keyboard_interface *ki, sds s);
// Else, if supplied as a normal C string, it's parsed.
void keyboard_queue_basic(struct keyboard_interface *ki, const char *s);
// Schedule press play on tape
void keyboard_queue_press_play(struct keyboard_interface *ki);

#endif
