/** \file
 *
 *  \brief Darwin keyboard handling.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <Carbon/Carbon.h>

#include "array.h"
#include "xalloc.h"

#include "hkbd.h"

// Scancode table formed with reference to the diagrams of the Apple Extended
// Keyboard II in Macintosh Toolbox Essentials cross-referenced with constants
// defined in HIToolbox/Events.h (part of the Carbon framework).

// Note 1
//
// Darwin VKC 0x0a (10) should be swapped with VKC 0x32 (50) when an ISO
// layout keyboard is reported.

// Note 2
//
// In ISO layout keyboards, the VKC for backslash is generated instead by the
// key in the non-US numbersign position.

// Note 3
//
// The diagram shows left and right modifiers having different values, but the
// Xkb database for Mac keyboards lists these separate codes for the versions
// on the left side of the keyboard.

const uint8_t darwin_to_hk_scancode[128] = {
	// 0x00 - 0x0f
	hk_scan_a,
	hk_scan_s,
	hk_scan_d,
	hk_scan_f,
	hk_scan_h,
	hk_scan_g,
	hk_scan_z,
	hk_scan_x,
	hk_scan_c,
	hk_scan_v,
	hk_scan_backslash_nonUS,  // see Note 1
	hk_scan_b,
	hk_scan_q,
	hk_scan_w,
	hk_scan_e,
	hk_scan_r,

	// 0x10 - 0x1f
	hk_scan_y,
	hk_scan_t,
	hk_scan_1,
	hk_scan_2,
	hk_scan_3,
	hk_scan_4,
	hk_scan_6,
	hk_scan_5,
	hk_scan_equal,
	hk_scan_9,
	hk_scan_7,
	hk_scan_minus,
	hk_scan_8,
	hk_scan_0,
	hk_scan_bracketright,
	hk_scan_o,

	// 0x20 - 0x2f
	hk_scan_u,
	hk_scan_bracketleft,
	hk_scan_i,
	hk_scan_p,
	hk_scan_Return,
	hk_scan_l,
	hk_scan_j,
	hk_scan_apostrophe,
	hk_scan_k,
	hk_scan_semicolon,
	hk_scan_backslash,  // see Note 2
	hk_scan_comma,
	hk_scan_slash,
	hk_scan_n,
	hk_scan_m,
	hk_scan_period,

	// 0x30 - 0x3f
	hk_scan_Tab,
	hk_scan_space,
	hk_scan_grave,  // see Note 1
	hk_scan_BackSpace,
	hk_scan_None,
	hk_scan_Escape,
	hk_scan_None,
	hk_scan_Super_L,
	hk_scan_Shift_L,
	hk_scan_Caps_Lock,
	hk_scan_Alt_L,
	hk_scan_Control_L,
	hk_scan_Shift_R,
	hk_scan_Alt_R,
	hk_scan_Control_R,
	hk_scan_None,

	// 0x40 - 0x4f
	hk_scan_F17,
	hk_scan_KP_Decimal,
	hk_scan_None,
	hk_scan_KP_Multiply,
	hk_scan_None,
	hk_scan_KP_Add,
	hk_scan_None,
	hk_scan_None,
	hk_scan_Volume_Up,
	hk_scan_Volume_Down,
	hk_scan_Mute,
	hk_scan_KP_Divide,
	hk_scan_KP_Enter,
	hk_scan_None,
	hk_scan_KP_Subtract,
	hk_scan_F18,

	// 0x50 - 0x5f
	hk_scan_F19,
	hk_scan_KP_Equal,
	hk_scan_KP_0,
	hk_scan_KP_1,
	hk_scan_KP_2,
	hk_scan_KP_3,
	hk_scan_KP_4,
	hk_scan_KP_5,
	hk_scan_KP_6,
	hk_scan_KP_7,
	hk_scan_F20,
	hk_scan_KP_8,
	hk_scan_KP_9,
	hk_scan_None,
	hk_scan_None,
	hk_scan_None,

	// 0x60 - 0x6f
	hk_scan_F5,
	hk_scan_F6,
	hk_scan_F7,
	hk_scan_F3,
	hk_scan_F8,
	hk_scan_F9,
	hk_scan_None,
	hk_scan_F11,
	hk_scan_None,
	hk_scan_F13,
	hk_scan_F16,
	hk_scan_F14,
	hk_scan_None,
	hk_scan_F10,
	hk_scan_None,
	hk_scan_F12,

	// 0x70 - 0x7f
	hk_scan_None,
	hk_scan_F15,
	hk_scan_Help,
	hk_scan_Home,
	hk_scan_Page_Up,
	hk_scan_Delete,
	hk_scan_F4,
	hk_scan_End,
	hk_scan_F2,
	hk_scan_Page_Down,
	hk_scan_F1,
	hk_scan_Left,
	hk_scan_Right,
	hk_scan_Down,
	hk_scan_Up,
	hk_scan_None
};

static int8_t hk_scancode_to_darwin[HK_NUM_SCANCODES];

// Update table of scancode+shift level to symbol mappings

_Bool hk_darwin_update_keymap(void) {
	// Invert the table above
	for (size_t i = 0; i < ARRAY_N_ELEMENTS(hk_scancode_to_darwin); ++i) {
		hk_scancode_to_darwin[i] = -1;
	}
	for (size_t i = 0; i < ARRAY_N_ELEMENTS(darwin_to_hk_scancode); ++i) {
		int scan = darwin_to_hk_scancode[i];
		if (scan != hk_scan_None) {
			hk_scancode_to_darwin[scan] = i;
		}
	}

	if (hkbd.layout == hk_layout_auto) {
		switch (KBGetLayoutType(LMGetKbdType())) {
		case kKeyboardJIS:
			hkbd.layout = hk_layout_jis;
			break;
		case kKeyboardANSI:
			hkbd.layout = hk_layout_ansi;
			break;
		case kKeyboardISO:
			hkbd.layout = hk_layout_iso;
			break;
		default:
			break;
		}
	}

	// Approach to navigating the convoluted way macosx hides its data away
	// adapted from a stackoverflow reply by jlstrecker.

	TISInputSourceRef kbd_ref = TISCopyCurrentKeyboardLayoutInputSource();

	CFDataRef layout_ref = TISGetInputSourceProperty(kbd_ref, kTISPropertyUnicodeKeyLayoutData);
	const UCKeyboardLayout *layout;
	if (layout_ref) {
		layout = (const UCKeyboardLayout *)CFDataGetBytePtr(layout_ref);
	} else {
		CFRelease(kbd_ref);
		return 0;
	}

	UniChar buf[8];

	for (unsigned c = 0; c < HK_NUM_SCANCODES; c++) {
		int darwin_vkc = hk_scancode_to_darwin[c];
		if (hkbd.layout == hk_layout_iso) {
			if (darwin_vkc == 0x0a) {
				darwin_vkc = 0x32;
			} else if (darwin_vkc == 0x32) {
				darwin_vkc = 0x0a;
			} else if (c == hk_scan_numbersign_nonUS) {
				darwin_vkc = 0x2a;
			}
		}

		UInt16 virtualKeyCode = darwin_vkc;
		for (unsigned l = 0; l < HK_NUM_LEVELS; l++) {
			UInt32 modifierKeyState = 0;
			if (l & 1)
				modifierKeyState |= ((1 << shiftKeyBit) >> 8);
			if (l & 2)
				modifierKeyState |= ((1 << optionKeyBit) >> 8);
			UInt32 deadKeyState = 0;
			UniCharCount uclen = 0;
			UCKeyTranslate(layout, virtualKeyCode,
				       kUCKeyActionDown, modifierKeyState, LMGetKbdType(),
				       kUCKeyTranslateNoDeadKeysMask, &deadKeyState, 8, &uclen, buf);

			uint16_t sym = hk_sym_None;
			if (uclen == 1 && buf[0] != 0) {
				if (buf[0] == kFunctionKeyCharCode) {
					switch (virtualKeyCode) {
					case kVK_F1: sym = hk_sym_F1; break;
					case kVK_F2: sym = hk_sym_F2; break;
					case kVK_F3: sym = hk_sym_F3; break;
					case kVK_F4: sym = hk_sym_F4; break;
					case kVK_F5: sym = hk_sym_F5; break;
					case kVK_F6: sym = hk_sym_F6; break;
					case kVK_F7: sym = hk_sym_F7; break;
					case kVK_F8: sym = hk_sym_F8; break;
					case kVK_F9: sym = hk_sym_F9; break;
					case kVK_F10: sym = hk_sym_F10; break;
					case kVK_F11: sym = hk_sym_F11; break;
					case kVK_F12: sym = hk_sym_F12; break;
					case kVK_F13: sym = hk_sym_F13; break;
					case kVK_F14: sym = hk_sym_F14; break;
					case kVK_F15: sym = hk_sym_F15; break;
					case kVK_F16: sym = hk_sym_F16; break;
					case kVK_F17: sym = hk_sym_F17; break;
					case kVK_F18: sym = hk_sym_F18; break;
					case kVK_F19: sym = hk_sym_F19; break;
					case kVK_F20: sym = hk_sym_F20; break;
					default: break;
					}
				} else {
					switch (buf[0]) {
					case kBellCharCode:
					case kCommandCharCode:
					case kCheckCharCode:
					case kDiamondCharCode:
					case kAppleLogoCharCode:
					case kBulletCharCode:
					case kNonBreakingSpaceCharCode:
						sym = hk_sym_None;
						break;

					case kHomeCharCode: sym = hk_sym_Home; break;
					case kEnterCharCode: sym = hk_sym_KP_Enter; break;
					case kEndCharCode: sym = hk_sym_End; break;
					case kHelpCharCode: sym = hk_sym_Help; break;
					case kBackspaceCharCode: sym = hk_sym_BackSpace; break;
					case kTabCharCode: sym = hk_sym_Tab; break;
					case kLineFeedCharCode: sym = hk_sym_Linefeed; break;
					case kPageUpCharCode: sym = hk_sym_Page_Up; break;
					case kPageDownCharCode: sym = hk_sym_Page_Down; break;
					case kReturnCharCode: sym = hk_sym_Return; break;
					case kEscapeCharCode: sym = hk_sym_Escape; break;
					case kLeftArrowCharCode: sym = hk_sym_Left; break;
					case kRightArrowCharCode: sym = hk_sym_Right; break;
					case kUpArrowCharCode: sym = hk_sym_Up; break;
					case kDownArrowCharCode: sym = hk_sym_Down; break;
					case kDeleteCharCode: sym = hk_sym_Delete; break;

					default: sym = buf[0]; break;
					}

				}
			}
			hkbd.code_to_sym[l][c] = sym;
		}
	}

	// Fixed mappings
	for (unsigned l = 0; l < HK_NUM_LEVELS; l++) {
		hkbd.code_to_sym[l][hk_scan_Shift_L] = hk_sym_Shift_L;
		hkbd.code_to_sym[l][hk_scan_Shift_R] = hk_sym_Shift_R;
		hkbd.code_to_sym[l][hk_scan_Control_L] = hk_sym_Control_L;
		hkbd.code_to_sym[l][hk_scan_Control_R] = hk_sym_Control_R;
		hkbd.code_to_sym[l][hk_scan_Alt_L] = hk_sym_Alt_L;
		hkbd.code_to_sym[l][hk_scan_Alt_R] = hk_sym_Alt_R;
		hkbd.code_to_sym[l][hk_scan_Super_L] = hk_sym_Super_L;
		hkbd.code_to_sym[l][hk_scan_Super_R] = hk_sym_Super_R;
	}
	hkbd.scancode_mod[hk_scan_Shift_L] = HK_MASK_SHIFT;
	hkbd.scancode_mod[hk_scan_Shift_R] = HK_MASK_SHIFT;
	hkbd.scancode_mod[hk_scan_Control_L] = HK_MASK_CONTROL;
	hkbd.scancode_mod[hk_scan_Control_R] = HK_MASK_CONTROL;
	hkbd.scancode_mod[hk_scan_Alt_L] = HK_MASK_ALT;
	hkbd.scancode_mod[hk_scan_Alt_R] = HK_MASK_ALTGR;

	CFRelease(kbd_ref);
	return 1;
}
