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
#include "mc6801.h"
#include "mc6809.h"
#include "part.h"
#include "tape.h"
#include "xroar.h"

// Might want to make a more general automation interface out of this at some
// point, but for now here it is, in with the keyboard stuff:

enum auto_type {
	auto_type_basic_command,  // type a command into BASIC
	auto_type_press_play,     // press play on tape
};

struct auto_event {
	enum auto_type type;
	union {
		sds string;
	} data;
};

/* Current chording mode - only affects how backslash is typed: */
static enum keyboard_chord_mode chord_mode = keyboard_chord_mode_dragon_32k_basic;

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

	struct slist *auto_event_list;
	unsigned command_index;  // when typing a basic command
};

extern inline void keyboard_press_matrix(struct keyboard_interface *ki, int col, int row);
extern inline void keyboard_release_matrix(struct keyboard_interface *ki, int col, int row);
extern inline void keyboard_press(struct keyboard_interface *ki, int s);
extern inline void keyboard_release(struct keyboard_interface *ki, int s);

static void do_auto_event(void *);

static struct machine_bp basic_command_breakpoint[] = {
	BP_DRAGON_ROM(.address = 0xbbe5, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO_BAS10_ROM(.address = 0xa1c1, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO_BAS11_ROM(.address = 0xa1c1, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO_BAS12_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO_BAS13_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_COCO3_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_MC10_ROM(.address = 0xf883, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
	BP_MX1600_BAS_ROM(.address = 0xa1cb, .handler = DELEGATE_INIT(do_auto_event, NULL) ),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void auto_event_free(struct auto_event *ae) {
	if (!ae)
		return;
	switch (ae->type) {
	case auto_type_basic_command:
		sdsfree(ae->data.string);
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
			kip->auto_event_list = slist_remove(kip->auto_event_list, ae);
			kip->command_index = 0;
			auto_event_free(ae);
			ae = kip->auto_event_list ? kip->auto_event_list->data : NULL;
		}
	}

	// Process all non-typing queued events that might follow - this allows
	// us to press PLAY immediately after typing when the keyboard
	// breakpoint won't be useful.

	while (ae && ae->type != auto_type_basic_command) {
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

static _Bool parse_escape(struct keyboard_interface_private *kip, const uint8_t **pp, size_t *lenp) {
	const uint8_t *p = *pp;
	size_t len = *lenp;
	if (len < 2 || *p != '[') {
		// doesn't look like an escape sequence
		return 0;
	}
	size_t elength;
	for (elength = 1; elength < len; elength++) {
		if (*(p + elength) == 'm')
			break;
	}
	if (elength >= len || *(p + elength) != 'm') {
		// supported escape sequence not found
		return 0;
	}
	// Split string and process each element
	struct sdsx_list *args = sdsx_split_str_len((char *)p + 1, elength, ";", 0);
	if (!args) {
		return 0;
	}
	// Looks like something we can at least try to parse - updated the
	// string and length pointers.
	*pp += (elength + 1);
	*lenp -= (elength + 1);
	for (unsigned i = 0; i < args->len; i++) {
		long a = strtol(args->elem[i], NULL, 10);
		switch (a) {
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
				int c = ansi_to_vdg_colour[kip->ansi_bold][a-30];
				kip->sg4_colour = 0x80 | (c << 4) | (kip->sg4_colour & 0x0f);
				kip->sg6_colour = 0x80 | ((c & 1) << 6) | (kip->sg6_colour & 0x3f);
			}
			break;
		default:
			break;
		}
	}
	sdsx_list_free(args);
	return 1;
}

static sds parse_string(struct keyboard_interface_private *kip, sds s) {
	if (!s)
		return NULL;
	enum dkbd_layout layout = kip->public.keymap.layout;
	// treat everything as uint8_t
	const uint8_t *p = (uint8_t *)s;
	size_t len = sdslen(s);
	sds new = sdsempty();
	while (len > 0) {
		uint8_t chr = *(p++);
		len--;

		// Most translations here are for the Dragon 200-E and MC-10
		// keyboards, but '\e' is either followed by '[' to introduce
		// an ANSI escape sequence, or it is mapped specially to BREAK.
		if (chr == 0x1b) {
			if (parse_escape(kip, &p, &len)) {
				continue;
			}
			chr = 0x03;
		}

		if (layout == dkbd_layout_mc10) {
			switch (chr) {
			case 0xe2:
				// U+258x and U+259x, "Block Elements"
				if (len < 2 || *p != 0x96)
					break;
				p++;
				len -= 2;
				switch (*(p++)) {
				case 0x80: chr = kip->sg4_colour ^ 0b1100; break;
				case 0x84: chr = kip->sg4_colour ^ 0b0011; break;
				case 0x88:
					// FULL BLOCK
					if (kip->sg6_mode) {
						chr = kip->sg6_colour ^ 0b111111;
					} else {
						chr = kip->sg4_colour ^ 0b1111;
					}
					break;
				case 0x8c:
					// LEFT HALF BLOCK
					if (kip->sg6_mode) {
						chr = kip->sg6_colour ^ 0b101010;
					} else {
						chr = kip->sg4_colour ^ 0b1010;
					}
					break;
				case 0x90:
					// RIGHT HALF BLOCK
					if (kip->sg6_mode) {
						chr = kip->sg6_colour ^ 0b010101;
					} else {
						chr = kip->sg4_colour ^ 0b0101;
					}
					break;
				case 0x91:  // LIGHT SHADE
				case 0x92:  // MEDIUM SHADE
				case 0x93:  // DARK SHADE
					chr = kip->sg6_mode ? kip->sg6_colour : kip->sg4_colour;
					break;
				case 0x96: chr = kip->sg4_colour ^ 0b0010; break;
				case 0x97: chr = kip->sg4_colour ^ 0b0001; break;
				case 0x98: chr = kip->sg4_colour ^ 0b1000; break;
				case 0x99: chr = kip->sg4_colour ^ 0b1011; break;
				case 0x9a: chr = kip->sg4_colour ^ 0b1001; break;
				case 0x9b: chr = kip->sg4_colour ^ 0b1110; break;
				case 0x9c: chr = kip->sg4_colour ^ 0b1101; break;
				case 0x9d: chr = kip->sg4_colour ^ 0b0100; break;
				case 0x9e: chr = kip->sg4_colour ^ 0b0110; break;
				case 0x9f: chr = kip->sg4_colour ^ 0b0111; break;
				default: p--; len++; break;
				}
				break;

			case 0xf0:
				// U+1FB0x to U+1FB3x, "Symbols for Legacy Computing"
				if (len < 3 || *p != 0x9f || *(p+1) != 0xac)
					break;
				p += 2;
				len -= 3;
				switch (*(p++)) {
				case 0x80: chr = kip->sg6_colour ^ 0b100000; break;
				case 0x81: chr = kip->sg6_colour ^ 0b010000; break;
				case 0x82: chr = kip->sg6_colour ^ 0b110000; break;
				case 0x83: chr = kip->sg6_colour ^ 0b001000; break;
				case 0x84: chr = kip->sg6_colour ^ 0b101000; break;
				case 0x85: chr = kip->sg6_colour ^ 0b011000; break;
				case 0x86: chr = kip->sg6_colour ^ 0b111000; break;
				case 0x87: chr = kip->sg6_colour ^ 0b000100; break;
				case 0x88: chr = kip->sg6_colour ^ 0b100100; break;
				case 0x89: chr = kip->sg6_colour ^ 0b010100; break;
				case 0x8a: chr = kip->sg6_colour ^ 0b110100; break;
				case 0x8b: chr = kip->sg6_colour ^ 0b001100; break;
				case 0x8c: chr = kip->sg6_colour ^ 0b101100; break;
				case 0x8d: chr = kip->sg6_colour ^ 0b011100; break;
				case 0x8e: chr = kip->sg6_colour ^ 0b111100; break;

				case 0x8f: chr = kip->sg6_colour ^ 0b000010; break;
				case 0x90: chr = kip->sg6_colour ^ 0b100010; break;
				case 0x91: chr = kip->sg6_colour ^ 0b010010; break;
				case 0x92: chr = kip->sg6_colour ^ 0b110010; break;
				case 0x93: chr = kip->sg6_colour ^ 0b001010; break;
				case 0x94: chr = kip->sg6_colour ^ 0b011010; break;
				case 0x95: chr = kip->sg6_colour ^ 0b111010; break;
				case 0x96: chr = kip->sg6_colour ^ 0b000110; break;
				case 0x97: chr = kip->sg6_colour ^ 0b100110; break;
				case 0x98: chr = kip->sg6_colour ^ 0b010110; break;
				case 0x99: chr = kip->sg6_colour ^ 0b110110; break;
				case 0x9a: chr = kip->sg6_colour ^ 0b001110; break;
				case 0x9b: chr = kip->sg6_colour ^ 0b101110; break;
				case 0x9c: chr = kip->sg6_colour ^ 0b011110; break;
				case 0x9d: chr = kip->sg6_colour ^ 0b111110; break;

				case 0x9e: chr = kip->sg6_colour ^ 0b000001; break;
				case 0x9f: chr = kip->sg6_colour ^ 0b100001; break;
				case 0xa0: chr = kip->sg6_colour ^ 0b010001; break;
				case 0xa1: chr = kip->sg6_colour ^ 0b110001; break;
				case 0xa2: chr = kip->sg6_colour ^ 0b001001; break;
				case 0xa3: chr = kip->sg6_colour ^ 0b101001; break;
				case 0xa4: chr = kip->sg6_colour ^ 0b011001; break;
				case 0xa5: chr = kip->sg6_colour ^ 0b111001; break;
				case 0xa6: chr = kip->sg6_colour ^ 0b000101; break;
				case 0xa7: chr = kip->sg6_colour ^ 0b100101; break;
				case 0xa8: chr = kip->sg6_colour ^ 0b110101; break;
				case 0xa9: chr = kip->sg6_colour ^ 0b001101; break;
				case 0xaa: chr = kip->sg6_colour ^ 0b101101; break;
				case 0xab: chr = kip->sg6_colour ^ 0b011101; break;
				case 0xac: chr = kip->sg6_colour ^ 0b111101; break;

				case 0xad: chr = kip->sg6_colour ^ 0b000011; break;
				case 0xae: chr = kip->sg6_colour ^ 0b100011; break;
				case 0xaf: chr = kip->sg6_colour ^ 0b010011; break;
				case 0xb0: chr = kip->sg6_colour ^ 0b110011; break;
				case 0xb1: chr = kip->sg6_colour ^ 0b001011; break;
				case 0xb2: chr = kip->sg6_colour ^ 0b101011; break;
				case 0xb3: chr = kip->sg6_colour ^ 0b011011; break;
				case 0xb4: chr = kip->sg6_colour ^ 0b111011; break;
				case 0xb5: chr = kip->sg6_colour ^ 0b000111; break;
				case 0xb6: chr = kip->sg6_colour ^ 0b100111; break;
				case 0xb7: chr = kip->sg6_colour ^ 0b010111; break;
				case 0xb8: chr = kip->sg6_colour ^ 0b110111; break;
				case 0xb9: chr = kip->sg6_colour ^ 0b001111; break;
				case 0xba: chr = kip->sg6_colour ^ 0b101111; break;
				case 0xbb: chr = kip->sg6_colour ^ 0b011111; break;

				default: p -= 2; len += 2; break;
				}
				break;

			default: break;
			}
		}

		if (layout == dkbd_layout_dragon200e) {
			switch (chr) {
			case '[': chr = 0x00; break;
			case ']': chr = 0x01; break;
			case '\\': chr = 0x0b; break;
			// some very partial utf-8 decoding:
			case 0xc2:
				if (len > 0) {
					len--;
					switch (*(p++)) {
					case 0xa1: chr = 0x5b; break; // ¡
					case 0xa7: chr = 0x13; break; // §
					case 0xba: chr = 0x14; break; // º
					case 0xbf: chr = 0x5d; break; // ¿
					default: p--; len++; break;
					}
				}
				break;
			case 0xc3:
				if (len > 0) {
					len--;
					switch (*(p++)) {
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
					default: p--; len++; break;
					}
				}
				break;
			case 0xce:
				if (len > 0) {
					len--;
					switch (*(p++)) {
					case 0xb1: case 0x91: chr = 0x04; break; // α
					case 0xb2: case 0x92: chr = 0x02; break; // β (also ß)
					default: p--; len++; break;
					}
				}
				break;
			default: break;
			}
		}

		new = sdscatlen(new, (char *)&chr, 1);
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

void keyboard_queue_press_play(struct keyboard_interface *ki) {
	struct keyboard_interface_private *kip = (struct keyboard_interface_private *)ki;
	struct auto_event *ae = xmalloc(sizeof(*ae));
	ae->type = auto_type_press_play;
	queue_auto_event(kip, ae);
}
