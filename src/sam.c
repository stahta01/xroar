/*  Copyright 2003-2017 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>

#include "xalloc.h"

#include "delegate.h"
#include "sam.h"

/* Constants for tracking VDG address counter */
static int const vdg_mod_xdivs[8] = { 1, 3, 1, 2, 1, 1, 1, 1 };
static int const vdg_mod_ydivs[8] = { 12, 1, 3, 1, 2, 1, 1, 1 };
static int const vdg_mod_adds[8] = { 16, 8, 16, 8, 16, 8, 16, 0 };
static uint16_t const vdg_mod_clears[8] = { ~30, ~14, ~30, ~14, ~30, ~14, ~30, ~0 };

/* Constants for address multiplexer
 * SAM Data Sheet,
 *   Figure 6 - Signal routing for address multiplexer */
static uint16_t const ram_row_masks[4] = { 0x007f, 0x007f, 0x00ff, 0x00ff };
static int const ram_col_shifts[4] = { 2, 1, 0, 0 };
static uint16_t const ram_col_masks[4] = { 0x3f00, 0x7f00, 0xff00, 0xff00 };
static uint16_t const ram_ras1_bits[4] = { 0x1000, 0x4000, 0, 0 };

struct MC6883_private {

	struct MC6883 public;

	/* SAM control register */
	uint_fast16_t reg;

	/* Address decode */
	_Bool map_type_1;

	/* Address multiplexer */
	uint16_t ram_row_mask;
	int ram_col_shift;
	uint16_t ram_col_mask;
	uint16_t ram_ras1_bit;
	uint16_t ram_ras1;
	uint16_t ram_page_bit;

	/* MPU rate */
	_Bool mpu_rate_fast;
	_Bool mpu_rate_ad;
	_Bool running_fast;
	_Bool extend_slow_cycle;

	/* VDG address counter */
	uint16_t vdg_base;
	uint16_t vdg_address;
	int vdg_mod_xdiv;
	int vdg_mod_ydiv;
	int vdg_mod_add;
	uint16_t vdg_mod_clear;
	int vdg_xcount;
	int vdg_ycount;

};

static void update_from_register(struct MC6883_private *);

struct MC6883 *sam_new(void) {
	struct MC6883_private *sam = xmalloc(sizeof(*sam));
	*sam = (struct MC6883_private){.public={0}};
	sam->public.cpu_cycle = DELEGATE_DEFAULT3(void, int, bool, uint16);
	return (struct MC6883 *)sam;
}

void sam_free(struct MC6883 *samp) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	free(sam);
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

/* The primary function of the SAM: translates an address (A) plus Read/!Write
 * flag (RnW) into an S value and RAM address (Z).  Writes to the SAM control
 * register will update the configuration.  The number of (SAM) cycles the CPU
 * clock would be use for this access is written to ncycles.  Returns 1 when
 * the access is to a RAM area, 0 otherwise. */

static unsigned const io_S[8] = { 4, 5, 6, 7, 7, 7, 7, 2 };
static unsigned const rom_S[4] = { 1, 2, 3, 3 };

void sam_mem_cycle(void *sptr, _Bool RnW, uint16_t A) {
	struct MC6883 *samp = sptr;
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	int ncycles;
	_Bool fast_cycle;
	_Bool is_io = (A >> 8) == 0xff;
	_Bool is_ram = !is_io && (!(A & 0x8000) || sam->map_type_1);
	_Bool is_rom = !is_io && !is_ram;

	samp->RAS = is_ram;
	if (is_io) {
		samp->S = io_S[(A >> 5) & 7];
		fast_cycle = sam->mpu_rate_fast || (samp->S != 4 && sam->mpu_rate_ad);
		if (samp->S == 7 && !RnW && A >= 0xffc0) {
			unsigned b = 1 << ((A >> 1) & 0x0f);
			if (A & 1) {
				sam->reg |= b;
			} else {
				sam->reg &= ~b;
			}
			update_from_register(sam);
		}
	} else if (is_rom) {
		samp->S = rom_S[(A >> 13) & 3];
		fast_cycle = sam->mpu_rate_fast || (!sam->map_type_1 && sam->mpu_rate_ad);
	} else {
		samp->S = RnW ? 0 : 7;
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
}

static void vdg_address_add(struct MC6883_private *sam, int n) {
	uint16_t new_B = sam->vdg_address + n;
	if ((sam->vdg_address ^ new_B) & 0x10) {
		sam->vdg_xcount = (sam->vdg_xcount + 1) % sam->vdg_mod_xdiv;
		if (sam->vdg_xcount != 0) {
			new_B -= 0x10;
		} else {
			if ((sam->vdg_address ^ new_B) & 0x20) {
				sam->vdg_ycount = (sam->vdg_ycount + 1) % sam->vdg_mod_ydiv;
				if (sam->vdg_ycount != 0) {
					new_B -= 0x20;
				}
			}
		}
	}
	sam->vdg_address = new_B;
}

void sam_vdg_hsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (level)
		return;
	/* The top cleared bit will, if a transition to low occurs, increment
	 * the bits above it.  This dummy fetch will achieve the same effective
	 * result. */
	vdg_address_add(sam, sam->vdg_mod_add);
	sam->vdg_address &= sam->vdg_mod_clear;
}

void sam_vdg_fsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (!level)
		return;
	sam->vdg_address = sam->vdg_base;
	sam->vdg_xcount = 0;
	sam->vdg_ycount = 0;
}

/* Called with the number of bytes of video data required, this implements the
 * divide-by-X and divide-by-Y parts of the SAM video address counter.  Updates
 * 'V' to the base address of available data and returns the actual number of
 * bytes available.  As the next byte may not be sequential, continue calling
 * until all required data is fetched. */

int sam_vdg_bytes(struct MC6883 *samp, int nbytes) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	uint16_t b3_0 = sam->vdg_address & 0xf;
	samp->V = sam->mpu_rate_fast ? samp->Z : VRAM_TRANSLATE(sam->vdg_address);
	if ((b3_0 + nbytes) < 16) {
		sam->vdg_address += nbytes;
		return nbytes;
	}
	nbytes = 16 - b3_0;
	vdg_address_add(sam, nbytes);
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
	int vdg_mode = sam->reg & 7;
	sam->vdg_base = (sam->reg & 0x03f8) << 6;
	sam->vdg_mod_xdiv = vdg_mod_xdivs[vdg_mode];
	sam->vdg_mod_ydiv = vdg_mod_ydivs[vdg_mode];
	sam->vdg_mod_add = vdg_mod_adds[vdg_mode];
	sam->vdg_mod_clear = vdg_mod_clears[vdg_mode];

	int memory_size = (sam->reg >> 13) & 3;
	sam->ram_row_mask = ram_row_masks[memory_size];
	sam->ram_col_shift = ram_col_shifts[memory_size];
	sam->ram_col_mask = ram_col_masks[memory_size];
	sam->ram_ras1_bit = ram_ras1_bits[memory_size];
	switch (memory_size) {
		case 0: /* 4K */
		case 1: /* 16K */
			sam->ram_page_bit = 0;
			sam->ram_ras1 = 0x8080;
			break;
		default:
		case 2:
		case 3: /* 64K */
			sam->ram_page_bit = (sam->reg & 0x0400) << 5;
			sam->ram_ras1 = 0;
			break;
	}

	sam->map_type_1 = ((sam->reg & 0x8000) != 0);
	sam->mpu_rate_fast = sam->reg & 0x1000;
	sam->mpu_rate_ad = !sam->map_type_1 && (sam->reg & 0x800);
}
