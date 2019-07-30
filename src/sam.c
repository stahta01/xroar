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

#include "config.h"

#include <stdint.h>
#include <stdlib.h>

#include "delegate.h"
#include "xalloc.h"

#include "part.h"
#include "sam.h"

// Constants for address multiplexer
// SAM Data Sheet,
//   Figure 6 - Signal routing for address multiplexer

static uint16_t const ram_row_masks[4] = { 0x007f, 0x007f, 0x00ff, 0x00ff };
static int const ram_col_shifts[4] = { 2, 1, 0, 0 };
static uint16_t const ram_col_masks[4] = { 0x3f00, 0x7f00, 0xff00, 0xff00 };
static uint16_t const ram_ras1_bits[4] = { 0x1000, 0x4000, 0, 0 };

// VDG X & Y divider configurations and HSync clear mode.

#define DIV1  (0)
#define DIV2  (1)
#define DIV3  (2)
#define DIV12 (3)

#define CLR_B4_1 (0)
#define CLR_B3_1 (1)
#define CLR_NONE (2)

static int const vdg_xdiv_modes[8] = {  DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1, DIV1 };
static int const vdg_ydiv_modes[8] = { DIV12, DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1 };
static int const vdg_clr_modes[8] = {
	CLR_B4_1, CLR_B3_1, CLR_B4_1, CLR_B3_1,
	CLR_B4_1, CLR_B3_1, CLR_B4_1, CLR_NONE };

// Duty cycles for VDG X and Y dividers.  Iterator advances through the 24
// entries each time the _input_ to the divider changes state, meaning the ÷1
// passthrough can be implemented without a special case.

static int const div_duty[4][24] = {
	{ 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1 }, // ÷1 - passthrough
	{ 0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1 }, // ÷2
	{ 0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,1,1 }, // ÷3
	{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1 }, // ÷12
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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

		// these are set according to mode
		int xdiv;  // DIV1, DIV2 or DIV3
		int ydiv;  // DIV1, DIV2, DIV3 or DIV12
		int clr_mode;  // CLR_B4_1, CLR_B3_1 or CLR_NONE

		// address counters
		uint16_t b15_5;  // top 11 bits follow Ydiv
		uint16_t b4;     // bit 4 follows Xdiv
		uint16_t b3_0;   // bits 0-3

		// index into duty cycle arrays
		int xduty;
		int yduty;
	} vdg;

};

static void update_from_register(struct MC6883_private *);

struct MC6883 *sam_new(void) {
	struct MC6883_private *sam = part_new(sizeof(*sam));
	*sam = (struct MC6883_private){0};
	part_init((struct part *)sam, "SN74LS783");
	sam->public.cpu_cycle = DELEGATE_DEFAULT3(void, int, bool, uint16);
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

void sam_vdg_hsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (level)
		return;

	_Bool old_xdiv_out = div_duty[sam->vdg.xdiv][sam->vdg.xduty];
	_Bool old_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];

	switch (sam->vdg.clr_mode) {

	case CLR_B4_1:
		// clear bits 4..1
		if (sam->vdg.b3_0 & 0x8) {
			sam->vdg.xduty = (sam->vdg.xduty + 1) % 24;
		}
		sam->vdg.b3_0 &= ~0xe;
		if (sam->vdg.b4) {
			sam->vdg.yduty = (sam->vdg.yduty + 1) % 24;
			_Bool new_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];
			if (old_ydiv_out && !new_ydiv_out) {
				sam->vdg.b15_5 += 0x20;
			}
		}
		sam->vdg.b4 = 0;
		break;

	case CLR_B3_1:
		// clear bits 3..1
		if (sam->vdg.b3_0 & 0x8) {
			sam->vdg.xduty = (sam->vdg.xduty + 1) % 24;
			_Bool new_xdiv_out = div_duty[sam->vdg.xdiv][sam->vdg.xduty];
			if (old_xdiv_out && !new_xdiv_out) {
				sam->vdg.b4 ^= 0x10;
				sam->vdg.yduty = (sam->vdg.yduty + 1) % 24;
				_Bool new_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];
				if (old_ydiv_out && !new_ydiv_out) {
					sam->vdg.b15_5 += 0x20;
				}
			}
		}
		sam->vdg.b3_0 &= ~0xe;
		break;

	default:
		break;
	}

}

void sam_vdg_fsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (!level) {
		return;
	}
	sam->vdg.b15_5 = sam->vdg.f;
	sam->vdg.b4 = 0;
	sam->vdg.b3_0 = 0;
	sam->vdg.xduty = 0;
	sam->vdg.yduty = 0;
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
	uint16_t V = sam->vdg.b15_5 | sam->vdg.b4 | sam->vdg.b3_0;
	samp->V = sam->mpu_rate_fast ? samp->Z : VRAM_TRANSLATE(V);

	// Either way, need to advance the VDG address pointer.

	// Simple case is where nbytes takes us to below the next 16-byte
	// boundary.  Need to record any rising edge of bit 3 (as input to X
	// divisor), but it will never fall here, so don't need to check for
	// that.
	if ((sam->vdg.b3_0 + nbytes) < 16) {
		sam->vdg.b3_0 += nbytes;
		sam->vdg.xduty |= (sam->vdg.b3_0 >> 3);  // potential rising edge
		return nbytes;
	}

	// Otherwise we have reached the boundary.  Bit 3 will always provide a
	// falling edge to the X divider, so work through how that affects
	// subsequent address bits.
	nbytes = 16 - sam->vdg.b3_0;
	sam->vdg.b3_0 = 0;
	sam->vdg.xduty |= 1;  // in case this was skipped past
	_Bool old_xdiv_out = div_duty[sam->vdg.xdiv][sam->vdg.xduty];
	sam->vdg.xduty = (sam->vdg.xduty + 1) % 24;
	_Bool new_xdiv_out = div_duty[sam->vdg.xdiv][sam->vdg.xduty];
	if (old_xdiv_out && !new_xdiv_out) {
		sam->vdg.b4 ^= 0x10;
		_Bool old_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];
		sam->vdg.yduty = (sam->vdg.yduty + 1) % 24;
		_Bool new_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];
		if (old_ydiv_out && !new_ydiv_out) {
			sam->vdg.b15_5 += 0x20;
		}
	}
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
	_Bool old_xdiv_out = div_duty[sam->vdg.xdiv][sam->vdg.xduty];
	_Bool old_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];

	sam->vdg.v = sam->reg & 7;
	sam->vdg.f = (sam->reg & 0x03f8) << 6;
	sam->vdg.clr_mode = vdg_clr_modes[sam->vdg.v];
	sam->vdg.xdiv = vdg_xdiv_modes[sam->vdg.v];

	_Bool new_xdiv_out = div_duty[sam->vdg.xdiv][sam->vdg.xduty];
	_Bool new_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];

	if (old_xdiv_out && !new_xdiv_out) {
		sam->vdg.b4 ^= 0x10;
		sam->vdg.yduty = (sam->vdg.yduty + 1) % 24;
		new_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];
		if (old_ydiv_out && !new_ydiv_out) {
			sam->vdg.b15_5 += 0x20;
		}
		old_ydiv_out = new_ydiv_out;
	}

	sam->vdg.ydiv = vdg_ydiv_modes[sam->vdg.v];
	new_ydiv_out = div_duty[sam->vdg.ydiv][sam->vdg.yduty];
	if (old_ydiv_out && !new_ydiv_out) {
		sam->vdg.b15_5 += 0x20;
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
