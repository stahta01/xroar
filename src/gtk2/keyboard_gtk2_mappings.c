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

/* Keymaps map GDK keysym to dkey. */

/* uk, United Kingdom, QWERTY */
/* cymru, Welsh, QWERTY */
/* eng, English, QWERTY */
/* scot, Scottish, QWERTY */
/* ie, Irish, QWERTY */
/* usa, USA, QWERTY */
static struct sym_dkey_mapping keymap_uk[] = {
	{ .sym = GDK_minus, .dkey = DSCAN_COLON },
	{ .sym = GDK_equal, .dkey = DSCAN_MINUS },
	{ .sym = GDK_bracketleft, .dkey = DSCAN_AT },
	{ .sym = GDK_semicolon, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_grave, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_slash, .dkey = DSCAN_SLASH },
};

/* be, Belgian, AZERTY */
static struct sym_dkey_mapping keymap_be[] = {
	{ .sym = GDK_ampersand, .dkey = DSCAN_1 },
	{ .sym = GDK_eacute, .dkey = DSCAN_2 },
	{ .sym = GDK_quotedbl, .dkey = DSCAN_3 },
	{ .sym = GDK_apostrophe, .dkey = DSCAN_4 },
	{ .sym = GDK_parenleft, .dkey = DSCAN_5 },
	{ .sym = GDK_section, .dkey = DSCAN_6 },
	{ .sym = GDK_egrave, .dkey = DSCAN_7 },
	{ .sym = GDK_exclam, .dkey = DSCAN_8 },
	{ .sym = GDK_ccedilla, .dkey = DSCAN_9 },
	{ .sym = GDK_agrave, .dkey = DSCAN_0 },
	{ .sym = GDK_parenright, .dkey = DSCAN_COLON },
	{ .sym = GDK_minus, .dkey = DSCAN_MINUS },
	{ .sym = GDK_a, .dkey = DSCAN_Q },
	{ .sym = GDK_z, .dkey = DSCAN_W },
	{ .sym = GDK_dead_circumflex, .dkey = DSCAN_AT },
	{ .sym = GDK_q, .dkey = DSCAN_A },
	{ .sym = GDK_m, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_twosuperior, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_w, .dkey = DSCAN_Z },
	{ .sym = GDK_comma, .dkey = DSCAN_M },
	{ .sym = GDK_semicolon, .dkey = DSCAN_COMMA },
	{ .sym = GDK_colon, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_equal, .dkey = DSCAN_SLASH },
};

/* de, German, QWERTZ */
static struct sym_dkey_mapping keymap_de[] = {
	{ .sym = GDK_ssharp, .dkey = DSCAN_COLON },
	{ .sym = GDK_dead_acute, .dkey = DSCAN_MINUS },
	{ .sym = GDK_z, .dkey = DSCAN_Y },
	{ .sym = GDK_udiaeresis, .dkey = DSCAN_AT },
	{ .sym = GDK_odiaeresis, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_dead_circumflex, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_y, .dkey = DSCAN_Z },
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* dk, Danish, QWERTY */
static struct sym_dkey_mapping keymap_dk[] = {
	{ .sym = GDK_plus, .dkey = DSCAN_COLON },
	{ .sym = GDK_dead_acute, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = GDK_aring, .dkey = DSCAN_AT }, // å
	{ .sym = GDK_ae, .dkey = DSCAN_SEMICOLON }, // æ
	{ .sym = GDK_onehalf, .dkey = DSCAN_CLEAR, .priority = 1 }, // ½
	{ .sym = GDK_oslash, .dkey = DSCAN_CLEAR, .priority = 1 }, // ø
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* es, Spanish, QWERTY */
static struct sym_dkey_mapping keymap_es[] = {
	{ .sym = GDK_apostrophe, .dkey = DSCAN_COLON },
	{ .sym = GDK_exclamdown, .dkey = DSCAN_MINUS },
	{ .sym = GDK_dead_grave, .dkey = DSCAN_AT },
	{ .sym = GDK_ntilde, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_masculine, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* fi, Finnish, QWERTY */
static struct sym_dkey_mapping keymap_fi[] = {
	{ .sym = GDK_plus, .dkey = DSCAN_COLON },
	{ .sym = GDK_dead_acute, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = GDK_aring, .dkey = DSCAN_AT }, // å
	{ .sym = GDK_odiaeresis, .dkey = DSCAN_SEMICOLON }, // ö
	{ .sym = GDK_section, .dkey = DSCAN_CLEAR, .priority = 1 }, // §
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* fr, French, AZERTY */
static struct sym_dkey_mapping keymap_fr[] = {
	{ .sym = GDK_ampersand, .dkey = DSCAN_1 },
	{ .sym = GDK_eacute, .dkey = DSCAN_2 },
	{ .sym = GDK_quotedbl, .dkey = DSCAN_3 },
	{ .sym = GDK_apostrophe, .dkey = DSCAN_4 },
	{ .sym = GDK_parenleft, .dkey = DSCAN_5 },
	{ .sym = GDK_minus, .dkey = DSCAN_6 },
	{ .sym = GDK_egrave, .dkey = DSCAN_7 },
	{ .sym = GDK_underscore, .dkey = DSCAN_8 },
	{ .sym = GDK_ccedilla, .dkey = DSCAN_9 },
	{ .sym = GDK_agrave, .dkey = DSCAN_0 },
	{ .sym = GDK_parenright, .dkey = DSCAN_COLON },
	{ .sym = GDK_equal, .dkey = DSCAN_MINUS },
	{ .sym = GDK_a, .dkey = DSCAN_Q },
	{ .sym = GDK_z, .dkey = DSCAN_W },
	{ .sym = GDK_dead_circumflex, .dkey = DSCAN_AT },
	{ .sym = GDK_q, .dkey = DSCAN_A },
	{ .sym = GDK_m, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_twosuperior, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_w, .dkey = DSCAN_Z },
	{ .sym = GDK_comma, .dkey = DSCAN_M },
	{ .sym = GDK_semicolon, .dkey = DSCAN_COMMA },
	{ .sym = GDK_colon, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_exclam, .dkey = DSCAN_SLASH },
};

/* fr_CA, Canadian French, QWERTY */
static struct sym_dkey_mapping keymap_fr_CA[] = {
	{ .sym = GDK_minus, .dkey = DSCAN_COLON },
	{ .sym = GDK_equal, .dkey = DSCAN_MINUS },
	{ .sym = GDK_dead_circumflex, .dkey = DSCAN_AT },
	{ .sym = GDK_semicolon, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_dead_grave, .dkey = DSCAN_CLEAR },
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_eacute, .dkey = DSCAN_SLASH },
};

/* is, Icelandic, QWERTY */
static struct sym_dkey_mapping keymap_is[] = {
	{ .sym = GDK_odiaeresis, .dkey = DSCAN_COLON }, // ö
	{ .sym = GDK_minus, .dkey = DSCAN_MINUS },
	{ .sym = GDK_eth, .dkey = DSCAN_AT }, // ð
	{ .sym = GDK_ae, .dkey = DSCAN_SEMICOLON }, // æ
	{ .sym = GDK_dead_abovering, .dkey = DSCAN_CLEAR, .priority = 1 }, // dead ring
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_thorn, .dkey = DSCAN_SLASH }, // þ
};

/* it, Italian, QWERTY */
static struct sym_dkey_mapping keymap_it[] = {
	{ .sym = GDK_apostrophe, .dkey = DSCAN_COLON },
	{ .sym = GDK_igrave, .dkey = DSCAN_MINUS }, // ì
	{ .sym = GDK_egrave, .dkey = DSCAN_AT }, // è
	{ .sym = GDK_ograve, .dkey = DSCAN_SEMICOLON }, // ò
	{ .sym = GDK_ugrave, .dkey = DSCAN_CLEAR, .priority = 1 }, // ù
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* nl, Dutch, QWERTY */
static struct sym_dkey_mapping keymap_nl[] = {
	{ .sym = GDK_slash, .dkey = DSCAN_COLON },
	{ .sym = GDK_degree, .dkey = DSCAN_MINUS },
	{ .sym = GDK_dead_diaeresis, .dkey = DSCAN_AT },
	{ .sym = GDK_plus, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_dead_acute, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* no, Norwegian, QWERTY */
static struct sym_dkey_mapping keymap_no[] = {
	{ .sym = GDK_plus, .dkey = DSCAN_COLON },
	{ .sym = GDK_backslash, .dkey = DSCAN_MINUS },
	{ .sym = GDK_aring, .dkey = DSCAN_AT }, // å
	{ .sym = GDK_oslash, .dkey = DSCAN_SEMICOLON }, // ø
	{ .sym = GDK_ae, .dkey = DSCAN_CLEAR, .priority = 1 }, // æ
	{ .sym = GDK_bar, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_dead_diaeresis, .dkey = DSCAN_CLEAR, .priority = 1 }, // dead diaeresis
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* pl, Polish, QWERTZ */
static struct sym_dkey_mapping keymap_pl[] = {
	{ .sym = GDK_plus, .dkey = DSCAN_COLON },
	{ .sym = GDK_apostrophe, .dkey = DSCAN_MINUS },
	{ .sym = GDK_z, .dkey = DSCAN_Y },
	{ .sym = GDK_zabovedot, .dkey = DSCAN_AT },
	{ .sym = GDK_lstroke, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_abovedot, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_y, .dkey = DSCAN_Z },
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* se, Swedish, QWERTY */
static struct sym_dkey_mapping keymap_se[] = {
	{ .sym = GDK_plus, .dkey = DSCAN_COLON },
	{ .sym = GDK_dead_acute, .dkey = DSCAN_MINUS }, // dead acute
	{ .sym = GDK_aring, .dkey = DSCAN_AT }, // å
	{ .sym = GDK_odiaeresis, .dkey = DSCAN_SEMICOLON }, // ö
	{ .sym = GDK_section, .dkey = DSCAN_CLEAR, .priority = 1 }, // §
	{ .sym = GDK_adiaeresis, .dkey = DSCAN_CLEAR, .priority = 1 }, // ä
	{ .sym = GDK_comma, .dkey = DSCAN_COMMA },
	{ .sym = GDK_period, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_minus, .dkey = DSCAN_SLASH },
};

/* DVORAK */
static struct sym_dkey_mapping keymap_dvorak[] = {
	{ .sym = GDK_bracketleft, .dkey = DSCAN_COLON },
	{ .sym = GDK_bracketright, .dkey = DSCAN_MINUS },
	{ .sym = GDK_apostrophe, .dkey = DSCAN_Q },
	{ .sym = GDK_comma, .dkey = DSCAN_W },
	{ .sym = GDK_period, .dkey = DSCAN_E },
	{ .sym = GDK_p, .dkey = DSCAN_R },
	{ .sym = GDK_y, .dkey = DSCAN_T },
	{ .sym = GDK_f, .dkey = DSCAN_Y },
	{ .sym = GDK_g, .dkey = DSCAN_U },
	{ .sym = GDK_c, .dkey = DSCAN_I },
	{ .sym = GDK_r, .dkey = DSCAN_O },
	{ .sym = GDK_l, .dkey = DSCAN_P },
	{ .sym = GDK_slash, .dkey = DSCAN_AT },
	{ .sym = GDK_a, .dkey = DSCAN_A },
	{ .sym = GDK_o, .dkey = DSCAN_S },
	{ .sym = GDK_e, .dkey = DSCAN_D },
	{ .sym = GDK_u, .dkey = DSCAN_F },
	{ .sym = GDK_i, .dkey = DSCAN_G },
	{ .sym = GDK_d, .dkey = DSCAN_H },
	{ .sym = GDK_h, .dkey = DSCAN_J },
	{ .sym = GDK_t, .dkey = DSCAN_K },
	{ .sym = GDK_n, .dkey = DSCAN_L },
	{ .sym = GDK_s, .dkey = DSCAN_SEMICOLON },
	{ .sym = GDK_grave, .dkey = DSCAN_CLEAR, .priority = 1 },
	{ .sym = GDK_semicolon, .dkey = DSCAN_Z },
	{ .sym = GDK_q, .dkey = DSCAN_X },
	{ .sym = GDK_j, .dkey = DSCAN_C },
	{ .sym = GDK_k, .dkey = DSCAN_V },
	{ .sym = GDK_x, .dkey = DSCAN_B },
	{ .sym = GDK_b, .dkey = DSCAN_N },
	{ .sym = GDK_m, .dkey = DSCAN_M },
	{ .sym = GDK_w, .dkey = DSCAN_COMMA },
	{ .sym = GDK_v, .dkey = DSCAN_FULL_STOP },
	{ .sym = GDK_z, .dkey = DSCAN_SLASH },
};

#define MAPPING(m) .num_mappings = G_N_ELEMENTS(m), .mappings = (m)

static struct keymap keymaps[] = {
	{ .name = "uk", MAPPING(keymap_uk), .description = "UK" },
	{ .name = "cymru", MAPPING(keymap_uk) },
	{ .name = "wales", MAPPING(keymap_uk) },
	{ .name = "eng", MAPPING(keymap_uk) },
	{ .name = "scot", MAPPING(keymap_uk) },

	{ .name = "be", MAPPING(keymap_be), .description = "Belgian" },
	{ .name = "de", MAPPING(keymap_de), .description = "German" },
	{ .name = "dk", MAPPING(keymap_dk), .description = "Danish" },
	{ .name = "es", MAPPING(keymap_es), .description = "Spanish" },
	{ .name = "fi", MAPPING(keymap_fi), .description = "Finnish" },
	{ .name = "fr", MAPPING(keymap_fr), .description = "French" },
	{ .name = "fr_CA", MAPPING(keymap_fr_CA), .description = "Canadian French" },
	{ .name = "ie", MAPPING(keymap_uk), .description = "Irish" },
	{ .name = "is", MAPPING(keymap_is), .description = "Icelandic" },
	{ .name = "it", MAPPING(keymap_it), .description = "Italian" },
	{ .name = "nl", MAPPING(keymap_nl), .description = "Dutch" },
	{ .name = "no", MAPPING(keymap_no), .description = "Norwegian" },
	{ .name = "pl", MAPPING(keymap_pl), .description = "Polish QWERTZ" },
	{ .name = "se", MAPPING(keymap_se), .description = "Swedish" },

	{ .name = "us", MAPPING(keymap_uk), .description = "American" },
	{ .name = "dvorak", MAPPING(keymap_dvorak), .description = "DVORAK" },
};
