/** \file
 *
 *  \brief Automatic keyboard entry.
 *
 *  \copyright Copyright 2023-2024 Ciaran Anscomb
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "delegate.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "auto_kbd.h"
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
#include "xroar.h"

// Each entry in the queue has a type:

enum auto_type {
	auto_type_basic_command,  // type a command into BASIC
	auto_type_basic_file,     // type BASIC from a file
};

// Queue entries

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

enum type_state {
	type_state_normal,
	type_state_esc,    // ESC seen
	type_state_csi,    // ESC '[' seen
};

struct auto_kbd {
	struct machine *machine;
	struct debug_cpu *debug_cpu;
	_Bool is_6809;
	_Bool is_6803;

	// These are refreshed each time data is submitted by checking the
	// machine's keyboard map.  XXX this should really be based on the
	// machine/ROM combination.
	_Bool is_dragon200e;
	_Bool is_mc10;

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

static void auto_event_free_void(void *);
static void auto_event_free(struct auto_event *ae);
static sds parse_string(struct auto_kbd *ak, sds s);
static void queue_auto_event(struct auto_kbd *ak, struct auto_event *ae);

static void do_rts(void *);
static void do_auto_event(void *);
static int parse_char(struct auto_kbd *ak, uint8_t c);

static struct machine_bp basic_command_breakpoint[] = {
	BP_DRAGON_ROM(.address = 0x851b, .handler = DELEGATE_INIT(do_rts, NULL) ),
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

struct auto_kbd *auto_kbd_new(struct machine *m) {
	struct auto_kbd *ak = xmalloc(sizeof(*ak));
	*ak = (struct auto_kbd){0};

	ak->machine = m;
	ak->debug_cpu = (struct debug_cpu *)part_component_by_id_is_a((struct part *)m, "CPU", "DEBUG-CPU");
	ak->is_6809 = part_is_a(&ak->debug_cpu->part, "MC6809");
	ak->is_6803 = part_is_a(&ak->debug_cpu->part, "MC6803");
	ak->sg6_mode = 0;
	ak->sg4_colour = 0x80;
	ak->sg6_colour = 0x80;

	return ak;
}

void auto_kbd_free(struct auto_kbd *ak) {
	if (ak->debug_cpu) {
		machine_bp_remove_list(ak->machine, basic_command_breakpoint);
	}
	slist_free_full(ak->auto_event_list, (slist_free_func)auto_event_free_void);
	free(ak);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void refresh_translation_type(struct auto_kbd *ak) {
	ak->is_dragon200e = 0;
	ak->is_mc10 = 0;
	struct keyboard_interface *ki = ak->machine->get_interface(ak->machine, "keyboard");
	if (ki) {
		ak->is_dragon200e = (ki->keymap.layout == dkbd_layout_dragon200e);
		ak->is_mc10 = (ki->keymap.layout == dkbd_layout_mc10);
	}
}

// Queue pre-parsed string to be typed

void ak_type_string_len(struct auto_kbd *ak, const char *str, size_t len) {
	sds s = sdsnewlen(str, len);
	ak_type_sds(ak, s);
	sdsfree(s);
}

void ak_type_sds(struct auto_kbd *ak, sds s) {
	if (!s)
		return;
	refresh_translation_type(ak);
	struct auto_event *ae = xmalloc(sizeof(*ae));
	ae->type = auto_type_basic_command;
	ae->data.string = parse_string(ak, s);
	queue_auto_event(ak, ae);
}

// Queue string to be parsed for escape characters then typed

void ak_parse_type_string(struct auto_kbd *ak, const char *str) {
	sds s = str ? sdsx_parse_str(str) : NULL;
	ak_type_sds(ak, s);
	if (s)
		sdsfree(s);
}

// Queue typing a whole file

void ak_type_file(struct auto_kbd *ak, const char *filename) {
	FILE *fd = fopen(filename, "rb");
	if (!fd) {
		LOG_MOD_WARN("type/file", "%s: %s\n", filename, strerror(errno));
		return;
	}
	refresh_translation_type(ak);
	struct auto_event *ae = xmalloc(sizeof(*ae));
	ae->type = auto_type_basic_file;
	ae->data.basic_file.fd = fd;
	ae->data.basic_file.utf8 = 0;
	queue_auto_event(ak, ae);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void auto_event_free_void(void *sptr) {
	auto_event_free((struct auto_event *)sptr);
}

static void auto_event_free(struct auto_event *ae) {
	if (!ae) {
		return;
	}
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

static void do_rts(void *sptr) {
	struct auto_kbd *ak = sptr;
	ak->machine->op_rts(ak->machine);
}

static void do_auto_event(void *sptr) {
	struct auto_kbd *ak = sptr;
	struct MC6801 *cpu01 = (struct MC6801 *)ak->debug_cpu;
	struct MC6809 *cpu09 = (struct MC6809 *)ak->debug_cpu;

	if (!ak->auto_event_list)
		return;

	// Default to no key pressed
	if (ak->is_6809 && cpu09) {
		MC6809_REG_A(cpu09) = 0;
		cpu09->reg_cc |= 4;
	}
	if (ak->is_6803 && cpu01) {
		MC6801_REG_A(cpu01) = 0;
		cpu01->reg_cc |= 4;
	}

	struct auto_event *ae = ak->auto_event_list->data;
	_Bool next_event = 0;

	if (ae->type == auto_type_basic_command) {
		// type a command into BASIC
		if (ak->command_index < sdslen(ae->data.string)) {
			uint8_t byte = ae->data.string[ak->command_index++];
			// CHR$(0)="[" on Dragon 200-E, so clear Z flag even if zero,
			// as otherwise BASIC will skip it.
			if (ak->is_6809 && cpu09) {
				MC6809_REG_A(cpu09) = byte;
				cpu09->reg_cc &= ~4;
			}
			if (ak->is_6803 && cpu01) {
				MC6801_REG_A(cpu01) = byte;
				cpu01->reg_cc &= ~4;
			}
		}
		if (ak->command_index >= sdslen(ae->data.string)) {
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
				byte = parse_char(ak, byte);
			if (byte >= 0) {
				if (ak->is_6809 && cpu09) {
					MC6809_REG_A(cpu09) = byte;
					cpu09->reg_cc &= ~4;
				}
				if (ak->is_6803 && cpu01) {
					MC6801_REG_A(cpu01) = byte;
					cpu01->reg_cc &= ~4;
				}
				break;
			}
		}
	}

	if (next_event) {
		ak->auto_event_list = slist_remove(ak->auto_event_list, ae);
		ak->command_index = 0;
		auto_event_free(ae);
	}

	// Use CPU read routine to pull return address back off stack
	ak->machine->op_rts(ak->machine);

	if (!ak->auto_event_list) {
		machine_bp_remove_list(ak->machine, basic_command_breakpoint);
	}
}

// Process escape sequences, called after encountering an ESC character.

static uint8_t ansi_to_vdg_colour[2][8] = {
	{ 0, 3, 0, 7, 2, 6, 5, 4 },  // not bold: yellow -> orange
	{ 0, 3, 0, 1, 2, 6, 5, 4 }   //     bold: yellow -> yellow
};

// Dragon 200-E character translation: 200-E can handle various Spanish and
// other special characters.

static int translate_dragon200e(struct auto_kbd *ak, int32_t uchr) {
	(void)ak;
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

static int translate_mc10(struct auto_kbd *ak, int32_t uchr) {
	switch (uchr) {

		// U+258x and U+259x, "Block Elements"
	case 0x2580: return ak->sg4_colour ^ 0x0c;
	case 0x2584: return ak->sg4_colour ^ 0x03;
	case 0x2588: // FULL BLOCK
		     if (ak->sg6_mode) {
			     return ak->sg6_colour ^ 0x3f;
		     }
		     return ak->sg4_colour ^ 0x0f;
	case 0x258c: // LEFT HALF BLOCK
		     if (ak->sg6_mode) {
			     return ak->sg6_colour ^ 0x2a;
		     }
		     return ak->sg4_colour ^ 0x0a;
	case 0x2590: // RIGHT HALF BLOCK
		     if (ak->sg6_mode) {
			     return ak->sg6_colour ^ 0x15;
		     }
		     return ak->sg4_colour ^ 0x05;
	case 0x2591: // LIGHT SHADE
	case 0x2592: // MEDIUM SHADE
	case 0x2593: // DARK SHADE
		     return ak->sg6_mode ? ak->sg6_colour : ak->sg4_colour;
	case 0x2596: return ak->sg4_colour ^ 0x02;
	case 0x2597: return ak->sg4_colour ^ 0x01;
	case 0x2598: return ak->sg4_colour ^ 0x08;
	case 0x2599: return ak->sg4_colour ^ 0x0b;
	case 0x259a: return ak->sg4_colour ^ 0x09;
	case 0x259b: return ak->sg4_colour ^ 0x0e;
	case 0x259c: return ak->sg4_colour ^ 0x0d;
	case 0x259d: return ak->sg4_colour ^ 0x04;
	case 0x259e: return ak->sg4_colour ^ 0x06;
	case 0x259f: return ak->sg4_colour ^ 0x07;

		     // U+1FB0x to U+1FB3x, "Symbols for Legacy Computing"
	case 0x1fb00: return ak->sg6_colour ^ 0x20;
	case 0x1fb01: return ak->sg6_colour ^ 0x10;
	case 0x1fb02: return ak->sg6_colour ^ 0x30;
	case 0x1fb03: return ak->sg6_colour ^ 0x08;
	case 0x1fb04: return ak->sg6_colour ^ 0x28;
	case 0x1fb05: return ak->sg6_colour ^ 0x18;
	case 0x1fb06: return ak->sg6_colour ^ 0x38;
	case 0x1fb07: return ak->sg6_colour ^ 0x04;
	case 0x1fb08: return ak->sg6_colour ^ 0x24;
	case 0x1fb09: return ak->sg6_colour ^ 0x14;
	case 0x1fb0a: return ak->sg6_colour ^ 0x34;
	case 0x1fb0b: return ak->sg6_colour ^ 0x0c;
	case 0x1fb0c: return ak->sg6_colour ^ 0x2c;
	case 0x1fb0d: return ak->sg6_colour ^ 0x1c;
	case 0x1fb0e: return ak->sg6_colour ^ 0x3c;

	case 0x1fb0f: return ak->sg6_colour ^ 0x02;
	case 0x1fb10: return ak->sg6_colour ^ 0x22;
	case 0x1fb11: return ak->sg6_colour ^ 0x12;
	case 0x1fb12: return ak->sg6_colour ^ 0x32;
	case 0x1fb13: return ak->sg6_colour ^ 0x0a;
	case 0x1fb14: return ak->sg6_colour ^ 0x1a;
	case 0x1fb15: return ak->sg6_colour ^ 0x3a;
	case 0x1fb16: return ak->sg6_colour ^ 0x06;
	case 0x1fb17: return ak->sg6_colour ^ 0x26;
	case 0x1fb18: return ak->sg6_colour ^ 0x16;
	case 0x1fb19: return ak->sg6_colour ^ 0x36;
	case 0x1fb1a: return ak->sg6_colour ^ 0x0e;
	case 0x1fb1b: return ak->sg6_colour ^ 0x2e;
	case 0x1fb1c: return ak->sg6_colour ^ 0x1e;
	case 0x1fb1d: return ak->sg6_colour ^ 0x3e;

	case 0x1fb1e: return ak->sg6_colour ^ 0x01;
	case 0x1fb1f: return ak->sg6_colour ^ 0x21;
	case 0x1fb20: return ak->sg6_colour ^ 0x11;
	case 0x1fb21: return ak->sg6_colour ^ 0x31;
	case 0x1fb22: return ak->sg6_colour ^ 0x09;
	case 0x1fb23: return ak->sg6_colour ^ 0x29;
	case 0x1fb24: return ak->sg6_colour ^ 0x19;
	case 0x1fb25: return ak->sg6_colour ^ 0x39;
	case 0x1fb26: return ak->sg6_colour ^ 0x05;
	case 0x1fb27: return ak->sg6_colour ^ 0x25;
	case 0x1fb28: return ak->sg6_colour ^ 0x35;
	case 0x1fb29: return ak->sg6_colour ^ 0x0d;
	case 0x1fb2a: return ak->sg6_colour ^ 0x2d;
	case 0x1fb2b: return ak->sg6_colour ^ 0x1d;
	case 0x1fb2c: return ak->sg6_colour ^ 0x3d;

	case 0x1fb2d: return ak->sg6_colour ^ 0x03;
	case 0x1fb2e: return ak->sg6_colour ^ 0x23;
	case 0x1fb2f: return ak->sg6_colour ^ 0x13;
	case 0x1fb30: return ak->sg6_colour ^ 0x33;
	case 0x1fb31: return ak->sg6_colour ^ 0x0b;
	case 0x1fb32: return ak->sg6_colour ^ 0x2b;
	case 0x1fb33: return ak->sg6_colour ^ 0x1b;
	case 0x1fb34: return ak->sg6_colour ^ 0x3b;
	case 0x1fb35: return ak->sg6_colour ^ 0x07;
	case 0x1fb36: return ak->sg6_colour ^ 0x27;
	case 0x1fb37: return ak->sg6_colour ^ 0x17;
	case 0x1fb38: return ak->sg6_colour ^ 0x37;
	case 0x1fb39: return ak->sg6_colour ^ 0x0f;
	case 0x1fb3a: return ak->sg6_colour ^ 0x2f;
	case 0x1fb3b: return ak->sg6_colour ^ 0x1f;

	default: break;
	}
	return uchr;
}

// Process ANSI 'Select Graphic Rendition' escape sequence

static void process_sgr(struct auto_kbd *ak) {
	for (unsigned i = 0; i <= ak->type.argnum; i++) {
		int arg = ak->type.arg[i];
		switch (arg) {
		case 0:
			// Reset
			ak->ansi_bold = 0;
			ak->sg6_mode = 0;
			ak->sg4_colour = 0x80;
			ak->sg6_colour = 0x80;
			break;
		case 1:
			// Set bold mode (colour 33 is yellow)
			ak->ansi_bold = 1;
			break;
		case 4:
			// Select SG4
			ak->sg6_mode = 0;
			break;
		case 6:
			// Select SG6
			ak->sg6_mode = 1;
			break;
		case 7:
			// Set invert mode
			ak->sg4_colour |= 0x0f;
			ak->sg6_colour |= 0x3f;
			break;
		case 21:
			// Unset bold mode (colour 33 is orange)
			ak->ansi_bold = 0;
			break;
		case 27:
			// Unset invert mode
			ak->sg4_colour &= 0xf0;
			ak->sg6_colour &= 0xc0;
			break;
		case 30: case 31: case 32: case 33:
		case 34: case 35: case 36: case 37:
			// Set colour
			{
				int c = ansi_to_vdg_colour[ak->ansi_bold][arg-30];
				ak->sg4_colour = 0x80 | (c << 4) | (ak->sg4_colour & 0x0f);
				ak->sg6_colour = 0x80 | ((c & 1) << 6) | (ak->sg6_colour & 0x3f);
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

static int parse_char(struct auto_kbd *ak, uint8_t c) {
	// Simple UTF-8 parsing
	int32_t uchr = ak->type.unicode;
	if (ak->type.expect_utf8 > 0 && (c & 0xc0) == 0x80) {
		uchr = (uchr << 6) | (c & 0x3f);
		ak->type.expect_utf8--;
	} else if ((c & 0xf8) == 0xf0) {
		ak->type.expect_utf8 = 3;
		uchr = c & 0x07;
	} else if ((c & 0xf0) == 0xe0) {
		ak->type.expect_utf8 = 2;
		uchr = c & 0x0f;
	} else if ((c & 0xe0) == 0xc0) {
		ak->type.expect_utf8 = 1;
		uchr = c & 0x1f;
	} else {
		ak->type.expect_utf8 = 0;
		if ((c & 0x80) == 0x80) {
			// Invalid UTF-8 sequence
			return -1;
		}
		uchr = c;
	}
	if (ak->type.expect_utf8 > 0) {
		ak->type.unicode = uchr;
		return -1;
	}

	// State machine handles the presence of ANSI escape sequences
	switch (ak->type.state) {
	case type_state_normal:
		if (uchr == 0x1b) {
			ak->type.state = type_state_esc;
			break;
		}
		// Apply keyboard-specific character translation.  XXX this
		// should really be based on the machine/ROM combination.
		if (ak->is_dragon200e) {
			return translate_dragon200e(ak, uchr);
		}
		if (ak->is_mc10) {
			return translate_mc10(ak, uchr);
		}
		return uchr;

	case type_state_esc:
		if (uchr == '[') {
			ak->type.state = type_state_csi;
			ak->type.arg[0] = 0;
			ak->type.argnum = 0;
			break;
		}
		ak->type.state = type_state_normal;
		if (uchr == 0x1b) {
			return 3;  // ESC ESC -> BREAK
		}
		return parse_char(ak, uchr);

	case type_state_csi:
		switch (uchr) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			ak->type.arg[ak->type.argnum] = ak->type.arg[ak->type.argnum] * 10 + (uchr - '0');
			break;

		case ';':
			ak->type.argnum++;
			if (ak->type.argnum >= 8)
				ak->type.argnum = 7;
			ak->type.arg[ak->type.argnum] = 0;
			break;
		case 'm':
			process_sgr(ak);
			ak->type.state = type_state_normal;
			break;
		default:
			ak->type.state = type_state_normal;
			break;
		}
		break;

	default:
		break;
	}
	return -1;
}

static sds parse_string(struct auto_kbd *ak, sds s) {
	if (!s)
		return NULL;
	// treat everything as uint8_t
	const uint8_t *p = (const uint8_t *)s;
	size_t len = sdslen(s);
	sds new = sdsempty();
	while (len > 0) {
		len--;
		int chr = parse_char(ak, *(p++));
		if (chr < 0)
			continue;
		char c = chr;
		new = sdscatlen(new, &c, 1);
	}
	return new;
}

static void queue_auto_event(struct auto_kbd *ak, struct auto_event *ae) {
	machine_bp_remove_list(ak->machine, basic_command_breakpoint);
	ak->auto_event_list = slist_append(ak->auto_event_list, ae);
	if (ak->auto_event_list) {
		machine_bp_add_list(ak->machine, basic_command_breakpoint, ak);
	}
}
