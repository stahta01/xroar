/** \file
 *
 *  \brief Host keyboard interface.
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
 *
 * Abstraction of keyboard handling.  Things like intercept for virtual
 * joystick done here.
 *
 * UI modules translate their idea of keypress / keyrelease to use these types.
 * User can then configure consistently whichever toolkit is in use.
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "c-strcase.h"
#include "delegate.h"
#include "slist.h"
#include "xalloc.h"

#include "dkbd.h"
#include "hkbd.h"
#include "keyboard.h"
#include "logging.h"
#include "messenger.h"
#include "ui.h"
#include "vdrive.h"
#include "xconfig.h"
#include "xroar.h"

#ifdef HAVE_X11
#include "x11/hkbd_x11.h"
#endif

#ifdef WINDOWS32
#include "windows32/hkbd_windows.h"
#endif

#ifdef HAVE_COCOA
#include "macosx/hkbd_darwin.h"
#endif

// If an OS-specific intialisation was able to generate a mapping table to
// HK scancodes, they will be here:

int hk_num_os_scancodes = 0;  // size of following table
uint8_t *os_scancode_to_hk_scancode = NULL;

// Uncomment to include code to check the symbol_names[] array is in order
//#define HKBD_DEBUG

struct hkbd hkbd;

#include "hkbd_lang_tables.c"

// Scancodes taken from USB Hid Usage Tables, Keyboard/Keypad Page (0x07)
//
// For now we can guarantee that all scancodes fit within a uint8_t, and that
// scancode 0 is invalid.
//
// Names are for convenience of translating between different scancode schemes
// and might not have any relation to the symbols on that key.

static const char *scan_names[] = {
	NULL,           // 0x00
	NULL,           // 0x01
	NULL,           // 0x02
	NULL,           // 0x03
	"a",            // 0x04
	"b",            // 0x05
	"c",            // 0x06
	"d",            // 0x07
	"e",            // 0x08
	"f",            // 0x09
	"g",            // 0x0a
	"h",            // 0x0b
	"i",            // 0x0c
	"j",            // 0x0d
	"k",            // 0x0e
	"l",            // 0x0f

	"m",            // 0x10
	"n",            // 0x11
	"o",            // 0x12
	"p",            // 0x13
	"q",            // 0x14
	"r",            // 0x15
	"s",            // 0x16
	"t",            // 0x17
	"u",            // 0x18
	"v",            // 0x19
	"w",            // 0x1a
	"x",            // 0x1b
	"y",            // 0x1c
	"z",            // 0x1d
	"1",            // 0x1e
	"2",            // 0x1f

	"3",            // 0x20
	"4",            // 0x21
	"5",            // 0x22
	"6",            // 0x23
	"7",            // 0x24
	"8",            // 0x25
	"9",            // 0x26
	"0",            // 0x27
	"Return",       // 0x28
	"Escape",       // 0x29
	"BackSpace",    // 0x2a
	"Tab",          // 0x2b
	"space",        // 0x2c
	"minus",        // 0x2d
	"equal",        // 0x2e
	"bracketleft",  // 0x2f

	"bracketright", // 0x30
	"backslash",    // 0x31
	"numbersign_nonUS",   // 0x32
	"semicolon",    // 0x33
	"apostrophe",   // 0x34
	"grave",        // 0x35
	"comma",        // 0x36
	"period",       // 0x37
	"slash",        // 0x38
	"Caps_Lock",    // 0x39
	"F1",           // 0x3a
	"F2",           // 0x3b
	"F3",           // 0x3c
	"F4",           // 0x3d
	"F5",           // 0x3e
	"F6",           // 0x3f

	"F7",           // 0x40
	"F8",           // 0x41
	"F9",           // 0x42
	"F10",          // 0x43
	"F11",          // 0x44
	"F12",          // 0x45
	"Print",        // 0x46
	"Scroll_Lock",  // 0x47
	"Pause",        // 0x48
	"Insert",       // 0x49
	"Home",         // 0x4a
	"Page_Up",      // 0x4b
	"Delete",       // 0x4c
	"End",          // 0x4d
	"Page_Down",    // 0x4e
	"Right",        // 0x4f

	"Left",         // 0x50
	"Down",         // 0x51
	"Up",           // 0x52
	"Num_Lock",     // 0x53
	"KP_Divide",    // 0x54
	"KP_Multiply",  // 0x55
	"KP_Subtract",  // 0x56
	"KP_Add",       // 0x57
	"KP_Enter",     // 0x58
	"KP_1",         // 0x59
	"KP_2",         // 0x5a
	"KP_3",         // 0x5b
	"KP_4",         // 0x5c
	"KP_5",         // 0x5d
	"KP_6",         // 0x5e
	"KP_7",         // 0x5f

	"KP_8",         // 0x60
	"KP_9",         // 0x61
	"KP_0",         // 0x62
	"KP_Decimal",   // 0x63
	"backslash_nonUS",      // 0x64
	"Application",  // 0x65
	"Power",        // 0x66
	"KP_Equal",     // 0x67,  // but see 0x86 below ???
	"F13",          // 0x68
	"F14",          // 0x69
	"F15",          // 0x6a
	"F16",          // 0x6b
	"F17",          // 0x6c
	"F18",          // 0x6d
	"F19",          // 0x6e
	"F20",          // 0x6f

	"F21",          // 0x70
	"F22",          // 0x71
	"F23",          // 0x72
	"F24",          // 0x73
	"Execute",      // 0x74
	"Help",         // 0x75
	"Menu",         // 0x76
	"Select",       // 0x77
	"Cancel",       // 0x78
	"Redo",         // 0x79
	"Undo",         // 0x7a
	"Cut",          // 0x7b
	"Copy",         // 0x7c
	"Paste",        // 0x7d
	"Find",         // 0x7e
	"Mute",         // 0x7f

	"Volume_Up",    // 0x80
	"Volume_Down",  // 0x81
	NULL,           // 0x82
	NULL,           // 0x83
	NULL,           // 0x84
	"KP_Separator", // 0x85
	NULL,   // 0x86 defined as "Keypad Equal Sign", where 0x67 is "Keypad =" - ???
	"International1",       // 0x87
	"International2",       // 0x88
	"International3",       // 0x89
	"International4",       // 0x8a
	"International5",       // 0x8b
	"International6",       // 0x8c
	"International7",       // 0x8d
	"International8",       // 0x8e
	"International9",       // 0x8f

	"Lang1",        // 0x90
	"Lang2",        // 0x91
	"Lang3",        // 0x92
	"Lang4",        // 0x93
	"Lang5",        // 0x94
	"Lang6",        // 0x95
	"Lang7",        // 0x96
	"Lang8",        // 0x97
	"Lang9",        // 0x98
	NULL,           // 0x99
	NULL,           // 0x9a
	NULL,           // 0x9b
	"Clear",        // 0x9c
	"Prior",        // 0x9d
	NULL,           // 0x9e
	NULL,           // 0x9f
};

static const char *scan_names_e0[] = {
	"Control_L",    // 0xe0
	"Shift_L",      // 0xe1
	"Alt_L",        // 0xe2
	"Super_L",      // 0xe3
	"Control_R",    // 0xe4
	"Shift_R",      // 0xe5
	"Alt_R",        // 0xe6
	"Super_R",      // 0xe7
};

// Key symbols.
//
// This enum is mostly Unicode values with X11 keysym names.  Non-Unicode
// values are taken from X11.

// Searched in order, so where two symbols have the same code, keep the more
// common name earlier in the list.

static struct {
	uint16_t sym;
	const char *name;
} symbol_names[] = {

	// TTY function keys

	{ hk_sym_BackSpace, "BackSpace" },
	{ hk_sym_Tab, "Tab" },
	{ hk_sym_Linefeed, "Linefeed" },
	{ hk_sym_Clear, "Clear" },
	{ hk_sym_Return, "Return" },
	{ hk_sym_Pause, "Pause" },
	{ hk_sym_Scroll_Lock, "Scroll_Lock" },
	{ hk_sym_Sys_Req, "Sys_Req" },
	{ hk_sym_Escape, "Escape" },
	{ hk_sym_Delete, "Delete" },

	// International & multi-key character composition

	{ hk_sym_Multi_key, "Multi_key" },
	{ hk_sym_Codeinput, "Codeinput" },
	{ hk_sym_Codeinput, "Kanji_Bangou" },
	{ hk_sym_SingleCandidate, "SingleCandidate" },
	{ hk_sym_MultipleCandidate, "MultipleCandidate" },
	{ hk_sym_MultipleCandidate, "Zen_Koho" },
	{ hk_sym_PreviousCandidate, "PreviousCandidate" },
	{ hk_sym_PreviousCandidate, "Mae_Koho" },

	// Japanese keyboard support

	{ hk_sym_Kanji, "Kanji" },
	{ hk_sym_Muhenkan, "Muhenkan" },
	{ hk_sym_Henkan, "Henkan" },
	{ hk_sym_Romaji, "Romaji" },
	{ hk_sym_Hiragana, "Hiragana" },
	{ hk_sym_Katakana, "Katakana" },
	{ hk_sym_Hiragana_Katakana, "Hiragana_Katakana" },
	{ hk_sym_Zenkaku, "Zenkaku" },
	{ hk_sym_Hankaku, "Hankaku" },
	{ hk_sym_Zenkaku_Hankaku, "Zenkaku_Hankaku" },
	{ hk_sym_Touroku, "Touroku" },
	{ hk_sym_Massyo, "Massyo" },
	{ hk_sym_Kana_Lock, "Kana_Lock" },
	{ hk_sym_Kana_Shift, "Kana_Shift" },
	{ hk_sym_Eisu_Shift, "Eisu_Shift" },
	{ hk_sym_Eisu_toggle, "Eisu_toggle" },

	// Cursor control & motion

	{ hk_sym_Home, "Home" },
	{ hk_sym_Left, "Left" },
	{ hk_sym_Up, "Up" },
	{ hk_sym_Right, "Right" },
	{ hk_sym_Down, "Down" },
	{ hk_sym_Page_Up, "Page_Up" },
	{ hk_sym_Prior, "Prior" },
	{ hk_sym_Page_Down, "Page_Down" },
	{ hk_sym_Next, "Next" },
	{ hk_sym_End, "End" },
	{ hk_sym_Begin, "Begin" },

	// Misc functions

	{ hk_sym_Select, "Select" },
	{ hk_sym_Print, "Print" },
	{ hk_sym_Execute, "Execute" },
	{ hk_sym_Insert, "Insert" },
	{ hk_sym_Undo, "Undo" },
	{ hk_sym_Redo, "Redo" },
	{ hk_sym_Menu, "Menu" },
	{ hk_sym_Find, "Find" },
	{ hk_sym_Cancel, "Cancel" },
	{ hk_sym_Help, "Help" },
	{ hk_sym_Break, "Break" },
	{ hk_sym_Volume_Down, "Volume_Down" },
	{ hk_sym_Mute, "Mute" },
	{ hk_sym_Volume_Up, "Volume_Up" },
	{ hk_sym_Mode_switch, "Mode_switch" },
	{ hk_sym_script_switch, "script_switch" },
	{ hk_sym_Num_Lock, "Num_Lock" },

	// Keypad functions

	{ hk_sym_KP_Space, "KP_Space" },
	{ hk_sym_KP_Tab, "KP_Tab" },
	{ hk_sym_KP_Enter, "KP_Enter" },
	{ hk_sym_KP_F1, "KP_F1" },
	{ hk_sym_KP_F2, "KP_F2" },
	{ hk_sym_KP_F3, "KP_F3" },
	{ hk_sym_KP_F4, "KP_F4" },
	{ hk_sym_KP_Home, "KP_Home" },
	{ hk_sym_KP_Left, "KP_Left" },
	{ hk_sym_KP_Up, "KP_Up" },
	{ hk_sym_KP_Right, "KP_Right" },
	{ hk_sym_KP_Down, "KP_Down" },
	{ hk_sym_KP_Page_Up, "KP_Page_Up" },
	{ hk_sym_KP_Prior, "Prior" },
	{ hk_sym_KP_Page_Down, "KP_Page_Down" },
	{ hk_sym_KP_Next, "Next" },
	{ hk_sym_KP_End, "KP_End" },
	{ hk_sym_KP_Begin, "KP_Begin" },
	{ hk_sym_KP_Insert, "KP_Insert" },
	{ hk_sym_KP_Delete, "KP_Delete" },
	{ hk_sym_KP_Equal, "KP_Equal" },
	{ hk_sym_KP_Multiply, "KP_Multiply" },
	{ hk_sym_KP_Add, "KP_Add" },
	{ hk_sym_KP_Separator, "KP_Separator" },
	{ hk_sym_KP_Subtract, "KP_Subtract" },
	{ hk_sym_KP_Decimal, "KP_Decimal" },
	{ hk_sym_KP_Divide, "KP_Divide" },

	{ hk_sym_KP_0, "KP_0" },
	{ hk_sym_KP_1, "KP_1" },
	{ hk_sym_KP_2, "KP_2" },
	{ hk_sym_KP_3, "KP_3" },
	{ hk_sym_KP_4, "KP_4" },
	{ hk_sym_KP_5, "KP_5" },
	{ hk_sym_KP_6, "KP_6" },
	{ hk_sym_KP_7, "KP_7" },
	{ hk_sym_KP_8, "KP_8" },
	{ hk_sym_KP_9, "KP_9" },

	// Auxiliary functions

	{ hk_sym_F1, "F1" },
	{ hk_sym_F2, "F2" },
	{ hk_sym_F3, "F3" },
	{ hk_sym_F4, "F4" },
	{ hk_sym_F5, "F5" },
	{ hk_sym_F6, "F6" },
	{ hk_sym_F7, "F7" },
	{ hk_sym_F8, "F8" },
	{ hk_sym_F9, "F9" },
	{ hk_sym_F10, "F10" },
	{ hk_sym_F11, "F11" },
	{ hk_sym_F12, "F12" },
	{ hk_sym_F13, "F13" },
	{ hk_sym_F14, "F14" },
	{ hk_sym_F15, "F15" },
	{ hk_sym_F16, "F16" },
	{ hk_sym_F17, "F17" },
	{ hk_sym_F18, "F18" },
	{ hk_sym_F19, "F19" },
	{ hk_sym_F20, "F20" },
	{ hk_sym_F21, "F21" },
	{ hk_sym_F22, "F22" },
	{ hk_sym_F23, "F23" },
	{ hk_sym_F24, "F24" },

	// Modifiers

	{ hk_sym_Shift_L, "Shift_L" },
	{ hk_sym_Shift_R, "Shift_R" },
	{ hk_sym_Control_L, "Control_L" },
	{ hk_sym_Control_R, "Control_R" },
	{ hk_sym_Caps_Lock, "Caps_Lock" },
	{ hk_sym_Shift_Lock, "Shift_Lock" },

	{ hk_sym_Meta_L, "Meta_L" },
	{ hk_sym_Meta_R, "Meta_R" },
	{ hk_sym_Alt_L, "Alt_L" },
	{ hk_sym_Alt_R, "Alt_R" },
	{ hk_sym_Super_L, "Super_L" },
	{ hk_sym_Super_R, "Super_R" },
	{ hk_sym_Hyper_L, "Hyper_L" },
	{ hk_sym_Hyper_R, "Hyper_R" },

	// Some ISO keys

	{ hk_sym_ISO_Lock, "ISO_Lock" },
	{ hk_sym_ISO_Level2_Latch, "ISO_Level2_Latch" },
	{ hk_sym_ISO_Level3_Shift, "ISO_Level3_Shift" },
	{ hk_sym_ISO_Level3_Latch, "ISO_Level3_Latch" },
	{ hk_sym_ISO_Level3_Lock, "ISO_Level3_Lock" },
	{ hk_sym_ISO_Level5_Shift, "ISO_Level5_Shift" },
	{ hk_sym_ISO_Level5_Latch, "ISO_Level5_Latch" },
	{ hk_sym_ISO_Level5_Lock, "ISO_Level5_Lock" },
	{ hk_sym_ISO_Group_Shift, "ISO_Group_Shift" },
	{ hk_sym_ISO_Group_Latch, "ISO_Group_Latch" },
	{ hk_sym_ISO_Group_Lock, "ISO_Group_Lock" },
	{ hk_sym_ISO_Next_Group, "ISO_Next_Group" },
	{ hk_sym_ISO_Next_Group_Lock, "ISO_Next_Group_Lock" },
	{ hk_sym_ISO_Prev_Group, "ISO_Prev_Group" },
	{ hk_sym_ISO_Prev_Group_Lock, "ISO_Prev_Group_Lock" },
	{ hk_sym_ISO_First_Group, "ISO_First_Group" },
	{ hk_sym_ISO_First_Group_Lock, "ISO_First_Group_Lock" },
	{ hk_sym_ISO_Last_Group, "ISO_Last_Group" },
	{ hk_sym_ISO_Last_Group_Lock, "ISO_Last_Group_Lock" },

	{ hk_sym_dead_grave, "dead_grave" },
	{ hk_sym_dead_acute, "dead_acute" },
	{ hk_sym_dead_circumflex, "dead_circumflex" },
	{ hk_sym_dead_tilde, "dead_tilde" },
	{ hk_sym_dead_perispomeni, "dead_perispomeni" },
	{ hk_sym_dead_macron, "dead_macron" },
	{ hk_sym_dead_breve, "dead_breve" },
	{ hk_sym_dead_abovedot, "dead_abovedot" },
	{ hk_sym_dead_diaeresis, "dead_diaeresis" },
	{ hk_sym_dead_abovering, "dead_abovering" },
	{ hk_sym_dead_doubleacute, "dead_doubleacute" },
	{ hk_sym_dead_caron, "dead_caron" },
	{ hk_sym_dead_cedilla, "dead_cedilla" },
	{ hk_sym_dead_ogonek, "dead_ogonek" },
	{ hk_sym_dead_iota, "dead_iota" },
	{ hk_sym_dead_voiced_sound, "dead_voiced_sound" },
	{ hk_sym_dead_semivoiced_sound, "dead_semivoiced_sound" },
	{ hk_sym_dead_belowdot, "dead_belowdot" },
	{ hk_sym_dead_hook, "dead_hook" },
	{ hk_sym_dead_horn, "dead_horn" },
	{ hk_sym_dead_stroke, "dead_stroke" },
	{ hk_sym_dead_abovecomma, "dead_abovecomma" },
	{ hk_sym_dead_psili, "dead_psili" },
	{ hk_sym_dead_abovereversedcomma, "dead_abovereversedcomma" },
	{ hk_sym_dead_dasia, "dead_dasia" },
	{ hk_sym_dead_doublegrave, "dead_doublegrave" },
	{ hk_sym_dead_belowring, "dead_belowring" },
	{ hk_sym_dead_belowmacron, "dead_belowmacron" },
	{ hk_sym_dead_belowcircumflex, "dead_belowcircumflex" },
	{ hk_sym_dead_belowtilde, "dead_belowtilde" },
	{ hk_sym_dead_belowbreve, "dead_belowbreve" },
	{ hk_sym_dead_belowdiaeresis, "dead_belowdiaeresis" },
	{ hk_sym_dead_invertedbreve, "dead_invertedbreve" },
	{ hk_sym_dead_belowcomma, "dead_belowcomma" },
	{ hk_sym_dead_currency, "dead_currency" },
	{ hk_sym_dead_greek, "dead_greek" },

	// Latin 1

	{ hk_sym_space, "space" },
	{ hk_sym_exclam, "exclam" },
	{ hk_sym_quotedbl, "quotedbl" },
	{ hk_sym_numbersign, "numbersign" },
	{ hk_sym_dollar, "dollar" },
	{ hk_sym_percent, "percent" },
	{ hk_sym_ampersand, "ampersand" },
	{ hk_sym_apostrophe, "apostrophe" },
	{ hk_sym_apostrophe, "quoteright" },
	{ hk_sym_parenleft, "parenleft" },
	{ hk_sym_parenright, "parenright" },
	{ hk_sym_asterisk, "asterisk" },
	{ hk_sym_plus, "plus" },
	{ hk_sym_comma, "comma" },
	{ hk_sym_minus, "minus" },
	{ hk_sym_period, "period" },
	{ hk_sym_slash, "slash" },
	{ hk_sym_0, "0" },
	{ hk_sym_1, "1" },
	{ hk_sym_2, "2" },
	{ hk_sym_3, "3" },
	{ hk_sym_4, "4" },
	{ hk_sym_5, "5" },
	{ hk_sym_6, "6" },
	{ hk_sym_7, "7" },
	{ hk_sym_8, "8" },
	{ hk_sym_9, "9" },
	{ hk_sym_colon, "colon" },
	{ hk_sym_semicolon, "semicolon" },
	{ hk_sym_less, "less" },
	{ hk_sym_equal, "equal" },
	{ hk_sym_greater, "greater" },
	{ hk_sym_question, "question" },
	{ hk_sym_at, "at" },
	{ hk_sym_A, "A" },
	{ hk_sym_B, "B" },
	{ hk_sym_C, "C" },
	{ hk_sym_D, "D" },
	{ hk_sym_E, "E" },
	{ hk_sym_F, "F" },
	{ hk_sym_G, "G" },
	{ hk_sym_H, "H" },
	{ hk_sym_I, "I" },
	{ hk_sym_J, "J" },
	{ hk_sym_K, "K" },
	{ hk_sym_L, "L" },
	{ hk_sym_M, "M" },
	{ hk_sym_N, "N" },
	{ hk_sym_O, "O" },
	{ hk_sym_P, "P" },
	{ hk_sym_Q, "Q" },
	{ hk_sym_R, "R" },
	{ hk_sym_S, "S" },
	{ hk_sym_T, "T" },
	{ hk_sym_U, "U" },
	{ hk_sym_V, "V" },
	{ hk_sym_W, "W" },
	{ hk_sym_X, "X" },
	{ hk_sym_Y, "Y" },
	{ hk_sym_Z, "Z" },
	{ hk_sym_bracketleft, "bracketleft" },
	{ hk_sym_backslash, "backslash" },
	{ hk_sym_bracketright, "bracketright" },
	{ hk_sym_asciicircum, "asciicircum" },
	{ hk_sym_underscore, "underscore" },
	{ hk_sym_grave, "grave" },
	{ hk_sym_a, "a" },
	{ hk_sym_b, "b" },
	{ hk_sym_c, "c" },
	{ hk_sym_d, "d" },
	{ hk_sym_e, "e" },
	{ hk_sym_f, "f" },
	{ hk_sym_g, "g" },
	{ hk_sym_h, "h" },
	{ hk_sym_i, "i" },
	{ hk_sym_j, "j" },
	{ hk_sym_k, "k" },
	{ hk_sym_l, "l" },
	{ hk_sym_m, "m" },
	{ hk_sym_n, "n" },
	{ hk_sym_o, "o" },
	{ hk_sym_p, "p" },
	{ hk_sym_q, "q" },
	{ hk_sym_r, "r" },
	{ hk_sym_s, "s" },
	{ hk_sym_t, "t" },
	{ hk_sym_u, "u" },
	{ hk_sym_v, "v" },
	{ hk_sym_w, "w" },
	{ hk_sym_x, "x" },
	{ hk_sym_y, "y" },
	{ hk_sym_z, "z" },
	{ hk_sym_braceleft, "braceleft" },
	{ hk_sym_bar, "bar" },
	{ hk_sym_braceright, "braceright" },
	{ hk_sym_asciitilde, "asciitilde" },

	{ hk_sym_nobreakspace, "nobreakspace" },
	{ hk_sym_exclamdown, "exclamdown" },
	{ hk_sym_cent, "cent" },
	{ hk_sym_sterling, "sterling" },
	{ hk_sym_currency, "currency" },
	{ hk_sym_yen, "yen" },
	{ hk_sym_brokenbar, "brokenbar" },
	{ hk_sym_section, "section" },
	{ hk_sym_diaeresis, "diaeresis" },
	{ hk_sym_copyright, "copyright" },
	{ hk_sym_ordfeminine, "ordfeminine" },
	{ hk_sym_guillemetleft, "guillemetleft" },
	{ hk_sym_notsign, "notsign" },
	{ hk_sym_hyphen, "hyphen" },
	{ hk_sym_registered, "registered" },
	{ hk_sym_macron, "macron" },
	{ hk_sym_degree, "degree" },
	{ hk_sym_plusminus, "plusminus" },
	{ hk_sym_twosuperior, "twosuperior" },
	{ hk_sym_threesuperior, "threesuperior" },
	{ hk_sym_acute, "acute" },
	{ hk_sym_mu, "mu" },
	{ hk_sym_paragraph, "paragraph" },
	{ hk_sym_periodcentered, "periodcentered" },
	{ hk_sym_cedilla, "cedilla" },
	{ hk_sym_onesuperior, "onesuperior" },
	{ hk_sym_masculine, "masculine" },
	{ hk_sym_guillemetright, "guillemetright" },
	{ hk_sym_onequarter, "onequarter" },
	{ hk_sym_onehalf, "onehalf" },
	{ hk_sym_threequarters, "threequarters" },
	{ hk_sym_questiondown, "questiondown" },
	{ hk_sym_Agrave, "Agrave" },
	{ hk_sym_Aacute, "Aacute" },
	{ hk_sym_Acircumflex, "Acircumflex" },
	{ hk_sym_Atilde, "Atilde" },
	{ hk_sym_Adiaeresis, "Adiaeresis" },
	{ hk_sym_Aring, "Aring" },
	{ hk_sym_AE, "AE" },
	{ hk_sym_Ccedilla, "Ccedilla" },
	{ hk_sym_Egrave, "Egrave" },
	{ hk_sym_Eacute, "Eacute" },
	{ hk_sym_Ecircumflex, "Ecircumflex" },
	{ hk_sym_Ediaeresis, "Ediaeresis" },
	{ hk_sym_Igrave, "Igrave" },
	{ hk_sym_Iacute, "Iacute" },
	{ hk_sym_Icircumflex, "Icircumflex" },
	{ hk_sym_Idiaeresis, "Idiaeresis" },
	{ hk_sym_ETH, "ETH" },
	{ hk_sym_Eth, "Eth" },
	{ hk_sym_Ntilde, "Ntilde" },
	{ hk_sym_Ograve, "Ograve" },
	{ hk_sym_Oacute, "Oacute" },
	{ hk_sym_Ocircumflex, "Ocircumflex" },
	{ hk_sym_Otilde, "Otilde" },
	{ hk_sym_Odiaeresis, "Odiaeresis" },
	{ hk_sym_multiply, "multiply" },
	{ hk_sym_Oslash, "Oslash" },
	{ hk_sym_Ooblique, "Ooblique" },
	{ hk_sym_Ugrave, "Ugrave" },
	{ hk_sym_Uacute, "Uacute" },
	{ hk_sym_Ucircumflex, "Ucircumflex" },
	{ hk_sym_Udiaeresis, "Udiaeresis" },
	{ hk_sym_Yacute, "Yacute" },
	{ hk_sym_THORN, "THORN" },
	{ hk_sym_Thorn, "Thorn" },
	{ hk_sym_ssharp, "ssharp" },
	{ hk_sym_agrave, "agrave" },
	{ hk_sym_aacute, "aacute" },
	{ hk_sym_acircumflex, "acircumflex" },
	{ hk_sym_atilde, "atilde" },
	{ hk_sym_adiaeresis, "adiaeresis" },
	{ hk_sym_aring, "aring" },
	{ hk_sym_ae, "ae" },
	{ hk_sym_ccedilla, "ccedilla" },
	{ hk_sym_egrave, "egrave" },
	{ hk_sym_eacute, "eacute" },
	{ hk_sym_ecircumflex, "ecircumflex" },
	{ hk_sym_ediaeresis, "ediaeresis" },
	{ hk_sym_igrave, "igrave" },
	{ hk_sym_iacute, "iacute" },
	{ hk_sym_icircumflex, "icircumflex" },
	{ hk_sym_idiaeresis, "idiaeresis" },
	{ hk_sym_eth, "eth" },
	{ hk_sym_ntilde, "ntilde" },
	{ hk_sym_ograve, "ograve" },
	{ hk_sym_oacute, "oacute" },
	{ hk_sym_ocircumflex, "ocircumflex" },
	{ hk_sym_otilde, "otilde" },
	{ hk_sym_odiaeresis, "odiaeresis" },
	{ hk_sym_division, "division" },
	{ hk_sym_oslash, "oslash" },
	{ hk_sym_ooblique, "ooblique" },
	{ hk_sym_ugrave, "ugrave" },
	{ hk_sym_uacute, "uacute" },
	{ hk_sym_ucircumflex, "ucircumflex" },
	{ hk_sym_udiaeresis, "udiaeresis" },
	{ hk_sym_yacute, "yacute" },
	{ hk_sym_thorn, "thorn" },
	{ hk_sym_ydiaeresis, "ydiaeresis" },

	// Latin 2

	{ hk_sym_Aogonek, "Aogonek" },
	{ hk_sym_breve, "breve" },
	{ hk_sym_Lstroke, "Lstroke" },
	{ hk_sym_Lcaron, "Lcaron" },
	{ hk_sym_Sacute, "Sacute" },
	{ hk_sym_Scaron, "Scaron" },
	{ hk_sym_Scedilla, "Scedilla" },
	{ hk_sym_Tcaron, "Tcaron" },
	{ hk_sym_Zacute, "Zacute" },
	{ hk_sym_Zcaron, "Zcaron" },
	{ hk_sym_Zabovedot, "Zabovedot" },
	{ hk_sym_aogonek, "aogonek" },
	{ hk_sym_ogonek, "ogonek" },
	{ hk_sym_lstroke, "lstroke" },
	{ hk_sym_lcaron, "lcaron" },
	{ hk_sym_sacute, "sacute" },
	{ hk_sym_caron, "caron" },
	{ hk_sym_scaron, "scaron" },
	{ hk_sym_scedilla, "scedilla" },
	{ hk_sym_tcaron, "tcaron" },
	{ hk_sym_zacute, "zacute" },
	{ hk_sym_doubleacute, "doubleacute" },
	{ hk_sym_zcaron, "zcaron" },
	{ hk_sym_zabovedot, "zabovedot" },
	{ hk_sym_Racute, "Racute" },
	{ hk_sym_Abreve, "Abreve" },
	{ hk_sym_Lacute, "Lacute" },
	{ hk_sym_Cacute, "Cacute" },
	{ hk_sym_Ccaron, "Ccaron" },
	{ hk_sym_Eogonek, "Eogonek" },
	{ hk_sym_Ecaron, "Ecaron" },
	{ hk_sym_Dcaron, "Dcaron" },
	{ hk_sym_Dstroke, "Dstroke" },
	{ hk_sym_Nacute, "Nacute" },
	{ hk_sym_Ncaron, "Ncaron" },
	{ hk_sym_Odoubleacute, "Odoubleacute" },
	{ hk_sym_Rcaron, "Rcaron" },
	{ hk_sym_Uring, "Uring" },
	{ hk_sym_Udoubleacute, "Udoubleacute" },
	{ hk_sym_Tcedilla, "Tcedilla" },
	{ hk_sym_racute, "racute" },
	{ hk_sym_abreve, "abreve" },
	{ hk_sym_lacute, "lacute" },
	{ hk_sym_cacute, "cacute" },
	{ hk_sym_ccaron, "ccaron" },
	{ hk_sym_eogonek, "eogonek" },
	{ hk_sym_ecaron, "ecaron" },
	{ hk_sym_dcaron, "dcaron" },
	{ hk_sym_dstroke, "dstroke" },
	{ hk_sym_nacute, "nacute" },
	{ hk_sym_ncaron, "ncaron" },
	{ hk_sym_odoubleacute, "odoubleacute" },
	{ hk_sym_rcaron, "rcaron" },
	{ hk_sym_uring, "uring" },
	{ hk_sym_udoubleacute, "udoubleacute" },
	{ hk_sym_tcedilla, "tcedilla" },
	{ hk_sym_abovedot, "abovedot" },

	// Latin 3

	{ hk_sym_Hstroke, "Hstroke" },
	{ hk_sym_Hcircumflex, "Hcircumflex" },
	{ hk_sym_Iabovedot, "Iabovedot" },
	{ hk_sym_Gbreve, "Gbreve" },
	{ hk_sym_Jcircumflex, "Jcircumflex" },
	{ hk_sym_hstroke, "hstroke" },
	{ hk_sym_hcircumflex, "hcircumflex" },
	{ hk_sym_idotless, "idotless" },
	{ hk_sym_gbreve, "gbreve" },
	{ hk_sym_jcircumflex, "jcircumflex" },
	{ hk_sym_Cabovedot, "Cabovedot" },
	{ hk_sym_Ccircumflex, "Ccircumflex" },
	{ hk_sym_Gabovedot, "Gabovedot" },
	{ hk_sym_Gcircumflex, "Gcircumflex" },
	{ hk_sym_Ubreve, "Ubreve" },
	{ hk_sym_Scircumflex, "Scircumflex" },
	{ hk_sym_cabovedot, "cabovedot" },
	{ hk_sym_ccircumflex, "ccircumflex" },
	{ hk_sym_gabovedot, "gabovedot" },
	{ hk_sym_gcircumflex, "gcircumflex" },
	{ hk_sym_ubreve, "ubreve" },
	{ hk_sym_scircumflex, "scircumflex" },

	// Latin 8

	{ hk_sym_Wcircumflex, "Wcircumflex" },
	{ hk_sym_wcircumflex, "wcircumflex" },
	{ hk_sym_Ycircumflex, "Ycircumflex" },
	{ hk_sym_ycircumflex, "ycircumflex" },

	{ hk_sym_Babovedot, "Babovedot" },
	{ hk_sym_babovedot, "babovedot" },
	{ hk_sym_Dabovedot, "Dabovedot" },
	{ hk_sym_dabovedot, "dabovedot" },
	{ hk_sym_Fabovedot, "Fabovedot" },
	{ hk_sym_fabovedot, "fabovedot" },
	{ hk_sym_Mabovedot, "Mabovedot" },
	{ hk_sym_mabovedot, "mabovedot" },
	{ hk_sym_Pabovedot, "Pabovedot" },
	{ hk_sym_pabovedot, "pabovedot" },
	{ hk_sym_Sabovedot, "Sabovedot" },
	{ hk_sym_sabovedot, "sabovedot" },
	{ hk_sym_Tabovedot, "Tabovedot" },
	{ hk_sym_tabovedot, "tabovedot" },
	{ hk_sym_Wgrave, "Wgrave" },
	{ hk_sym_wgrave, "wgrave" },
	{ hk_sym_Wacute, "Wacute" },
	{ hk_sym_wacute, "wacute" },
	{ hk_sym_Wdiaeresis, "Wdiaeresis" },
	{ hk_sym_wdiaeresis, "wdiaeresis" },
	{ hk_sym_Ygrave, "Ygrave" },
	{ hk_sym_ygrave, "ygrave" },

	// Latin 9

	{ hk_sym_OE, "OE" },
	{ hk_sym_oe, "oe" },
	{ hk_sym_Ydiaeresis, "Ydiaeresis" },

	// Greek

	{ hk_sym_Greek_ALPHAaccent, "Greek_ALPHAaccent" },
	{ hk_sym_Greek_EPSILONaccent, "Greek_EPSILONaccent" },
	{ hk_sym_Greek_ETAaccent, "Greek_ETAaccent" },
	{ hk_sym_Greek_IOTAaccent, "Greek_IOTAaccent" },
	{ hk_sym_Greek_IOTAdieresis, "Greek_IOTAdieresis" },
	{ hk_sym_Greek_OMICRONaccent, "Greek_OMICRONaccent" },
	{ hk_sym_Greek_UPSILONaccent, "Greek_UPSILONaccent" },
	{ hk_sym_Greek_UPSILONdieresis, "Greek_UPSILONdieresis" },
	{ hk_sym_Greek_OMEGAaccent, "Greek_OMEGAaccent" },
	{ hk_sym_Greek_accentdieresis, "Greek_accentdieresis" },
	{ hk_sym_Greek_horizbar, "Greek_horizbar" },
	{ hk_sym_Greek_alphaaccent, "Greek_alphaaccent" },
	{ hk_sym_Greek_epsilonaccent, "Greek_epsilonaccent" },
	{ hk_sym_Greek_etaaccent, "Greek_etaaccent" },
	{ hk_sym_Greek_iotaaccent, "Greek_iotaaccent" },
	{ hk_sym_Greek_iotadieresis, "Greek_iotadieresis" },
	{ hk_sym_Greek_iotaaccentdieresis, "Greek_iotaaccentdieresis" },
	{ hk_sym_Greek_omicronaccent, "Greek_omicronaccent" },
	{ hk_sym_Greek_upsilonaccent, "Greek_upsilonaccent" },
	{ hk_sym_Greek_upsilondieresis, "Greek_upsilondieresis" },
	{ hk_sym_Greek_upsilonaccentdieresis, "Greek_upsilonaccentdieresis" },
	{ hk_sym_Greek_omegaaccent, "Greek_omegaaccent" },
	{ hk_sym_Greek_ALPHA, "Greek_ALPHA" },
	{ hk_sym_Greek_BETA, "Greek_BETA" },
	{ hk_sym_Greek_GAMMA, "Greek_GAMMA" },
	{ hk_sym_Greek_DELTA, "Greek_DELTA" },
	{ hk_sym_Greek_EPSILON, "Greek_EPSILON" },
	{ hk_sym_Greek_ZETA, "Greek_ZETA" },
	{ hk_sym_Greek_ETA, "Greek_ETA" },
	{ hk_sym_Greek_THETA, "Greek_THETA" },
	{ hk_sym_Greek_IOTA, "Greek_IOTA" },
	{ hk_sym_Greek_KAPPA, "Greek_KAPPA" },
	{ hk_sym_Greek_LAMDA, "Greek_LAMDA" },
	{ hk_sym_Greek_LAMDA, "Greek_LAMBDA" },
	{ hk_sym_Greek_MU, "Greek_MU" },
	{ hk_sym_Greek_NU, "Greek_NU" },
	{ hk_sym_Greek_XI, "Greek_XI" },
	{ hk_sym_Greek_OMICRON, "Greek_OMICRON" },
	{ hk_sym_Greek_PI, "Greek_PI" },
	{ hk_sym_Greek_RHO, "Greek_RHO" },
	{ hk_sym_Greek_SIGMA, "Greek_SIGMA" },
	{ hk_sym_Greek_TAU, "Greek_TAU" },
	{ hk_sym_Greek_UPSILON, "Greek_UPSILON" },
	{ hk_sym_Greek_PHI, "Greek_PHI" },
	{ hk_sym_Greek_CHI, "Greek_CHI" },
	{ hk_sym_Greek_PSI, "Greek_PSI" },
	{ hk_sym_Greek_OMEGA, "Greek_OMEGA" },
	{ hk_sym_Greek_alpha, "Greek_alpha" },
	{ hk_sym_Greek_beta, "Greek_beta" },
	{ hk_sym_Greek_gamma, "Greek_gamma" },
	{ hk_sym_Greek_delta, "Greek_delta" },
	{ hk_sym_Greek_epsilon, "Greek_epsilon" },
	{ hk_sym_Greek_zeta, "Greek_zeta" },
	{ hk_sym_Greek_eta, "Greek_eta" },
	{ hk_sym_Greek_theta, "Greek_theta" },
	{ hk_sym_Greek_iota, "Greek_iota" },
	{ hk_sym_Greek_kappa, "Greek_kappa" },
	{ hk_sym_Greek_lamda, "Greek_lamda" },
	{ hk_sym_Greek_lamda, "Greek_lambda" },
	{ hk_sym_Greek_mu, "Greek_mu" },
	{ hk_sym_Greek_nu, "Greek_nu" },
	{ hk_sym_Greek_xi, "Greek_xi" },
	{ hk_sym_Greek_omicron, "Greek_omicron" },
	{ hk_sym_Greek_pi, "Greek_pi" },
	{ hk_sym_Greek_rho, "Greek_rho" },
	{ hk_sym_Greek_sigma, "Greek_sigma" },
	{ hk_sym_Greek_finalsmallsigma, "Greek_finalsmallsigma" },
	{ hk_sym_Greek_tau, "Greek_tau" },
	{ hk_sym_Greek_upsilon, "Greek_upsilon" },
	{ hk_sym_Greek_phi, "Greek_phi" },
	{ hk_sym_Greek_chi, "Greek_chi" },
	{ hk_sym_Greek_psi, "Greek_psi" },
	{ hk_sym_Greek_omega, "Greek_omega" },

	// Technical

	{ hk_sym_leftradical, "leftradical" },
	{ hk_sym_topintegral, "topintegral" },
	{ hk_sym_botintegral, "botintegral" },
	{ hk_sym_topleftsqbracket, "topleftsqbracket" },
	{ hk_sym_botleftsqbracket, "botleftsqbracket" },
	{ hk_sym_toprightsqbracket, "toprightsqbracket" },
	{ hk_sym_botrightsqbracket, "botrightsqbracket" },
	{ hk_sym_topleftparens, "topleftparens" },
	{ hk_sym_botleftparens, "botleftparens" },
	{ hk_sym_toprightparens, "toprightparens" },
	{ hk_sym_botrightparens, "botrightparens" },
	{ hk_sym_leftmiddlecurlybrace, "leftmiddlecurlybrace" },
	{ hk_sym_rightmiddlecurlybrace, "rightmiddlecurlybrace" },
	{ hk_sym_lessthanequal, "lessthanequal" },
	{ hk_sym_notequal, "notequal" },
	{ hk_sym_greaterthanequal, "greaterthanequal" },
	{ hk_sym_integral, "integral" },
	{ hk_sym_therefore, "therefore" },
	{ hk_sym_variation, "variation" },
	{ hk_sym_infinity, "infinity" },
	{ hk_sym_nabla, "nabla" },
	{ hk_sym_approximate, "approximate" },
	{ hk_sym_similarequal, "similarequal" },
	{ hk_sym_ifonlyif, "ifonlyif" },
	{ hk_sym_implies, "implies" },
	{ hk_sym_identical, "identical" },
	{ hk_sym_radical, "radical" },
	{ hk_sym_includedin, "includedin" },
	{ hk_sym_includes, "includes" },
	{ hk_sym_intersection, "intersection" },
	{ hk_sym_union, "union" },
	{ hk_sym_logicaland, "logicaland" },
	{ hk_sym_logicalor, "logicalor" },
	{ hk_sym_partialderivative, "partialderivative" },
	{ hk_sym_function, "function" },
	{ hk_sym_leftarrow, "leftarrow" },
	{ hk_sym_uparrow, "uparrow" },
	{ hk_sym_rightarrow, "rightarrow" },
	{ hk_sym_downarrow, "downarrow" },

	// Publishing

	{ hk_sym_emspace, "emspace" },
	{ hk_sym_enspace, "enspace" },
	{ hk_sym_em3space, "em3space" },
	{ hk_sym_em4space, "em4space" },
	{ hk_sym_digitspace, "digitspace" },
	{ hk_sym_punctspace, "punctspace" },
	{ hk_sym_thinspace, "thinspace" },
	{ hk_sym_hairspace, "hairspace" },
	{ hk_sym_emdash, "emdash" },
	{ hk_sym_endash, "endash" },
	{ hk_sym_ellipsis, "ellipsis" },
	{ hk_sym_doubbaselinedot, "doubbaselinedot" },
	{ hk_sym_onethird, "onethird" },
	{ hk_sym_twothirds, "twothirds" },
	{ hk_sym_onefifth, "onefifth" },
	{ hk_sym_twofifths, "twofifths" },
	{ hk_sym_threefifths, "threefifths" },
	{ hk_sym_fourfifths, "fourfifths" },
	{ hk_sym_onesixth, "onesixth" },
	{ hk_sym_fivesixths, "fivesixths" },
	{ hk_sym_oneeighth, "oneeighth" },
	{ hk_sym_threeeighths, "threeeighths" },
	{ hk_sym_fiveeighths, "fiveeighths" },
	{ hk_sym_seveneighths, "seveneighths" },
	{ hk_sym_trademark, "trademark" },
	{ hk_sym_leftsinglequotemark, "leftsinglequotemark" },
	{ hk_sym_rightsinglequotemark, "rightsinglequotemark" },
	{ hk_sym_leftdoublequotemark, "leftdoublequotemark" },
	{ hk_sym_rightdoublequotemark, "rightdoublequotemark" },
	{ hk_sym_permille, "permille" },
	{ hk_sym_dagger, "dagger" },
	{ hk_sym_doubledagger, "doubledagger" },
	{ hk_sym_singlelowquotemark, "singlelowquotemark" },
	{ hk_sym_doublelowquotemark, "doublelowquotemark" },

	// Caucasus

	{ hk_sym_Xabovedot, "Xabovedot" },
	{ hk_sym_Ibreve, "Ibreve" },
	{ hk_sym_Zstroke, "Zstroke" },
	{ hk_sym_Gcaron, "Gcaron" },
	{ hk_sym_Ocaron, "Ocaron" },
	{ hk_sym_Obarred, "Obarred" },
	{ hk_sym_xabovedot, "xabovedot" },
	{ hk_sym_ibreve, "ibreve" },
	{ hk_sym_zstroke, "zstroke" },
	{ hk_sym_gcaron, "gcaron" },
	{ hk_sym_ocaron, "ocaron" },
	{ hk_sym_obarred, "obarred" },
	{ hk_sym_SCHWA, "SCHWA" },
	{ hk_sym_schwa, "schwa" },
	{ hk_sym_EZH, "EZH" },
	{ hk_sym_ezh, "ezh" },

	// Currency

	{ hk_sym_EuroSign, "EuroSign" },

	// Additional

	{ hk_sym_YOGH, "YOGH" },
	{ hk_sym_yogh, "yogh" },
};

//

static struct {
	uint8_t code;
	int8_t dkey;
	_Bool preempt;  // key overrides unicode translation
} code_dkey_default[] = {
	// Rest of the normal keys
	{ hk_scan_0, DSCAN_0, 0 },
	{ hk_scan_minus, DSCAN_COLON, 0 },
	{ hk_scan_equal, DSCAN_MINUS, 0 },
	{ hk_scan_bracketleft, DSCAN_AT, 0 },
	{ hk_scan_semicolon, DSCAN_SEMICOLON, 0 },
	{ hk_scan_comma, DSCAN_COMMA, 0 },
	{ hk_scan_period, DSCAN_FULL_STOP, 0 },
	{ hk_scan_slash, DSCAN_SLASH, 0 },

	// Common
	{ hk_scan_Escape, DSCAN_BREAK, 1 },
	{ hk_scan_Return, DSCAN_ENTER, 0 },
	{ hk_scan_Home, DSCAN_CLEAR, 1 },
	{ hk_scan_Shift_L, DSCAN_SHIFT, 1 },
	{ hk_scan_Shift_R, DSCAN_SHIFT, 1 },
	{ hk_scan_space, DSCAN_SPACE, 0 },

	// Not so common
	{ hk_scan_Clear, DSCAN_CLEAR, 1 },

	// Cursor keys
	{ hk_scan_Up, DSCAN_UP, 1 },
	{ hk_scan_Down, DSCAN_DOWN, 1 },
	{ hk_scan_Left, DSCAN_LEFT, 1 },
	{ hk_scan_Right, DSCAN_RIGHT, 1 },
	{ hk_scan_BackSpace, DSCAN_BACKSPACE, 1 },
	{ hk_scan_Delete, DSCAN_BACKSPACE, 1 },
	{ hk_scan_Tab, DSCAN_RIGHT, 1 },

	// CoCo 3
	//{ SDL_SCANCODE_MODE, DSCAN_ALT, 1 },  // _MODE ???
	{ hk_scan_Alt_L, DSCAN_ALT, 1 },
	{ hk_scan_Caps_Lock, DSCAN_CTRL, 1 },
	{ hk_scan_Super_L, DSCAN_CTRL, 1 },
	{ hk_scan_Super_R, DSCAN_CTRL, 1 },
	{ hk_scan_F1, DSCAN_F1, 1 },
	{ hk_scan_F2, DSCAN_F2, 1 },

	// Keypad
	{ hk_scan_KP_Multiply, DSCAN_COLON, 1 },
	{ hk_scan_KP_Subtract, DSCAN_MINUS, 1 },
	{ hk_scan_KP_Add, DSCAN_SEMICOLON, 1 },
	{ hk_scan_KP_Decimal, DSCAN_FULL_STOP, 1 },
	{ hk_scan_KP_Divide, DSCAN_SLASH, 1 },
	{ hk_scan_KP_Enter, DSCAN_ENTER, 0 },
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct xconfig_enum hkbd_layout_list[] = {
	{ XC_ENUM_INT("auto", hk_layout_auto, "Automatic") },

	{ XC_ENUM_INT("unix", hk_layout_unix, "UNIX") },
	{ XC_ENUM_INT("ansi", hk_layout_ansi, "ANSI") },
	{ XC_ENUM_INT("iso", hk_layout_iso, "ISO") },
	{ XC_ENUM_INT("jis", hk_layout_jis, "JIS") },

	{ XC_ENUM_END() }
};

struct xconfig_enum hkbd_lang_list[] = {
	{ XC_ENUM_INT("auto", hk_lang_auto, "Automatic") },

	{ XC_ENUM_INT("be", hk_lang_be, "Belgian") },
	{ XC_ENUM_INT("br", hk_lang_br, "Brazilian") },
	{ XC_ENUM_INT("de", hk_lang_de, "German") },
	{ XC_ENUM_INT("dk", hk_lang_dk, "Danish") },
	{ XC_ENUM_INT("es", hk_lang_es, "Spanish") },
	{ XC_ENUM_INT("fi", hk_lang_fi, "Finnish") },
	{ XC_ENUM_INT("fr", hk_lang_fr, "French") },
	{ XC_ENUM_INT("fr_CA", hk_lang_fr_CA, "Canadian French") },
	{ XC_ENUM_INT("gb", hk_lang_gb, "British English") },
	{ XC_ENUM_INT("is", hk_lang_is, "Icelandic") },
	{ XC_ENUM_INT("it", hk_lang_it, "Italian") },
	{ XC_ENUM_INT("jp", hk_lang_jp, "Japanese (JIS)") },
	{ XC_ENUM_INT("nl", hk_lang_nl, "Dutch") },
	{ XC_ENUM_INT("no", hk_lang_no, "Norwegian") },
	{ XC_ENUM_INT("pl", hk_lang_pl, "Polish QWERTY") },
	{ XC_ENUM_INT("pl_QWERTZ", hk_lang_pl_QWERTZ, "Polish QWERTZ") },
	{ XC_ENUM_INT("se", hk_lang_se, "Swedish") },
	{ XC_ENUM_INT("us", hk_lang_us, "American English") },
	{ XC_ENUM_INT("dvorak", hk_lang_dvorak, "DVORAK") },

	{ XC_ENUM_INT("cymru", hk_lang_gb, NULL) },
	{ XC_ENUM_INT("eng", hk_lang_gb, NULL) },
	{ XC_ENUM_INT("ie", hk_lang_gb, NULL) },
	{ XC_ENUM_INT("scot", hk_lang_gb, NULL) },
	{ XC_ENUM_INT("wales", hk_lang_gb, NULL) },

	{ XC_ENUM_END() }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool hk_default_update_keymap(void);
static void apply_lang_table(unsigned lang);

static _Bool is_dragon_key(uint16_t sym);
static void emulator_command(uint16_t sym, _Bool shift);

static void hk_ui_set_hkbd_layout(void *, int tag, void *smsg);
static void hk_ui_set_hkbd_lang(void *, int tag, void *smsg);
static void hk_ui_set_kbd_translate(void *, int tag, void *smsg);

_Bool hkbd_js_keypress(uint8_t code);
_Bool hkbd_js_keyrelease(uint8_t code);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void hk_init(void) {
	// Register with messenger
	hkbd.msgr_client_id = messenger_client_register();

	ui_messenger_preempt_group(hkbd.msgr_client_id, ui_tag_hkbd_layout, MESSENGER_NOTIFY_DELEGATE(hk_ui_set_hkbd_layout, &hkbd));
	ui_messenger_preempt_group(hkbd.msgr_client_id, ui_tag_hkbd_lang, MESSENGER_NOTIFY_DELEGATE(hk_ui_set_hkbd_lang, &hkbd));
	ui_messenger_preempt_group(hkbd.msgr_client_id, ui_tag_kbd_translate, MESSENGER_NOTIFY_DELEGATE(hk_ui_set_kbd_translate, &hkbd));

	// Initialise to a known state
	hk_update_keymap();
}

void hk_shutdown(void) {
	messenger_client_unregister(hkbd.msgr_client_id);
	if (os_scancode_to_hk_scancode) {
		free(os_scancode_to_hk_scancode);
		os_scancode_to_hk_scancode = NULL;
	}
}

void hk_update_keymap(void) {
	// Clear any old mappings
	for (int c = 0; c < HK_NUM_SCANCODES; c++) {
		for (int l = 0; l < HK_NUM_LEVELS; l++) {
			hkbd.code_to_sym[l][c] = hk_sym_None;
		}
		hkbd.scancode_mod[c] = 0;
	}

	hkbd.layout = hkbd.cfg.layout;

	if (os_scancode_to_hk_scancode) {
		free(os_scancode_to_hk_scancode);
		os_scancode_to_hk_scancode = NULL;
	}

	// Any OS-specific defaults
	_Bool have_keymap = 0;
#if defined(HAVE_X11)
	have_keymap = have_keymap || hk_x11_update_keymap();
#elif defined(WINDOWS32)
	have_keymap = have_keymap || hk_windows_update_keymap();
#elif defined(HAVE_COCOA)
	have_keymap = have_keymap || hk_darwin_update_keymap();
#endif
	if (hkbd.cfg.lang != hk_lang_auto) {
		have_keymap = 0;
	}
	have_keymap = have_keymap || hk_default_update_keymap();

	// For empty shift levels, duplicate the lower one
	for (unsigned c = 0; c < HK_NUM_SCANCODES; c++) {
		if (hkbd.code_to_sym[1][c] == hk_sym_None) {
			hkbd.code_to_sym[1][c] = hkbd.code_to_sym[0][c];
		}
		if (hkbd.code_to_sym[2][c] == hk_sym_None &&
		    hkbd.code_to_sym[3][c] == hk_sym_None) {
			hkbd.code_to_sym[2][c] = hkbd.code_to_sym[0][c];
			hkbd.code_to_sym[3][c] = hkbd.code_to_sym[1][c];
		}
	}

	// Mappings to emulated keyboard

	// Clear mapping
	for (unsigned i = 0; i < HK_NUM_SCANCODES; i++) {
		hkbd.code_to_dkey[i] = DSCAN_INVALID;
		hkbd.code_preempt[i] = 0;
		hkbd.scancode_pressed_sym[i] = hk_sym_None;
	}

	// Default mappings.  From table:
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(code_dkey_default); i++) {
		hkbd.code_to_dkey[code_dkey_default[i].code] = code_dkey_default[i].dkey;
		hkbd.code_preempt[code_dkey_default[i].code] = code_dkey_default[i].preempt;
	}

	// Most of the preempting entries in that table are uncontroversial.
	// But it turns out that in some language mappings, the "grave" keycode
	// generates a key useful in translated mode.  So if either of the
	// lower shift levels for it look like normal ASCII, don't preempt:

	if (!is_dragon_key(hkbd.code_to_sym[0][hk_scan_grave]) && !is_dragon_key(hkbd.code_to_sym[1][hk_scan_grave])) {
		hkbd.code_to_dkey[hk_scan_grave] = DSCAN_CLEAR;
		hkbd.code_preempt[hk_scan_grave] = 1;
	}

	// 1-9 (0 is in the table):
	for (unsigned i = 0; i < 9; i++) {
		hkbd.code_to_dkey[hk_scan_1 + i] = (int8_t)(DSCAN_1 + i);
		hkbd.code_to_dkey[hk_scan_KP_1 + i] = (int8_t)(DSCAN_1 + i);
	}
	// a-z:
	for (unsigned i = 0; i <= 25; i++) {
		hkbd.code_to_dkey[hk_scan_a + i] = (int8_t)(DSCAN_A + i);
	}

	// Apply user-supplied binds:
	for (struct slist *iter = xroar.cfg.kbd.bind_list; iter; iter = iter->next) {
		struct dkbd_bind *bind = (struct dkbd_bind *)iter->data;
		uint8_t code = hk_scancode_from_name(bind->hostkey);
		if (code != hk_scan_None) {
			hkbd.code_to_dkey[code] = bind->dk_key;
			hkbd.code_preempt[code] = bind->preempt;
		} else {
			LOG_WARN("[hkbd] key named '%s' not found\n", bind->hostkey);
		}
	}
}

void hk_focus_in(void) {
	_Bool done = 0;
#if defined(HAVE_X11)
	done = done || hk_x11_focus_in();
#endif
	// Default to just releasing any key marked as pressed.
	if (!done) {
		for (unsigned i = 0; i < HK_NUM_SCANCODES; i++) {
			if (hkbd.scancode_pressed_sym[i] != hk_sym_None) {
				hk_scan_release(i);
			}
		}
		// And for good measure
		hkbd.state = 0;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Metadata

// Convert to and from scancode values and name strings.  Names return from
// hk_name_from_scancode() will be capitalised as in enum hk_scan.  Input to
// hk_scancode_from_name() is case-insensitive.  Modifiers ignored in these
// calls.

const char *hk_name_from_scancode(uint8_t code) {
	if (code < 0xe0) {
		if (code < ARRAY_N_ELEMENTS(scan_names) && scan_names[code])
			return scan_names[code];
	} else {
		unsigned code_e0 = code - 0xe0;
		if (code_e0 < ARRAY_N_ELEMENTS(scan_names_e0) && scan_names_e0[code_e0])
			return scan_names_e0[code_e0];
	}

	static char buf[5];
	snprintf(buf, sizeof(buf), "0x%02x", code);
	return buf;
}

uint8_t hk_scancode_from_name(const char *name) {
	if (!name) {
		return hk_scan_None;
	}
	if (*name == '0' && *(name+1) == 'x') {
		return (uint8_t)strtol(name, NULL, 16);
	}

	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(scan_names); i++) {
		if (scan_names[i] && c_strcasecmp(name, scan_names[i]) == 0)
			return (uint8_t)i;
	}

	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(scan_names_e0); i++) {
		if (scan_names_e0[i] && c_strcasecmp(name, scan_names_e0[i]) == 0)
			return (uint8_t)(i + 0xe0);
	}

	return hk_scan_None;
}

// Same for symbols.

const char *hk_name_from_symbol(uint16_t sym) {
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(symbol_names); i++) {
		if (sym == symbol_names[i].sym)
			return symbol_names[i].name;
	}

	static char buf[7];
	snprintf(buf, sizeof(buf), "0x%04x", sym);
	return buf;
};

uint16_t hk_symbol_from_name(const char *name) {
	if (!name) {
		return hk_sym_None;
	}
	if (*name == '0' && *(name+1) == 'x') {
		return (uint16_t)strtol(name, NULL, 16);
	}

	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(symbol_names); i++) {
		if (c_strcasecmp(name, symbol_names[i].name) == 0)
			return symbol_names[i].sym;
	}

	return hk_sym_None;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Actions

// Key press & release by scancode.

void hk_scan_press(uint8_t code) {
	if (!code)
		return;

	if (hkbd.scancode_mod[code]) {
		hkbd.state |= hkbd.scancode_mod[code];
	}
	_Bool shift = hkbd.state & HK_MASK_SHIFT;
	_Bool altgr = hkbd.state & HK_MASK_ALTGR;

	int level = (shift ? HK_LEVEL_SHIFT : 0) | (altgr ? HK_LEVEL_ALTGR : 0);
	uint16_t sym = hkbd.code_to_sym[level][code];
	hkbd.scancode_pressed_sym[code] = sym;

	if (logging.debug_ui & LOG_UI_KBD_EVENT) {
		LOG_PRINT("key press   scan=%3d(%16s)   state=%02x   sym=%04x(%16s)\n", code, hk_name_from_scancode(code), hkbd.state, sym, hk_name_from_symbol(sym));
	}

	// If Control is pressed, perform an emulator command.
	if (hkbd.state & HK_MASK_CONTROL) {
		uint16_t unshifted_sym = hkbd.code_to_sym[level&~HK_LEVEL_SHIFT][code];
		uint16_t shifted_sym = hkbd.code_to_sym[level|HK_LEVEL_SHIFT][code];
		if (shifted_sym >= hk_sym_0 && shifted_sym <= hk_sym_9) {
			sym = shifted_sym;
		} else if (unshifted_sym >= hk_sym_0 && unshifted_sym <= hk_sym_9) {
			sym = unshifted_sym;
		} else if (unshifted_sym >= hk_sym_a && unshifted_sym <= hk_sym_z) {
			sym = unshifted_sym;
		}
		emulator_command(sym, hkbd.state & HK_MASK_SHIFT);
		return;
	}

	// Test with keyboard virtual joystick handler
	if (hkbd_js_keypress(code)) {
		return;
	}

	// If scancode preempts
	if (hkbd.code_preempt[code]) {
		keyboard_press(xroar.keyboard_interface, hkbd.code_to_dkey[code]);
		return;
	}

	switch (sym) {
	case hk_sym_F11:
		ui_update_state(-1, ui_tag_fullscreen, UI_NEXT, NULL);
		return;

	case hk_sym_F12:
		if (shift) {
			ui_update_state(-1, ui_tag_ratelimit_latch, UI_NEXT, NULL);
		} else {
			ui_update_state(-1, ui_tag_ratelimit, 0, NULL);
		}
		return;

	case hk_sym_Pause:
		xroar_set_pause(1, XROAR_NEXT);
		return;

	default:
		break;
	}

	// Translated mode.  The HK symbol is usually its Unicode value so most
	// are used directly.  There are a few special supplementary cases.
	if (hkbd.translate) {
		unsigned unicode = sym;
		if (shift && (sym == hk_sym_BackSpace || sym == hk_sym_Delete)) {
			// shift + backspace -> erase line
			unicode = DKBD_U_ERASE_LINE;
		} else if (shift && hkbd.code_to_dkey[code] == DSCAN_ENTER) {
			// shift + enter -> caps lock
			unicode = DKBD_U_CAPS_LOCK;
		} else if (shift && hkbd.code_to_dkey[code] == DSCAN_SPACE) {
			// shift + space -> pause output
			unicode = DKBD_U_PAUSE_OUTPUT;
		} else switch (sym) {
		case hk_sym_BackSpace:
		case hk_sym_Delete: unicode = 8; break;
		case hk_sym_Tab: unicode = 9; break;
		case hk_sym_Clear: unicode = 12; break;
		case hk_sym_Return: unicode = 13; break;
		case hk_sym_Escape: unicode = 3; break;
		default: unicode = sym; break;
		}
		// Record computed Unicode value for this this scancode
		hkbd.scancode_pressed_unicode[code] = unicode;
		keyboard_unicode_press(xroar.keyboard_interface, unicode);
		return;
	}

	// Otherwise, just press the dkey bound to this scancode.
	keyboard_press(xroar.keyboard_interface, hkbd.code_to_dkey[code]);
}

void hk_scan_release(uint8_t code) {
	if (!code)
		return;

	if (hkbd.scancode_mod[code]) {
		hkbd.state &= ~hkbd.scancode_mod[code];
	}
	_Bool shift = hkbd.state & HK_MASK_SHIFT;
	_Bool altgr = hkbd.state & HK_MASK_ALTGR;

	int level = (shift ? HK_LEVEL_SHIFT : 0) | (altgr ? HK_LEVEL_ALTGR : 0);
	uint16_t sym = hkbd.scancode_pressed_sym[code];
	hkbd.scancode_pressed_sym[code] = hk_sym_None;
	if (!sym) {
		sym = hkbd.code_to_sym[level][code];
	}

	if (logging.debug_ui & LOG_UI_KBD_EVENT) {
		LOG_PRINT("key release scan=%3d(%16s)   state=%02x   sym=%04x(%16s)\n", code, hk_name_from_scancode(code), hkbd.state, sym, hk_name_from_symbol(sym));
	}

	// Test with keyboard virtual joystick handler
	if (hkbd_js_keyrelease(code)) {
		return;
	}

	// If scancode preempts
	if (hkbd.code_preempt[code]) {
		keyboard_release(xroar.keyboard_interface, hkbd.code_to_dkey[code]);
		return;
	}

	switch (sym) {
	case hk_sym_F12:
		ui_update_state(-1, ui_tag_ratelimit, 1, NULL);
		return;

	default:
		break;
	}

	if (hkbd.translate) {
		// Use the last recorded Unicode value for this scancode
		unsigned unicode = hkbd.scancode_pressed_unicode[code];
		keyboard_unicode_release(xroar.keyboard_interface, unicode);
		// Put shift back the way it should be
		if (shift) {
			KBD_MATRIX_PRESS(xroar.keyboard_interface, DSCAN_SHIFT);
		} else {
			KBD_MATRIX_RELEASE(xroar.keyboard_interface, DSCAN_SHIFT);
		}
		return;
	}

	keyboard_release(xroar.keyboard_interface, hkbd.code_to_dkey[code]);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Use a specific keyboard language table.  These are fixed, and should only be
// used as a last resort, or if the user explicitly specifies a language.

static _Bool hk_default_update_keymap(void) {
	// Initialise
	for (unsigned c = 0; c < HK_NUM_SCANCODES; c++) {
		for (unsigned l = 0; l < HK_NUM_LEVELS; l++) {
			hkbd.code_to_sym[l][c] = hk_sym_None;
		}
		hkbd.scancode_mod[c] = 0;
	}

	unsigned lang = (unsigned)hkbd.cfg.lang;
	if (hkbd.layout == hk_layout_auto) {
		// Japanese -> JIS, else ANSI
		hkbd.layout = lang == hk_lang_jp ? hk_layout_jis : hk_layout_ansi;
	}
	if (lang == hk_lang_auto) {
		// JIS -> Japanese, else GB
		lang = (hkbd.layout == hk_layout_jis) ? hk_lang_jp : hk_lang_gb;
	}

	apply_lang_table(0);  // default
	apply_lang_table(lang);

	for (unsigned c = 0; c < HK_NUM_SCANCODES; c++) {
		switch (hkbd.code_to_sym[0][c]) {
		case hk_sym_Shift_L:
		case hk_sym_Shift_R:
			hkbd.scancode_mod[c] = HK_MASK_SHIFT;
			break;

		case hk_sym_Control_L:
		case hk_sym_Control_R:
			hkbd.scancode_mod[c] = HK_MASK_CONTROL;
			break;

		case hk_sym_Alt_L:
			hkbd.scancode_mod[c] = HK_MASK_ALT;
			break;

		case hk_sym_Alt_R:
			hkbd.scancode_mod[c] = HK_MASK_ALTGR;
			break;

		case hk_sym_Super_L:
		case hk_sym_Super_R:
			hkbd.scancode_mod[c] = HK_MASK_SUPER;
			break;

		default:
			break;
		}
	}

	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Apply a keyboard language table (from hkbd_lang_tables.c).  Tables can
// specify other tables as dependencies, so may recurse.

static void apply_lang_table(unsigned lang) {
	if (lang >= ARRAY_N_ELEMENTS(lang_table)) {
		return;
	}
	const uint16_t *table = lang_table[lang];
	while (*table != HKL_END) {
		uint16_t flags = *(table++);
		uint8_t code = (uint8_t)(flags & 0xff);
		if (flags & HKL_LANG) {
			uint16_t inherit_lang = *(table++);
			apply_lang_table(inherit_lang);
		}
		if (flags & HKL_CLR) {
			for (unsigned l = 0; l < HK_NUM_LEVELS; l++) {
				hkbd.code_to_sym[l][code] = hk_sym_None;
			}
		}
		if (flags & HKL_SYM1) {
			hkbd.code_to_sym[0][code] = *(table++);
		}
		if (flags & HKL_SYM2) {
			hkbd.code_to_sym[1][code] = *(table++);
		}
		if (flags & HKL_SYM3) {
			hkbd.code_to_sym[2][code] = *(table++);
		}
		if (flags & HKL_SYM4) {
			hkbd.code_to_sym[3][code] = *(table++);
		}
		if (flags & HKL_DUP1) {
			if (flags & HKL_SYM1) {
				hkbd.code_to_sym[1][code] = hkbd.code_to_sym[0][code];
				flags |= HKL_SYM2;  // just for checking DUP12
			}
		}
		if (flags & HKL_DUP12) {
			if (flags & HKL_SYM1) {
				hkbd.code_to_sym[2][code] = hkbd.code_to_sym[0][code];
			}
			if (flags & HKL_SYM2) {
				hkbd.code_to_sym[3][code] = hkbd.code_to_sym[1][code];
			}
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Test whether a sym corresponds to a valid key on a Dragon or CoCo keyboard.
// Purely used to avoid mapping the 'grave' key as CLEAR if it's got a useful
// character on it.

static _Bool is_dragon_key(uint16_t sym) {
	if (sym >= hk_sym_space && sym <= hk_sym_asciicircum)
		return 1;
	if (sym >= hk_sym_a && sym <= hk_sym_z)
		return 1;
	switch (sym) {
	case hk_sym_BackSpace:
	case hk_sym_Tab:
	case hk_sym_Return:
	case hk_sym_Escape:
	case hk_sym_Delete:
		return 1;
	default:
		break;
	}
	return 0;
}

// Note that a lot of shortcuts are omitted in WebAssembly builds - browsers
// tend to steal all those keys for themselves.

static void emulator_command(uint16_t sym, _Bool shift) {
	switch (sym) {
	case hk_sym_1: case hk_sym_2: case hk_sym_3: case hk_sym_4:
		if (shift) {
			xroar_new_disk(sym - hk_sym_1);
		} else {
			xroar_insert_disk(sym - hk_sym_1);
		}
		break;

	case hk_sym_5: case hk_sym_6: case hk_sym_7: case hk_sym_8:
		if (shift) {
			ui_update_state(-1, ui_tag_disk_write_back, UI_NEXT, (void *)(intptr_t)(sym - hk_sym_5));
		} else {
			ui_update_state(-1, ui_tag_disk_write_enable, UI_NEXT, (void *)(intptr_t)(sym - hk_sym_5));
		}
		break;

	case hk_sym_a:
		ui_update_state(-1, ui_tag_tv_input, UI_NEXT, NULL);
		return;

	case hk_sym_d:
		if (shift) {
			vdrive_flush(xroar.vdrive_interface);
		} else {
			ui_update_state(-1, ui_tag_disk_dialog, UI_NEXT, NULL);
		}
		return;

	case hk_sym_e:
		ui_update_state(-1, ui_tag_cartridge, UI_NEXT, NULL);
		return;

	case hk_sym_f:
		ui_update_state(-1, ui_tag_fullscreen, UI_NEXT, NULL);
		return;

	case hk_sym_h:
		if (shift) {
			xroar_set_pause(1, XROAR_NEXT);
		}
		return;

	case hk_sym_i:
		if (shift) {
			ui_update_state(-1, ui_tag_vdg_inverse, UI_NEXT, NULL);
#ifndef HAVE_WASM
		} else {
			xroar_run_file();
#endif
		}
		return;

	case hk_sym_j:
		if (shift) {
			ui_update_state(-1, ui_tag_joystick_cycle, 1, NULL);
		} else {
			ui_update_state(-1, ui_tag_joystick_cycle, UI_NEXT, NULL);
		}
		return;

	case hk_sym_k:
		ui_update_state(-1, ui_tag_keymap, UI_NEXT, NULL);
		return;

#ifndef HAVE_WASM
	case hk_sym_l:
		if (shift) {
			xroar_run_file();
		} else {
			xroar_load_file();
		}
		return;

	case hk_sym_m:
		ui_update_state(-1, ui_tag_menubar, UI_NEXT, NULL);
		return;

	case hk_sym_p:
		if (shift) {
			xroar_flush_printer();
		} else {
			ui_update_state(-1, ui_tag_print_dialog, UI_NEXT, NULL);
		}
		return;

	case hk_sym_q:
		xroar_quit();
		return;
#endif

	case hk_sym_r:
		if (shift) {
			xroar_hard_reset();
		} else {
			xroar_soft_reset();
		}
		return;

#ifndef HAVE_WASM
	case hk_sym_s:
		if (shift) {
#ifdef SCREENSHOT
			xroar_screenshot();
#endif
		} else {
			xroar_save_snapshot();
		}
		return;

	case hk_sym_w:
		xroar_insert_output_tape();
		return;
#endif

	case hk_sym_t:
		ui_update_state(-1, ui_tag_tape_dialog, UI_NEXT, NULL);
		return;

	case hk_sym_v:
		if (shift) {
			ui_update_state(-1, ui_tag_tv_dialog, UI_NEXT, NULL);
		} else {
#ifdef TRACE
			xroar_set_trace(XROAR_NEXT);
#endif
		}
		return;

	case hk_sym_z:
		ui_update_state(-1, ui_tag_kbd_translate, UI_NEXT, NULL);
		return;

#ifndef HAVE_WASM
	case hk_sym_0:
		ui_update_state(-1, ui_tag_zoom, 0, NULL);
		return;

	case hk_sym_minus:
		ui_update_state(-1, ui_tag_zoom, UI_PREV, NULL);
		return;

	case hk_sym_plus:
		ui_update_state(-1, ui_tag_zoom, UI_NEXT, NULL);
		return;
#endif

	case hk_sym_comma:
	case hk_sym_less:
		ui_update_state(-1, ui_tag_picture, UI_NEXT, NULL);
		return;

	case hk_sym_period:
	case hk_sym_greater:
		ui_update_state(-1, ui_tag_picture, UI_PREV, NULL);
		return;

	default:
		break;
	}
}

static void hk_ui_set_hkbd_layout(void *sptr, int tag, void *smsg) {
	(void)sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_hkbd_layout);
	int layout = uimsg->value;
	if (uimsg->value == XROAR_NEXT) {
		layout = hkbd.cfg.layout + 1;
	} else if (uimsg->value == XROAR_PREV) {
		layout = hkbd.cfg.layout - 1;
	}
	if (layout < hk_layout_auto) {
		layout = hk_layout_jis;
	} else if (layout > hk_layout_jis) {
		layout = hk_layout_auto;
	}
	hkbd.cfg.layout = layout;
	hk_update_keymap();
	uimsg->value = layout;
}

static void hk_ui_set_hkbd_lang(void *sptr, int tag, void *smsg) {
	(void)sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_hkbd_lang);
	int lang = uimsg->value;
	if (uimsg->value == XROAR_NEXT) {
		lang = hkbd.cfg.lang + 1;
	} else if (uimsg->value == XROAR_PREV) {
		lang = hkbd.cfg.lang - 1;
	}
	if (lang < hk_lang_auto) {
		lang = hk_lang_dvorak;
	} else if (lang > hk_lang_dvorak) {
		lang = hk_lang_auto;
	}
	hkbd.cfg.lang = lang;
	hk_update_keymap();
	uimsg->value = lang;
}

static void hk_ui_set_kbd_translate(void *sptr, int tag, void *smsg) {
	(void)sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_kbd_translate);
	hkbd.translate = ui_msg_adjust_value_range(uimsg, hkbd.translate, 0, 0, 1,
						   UI_ADJUST_FLAG_CYCLE);
}
