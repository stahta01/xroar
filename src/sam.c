/*

Motorola SN74LS783/MC6883 Synchronous Address Multiplexer

Copyright 2003-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>

#include "delegate.h"
#include "xalloc.h"

#include "part.h"
#include "sam.h"

// Constants for address multiplexer
// SAM Data Sheet,
//   Figure 6 - Signal routing for address multiplexer

// Research into how SAM VDG mode transitions affect addressing and the various
// associated "glitches" by Stewart Orchard.
//
// As the code currently stands, implementation of this undocumented behaviour
// is partial and you shouldn't rely on it to accurately represent real
// hardware.  However, if you're testing on the real thing too, this could
// still allow you to achieve some nice effects.
//
// Currently unoptimised as whole behaviour not implemented.  In normal
// operation, this adds <1% to execution time.  Pathological case of constantly
// varying SAM VDG mode adds a little over 5%.

static uint16_t const ram_row_masks[4] = { 0x007f, 0x007f, 0x00ff, 0x00ff };
static int const ram_col_shifts[4] = { 2, 1, 0, 0 };
static uint16_t const ram_col_masks[4] = { 0x3f00, 0x7f00, 0xff00, 0xff00 };
static uint16_t const ram_ras1_bits[4] = { 0x1000, 0x4000, 0, 0 };

// VDG X & Y divider configurations and HSync clear mode.

enum { DIV1 = 0, DIV2, DIV3, DIV12 };
enum { CLRN = 0, CLR3, CLR4 };

static const int vdg_ydivs[8] = { DIV12, DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1 };
static const int vdg_xdivs[8] = {  DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1, DIV1 };
static const int vdg_hclrs[8] = {  CLR4, CLR3, CLR4, CLR3, CLR4, CLR3, CLR4, CLRN };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct vcounter {
	_Bool input;
	uint16_t value;
	_Bool output;
	struct vcounter *input_from;
};

struct MC6883_private {

	struct MC6883 public;

	// SAM control register
	uint_fast16_t reg;

	// Address decode
	_Bool map_type_1;

	// Address multiplexer
	uint16_t ram_row_mask;
	int ram_col_shift;
	uint16_t ram_col_mask;
	uint16_t ram_ras1_bit;
	uint16_t ram_ras1;
	uint16_t ram_page_bit;

	// MPU rate
	_Bool mpu_rate_fast;
	_Bool mpu_rate_ad;
	_Bool running_fast;
	_Bool extend_slow_cycle;

	struct {
		unsigned v;  // video mode
		uint16_t f;  // VDG address bits 15..9 latched on FSync

		// end of line clear mode
		int clr_mode;  // CLR4, CLR3 or CLRN

		struct vcounter b15_5;
		struct vcounter b4;
		struct vcounter b3_0;
		struct vcounter ydiv4;
		struct vcounter ydiv3;
		struct vcounter ydiv2;
		struct vcounter xdiv3;
		struct vcounter xdiv2;
	} vdg;

};

static const struct vcounter ground = { .output = 0 };

static void update_from_register(struct MC6883_private *);

struct MC6883 *sam_new(void) {
	struct MC6883_private *sam = part_new(sizeof(*sam));
	*sam = (struct MC6883_private){0};
	part_init((struct part *)sam, "SN74LS783");
	sam->public.cpu_cycle = DELEGATE_DEFAULT3(void, int, bool, uint16);

	// Set up VDG address divider sources.  Set initial V=7 so that first
	// call to reset() changes them.
	sam->vdg.v = 7;
	sam->vdg.b15_5.input_from = &sam->vdg.b4;
	sam->vdg.ydiv4.input_from = &sam->vdg.ydiv3;
	sam->vdg.ydiv3.input_from = &sam->vdg.b4;
	sam->vdg.ydiv2.input_from = &sam->vdg.b4;
	sam->vdg.b4.input_from = &sam->vdg.b3_0;
	sam->vdg.xdiv3.input_from = &sam->vdg.b3_0;
	sam->vdg.xdiv2.input_from = &sam->vdg.b3_0;

	return (struct MC6883 *)sam;
}

void sam_reset(struct MC6883 *samp) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;

	sam_set_register(samp, 0);
	sam_vdg_fsync(samp, 1);
	sam->running_fast = 0;
	sam->extend_slow_cycle = 0;
}

#define VRAM_TRANSLATE(a) ( \
		((a << sam->ram_col_shift) & sam->ram_col_mask) \
		| (a & sam->ram_row_mask) \
		| (!(a & sam->ram_ras1_bit) ? sam->ram_ras1 : 0) \
	)

#define RAM_TRANSLATE(a) (VRAM_TRANSLATE(a) | sam->ram_page_bit)

// The primary function of the SAM: translates an address (A) plus Read/!Write
// flag (RnW) into an S value and RAM address (Z).  Writes to the SAM control
// register will update the internal configuration.  The CPU delegate is called
// with the number of (SAM) cycles elapsed, RnW flag and translated address.

static unsigned const io_S[8] = { 4, 5, 6, 7, 7, 7, 7, 2 };
static unsigned const data_S[8] = { 7, 7, 7, 7, 1, 2, 3, 3 };

void sam_mem_cycle(void *sptr, _Bool RnW, uint16_t A) {
	struct MC6883 *samp = sptr;
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	int ncycles;
	_Bool fast_cycle;
	_Bool want_register_update = 0;

	if ((A >> 8) == 0xff) {
		// I/O area
		samp->S = io_S[(A >> 5) & 7];
		samp->RAS = 0;
		fast_cycle = sam->mpu_rate_fast || (samp->S != 4 && sam->mpu_rate_ad);
		if (samp->S == 7 && !RnW && A >= 0xffc0) {
			unsigned b = 1 << ((A >> 1) & 0x0f);
			if (A & 1) {
				sam->reg |= b;
			} else {
				sam->reg &= ~b;
			}
			want_register_update = 1;
		}
	} else if ((A & 0x8000) && !sam->map_type_1) {
		samp->S = data_S[A >> 13];
		samp->RAS = 0;
		fast_cycle = sam->mpu_rate_fast || sam->mpu_rate_ad;
	} else {
		samp->S = RnW ? 0 : data_S[A >> 13];
		samp->RAS = 1;
		samp->Z = RAM_TRANSLATE(A);
		fast_cycle = sam->mpu_rate_fast;
	}

	if (!sam->running_fast) {
		// Last cycle was slow
		if (!fast_cycle) {
			// Slow cycle
			ncycles = EVENT_SAM_CYCLES(16);
		} else {
			// Transition slow to fast
			ncycles = EVENT_SAM_CYCLES(15);
			sam->running_fast = 1;
		}
	} else {
		// Last cycle was fast
		if (!fast_cycle) {
			// Transition fast to slow
			if (!sam->extend_slow_cycle) {
				// Still interleaved
				ncycles = EVENT_SAM_CYCLES(17);
			} else {
				// Re-interleave
				ncycles = EVENT_SAM_CYCLES(25);
				sam->extend_slow_cycle = 0;
			}
			sam->running_fast = 0;
		} else {
			// Fast cycle, may become un-interleaved
			ncycles = EVENT_SAM_CYCLES(8);
			sam->extend_slow_cycle = !sam->extend_slow_cycle;
		}
	}

	DELEGATE_CALL3(samp->cpu_cycle, ncycles, RnW, A);

	if (want_register_update) {
		update_from_register(sam);
	}

}

static void vdg_set_b3_0(struct MC6883_private *sam, uint16_t b3_0);

static void update_b15_5_input(struct MC6883_private *sam) {
	_Bool old_input = sam->vdg.b15_5.input;
	_Bool new_input = sam->vdg.b15_5.input_from->output;
	if (new_input != old_input) {
		sam->vdg.b15_5.input = new_input;
		if (!new_input) {
			sam->vdg.b15_5.value += 0x20;
			// output from this counter irrelevant
		}
	}
}

static void update_ydiv4_input(struct MC6883_private *sam) {
	_Bool old_input = sam->vdg.ydiv4.input;
	_Bool new_input = sam->vdg.ydiv4.input_from->output;
	if (new_input != old_input) {
		sam->vdg.ydiv4.input = new_input;
		if (!new_input) {
			sam->vdg.ydiv4.value = (sam->vdg.ydiv4.value + 1) % 4;
			sam->vdg.ydiv4.output = (sam->vdg.ydiv4.value & 2);
			update_b15_5_input(sam);
		}
	}
}

static void update_ydiv3_input(struct MC6883_private *sam) {
	_Bool old_input = sam->vdg.ydiv3.input;
	_Bool new_input = sam->vdg.ydiv3.input_from->output;
	if (new_input != old_input) {
		sam->vdg.ydiv3.input = new_input;
		if (!new_input) {
			sam->vdg.ydiv3.value = (sam->vdg.ydiv3.value + 1) % 3;
			sam->vdg.ydiv3.output = (sam->vdg.ydiv3.value & 2);
			update_ydiv4_input(sam);
			update_b15_5_input(sam);
		}
	}
}

static void update_ydiv2_input(struct MC6883_private *sam) {
	_Bool old_input = sam->vdg.ydiv2.input;
	_Bool new_input = sam->vdg.ydiv2.input_from->output;
	if (new_input != old_input) {
		sam->vdg.ydiv2.input = new_input;
		if (!new_input) {
			sam->vdg.ydiv2.value = !sam->vdg.ydiv2.value;
			sam->vdg.ydiv2.output = sam->vdg.ydiv2.value;
			update_b15_5_input(sam);
		}
	}
}

static void update_b4_input(struct MC6883_private *sam) {
	_Bool old_input = sam->vdg.b4.input;
	_Bool new_input = sam->vdg.b4.input_from->output;
	if (new_input != old_input) {
		sam->vdg.b4.input = new_input;
		if (!new_input) {
			sam->vdg.b4.value ^= 0x10;
			sam->vdg.b4.output = sam->vdg.b4.value;
			update_ydiv2_input(sam);
			update_ydiv3_input(sam);
			update_ydiv4_input(sam);
			update_b15_5_input(sam);
		}
	}
}

static void update_xdiv3_input(struct MC6883_private *sam) {
	_Bool old_input = sam->vdg.xdiv3.input;
	_Bool new_input = sam->vdg.xdiv3.input_from->output;
	if (new_input != old_input) {
		sam->vdg.xdiv3.input = new_input;
		if (!new_input) {
			sam->vdg.xdiv3.value = (sam->vdg.xdiv3.value + 1) % 3;
			sam->vdg.xdiv3.output = (sam->vdg.xdiv3.value & 2);
			update_b4_input(sam);
		}
	}
}

static void update_xdiv2_input(struct MC6883_private *sam) {
	_Bool old_input = sam->vdg.xdiv2.input;
	_Bool new_input = sam->vdg.xdiv2.input_from->output;
	if (new_input != old_input) {
		sam->vdg.xdiv2.input = new_input;
		if (!new_input) {
			sam->vdg.xdiv2.value = !sam->vdg.xdiv2.value;
			sam->vdg.xdiv2.output = sam->vdg.xdiv2.value;
			update_b4_input(sam);
		}
	}
}

static void vdg_set_b3_0(struct MC6883_private *sam, uint16_t b3_0) {
	sam->vdg.b3_0.value = b3_0 & 15;
	_Bool new_output = b3_0 & 8;
	if (new_output != sam->vdg.b3_0.output) {
		sam->vdg.b3_0.output = new_output;
		update_xdiv2_input(sam);
		update_xdiv3_input(sam);
		update_b4_input(sam);
	}
}

void sam_vdg_hsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (level)
		return;

	switch (sam->vdg.clr_mode) {

	case CLR4:
		// clear bits 4..1
		sam->vdg.b3_0.value = 0;
		sam->vdg.b3_0.output = 0;
		sam->vdg.xdiv3.input = 0;
		sam->vdg.xdiv2.input = 0;
		sam->vdg.b4.input = 0;
		sam->vdg.b4.value = 0;
		sam->vdg.b4.output = 0;
		update_ydiv2_input(sam);
		update_ydiv3_input(sam);
		update_ydiv4_input(sam);
		update_b15_5_input(sam);
		break;

	case CLR3:
		// clear bits 3..1
		sam->vdg.b3_0.value = 0;
		sam->vdg.b3_0.output = 0;
		update_xdiv2_input(sam);
		update_xdiv3_input(sam);
		update_b4_input(sam);
		break;

	default:
		break;
	}

}

static inline void vcounter_reset(struct vcounter *vc) {
	vc->input = 0;
	vc->value = 0;
	vc->output = 0;
}

void sam_vdg_fsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (!level) {
		return;
	}
	vcounter_reset(&sam->vdg.b3_0);
	vcounter_reset(&sam->vdg.xdiv2);
	vcounter_reset(&sam->vdg.xdiv3);
	vcounter_reset(&sam->vdg.b4);
	vcounter_reset(&sam->vdg.ydiv2);
	vcounter_reset(&sam->vdg.ydiv3);
	vcounter_reset(&sam->vdg.ydiv4);
	vcounter_reset(&sam->vdg.b15_5);
	sam->vdg.b15_5.value = sam->vdg.f;
}

// Called with the number of bytes of video data required.  Any one call will
// provide data up to a limit of the next 16-byte boundary, meaning multiple
// calls may be required.  Updates V to the translated base address of the
// available data, and returns the number of bytes available there.
//
// When the 16-byte boundary is reached, there is a falling edge on the input
// to the X divider (bit 3 transitions from 1 to 0), which may affect its
// output, thus advancing bit 4.  This in turn alters the input to the Y
// divider.

int sam_vdg_bytes(struct MC6883 *samp, int nbytes) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;

	// In fast mode, there's no time to latch video RAM, so just point at
	// whatever was being access by the CPU.  This won't be terribly
	// accurate, as this function is called a lot less frequently than the
	// CPU address changes.
	uint16_t V = sam->vdg.b15_5.value | sam->vdg.b4.value | sam->vdg.b3_0.value;
	samp->V = sam->mpu_rate_fast ? samp->Z : VRAM_TRANSLATE(V);

	// Either way, need to advance the VDG address pointer.

	// Simple case is where nbytes takes us to below the next 16-byte
	// boundary.  Need to record any rising edge of bit 3 (as input to X
	// divisor), but it will never fall here, so don't need to check for
	// that.
	if ((sam->vdg.b3_0.value + nbytes) < 16) {
		vdg_set_b3_0(sam, sam->vdg.b3_0.value + nbytes);
		return nbytes;
	}

	// Otherwise we have reached the boundary.  Bit 3 will always provide a
	// falling edge to the X divider, so work through how that affects
	// subsequent address bits.
	nbytes = 16 - sam->vdg.b3_0.value;
	vdg_set_b3_0(sam, 15);  // in case rising edge of b3 was skipped
	vdg_set_b3_0(sam, 0);   // falling edge of b3
	return nbytes;
}

void sam_set_register(struct MC6883 *samp, unsigned int value) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	sam->reg = value;
	update_from_register(sam);
}

unsigned int sam_get_register(struct MC6883 *samp) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	return sam->reg;
}

static void update_from_register(struct MC6883_private *sam) {
	int old_v = sam->vdg.v;

	int old_ydiv = vdg_ydivs[old_v];
	int old_xdiv = vdg_xdivs[old_v];

	int new_v = sam->reg & 7;

	int new_ydiv = vdg_ydivs[new_v];
	int new_xdiv = vdg_xdivs[new_v];
	int new_hclr = vdg_hclrs[new_v];

	sam->vdg.v = sam->reg & 7;
	sam->vdg.f = (sam->reg & 0x03f8) << 6;
	sam->vdg.clr_mode = new_hclr;

	if (new_ydiv != old_ydiv) {
		switch (new_ydiv) {
		case DIV12:
			if (old_ydiv == DIV3) {
				// 'glitch'
				sam->vdg.b15_5.input_from = (struct vcounter *)&ground;
				update_b15_5_input(sam);
			} else if (old_ydiv == DIV2) {
				// 'glitch'
				sam->vdg.b15_5.input_from = &sam->vdg.b4;
				update_b15_5_input(sam);
			}
			sam->vdg.b15_5.input_from = &sam->vdg.ydiv4;
			break;
		case DIV3:
			if (old_ydiv == DIV12) {
				// 'glitch'
				sam->vdg.b15_5.input_from = (struct vcounter *)&ground;
				update_b15_5_input(sam);
			}
			sam->vdg.b15_5.input_from = &sam->vdg.ydiv3;
			break;
		case DIV2:
			if (old_ydiv == DIV12) {
				// 'glitch'
				sam->vdg.b15_5.input_from = &sam->vdg.b4;
				update_b15_5_input(sam);
			}
			sam->vdg.b15_5.input_from = &sam->vdg.ydiv2;
			break;
		case DIV1: default:
			sam->vdg.b15_5.input_from = &sam->vdg.b4;
			break;
		}
		update_ydiv2_input(sam);
		update_ydiv3_input(sam);
		update_ydiv4_input(sam);
		update_b15_5_input(sam);
	}

	if (new_xdiv != old_xdiv) {
		switch (new_xdiv) {
		case DIV3:
			if (old_xdiv == DIV2) {
				// 'glitch'
				sam->vdg.b4.input_from = (struct vcounter *)&ground;
				update_b4_input(sam);
			}
			sam->vdg.b4.input_from = &sam->vdg.xdiv3;
			break;
		case DIV2:
			if (old_xdiv == DIV3) {
				// 'glitch'
				sam->vdg.b4.input_from = (struct vcounter *)&ground;
				update_b4_input(sam);
			}
			sam->vdg.b4.input_from = &sam->vdg.xdiv2;
			break;
		case DIV1: default:
			sam->vdg.b4.input_from = &sam->vdg.b3_0;
			break;
		}
		update_xdiv2_input(sam);
		update_xdiv3_input(sam);
		update_b4_input(sam);
	}

	int memory_size = (sam->reg >> 13) & 3;
	sam->ram_row_mask = ram_row_masks[memory_size];
	sam->ram_col_shift = ram_col_shifts[memory_size];
	sam->ram_col_mask = ram_col_masks[memory_size];
	sam->ram_ras1_bit = ram_ras1_bits[memory_size];
	switch (memory_size) {
	case 0: // 4K
	case 1: // 16K
		sam->ram_page_bit = 0;
		sam->ram_ras1 = 0x8080;
		break;
	default:
	case 2:
	case 3: // 64K
		sam->ram_page_bit = (sam->reg & 0x0400) << 5;
		sam->ram_ras1 = 0;
		break;
	}

	sam->map_type_1 = ((sam->reg & 0x8000) != 0);
	sam->mpu_rate_fast = sam->reg & 0x1000;
	sam->mpu_rate_ad = !sam->map_type_1 && (sam->reg & 0x800);
}
