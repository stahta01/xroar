/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2016  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_KEYBOARD_H_
#define XROAR_KEYBOARD_H_

#include "dkbd.h"

struct machine;

#define NUM_KEYMAPS   (3)
#define KEYMAP_DRAGON (0)
#define KEYMAP_COCO   (1)
#define KEYMAP_DRAGON200E (2)

#define IS_DRAGON_KEYMAP (xroar_machine_config->keymap == KEYMAP_DRAGON)
#define IS_COCO_KEYMAP (xroar_machine_config->keymap == KEYMAP_COCO)

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
}

inline void keyboard_release(struct keyboard_interface *ki, int s) {
	keyboard_release_matrix(ki, ki->keymap.point[s].col, ki->keymap.point[s].row);
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
void keyboard_queue_basic(struct keyboard_interface *ki, const char *s);

#endif  /* XROAR_KEYBOARD_H_ */
