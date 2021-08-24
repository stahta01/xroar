/** \file
 *
 *  \brief TCC1014 (GIME) support.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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
 *  Tandy CoCo 3 support is UNFINISHED and UNSUPPORTED, and much is KNOWN NOT
 *  TO WORK.  Please do not use except for testing.
 */

#ifndef XROAR_TCC1014_TCC1014_H
#define XROAR_TCC1014_TCC1014_H

#include <stdint.h>

#include "delegate.h"

// Horizontal timing, all measured in pixels

#define TCC1014_tFP   (34)  // 28
#define TCC1014_tWHS  (64)  // 70
#define TCC1014_tBP   (70)
#define TCC1014_tHBNK (TCC1014_tFP + TCC1014_tWHS + TCC1014_tBP)
#define TCC1014_tLB   (120)  // 118
#define TCC1014_tAV   (512)
#define TCC1014_tRB   (112)
#define TCC1014_tAVB  (TCC1014_tLB + TCC1014_tAV + TCC1014_tRB)
#define TCC1014_tHST  (TCC1014_tHBNK + TCC1014_tAVB)
// tHCD = time from start of back porch to beginning of colour burst
#define TCC1014_tHCD  (14)
// tCB = duration of colour burst
#define TCC1014_tCB   (42)

/* All horizontal timings shall remain relative to the HS pulse falling edge */
#define TCC1014_HS_FALLING_EDGE    (0)
#define TCC1014_HS_RISING_EDGE     (TCC1014_HS_FALLING_EDGE + TCC1014_tWHS)
#define TCC1014_LEFT_BORDER_START  (TCC1014_HS_FALLING_EDGE + TCC1014_tWHS + TCC1014_tBP)
#define TCC1014_ACTIVE_LINE_START  (TCC1014_LEFT_BORDER_START + TCC1014_tLB)
#define TCC1014_RIGHT_BORDER_START (TCC1014_ACTIVE_LINE_START + TCC1014_tAV)
#define TCC1014_RIGHT_BORDER_END   (TCC1014_RIGHT_BORDER_START + TCC1014_tRB)
#define TCC1014_LINE_DURATION      (TCC1014_tHBNK + TCC1014_tAVB)
#define TCC1014_PAL_PADDING_LINE   TCC1014_LINE_DURATION

#define TCC1014_VBLANK_START       (0)
#define TCC1014_TOP_BORDER_START   (TCC1014_VBLANK_START + 13)
#define TCC1014_ACTIVE_AREA_START  (TCC1014_TOP_BORDER_START + 25)
#define TCC1014_ACTIVE_AREA_END    (TCC1014_ACTIVE_AREA_START + 192)
#define TCC1014_BOTTOM_BORDER_END  (TCC1014_ACTIVE_AREA_END + 26)
#define TCC1014_VRETRACE_END       (TCC1014_BOTTOM_BORDER_END + 6)
#define TCC1014_FRAME_DURATION     (262)

/* Basic colours the VDG can generate */
/* XXX GIME is more complex than this! */

enum tcc1014_colour {
	TCC1014_GREEN, TCC1014_YELLOW, TCC1014_BLUE, TCC1014_RED,
	TCC1014_WHITE, TCC1014_CYAN, TCC1014_MAGENTA, TCC1014_ORANGE,
	TCC1014_BLACK, TCC1014_DARK_GREEN, TCC1014_DARK_ORANGE, TCC1014_BRIGHT_ORANGE
};

struct TCC1014 {
	struct part part;

	unsigned S;
	uint32_t Z;
	_Bool RAS;

	_Bool FIRQ;
	_Bool IRQ;

	_Bool IL0, IL1, IL2;

	uint8_t *CPUD;

	// Delegates to notify on signal edges.
	DELEGATE_T1(void, bool) signal_hs;
	DELEGATE_T1(void, bool) signal_fs;

	DELEGATE_T3(void, int, bool, uint16) cpu_cycle;
	DELEGATE_T1(uint8, uint32) fetch_vram;

	// Render line
	DELEGATE_T2(void, uint8p, unsigned) render_line;
};

/* Fetched data is a buffer of uint16_t, with bits:
 *
 *     10   ¬INT/EXT
 *      9   ¬A/S
 *      8   INV
 *  7...0   DD7..DD0
 */

/** \brief Create a new TCC1014 (GIME) part.
 *
 * \param type    one of VDG_GIME_1986 or VDG_GIME_1987.
 */

struct TCC1014 *tcc1014_new(int type);

void tcc1014_reset(struct TCC1014 *gimep);
void tcc1014_mem_cycle(void *sptr, _Bool RnW, uint16_t A);

void tcc1014_set_sam_register(struct TCC1014 *gimep, unsigned val);

void tcc1014_set_palette(struct TCC1014 *gimep, const struct ntsc_palette *np);
void tcc1014_set_inverted_text(struct TCC1014 *gimep, _Bool);

#endif
