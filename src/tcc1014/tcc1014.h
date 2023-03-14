/** \file
 *
 *  \brief TCC1014 (GIME) support.
 *
 *  \copyright Copyright 2003-2023 Ciaran Anscomb
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

#ifndef XROAR_TCC1014_TCC1014_H
#define XROAR_TCC1014_TCC1014_H

#include <stdint.h>

#include "delegate.h"

// Horizontal timing, all measured in pixels

#define TCC1014_tFP   (28)
#define TCC1014_tWHS  (80)  // measured
#define TCC1014_tBP   (60)  // measured
#define TCC1014_tHBNK (TCC1014_tFP + TCC1014_tWHS + TCC1014_tBP)
#define TCC1014_tAV   (512)
#define TCC1014_tRB   (112)
#define TCC1014_tAVB  (TCC1014_tLB + TCC1014_tAV + TCC1014_tRB)
#define TCC1014_tHST  (TCC1014_tHBNK + TCC1014_tAVB)
// tHCD = time from start of back porch to beginning of colour burst
#define TCC1014_tHCD  (14)
// tCB = duration of colour burst
#define TCC1014_tCB   (40)

/* All horizontal timings shall remain relative to the HS pulse falling edge */
#define TCC1014_HS_FALLING_EDGE    (0)
#define TCC1014_HS_RISING_EDGE     (TCC1014_HS_FALLING_EDGE + TCC1014_tWHS)
#define TCC1014_LEFT_BORDER_START  (TCC1014_HS_FALLING_EDGE + TCC1014_tWHS + TCC1014_tBP)
#define TCC1014_LINE_DURATION      (912)
#define TCC1014_RIGHT_BORDER_END   (TCC1014_LINE_DURATION - TCC1014_tFP)

#define TCC1014_VBLANK_START       (0)
#define TCC1014_TOP_BORDER_START   (TCC1014_VBLANK_START + 3)

// GIME palette indices

enum tcc1014_colour {
	TCC1014_GREEN, TCC1014_YELLOW, TCC1014_BLUE, TCC1014_RED,
	TCC1014_WHITE, TCC1014_CYAN, TCC1014_MAGENTA, TCC1014_ORANGE,
	TCC1014_RGCSS0_0, TCC1014_RGCSS0_1, TCC1014_RGCSS1_0, TCC1014_RGCSS1_1,
	TCC1014_DARK_GREEN, TCC1014_BRIGHT_GREEN, TCC1014_DARK_ORANGE, TCC1014_BRIGHT_ORANGE
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
	//
	//     unsigned burst;       // burst index for this line
	//     unsigned npixels;     // no. pixels in scanline
	//     const uint8_t *data;  // palettised data, NULL for dummy line
	//
	// GIME will set 'burst' to 0 (normal burst) or 1 (inverted burst).

	DELEGATE_T3(void, unsigned, unsigned, uint8cp) render_line;
};

/* Fetched data is a buffer of uint16_t, with bits:
 *
 *     10   ¬INT/EXT
 *      9   ¬A/S
 *      8   INV
 *  7...0   DD7..DD0
 */

void tcc1014_reset(struct TCC1014 *gimep);
void tcc1014_mem_cycle(void *sptr, _Bool RnW, uint16_t A);

void tcc1014_set_sam_register(struct TCC1014 *gimep, unsigned val);

void tcc1014_set_inverted_text(struct TCC1014 *gimep, _Bool);

#endif
