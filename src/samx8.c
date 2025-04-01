/** \file
 *
 *  \brief SAMx8 512K SRAM expansion.
 *
 *  \copyright Copyright 2025 Ciaran Anscomb
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

#include "array.h"
#include "delegate.h"

#include "events.h"
#include "mc6883.h"
#include "part.h"
#include "serialise.h"

// VDG X & Y divider configurations and HSync clear mode.

enum { DIV1 = 0, DIV2, DIV3, DIV12 };
enum { CLRN = 0, CLR3, CLR4 };

static const int vdg_ydivs[8] = { DIV12, DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1 };
static const int vdg_xdivs[8] = {  DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1, DIV1 };
static const int vdg_hclrs[8] = {  CLR4, CLR3, CLR4, CLR3, CLR4, CLR3, CLR4, CLRN };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define VC_B18_5  (0)
#define VC_YDIV4  (1)
#define VC_YDIV3  (2)
#define VC_YDIV2  (3)
#define VC_B4     (4)
#define VC_XDIV3  (5)
#define VC_XDIV2  (6)
#define VC_B3_0   (7)
#define NUM_VCOUNTERS (8)

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
	{ VC_B4,   16384, 0 },
	{ VC_YDIV3,    4, 2 },
	{ VC_B4,       3, 2 },
	{ VC_B4,       2, 1 },
	{ VC_B3_0,     2, 1 },
	{ VC_B3_0,     3, 2 },
	{ VC_B3_0,     2, 1 },
	{ -1,         16, 8 }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct SAMx8_private {
	struct MC6883 public;

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

	// F: VDG address offset.  Specifies bits 18 downto 5 of the video RAM
	// base address.
	uint32_t F;

	// TASK: Task selection.
	_Bool TASK;

        // R: MPU rate.
        _Bool R;

	// TY: Map type.  0 selects 32K RAM, 32K ROM.  1 selects 64K RAM.
	_Bool TY;

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	// -- Timing

	_Bool mpu_rate_fast;
	_Bool running_fast;
	_Bool extend_slow_cycle;

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
	// -- VDG

	// Glitching
	//
	// Comparison of V to Vprev indicates need to mode change.

	unsigned Vprev;

	// Counters, dividers

	int clr_mode;  // end of line clear mode: CLR4, CLR3 or CLRN
	struct vcounter vcounter[NUM_VCOUNTERS];

	// SAMx8-specific stuff

	// COMMON flags
	// Bit 0: $f000--$feff, $ffe0--$ffff common
	// Bit 1: $e000--$efff common
	uint_fast8_t COMMON;

	// 2 * 4 banks of 16K
	uint8_t page_map[8];

};

static struct ser_struct ser_struct_samx8[] = {
	SER_ID_STRUCT_ELEM(1, struct MC6883, S),
	SER_ID_STRUCT_ELEM(32, struct MC6883, Zrow),
	SER_ID_STRUCT_ELEM(33, struct MC6883, Zcol),
	SER_ID_STRUCT_ELEM(34, struct MC6883, Vrow),
	SER_ID_STRUCT_ELEM(35, struct MC6883, Vcol),
	SER_ID_STRUCT_ELEM(37, struct MC6883, nWE),
	SER_ID_STRUCT_ELEM(4, struct MC6883, RAS0),
	SER_ID_STRUCT_ELEM(31, struct MC6883, RAS1),

	SER_ID_STRUCT_ELEM(13, struct SAMx8_private, mpu_rate_fast),
	SER_ID_STRUCT_ELEM(15, struct SAMx8_private, running_fast),
	SER_ID_STRUCT_ELEM(16, struct SAMx8_private, extend_slow_cycle),

	SER_ID_STRUCT_ELEM(17, struct SAMx8_private, V),
	SER_ID_STRUCT_ELEM(18, struct SAMx8_private, F),
	SER_ID_STRUCT_ELEM(29, struct SAMx8_private, R),
	SER_ID_STRUCT_ELEM(6, struct SAMx8_private, TY),

	SER_ID_STRUCT_ELEM(38, struct SAMx8_private, COMMON),

	SER_ID_STRUCT_ELEM(36, struct SAMx8_private, Vprev),

	SER_ID_STRUCT_ELEM(19, struct SAMx8_private, clr_mode),

	SER_ID_STRUCT_SUBSTRUCT(20, struct SAMx8_private, vcounter[VC_B18_5], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(21, struct SAMx8_private, vcounter[VC_B4], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(22, struct SAMx8_private, vcounter[VC_B3_0], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(23, struct SAMx8_private, vcounter[VC_YDIV4], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(24, struct SAMx8_private, vcounter[VC_YDIV3], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(25, struct SAMx8_private, vcounter[VC_YDIV2], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(26, struct SAMx8_private, vcounter[VC_XDIV3], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(27, struct SAMx8_private, vcounter[VC_XDIV2], &vcounter_ser_struct_data),
};

static const struct ser_struct_data samx8_ser_struct_data = {
	.elems = ser_struct_samx8,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_samx8),
};

static void update_vcounter_inputs(struct SAMx8_private *sam);
static void update_from_register(struct SAMx8_private *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// SAM part creation

static struct part *samx8_allocate(void);
static _Bool samx8_finish(struct part *p);
static _Bool samx8_is_a(struct part *p, const char *name);

static void samx8_reset(struct MC6883 *);
static void samx8_mem_cycle(void *, _Bool RnW, uint16_t A);
static unsigned samx8_decode(struct MC6883 *, _Bool RnW, uint16_t A);
static void samx8_vdg_hsync(struct MC6883 *, _Bool level);
static void samx8_vdg_fsync(struct MC6883 *, _Bool level);
static int samx8_vdg_bytes(struct MC6883 *, int nbytes);
static void samx8_set_register(struct MC6883 *, unsigned value);
static unsigned samx8_get_register(struct MC6883 *);

static const struct partdb_entry_funcs samx8_funcs = {
	.allocate = samx8_allocate,
	.finish = samx8_finish,

	.ser_struct_data = &samx8_ser_struct_data,

	.is_a = samx8_is_a,
};

const struct partdb_entry samx8_part = { .name = "SAMx8", .description = "Teipen Mwnci | SAMx8", .funcs = &samx8_funcs };

static struct part *samx8_allocate(void) {
	struct SAMx8_private *sam = part_new(sizeof(*sam));
	struct MC6883 *samp = &sam->public;
	struct part *p = &samp->part;

	*sam = (struct SAMx8_private){0};

	sam->public.cpu_cycle = DELEGATE_DEFAULT3(void, int, bool, uint16);
	sam->public.vdg_update = DELEGATE_DEFAULT0(void);

	samp->reset = samx8_reset;
	samp->mem_cycle = samx8_mem_cycle;
	samp->decode = samx8_decode;
	samp->vdg_hsync = samx8_vdg_hsync;
	samp->vdg_fsync = samx8_vdg_fsync;
	samp->vdg_bytes = samx8_vdg_bytes;
	samp->set_register = samx8_set_register;
	samp->get_register = samx8_get_register;

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

static _Bool samx8_finish(struct part *p) {
	struct SAMx8_private *sam = (struct SAMx8_private *)p;
	sam->Vprev = sam->V;
	update_vcounter_inputs(sam);
	update_from_register(sam);
	return 1;
}

static _Bool samx8_is_a(struct part *p, const char *name) {
	(void)p;
	return strcmp(name, "SN74LS783") == 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void samx8_reset(struct MC6883 *samp) {
	struct SAMx8_private *sam = (struct SAMx8_private *)samp;

	sam->V = 0;
	sam->F = 0;
	sam->COMMON = 0;
	sam->R = 0;
	sam->TY = 0;
	samx8_vdg_fsync(samp, 1);
	sam->running_fast = 0;
	sam->extend_slow_cycle = 0;
	sam->page_map[0] = 0;
	sam->page_map[1] = 1;
	sam->page_map[2] = 2;
	sam->page_map[3] = 3;
	sam->page_map[4] = 2;
	sam->page_map[5] = 3;
	sam->page_map[6] = 2;
	sam->page_map[7] = 3;
}

// The primary function of the SAM: translates an address (A) plus Read/!Write
// flag (RnW) into an S value and RAM address (Z).  Writes to the SAM control
// register will update the internal configuration.  The CPU delegate is called
// with the number of (SAM) cycles elapsed, RnW flag and translated address.

static uint8_t const io_S[8] = { 4, 5, 6, 7, 7, 7, 7, 2 };
static uint8_t const data_S[8] = { 7, 7, 7, 7, 1, 2, 3, 3 };

void samx8_mem_cycle(void *sptr, _Bool RnW, uint16_t A) {
	struct MC6883 *samp = sptr;
	struct SAMx8_private *sam = (struct SAMx8_private *)samp;
	int ncycles;
	_Bool fast_cycle;
	_Bool want_register_update = 0;

	_Bool is_FFxx    = ((A >> 8) & 0xff) == 0xff;
	_Bool is_IO0     = is_FFxx && ((A >> 5) & 0x7) == 0x0;  // FF0x and FF1x
	_Bool is_IO1     = is_FFxx && ((A >> 4) & 0xf) == 0x2;  // FF2x ONLY
	_Bool is_FF3x    = is_FFxx && ((A >> 4) & 0xf) == 0x3;  // FF3x ONLY
	_Bool is_IO2     = is_FFxx && ((A >> 5) & 0x7) == 0x2;  // FF4x and FF5x
	_Bool is_SAM_REG = is_FFxx && ((A >> 5) & 0x7) == 0x6;  // FFCx and FFDx
	_Bool is_IRQ_VEC = is_FFxx && ((A >> 5) & 0x7) == 0x7;  // FFEx and FFFx

	_Bool is_COMMON0 = (sam->COMMON & 1) && ((A >> 12) & 0xf) == 0xf;
	_Bool is_COMMON1 = (sam->COMMON & 2) && ((A >> 12) & 0xf) == 0xe;
	_Bool is_COMMON = (is_COMMON0 || is_COMMON1) && (is_IRQ_VEC || !is_FFxx);
	_Bool is_8xxx = ((A >> 13) & 0x7) == 0x4;
	_Bool is_Axxx = ((A >> 13) & 0x7) == 0x5;
	_Bool is_Cxxx = ((A >> 14) & 0x3) == 0x3 && !is_FFxx;

	_Bool is_ROM0 = !sam->TY && is_8xxx;
	_Bool is_ROM1 = !sam->TY && is_Axxx;
	_Bool is_ROM2 = !sam->TY && is_Cxxx;

	_Bool is_RAM = !(A & 0x8000) || (sam->TY && !is_FFxx);

	// IO, SAM registers, IRQ vectors
	if (is_IO0) samp->S = 0x4;
	else if (is_IO1) samp->S = 0x5;
	else if (is_IO2) samp->S = 0x6;
	// RAM for COMMON:
	else if (is_COMMON) samp->S = 0x0;
	// ROM1 for IRQ vectors:
	else if (is_IRQ_VEC) samp->S = 0x2;
	else if (is_FFxx) samp->S = 0x7;
	// Upper 32K in map type 0:
	else if (is_ROM0) samp->S = 0x1;
	else if (is_ROM1) samp->S = 0x2;
	else if (is_ROM2) samp->S = 0x3;
	// RAM
	else samp->S = 0x0;

	samp->nWE = is_RAM ? RnW : 1;
	samp->RAS0 = samp->RAS1 = 0;

	fast_cycle = sam->R;

	if (is_SAM_REG && !RnW) {
		if (A <= 0xffc5) {
			// might affect display, so update VDG
			DELEGATE_CALL(samp->vdg_update);
		}
		switch ((A >> 1) & 0xf) {
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
		case 0xa: sam->TASK = A & 0x1; break;
		case 0xb: sam->R = A & 0x1; break;
		case 0xc: sam->R = A & 0x1; break;
		case 0xf: sam->TY = A & 0x1; break;
		default: break;
		}
		want_register_update = 1;
	}

	if (is_FF3x && !RnW) {
		if ((A & 15) < 8) {
			sam->page_map[A & 15] = *samp->CPUD & 0x1f;
		} else if ((A & 15) == 8) {
			sam->F = (sam->F & ~0x7e000) | ((*samp->CPUD & 0x3f) << 13);
		} else if ((A & 15) == 9) {
			sam->F = (sam->F & ~0x01fe0) | (*samp->CPUD << 5);
		} else if ((A & 15) == 15) {
			sam->COMMON = *samp->CPUD & 3;
		}
	}

	if (RnW && is_SAM_REG && ((A >> 1) & 0xf) == 0xa) {
		*samp->CPUD = (*samp->CPUD & ~1) | sam->TASK;
	}

	unsigned bank = (sam->TASK ? 4 : 0) | ((A >> 14) & 0x3);
	unsigned page;
	if (is_COMMON) {
		page = 31 << 14;
	} else {
		page = sam->page_map[bank] << 14;
	}
	samp->RAS0 = is_RAM || is_COMMON;
	samp->RAS1 = 0;
	samp->Zrow = page | (A & 0x3fff);
	samp->Zcol = 0;

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

// Just the address decode from samx8_mem_cycle().  Used to verify that a
// breakpoint refers to ROM.

unsigned samx8_decode(struct MC6883 *samp, _Bool RnW, uint16_t A) {
	const struct SAMx8_private *sam = (struct SAMx8_private *)samp;
	if ((A >> 8) == 0xff) {
		// I/O area
		return io_S[(A >> 5) & 7];
	} else if ((A & 0x8000) && !sam->TY) {
		return data_S[A >> 13];
	}
	return RnW ? 0 : data_S[A >> 13];
}

static void vcounter_set(struct SAMx8_private *sam, int i, int val);

static void vcounter_update(struct SAMx8_private *sam, int i) {
	_Bool old_input = sam->vcounter[i].input;
	_Bool new_input = sam->vcounter[sam->vcounter[i].input_from].output;
	if (new_input != old_input) {
		sam->vcounter[i].input = new_input;
		if (!new_input) {
			vcounter_set(sam, i, (sam->vcounter[i].value + 1) % sam->vcounter[i].val_mod);
		}
	}
}

static void vcounter_set(struct SAMx8_private *sam, int i, int val) {
	sam->vcounter[i].value = val;
	sam->vcounter[i].output = val & sam->vcounter[i].out_mask;
	for (int j = 0; j < NUM_VCOUNTERS - 2; j++) {
		if (sam->vcounter[j].input_from == i)
			vcounter_update(sam, j);
	}
}

void samx8_vdg_hsync(struct MC6883 *samp, _Bool level) {
	struct SAMx8_private *sam = (struct SAMx8_private *)samp;
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
		vcounter_update(sam, VC_B18_5);
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

static inline void vcounter_reset(struct SAMx8_private *sam, int i) {
	sam->vcounter[i].input = 0;
	sam->vcounter[i].value = 0;
	sam->vcounter[i].output = 0;
}

void samx8_vdg_fsync(struct MC6883 *samp, _Bool level) {
	struct SAMx8_private *sam = (struct SAMx8_private *)samp;
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
	vcounter_reset(sam, VC_B18_5);
	sam->vcounter[VC_B18_5].value = sam->F >> 5;
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

int samx8_vdg_bytes(struct MC6883 *samp, int nbytes) {
	struct SAMx8_private *sam = (struct SAMx8_private *)samp;

	// SAMx8 supports video in fast mode, so just set the video address
	// and done.
	uint16_t b3_0 = sam->vcounter[VC_B3_0].value;
	uint32_t V = (sam->vcounter[VC_B18_5].value << 5) | (sam->vcounter[VC_B4].value << 4) | b3_0;
	samp->Vrow = V;
	samp->Vcol = 0;

	// Need to advance the VDG address pointer.

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

static void samx8_set_register(struct MC6883 *samp, unsigned value) {
	(void)samp;
	(void)value;
}

static unsigned samx8_get_register(struct MC6883 *samp) {
	(void)samp;
	return 0;
}

static void update_vcounter_inputs(struct SAMx8_private *sam) {
	switch (vdg_ydivs[sam->V]) {
	case DIV12:
		sam->vcounter[VC_B18_5].input_from = VC_YDIV4;
		break;
	case DIV3:
		sam->vcounter[VC_B18_5].input_from = VC_YDIV3;
		break;
	case DIV2:
		sam->vcounter[VC_B18_5].input_from = VC_YDIV2;
		break;
	case DIV1: default:
		sam->vcounter[VC_B18_5].input_from = VC_B4;
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

static void update_from_register(struct SAMx8_private *sam) {
	int old_ydiv = vdg_ydivs[sam->Vprev];
	int old_xdiv = vdg_xdivs[sam->Vprev];

	int new_ydiv = vdg_ydivs[sam->V];
	int new_xdiv = vdg_xdivs[sam->V];
	sam->clr_mode = vdg_hclrs[sam->V];

	sam->Vprev = sam->V;

	if (new_ydiv != old_ydiv) {
		switch (new_ydiv) {
		case DIV12:
			sam->vcounter[VC_B18_5].input_from = VC_YDIV4;
			break;
		case DIV3:
			sam->vcounter[VC_B18_5].input_from = VC_YDIV3;
			break;
		case DIV2:
			sam->vcounter[VC_B18_5].input_from = VC_YDIV2;
			break;
		case DIV1: default:
			sam->vcounter[VC_B18_5].input_from = VC_B4;
			break;
		}
		vcounter_update(sam, VC_YDIV2);
		vcounter_update(sam, VC_YDIV3);
		vcounter_update(sam, VC_YDIV4);
		vcounter_update(sam, VC_B18_5);
	}

	if (new_xdiv != old_xdiv) {
		switch (new_xdiv) {
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
		vcounter_update(sam, VC_XDIV2);
		vcounter_update(sam, VC_XDIV3);
		vcounter_update(sam, VC_B4);
	}

	sam->mpu_rate_fast = sam->R;
}
