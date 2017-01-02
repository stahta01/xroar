/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_VDG_H_
#define XROAR_VDG_H_

#include <stdint.h>

#include "delegate.h"

struct ntsc_palette;

// Output voltages

// Luma
#define VDG_VBLANK (0.77)
#define VDG_VBLACK (0.72)
#define VDG_VWL    (0.65)
#define VDG_VWM    (0.54)
#define VDG_VWH    (0.42)

// Chroma
#define VDG_VIH    (2.00)
#define VDG_VR     (1.50)
#define VDG_VOL    (1.00)
#define VDG_VBURST (1.25)
#define VDG_CHB    (1.50)

// Horizontal timing, all measured in quarter-VDG-clocks (i.e., half-pixels)

#define VDG_tFP   (34)  // 28
#define VDG_tWHS  (64)  // 70
#define VDG_tBP   (70)
#define VDG_tHBNK (VDG_tFP + VDG_tWHS + VDG_tBP)
#define VDG_tLB   (120)  // 118
#define VDG_tAV   (512)
#define VDG_tRB   (112)
#define VDG_tAVB  (VDG_tLB + VDG_tAV + VDG_tRB)
#define VDG_tHST  (VDG_tHBNK + VDG_tAVB)
// tHCD = time from start of back porch to beginning of colour burst
#define VDG_tHCD  (14)
// tCB = duration of colour burst
#define VDG_tCB   (42)

/* All horizontal timings shall remain relative to the HS pulse falling edge */
#define VDG_HS_FALLING_EDGE    (0)
#define VDG_HS_RISING_EDGE     (VDG_HS_FALLING_EDGE + VDG_tWHS)
#define VDG_LEFT_BORDER_START  (VDG_HS_FALLING_EDGE + VDG_tWHS + VDG_tBP)
#define VDG_ACTIVE_LINE_START  (VDG_LEFT_BORDER_START + VDG_tLB)
#define VDG_RIGHT_BORDER_START (VDG_ACTIVE_LINE_START + VDG_tAV)
#define VDG_RIGHT_BORDER_END   (VDG_RIGHT_BORDER_START + VDG_tRB)
#define VDG_LINE_DURATION      (VDG_tHBNK + VDG_tAVB)
#define VDG_PAL_PADDING_LINE   VDG_LINE_DURATION

#define VDG_VBLANK_START       (0)
#define VDG_TOP_BORDER_START   (VDG_VBLANK_START + 13)
#define VDG_ACTIVE_AREA_START  (VDG_TOP_BORDER_START + 25)
#define VDG_ACTIVE_AREA_END    (VDG_ACTIVE_AREA_START + 192)
#define VDG_BOTTOM_BORDER_END  (VDG_ACTIVE_AREA_END + 26)
#define VDG_VRETRACE_END       (VDG_BOTTOM_BORDER_END + 6)
#define VDG_FRAME_DURATION     (262)

/* Basic colours the VDG can generate */

enum vdg_colour {
	VDG_GREEN, VDG_YELLOW, VDG_BLUE, VDG_RED,
	VDG_WHITE, VDG_CYAN, VDG_MAGENTA, VDG_ORANGE,
	VDG_BLACK, VDG_DARK_GREEN, VDG_DARK_ORANGE, VDG_BRIGHT_ORANGE
};

struct MC6847 {
	// Text row (0-11). In reality, this would be external circuitry
	// clocked by HS and cleared by RP, but provided here for now.
	unsigned row;

	// Delegates to notify on signal edges.
	DELEGATE_T1(void, bool) signal_hs;
	DELEGATE_T1(void, bool) signal_fs;
	// External handler to fetch data for display. First arg is number of
	// 16-bit words, second a pointer to a buffer to receive them.
	DELEGATE_T2(void, int, uint16p) fetch_data;

	// Render line
	DELEGATE_T2(void, uint8p, unsigned) render_line;

	// Flags to affect behaviour.  Really, these should be handled by
	// machine-specific code.
	_Bool is_dragon64;
	_Bool is_dragon32;
	_Bool is_coco;
	_Bool is_pal;
};

/* Fetched data is a buffer of uint16_t, with bits:
 *
 *     10   ¬INT/EXT
 *      9   ¬A/S
 *      8   INV
 *  7...0   DD7..DD0
 */

struct MC6847 *mc6847_new(_Bool t1);
void mc6847_free(struct MC6847 *);

void mc6847_reset(struct MC6847 *);

void mc6847_set_palette(struct MC6847 *, const struct ntsc_palette *np);
void mc6847_set_inverted_text(struct MC6847 *, _Bool);

/*
 * Mode bits:
 * 7    ¬A/G
 * 6..4 GM2..GM0
 * 3    CSS
 * 2..0 ignored
 */

void mc6847_set_mode(struct MC6847 *, unsigned mode);

#endif  /* XROAR_VDG_H_ */
