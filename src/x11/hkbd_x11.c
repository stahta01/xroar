/** \file
 *
 *  \brief X11 keyboard handling.
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#define XK_TECHNICAL
#define XK_PUBLISHING
#include <X11/keysym.h>

#include "array.h"
#include "logging.h"
#include "xalloc.h"

#include "hkbd.h"
#include "x11/hkbd_x11.h"

#include "x11/hkbd_x11_keycode_tables.h"

// We borrow a method of "fingerprinting" the keyboard from SDL: check which
// keycodes a particular set of keysyms are bound to.  Pick keys that don't
// tend to wander based on internationalised layouts.  Here are the syms to
// check:

static struct {
	KeySym keysym;
	KeyCode keycode;
} fingerprint[] = {
	{ .keysym = XK_Home },
	{ .keysym = XK_Prior },
	{ .keysym = XK_Up },
	{ .keysym = XK_Left },
	{ .keysym = XK_Delete },
	{ .keysym = XK_KP_Enter },
};

// And here are the keycode fingerprints:

static const struct {
	const char *description;
	const int keycode_fingerprint[6];
	const uint8_t *keycode_table;
} fingerprint_to_map[] = {
	{
		.description = "xfree86(xfree86)",
		.keycode_fingerprint = { 97, 99, 98, 100, 107, 108 },
		.keycode_table = xkb_xfree86_xfree86_to_hk_scancode,
	},
	{
		.description = "evdev(evdev)",
		.keycode_fingerprint = { 110, 112, 111, 113, 119, 104 },
		.keycode_table = xkb_evdev_evdev_to_hk_scancode,
	},
	{
		.description = "sun(type6tuv)",
		.keycode_fingerprint = { 59, 103, 27, 31, 73, 97 },
		.keycode_table = xkb_macintosh_old_to_hk_scancode,
	},
	{
		.description = "macintosh(old)",
		.keycode_fingerprint = { 123, 124, 134, 131, 125, 60 },
		.keycode_table = xkb_macintosh_old_to_hk_scancode,
	},
};

static Display *display = NULL;

// Initialise X11 keyboard handling for the provided display.

void hk_x11_set_display(Display *d) {
	display = d;
}

// Fingerprint keyboard and update code to sym mapping table.

_Bool hk_x11_update_keymap(void) {
	// Toolkit should have called hk_x11_set_display() first:
	if (!display) {
		return 0;
	}

	// Take fingerprint
	for (size_t i = 0; i < ARRAY_N_ELEMENTS(fingerprint); ++i) {
		fingerprint[i].keycode = XKeysymToKeycode(display, fingerprint[i].keysym);
	}

	// Find best match
	int max_matched = 1;  // require at least two matches
	int table = -1;
	for (size_t i = 0; i < ARRAY_N_ELEMENTS(fingerprint_to_map); ++i) {
		int matched = 0;
		for (size_t j = 0; j < ARRAY_N_ELEMENTS(fingerprint); ++j) {
			if (fingerprint_to_map[i].keycode_fingerprint[j] == fingerprint[j].keycode)
				matched++;
		}
		if (matched > max_matched) {
			max_matched = matched;
			table = i;
		}
	}

	// Check that a suitable keycode table has been identified
	if (table < 0) {
		LOG_DEBUG(2, "[hkbd/x11] no keycode table found\n");
		if (os_scancode_to_hk_scancode) {
			free(os_scancode_to_hk_scancode);
			os_scancode_to_hk_scancode = NULL;
		}
		hk_num_os_scancodes = 0;
		return 0;
	}
	LOG_DEBUG(2, "[hkbd/x11] keycode table: %s\n", fingerprint_to_map[table].description);
	hk_num_os_scancodes = 256;
	os_scancode_to_hk_scancode = xmalloc(256 * sizeof(*os_scancode_to_hk_scancode));
	memcpy(os_scancode_to_hk_scancode, fingerprint_to_map[table].keycode_table, 256 * sizeof(*os_scancode_to_hk_scancode));

	XModifierKeymap *modmap = XGetModifierMapping(display);
	unsigned max_keypermod = modmap->max_keypermod >= 0 ? modmap->max_keypermod : 0;

	// Build the scancode to symbol mapping table
	for (unsigned x11_keycode = 8; x11_keycode < 256; ++x11_keycode) {
		int code = os_scancode_to_hk_scancode[x11_keycode];
		if (!code) {
			continue;
		}

		for (unsigned m = 0; m < max_keypermod; ++m) {
			if (modmap->modifiermap[ShiftMapIndex * max_keypermod + m] == x11_keycode) {
				hkbd.scancode_mod[code] |= HK_MASK_SHIFT;
			}
			if (modmap->modifiermap[ControlMapIndex * max_keypermod + m] == x11_keycode) {
				hkbd.scancode_mod[code] |= HK_MASK_CONTROL;
			}
			if (modmap->modifiermap[Mod1MapIndex * max_keypermod + m] == x11_keycode) {
				hkbd.scancode_mod[code] |= HK_MASK_ALT;
			}
		}

		int tmp_nlev;
		KeySym *syms = XGetKeyboardMapping(display, (KeyCode)x11_keycode, 1, &tmp_nlev);
		unsigned nlevels = tmp_nlev >= 0 ? tmp_nlev : 0;
		for (unsigned l = 0; l < HK_NUM_LEVELS && l < nlevels; ++l) {
			if (hkbd.code_to_sym[l][code] != hk_sym_None)
				continue;

			unsigned syml = l;
			if (syml >= 2) {
				syml += 2;
			}
			if (syml >= nlevels) {
				continue;
			}
			KeySym x11_sym = syms[syml];

			if (l == 0) {
				switch (x11_sym) {
				case XK_ISO_Level3_Shift:
					hkbd.scancode_mod[code] |= HK_MASK_ALTGR;
					break;
				default:
					break;
				}
			}

			hkbd.code_to_sym[l][code] = x11_keysym_to_hk_sym(x11_sym);
		}
		XFree(syms);
	}
	XFreeModifiermap(modmap);

	if (hkbd.layout == hk_layout_auto) {
		hkbd.layout = hk_layout_ansi;
		if (hkbd.code_to_sym[0][hk_scan_backslash] == hk_sym_None &&
		    hkbd.code_to_sym[0][hk_scan_backslash_nonUS] != hk_sym_None) {
			hkbd.layout = hk_layout_iso;
		}
	}
	return 1;
}

// Many X11 syms map directly.  But many still require translation as the X11
// values were clearly assigned before Unicode standardised.
//
// Latin 8 values are Unicode with bit 24 set, so they fall through simply by
// masking.

uint16_t x11_keysym_to_hk_sym(KeySym x11_sym) {
	uint16_t sym;
	switch (x11_sym) {
	default:
		sym = x11_sym & 0xffff;
		break;

	case XK_VoidSymbol: sym = hk_sym_None; break;

	case XK_Aogonek: sym = hk_sym_Aogonek; break;  // Latin 2
	case XK_breve: sym = hk_sym_breve; break;
	case XK_Lstroke: sym = hk_sym_Lstroke; break;
	case XK_Lcaron: sym = hk_sym_Lcaron; break;
	case XK_Sacute: sym = hk_sym_Sacute; break;
	case XK_Scaron: sym = hk_sym_Scaron; break;
	case XK_Scedilla: sym = hk_sym_Scedilla; break;
	case XK_Tcaron: sym = hk_sym_Tcaron; break;
	case XK_Zacute: sym = hk_sym_Zacute; break;
	case XK_Zcaron: sym = hk_sym_Zcaron; break;
	case XK_Zabovedot: sym = hk_sym_Zabovedot; break;
	case XK_aogonek: sym = hk_sym_aogonek; break;
	case XK_ogonek: sym = hk_sym_ogonek; break;
	case XK_lstroke: sym = hk_sym_lstroke; break;
	case XK_lcaron: sym = hk_sym_lcaron; break;
	case XK_sacute: sym = hk_sym_sacute; break;
	case XK_caron: sym = hk_sym_caron; break;
	case XK_scaron: sym = hk_sym_scaron; break;
	case XK_scedilla: sym = hk_sym_scedilla; break;
	case XK_tcaron: sym = hk_sym_tcaron; break;
	case XK_zacute: sym = hk_sym_zacute; break;
	case XK_doubleacute: sym = hk_sym_doubleacute; break;
	case XK_zcaron: sym = hk_sym_zcaron; break;
	case XK_zabovedot: sym = hk_sym_zabovedot; break;
	case XK_Racute: sym = hk_sym_Racute; break;
	case XK_Abreve: sym = hk_sym_Abreve; break;
	case XK_Lacute: sym = hk_sym_Lacute; break;
	case XK_Cacute: sym = hk_sym_Cacute; break;
	case XK_Ccaron: sym = hk_sym_Ccaron; break;
	case XK_Eogonek: sym = hk_sym_Eogonek; break;
	case XK_Ecaron: sym = hk_sym_Ecaron; break;
	case XK_Dcaron: sym = hk_sym_Dcaron; break;
	case XK_Dstroke: sym = hk_sym_Dstroke; break;
	case XK_Nacute: sym = hk_sym_Nacute; break;
	case XK_Ncaron: sym = hk_sym_Ncaron; break;
	case XK_Odoubleacute: sym = hk_sym_Odoubleacute; break;
	case XK_Rcaron: sym = hk_sym_Rcaron; break;
	case XK_Uring: sym = hk_sym_Uring; break;
	case XK_Udoubleacute: sym = hk_sym_Udoubleacute; break;
	case XK_Tcedilla: sym = hk_sym_Tcedilla; break;
	case XK_racute: sym = hk_sym_racute; break;
	case XK_abreve: sym = hk_sym_abreve; break;
	case XK_lacute: sym = hk_sym_lacute; break;
	case XK_cacute: sym = hk_sym_cacute; break;
	case XK_ccaron: sym = hk_sym_ccaron; break;
	case XK_eogonek: sym = hk_sym_eogonek; break;
	case XK_ecaron: sym = hk_sym_ecaron; break;
	case XK_dcaron: sym = hk_sym_dcaron; break;
	case XK_dstroke: sym = hk_sym_dstroke; break;
	case XK_nacute: sym = hk_sym_nacute; break;
	case XK_ncaron: sym = hk_sym_ncaron; break;
	case XK_odoubleacute: sym = hk_sym_odoubleacute; break;
	case XK_rcaron: sym = hk_sym_rcaron; break;
	case XK_uring: sym = hk_sym_uring; break;
	case XK_udoubleacute: sym = hk_sym_udoubleacute; break;
	case XK_tcedilla: sym = hk_sym_tcedilla; break;
	case XK_abovedot: sym = hk_sym_abovedot; break;

	case XK_Hstroke: sym = hk_sym_Hstroke; break;  // Latin 3
	case XK_Hcircumflex: sym = hk_sym_Hcircumflex; break;
	case XK_Iabovedot: sym = hk_sym_Iabovedot; break;
	case XK_Gbreve: sym = hk_sym_Gbreve; break;
	case XK_Jcircumflex: sym = hk_sym_Jcircumflex; break;
	case XK_hstroke: sym = hk_sym_hstroke; break;
	case XK_hcircumflex: sym = hk_sym_hcircumflex; break;
	case XK_idotless: sym = hk_sym_idotless; break;
	case XK_gbreve: sym = hk_sym_gbreve; break;
	case XK_jcircumflex: sym = hk_sym_jcircumflex; break;
	case XK_Cabovedot: sym = hk_sym_Cabovedot; break;
	case XK_Ccircumflex: sym = hk_sym_Ccircumflex; break;
	case XK_Gabovedot: sym = hk_sym_Gabovedot; break;
	case XK_Gcircumflex: sym = hk_sym_Gcircumflex; break;
	case XK_Ubreve: sym = hk_sym_Ubreve; break;
	case XK_Scircumflex: sym = hk_sym_Scircumflex; break;
	case XK_cabovedot: sym = hk_sym_cabovedot; break;
	case XK_ccircumflex: sym = hk_sym_ccircumflex; break;
	case XK_gabovedot: sym = hk_sym_gabovedot; break;
	case XK_gcircumflex: sym = hk_sym_gcircumflex; break;
	case XK_ubreve: sym = hk_sym_ubreve; break;
	case XK_scircumflex: sym = hk_sym_scircumflex; break;

	case XK_kra: sym = hk_sym_kra; break;  // Latin 4
	case XK_Rcedilla: sym = hk_sym_Rcedilla; break;
	case XK_Itilde: sym = hk_sym_Itilde; break;
	case XK_Lcedilla: sym = hk_sym_Lcedilla; break;
	case XK_Emacron: sym = hk_sym_Emacron; break;
	case XK_Gcedilla: sym = hk_sym_Gcedilla; break;
	case XK_Tslash: sym = hk_sym_Tslash; break;
	case XK_rcedilla: sym = hk_sym_rcedilla; break;
	case XK_itilde: sym = hk_sym_itilde; break;
	case XK_lcedilla: sym = hk_sym_lcedilla; break;
	case XK_emacron: sym = hk_sym_emacron; break;
	case XK_gcedilla: sym = hk_sym_gcedilla; break;
	case XK_tslash: sym = hk_sym_tslash; break;
	case XK_ENG: sym = hk_sym_ENG; break;
	case XK_eng: sym = hk_sym_eng; break;
	case XK_Amacron: sym = hk_sym_Amacron; break;
	case XK_Iogonek: sym = hk_sym_Iogonek; break;
	case XK_Eabovedot: sym = hk_sym_Eabovedot; break;
	case XK_Imacron: sym = hk_sym_Imacron; break;
	case XK_Ncedilla: sym = hk_sym_Ncedilla; break;
	case XK_Omacron: sym = hk_sym_Omacron; break;
	case XK_Kcedilla: sym = hk_sym_Kcedilla; break;
	case XK_Uogonek: sym = hk_sym_Uogonek; break;
	case XK_Utilde: sym = hk_sym_Utilde; break;
	case XK_Umacron: sym = hk_sym_Umacron; break;
	case XK_amacron: sym = hk_sym_amacron; break;
	case XK_iogonek: sym = hk_sym_iogonek; break;
	case XK_eabovedot: sym = hk_sym_eabovedot; break;
	case XK_imacron: sym = hk_sym_imacron; break;
	case XK_ncedilla: sym = hk_sym_ncedilla; break;
	case XK_omacron: sym = hk_sym_omacron; break;
	case XK_kcedilla: sym = hk_sym_kcedilla; break;
	case XK_uogonek: sym = hk_sym_uogonek; break;
	case XK_utilde: sym = hk_sym_utilde; break;
	case XK_umacron: sym = hk_sym_umacron; break;

	case XK_OE: sym = hk_sym_OE; break;  // Latin 9
	case XK_oe: sym = hk_sym_oe; break;
	case XK_Ydiaeresis: sym = hk_sym_Ydiaeresis; break;

	case XK_Greek_ALPHAaccent: sym = hk_sym_Greek_ALPHAaccent; break;  // Greek
	case XK_Greek_EPSILONaccent: sym = hk_sym_Greek_EPSILONaccent; break;
	case XK_Greek_ETAaccent: sym = hk_sym_Greek_ETAaccent; break;
	case XK_Greek_IOTAaccent: sym = hk_sym_Greek_IOTAaccent; break;
	case XK_Greek_IOTAdieresis: sym = hk_sym_Greek_IOTAdieresis; break;
	case XK_Greek_OMICRONaccent: sym = hk_sym_Greek_OMICRONaccent; break;
	case XK_Greek_UPSILONaccent: sym = hk_sym_Greek_UPSILONaccent; break;
	case XK_Greek_UPSILONdieresis: sym = hk_sym_Greek_UPSILONdieresis; break;
	case XK_Greek_OMEGAaccent: sym = hk_sym_Greek_OMEGAaccent; break;
	case XK_Greek_accentdieresis: sym = hk_sym_Greek_accentdieresis; break;
	case XK_Greek_horizbar: sym = hk_sym_Greek_horizbar; break;
	case XK_Greek_alphaaccent: sym = hk_sym_Greek_alphaaccent; break;
	case XK_Greek_epsilonaccent: sym = hk_sym_Greek_epsilonaccent; break;
	case XK_Greek_etaaccent: sym = hk_sym_Greek_etaaccent; break;
	case XK_Greek_iotaaccent: sym = hk_sym_Greek_iotaaccent; break;
	case XK_Greek_iotadieresis: sym = hk_sym_Greek_iotadieresis; break;
	case XK_Greek_iotaaccentdieresis: sym = hk_sym_Greek_iotaaccentdieresis; break;
	case XK_Greek_omicronaccent: sym = hk_sym_Greek_omicronaccent; break;
	case XK_Greek_upsilonaccent: sym = hk_sym_Greek_upsilonaccent; break;
	case XK_Greek_upsilondieresis: sym = hk_sym_Greek_upsilondieresis; break;
	case XK_Greek_upsilonaccentdieresis: sym = hk_sym_Greek_upsilonaccentdieresis; break;
	case XK_Greek_omegaaccent: sym = hk_sym_Greek_omegaaccent; break;
	case XK_Greek_ALPHA: sym = hk_sym_Greek_ALPHA; break;
	case XK_Greek_BETA: sym = hk_sym_Greek_BETA; break;
	case XK_Greek_GAMMA: sym = hk_sym_Greek_GAMMA; break;
	case XK_Greek_DELTA: sym = hk_sym_Greek_DELTA; break;
	case XK_Greek_EPSILON: sym = hk_sym_Greek_EPSILON; break;
	case XK_Greek_ZETA: sym = hk_sym_Greek_ZETA; break;
	case XK_Greek_ETA: sym = hk_sym_Greek_ETA; break;
	case XK_Greek_THETA: sym = hk_sym_Greek_THETA; break;
	case XK_Greek_IOTA: sym = hk_sym_Greek_IOTA; break;
	case XK_Greek_KAPPA: sym = hk_sym_Greek_KAPPA; break;
	case XK_Greek_LAMDA: sym = hk_sym_Greek_LAMDA; break;
	case XK_Greek_MU: sym = hk_sym_Greek_MU; break;
	case XK_Greek_NU: sym = hk_sym_Greek_NU; break;
	case XK_Greek_XI: sym = hk_sym_Greek_XI; break;
	case XK_Greek_OMICRON: sym = hk_sym_Greek_OMICRON; break;
	case XK_Greek_PI: sym = hk_sym_Greek_PI; break;
	case XK_Greek_RHO: sym = hk_sym_Greek_RHO; break;
	case XK_Greek_SIGMA: sym = hk_sym_Greek_SIGMA; break;
	case XK_Greek_TAU: sym = hk_sym_Greek_TAU; break;
	case XK_Greek_UPSILON: sym = hk_sym_Greek_UPSILON; break;
	case XK_Greek_PHI: sym = hk_sym_Greek_PHI; break;
	case XK_Greek_CHI: sym = hk_sym_Greek_CHI; break;
	case XK_Greek_PSI: sym = hk_sym_Greek_PSI; break;
	case XK_Greek_OMEGA: sym = hk_sym_Greek_OMEGA; break;
	case XK_Greek_alpha: sym = hk_sym_Greek_alpha; break;
	case XK_Greek_beta: sym = hk_sym_Greek_beta; break;
	case XK_Greek_gamma: sym = hk_sym_Greek_gamma; break;
	case XK_Greek_delta: sym = hk_sym_Greek_delta; break;
	case XK_Greek_epsilon: sym = hk_sym_Greek_epsilon; break;
	case XK_Greek_zeta: sym = hk_sym_Greek_zeta; break;
	case XK_Greek_eta: sym = hk_sym_Greek_eta; break;
	case XK_Greek_theta: sym = hk_sym_Greek_theta; break;
	case XK_Greek_iota: sym = hk_sym_Greek_iota; break;
	case XK_Greek_kappa: sym = hk_sym_Greek_kappa; break;
	case XK_Greek_lamda: sym = hk_sym_Greek_lamda; break;
	case XK_Greek_mu: sym = hk_sym_Greek_mu; break;
	case XK_Greek_nu: sym = hk_sym_Greek_nu; break;
	case XK_Greek_xi: sym = hk_sym_Greek_xi; break;
	case XK_Greek_omicron: sym = hk_sym_Greek_omicron; break;
	case XK_Greek_pi: sym = hk_sym_Greek_pi; break;
	case XK_Greek_rho: sym = hk_sym_Greek_rho; break;
	case XK_Greek_sigma: sym = hk_sym_Greek_sigma; break;
	case XK_Greek_finalsmallsigma: sym = hk_sym_Greek_finalsmallsigma; break;
	case XK_Greek_tau: sym = hk_sym_Greek_tau; break;
	case XK_Greek_upsilon: sym = hk_sym_Greek_upsilon; break;
	case XK_Greek_phi: sym = hk_sym_Greek_phi; break;
	case XK_Greek_chi: sym = hk_sym_Greek_chi; break;
	case XK_Greek_psi: sym = hk_sym_Greek_psi; break;
	case XK_Greek_omega: sym = hk_sym_Greek_omega; break;

	case XK_leftradical: sym = hk_sym_leftradical; break;  // Technical
	case XK_topintegral: sym = hk_sym_topintegral; break;
	case XK_botintegral: sym = hk_sym_botintegral; break;
	case XK_topleftsqbracket: sym = hk_sym_topleftsqbracket; break;
	case XK_botleftsqbracket: sym = hk_sym_botleftsqbracket; break;
	case XK_toprightsqbracket: sym = hk_sym_toprightsqbracket; break;
	case XK_botrightsqbracket: sym = hk_sym_botrightsqbracket; break;
	case XK_topleftparens: sym = hk_sym_topleftparens; break;
	case XK_botleftparens: sym = hk_sym_botleftparens; break;
	case XK_toprightparens: sym = hk_sym_toprightparens; break;
	case XK_botrightparens: sym = hk_sym_botrightparens; break;
	case XK_leftmiddlecurlybrace: sym = hk_sym_leftmiddlecurlybrace; break;
	case XK_rightmiddlecurlybrace: sym = hk_sym_rightmiddlecurlybrace; break;
	case XK_lessthanequal: sym = hk_sym_lessthanequal; break;
	case XK_notequal: sym = hk_sym_notequal; break;
	case XK_greaterthanequal: sym = hk_sym_greaterthanequal; break;
	case XK_integral: sym = hk_sym_integral; break;
	case XK_therefore: sym = hk_sym_therefore; break;
	case XK_variation: sym = hk_sym_variation; break;
	case XK_infinity: sym = hk_sym_infinity; break;
	case XK_nabla: sym = hk_sym_nabla; break;
	case XK_approximate: sym = hk_sym_approximate; break;
	case XK_similarequal: sym = hk_sym_similarequal; break;
	case XK_ifonlyif: sym = hk_sym_ifonlyif; break;
	case XK_implies: sym = hk_sym_implies; break;
	case XK_identical: sym = hk_sym_identical; break;
	case XK_radical: sym = hk_sym_radical; break;
	case XK_includedin: sym = hk_sym_includedin; break;
	case XK_includes: sym = hk_sym_includes; break;
	case XK_intersection: sym = hk_sym_intersection; break;
	case XK_union: sym = hk_sym_union; break;
	case XK_logicaland: sym = hk_sym_logicaland; break;
	case XK_logicalor: sym = hk_sym_logicalor; break;
	case XK_partialderivative: sym = hk_sym_partialderivative; break;
	case XK_function: sym = hk_sym_function; break;
	case XK_leftarrow: sym = hk_sym_leftarrow; break;
	case XK_uparrow: sym = hk_sym_uparrow; break;
	case XK_rightarrow: sym = hk_sym_rightarrow; break;
	case XK_downarrow: sym = hk_sym_downarrow; break;

	case XK_emspace: sym = hk_sym_emspace; break;  // Publishing
	case XK_enspace: sym = hk_sym_enspace; break;
	case XK_em3space: sym = hk_sym_em3space; break;
	case XK_em4space: sym = hk_sym_em4space; break;
	case XK_digitspace: sym = hk_sym_digitspace; break;
	case XK_punctspace: sym = hk_sym_punctspace; break;
	case XK_thinspace: sym = hk_sym_thinspace; break;
	case XK_hairspace: sym = hk_sym_hairspace; break;
	case XK_emdash: sym = hk_sym_emdash; break;
	case XK_endash: sym = hk_sym_endash; break;
	case XK_ellipsis: sym = hk_sym_ellipsis; break;
	case XK_doubbaselinedot: sym = hk_sym_doubbaselinedot; break;
	case XK_onethird: sym = hk_sym_onethird; break;
	case XK_twothirds: sym = hk_sym_twothirds; break;
	case XK_onefifth: sym = hk_sym_onefifth; break;
	case XK_twofifths: sym = hk_sym_twofifths; break;
	case XK_threefifths: sym = hk_sym_threefifths; break;
	case XK_fourfifths: sym = hk_sym_fourfifths; break;
	case XK_onesixth: sym = hk_sym_onesixth; break;
	case XK_fivesixths: sym = hk_sym_fivesixths; break;
	case XK_oneeighth: sym = hk_sym_oneeighth; break;
	case XK_threeeighths: sym = hk_sym_threeeighths; break;
	case XK_fiveeighths: sym = hk_sym_fiveeighths; break;
	case XK_seveneighths: sym = hk_sym_seveneighths; break;
	case XK_trademark: sym = hk_sym_trademark; break;
	case XK_leftsinglequotemark: sym = hk_sym_leftsinglequotemark; break;
	case XK_rightsinglequotemark: sym = hk_sym_rightsinglequotemark; break;
	case XK_leftdoublequotemark: sym = hk_sym_leftdoublequotemark; break;
	case XK_rightdoublequotemark: sym = hk_sym_rightdoublequotemark; break;
	case XK_permille: sym = hk_sym_permille; break;
	case XK_dagger: sym = hk_sym_dagger; break;
	case XK_doubledagger: sym = hk_sym_doubledagger; break;
	case XK_singlelowquotemark: sym = hk_sym_singlelowquotemark; break;
	case XK_doublelowquotemark: sym = hk_sym_doublelowquotemark; break;
	}
	return sym;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Call on receipt of an X11 MappingNotify event.

void hk_x11_handle_mapping_event(XMappingEvent *xmapping) {
	if (xmapping->request == MappingModifier || xmapping->request == MappingKeyboard) {
		hk_update_keymap();
	}
}

// Call on receipt of an X11 KeymapNotify event.

void hk_x11_handle_keymap_event(XKeymapEvent *xkeymap) {
	hkbd.state = 0;
	// Start from 1 - skip the first 8 (invalid) keycodes
	for (unsigned i = 1; i < 32; ++i) {
		if (xkeymap->key_vector[i] == 0) {
			continue;
		}
		for (unsigned j = 0; j < 8; ++j) {
			if ((xkeymap->key_vector[i] & (1 << j)) == 0) {
				continue;
			}
			unsigned x11_keycode = i * 8 + j;
			uint8_t code = os_scancode_to_hk_scancode[x11_keycode];
			hkbd.state |= hkbd.scancode_mod[code];
		}
	}
}

// Call on focus event.  This does a better job at syncing keyboard state than
// the default, which just releases all keys.  Note: we _only_ release keys,
// not press them here, otherwise phantom keypresses fall through to us when a
// dialog window is closed and we happen to get focus next.

_Bool hk_x11_focus_in(void) {
	if (!display || !os_scancode_to_hk_scancode) {
		return 0;
	}

	char keys[HK_NUM_SCANCODES/8];
	(void)XQueryKeymap(display, keys);
	for (unsigned i = 8; i < 256; ++i) {
		uint8_t code = os_scancode_to_hk_scancode[i];
		if (code == hk_scan_None) {
			continue;
		}
		if (!(keys[i >> 3] & (1 << (i & 7)))) {
			if (hkbd.scancode_pressed_sym[code] != hk_sym_None) {
				hk_scan_release(code);
			}
		}
	}
	return 1;
}
