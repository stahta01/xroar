/*  Copyright 2003-2017 Ciaran Anscomb
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

/* Keymaps map SDL keysym to dkey. */

/* uk, United Kingdom, QWERTY */
/* cymru, Welsh, QWERTY */
/* eng, English, QWERTY */
/* scot, Scottish, QWERTY */
/* ie, Irish, QWERTY */
/* usa, USA, QWERTY */
static struct sym_dkey_mapping keymap_uk[] = {
	{ .sym = SDLK_MINUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_EQUALS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_LEFTBRACKET, .dkey = DSCAN_AT },
	{ .sym = SDLK_SEMICOLON, .dkey = DSCAN_SEMICOLON },
	{ .sym = SDLK_BACKQUOTE, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_SLASH, .dkey = DSCAN_SLASH },
};

/* be, Belgian, AZERTY */
static struct sym_dkey_mapping keymap_be[] = {
	{ .sym = SDLK_AMPERSAND, .dkey = DSCAN_1 },
	{ .sym = SDLK_QUOTEDBL, .dkey = DSCAN_3 },
	{ .sym = SDLK_QUOTE, .dkey = DSCAN_4 },
	{ .sym = SDLK_LEFTPAREN, .dkey = DSCAN_5 },
	{ .sym = SDLK_EXCLAIM, .dkey = DSCAN_8 },
	{ .sym = SDLK_RIGHTPAREN, .dkey = DSCAN_COLON },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_a, .dkey = DSCAN_Q },
	{ .sym = SDLK_z, .dkey = DSCAN_W },
	{ .sym = SDLK_CARET, .dkey = DSCAN_AT },
	{ .sym = SDLK_q, .dkey = DSCAN_A },
	{ .sym = SDLK_m, .dkey = DSCAN_SEMICOLON },
	{ .sym = SDLK_w, .dkey = DSCAN_Z },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_M },
	{ .sym = SDLK_SEMICOLON, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_COLON, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_EQUALS, .dkey = DSCAN_SLASH },
#ifndef HAVE_COCOA
	{ .sym = SDLK_WORLD_73, .dkey = DSCAN_2 }, // é
	{ .sym = SDLK_WORLD_7, .dkey = DSCAN_6 }, // §
	{ .sym = SDLK_WORLD_72, .dkey = DSCAN_7 }, // è
	{ .sym = SDLK_WORLD_71, .dkey = DSCAN_9 }, // ç
	{ .sym = SDLK_WORLD_64, .dkey = DSCAN_0 }, // à
	{ .sym = SDLK_WORLD_18, .dkey = DSCAN_CLEAR, .priority = 1 }, // ²
#else
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_2 }, // é
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_6 }, // §
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_7 }, // è
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_9 }, // ç
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_0 }, // à
	{ .sym = SDLK_BACKQUOTE, .dkey = DSCAN_CLEAR, .priority = 1 },
#endif
};

/* de, German, QWERTZ */
static struct sym_dkey_mapping keymap_de[] = {
	{ .sym = SDLK_z, .dkey = DSCAN_Y },
	{ .sym = SDLK_y, .dkey = DSCAN_Z },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#ifndef HAVE_COCOA
	{ .sym = SDLK_WORLD_63, .dkey = DSCAN_COLON }, // ß
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_92, .dkey = DSCAN_AT }, // ü
	{ .sym = SDLK_WORLD_86, .dkey = DSCAN_SEMICOLON }, // ö
	{ .sym = SDLK_CARET, .dkey = DSCAN_CLEAR, .priority = 1 }, // dead caret
#else
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_COLON }, // ß
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_AT }, // ü
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_SEMICOLON }, // ö
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_CLEAR, .priority = 1 }, // ä
#endif
};

/* dk, Danish, QWERTY */
static struct sym_dkey_mapping keymap_dk[] = {
	{ .sym = SDLK_PLUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#ifndef HAVE_COCOA
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_69, .dkey = DSCAN_AT }, // å
	{ .sym = SDLK_WORLD_70, .dkey = DSCAN_SEMICOLON }, // æ
	{ .sym = SDLK_WORLD_29, .dkey = DSCAN_CLEAR, .priority = 1 }, // ½
	{ .sym = SDLK_WORLD_88, .dkey = DSCAN_CLEAR, .priority = 1 }, // ø
#else
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_AT }, // å
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_SEMICOLON }, // æ
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_CLEAR, .priority = 1 }, // ø
#endif
};

/* es, Spanish, QWERTY */
static struct sym_dkey_mapping keymap_es[] = {
	{ .sym = SDLK_QUOTE, .dkey = DSCAN_COLON },
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#ifndef HAVE_COCOA
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_AT }, // dead grave
	{ .sym = SDLK_WORLD_81, .dkey = DSCAN_SEMICOLON }, // ñ
	{ .sym = SDLK_WORLD_26, .dkey = DSCAN_CLEAR, .priority = 1 }, // º
#else
	{ .sym = SDLK_BACKQUOTE, .dkey = DSCAN_AT },
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_SEMICOLON },
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_CLEAR, .priority = 1 }, // dead acute
#endif
};

#ifdef HAVE_COCOA
/* es, Spanish, QWERTY (Apple) */
static struct sym_dkey_mapping keymap_es_apple[] = {
	{ .sym = SDLK_MINUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_EQUALS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_AT }, // dead acute
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_SEMICOLON }, // ñ
	// No obvious pick for CLEAR - use Home
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_SLASH }, // ç
};
#endif

/* fi, Finnish, QWERTY */
static struct sym_dkey_mapping keymap_fi[] = {
	{ .sym = SDLK_PLUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#ifndef HAVE_COCOA
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_69, .dkey = DSCAN_AT }, // å
	{ .sym = SDLK_WORLD_86, .dkey = DSCAN_SEMICOLON }, // ö
	{ .sym = SDLK_WORLD_7, .dkey = DSCAN_CLEAR, .priority = 1 }, // §
#else
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_AT }, // å
	{ .sym = SDLK_WORLD_5, .dkey = DSCAN_SEMICOLON }, // ö
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_CLEAR, .priority = 1 }, // ä
#endif
};

/* fr, French, AZERTY */
static struct sym_dkey_mapping keymap_fr[] = {
	{ .sym = SDLK_AMPERSAND, .dkey = DSCAN_1 },
	{ .sym = SDLK_QUOTEDBL, .dkey = DSCAN_3 },
	{ .sym = SDLK_QUOTE, .dkey = DSCAN_4 },
	{ .sym = SDLK_LEFTPAREN, .dkey = DSCAN_5 },
	{ .sym = SDLK_RIGHTPAREN, .dkey = DSCAN_COLON },
	{ .sym = SDLK_a, .dkey = DSCAN_Q },
	{ .sym = SDLK_z, .dkey = DSCAN_W },
	{ .sym = SDLK_CARET, .dkey = DSCAN_AT },
	{ .sym = SDLK_q, .dkey = DSCAN_A },
	{ .sym = SDLK_m, .dkey = DSCAN_SEMICOLON },
	{ .sym = SDLK_w, .dkey = DSCAN_Z },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_M },
	{ .sym = SDLK_SEMICOLON, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_COLON, .dkey = DSCAN_FULL_STOP },
#ifndef HAVE_COCOA
	{ .sym = SDLK_WORLD_73, .dkey = DSCAN_2 }, // é
	{ .sym = SDLK_MINUS, .dkey = DSCAN_6 },
	{ .sym = SDLK_WORLD_72, .dkey = DSCAN_7 }, // è
	{ .sym = SDLK_UNDERSCORE, .dkey = DSCAN_8 },
	{ .sym = SDLK_WORLD_71, .dkey = DSCAN_9 }, // ç
	{ .sym = SDLK_WORLD_64, .dkey = DSCAN_0 }, // à
	{ .sym = SDLK_EQUALS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_WORLD_18, .dkey = DSCAN_CLEAR, .priority = 1 }, // ²
	{ .sym = SDLK_EXCLAIM, .dkey = DSCAN_SLASH },
#else
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_2 }, // é
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_6 }, // §
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_7 }, // è
	{ .sym = SDLK_EXCLAIM, .dkey = DSCAN_8 },
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_9 }, // ç
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_0 }, // à
	{ .sym = SDLK_MINUS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_BACKQUOTE, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = SDLK_EQUALS, .dkey = DSCAN_SLASH },
#endif
};

/* fr_CA, Canadian French, QWERTY */
static struct sym_dkey_mapping keymap_fr_CA[] = {
	{ .sym = SDLK_MINUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_EQUALS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_CARET, .dkey = DSCAN_AT },
	{ .sym = SDLK_SEMICOLON, .dkey = DSCAN_SEMICOLON },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
#ifndef HAVE_COCOA
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_CLEAR, .priority = 1 }, // various
	{ .sym = SDLK_WORLD_73, .dkey = DSCAN_SLASH }, // é
#else
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_CLEAR, .priority = 1 }, // ù
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_SLASH }, // é
#endif
};

/* is, Icelandic, QWERTY */
static struct sym_dkey_mapping keymap_is[] = {
	{ .sym = SDLK_MINUS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
#ifndef HAVE_COCOA
	{ .sym = SDLK_WORLD_86, .dkey = DSCAN_COLON }, // ö
	{ .sym = SDLK_WORLD_80, .dkey = DSCAN_AT }, // ð
	{ .sym = SDLK_WORLD_70, .dkey = DSCAN_SEMICOLON }, // æ
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_CLEAR, .priority = 1 }, // dead ring
	{ .sym = SDLK_WORLD_94, .dkey = DSCAN_SLASH }, // þ
#else
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_COLON }, // ö
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_AT }, // ð
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_SEMICOLON }, // æ
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_CLEAR, .priority = 1 }, // dead acute
	{ .sym = SDLK_WORLD_5, .dkey = DSCAN_SLASH }, // þ
#endif
};

/* it, Italian, QWERTY */
static struct sym_dkey_mapping keymap_it[] = {
	{ .sym = SDLK_QUOTE, .dkey = DSCAN_COLON },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#ifndef HAVE_COCOA
	{ .sym = SDLK_WORLD_76, .dkey = DSCAN_MINUS }, // ì
	{ .sym = SDLK_WORLD_72, .dkey = DSCAN_AT }, // è
	{ .sym = SDLK_WORLD_82, .dkey = DSCAN_SEMICOLON }, // ò
	{ .sym = SDLK_WORLD_89, .dkey = DSCAN_CLEAR, .priority = 1 }, // ù
#else
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_MINUS }, // ì
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_AT }, // è
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_SEMICOLON }, // ò
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_CLEAR, .priority = 1 }, // ù
#endif
};

#ifdef HAVE_COCOA
/* it, Italian, QZERTY (Apple) */
static struct sym_dkey_mapping keymap_it_apple[] = {
	{ .sym = SDLK_AMPERSAND, .dkey = DSCAN_1 },
	{ .sym = SDLK_QUOTEDBL, .dkey = DSCAN_2 },
	{ .sym = SDLK_QUOTE, .dkey = DSCAN_3 },
	{ .sym = SDLK_LEFTPAREN, .dkey = DSCAN_4 },
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_5 }, // ç
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_6 }, // è
	{ .sym = SDLK_RIGHTPAREN, .dkey = DSCAN_7 },
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_8 }, // £
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_9 }, // à
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_0 }, // é
	{ .sym = SDLK_MINUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_EQUALS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_z, .dkey = DSCAN_W },
	{ .sym = SDLK_WORLD_5, .dkey = DSCAN_AT }, // ì
	{ .sym = SDLK_m, .dkey = DSCAN_SEMICOLON },
	{ .sym = SDLK_WORLD_7, .dkey = DSCAN_CLEAR, .priority = 1 }, // §
	{ .sym = SDLK_w, .dkey = DSCAN_Z },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_M },
	{ .sym = SDLK_SEMICOLON, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_COLON, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_WORLD_8, .dkey = DSCAN_SLASH }, // ò
};
#endif

/* nl, Dutch, QWERTY */
static struct sym_dkey_mapping keymap_nl[] = {
#ifndef HAVE_COCOA
	{ .sym = SDLK_SLASH, .dkey = DSCAN_COLON },
	{ .sym = SDLK_WORLD_16, .dkey = DSCAN_MINUS }, // °
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_AT }, // dead diaeresis
	{ .sym = SDLK_PLUS, .dkey = DSCAN_SEMICOLON },
	// No obvious pick for CLEAR - use Home
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#else
	{ .sym = SDLK_MINUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_EQUALS, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_LEFTBRACKET, .dkey = DSCAN_AT },
	{ .sym = SDLK_SEMICOLON, .dkey = DSCAN_SEMICOLON },
	{ .sym = SDLK_BACKQUOTE, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_SLASH, .dkey = DSCAN_SLASH },
#endif
};

/* no, Norwegian, QWERTY */
static struct sym_dkey_mapping keymap_no[] = {
	{ .sym = SDLK_PLUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#ifndef HAVE_COCOA
	{ .sym = SDLK_BACKSLASH, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_WORLD_69, .dkey = DSCAN_AT }, // å
	{ .sym = SDLK_WORLD_88, .dkey = DSCAN_SEMICOLON }, // ø
	{ .sym = SDLK_WORLD_70, .dkey = DSCAN_CLEAR, .priority = 1 }, // æ
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_CLEAR, .priority = 1 }, // dead diaeresis
#else
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_AT }, // å
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_SEMICOLON }, // æ
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_CLEAR, .priority = 1 }, // ø
#endif
};

/* pl, Polish, QWERTZ */
static struct sym_dkey_mapping keymap_pl[] = {
#ifndef HAVE_COCOA
	{ .sym = SDLK_PLUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_QUOTE, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_WORLD_31, .dkey = DSCAN_AT }, // ż
	{ .sym = SDLK_z, .dkey = DSCAN_Y },
	{ .sym = SDLK_WORLD_19, .dkey = DSCAN_SEMICOLON }, // ł
	{ .sym = SDLK_WORLD_95, .dkey = DSCAN_CLEAR, .priority = 1 }, // ˙
	{ .sym = SDLK_y, .dkey = DSCAN_Z },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#else
	{ .sym = SDLK_WORLD_0, .dkey = DSCAN_COLON }, // ż
	{ .sym = SDLK_LEFTBRACKET, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_AT }, // ó
	{ .sym = SDLK_z, .dkey = DSCAN_Y },
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_SEMICOLON }, // ł
	{ .sym = SDLK_WORLD_2, .dkey = DSCAN_CLEAR, .priority = 1 }, // ą
	{ .sym = SDLK_y, .dkey = DSCAN_Z },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#endif
};

/* se, Swedish, QWERTY */
static struct sym_dkey_mapping keymap_se[] = {
#ifndef HAVE_COCOA
	{ .sym = SDLK_PLUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_COMPOSE, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_69, .dkey = DSCAN_AT }, // å
	{ .sym = SDLK_WORLD_86, .dkey = DSCAN_SEMICOLON }, // ö
	{ .sym = SDLK_WORLD_7, .dkey = DSCAN_CLEAR, .priority = 1 }, // §
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#else
	{ .sym = SDLK_PLUS, .dkey = DSCAN_COLON },
	{ .sym = SDLK_WORLD_1, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = SDLK_WORLD_3, .dkey = DSCAN_AT }, // å
	{ .sym = SDLK_WORLD_5, .dkey = DSCAN_SEMICOLON }, // ö
	{ .sym = SDLK_WORLD_4, .dkey = DSCAN_CLEAR, .priority = 1 }, // ä
	{ .sym = SDLK_COMMA, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_MINUS, .dkey = DSCAN_SLASH },
#endif
};

/* DVORAK */
static struct sym_dkey_mapping keymap_dvorak[] = {
	{ .sym = SDLK_LEFTBRACKET, .dkey = DSCAN_COLON },
	{ .sym = SDLK_RIGHTBRACKET, .dkey = DSCAN_MINUS },
	{ .sym = SDLK_QUOTE, .dkey = DSCAN_Q },
	{ .sym = SDLK_COMMA, .dkey = DSCAN_W },
	{ .sym = SDLK_PERIOD, .dkey = DSCAN_E },
	{ .sym = SDLK_p, .dkey = DSCAN_R },
	{ .sym = SDLK_y, .dkey = DSCAN_T },
	{ .sym = SDLK_f, .dkey = DSCAN_Y },
	{ .sym = SDLK_g, .dkey = DSCAN_U },
	{ .sym = SDLK_c, .dkey = DSCAN_I },
	{ .sym = SDLK_r, .dkey = DSCAN_O },
	{ .sym = SDLK_l, .dkey = DSCAN_P },
	{ .sym = SDLK_SLASH, .dkey = DSCAN_AT },
	{ .sym = SDLK_a, .dkey = DSCAN_A },
	{ .sym = SDLK_o, .dkey = DSCAN_S },
	{ .sym = SDLK_e, .dkey = DSCAN_D },
	{ .sym = SDLK_u, .dkey = DSCAN_F },
	{ .sym = SDLK_i, .dkey = DSCAN_G },
	{ .sym = SDLK_d, .dkey = DSCAN_H },
	{ .sym = SDLK_h, .dkey = DSCAN_J },
	{ .sym = SDLK_t, .dkey = DSCAN_K },
	{ .sym = SDLK_n, .dkey = DSCAN_L },
	{ .sym = SDLK_s, .dkey = DSCAN_SEMICOLON },
	{ .sym = SDLK_BACKQUOTE, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = SDLK_SEMICOLON, .dkey = DSCAN_Z },
	{ .sym = SDLK_q, .dkey = DSCAN_X },
	{ .sym = SDLK_j, .dkey = DSCAN_C },
	{ .sym = SDLK_k, .dkey = DSCAN_V },
	{ .sym = SDLK_x, .dkey = DSCAN_B },
	{ .sym = SDLK_b, .dkey = DSCAN_N },
	{ .sym = SDLK_m, .dkey = DSCAN_M },
	{ .sym = SDLK_w, .dkey = DSCAN_COMMA },
	{ .sym = SDLK_v, .dkey = DSCAN_FULL_STOP },
	{ .sym = SDLK_z, .dkey = DSCAN_SLASH },
};

#define MAPPING(m) .num_mappings = ARRAY_N_ELEMENTS(m), .mappings = (m)

static struct keymap keymaps[] = {
	{ .name = "uk", MAPPING(keymap_uk), .description = "UK" },
	{ .name = "cymru", MAPPING(keymap_uk) },
	{ .name = "wales", MAPPING(keymap_uk) },
	{ .name = "eng", MAPPING(keymap_uk) },
	{ .name = "scot", MAPPING(keymap_uk) },

	{ .name = "be", MAPPING(keymap_be), .description = "Belgian" },
	{ .name = "de", MAPPING(keymap_de), .description = "German" },
	{ .name = "dk", MAPPING(keymap_dk), .description = "Danish" },

#ifndef HAVE_COCOA
	{ .name = "es", MAPPING(keymap_es), .description = "Spanish" },
#else
	{ .name = "es", MAPPING(keymap_es_apple), .description = "Spanish" },
	{ .name = "es_iso", MAPPING(keymap_es), .description = "Spanish ISO" },
#endif

	{ .name = "fi", MAPPING(keymap_fi), .description = "Finnish" },
	{ .name = "fr", MAPPING(keymap_fr), .description = "French" },
	{ .name = "fr_CA", MAPPING(keymap_fr_CA), .description = "Canadian French" },
	{ .name = "ie", MAPPING(keymap_uk), .description = "Irish" },
	{ .name = "is", MAPPING(keymap_is), .description = "Icelandic" },

#ifndef HAVE_COCOA
	{ .name = "it", MAPPING(keymap_it), .description = "Italian" },
#else
	{ .name = "it", MAPPING(keymap_it_apple), .description = "Italian" },
	{ .name = "it_pro", MAPPING(keymap_it), .description = "Italian PRO" },
#endif

	{ .name = "nl", MAPPING(keymap_nl), .description = "Dutch" },
	{ .name = "no", MAPPING(keymap_no), .description = "Norwegian" },
	{ .name = "pl", MAPPING(keymap_pl), .description = "Polish QWERTZ" },
	{ .name = "se", MAPPING(keymap_se), .description = "Swedish" },

	{ .name = "us", MAPPING(keymap_uk), .description = "American" },
	{ .name = "dvorak", MAPPING(keymap_dvorak), .description = "DVORAK" },

};
