/** \file
 *
 *  \brief Dragon keyboard mapping.
 *
 *  \copyright Copyright 2013-2024 Ciaran Anscomb
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

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "c-strcase.h"

#include "dkbd.h"

/* This could be a lot simpler, but at some point I want to make these mappings
 * completely user definable, so I'm abstracting the definitions a little. */

struct dkey_chord_mapping {
	unsigned unicode;
	struct dkey_chord chord;
};

/* A map variant consists of the base layout (dragon or coco ONLY), and then
 * additional mappings. */

struct dkbd_layout_variant {
	enum dkbd_layout base_layout;
	size_t num_chord_mappings;
	struct dkey_chord_mapping *chord_mappings;
};

static struct dkey_chord_mapping dragon_chord_mappings[] = {
	{ DKBD_U_CAPS_LOCK, { DSCAN_0, DK_MOD_SHIFT } },
	{ DKBD_U_PAUSE_OUTPUT, { DSCAN_AT, DK_MOD_SHIFT } },
	{ '@', { DSCAN_AT, DK_MOD_UNSHIFT } },
	{ '\\', { DSCAN_INVALID, DK_MOD_SHIFT|DK_MOD_CLEAR } },
	{ '[', { DSCAN_DOWN, DK_MOD_SHIFT} },
	{ ']', { DSCAN_RIGHT, DK_MOD_SHIFT} },
	{ '~', { DSCAN_AT, DK_MOD_SHIFT } },
};

static struct dkey_chord_mapping dragon200e_chord_mappings[] = {
	{ 0xc7, { DSCAN_0, DK_MOD_SHIFT } },  // 'Ç'
	{ 0xe7, { DSCAN_0, DK_MOD_SHIFT } },  // 'ç'
	{ 0xdc, { DSCAN_BREAK, DK_MOD_SHIFT } },  // 'Ü'
	{ 0xfc, { DSCAN_BREAK, DK_MOD_SHIFT } },  // 'ü'
	{ ';', { DSCAN_AT, DK_MOD_UNSHIFT } },
	{ '+', { DSCAN_AT, DK_MOD_SHIFT } },
	{ 0xcf, { DSCAN_RIGHT, DK_MOD_UNSHIFT } },  // 'Î'
	{ 0xef, { DSCAN_RIGHT, DK_MOD_UNSHIFT } },  // 'î'
	{ 0xbf, { DSCAN_RIGHT, DK_MOD_SHIFT } },  // '¿'
	{ 0xc3, { DSCAN_DOWN, DK_MOD_UNSHIFT } },  // 'Ã'
	{ 0xe3, { DSCAN_DOWN, DK_MOD_UNSHIFT } },  // 'ã'
	{ 0xa1, { DSCAN_DOWN, DK_MOD_SHIFT } },  // '¡'
	{ 0xf1, { DSCAN_SEMICOLON, DK_MOD_UNSHIFT } },  // 'ñ'
	{ 0xd1, { DSCAN_SEMICOLON, DK_MOD_SHIFT } },  // 'Ñ'
	{ DKBD_U_CAPS_LOCK, { DSCAN_ENTER, DK_MOD_SHIFT } },
	{ DKBD_U_PAUSE_OUTPUT, { DSCAN_SPACE, DK_MOD_SHIFT } },
	{ '@', { DSCAN_CLEAR, DK_MOD_SHIFT } },
	{ 0xa7, { DSCAN_SPACE, DK_MOD_SHIFT } },  // '§'
	{ '~', { DSCAN_SPACE, DK_MOD_SHIFT } },
};

static struct dkey_chord_mapping coco3_chord_mappings[] = {
	{ DKBD_U_CAPS_LOCK, { DSCAN_0, DK_MOD_SHIFT } },
	{ DKBD_U_PAUSE_OUTPUT, { DSCAN_AT, DK_MOD_SHIFT } },
	// TODO: ALT
	{ '@', { DSCAN_AT, DK_MOD_UNSHIFT } },
	{ '\\', { DSCAN_INVALID, DK_MOD_SHIFT|DK_MOD_CLEAR } },
	{ '[', { DSCAN_DOWN, DK_MOD_SHIFT} },
	{ ']', { DSCAN_RIGHT, DK_MOD_SHIFT} },
	{ '~', { DSCAN_AT, DK_MOD_SHIFT } },
	// TODO: CONTROL
	{ DKBD_U_F1, { DSCAN_F1, 0 } },
	{ DKBD_U_F2, { DSCAN_F2, 0 } },
};

static struct dkey_chord_mapping mc10_chord_mappings[] = {
	{ DKBD_U_CAPS_LOCK, { DSCAN_0, DK_MOD_SHIFT } },
	{ DKBD_U_PAUSE_OUTPUT, { DSCAN_AT, DK_MOD_SHIFT } },
	{ '@', { DSCAN_AT, DK_MOD_UNSHIFT } },
};

static struct dkey_chord_mapping alice_chord_mappings[] = {
	{ DKBD_U_CAPS_LOCK, { DSCAN_0, DK_MOD_SHIFT } },
	{ DKBD_U_PAUSE_OUTPUT, { DSCAN_AT, DK_MOD_SHIFT } },
	{ '@', { DSCAN_AT, DK_MOD_UNSHIFT } },
	{ 'q', { DSCAN_A, DK_MOD_UNSHIFT } },
	{ 'w', { DSCAN_Z, DK_MOD_UNSHIFT } },
	{ 'a', { DSCAN_Q, DK_MOD_UNSHIFT } },
	{ ';', { DSCAN_SLASH, DK_MOD_UNSHIFT } },
	{ '+', { DSCAN_SLASH, DK_MOD_SHIFT } },
	{ 'z', { DSCAN_W, DK_MOD_UNSHIFT } },
	{ 'm', { DSCAN_SEMICOLON, DK_MOD_UNSHIFT } },
	{ '/', { DSCAN_M, DK_MOD_UNSHIFT } },
	{ '?', { DSCAN_M, DK_MOD_SHIFT } },
};

#define CMAPPING(m) .num_chord_mappings = ARRAY_N_ELEMENTS(m), .chord_mappings = (m)

static struct dkbd_layout_variant dkbd_layout_variants[] = {
	{ .base_layout = dkbd_layout_dragon, CMAPPING(dragon_chord_mappings), },
	{ .base_layout = dkbd_layout_coco, CMAPPING(dragon_chord_mappings), },
	{ .base_layout = dkbd_layout_dragon, CMAPPING(dragon200e_chord_mappings), },
	{ .base_layout = dkbd_layout_coco, CMAPPING(coco3_chord_mappings), },
	{ .base_layout = dkbd_layout_mc10, CMAPPING(mc10_chord_mappings), },
	{ .base_layout = dkbd_layout_mc10, CMAPPING(alice_chord_mappings), },
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Key values are chosen so that they directly encode the crosspoint locations
 * for a normal Dragon.  CoCo map requires a small translation. */

void dkbd_map_init(struct dkbd_map *map, enum dkbd_layout layout) {
	assert(layout >= 0 && layout < ARRAY_N_ELEMENTS(dkbd_layout_variants));
	struct dkbd_layout_variant *variant = &dkbd_layout_variants[layout];
	map->layout = layout;

	/* Populate the matrix crosspoint map */

	// Clear table
	for (unsigned i = 0; i < DKBD_POINT_TABLE_SIZE; ++i) {
		map->point[i] = (struct dkbd_matrix_point){8, 8, 0};
	}

	// Map the easy stuff
	for (unsigned i = 0; i < 56; ++i) {
		unsigned col = i & 7;
		unsigned row = (i >> 3) & 7;
		if (variant->base_layout == dkbd_layout_coco ||
		    variant->base_layout == dkbd_layout_mc10 ||
		    variant->base_layout == dkbd_layout_alice) {
			if (row != 6) {
				row = (row + 4) % 6;
			}
		}
		map->point[i] = (struct dkbd_matrix_point){row, col, 0};
	}

	// CoCo 3 specials
	if (layout != dkbd_layout_coco3) {
		// Unmap CoCo 3 extended keys
		for (unsigned i = DSCAN_ALT; i <= DSCAN_F2; ++i) {
			map->point[i] = (struct dkbd_matrix_point){8, 8, 0};
		}
	}

	// For most machines, this is true.  Overridden later for MC-10:
	map->point[DSCAN_BACKSPACE] = map->point[DSCAN_LEFT];

	// MC-10
	if (layout == dkbd_layout_mc10 || layout == dkbd_layout_alice) {
		// Tweak MC-10 layout
		map->point[DSCAN_UP] = map->point[DSCAN_W];
		map->point[DSCAN_DOWN] = map->point[DSCAN_Z];
		map->point[DSCAN_LEFT] = map->point[DSCAN_A];
		map->point[DSCAN_RIGHT] = map->point[DSCAN_S];
		map->point[DSCAN_SPACE] = (struct dkbd_matrix_point){3, 7, 0};
		map->point[DSCAN_ENTER] = (struct dkbd_matrix_point){3, 6, 0};
		map->point[DSCAN_BREAK] = (struct dkbd_matrix_point){6, 2, 0};
		map->point[DSCAN_CTRL] = (struct dkbd_matrix_point){6, 0, 0};
		map->point[DSCAN_CLEAR] = map->point[DSCAN_CTRL];
		map->point[DSCAN_SHIFT] = (struct dkbd_matrix_point){6, 7, 0};
		map->point[DSCAN_BACKSPACE] = (struct dkbd_matrix_point){0, 1, DSCAN_CTRL};
	}

	/* Populate the unicode_to_dkey map */

	// Clear table
	for (size_t i = 0; i < ARRAY_N_ELEMENTS(map->unicode_to_dkey); ++i) {
		map->unicode_to_dkey[i] = (struct dkey_chord){DSCAN_INVALID, 0};
	}
	// "1!" - "9)", ":*", ";+"
	for (unsigned i = 0; i <= 10; ++i) {
		map->unicode_to_dkey['1'+i] = (struct dkey_chord){DSCAN_1+i, DK_MOD_UNSHIFT};
		map->unicode_to_dkey['!'+i] = (struct dkey_chord){DSCAN_1+i, DK_MOD_SHIFT};
	}
	// ",<", "-=", ".>", "/?"
	for (unsigned i = 0; i <= 3; ++i) {
		map->unicode_to_dkey[','+i] = (struct dkey_chord){DSCAN_COMMA+i, DK_MOD_UNSHIFT};
		map->unicode_to_dkey['<'+i] = (struct dkey_chord){DSCAN_COMMA+i, DK_MOD_SHIFT};
	}
	// "aA" - "zZ"
	for (unsigned i = 0; i <= 25; ++i) {
		map->unicode_to_dkey['a'+i] = (struct dkey_chord){DSCAN_A+i, DK_MOD_UNSHIFT};
		map->unicode_to_dkey['A'+i] = (struct dkey_chord){DSCAN_A+i, DK_MOD_SHIFT};
	}
	// Rest of standard keys
	map->unicode_to_dkey['0'] = (struct dkey_chord){DSCAN_0, DK_MOD_UNSHIFT};
	map->unicode_to_dkey[' '] = (struct dkey_chord){DSCAN_SPACE, 0};
	map->unicode_to_dkey[DKBD_U_BREAK] = (struct dkey_chord){DSCAN_BREAK, 0};
	map->unicode_to_dkey[0x08] = (struct dkey_chord){DSCAN_LEFT, DK_MOD_UNSHIFT};  // BS
	map->unicode_to_dkey[0x09] = (struct dkey_chord){DSCAN_RIGHT, DK_MOD_UNSHIFT};  // HT
	map->unicode_to_dkey[0x0a] = (struct dkey_chord){DSCAN_ENTER, 0};  // LF
	map->unicode_to_dkey[0x0c] = (struct dkey_chord){DSCAN_CLEAR, 0};  // FF
	map->unicode_to_dkey[0x0d] = (struct dkey_chord){DSCAN_ENTER, 0};  // CR
	map->unicode_to_dkey[0x19] = (struct dkey_chord){DSCAN_RIGHT, 0};  // EM
	map->unicode_to_dkey[0x5e] = (struct dkey_chord){DSCAN_UP, DK_MOD_UNSHIFT};  // '^'
	map->unicode_to_dkey[0x5f] = (struct dkey_chord){DSCAN_UP, DK_MOD_SHIFT};  // '_'
	map->unicode_to_dkey[0x7f] = (struct dkey_chord){DSCAN_LEFT, DK_MOD_UNSHIFT};  // DEL
	// Standard extras
	map->unicode_to_dkey[DKBD_U_ERASE_LINE] = (struct dkey_chord){DSCAN_LEFT, DK_MOD_SHIFT};
	map->unicode_to_dkey[0xa3] = (struct dkey_chord){DSCAN_3, DK_MOD_SHIFT};  // '£'
	map->unicode_to_dkey[0xba] = (struct dkey_chord){DSCAN_CLEAR, DK_MOD_UNSHIFT};  // 'º'
	map->unicode_to_dkey[0xaa] = (struct dkey_chord){DSCAN_CLEAR, DK_MOD_SHIFT};  // 'ª'
	// Variant mappings
	for (size_t i = 0; i < variant->num_chord_mappings; ++i) {
		unsigned unicode = variant->chord_mappings[i].unicode;
		map->unicode_to_dkey[unicode] = variant->chord_mappings[i].chord;
	}
}

struct dk_name_to_key {
	const char *name;
	int8_t dk_key;
} key_names[] = {
	{ "colon", DSCAN_COLON },
	{ "semicolon", DSCAN_SEMICOLON },
	{ "comma", DSCAN_COMMA },
	{ "minus", DSCAN_MINUS },
	{ "fullstop", DSCAN_FULL_STOP },
	{ "period", DSCAN_FULL_STOP },
	{ "dot", DSCAN_FULL_STOP },
	{ "slash", DSCAN_SLASH },
	{ "at", DSCAN_AT },
	{ "up", DSCAN_UP },
	{ "down", DSCAN_DOWN },
	{ "left", DSCAN_LEFT },
	{ "right", DSCAN_RIGHT },
	{ "space", DSCAN_SPACE },
	{ "enter", DSCAN_ENTER },
	{ "clear", DSCAN_CLEAR },
	{ "break", DSCAN_BREAK },
	{ "escape", DSCAN_BREAK },
	{ "shift", DSCAN_SHIFT },
	{ "alt", DSCAN_ALT },
	{ "ctrl", DSCAN_CTRL },
	{ "control", DSCAN_CTRL },
	{ "f1", DSCAN_F1 },
	{ "f2", DSCAN_F2 },
	{ "unbind", DSCAN_INVALID },
	{ "unmap", DSCAN_INVALID },
};

int8_t dk_key_by_name(const char *name) {
	if (strlen(name) == 1) {
		if (*name >= '0' && *name <= '9') {
			return DSCAN_0 + (*name - '0');
		}
		if (tolower(*name) >= 'a' && tolower(*name) <= 'z') {
			return DSCAN_A + (tolower(*name) - 'a');
		}
		switch (*name) {
		case ':': return DSCAN_COLON;
		case ';': return DSCAN_SEMICOLON;
		case ',': return DSCAN_COMMA;
		case '-': return DSCAN_MINUS;
		case '.': return DSCAN_FULL_STOP;
		case '/': return DSCAN_SLASH;
		case '@': return DSCAN_AT;
		case '^': return DSCAN_UP;
		case 0x0a: return DSCAN_DOWN;
		case 0x08: return DSCAN_LEFT;
		case 0x09: return DSCAN_RIGHT;
		case ' ': return DSCAN_SPACE;
		case 0x0d: return DSCAN_ENTER;
		case 0x0c: return DSCAN_CLEAR;
		case 0x1b: return DSCAN_BREAK;
		default:
			   break;
		}
	}
	for (size_t i = 0; i < ARRAY_N_ELEMENTS(key_names); ++i) {
		if (c_strcasecmp(key_names[i].name, name) == 0) {
			return key_names[i].dk_key;
		}
	}

	return -1;
}
