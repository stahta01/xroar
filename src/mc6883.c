/** \file
 *
 *  \brief Motorola SN74LS783/MC6883 Synchronous Address Multiplexer.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
 *  Research into how SAM VDG mode transitions affect addressing and the
 *  various associated "glitches" by Stewart Orchard.
 *
 *  As the code currently stands, implementation of this undocumented behaviour
 *  is partial and you shouldn't rely on it to accurately represent real
 *  hardware.  However, if you're testing on the real thing too, this could
 *  still allow you to achieve some nice effects.
 *
 *  Currently unoptimised as whole behaviour not implemented.  In normal
 *  operation, this adds <1% to execution time.  Pathological case of
 *  constantly varying SAM VDG mode adds a little over 5%.
 */

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "delegate.h"

#include "events.h"
#include "mc6883.h"
#include "part.h"
#include "serialise.h"

// Constants for address multiplexer
// SAM Data Sheet,
//   Figure 6 - Signal routing for address multiplexer

static uint16_t const ram_row_masks[4] = { 0x7f, 0x7f, 0xff, 0xff };
static uint8_t const ram_col_shifts[4] = { 6, 7, 8, 8 };
static uint16_t const ram_col_masks[4] = { 0x3f, 0x7f, 0xff, 0xff };
static uint16_t const ram_ras1_bits[4] = { 0x1000, 0x4000, 0, 0 };

// VDG X & Y divider configurations and HSync clear mode.

enum { DIV1 = 0, DIV2, DIV3, DIV12 };
enum { CLRN = 0, CLR3, CLR4 };

static const int vdg_ydivs[8] = { DIV12, DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1 };
static const int vdg_xdivs[8] = {  DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1, DIV1 };
static const int vdg_hclrs[8] = {  CLR4, CLR3, CLR4, CLR3, CLR4, CLR3, CLR4, CLRN };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define VC_B15_5  (0)
#define VC_YDIV4  (1)
#define VC_YDIV3  (2)
#define VC_YDIV2  (3)
#define VC_B4     (4)
#define VC_XDIV3  (5)
#define VC_XDIV2  (6)
#define VC_B3_0   (7)
#define VC_GROUND (8)
#define NUM_VCOUNTERS (9)

struct vcounter {
	uint16_t value;
	_Bool input;
	_Bool output;
	uint16_t val_mod;
	uint16_t out_mask;
	int input_from;
};

static struct ser_struct ser_struct_vcounter[] = {
	SER_ID_STRUCT_ELEM(1, struct vcounter, input),
	SER_ID_STRUCT_ELEM(2, struct vcounter, value),
	SER_ID_STRUCT_ELEM(3, struct vcounter, output),
};

static const struct ser_struct_data vcounter_ser_struct_data = {
	.elems = ser_struct_vcounter,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_vcounter),
};

static const struct {
	int input_from;
	uint16_t val_mod;
	uint16_t out_mask;
} vcounter_init[NUM_VCOUNTERS] = {
	{ VC_B4,    2048, 0 },
	{ VC_YDIV3,    4, 2 },
	{ VC_B4,       3, 2 },
	{ VC_B4,       2, 1 },
	{ VC_B3_0,     2, 1 },
	{ VC_B3_0,     3, 2 },
	{ VC_B3_0,     2, 1 },
	{ -1,         16, 8 },
	{ -1,          0, 0 }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct MC6883_private {
	struct MC6883 public;

	// SAM control register
	unsigned reg;

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	// -- Registers

        // V: VDG addressing mode
        // Mode         Division        Bits cleared on HS#
        // V2 V1 V0     X   Y
        //  0  0  0     1  12           B1-B4
        //  0  0  1     3   1           B1-B3
        //  0  1  0     1   3           B1-B4
        //  0  1  1     2   1           B1-B3
        //  1  0  0     1   2           B1-B4
        //  1  0  1     1   1           B1-B3
        //  1  1  0     1   1           B1-B4
        //  1  1  1     1   1           None (DMA MODE)
	unsigned V;

	// F: VDG address offset.  Specifies bits 15 downto 9 of the video RAM
	// base address.
	uint16_t F;

	// P: Page bit.
	_Bool P;

        // R: MPU rate.
        unsigned R;

        // M: Memory type.
        unsigned M;

	// TY: Map type.  0 selects 32K RAM, 32K ROM.  1 selects 64K RAM.
	_Bool TY;

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	// -- Timing

	_Bool mpu_rate_fast;
	_Bool mpu_rate_ad;
	_Bool running_fast;
	_Bool extend_slow_cycle;

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	// -- Address multiplexer

	// Address multiplexer
	uint16_t ram_row_mask;
	uint8_t ram_col_shift;
	uint16_t ram_col_mask;
	uint16_t ram_ras1_bit;
	uint16_t ram_ras1;
	uint16_t ram_page_bit;

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	// -- VDG

	// Glitching
	//
	// Comparison of V to Vprev provides the "glitching".

	unsigned Vprev;

	// Counters, dividers

	int clr_mode;  // end of line clear mode: CLR4, CLR3 or CLRN
	struct vcounter vcounter[NUM_VCOUNTERS];

};

#define MC6883_SER_Z             (2)
#define MC6883_SER_V             (3)
#define MC6883_SER_REG           (5)
#define MC6883_SER_RAM_ROW_MASK  (7)
#define MC6883_SER_RAM_COL_SHIFT (8)
#define MC6883_SER_RAM_COL_MASK  (9)
#define MC6883_SER_RAM_RAS1_BIT  (10)
#define MC6883_SER_RAM_RAS1      (11)
#define MC6883_SER_OLD_P         (12)

static struct ser_struct ser_struct_mc6883[] = {
	// Old values translated on read:
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_Z),
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_V),
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_REG),

	// Deprecated:
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_RAM_ROW_MASK),
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_RAM_COL_SHIFT),
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_RAM_COL_MASK),
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_RAM_RAS1_BIT),
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_RAM_RAS1),
	SER_ID_STRUCT_UNHANDLED(MC6883_SER_OLD_P),

	SER_ID_STRUCT_ELEM(1, struct MC6883, S),
	SER_ID_STRUCT_ELEM(32, struct MC6883, Zrow),
	SER_ID_STRUCT_ELEM(33, struct MC6883, Zcol),
	SER_ID_STRUCT_ELEM(34, struct MC6883, Vrow),
	SER_ID_STRUCT_ELEM(35, struct MC6883, Vcol),
	SER_ID_STRUCT_ELEM(4, struct MC6883, RAS0),
	SER_ID_STRUCT_ELEM(31, struct MC6883, RAS1),

	SER_ID_STRUCT_ELEM(13, struct MC6883_private, mpu_rate_fast),
	SER_ID_STRUCT_ELEM(14, struct MC6883_private, mpu_rate_ad),
	SER_ID_STRUCT_ELEM(15, struct MC6883_private, running_fast),
	SER_ID_STRUCT_ELEM(16, struct MC6883_private, extend_slow_cycle),

	SER_ID_STRUCT_ELEM(17, struct MC6883_private, V),
	SER_ID_STRUCT_ELEM(18, struct MC6883_private, F),
	SER_ID_STRUCT_ELEM(28, struct MC6883_private, P),
	SER_ID_STRUCT_ELEM(29, struct MC6883_private, R),
	SER_ID_STRUCT_ELEM(30, struct MC6883_private, M),
	SER_ID_STRUCT_ELEM(6, struct MC6883_private, TY),

	SER_ID_STRUCT_ELEM(36, struct MC6883_private, Vprev),

	SER_ID_STRUCT_ELEM(19, struct MC6883_private, clr_mode),

	SER_ID_STRUCT_SUBSTRUCT(20, struct MC6883_private, vcounter[VC_B15_5], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(21, struct MC6883_private, vcounter[VC_B4], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(22, struct MC6883_private, vcounter[VC_B3_0], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(23, struct MC6883_private, vcounter[VC_YDIV4], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(24, struct MC6883_private, vcounter[VC_YDIV3], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(25, struct MC6883_private, vcounter[VC_YDIV2], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(26, struct MC6883_private, vcounter[VC_XDIV3], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(27, struct MC6883_private, vcounter[VC_XDIV2], &vcounter_ser_struct_data),
};

static _Bool mc6883_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool mc6883_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data mc6883_ser_struct_data = {
	.elems = ser_struct_mc6883,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6883),
	.read_elem = mc6883_read_elem,
	.write_elem = mc6883_write_elem,
};

static void update_vcounter_inputs(struct MC6883_private *sam);
static void update_from_register(struct MC6883_private *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// SAM part creation

static struct part *mc6883_allocate(void);
static _Bool mc6883_finish(struct part *p);

static const struct partdb_entry_funcs mc6883_funcs = {
	.allocate = mc6883_allocate,
	.finish = mc6883_finish,

	.ser_struct_data = &mc6883_ser_struct_data,
};

const struct partdb_entry mc6883_part = { .name = "SN74LS783", .description = "Motorola | SN74LS783/MC6883 SAM", .funcs = &mc6883_funcs };

static struct part *mc6883_allocate(void) {
	struct MC6883_private *sam = part_new(sizeof(*sam));
	struct MC6883 *samp = &sam->public;
	struct part *p = &samp->part;

	*sam = (struct MC6883_private){0};

	sam->public.cpu_cycle = DELEGATE_DEFAULT3(void, int, bool, uint16);
	sam->public.vdg_update = DELEGATE_DEFAULT0(void);

	// Set up VDG address divider sources.  Set initial Vprev=7 so that first
	// call to reset() changes them.
	sam->Vprev = 7;

	for (int i = 0; i < NUM_VCOUNTERS; i++) {
		sam->vcounter[i].input_from = vcounter_init[i].input_from;
		sam->vcounter[i].val_mod = vcounter_init[i].val_mod;
		sam->vcounter[i].out_mask = vcounter_init[i].out_mask;
	}

	return p;
}

static _Bool mc6883_finish(struct part *p) {
	struct MC6883_private *sam = (struct MC6883_private *)p;
	sam->Vprev = sam->V;
	update_vcounter_inputs(sam);
	update_from_register(sam);
	return 1;
}

static _Bool mc6883_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct MC6883_private *sam = sptr;
	switch (tag) {
	case MC6883_SER_REG:
		{
			uint16_t reg = ser_read_vuint32(sh);
			mc6883_set_register(&sam->public, reg);
		}
		break;

	case MC6883_SER_Z:
		{
			uint16_t z = ser_read_vuint32(sh);
			sam->public.Zrow = z & 0xff;
			sam->public.Zcol = (z >> 8) & 0xff;
		}
		break;

	case MC6883_SER_V:
		{
			uint16_t z = ser_read_vuint32(sh);
			sam->public.Vrow = z & 0xff;
			sam->public.Vcol = (z >> 8) & 0xff;
		}
		break;

	case MC6883_SER_RAM_ROW_MASK:
	case MC6883_SER_RAM_COL_SHIFT:
	case MC6883_SER_RAM_COL_MASK:
	case MC6883_SER_RAM_RAS1_BIT:
	case MC6883_SER_RAM_RAS1:
	case MC6883_SER_OLD_P:
		break;

	default:
		return 0;
	}
	return 1;
}

static _Bool mc6883_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct MC6883_private *sam = sptr;
	(void)sam;
	(void)sh;
	switch (tag) {
	case MC6883_SER_REG:
	case MC6883_SER_Z:
	case MC6883_SER_V:
	case MC6883_SER_RAM_ROW_MASK:
	case MC6883_SER_RAM_COL_SHIFT:
	case MC6883_SER_RAM_COL_MASK:
	case MC6883_SER_RAM_RAS1_BIT:
	case MC6883_SER_RAM_RAS1:
	case MC6883_SER_OLD_P:
		break;

	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void mc6883_reset(struct MC6883 *samp) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;

	mc6883_set_register(samp, 0);
	mc6883_vdg_fsync(samp, 1);
	sam->running_fast = 0;
	sam->extend_slow_cycle = 0;
}

#define VRAM_TRANSLATE_ROW(a) \
	( ((a) & sam->ram_row_mask) | \
	  (!((a) & sam->ram_ras1_bit) ? sam->ram_ras1 : 0) )

#define VRAM_TRANSLATE_COL(a) \
	( (((a) >> sam->ram_col_shift) & sam->ram_col_mask) | \
	  (!((a) & sam->ram_ras1_bit) ? sam->ram_ras1 : 0) )

#define RAM_TRANSLATE_ROW(a) (VRAM_TRANSLATE_ROW(a))
#define RAM_TRANSLATE_COL(a) (VRAM_TRANSLATE_COL(a) | sam->ram_page_bit)

// The primary function of the SAM: translates an address (A) plus Read/!Write
// flag (RnW) into an S value and RAM address (Z).  Writes to the SAM control
// register will update the internal configuration.  The CPU delegate is called
// with the number of (SAM) cycles elapsed, RnW flag and translated address.

static uint8_t const io_S[8] = { 4, 5, 6, 7, 7, 7, 7, 2 };
static uint8_t const data_S[8] = { 7, 7, 7, 7, 1, 2, 3, 3 };

void mc6883_mem_cycle(void *sptr, _Bool RnW, uint16_t A) {
	struct MC6883 *samp = sptr;
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	int ncycles;
	_Bool fast_cycle;
	_Bool want_register_update = 0;

	if ((A >> 8) == 0xff) {
		// I/O area
		samp->S = io_S[(A >> 5) & 7];
		samp->RAS0 = samp->RAS1 = 0;
		fast_cycle = sam->mpu_rate_fast || (samp->S != 4 && sam->mpu_rate_ad);
		if (samp->S == 7 && !RnW && A >= 0xffc0) {
			if (A <= 0xffc5 || (A >= 0xffda && A <= 0xffdd)) {
				// might affect display, so update VDG
				DELEGATE_CALL(samp->vdg_update);
			}
			switch ((A >> 1) & 0xf) {
			default:
			case 0x0: sam->V = (sam->V & ~(1 <<  0)) | ((A & 0x1) <<  0); break;
			case 0x1: sam->V = (sam->V & ~(1 <<  1)) | ((A & 0x1) <<  1); break;
			case 0x2: sam->V = (sam->V & ~(1 <<  2)) | ((A & 0x1) <<  2); break;
			case 0x3: sam->F = (sam->F & ~(1 <<  9)) | ((A & 0x1) <<  9); break;
			case 0x4: sam->F = (sam->F & ~(1 << 10)) | ((A & 0x1) << 10); break;
			case 0x5: sam->F = (sam->F & ~(1 << 11)) | ((A & 0x1) << 11); break;
			case 0x6: sam->F = (sam->F & ~(1 << 12)) | ((A & 0x1) << 12); break;
			case 0x7: sam->F = (sam->F & ~(1 << 13)) | ((A & 0x1) << 13); break;
			case 0x8: sam->F = (sam->F & ~(1 << 14)) | ((A & 0x1) << 14); break;
			case 0x9: sam->F = (sam->F & ~(1 << 15)) | ((A & 0x1) << 15); break;
			case 0xa: sam->P = A & 0x1; break;
			case 0xb: sam->R = (sam->R & ~(1 <<  0)) | ((A & 0x1) <<  0); break;
			case 0xc: sam->R = (sam->R & ~(1 <<  1)) | ((A & 0x1) <<  1); break;
			case 0xd: sam->M = (sam->M & ~(1 <<  0)) | ((A & 0x1) <<  0); break;
			case 0xe: sam->M = (sam->M & ~(1 <<  1)) | ((A & 0x1) <<  1); break;
			case 0xf: sam->TY = A & 0x1; break;
			}
			want_register_update = 1;
		}
	} else if ((A & 0x8000) && !sam->TY) {
		samp->S = data_S[A >> 13];
		samp->RAS0 = samp->RAS1 = 0;
		fast_cycle = sam->mpu_rate_fast || sam->mpu_rate_ad;
	} else {
		samp->S = RnW ? 0 : data_S[A >> 13];
		samp->RAS1 = A & sam->ram_ras1_bit;
		samp->RAS0 = !samp->RAS1;
		samp->Zrow = RAM_TRANSLATE_ROW(A);
		samp->Zcol = RAM_TRANSLATE_COL(A);
		fast_cycle = sam->mpu_rate_fast;
	}

	if (!sam->running_fast) {
		// Last cycle was slow
		if (!fast_cycle) {
			// Slow cycle
			ncycles = EVENT_TICKS_14M31818(16);
		} else {
			// Transition slow to fast
			ncycles = EVENT_TICKS_14M31818(15);
			sam->running_fast = 1;
		}
	} else {
		// Last cycle was fast
		if (!fast_cycle) {
			// Transition fast to slow
			if (!sam->extend_slow_cycle) {
				// Still interleaved
				ncycles = EVENT_TICKS_14M31818(17);
			} else {
				// Re-interleave
				ncycles = EVENT_TICKS_14M31818(25);
				sam->extend_slow_cycle = 0;
			}
			sam->running_fast = 0;
		} else {
			// Fast cycle, may become un-interleaved
			ncycles = EVENT_TICKS_14M31818(8);
			sam->extend_slow_cycle = !sam->extend_slow_cycle;
		}
	}

	DELEGATE_CALL(samp->cpu_cycle, ncycles, RnW, A);

	if (want_register_update) {
		update_from_register(sam);
	}

}

// Just the address decode from mc6883_mem_cycle().  Used to verify that a
// breakpoint refers to ROM.

unsigned mc6883_decode(struct MC6883 *samp, _Bool RnW, uint16_t A) {
	const struct MC6883_private *sam = (struct MC6883_private *)samp;
	if ((A >> 8) == 0xff) {
		// I/O area
		return io_S[(A >> 5) & 7];
	} else if ((A & 0x8000) && !sam->TY) {
		return data_S[A >> 13];
	}
	return RnW ? 0 : data_S[A >> 13];
}

static void vcounter_set(struct MC6883_private *sam, int i, int val);

static void vcounter_update(struct MC6883_private *sam, int i) {
	_Bool old_input = sam->vcounter[i].input;
	_Bool new_input = sam->vcounter[sam->vcounter[i].input_from].output;
	if (new_input != old_input) {
		sam->vcounter[i].input = new_input;
		if (!new_input) {
			vcounter_set(sam, i, (sam->vcounter[i].value + 1) % sam->vcounter[i].val_mod);
		}
	}
}

static void vcounter_set(struct MC6883_private *sam, int i, int val) {
	sam->vcounter[i].value = val;
	sam->vcounter[i].output = val & sam->vcounter[i].out_mask;
	// Never need to check VC_GROUND or VC_B3_0
	for (int j = 0; j < NUM_VCOUNTERS - 2; j++) {
		if (sam->vcounter[j].input_from == i)
			vcounter_update(sam, j);
	}
}

void mc6883_vdg_hsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (level)
		return;

	switch (sam->clr_mode) {

	case CLR4:
		// clear bits 4..1
		sam->vcounter[VC_B3_0].value = 0;
		sam->vcounter[VC_B3_0].output = 0;
		sam->vcounter[VC_XDIV3].input = 0;
		sam->vcounter[VC_XDIV2].input = 0;
		sam->vcounter[VC_B4].input = 0;
		sam->vcounter[VC_B4].value = 0;
		sam->vcounter[VC_B4].output = 0;
		vcounter_update(sam, VC_YDIV2);
		vcounter_update(sam, VC_YDIV3);
		vcounter_update(sam, VC_YDIV4);
		vcounter_update(sam, VC_B15_5);
		break;

	case CLR3:
		// clear bits 3..1
		sam->vcounter[VC_B3_0].value = 0;
		sam->vcounter[VC_B3_0].output = 0;
		vcounter_update(sam, VC_XDIV2);
		vcounter_update(sam, VC_XDIV3);
		vcounter_update(sam, VC_B4);
		break;

	default:
		break;
	}

}

static inline void vcounter_reset(struct MC6883_private *sam, int i) {
	sam->vcounter[i].input = 0;
	sam->vcounter[i].value = 0;
	sam->vcounter[i].output = 0;
}

void mc6883_vdg_fsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (!level) {
		return;
	}
	vcounter_reset(sam, VC_B3_0);
	vcounter_reset(sam, VC_XDIV2);
	vcounter_reset(sam, VC_XDIV3);
	vcounter_reset(sam, VC_B4);
	vcounter_reset(sam, VC_YDIV2);
	vcounter_reset(sam, VC_YDIV3);
	vcounter_reset(sam, VC_YDIV4);
	vcounter_reset(sam, VC_B15_5);
	sam->vcounter[VC_B15_5].value = sam->F >> 5;
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

int mc6883_vdg_bytes(struct MC6883 *samp, int nbytes) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;

	// In fast mode, there's no time to latch video RAM, so just point at
	// whatever was being access by the CPU.  This won't be terribly
	// accurate, as this function is called a lot less frequently than the
	// CPU address changes.
	uint16_t b3_0 = sam->vcounter[VC_B3_0].value;
	uint16_t V = (sam->vcounter[VC_B15_5].value << 5) | (sam->vcounter[VC_B4].value << 4) | b3_0;
	if (sam->mpu_rate_fast) {
		samp->Vrow = samp->Zrow;
		samp->Vcol = samp->Zcol;
	} else {
		samp->Vrow = VRAM_TRANSLATE_ROW(V);
		samp->Vcol = VRAM_TRANSLATE_COL(V);
	}

	// Either way, need to advance the VDG address pointer.

	// Simple case is where nbytes takes us to below the next 16-byte
	// boundary.  Need to record any rising edge of bit 3 (as input to X
	// divisor), but it will never fall here, so don't need to check for
	// that.
	if ((b3_0 + nbytes) < 16) {
		vcounter_set(sam, VC_B3_0, b3_0 + nbytes);
		return nbytes;
	}

	// Otherwise we have reached the boundary.  Bit 3 will always provide a
	// falling edge to the X divider, so work through how that affects
	// subsequent address bits.
	nbytes = 16 - b3_0;
	vcounter_set(sam, VC_B3_0, 15);  // in case rising edge of b3 was skipped
	vcounter_set(sam, VC_B3_0, 0);  // falling edge of b3
	return nbytes;
}

void mc6883_set_register(struct MC6883 *samp, unsigned int value) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	sam->V = value & 7;
	sam->F = (value << 6) & 0xfe00;
	sam->P = (value >> 10) & 1;
	sam->R = (value >> 11) & 3;
	sam->M = (value >> 13) & 3;
	sam->TY = (value >> 15) & 1;
	update_from_register(sam);
}

unsigned int mc6883_get_register(struct MC6883 *samp) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	unsigned value = sam->V & 7;
	value |= (sam->F & 0xfe00) >> 6;
	value |= (sam->P ? 0x0400 : 0);
	value |= (sam->R << 11);
	value |= (sam->M << 13);
	value |= (sam->TY ? 0x8000 : 0);
	return value;
}

static void update_vcounter_inputs(struct MC6883_private *sam) {
	switch (vdg_ydivs[sam->V]) {
	case DIV12:
		sam->vcounter[VC_B15_5].input_from = VC_YDIV4;
		break;
	case DIV3:
		sam->vcounter[VC_B15_5].input_from = VC_YDIV3;
		break;
	case DIV2:
		sam->vcounter[VC_B15_5].input_from = VC_YDIV2;
		break;
	case DIV1: default:
		sam->vcounter[VC_B15_5].input_from = VC_B4;
		break;
	}
	switch (vdg_xdivs[sam->V]) {
	case DIV3:
		sam->vcounter[VC_B4].input_from = VC_XDIV3;
		break;
	case DIV2:
		sam->vcounter[VC_B4].input_from = VC_XDIV2;
		break;
	case DIV1: default:
		sam->vcounter[VC_B4].input_from = VC_B3_0;
		break;
	}
}

static void update_from_register(struct MC6883_private *sam) {
	int old_ydiv = vdg_ydivs[sam->Vprev];
	int old_xdiv = vdg_xdivs[sam->Vprev];

	int new_ydiv = vdg_ydivs[sam->V];
	int new_xdiv = vdg_xdivs[sam->V];
	sam->clr_mode = vdg_hclrs[sam->V];

	sam->Vprev = sam->V;

	if (new_ydiv != old_ydiv) {
		switch (new_ydiv) {
		case DIV12:
			if (old_ydiv == DIV3) {
				// 'glitch'
				sam->vcounter[VC_B15_5].input_from = VC_GROUND;
				vcounter_update(sam, VC_B15_5);
			} else if (old_ydiv == DIV2) {
				// 'glitch'
				sam->vcounter[VC_B15_5].input_from = VC_B4;
				vcounter_update(sam, VC_B15_5);
			}
			sam->vcounter[VC_B15_5].input_from = VC_YDIV4;
			break;
		case DIV3:
			if (old_ydiv == DIV12) {
				// 'glitch'
				sam->vcounter[VC_B15_5].input_from = VC_GROUND;
				vcounter_update(sam, VC_B15_5);
			}
			sam->vcounter[VC_B15_5].input_from = VC_YDIV3;
			break;
		case DIV2:
			if (old_ydiv == DIV12) {
				// 'glitch'
				sam->vcounter[VC_B15_5].input_from = VC_B4;
				vcounter_update(sam, VC_B15_5);
			}
			sam->vcounter[VC_B15_5].input_from = VC_YDIV2;
			break;
		case DIV1: default:
			sam->vcounter[VC_B15_5].input_from = VC_B4;
			break;
		}
		vcounter_update(sam, VC_YDIV2);
		vcounter_update(sam, VC_YDIV3);
		vcounter_update(sam, VC_YDIV4);
		vcounter_update(sam, VC_B15_5);
	}

	if (new_xdiv != old_xdiv) {
		switch (new_xdiv) {
		case DIV3:
			if (old_xdiv == DIV2) {
				// 'glitch'
				sam->vcounter[VC_B4].input_from = VC_GROUND;
				vcounter_update(sam, VC_B4);
			}
			sam->vcounter[VC_B4].input_from = VC_XDIV3;
			break;
		case DIV2:
			if (old_xdiv == DIV3) {
				// 'glitch'
				sam->vcounter[VC_B4].input_from = VC_GROUND;
				vcounter_update(sam, VC_B4);
			}
			sam->vcounter[VC_B4].input_from = VC_XDIV2;
			break;
		case DIV1: default:
			sam->vcounter[VC_B4].input_from = VC_B3_0;
			break;
		}
		vcounter_update(sam, VC_XDIV2);
		vcounter_update(sam, VC_XDIV3);
		vcounter_update(sam, VC_B4);
	}

	sam->ram_row_mask = ram_row_masks[sam->M];
	sam->ram_col_shift = ram_col_shifts[sam->M];
	sam->ram_col_mask = ram_col_masks[sam->M];
	sam->ram_ras1_bit = ram_ras1_bits[sam->M];
	switch (sam->M) {
	case 0: // 4K
	case 1: // 16K
		sam->ram_page_bit = 0;
		sam->ram_ras1 = 0x80;
		break;
	default:
	case 2:
	case 3: // 64K
		sam->ram_page_bit = sam->P ? 0x80 : 0;
		sam->ram_ras1 = 0;
		break;
	}

	sam->mpu_rate_fast = sam->R & 0x2;
	// XXX it isn't as simple as this
	sam->mpu_rate_ad = !sam->TY && (sam->R & 0x1);
}
