/*  Copyright 2003-2015 Ciaran Anscomb
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

/* The Dragon keyboard layout:
 *
 *   1   2   3   4   5   6   7   8   9   0   :   -  brk
 * up   Q   W   E   R   T   Y   U   I   O   P   @  lft rgt
 *  dwn  A   S   D   F   G   H   J   K   L   ;   enter  clr
 *  shft  Z   X   C   V   B   N   M   , .   /   shft
 *                         space
 */

/* Keymaps map SDL keyscancode to dkey. */

/* uk, United Kingdom, QWERTY */
/* cymru, Welsh, QWERTY */
/* eng, English, QWERTY */
/* scot, Scottish, QWERTY */
/* ie, Irish, QWERTY */
/* usa, USA, QWERTY */
static struct scancode_dkey_mapping keymap_uk[] = {
	{ .scancode = SDL_SCANCODE_MINUS, .dkey = DSCAN_COLON },
	{ .scancode = SDL_SCANCODE_EQUALS, .dkey = DSCAN_MINUS },
	{ .scancode = SDL_SCANCODE_LEFTBRACKET, .dkey = DSCAN_AT },
	{ .scancode = SDL_SCANCODE_SEMICOLON, .dkey = DSCAN_SEMICOLON },
	{ .scancode = SDL_SCANCODE_GRAVE, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .scancode = SDL_SCANCODE_COMMA, .dkey = DSCAN_COMMA },
	{ .scancode = SDL_SCANCODE_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .scancode = SDL_SCANCODE_SLASH, .dkey = DSCAN_SLASH },
};

#define MAPPING(m) .num_mappings = ARRAY_N_ELEMENTS(m), .mappings = (m)

static struct keymap keymaps[] = {
	{ .name = "uk", MAPPING(keymap_uk), .description = "UK" },
	{ .name = "cymru", MAPPING(keymap_uk) },
	{ .name = "wales", MAPPING(keymap_uk) },
	{ .name = "eng", MAPPING(keymap_uk) },
	{ .name = "scot", MAPPING(keymap_uk) },

	{ .name = "us", MAPPING(keymap_uk), .description = "American" },

};
