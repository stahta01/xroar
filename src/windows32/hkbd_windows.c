/** \file
 *
 *  \brief Windows keyboard handling.
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

#include <windows.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "xalloc.h"

#include "hkbd.h"

// From Windows Platform Design Notes, Keyboard Scan Code Specification
//
// https://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/scancode.doc
//
// The notes in the file are rephrased from text in that document, but in many
// cases we can ignore them.  The idea of scan codes prefixed by E0 is
// swallowed up by the driver - by the time we get the data in a WM_KEYDOWN or
// WM_KEYUP message, that is represented as bit 24 in lParam.
//
// Where the second byte of a prefixed scan code doesn't conflict with a
// regular scan code, it is included in the table.  For the rest, some
// inspection of context may be required.  e.g. Windows has probably already
// interpreted the various Pause key cases and translated that into a virtual
// key code, passed in wParam.

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Windows is described as configuring the i8042 port chip such that it
// translates "Scan Code Set 2" codes from the hardware into "Scan Code Set 1"
// before the driver even sees them.  Thus, these are ordered by their Set 1
// make codes.

// General notes:
//
// Make codes >= 0x7a are discouraged.
//
// Right Control and Right Alt are "extended keys", and so send E0 followed by
// the code for their left side counterpart.  This doesn't appear to be the
// case for Shift or Super, which have separate codes for left and right.
//
// Pretty much every other key seems to be listed as being optionally prefixed
// with E0.  Not sure how common that is.
//
// Break codes are generally the same value as the make code with top bit set.
//
// For the special keys documented as inserting extra make or break codes that
// represent modifier keys ahead of their own make code, it's not certain how
// the break code is affected if said modifier is changed before the key is
// released.

// Numbered notes from the document:

// Note 1
//
// Certain cursor control keys generate complex series of codes as they may
// share keys with the keypad.  If Num Lock is ON, make codes are preceded by
// E0 2A (make Shift_R).  If Num Lock is OFF, but any Shift keys are pressed,
// the break codes for Shift_L and Shift_R precede the make code for the key.
// Any of these make/break prefixes are reversed and sense-inverted following
// the key's break code.

// Insert       [ E0 2A | [E0 AA] [E0 B6] ] E0 52       (52 = KP_0)
// Delete       [ E0 2A | [E0 AA] [E0 B6] ] E0 53       (53 = KP_Decimal)
// Left         [ E0 2A | [E0 AA] [E0 B6] ] E0 4B       (4B = KP_4)
// Home         [ E0 2A | [E0 AA] [E0 B6] ] E0 47       (47 = KP_7)
// End          [ E0 2A | [E0 AA] [E0 B6] ] E0 4F       (4F = KP_1)
// Up           [ E0 2A | [E0 AA] [E0 B6] ] E0 48       (48 = KP_8)
// Down         [ E0 2A | [E0 AA] [E0 B6] ] E0 50       (50 = KP_2)
// Page_Up      [ E0 2A | [E0 AA] [E0 B6] ] E0 49       (49 = KP_9)
// Page_Down    [ E0 2A | [E0 AA] [E0 B6] ] E0 51       (51 = KP_3)
// Right        [ E0 2A | [E0 AA] [E0 B6] ] E0 4D       (4D = KP_6)

// Note 2 only concerns Scan Code Set 2, so irrelevant to us.

// Note 3
//
// Concerning "numeric /", similar to note 1 with Num Lock OFF.  The aliased
// key this time is '/' on the main keyboard.

// KP_Divide    [E0 AA] [E0 B6] E0 35                   (35 = slash)

// Note 4
//
// The Print Screen key behaves differently depending on which modifiers are
// active.
//
// On make, if an Alt key is held, generates 54.  If BOTH a Control key AND a
// Shift key are held, generates E0 37.  OTHERWISE, generates E0 2A E0 37 (i.e.
// Shift_R).  On break, make codes are unwound as usual.

// Print        [E0 2A] E0 37 | 54

// Note 5
//
// Pause effectively sends make followed immediately by break, but how it does
// so changes if a Control key is held:

// Pause        E1 1D 45 E1 9D C5               (1D = equal, 45 = Num_Lock)
// Pause+Ctrl   E0 46 E0 C6                     (46 = Scroll_Lock)

// As such, while Pause can be recognised as a keypress, it's impossible to
// tell if it's held down, or when it is released.  It's also the only key
// documented as using the E1 prefix, AFAICT.
//
// In reality, I've seen several behaviours:
//
// - An HHKB appears to treat Pause like any other key with separate make and
// break sequences, the OS even autorepeating the key.
//
// - A ThinkPad generates make-then-break on keypress as described.
//
// - A Dell generates make-then-break on key _release_ instead.

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Finally, the table.  This maps Windows virtual scancode (vsc) to an HK
// scancode.  The top byte of the vsc is set to 0xe0 or 0xe1 to indicate an
// extended scancode.

const struct {
	uint16_t vsc;
	uint8_t hk_scancode;
} windows_to_hk_scancode[] = {
	{ 0x0001, hk_scan_Escape },
	{ 0x0002, hk_scan_1 },
	{ 0x0003, hk_scan_2 },
	{ 0x0004, hk_scan_3 },
	{ 0x0005, hk_scan_4 },
	{ 0x0006, hk_scan_5 },
	{ 0x0007, hk_scan_6 },
	{ 0x0008, hk_scan_7 },
	{ 0x0009, hk_scan_8 },
	{ 0x000a, hk_scan_9 },
	{ 0x000b, hk_scan_0 },
	{ 0x000c, hk_scan_minus },
	{ 0x000d, hk_scan_equal },
	{ 0x000e, hk_scan_BackSpace },
	{ 0x000f, hk_scan_Tab },

	{ 0x0010, hk_scan_q },
	{ 0x0011, hk_scan_w },
	{ 0x0012, hk_scan_e },
	{ 0x0013, hk_scan_r },
	{ 0x0014, hk_scan_t },
	{ 0x0015, hk_scan_y },
	{ 0x0016, hk_scan_u },
	{ 0x0017, hk_scan_i },
	{ 0x0018, hk_scan_o },
	{ 0x0019, hk_scan_p },
	{ 0x001a, hk_scan_bracketleft },
	{ 0x001b, hk_scan_bracketright },
	{ 0x001c, hk_scan_Return },
	{ 0x001d, hk_scan_Control_L },
	{ 0x001e, hk_scan_a },
	{ 0x001f, hk_scan_s },

	{ 0x0020, hk_scan_d },
	{ 0x0021, hk_scan_f },
	{ 0x0022, hk_scan_g },
	{ 0x0023, hk_scan_h },
	{ 0x0024, hk_scan_j },
	{ 0x0025, hk_scan_k },
	{ 0x0026, hk_scan_l },
	{ 0x0027, hk_scan_semicolon },
	{ 0x0028, hk_scan_apostrophe },
	{ 0x0029, hk_scan_grave },
	{ 0x002a, hk_scan_Shift_L },
	{ 0x002b, hk_scan_backslash },
	{ 0x002c, hk_scan_z },
	{ 0x002d, hk_scan_x },
	{ 0x002e, hk_scan_c },
	{ 0x002f, hk_scan_v },

	{ 0x0030, hk_scan_b },
	{ 0x0031, hk_scan_n },
	{ 0x0032, hk_scan_m },
	{ 0x0033, hk_scan_comma },
	{ 0x0034, hk_scan_period },
	{ 0x0035, hk_scan_slash },              // see Note 3 (KP_Divide)
	{ 0x0036, hk_scan_Shift_R },
	{ 0x0037, hk_scan_Print },              // see Note 4
	{ 0x0038, hk_scan_Alt_L },
	{ 0x0039, hk_scan_space },
	{ 0x003a, hk_scan_Caps_Lock },
	{ 0x003b, hk_scan_F1 },
	{ 0x003c, hk_scan_F2 },
	{ 0x003d, hk_scan_F3 },
	{ 0x003e, hk_scan_F4 },
	{ 0x003f, hk_scan_F5 },

	{ 0x0040, hk_scan_F6 },
	{ 0x0041, hk_scan_F7 },
	{ 0x0042, hk_scan_F8 },
	{ 0x0043, hk_scan_F9 },
	{ 0x0044, hk_scan_F10 },
	{ 0x0045, hk_scan_Num_Lock },
	{ 0x0046, hk_scan_Scroll_Lock },
	{ 0x0047, hk_scan_KP_7 },               // see Note 1 (Home)
	{ 0x0048, hk_scan_KP_8 },               // see Note 1 (Up)
	{ 0x0049, hk_scan_KP_9 },               // see Note 1 (Page_Up)
	{ 0x004a, hk_scan_KP_Subtract },
	{ 0x004b, hk_scan_KP_4 },               // see Note 1 (Left)
	{ 0x004c, hk_scan_KP_5 },
	{ 0x004d, hk_scan_KP_6 },               // see Note 1 (Right)
	{ 0x004e, hk_scan_KP_Add },
	{ 0x004f, hk_scan_KP_1 },               // see Note 1 (End)

	{ 0x0050, hk_scan_KP_2 },               // see Note 1 (Down)
	{ 0x0051, hk_scan_KP_3 },               // see Note 1 (Page_Down)
	{ 0x0052, hk_scan_KP_0 },               // see Note 1 (Insert)
	{ 0x0053, hk_scan_KP_Decimal },         // see Note 1 (Delete)
	{ 0x0054, hk_scan_Print },              // see Note 4
	{ 0x0056, hk_scan_backslash_nonUS },
	{ 0x0057, hk_scan_F11 },
	{ 0x0058, hk_scan_F12 },
	{ 0x0059, hk_scan_Pause },              // see Note 5

	{ 0x0064, hk_scan_F13 },
	{ 0x0065, hk_scan_F14 },
	{ 0x0066, hk_scan_F15 },
	{ 0x0067, hk_scan_F16 },
	{ 0x0068, hk_scan_F17 },
	{ 0x0069, hk_scan_F18 },
	{ 0x006a, hk_scan_F19 },

	{ 0x0070, hk_scan_International2 },
	{ 0x0073, hk_scan_International1 },

	{ 0xe01d, hk_scan_Control_R },

	{ 0xe020, hk_scan_Mute },
	{ 0xe02e, hk_scan_Volume_Down },

	{ 0xe030, hk_scan_Volume_Up },
	{ 0xe035, hk_scan_KP_Divide },          // see Note 3 (slash)
	{ 0xe038, hk_scan_Alt_R },

	{ 0xe047, hk_scan_Home },               // see Note 1 (KP_7)
	{ 0xe048, hk_scan_Up },                 // see Note 1 (KP_8)
	{ 0xe049, hk_scan_Page_Up },            // see Note 1 (KP_9)
	{ 0xe04b, hk_scan_Left },               // see Note 1 (KP_4)
	{ 0xe04d, hk_scan_Right },              // see Note 1 (KP_6)
	{ 0xe04f, hk_scan_End },                // see Note 1 (KP_1)

	{ 0xe050, hk_scan_Down },               // see Note 1 (KP_2)
	{ 0xe051, hk_scan_Page_Down },          // see Note 1 (KP_3)
	{ 0xe052, hk_scan_Insert },             // see Note 1 (KP_0)
	{ 0xe053, hk_scan_Delete },             // see Note 1 (KP_Decimal)
	{ 0xe05b, hk_scan_Super_L },
	{ 0xe05c, hk_scan_Super_R },
	{ 0xe05d, hk_scan_Application },
};

// Similar table mapping Windows Virtual Keycode to HK sym:

const uint16_t windows_to_hk_sym[] = {
	// 0x00 - 0x0f
	hk_sym_None,
	hk_sym_None,            // 0x01 VK_LBUTTON
	hk_sym_None,            // 0x02 VK_RBUTTON
	hk_sym_None,            // 0x03 VK_CANCEL
	hk_sym_None,            // 0x04 VK_MBUTTON
	hk_sym_None,            // 0x05 VK_XBUTTON1
	hk_sym_None,            // 0x06 VK_XBUTTON2
	hk_sym_None,
	hk_sym_BackSpace,       // 0x08 VK_BACK
	hk_sym_Tab,             // 0x09 VK_TAB
	hk_sym_None,
	hk_sym_None,
	hk_sym_Clear,           // 0x0c VK_CLEAR
	hk_sym_Return,          // 0c0d VK_RETURN
	hk_sym_None,
	hk_sym_None,

	// 0x10 - 0x1f
	hk_sym_Shift_L,         // 0x10 VK_SHIFT
	hk_sym_Control_L,       // 0x11 VK_CONTROL
	hk_sym_Alt_L,           // 0x12 VK_MENU
	hk_sym_Pause,           // 0x13 VK_PAUSE
	hk_sym_Caps_Lock,       // 0x14 VK_CAPITAL
	hk_sym_None,            // 0x15 VK_KANA / VK_HANGEUL / VK_HANGUL
	hk_sym_None,            // 0x16 VK_IME_ON
	hk_sym_None,            // 0x17 VK_JUNJA
	hk_sym_None,            // 0x18 VK_FINAL
	hk_sym_None,            // 0x19 VK_HANJA / VK_KANJI
	hk_sym_None,            // 0x1a VK_IME_OFF
	hk_sym_Escape,          // 0x1b VK_ESCAPE
	hk_sym_None,            // 0x1c VK_CONVERT
	hk_sym_None,            // 0x1d VK_NONCONVERT
	hk_sym_None,            // 0x1e VK_ACCEPT
	hk_sym_None,            // 0x1f VK_MODECHANGE

	// 0x20 - 0x2f
	hk_sym_space,           // 0x20 VK_SPACE
	hk_sym_Page_Up,         // 0x21 VK_PRIOR
	hk_sym_Page_Down,       // 0x22 VK_NEXT
	hk_sym_End,             // 0x23 VK_END
	hk_sym_Home,            // 0x24 VK_HOME
	hk_sym_Left,            // 0x25 VK_LEFT
	hk_sym_Up,              // 0x26 VK_UP
	hk_sym_Right,           // 0x27 VK_RIGHT
	hk_sym_Down,            // 0x28 VK_DOWN
	hk_sym_Select,          // 0x29 VK_SELECT
	hk_sym_Print,           // 0x2a VK_PRINT
	hk_sym_Execute,         // 0x2b VK_EXECUTE
	hk_sym_None,            // 0x2c VK_SNAPSHOT
	hk_sym_Insert,          // 0x2d VK_INSERT
	hk_sym_Delete,          // 0x2e VK_DELETE
	hk_sym_Help,            // 0x2f VK_HELP

	// 0x30 - 0x3f
	hk_sym_0,
	hk_sym_1,
	hk_sym_2,
	hk_sym_3,
	hk_sym_4,
	hk_sym_5,
	hk_sym_6,
	hk_sym_7,
	hk_sym_8,
	hk_sym_9,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,

	// 0x40 - 0x4f
	hk_sym_None,
	hk_sym_A,
	hk_sym_B,
	hk_sym_C,
	hk_sym_D,
	hk_sym_E,
	hk_sym_F,
	hk_sym_G,
	hk_sym_H,
	hk_sym_I,
	hk_sym_J,
	hk_sym_K,
	hk_sym_L,
	hk_sym_M,
	hk_sym_N,
	hk_sym_O,

	// 0x50 - 0x5f
	hk_sym_P,
	hk_sym_Q,
	hk_sym_R,
	hk_sym_S,
	hk_sym_T,
	hk_sym_U,
	hk_sym_V,
	hk_sym_W,
	hk_sym_X,
	hk_sym_Y,
	hk_sym_Z,
	hk_sym_Super_L,         // 0x5b VK_LWIN
	hk_sym_Super_R,         // 0x5c VK_RWIN
	hk_sym_Menu,            // 0x5d VK_APPS
	hk_sym_None,
	hk_sym_None,            // 0x5f VK_SLEEP

	// 0x60 - 0x6f
	hk_sym_KP_0,            // 0x60 VK_NUMPAD0
	hk_sym_KP_1,            // 0x61 VK_NUMPAD1
	hk_sym_KP_2,            // 0x62 VK_NUMPAD2
	hk_sym_KP_3,            // 0x63 VK_NUMPAD3
	hk_sym_KP_4,            // 0x64 VK_NUMPAD4
	hk_sym_KP_5,            // 0x65 VK_NUMPAD5
	hk_sym_KP_6,            // 0x66 VK_NUMPAD6
	hk_sym_KP_7,            // 0x67 VK_NUMPAD7
	hk_sym_KP_8,            // 0x68 VK_NUMPAD8
	hk_sym_KP_9,            // 0x69 VK_NUMPAD9
	hk_sym_KP_Multiply,     // 0x6a VK_MULTIPLY
	hk_sym_KP_Add,          // 0x6b VK_ADD
	hk_sym_KP_Separator,    // 0x6c VK_SEPARATOR
	hk_sym_KP_Subtract,     // 0x6d VK_SUBTRACT
	hk_sym_KP_Decimal,      // 0x6e VK_DECIMAL
	hk_sym_KP_Divide,       // 0x6f VK_DIVIDE

	// 0x70 - 0x7f
	hk_sym_F1,              // 0x70 VK_F1
	hk_sym_F2,              // 0x71 VK_F2
	hk_sym_F3,              // 0x72 VK_F3
	hk_sym_F4,              // 0x73 VK_F4
	hk_sym_F5,              // 0x74 VK_F5
	hk_sym_F6,              // 0x75 VK_F6
	hk_sym_F7,              // 0x76 VK_F7
	hk_sym_F8,              // 0x77 VK_F8
	hk_sym_F9,              // 0x78 VK_F9
	hk_sym_F10,             // 0x79 VK_F10
	hk_sym_F11,             // 0x7a VK_F11
	hk_sym_F12,             // 0x7b VK_F12
	hk_sym_F13,             // 0x7c VK_F13
	hk_sym_F14,             // 0x7d VK_F14
	hk_sym_F15,             // 0x7e VK_F15
	hk_sym_F16,             // 0x7f VK_F16

	// 0x80 - 0x8f
	hk_sym_F17,             // 0x80 VK_F17
	hk_sym_F18,             // 0x81 VK_F18
	hk_sym_F19,             // 0x82 VK_F19
	hk_sym_F20,             // 0x83 VK_F20
	hk_sym_F21,             // 0x84 VK_F21
	hk_sym_F22,             // 0x85 VK_F22
	hk_sym_F23,             // 0x86 VK_F23
	hk_sym_F24,             // 0x87 VK_F24
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,

	// 0x90 - 0x9f
	hk_sym_Num_Lock,        // 0x90 VK_NUMLOCK
	hk_sym_Scroll_Lock,     // 0x91 VK_SCROLL
	hk_sym_None,            // 0x92 VK_OEM_NEC_EQUAL / VK_OEM_FJ_JISHO
	hk_sym_None,            // 0x93 VK_OEM_FJ_MASSHOU
	hk_sym_None,            // 0x94 VK_OEM_FJ_TOUROKU
	hk_sym_None,            // 0x95 VK_OEM_FJ_LOYA
	hk_sym_None,            // 0x96 VK_OEM_FJ_ROYA
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,

	// 0xa0 - 0xaf
	hk_sym_Shift_L,         // 0xa0 VK_LSHIFT
	hk_sym_Shift_R,         // 0xa1 VK_RSHIFT
	hk_sym_Control_L,       // 0xa2 VK_LCONTROL
	hk_sym_Control_R,       // 0xa3 VK_RCONTROL
	hk_sym_Alt_L,           // 0xa4 VK_LMENU
	hk_sym_Alt_R,           // 0xa5 VK_RMENU
	hk_sym_None,            // 0xa6 VK_BROWSER_BACK
	hk_sym_None,            // 0xa7 VK_BROWSER_FORWARD
	hk_sym_None,            // 0xa8 VK_BROWSER_REFRESH
	hk_sym_None,            // 0xa9 VK_BROWSER_STOP
	hk_sym_None,            // 0xaa VK_BROWSER_SEARCH
	hk_sym_None,            // 0xab VK_BROWSER_FAVORITES
	hk_sym_None,            // 0xac VK_BROWSER_HOME
	hk_sym_Mute,            // 0xad VK_VOLUME_MUTE
	hk_sym_Volume_Down,     // 0xae VK_VOLUME_DOWN
	hk_sym_Volume_Up,       // 0xaf VK_VOLUME_UP

	/*
	// 0xb0 - 0xbf
	hk_sym_None,            // 0xb0 VK_MEDIA_NEXT_TRACK
	hk_sym_None,            // 0xb1 VK_MEDIA_PREV_TRACK
	hk_sym_None,            // 0xb2 VK_MEDIA_STOP
	hk_sym_None,            // 0xb3 VK_MEDIA_PLAY_PAUSE
	hk_sym_None,            // 0xb4 VK_LAUNCH_MAIL
	hk_sym_None,            // 0xb5 VK_LAUNCH_MEDIA_SELECT
	hk_sym_None,            // 0xb6 VK_LAUNCH_APP1
	hk_sym_None,            // 0xb7 VK_LAUNCH_APP2
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,            // 0xba VK_OEM_1
	hk_sym_None,            // 0xbb VK_OEM_PLUS
	hk_sym_None,            // 0xbc VK_OEM_COMMA
	hk_sym_None,            // 0xbd VK_OEM_MINUS
	hk_sym_None,            // 0xbe VK_OEM_PERIOD
	hk_sym_None,            // 0xbf VK_OEM_2

	// 0xc0 - 0xcf
	hk_sym_None,            // 0xc0 VK_OEM_3
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,

	// 0xd0 - 0xdf
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,
	hk_sym_None,            // 0xdb VK_OEM_4
	hk_sym_None,            // 0xdc VK_OEM_5
	hk_sym_None,            // 0xdd VK_OEM_6
	hk_sym_None,            // 0xde VK_OEM_7
	hk_sym_None,            // 0xdf VK_OEM_8

	// 0xe0 - 0xef
	hk_sym_None,
	hk_sym_None,            // 0xe1 VK_OEM_AX
	hk_sym_None,            // 0xe2 VK_OEM_102
	hk_sym_None,            // 0xe3 VK_ICO_HELP
	hk_sym_None,            // 0xe4 VK_ICO_00
	hk_sym_None,            // 0xe5 VK_PROCESSKEY
	hk_sym_None,            // 0xe6 VK_ICO_CLEAR
	hk_sym_None,            // 0xe7 VK_PACKET
	hk_sym_None,
	hk_sym_None,            // 0xe9 VK_OEM_RESET
	hk_sym_None,            // 0xea VK_OEM_JUMP
	hk_sym_None,            // 0xeb VK_OEM_PA1
	hk_sym_None,            // 0xec VK_OEM_PA2
	hk_sym_None,            // 0xed VK_OEM_PA3
	hk_sym_None,            // 0xee VK_OEM_WSCTRL
	hk_sym_None,            // 0xef VK_OEM_CUSEL

	// 0xf0 - 0xff
	hk_sym_None,            // 0xf0 VK_OEM_ATTN
	hk_sym_None,            // 0xf1 VK_OEM_FINISH
	hk_sym_None,            // 0xf2 VK_OEM_COPY
	hk_sym_None,            // 0xf3 VK_OEM_AUTO
	hk_sym_None,            // 0xf4 VK_OEM_ENLW
	hk_sym_None,            // 0xf5 VK_OEM_BACKTAB
	hk_sym_None,            // 0xf6 VK_ATTN
	hk_sym_None,            // 0xf7 VK_CRSEL
	hk_sym_None,            // 0xf8 VK_EXSEL
	hk_sym_None,            // 0xf9 VK_EREOF
	hk_sym_None,            // 0xfa VK_PLAY
	hk_sym_None,            // 0xfb VK_ZOOM
	hk_sym_None,            // 0xfc VK_NONAME
	hk_sym_None,            // 0xfd VK_PA1
	hk_sym_None,            // 0xfe VK_OEM_CLEAR
	hk_sym_None,
	*/
};

// Update table of scancode+shift level to symbol mappings

_Bool hk_windows_update_keymap(void) {

	BYTE state[256];
	memset(state, 0, sizeof(state));

	if (hkbd.layout == hk_layout_auto) {
		hkbd.layout = hk_layout_ansi;
		{
			UINT vsc = 0x0056;  // non-US backslash
			UINT vk = MapVirtualKey(vsc, MAPVK_VSC_TO_VK_EX);
			UINT16 wchar;
			(void)ToUnicode(vk, vsc, state, &wchar, 1, 0);
			if (ToUnicode(vk, vsc, state, &wchar, 1, 0) > 0) {
				hkbd.layout = hk_layout_iso;
			}
		}
	}

	// Passing 0xe0XX or 0xe1XX to MapVirtualKey() to query extended
	// scancodes is valid in Windows Vista and later.  That seems to rule
	// out 2000 and XP from the modern era, but they are both long past end
	// of support so that's fine.  Anyone persisting with them will see
	// slightly screwy mappings but things might mostly work.

	// The extended scancode distinction seems to be enough for almost
	// everything, except the keypad / cursor key distinction (see Note 1).
	// MapVirtualKey() returns the cursor keys for both, and there is no
	// Unicode value to distinguish them.  SDL seems to have hit the same
	// problem, and special-cases these keys if the OS flagged an extended
	// code on keypress, i.e. it seems like the difference can only be
	// spotted "live".

	// Due to a Windows curiosity, to query what symbol is bound to
	// AltGr+Key we need to set Left Control and Right Alt in the keyboard
	// state.

#if 0
	for (int vsc = 0; vsc < 128; vsc++) {
		for (int ec = 0; ec < 3; ec++) {
			UINT ex = ec ? ((0xe0 | (ec-1)) << 8) : 0;
			UINT vk = MapVirtualKey(ex|vsc, MAPVK_VSC_TO_VK_EX);
			printf("%04x (%04x):", ex|vsc, vk);
			for (int l = 0; l < HK_NUM_LEVELS; l++) {
				state[VK_SHIFT] = (l & 1) ? 0x80 : 0;
				state[VK_LSHIFT] = (l & 1) ? 0x80 : 0;
				state[VK_MENU] = (l & 2) ? 0x80 : 0;
				state[VK_RMENU] = (l & 2) ? 0x80 : 0;
				state[VK_CONTROL] = (l & 2) ? 0x80 : 0;
				state[VK_LCONTROL] = (l & 2) ? 0x80 : 0;
				UINT16 wchar;

				// Request the key twice.  This way, dead keys should
				// be mapped into the symbol representing the diacritic
				// and the dead state cleared before the next mapping.
				(void)ToUnicode(vk, vsc, state, &wchar, 1, 0);
				if (ToUnicode(vk, vsc, state, &wchar, 1, 0) > 0) {
					printf(" | %04x", wchar);
					if (isprint(wchar)) {
						printf(" '%c'", wchar);
					} else {
						printf("    ");
					}
				} else {
					printf(" |         ");
				}
			}
			printf("\n");
		}
		printf("\n");
	}
#endif

	for (size_t i = 0; i < ARRAY_N_ELEMENTS(windows_to_hk_scancode); i++) {
		UINT vsc = windows_to_hk_scancode[i].vsc;
		UINT vk = MapVirtualKey(vsc, MAPVK_VSC_TO_VK_EX);
		int c = windows_to_hk_scancode[i].hk_scancode;
		if (c == hk_scan_backslash && hkbd.layout == hk_layout_iso) {
			c = hk_scan_numbersign_nonUS;
		}

		for (unsigned l = 0; l < HK_NUM_LEVELS; l++) {
			state[VK_SHIFT] = (l & 1) ? 0x80 : 0;
			state[VK_LSHIFT] = (l & 1) ? 0x80 : 0;
			state[VK_MENU] = (l & 2) ? 0x80 : 0;
			state[VK_RMENU] = (l & 2) ? 0x80 : 0;
			state[VK_CONTROL] = (l & 2) ? 0x80 : 0;
			state[VK_LCONTROL] = (l & 2) ? 0x80 : 0;

			UINT16 wchar = 0;
			uint16_t sym = hk_sym_None;

			// Request the key twice.  This way, dead keys should
			// be mapped into the symbol representing the diacritic
			// and the dead state cleared before the next mapping.
			(void)ToUnicode(vk, vsc & 0x7f, state, &wchar, 1, 0);
			if (ToUnicode(vk, vsc & 0x7f, state, &wchar, 1, 0) > 0) {
				switch (wchar) {
				case 0x08: sym = hk_sym_BackSpace; break;
				case 0x09: sym = hk_sym_Tab; break;
				case 0x0d: sym = hk_sym_Return; break;
				case 0x1b: sym = hk_sym_Escape; break;
				default: sym = wchar; break;
				}
			} else if (vk < ARRAY_N_ELEMENTS(windows_to_hk_sym) && windows_to_hk_sym[vk] != hk_sym_None) {
				sym = windows_to_hk_sym[vk];
			}
			hkbd.code_to_sym[l][c] = sym;

			// I don't believe there's any way to query the OS
			// about which keys generate modifier state, implying
			// it's based purely on the Virtual Key Code:

			switch (sym) {
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
	}
	return 1;
}
