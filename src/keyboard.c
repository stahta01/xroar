/** \file
 *
 *  \brief Dragon keyboard.
 *
 *  \copyright Copyright 2003-2022 Ciaran Anscomb
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

#include <stdlib.h>
#include <string.h>

#include "delegate.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "breakpoint.h"
#include "debug_cpu.h"
#include "dkbd.h"
#include "events.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6801/mc6801.h"
#include "mc6809/mc6809.h"
#include "part.h"
#include "tape.h"
#include "xroar.h"

// Might want to make a more general automation interface out of this at some
// point, but for now here it is, in with the keyboard stuff:

enum auto_type {
	auto_type_basic_command,  // type a command into BASIC
	auto_type_basic_file,     // type BASIC from a file
	// keep these in order
	auto_type_press_play,     // press play on tape
};

struct auto_event {
	enum auto_type type;
	union {
		sds string;
		struct {
			FILE *fd;
			_Bool utf8;
		} basic_file;
	} data;
};

/* Current chording mode - only affects how backslash is typed: */
static enum keyboard_chord_mode chord_mode = keyboard_chord_mode_dragon_32k_basic;

enum type_state {
	type_state_normal,
	type_state_esc,    // ESC seen
	type_state_csi,    // ESC '[' seen
};

struct keyboard_interface_private {
	struct keyboard_interface public;

	struct machine *machine;
	struct debug_cpu *debug_cpu;
	_Bool is_6809;
	_Bool is_6803;

	_Bool ansi_bold;    // track whether ANSI 'bold' is on or off
	_Bool sg6_mode;     // how to interpret block characters on MC-10
	uint8_t sg4_colour;  // colour of SG4 graphics on MC-10
	uint8_t sg6_colour;  // colour of SG6 graphics on MC-10

	struct {
		enum type_state state;
		int32_t unicode;
		unsigned expect_utf8;
		int32_t arg[8];
		unsigned argnum;
	} type;

	struct slist *auto_event_list;
	unsigned command_index;  // when typing a basic command
};

extern inline void keyboard_press_matrix(struct keyboard_interface *ki, int col, int row);
extern inline void keyboard_release_matrix(struct keyboard_interface *ki, int col, int row);
extern inline void keyboard_press(struct keyboard_interface *ki, int s);
extern inline void keyboard_release(struct keyboard_interface *ki, int s);

static void do_auto_event(void *);
static void do_rts(void *);
static int parse_char(struct keyboard_interface_private *kip, uint8_t c);

static struct machine_bp basic_command_breakpoint[] = {
	BP_DRAGON_ROM(.address = 0xbbe5, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO_BAS10_ROM(.address = 0xa1c1, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO_BAS11_ROM(.address = 0xa1c1, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO_BAS12_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO_BAS13_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO3_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_MC10_ROM(.address = 0xf883, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_MX1600_BAS_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_DRAGON_ROM(.address = 0xbbc5, .handler = DELEGATE_INIT(do_rts, NULL) ),
	BP_COCO_ROM(.address = 0xa7d3, .handler = DELEGATE_INIT(do_rts, NULL) ),
	BP_MC10_ROM(.address = 0xf83f, .handler = DELEGATE_INIT(do_rts, NULL) ),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void auto_event_free(struct auto_event *ae) {
	if (!ae)
		return;
	switch (ae->type) {
	case auto_type_basic_command:
		sdsfree(ae->data.string);
		break;
	case auto_type_basic_file:
		fclose(ae->data.basic_file.fd);
		break;
	default:
		break;
	}
	free(ae);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct keyboard_interface *keyboard_interface_new(struct machine *m) {
	struct keyboard_interface_private *kip = xmalloc(sizeof(*kip));
	*kip = (struct keyboard_interface_private){0};
	struct keyboard_interface *ki = &kip->public;
	kip->machine = m;
	kip->debug_cpu = (struct debug_cpu *)part_component_by_id_is_a((struct part *)m, "CPU", "DEBUG-CPU");
	kip->is_6809 = part_is_a(&kip->debug_cpu->part, "MC6809");
	kip->is_6803 = part_is_a(&kip->debug_cpu->part, "MC6803");
	kip->sg6_mode = 0;
	kip->sg4_colour = 0x80;
	kip->sg6_colour = 0x80;
	for (int i = 0; i < 8; i++) {
		ki->keyboard_column[i] = ~0;
		ki->keyboard_row[i] = ~0;
	}
	return ki;
}

void keyboard_interface_free(struct keyboard_interface *ki) {
	struct keyboard_interface_private *kip = (struct keyboard_interface_private *)ki;
	if (kip->debug_cpu)
		machine_bp_remove_list(kip->machine, basic_command_breakpoint);
	slist_free_full(kip->auto_event_list, (slist_free_func)auto_event_free);
	free(kip);
}

void keyboard_set_keymap(struct keyboard_interface *ki, int map) {
	map %= dkbd_num_layouts;
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
	int s = ki->keymap.unicode_to_dkey[unicode].dk_key;
	keyboard_press_matrix(ki, ki->keymap.point[s].col, ki->keymap.point[s].row);
	DELEGATE_SAFE_CALL(ki->update);
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
	int s = ki->keymap.unicode_to_dkey[unicode].dk_key;
	keyboard_release_matrix(ki, ki->keymap.point[s].col, ki->keymap.point[s].row);
	DELEGATE_SAFE_CALL(ki->update);
}

static void do_rts(void *sptr) {
	struct keyboard_interface_private *kip = sptr;
	kip->machine->op_rts(kip->machine);
}

static void do_auto_event(void *sptr) {
	struct keyboard_interface_private *kip = sptr;
	struct MC6801 *cpu01 = (struct MC6801 *)kip->debug_cpu;
	struct MC6809 *cpu09 = (struct MC6809 *)kip->debug_cpu;

	if (!kip->auto_event_list)
		return;

	// Default to no key pressed
	if (kip->is_6809 && cpu09) {
		MC6809_REG_A(cpu09) = 0;
		cpu09->reg_cc |= 4;
	}
	if (kip->is_6803 && cpu01) {
		MC6801_REG_A(cpu01) = 0;
		cpu01->reg_cc |= 4;
	}

	struct auto_event *ae = kip->auto_event_list->data;
	_Bool next_event = 0;

	if (ae->type == auto_type_basic_command) {
		// type a command into BASIC
		if (kip->command_index < sdslen(ae->data.string)) {
			uint8_t byte = ae->data.string[kip->command_index++];
			// CHR$(0)="[" on Dragon 200-E, so clear Z flag even if zero,
			// as otherwise BASIC will skip it.
			if (kip->is_6809 && cpu09) {
				MC6809_REG_A(cpu09) = byte;
				cpu09->reg_cc &= ~4;
			}
			if (kip->is_6803 && cpu01) {
				MC6801_REG_A(cpu01) = byte;
				cpu01->reg_cc &= ~4;
			}
		}
		if (kip->command_index >= sdslen(ae->data.string)) {
			next_event = 1;
		}
	} else if (ae->type == auto_type_basic_file) {
		for (;;) {
			int byte = fgetc(ae->data.basic_file.fd);
			if (byte < 0) {
				next_event = 1;
				break;
			}
			if (byte == 10)
				byte = 13;
			if (byte == 0x1b)
				ae->data.basic_file.utf8 = 1;
			if (ae->data.basic_file.utf8)
				byte = parse_char(kip, byte);
			if (byte >= 0) {
				if (kip->is_6809 && cpu09) {
					MC6809_REG_A(cpu09) = byte;
					cpu09->reg_cc &= ~4;
				}
				if (kip->is_6803 && cpu01) {
					MC6801_REG_A(cpu01) = byte;
					cpu01->reg_cc &= ~4;
				}
				break;
			}
		}
	}

	if (next_event) {
		kip->auto_event_list = slist_remove(kip->auto_event_list, ae);
		kip->command_index = 0;
		auto_event_free(ae);
		ae = kip->auto_event_list ? kip->auto_event_list->data : NULL;
	}

	// Process all non-typing queued events that might follow - this allows
	// us to press PLAY immediately after typing when the keyboard
	// breakpoint won't be useful.

	while (ae && ae->type >= auto_type_press_play) {
		switch (ae->type) {

		case auto_type_press_play:
			// press play on tape
			tape_set_playing(xroar_tape_interface, 1, 1);
			break;

		default:
			break;
		}

		kip->auto_event_list = slist_remove(kip->auto_event_list, ae);
		auto_event_free(ae);
		ae = kip->auto_event_list ? kip->auto_event_list->data : NULL;
	}

	// Use CPU read routine to pull return address back off stack
	kip->machine->op_rts(kip->machine);

	if (!kip->auto_event_list) {
		machine_bp_remove_list(kip->machine, basic_command_breakpoint);
	}
}

// Process escape sequences, called after encountering an ESC character.

static uint8_t ansi_to_vdg_colour[2][8] = {
	{ 0, 3, 0, 7, 2, 6, 5, 4 },  // not bold: yellow -> orange
	{ 0, 3, 0, 1, 2, 6, 5, 4 }   //     bold: yellow -> yellow
};

// Dragon 200-E character translation: 200-E can handle various Spanish and
// other special characters.

int translate_dragon200e(struct keyboard_interface_private *kip, int32_t uchr) {
	(void)kip;
	switch (uchr) {
	case '[': return 0x00;
	case ']': return 0x01;
	case '\\': return 0x0b;

	case 0xa1: return 0x5b; // ¡
	case 0xa7: return 0x13; // §
	case 0xba: return 0x14; // º
	case 0xbf: return 0x5d; // ¿

	case 0xc0: case 0xe0: return 0x1b; // à
	case 0xc1: case 0xe1: return 0x16; // á
	case 0xc2: case 0xe2: return 0x0e; // â
	case 0xc3: case 0xe3: return 0x0a; // ã
	case 0xc4: case 0xe4: return 0x05; // ä
	case 0xc7: case 0xe7: return 0x7d; // ç
	case 0xc8: case 0xe8: return 0x1c; // è
	case 0xc9: case 0xe9: return 0x17; // é
	case 0xca: case 0xea: return 0x0f; // ê
	case 0xcb: case 0xeb: return 0x06; // ë
	case 0xcc: case 0xec: return 0x1d; // ì
	case 0xcd: case 0xed: return 0x18; // í
	case 0xce: case 0xee: return 0x10; // î
	case 0xcf: case 0xef: return 0x09; // ï
	case 0xd1:            return 0x5c; // Ñ
	case 0xd2: case 0xf2: return 0x1e; // ò
	case 0xd3: case 0xf3: return 0x19; // ó
	case 0xd4: case 0xf4: return 0x11; // ô
	case 0xd6: case 0xf6: return 0x07; // ö
	case 0xd9: case 0xf9: return 0x1f; // ù
	case 0xda: case 0xfa: return 0x1a; // ú
	case 0xdb: case 0xfb: return 0x12; // û
	case 0xdc:            return 0x7f; // Ü
	case 0xdf:            return 0x02; // ß
	case 0xf1:            return 0x7c; // ñ
	case 0xfc:            return 0x7b; // ü

	case 0x0391: case 0x03b1: return 0x04; // α
	case 0x0392: case 0x03b2: return 0x02; // β

	default: break;
	}
	return uchr;
}

// MC-10 character translation: MC-10 can type semigraphics characters
// directly, so here we translate various Unicode block elements.  Although not
// intended for inputting SG6 characters, we allow the user to switch to SG6
// mode and translate accordingly.

int translate_mc10(struct keyboard_interface_private *kip, int32_t uchr) {
	switch (uchr) {

		// U+258x and U+259x, "Block Elements"
	case 0x2580: return kip->sg4_colour ^ 0b1100;
	case 0x2584: return kip->sg4_colour ^ 0b0011;
	case 0x2588: // FULL BLOCK
		     if (kip->sg6_mode) {
			     return kip->sg6_colour ^ 0b111111;
		     }
		     return kip->sg4_colour ^ 0b1111;
	case 0x258c: // LEFT HALF BLOCK
		     if (kip->sg6_mode) {
			     return kip->sg6_colour ^ 0b101010;
		     }
		     return kip->sg4_colour ^ 0b1010;
	case 0x2590: // RIGHT HALF BLOCK
		     if (kip->sg6_mode) {
			     return kip->sg6_colour ^ 0b010101;
		     }
		     return kip->sg4_colour ^ 0b0101;
	case 0x2591: // LIGHT SHADE
	case 0x2592: // MEDIUM SHADE
	case 0x2593: // DARK SHADE
		     return kip->sg6_mode ? kip->sg6_colour : kip->sg4_colour;
	case 0x2596: return kip->sg4_colour ^ 0b0010;
	case 0x2597: return kip->sg4_colour ^ 0b0001;
	case 0x2598: return kip->sg4_colour ^ 0b1000;
	case 0x2599: return kip->sg4_colour ^ 0b1011;
	case 0x259a: return kip->sg4_colour ^ 0b1001;
	case 0x259b: return kip->sg4_colour ^ 0b1110;
	case 0x259c: return kip->sg4_colour ^ 0b1101;
	case 0x259d: return kip->sg4_colour ^ 0b0100;
	case 0x259e: return kip->sg4_colour ^ 0b0110;
	case 0x259f: return kip->sg4_colour ^ 0b0111;

		     // U+1FB0x to U+1FB3x, "Symbols for Legacy Computing"
	case 0x1fb00: return kip->sg6_colour ^ 0b100000;
	case 0x1fb01: return kip->sg6_colour ^ 0b010000;
	case 0x1fb02: return kip->sg6_colour ^ 0b110000;
	case 0x1fb03: return kip->sg6_colour ^ 0b001000;
	case 0x1fb04: return kip->sg6_colour ^ 0b101000;
	case 0x1fb05: return kip->sg6_colour ^ 0b011000;
	case 0x1fb06: return kip->sg6_colour ^ 0b111000;
	case 0x1fb07: return kip->sg6_colour ^ 0b000100;
	case 0x1fb08: return kip->sg6_colour ^ 0b100100;
	case 0x1fb09: return kip->sg6_colour ^ 0b010100;
	case 0x1fb0a: return kip->sg6_colour ^ 0b110100;
	case 0x1fb0b: return kip->sg6_colour ^ 0b001100;
	case 0x1fb0c: return kip->sg6_colour ^ 0b101100;
	case 0x1fb0d: return kip->sg6_colour ^ 0b011100;
	case 0x1fb0e: return kip->sg6_colour ^ 0b111100;

	case 0x1fb0f: return kip->sg6_colour ^ 0b000010;
	case 0x1fb10: return kip->sg6_colour ^ 0b100010;
	case 0x1fb11: return kip->sg6_colour ^ 0b010010;
	case 0x1fb12: return kip->sg6_colour ^ 0b110010;
	case 0x1fb13: return kip->sg6_colour ^ 0b001010;
	case 0x1fb14: return kip->sg6_colour ^ 0b011010;
	case 0x1fb15: return kip->sg6_colour ^ 0b111010;
	case 0x1fb16: return kip->sg6_colour ^ 0b000110;
	case 0x1fb17: return kip->sg6_colour ^ 0b100110;
	case 0x1fb18: return kip->sg6_colour ^ 0b010110;
	case 0x1fb19: return kip->sg6_colour ^ 0b110110;
	case 0x1fb1a: return kip->sg6_colour ^ 0b001110;
	case 0x1fb1b: return kip->sg6_colour ^ 0b101110;
	case 0x1fb1c: return kip->sg6_colour ^ 0b011110;
	case 0x1fb1d: return kip->sg6_colour ^ 0b111110;

	case 0x1fb1e: return kip->sg6_colour ^ 0b000001;
	case 0x1fb1f: return kip->sg6_colour ^ 0b100001;
	case 0x1fb20: return kip->sg6_colour ^ 0b010001;
	case 0x1fb21: return kip->sg6_colour ^ 0b110001;
	case 0x1fb22: return kip->sg6_colour ^ 0b001001;
	case 0x1fb23: return kip->sg6_colour ^ 0b101001;
	case 0x1fb24: return kip->sg6_colour ^ 0b011001;
	case 0x1fb25: return kip->sg6_colour ^ 0b111001;
	case 0x1fb26: return kip->sg6_colour ^ 0b000101;
	case 0x1fb27: return kip->sg6_colour ^ 0b100101;
	case 0x1fb28: return kip->sg6_colour ^ 0b110101;
	case 0x1fb29: return kip->sg6_colour ^ 0b001101;
	case 0x1fb2a: return kip->sg6_colour ^ 0b101101;
	case 0x1fb2b: return kip->sg6_colour ^ 0b011101;
	case 0x1fb2c: return kip->sg6_colour ^ 0b111101;

	case 0x1fb2d: return kip->sg6_colour ^ 0b000011;
	case 0x1fb2e: return kip->sg6_colour ^ 0b100011;
	case 0x1fb2f: return kip->sg6_colour ^ 0b010011;
	case 0x1fb30: return kip->sg6_colour ^ 0b110011;
	case 0x1fb31: return kip->sg6_colour ^ 0b001011;
	case 0x1fb32: return kip->sg6_colour ^ 0b101011;
	case 0x1fb33: return kip->sg6_colour ^ 0b011011;
	case 0x1fb34: return kip->sg6_colour ^ 0b111011;
	case 0x1fb35: return kip->sg6_colour ^ 0b000111;
	case 0x1fb36: return kip->sg6_colour ^ 0b100111;
	case 0x1fb37: return kip->sg6_colour ^ 0b010111;
	case 0x1fb38: return kip->sg6_colour ^ 0b110111;
	case 0x1fb39: return kip->sg6_colour ^ 0b001111;
	case 0x1fb3a: return kip->sg6_colour ^ 0b101111;
	case 0x1fb3b: return kip->sg6_colour ^ 0b011111;

	default: break;
	}
	return uchr;
}

// Process ANSI 'Select Graphic Rendition' escape sequence

static void process_sgr(struct keyboard_interface_private *kip) {
	for (unsigned i = 0; i <= kip->type.argnum; i++) {
		int arg = kip->type.arg[i];
		switch (arg) {
		case 0:
			// Reset
			kip->ansi_bold = 0;
			kip->sg6_mode = 0;
			kip->sg4_colour = 0x80;
			kip->sg6_colour = 0x80;
			break;
		case 1:
			// Set bold mode (colour 33 is yellow)
			kip->ansi_bold = 1;
			break;
		case 4:
			// Select SG4
			kip->sg6_mode = 0;
			break;
		case 6:
			// Select SG6
			kip->sg6_mode = 1;
			break;
		case 7:
			// Set invert mode
			kip->sg4_colour |= 0x0f;
			kip->sg6_colour |= 0x3f;
			break;
		case 21:
			// Unset bold mode (colour 33 is orange)
			kip->ansi_bold = 0;
			break;
		case 27:
			// Unset invert mode
			kip->sg4_colour &= 0xf0;
			kip->sg6_colour &= 0xc0;
			break;
		case 30: case 31: case 32: case 33:
		case 34: case 35: case 36: case 37:
			// Set colour
			{
				int c = ansi_to_vdg_colour[kip->ansi_bold][arg-30];
				kip->sg4_colour = 0x80 | (c << 4) | (kip->sg4_colour & 0x0f);
				kip->sg6_colour = 0x80 | ((c & 1) << 6) | (kip->sg6_colour & 0x3f);
			}
			break;
		default:
			break;
		}
	}
}

// Parse a character.  Returns -1 if this does not translate to a valid
// character for the selected machine, or a positive 8-bit integer if it does.
// Processes limited UTF-8 and ANSI escape sequences.

static int parse_char(struct keyboard_interface_private *kip, uint8_t c) {
	// Simple UTF-8 parsing
	int32_t uchr = kip->type.unicode;
	if (kip->type.expect_utf8 > 0 && (c & 0xc0) == 0x80) {
		uchr = (uchr << 6) | (c & 0x3f);
		kip->type.expect_utf8--;
	} else if ((c & 0xf8) == 0xf0) {
		kip->type.expect_utf8 = 3;
		uchr = c & 0x07;
	} else if ((c & 0xf0) == 0xe0) {
		kip->type.expect_utf8 = 2;
		uchr = c & 0x0f;
	} else if ((c & 0xe0) == 0xc0) {
		kip->type.expect_utf8 = 1;
		uchr = c & 0x1f;
	} else {
		kip->type.expect_utf8 = 0;
		if ((c & 0x80) == 0x80) {
			// Invalid UTF-8 sequence
			return -1;
		}
		uchr = c;
	}
	if (kip->type.expect_utf8 > 0) {
		kip->type.unicode = uchr;
		return -1;
	}

	// State machine handles the presence of ANSI escape sequences
	switch (kip->type.state) {
	case type_state_normal:
		if (uchr == 0x1b) {
			kip->type.state = type_state_esc;
			break;
		}
		// Apply keyboard-specific character translation.  XXX this
		// should really be based on the machine/ROM combination.
		if (kip->public.keymap.layout == dkbd_layout_mc10) {
			return translate_mc10(kip, uchr);
		}
		if (kip->public.keymap.layout == dkbd_layout_dragon200e) {
			return translate_dragon200e(kip, uchr);
		}
		return uchr;

	case type_state_esc:
		if (uchr == '[') {
			kip->type.state = type_state_csi;
			kip->type.arg[0] = 0;
			kip->type.argnum = 0;
			break;
		}
		kip->type.state = type_state_normal;
		if (uchr == 0x1b) {
			return 3;  // ESC ESC -> BREAK
		}
		return parse_char(kip, uchr);

	case type_state_csi:
		switch (uchr) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			kip->type.arg[kip->type.argnum] = kip->type.arg[kip->type.argnum] * 10 + (uchr - '0');
			break;

		case ';':
			kip->type.argnum++;
			if (kip->type.argnum >= 8)
				kip->type.argnum = 7;
			kip->type.arg[kip->type.argnum] = 0;
			break;
		case 'm':
			process_sgr(kip);
			kip->type.state = type_state_normal;
			break;
		default:
			kip->type.state = type_state_normal;
			break;
		}
		break;

	default:
		break;
	}
	return -1;
}

static sds parse_string(struct keyboard_interface_private *kip, sds s) {
	if (!s)
		return NULL;
	// treat everything as uint8_t
	const uint8_t *p = (const uint8_t *)s;
	size_t len = sdslen(s);
	sds new = sdsempty();
	while (len > 0) {
		len--;
		int chr = parse_char(kip, *(p++));
		if (chr < 0)
			continue;
		char c = chr;
		new = sdscatlen(new, &c, 1);
	}
	return new;
}

static void queue_auto_event(struct keyboard_interface_private *kip, struct auto_event *ae) {
	machine_bp_remove_list(kip->machine, basic_command_breakpoint);
	kip->auto_event_list = slist_append(kip->auto_event_list, ae);
	if (kip->auto_event_list) {
		machine_bp_add_list(kip->machine, basic_command_breakpoint, kip);
	}
}

void keyboard_queue_basic_sds(struct keyboard_interface *ki, sds s) {
	struct keyboard_interface_private *kip = (struct keyboard_interface_private *)ki;
	if (s) {
		struct auto_event *ae = xmalloc(sizeof(*ae));
		ae->type = auto_type_basic_command;
		ae->data.string = parse_string(kip, s);
		queue_auto_event(kip, ae);
	}
}

void keyboard_queue_basic(struct keyboard_interface *ki, const char *str) {
	sds s = str ? sdsx_parse_str(str): NULL;
	keyboard_queue_basic_sds(ki, s);
	if (s)
		sdsfree(s);
}

void keyboard_queue_basic_file(struct keyboard_interface *ki, const char *filename) {
	struct keyboard_interface_private *kip = (struct keyboard_interface_private *)ki;
	FILE *fd = fopen(filename, "rb");
	if (!fd) {
		LOG_WARN("Failed to open '%s'\n", filename);
		return;
	}
	struct auto_event *ae = xmalloc(sizeof(*ae));
	ae->type = auto_type_basic_file;
	ae->data.basic_file.fd = fd;
	ae->data.basic_file.utf8 = 0;
	queue_auto_event(kip, ae);
}

void keyboard_queue_press_play(struct keyboard_interface *ki) {
	struct keyboard_interface_private *kip = (struct keyboard_interface_private *)ki;
	struct auto_event *ae = xmalloc(sizeof(*ae));
	ae->type = auto_type_press_play;
	queue_auto_event(kip, ae);
}
