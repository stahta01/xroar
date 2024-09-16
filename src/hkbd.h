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

// hk_init() will call OS-specific initialisation - hk_init_*() - which will
// try to do two things:
//
// 1. Identify how OS-native scancodes relate to real keyboards, and build a
// table - hkbd.os_scancode_to_hk_scancode - mapping those codes to USB HID
// style position based scancodes (hk_scan_*).
//
// - Under X11, a keyboard "fingerprint" is taken (technique borrowed from
// SDL), and the table maps X11 keycodes, supporting quite a few common
// mappings.
//
// - Under Windows, the table is fixed (bar user configuration) and is indexed
// by virtual scancode (VSC).  XXX not done yes as not required for SDL.
//
// - Under Mac OS X+, the table is similarly fixed, and is indexed by virtual
// keycode.  XXX not done yes as not required for SDL.
//
// - This table is intended to help UI modules use the scancode-based host
// keyboard interface.
//
// 2. Query which symbol is on each key at each relevant shift level, builidng
// another table - hkbd.code_to_sym - mapping hk_scan_* + level to symbol
// (hk_sym_*).

#ifndef XROAR_HKBD_H_
#define XROAR_HKBD_H_

#include <stdint.h>

#include "delegate.h"

#include "xconfig.h"

// Scancodes can be marked as affecting a number of modifiers.  Some modifiers
// will contribute to the "shift level" of a keypress, others may trigger bound
// functions.

#define HK_MOD_SHIFT    (0)
#define HK_MOD_CONTROL  (1)
#define HK_MOD_META     (2)
#define HK_MOD_ALT      (3)
#define HK_MOD_SUPER    (4)
#define HK_MOD_HYPER    (5)
#define HK_MOD_ALTGR    (6)

#define HK_MASK_SHIFT   (1 << HK_MOD_SHIFT)
#define HK_MASK_CONTROL (1 << HK_MOD_CONTROL)
#define HK_MASK_META    (1 << HK_MOD_META)
#define HK_MASK_ALT     (1 << HK_MOD_ALT)
#define HK_MASK_SUPER   (1 << HK_MOD_SUPER)
#define HK_MASK_HYPER   (1 << HK_MOD_HYPER)
#define HK_MASK_ALTGR   (1 << HK_MOD_ALTGR)

// Some host modifiers are translated into one of four "shift levels":
//
// 0 - no modifier
// 1 - Shift
// 2 - AltGr
// 3 - Shift + AltGr

#define HK_NUM_LEVELS (4)

#define HK_LEVEL_SHIFT (1 << 0)
#define HK_LEVEL_ALTGR (1 << 1)

// Enumerations and types

// Scancodes taken from USB Hid Usage Tables, Keyboard/Keypad Page (0x07)
//
// For now we can guarantee that all scancodes fit within a uint8_t, and that
// scancode 0 is invalid.
//
// Names are for convenience of translating between different scancode schemes
// and might not have an relation to the symbols on that key.

enum {
	hk_scan_None = 0x00,
	hk_scan_a = 0x04,
	hk_scan_b = 0x05,
	hk_scan_c = 0x06,
	hk_scan_d = 0x07,
	hk_scan_e = 0x08,
	hk_scan_f = 0x09,
	hk_scan_g = 0x0a,
	hk_scan_h = 0x0b,
	hk_scan_i = 0x0c,
	hk_scan_j = 0x0d,
	hk_scan_k = 0x0e,
	hk_scan_l = 0x0f,
	hk_scan_m = 0x10,
	hk_scan_n = 0x11,
	hk_scan_o = 0x12,
	hk_scan_p = 0x13,
	hk_scan_q = 0x14,
	hk_scan_r = 0x15,
	hk_scan_s = 0x16,
	hk_scan_t = 0x17,
	hk_scan_u = 0x18,
	hk_scan_v = 0x19,
	hk_scan_w = 0x1a,
	hk_scan_x = 0x1b,
	hk_scan_y = 0x1c,
	hk_scan_z = 0x1d,
	hk_scan_1 = 0x1e,
	hk_scan_2 = 0x1f,
	hk_scan_3 = 0x20,
	hk_scan_4 = 0x21,
	hk_scan_5 = 0x22,
	hk_scan_6 = 0x23,
	hk_scan_7 = 0x24,
	hk_scan_8 = 0x25,
	hk_scan_9 = 0x26,
	hk_scan_0 = 0x27,
	hk_scan_Return = 0x28,
	hk_scan_Escape = 0x29,
	hk_scan_BackSpace = 0x2a,
	hk_scan_Tab = 0x2b,
	hk_scan_space = 0x2c,
	hk_scan_minus = 0x2d,
	hk_scan_equal = 0x2e,
	hk_scan_bracketleft = 0x2f,
	hk_scan_bracketright = 0x30,
	hk_scan_backslash = 0x31,
	hk_scan_numbersign_nonUS = 0x32,
	hk_scan_semicolon = 0x33,
	hk_scan_apostrophe = 0x34,
	hk_scan_grave = 0x35,
	hk_scan_comma = 0x36,
	hk_scan_period = 0x37,
	hk_scan_slash = 0x38,
	hk_scan_Caps_Lock = 0x39,
	hk_scan_F1 = 0x3a,
	hk_scan_F2 = 0x3b,
	hk_scan_F3 = 0x3c,
	hk_scan_F4 = 0x3d,
	hk_scan_F5 = 0x3e,
	hk_scan_F6 = 0x3f,
	hk_scan_F7 = 0x40,
	hk_scan_F8 = 0x41,
	hk_scan_F9 = 0x42,
	hk_scan_F10 = 0x43,
	hk_scan_F11 = 0x44,
	hk_scan_F12 = 0x45,
	hk_scan_Print = 0x46,
	hk_scan_Scroll_Lock = 0x47,
	hk_scan_Pause = 0x48,
	hk_scan_Insert = 0x49,
	hk_scan_Home = 0x4a,
	hk_scan_Page_Up = 0x4b,
	hk_scan_Delete = 0x4c,
	hk_scan_End = 0x4d,
	hk_scan_Page_Down = 0x4e,
	hk_scan_Right = 0x4f,
	hk_scan_Left = 0x50,
	hk_scan_Down = 0x51,
	hk_scan_Up = 0x52,
	hk_scan_Num_Lock = 0x53,
	hk_scan_KP_Divide = 0x54,
	hk_scan_KP_Multiply = 0x55,
	hk_scan_KP_Subtract = 0x56,
	hk_scan_KP_Add = 0x57,
	hk_scan_KP_Enter = 0x58,
	hk_scan_KP_1 = 0x59,
	hk_scan_KP_2 = 0x5a,
	hk_scan_KP_3 = 0x5b,
	hk_scan_KP_4 = 0x5c,
	hk_scan_KP_5 = 0x5d,
	hk_scan_KP_6 = 0x5e,
	hk_scan_KP_7 = 0x5f,
	hk_scan_KP_8 = 0x60,
	hk_scan_KP_9 = 0x61,
	hk_scan_KP_0 = 0x62,
	hk_scan_KP_Decimal = 0x63,
	hk_scan_backslash_nonUS = 0x64,
	hk_scan_Application = 0x65,
	hk_scan_Power = 0x66,
	hk_scan_KP_Equal = 0x67,  // but see 0x86 below ???
	hk_scan_F13 = 0x68,
	hk_scan_F14 = 0x69,
	hk_scan_F15 = 0x6a,
	hk_scan_F16 = 0x6b,
	hk_scan_F17 = 0x6c,
	hk_scan_F18 = 0x6d,
	hk_scan_F19 = 0x6e,
	hk_scan_F20 = 0x6f,
	hk_scan_F21 = 0x70,
	hk_scan_F22 = 0x71,
	hk_scan_F23 = 0x72,
	hk_scan_F24 = 0x73,
	hk_scan_Execute = 0x74,
	hk_scan_Help = 0x75,
	hk_scan_Menu = 0x76,
	hk_scan_Select = 0x77,
	hk_scan_Cancel = 0x78,
	hk_scan_Redo = 0x79,
	hk_scan_Undo = 0x7a,
	hk_scan_Cut = 0x7b,
	hk_scan_Copy = 0x7c,
	hk_scan_Paste = 0x7d,
	hk_scan_Find = 0x7e,
	hk_scan_Mute = 0x7f,
	hk_scan_Volume_Up = 0x80,
	hk_scan_Volume_Down = 0x81,
	hk_scan_KP_Separator = 0x85,
	// 0x86 defined as "Keypad Equal Sign", where 0x67 is "Keypad =" - ???
	hk_scan_International1 = 0x87,
	hk_scan_International2 = 0x88,
	hk_scan_International3 = 0x89,
	hk_scan_International4 = 0x8a,
	hk_scan_International5 = 0x8b,
	hk_scan_International6 = 0x8c,
	hk_scan_International7 = 0x8d,
	hk_scan_International8 = 0x8e,
	hk_scan_International9 = 0x8f,
	hk_scan_Lang1 = 0x90,
	hk_scan_Lang2 = 0x91,
	hk_scan_Lang3 = 0x92,
	hk_scan_Lang4 = 0x93,
	hk_scan_Lang5 = 0x94,
	hk_scan_Lang6 = 0x95,
	hk_scan_Lang7 = 0x96,
	hk_scan_Lang8 = 0x97,
	hk_scan_Lang9 = 0x98,
	hk_scan_Clear = 0x9c,
	hk_scan_Prior = 0x9d,
	hk_scan_Control_L = 0xe0,
	hk_scan_Shift_L = 0xe1,
	hk_scan_Alt_L = 0xe2,
	hk_scan_Super_L = 0xe3,
	hk_scan_Control_R = 0xe4,
	hk_scan_Shift_R = 0xe5,
	hk_scan_Alt_R = 0xe6,
	hk_scan_Super_R = 0xe7,
};

// Per-scancode tables should always cover this many scancodes

#define HK_NUM_SCANCODES (256)

// If an OS-specific intialisation was able to generate a mapping table to
// HK scancodes, they will be here:

extern int hk_num_os_scancodes;  // size of following table
extern uint8_t *os_scancode_to_hk_scancode;

// Key symbols.
//
// ASCII values where they're printable, else from this enum which is mostly
// Unicode values with X11 keysym names.  Non-Unicode values taken from X11.

enum {
	hk_sym_None = 0x0000,

	// XRoar special functions

	hk_sym_Pause_Output = 0x0013,
	hk_sym_Erase_Line = 0x0015,

	// TTY function keys

	hk_sym_BackSpace = 0xff08,
	hk_sym_Tab = 0xff09,
	hk_sym_Linefeed = 0xff0a,
	hk_sym_Clear = 0xff0b,
	hk_sym_Return = 0xff0d,
	hk_sym_Pause = 0xff13,
	hk_sym_Scroll_Lock = 0xff14,
	hk_sym_Sys_Req = 0xff15,
	hk_sym_Escape = 0xff1b,
	hk_sym_Delete = 0xffff,

	// International & multi-key character composition

	hk_sym_Multi_key = 0xff20,
	hk_sym_Codeinput = 0xff37,              // reused as Japanese Kanji_Bangou
	hk_sym_SingleCandidate = 0xff3c,
	hk_sym_MultipleCandidate = 0xff3d,      // reused as Japanese Zen_Koho
	hk_sym_PreviousCandidate = 0xff3e,      // reused as Japanese Mae_Koho

	// Japanese keyboard support

	hk_sym_Kanji = 0xff21,
	hk_sym_Muhenkan = 0xff22,
	hk_sym_Henkan = 0xff23,
	hk_sym_Romaji = 0xff24,
	hk_sym_Hiragana = 0xff25,
	hk_sym_Katakana = 0xff26,
	hk_sym_Hiragana_Katakana = 0xff27,
	hk_sym_Zenkaku = 0xff28,
	hk_sym_Hankaku = 0xff29,
	hk_sym_Zenkaku_Hankaku = 0xff2a,
	hk_sym_Touroku = 0xff2b,
	hk_sym_Massyo = 0xff2c,
	hk_sym_Kana_Lock = 0xff2d,
	hk_sym_Kana_Shift = 0xff2e,
	hk_sym_Eisu_Shift = 0xff2f,
	hk_sym_Eisu_toggle = 0xff30,

	// Cursor control & motion

	hk_sym_Home = 0xff50,
	hk_sym_Left = 0xff51,
	hk_sym_Up = 0xff52,
	hk_sym_Right = 0xff53,
	hk_sym_Down = 0xff54,
	hk_sym_Page_Up = 0xff55,
	hk_sym_Prior = 0xff55,
	hk_sym_Page_Down = 0xff56,
	hk_sym_Next = 0xff56,
	hk_sym_End = 0xff57,
	hk_sym_Begin = 0xff58,

	// Misc functions

	hk_sym_Select = 0xff60,
	hk_sym_Print = 0xff61,
	hk_sym_Execute = 0xff62,
	hk_sym_Insert = 0xff63,
	hk_sym_Undo = 0xff65,
	hk_sym_Redo = 0xff66,
	hk_sym_Menu = 0xff67,
	hk_sym_Find = 0xff68,
	hk_sym_Cancel = 0xff69,
	hk_sym_Help = 0xff6a,
	hk_sym_Break = 0xff6b,
	hk_sym_Volume_Down = 0xff77,  // SunXK_AudioLowerVolume & 0xffff
	hk_sym_Mute = 0xff78,         // SunXK_AudioMute & 0xffff
	hk_sym_Volume_Up = 0xff79,    // SunXK_AudioRaiseVolume & 0xffff
	hk_sym_Mode_switch = 0xff7e,
	hk_sym_script_switch = 0xff7e,
	hk_sym_Num_Lock = 0xff7f,

	// Keypad functions

	hk_sym_KP_Space = 0xff80,
	hk_sym_KP_Tab = 0xff89,
	hk_sym_KP_Enter = 0xff8d,
	hk_sym_KP_F1 = 0xff91,
	hk_sym_KP_F2 = 0xff92,
	hk_sym_KP_F3 = 0xff93,
	hk_sym_KP_F4 = 0xff94,
	hk_sym_KP_Home = 0xff95,
	hk_sym_KP_Left = 0xff96,
	hk_sym_KP_Up = 0xff97,
	hk_sym_KP_Right = 0xff98,
	hk_sym_KP_Down = 0xff99,
	hk_sym_KP_Page_Up = 0xff9a,
	hk_sym_KP_Prior = 0xff9a,
	hk_sym_KP_Page_Down = 0xff9b,
	hk_sym_KP_Next = 0xff9b,
	hk_sym_KP_End = 0xff9c,
	hk_sym_KP_Begin = 0xff9d,
	hk_sym_KP_Insert = 0xff9e,
	hk_sym_KP_Delete = 0xff9f,
	hk_sym_KP_Equal = 0xffbd,
	hk_sym_KP_Multiply = 0xffaa,
	hk_sym_KP_Add = 0xffab,
	hk_sym_KP_Separator = 0xffac,
	hk_sym_KP_Subtract = 0xffad,
	hk_sym_KP_Decimal = 0xffae,
	hk_sym_KP_Divide = 0xffaf,

	hk_sym_KP_0 = 0xffb0,
	hk_sym_KP_1 = 0xffb1,
	hk_sym_KP_2 = 0xffb2,
	hk_sym_KP_3 = 0xffb3,
	hk_sym_KP_4 = 0xffb4,
	hk_sym_KP_5 = 0xffb5,
	hk_sym_KP_6 = 0xffb6,
	hk_sym_KP_7 = 0xffb7,
	hk_sym_KP_8 = 0xffb8,
	hk_sym_KP_9 = 0xffb9,

	// Auxiliary functions

	hk_sym_F1 = 0xffbe,
	hk_sym_F2 = 0xffbf,
	hk_sym_F3 = 0xffc0,
	hk_sym_F4 = 0xffc1,
	hk_sym_F5 = 0xffc2,
	hk_sym_F6 = 0xffc3,
	hk_sym_F7 = 0xffc4,
	hk_sym_F8 = 0xffc5,
	hk_sym_F9 = 0xffc6,
	hk_sym_F10 = 0xffc7,
	hk_sym_F11 = 0xffc8,
	hk_sym_F12 = 0xffc9,
	hk_sym_F13 = 0xffca,
	hk_sym_F14 = 0xffcb,
	hk_sym_F15 = 0xffcc,
	hk_sym_F16 = 0xffcd,
	hk_sym_F17 = 0xffce,
	hk_sym_F18 = 0xffcf,
	hk_sym_F19 = 0xffd0,
	hk_sym_F20 = 0xffd1,
	hk_sym_F21 = 0xffd2,
	hk_sym_F22 = 0xffd3,
	hk_sym_F23 = 0xffd4,
	hk_sym_F24 = 0xffd5,

	// Modifiers

	hk_sym_Shift_L = 0xffe1,
	hk_sym_Shift_R = 0xffe2,
	hk_sym_Control_L = 0xffe3,
	hk_sym_Control_R = 0xffe4,
	hk_sym_Caps_Lock = 0xffe5,
	hk_sym_Shift_Lock = 0xffe6,

	hk_sym_Meta_L = 0xffe7,
	hk_sym_Meta_R = 0xffe8,
	hk_sym_Alt_L = 0xffe9,
	hk_sym_Alt_R = 0xffea,
	hk_sym_Super_L = 0xffeb,
	hk_sym_Super_R = 0xffec,
	hk_sym_Hyper_L = 0xffed,
	hk_sym_Hyper_R = 0xffee,

	// Some ISO keys

	hk_sym_ISO_Lock = 0xfe01,
	hk_sym_ISO_Level2_Latch = 0xfe02,
	hk_sym_ISO_Level3_Shift = 0xfe03,
	hk_sym_ISO_Level3_Latch = 0xfe04,
	hk_sym_ISO_Level3_Lock = 0xfe05,
	hk_sym_ISO_Level5_Shift = 0xfe11,
	hk_sym_ISO_Level5_Latch = 0xfe12,
	hk_sym_ISO_Level5_Lock = 0xfe13,
	hk_sym_ISO_Group_Shift = 0xff7e,
	hk_sym_ISO_Group_Latch = 0xfe06,
	hk_sym_ISO_Group_Lock = 0xfe07,
	hk_sym_ISO_Next_Group = 0xfe08,
	hk_sym_ISO_Next_Group_Lock = 0xfe09,
	hk_sym_ISO_Prev_Group = 0xfe0a,
	hk_sym_ISO_Prev_Group_Lock = 0xfe0b,
	hk_sym_ISO_First_Group = 0xfe0c,
	hk_sym_ISO_First_Group_Lock = 0xfe0d,
	hk_sym_ISO_Last_Group = 0xfe0e,
	hk_sym_ISO_Last_Group_Lock = 0xfe0f,

	hk_sym_dead_grave = 0xfe50,
	hk_sym_dead_acute = 0xfe51,
	hk_sym_dead_circumflex = 0xfe52,
	hk_sym_dead_tilde = 0xfe53,
	hk_sym_dead_perispomeni = 0xfe53,
	hk_sym_dead_macron = 0xfe54,
	hk_sym_dead_breve = 0xfe55,
	hk_sym_dead_abovedot = 0xfe56,
	hk_sym_dead_diaeresis = 0xfe57,
	hk_sym_dead_abovering = 0xfe58,
	hk_sym_dead_doubleacute = 0xfe59,
	hk_sym_dead_caron = 0xfe5a,
	hk_sym_dead_cedilla = 0xfe5b,
	hk_sym_dead_ogonek = 0xfe5c,
	hk_sym_dead_iota = 0xfe5d,
	hk_sym_dead_voiced_sound = 0xfe5e,
	hk_sym_dead_semivoiced_sound = 0xfe5f,
	hk_sym_dead_belowdot = 0xfe60,
	hk_sym_dead_hook = 0xfe61,
	hk_sym_dead_horn = 0xfe62,
	hk_sym_dead_stroke = 0xfe63,
	hk_sym_dead_abovecomma = 0xfe64,
	hk_sym_dead_psili = 0xfe64,
	hk_sym_dead_abovereversedcomma = 0xfe65,
	hk_sym_dead_dasia = 0xfe65,
	hk_sym_dead_doublegrave = 0xfe66,
	hk_sym_dead_belowring = 0xfe67,
	hk_sym_dead_belowmacron = 0xfe68,
	hk_sym_dead_belowcircumflex = 0xfe69,
	hk_sym_dead_belowtilde = 0xfe6a,
	hk_sym_dead_belowbreve = 0xfe6b,
	hk_sym_dead_belowdiaeresis = 0xfe6c,
	hk_sym_dead_invertedbreve = 0xfe6d,
	hk_sym_dead_belowcomma = 0xfe6e,
	hk_sym_dead_currency = 0xfe6f,
	hk_sym_dead_greek = 0xfe8c,

	// Latin 1

	hk_sym_space = 0x0020,
	hk_sym_exclam = 0x0021,
	hk_sym_quotedbl = 0x0022,
	hk_sym_numbersign = 0x0023,
	hk_sym_dollar = 0x0024,
	hk_sym_percent = 0x0025,
	hk_sym_ampersand = 0x0026,
	hk_sym_apostrophe = 0x0027,
	hk_sym_parenleft = 0x0028,
	hk_sym_parenright = 0x0029,
	hk_sym_asterisk = 0x002a,
	hk_sym_plus = 0x002b,
	hk_sym_comma = 0x002c,
	hk_sym_minus = 0x002d,
	hk_sym_period = 0x002e,
	hk_sym_slash = 0x002f,
	hk_sym_0 = 0x0030,
	hk_sym_1 = 0x0031,
	hk_sym_2 = 0x0032,
	hk_sym_3 = 0x0033,
	hk_sym_4 = 0x0034,
	hk_sym_5 = 0x0035,
	hk_sym_6 = 0x0036,
	hk_sym_7 = 0x0037,
	hk_sym_8 = 0x0038,
	hk_sym_9 = 0x0039,
	hk_sym_colon = 0x003a,
	hk_sym_semicolon = 0x003b,
	hk_sym_less = 0x003c,
	hk_sym_equal = 0x003d,
	hk_sym_greater = 0x003e,
	hk_sym_question = 0x003f,
	hk_sym_at = 0x0040,
	hk_sym_A = 0x0041,
	hk_sym_B = 0x0042,
	hk_sym_C = 0x0043,
	hk_sym_D = 0x0044,
	hk_sym_E = 0x0045,
	hk_sym_F = 0x0046,
	hk_sym_G = 0x0047,
	hk_sym_H = 0x0048,
	hk_sym_I = 0x0049,
	hk_sym_J = 0x004a,
	hk_sym_K = 0x004b,
	hk_sym_L = 0x004c,
	hk_sym_M = 0x004d,
	hk_sym_N = 0x004e,
	hk_sym_O = 0x004f,
	hk_sym_P = 0x0050,
	hk_sym_Q = 0x0051,
	hk_sym_R = 0x0052,
	hk_sym_S = 0x0053,
	hk_sym_T = 0x0054,
	hk_sym_U = 0x0055,
	hk_sym_V = 0x0056,
	hk_sym_W = 0x0057,
	hk_sym_X = 0x0058,
	hk_sym_Y = 0x0059,
	hk_sym_Z = 0x005a,
	hk_sym_bracketleft = 0x005b,
	hk_sym_backslash = 0x005c,
	hk_sym_bracketright = 0x005d,
	hk_sym_asciicircum = 0x005e,
	hk_sym_underscore = 0x005f,
	hk_sym_grave = 0x0060,
	hk_sym_a = 0x0061,
	hk_sym_b = 0x0062,
	hk_sym_c = 0x0063,
	hk_sym_d = 0x0064,
	hk_sym_e = 0x0065,
	hk_sym_f = 0x0066,
	hk_sym_g = 0x0067,
	hk_sym_h = 0x0068,
	hk_sym_i = 0x0069,
	hk_sym_j = 0x006a,
	hk_sym_k = 0x006b,
	hk_sym_l = 0x006c,
	hk_sym_m = 0x006d,
	hk_sym_n = 0x006e,
	hk_sym_o = 0x006f,
	hk_sym_p = 0x0070,
	hk_sym_q = 0x0071,
	hk_sym_r = 0x0072,
	hk_sym_s = 0x0073,
	hk_sym_t = 0x0074,
	hk_sym_u = 0x0075,
	hk_sym_v = 0x0076,
	hk_sym_w = 0x0077,
	hk_sym_x = 0x0078,
	hk_sym_y = 0x0079,
	hk_sym_z = 0x007a,
	hk_sym_braceleft = 0x007b,
	hk_sym_bar = 0x007c,
	hk_sym_braceright = 0x007d,
	hk_sym_asciitilde = 0x007e,

	hk_sym_nobreakspace = 0x00a0,
	hk_sym_exclamdown = 0x00a1,
	hk_sym_cent = 0x00a2,
	hk_sym_sterling = 0x00a3,
	hk_sym_currency = 0x00a4,
	hk_sym_yen = 0x00a5,
	hk_sym_brokenbar = 0x00a6,
	hk_sym_section = 0x00a7,
	hk_sym_diaeresis = 0x00a8,
	hk_sym_copyright = 0x00a9,
	hk_sym_ordfeminine = 0x00aa,
	hk_sym_guillemetleft = 0x00ab,
	hk_sym_notsign = 0x00ac,
	hk_sym_hyphen = 0x00ad,
	hk_sym_registered = 0x00ae,
	hk_sym_macron = 0x00af,
	hk_sym_degree = 0x00b0,
	hk_sym_plusminus = 0x00b1,
	hk_sym_twosuperior = 0x00b2,
	hk_sym_threesuperior = 0x00b3,
	hk_sym_acute = 0x00b4,
	hk_sym_mu = 0x00b5,
	hk_sym_paragraph = 0x00b6,
	hk_sym_periodcentered = 0x00b7,
	hk_sym_cedilla = 0x00b8,
	hk_sym_onesuperior = 0x00b9,
	hk_sym_masculine = 0x00ba,
	hk_sym_guillemetright = 0x00bb,
	hk_sym_onequarter = 0x00bc,
	hk_sym_onehalf = 0x00bd,
	hk_sym_threequarters = 0x00be,
	hk_sym_questiondown = 0x00bf,
	hk_sym_Agrave = 0x00c0,
	hk_sym_Aacute = 0x00c1,
	hk_sym_Acircumflex = 0x00c2,
	hk_sym_Atilde = 0x00c3,
	hk_sym_Adiaeresis = 0x00c4,
	hk_sym_Aring = 0x00c5,
	hk_sym_AE = 0x00c6,
	hk_sym_Ccedilla = 0x00c7,
	hk_sym_Egrave = 0x00c8,
	hk_sym_Eacute = 0x00c9,
	hk_sym_Ecircumflex = 0x00ca,
	hk_sym_Ediaeresis = 0x00cb,
	hk_sym_Igrave = 0x00cc,
	hk_sym_Iacute = 0x00cd,
	hk_sym_Icircumflex = 0x00ce,
	hk_sym_Idiaeresis = 0x00cf,
	hk_sym_ETH = 0x00d0,
	hk_sym_Eth = 0x00d0,
	hk_sym_Ntilde = 0x00d1,
	hk_sym_Ograve = 0x00d2,
	hk_sym_Oacute = 0x00d3,
	hk_sym_Ocircumflex = 0x00d4,
	hk_sym_Otilde = 0x00d5,
	hk_sym_Odiaeresis = 0x00d6,
	hk_sym_multiply = 0x00d7,
	hk_sym_Oslash = 0x00d8,
	hk_sym_Ooblique = 0x00d8,
	hk_sym_Ugrave = 0x00d9,
	hk_sym_Uacute = 0x00da,
	hk_sym_Ucircumflex = 0x00db,
	hk_sym_Udiaeresis = 0x00dc,
	hk_sym_Yacute = 0x00dd,
	hk_sym_THORN = 0x00de,
	hk_sym_Thorn = 0x00de,
	hk_sym_ssharp = 0x00df,
	hk_sym_agrave = 0x00e0,
	hk_sym_aacute = 0x00e1,
	hk_sym_acircumflex = 0x00e2,
	hk_sym_atilde = 0x00e3,
	hk_sym_adiaeresis = 0x00e4,
	hk_sym_aring = 0x00e5,
	hk_sym_ae = 0x00e6,
	hk_sym_ccedilla = 0x00e7,
	hk_sym_egrave = 0x00e8,
	hk_sym_eacute = 0x00e9,
	hk_sym_ecircumflex = 0x00ea,
	hk_sym_ediaeresis = 0x00eb,
	hk_sym_igrave = 0x00ec,
	hk_sym_iacute = 0x00ed,
	hk_sym_icircumflex = 0x00ee,
	hk_sym_idiaeresis = 0x00ef,
	hk_sym_eth = 0x00f0,
	hk_sym_ntilde = 0x00f1,
	hk_sym_ograve = 0x00f2,
	hk_sym_oacute = 0x00f3,
	hk_sym_ocircumflex = 0x00f4,
	hk_sym_otilde = 0x00f5,
	hk_sym_odiaeresis = 0x00f6,
	hk_sym_division = 0x00f7,
	hk_sym_oslash = 0x00f8,
	hk_sym_ooblique = 0x00f8,
	hk_sym_ugrave = 0x00f9,
	hk_sym_uacute = 0x00fa,
	hk_sym_ucircumflex = 0x00fb,
	hk_sym_udiaeresis = 0x00fc,
	hk_sym_yacute = 0x00fd,
	hk_sym_thorn = 0x00fe,
	hk_sym_ydiaeresis = 0x00ff,

	// Latin 2

	hk_sym_Aogonek = 0x0104,
	hk_sym_breve = 0x02d8,
	hk_sym_Lstroke = 0x0141,
	hk_sym_Lcaron = 0x013d,
	hk_sym_Sacute = 0x015a,
	hk_sym_Scaron = 0x0160,
	hk_sym_Scedilla = 0x015e,
	hk_sym_Tcaron = 0x0164,
	hk_sym_Zacute = 0x0179,
	hk_sym_Zcaron = 0x017d,
	hk_sym_Zabovedot = 0x017b,
	hk_sym_aogonek = 0x0105,
	hk_sym_ogonek = 0x02db,
	hk_sym_lstroke = 0x0142,
	hk_sym_lcaron = 0x013e,
	hk_sym_sacute = 0x015b,
	hk_sym_caron = 0x02c7,
	hk_sym_scaron = 0x0161,
	hk_sym_scedilla = 0x015f,
	hk_sym_tcaron = 0x0165,
	hk_sym_zacute = 0x017a,
	hk_sym_doubleacute = 0x02dd,
	hk_sym_zcaron = 0x017e,
	hk_sym_zabovedot = 0x017c,
	hk_sym_Racute = 0x0154,
	hk_sym_Abreve = 0x0102,
	hk_sym_Lacute = 0x0139,
	hk_sym_Cacute = 0x0106,
	hk_sym_Ccaron = 0x010c,
	hk_sym_Eogonek = 0x0118,
	hk_sym_Ecaron = 0x011a,
	hk_sym_Dcaron = 0x010e,
	hk_sym_Dstroke = 0x0110,
	hk_sym_Nacute = 0x0143,
	hk_sym_Ncaron = 0x0147,
	hk_sym_Odoubleacute = 0x0150,
	hk_sym_Rcaron = 0x0158,
	hk_sym_Uring = 0x016e,
	hk_sym_Udoubleacute = 0x0170,
	hk_sym_Tcedilla = 0x0162,
	hk_sym_racute = 0x0155,
	hk_sym_abreve = 0x0103,
	hk_sym_lacute = 0x013a,
	hk_sym_cacute = 0x0107,
	hk_sym_ccaron = 0x010d,
	hk_sym_eogonek = 0x0119,
	hk_sym_ecaron = 0x011b,
	hk_sym_dcaron = 0x010f,
	hk_sym_dstroke = 0x0111,
	hk_sym_nacute = 0x0144,
	hk_sym_ncaron = 0x0148,
	hk_sym_odoubleacute = 0x0151,
	hk_sym_rcaron = 0x0159,
	hk_sym_uring = 0x016f,
	hk_sym_udoubleacute = 0x0171,
	hk_sym_tcedilla = 0x0163,
	hk_sym_abovedot = 0x02d9,

	// Latin 3

	hk_sym_Hstroke = 0x0126,
	hk_sym_Hcircumflex = 0x0124,
	hk_sym_Iabovedot = 0x0130,
	hk_sym_Gbreve = 0x011e,
	hk_sym_Jcircumflex = 0x0134,
	hk_sym_hstroke = 0x0127,
	hk_sym_hcircumflex = 0x0125,
	hk_sym_idotless = 0x0131,
	hk_sym_gbreve = 0x011f,
	hk_sym_jcircumflex = 0x0135,
	hk_sym_Cabovedot = 0x010a,
	hk_sym_Ccircumflex = 0x0108,
	hk_sym_Gabovedot = 0x0120,
	hk_sym_Gcircumflex = 0x011c,
	hk_sym_Ubreve = 0x016c,
	hk_sym_Scircumflex = 0x015c,
	hk_sym_cabovedot = 0x010b,
	hk_sym_ccircumflex = 0x0109,
	hk_sym_gabovedot = 0x0121,
	hk_sym_gcircumflex = 0x011d,
	hk_sym_ubreve = 0x016d,
	hk_sym_scircumflex = 0x015d,

	// Latin 4

	hk_sym_kra = 0x0138,
	hk_sym_Rcedilla = 0x0156,
	hk_sym_Itilde = 0x0128,
	hk_sym_Lcedilla = 0x013b,
	hk_sym_Emacron = 0x0112,
	hk_sym_Gcedilla = 0x0122,
	hk_sym_Tslash = 0x0166,
	hk_sym_rcedilla = 0x0157,
	hk_sym_itilde = 0x0129,
	hk_sym_lcedilla = 0x013c,
	hk_sym_emacron = 0x0113,
	hk_sym_gcedilla = 0x0123,
	hk_sym_tslash = 0x0167,
	hk_sym_ENG = 0x014a,
	hk_sym_eng = 0x014b,
	hk_sym_Amacron = 0x0100,
	hk_sym_Iogonek = 0x012e,
	hk_sym_Eabovedot = 0x0116,
	hk_sym_Imacron = 0x012a,
	hk_sym_Ncedilla = 0x0145,
	hk_sym_Omacron = 0x014c,
	hk_sym_Kcedilla = 0x0136,
	hk_sym_Uogonek = 0x0172,
	hk_sym_Utilde = 0x0168,
	hk_sym_Umacron = 0x016a,
	hk_sym_amacron = 0x0101,
	hk_sym_iogonek = 0x012f,
	hk_sym_eabovedot = 0x0117,
	hk_sym_imacron = 0x012b,
	hk_sym_ncedilla = 0x0146,
	hk_sym_omacron = 0x014d,
	hk_sym_kcedilla = 0x0137,
	hk_sym_uogonek = 0x0173,
	hk_sym_utilde = 0x0169,
	hk_sym_umacron = 0x016b,

	// Latin 8

	hk_sym_Wcircumflex = 0x0174,
	hk_sym_wcircumflex = 0x0175,
	hk_sym_Ycircumflex = 0x0176,
	hk_sym_ycircumflex = 0x0177,

	hk_sym_Babovedot = 0x1e02,
	hk_sym_babovedot = 0x1e03,
	hk_sym_Dabovedot = 0x1e0a,
	hk_sym_dabovedot = 0x1e0b,
	hk_sym_Fabovedot = 0x1e1e,
	hk_sym_fabovedot = 0x1e1f,
	hk_sym_Mabovedot = 0x1e40,
	hk_sym_mabovedot = 0x1e41,
	hk_sym_Pabovedot = 0x1e56,
	hk_sym_pabovedot = 0x1e57,
	hk_sym_Sabovedot = 0x1e60,
	hk_sym_sabovedot = 0x1e61,
	hk_sym_Tabovedot = 0x1e6a,
	hk_sym_tabovedot = 0x1e6b,
	hk_sym_Wgrave = 0x1e80,
	hk_sym_wgrave = 0x1e81,
	hk_sym_Wacute = 0x1e82,
	hk_sym_wacute = 0x1e83,
	hk_sym_Wdiaeresis = 0x1e84,
	hk_sym_wdiaeresis = 0x1e85,
	hk_sym_Ygrave = 0x1ef2,
	hk_sym_ygrave = 0x1ef3,

	// Latin 9

	hk_sym_OE = 0x0152,
	hk_sym_oe = 0x0153,
	hk_sym_Ydiaeresis = 0x0178,

	// Greek

	hk_sym_Greek_ALPHAaccent = 0x0386,
	hk_sym_Greek_EPSILONaccent = 0x0388,
	hk_sym_Greek_ETAaccent = 0x0389,
	hk_sym_Greek_IOTAaccent = 0x038a,
	hk_sym_Greek_IOTAdieresis = 0x03aa,
	hk_sym_Greek_OMICRONaccent = 0x038c,
	hk_sym_Greek_UPSILONaccent = 0x038e,
	hk_sym_Greek_UPSILONdieresis = 0x03ab,
	hk_sym_Greek_OMEGAaccent = 0x038f,
	hk_sym_Greek_accentdieresis = 0x0385,
	hk_sym_Greek_horizbar = 0x2015,
	hk_sym_Greek_alphaaccent = 0x03ac,
	hk_sym_Greek_epsilonaccent = 0x03ad,
	hk_sym_Greek_etaaccent = 0x03ae,
	hk_sym_Greek_iotaaccent = 0x03af,
	hk_sym_Greek_iotadieresis = 0x03ca,
	hk_sym_Greek_iotaaccentdieresis = 0x0390,
	hk_sym_Greek_omicronaccent = 0x03cc,
	hk_sym_Greek_upsilonaccent = 0x03cd,
	hk_sym_Greek_upsilondieresis = 0x03cb,
	hk_sym_Greek_upsilonaccentdieresis = 0x03b0,
	hk_sym_Greek_omegaaccent = 0x03ce,
	hk_sym_Greek_ALPHA = 0x0391,
	hk_sym_Greek_BETA = 0x0392,
	hk_sym_Greek_GAMMA = 0x0393,
	hk_sym_Greek_DELTA = 0x0394,
	hk_sym_Greek_EPSILON = 0x0395,
	hk_sym_Greek_ZETA = 0x0396,
	hk_sym_Greek_ETA = 0x0397,
	hk_sym_Greek_THETA = 0x0398,
	hk_sym_Greek_IOTA = 0x0399,
	hk_sym_Greek_KAPPA = 0x039a,
	hk_sym_Greek_LAMDA = 0x039b,
	hk_sym_Greek_MU = 0x039c,
	hk_sym_Greek_NU = 0x039d,
	hk_sym_Greek_XI = 0x039e,
	hk_sym_Greek_OMICRON = 0x039f,
	hk_sym_Greek_PI = 0x03a0,
	hk_sym_Greek_RHO = 0x03a1,
	hk_sym_Greek_SIGMA = 0x03a3,
	hk_sym_Greek_TAU = 0x03a4,
	hk_sym_Greek_UPSILON = 0x03a5,
	hk_sym_Greek_PHI = 0x03a6,
	hk_sym_Greek_CHI = 0x03a7,
	hk_sym_Greek_PSI = 0x03a8,
	hk_sym_Greek_OMEGA = 0x03a9,
	hk_sym_Greek_alpha = 0x03b1,
	hk_sym_Greek_beta = 0x03b2,
	hk_sym_Greek_gamma = 0x03b3,
	hk_sym_Greek_delta = 0x03b4,
	hk_sym_Greek_epsilon = 0x03b5,
	hk_sym_Greek_zeta = 0x03b6,
	hk_sym_Greek_eta = 0x03b7,
	hk_sym_Greek_theta = 0x03b8,
	hk_sym_Greek_iota = 0x03b9,
	hk_sym_Greek_kappa = 0x03ba,
	hk_sym_Greek_lamda = 0x03bb,
	hk_sym_Greek_mu = 0x03bc,
	hk_sym_Greek_nu = 0x03bd,
	hk_sym_Greek_xi = 0x03be,
	hk_sym_Greek_omicron = 0x03bf,
	hk_sym_Greek_pi = 0x03c0,
	hk_sym_Greek_rho = 0x03c1,
	hk_sym_Greek_sigma = 0x03c3,
	hk_sym_Greek_finalsmallsigma = 0x03c2,
	hk_sym_Greek_tau = 0x03c4,
	hk_sym_Greek_upsilon = 0x03c5,
	hk_sym_Greek_phi = 0x03c6,
	hk_sym_Greek_chi = 0x03c7,
	hk_sym_Greek_psi = 0x03c8,
	hk_sym_Greek_omega = 0x03c9,

	// Technical

	hk_sym_leftradical = 0x23b7,
	hk_sym_topintegral = 0x2320,
	hk_sym_botintegral = 0x2321,
	hk_sym_topleftsqbracket = 0x23a1,
	hk_sym_botleftsqbracket = 0x23a3,
	hk_sym_toprightsqbracket = 0x23a4,
	hk_sym_botrightsqbracket = 0x23a6,
	hk_sym_topleftparens = 0x239b,
	hk_sym_botleftparens = 0x239d,
	hk_sym_toprightparens = 0x239e,
	hk_sym_botrightparens = 0x23a0,
	hk_sym_leftmiddlecurlybrace = 0x23a8,
	hk_sym_rightmiddlecurlybrace = 0x23ac,
	hk_sym_lessthanequal = 0x2264,
	hk_sym_notequal = 0x2260,
	hk_sym_greaterthanequal = 0x2265,
	hk_sym_integral = 0x222b,
	hk_sym_therefore = 0x2234,
	hk_sym_variation = 0x221d,
	hk_sym_infinity = 0x221e,
	hk_sym_nabla = 0x2207,
	hk_sym_approximate = 0x223c,
	hk_sym_similarequal = 0x2243,
	hk_sym_ifonlyif = 0x21d4,
	hk_sym_implies = 0x21d2,
	hk_sym_identical = 0x2261,
	hk_sym_radical = 0x221a,
	hk_sym_includedin = 0x2282,
	hk_sym_includes = 0x2283,
	hk_sym_intersection = 0x2229,
	hk_sym_union = 0x222a,
	hk_sym_logicaland = 0x2227,
	hk_sym_logicalor = 0x2228,
	hk_sym_partialderivative = 0x2202,
	hk_sym_function = 0x0192,
	hk_sym_leftarrow = 0x2190,
	hk_sym_uparrow = 0x2191,
	hk_sym_rightarrow = 0x2192,
	hk_sym_downarrow = 0x2193,

	// Publishing

	hk_sym_emspace = 0x2003,
	hk_sym_enspace = 0x2002,
	hk_sym_em3space = 0x2004,
	hk_sym_em4space = 0x2005,
	hk_sym_digitspace = 0x2007,
	hk_sym_punctspace = 0x2008,
	hk_sym_thinspace = 0x2009,
	hk_sym_hairspace = 0x200a,
	hk_sym_emdash = 0x2014,
	hk_sym_endash = 0x2013,
	hk_sym_ellipsis = 0x2026,
	hk_sym_doubbaselinedot = 0x2025,
	hk_sym_onethird = 0x2153,
	hk_sym_twothirds = 0x2154,
	hk_sym_onefifth = 0x2155,
	hk_sym_twofifths = 0x2156,
	hk_sym_threefifths = 0x2157,
	hk_sym_fourfifths = 0x2158,
	hk_sym_onesixth = 0x2159,
	hk_sym_fivesixths = 0x215a,
	hk_sym_oneeighth = 0x215b,
	hk_sym_threeeighths = 0x215c,
	hk_sym_fiveeighths = 0x215d,
	hk_sym_seveneighths = 0x215e,
	hk_sym_trademark = 0x2122,
	hk_sym_leftsinglequotemark = 0x2018,
	hk_sym_rightsinglequotemark = 0x2019,
	hk_sym_leftdoublequotemark = 0x201c,
	hk_sym_rightdoublequotemark = 0x201d,
	hk_sym_permille = 0x2030,
	hk_sym_dagger = 0x2020,
	hk_sym_doubledagger = 0x2021,
	hk_sym_singlelowquotemark = 0x201a,
	hk_sym_doublelowquotemark = 0x201e,

	// Caucasus

	hk_sym_Xabovedot = 0x1e8a,
	hk_sym_Ibreve = 0x012c,
	hk_sym_Zstroke = 0x01b5,
	hk_sym_Gcaron = 0x01e6,
	hk_sym_Ocaron = 0x01d1,
	hk_sym_Obarred = 0x019f,
	hk_sym_xabovedot = 0x1e8b,
	hk_sym_ibreve = 0x012d,
	hk_sym_zstroke = 0x01b6,
	hk_sym_gcaron = 0x01e7,
	hk_sym_ocaron = 0x01d2,
	hk_sym_obarred = 0x0275,
	hk_sym_SCHWA = 0x018f,
	hk_sym_schwa = 0x0259,
	hk_sym_EZH = 0x01b7,
	hk_sym_ezh = 0x0292,

	// Currency

	hk_sym_EuroSign = 0x20ac,

	// Additional

	hk_sym_YOGH = 0x021c,
	hk_sym_yogh = 0x021d,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Keyboard layouts.
//
// Note that in each case, the symbols shown correspond to the SCANCODE
// (hk_scan_*), NOT the symbol that might be on each key.
//
// ISO key scancodes named for the symbol on UK keyboards may be used
// differently:
//
// [# ] numbersign_nonUS, may generate backslash
//
// [\ ] backslash_nonUS, may generate square or angle brackets

// US/UNIX
//
//            F1   F2   F3   F4     F5   F6   F7   F8      F9  F10  F11  F12
// ESC   1    2    3    4    5    6    7    8    9    0    -    =   [# ]  `
// TAB---  Q    W    E    R    T    Y    U    I    O    P    [    ]   DELETE
// CTRL----  A    S    D    F    G    H    J    K    L    ;    '   ---RETURN
// SHIFT----   Z    X    C    V    B    N    M    ,    .    /   -------SHIFT
// CAPS---- ALT- SUPER ----------------SPACE---------------- SUPER CTRL ALGR

// ANSI
//
// ESC        F1   F2   F3   F4     F5   F6   F7   F8      F9  F10  F11  F12
//  `    1    2    3    4    5    6    7    8    9    0    -    =   --DELETE
// TAB---  Q    W    E    R    T    Y    U    I    O    P    [    ]    \ ---
// CAPS----  A    S    D    F    G    H    J    K    L    ;    '   ---RETURN
// SHIFT----   Z    X    C    V    B    N    M    ,    .    /   -------SHIFT
// CTRL---- ALT- SUPER ----------------SPACE---------------- SUPER CTRL ALGR

// ISO
//
// ESC-       F1   F2   F3   F4     F5   F6   F7   F8      F9  F10  F11  F12
//  `    1    2    3    4    5    6    7    8    9    0    -    =   --DELETE
// TAB---  Q    W    E    R    T    Y    U    I    O    P    [    ]   RETURN
// CAPS----  A    S    D    F    G    H    J    K    L    ;    '   [# ] ----
// SHIFT [\ ]  Z    X    C    V    B    N    M    ,    .    /   -------SHIFT
// CTRL---- ALT- SUPER ----------------SPACE---------------- SUPER CTRL ALGR

// JIS
//
// ESC        F1   F2   F3   F4     F5   F6   F7   F8      F9  F10  F11  F12
// LNG5  1    2    3    4    5    6    7    8    9    0    -    =   INT3 DEL
// TAB---  Q    W    E    R    T    Y    U    I    O    P    [    ]   RETURN
// CAPS----  A    S    D    F    G    H    J    K    L    ;    :   [# ] ----
// SHIFT----   Z    X    C    V    B    N    M    ,    .    /   INT1 --SHIFT
// CTRL---- ALT- SUPER INT5- -------SPACE-------- -INT4 INT2 SUPER CTRL ALGR

enum {
	hk_layout_auto,
	hk_layout_unix,
	hk_layout_ansi,
	hk_layout_iso,
	hk_layout_jis,
};

enum {
	hk_lang_auto,
	hk_lang_be,
	hk_lang_br,
	hk_lang_de,
	hk_lang_dk,
	hk_lang_es,
	hk_lang_fi,
	hk_lang_fr,
	hk_lang_fr_CA,
	hk_lang_gb,
	hk_lang_is,
	hk_lang_it,
	hk_lang_jp,
	hk_lang_nl,
	hk_lang_no,
	hk_lang_pl,
	hk_lang_pl_QWERTZ,
	hk_lang_se,
	hk_lang_us,
	hk_lang_dvorak,
};

extern struct xconfig_enum hkbd_layout_list[];
extern struct xconfig_enum hkbd_lang_list[];

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Host keyboard state.

struct hkbd {
	// Configured values (can be auto)
	struct {
		int layout;
		int lang;
	} cfg;

	// Messenger client id
	int msgr_client_id;

	int layout;  // one of hk_layout_*

	uint8_t scancode_mod[HK_NUM_SCANCODES];
	uint16_t code_to_sym[HK_NUM_LEVELS][HK_NUM_SCANCODES];

	// Symbol translation enabled
	_Bool translate;

	// The symbol that was registered as pressed last by each scancode.
	// Lets us report release of the same symbol as was pressed, even if
	// the shift level has changed since.
	uint16_t scancode_pressed_sym[HK_NUM_SCANCODES];

	// Same, but post- a conversion to unicode (TODO: move this elsewhere?)
	unsigned scancode_pressed_unicode[HK_NUM_SCANCODES];

	// Map scancode to emulated key (TODO: move this elsewhere?)
	int8_t code_to_dkey[HK_NUM_SCANCODES];
	_Bool code_preempt[HK_NUM_SCANCODES];

	uint8_t state;  // current modifier state
};

extern struct hkbd hkbd;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void hk_init(void);
void hk_shutdown(void);

void hk_update_keymap(void);

// Call when window gets focus.  Will try and update keypress state, falling
// back to just releasing any key marked as pressed.
void hk_focus_in(void);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Metadata

// Convert to and from scancode values and name strings.  Names return from
// hk_name_from_scancode() will be capitalised as in enum hk_scan.  Input to
// hk_scancode_from_name() is case-insensitive.  Modifiers ignored in these
// calls.

const char *hk_name_from_scancode(uint8_t code);
uint8_t hk_scancode_from_name(const char *name);

// Same for symbols.

const char *hk_name_from_symbol(uint16_t sym);
uint16_t hk_symbol_from_name(const char *name);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Actions

// Key press & release by scancode.

void hk_scan_press(uint8_t code);
void hk_scan_release(uint8_t code);

#endif
