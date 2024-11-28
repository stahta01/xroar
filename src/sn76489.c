/** \file
 *
 *  \brief TI SN76489 sound chip.
 *
 *  \copyright Copyright 2018-2024 Ciaran Anscomb
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
 *  \par Sources
 *
 *  - SN76489AN data sheet
 *
 *  - SMS Power!  SN76489 - Development, http://www.smspower.org/Development/SN76489
 */

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "intfuncs.h"

#include "filter.h"
#include "part.h"
#include "serialise.h"
#include "sn76489.h"

/*
 * Initial state doesn't seem to be quite random.  First two channels seem to
 * be on, with first generating very high tone, and second at lowest frequency.
 * Volume not maxed out.  There may be more state to explore here.
 *
 * All channels - including noise - contribute either zero or a +ve offset to
 * the signal.
 *
 * f=0 on tones is equivalent to f=1024.
 *
 * No special-casing for f=1 on tones.  Doc suggests some variants produce DC
 * for this, but Stewart Orchard has better measure-fu than me and proved it
 * yields 125kHz as predicted.
 */

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct SN76489_private {
	struct SN76489 public;

	uint32_t last_write_tick;
	uint32_t last_fragment_tick;

	int refrate;  // reference clock rate
	int framerate;  // output rate
	int tickrate;  // system clock rate

	int readyticks;  // computed conversion of systicks to refticks
	int frameerror;  // track refrate/framerate error
	int tickerror;  // track redrate/tickrate error
	_Bool overrun;  // carry sample from previous call
	int nticks;

	unsigned reg_sel;  // latched register select
	unsigned reg_val[8];  // raw register value (interpreted below)

	unsigned frequency[4];  // counter reset value
	float amplitude[4][2];  // output amplitudes
	unsigned counter[4];  // current counter value
	_Bool state[4];  // current output state (0/1, indexes amplitude)
	float level[4];  // set from amplitude[], decays over time
	_Bool nstate;  // separate state toggle for noise channel

	// noise-specific state
	_Bool noise_white;  // 0 = periodic, 1 = white
	_Bool noise_tone3;  // 1 = clocked from output of tone3
	unsigned noise_lfsr;

	// low-pass filter state
	struct filter_iir *filter;
};

#define SN76489_SER_REG_VAL (6)
#define SN76489_SER_COUNTER (7)
#define SN76489_SER_STATE (8)

static struct ser_struct ser_struct_sn76489[] = {
	SER_ID_STRUCT_ELEM(1, struct SN76489_private, public.ready),

	SER_ID_STRUCT_ELEM(2, struct SN76489_private, refrate),
	// ID 3 used to be 'framerate', but this is a local parameter
	SER_ID_STRUCT_ELEM(4, struct SN76489_private, tickrate),

	SER_ID_STRUCT_ELEM(5, struct SN76489_private, reg_sel),
	SER_ID_STRUCT_UNHANDLED(SN76489_SER_REG_VAL),

	SER_ID_STRUCT_UNHANDLED(SN76489_SER_COUNTER),
	SER_ID_STRUCT_UNHANDLED(SN76489_SER_STATE),
	SER_ID_STRUCT_ELEM(9, struct SN76489_private, nstate),

	SER_ID_STRUCT_ELEM(10, struct SN76489_private, noise_lfsr),
};

static _Bool sn76489_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool sn76489_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data sn76489_ser_struct_data = {
	.elems = ser_struct_sn76489,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_sn76489),
	.read_elem = sn76489_read_elem,
	.write_elem = sn76489_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// attenuation lookup table, 10 ^ (-i / 10)
static const float attenuation[16] = {
	1.000000/4.0, 0.794328/4.0, 0.630957/4.0, 0.501187/4.0,
	0.398107/4.0, 0.316228/4.0, 0.251189/4.0, 0.199526/4.0,
	0.158489/4.0, 0.125893/4.0, 0.100000/4.0, 0.079433/4.0,
	0.063096/4.0, 0.050119/4.0, 0.039811/4.0, 0.000000/4.0 };

static void update_reg(struct SN76489_private *csg_, unsigned reg_sel, unsigned reg_val);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// SN76489 part creation

static struct part *sn76489_allocate(void);
static void sn76489_initialise(struct part *p, void *options);
static _Bool sn76489_finish(struct part *p);
static void sn76489_free(struct part *p);

static const struct partdb_entry_funcs sn76489_funcs = {
	.allocate = sn76489_allocate,
	.initialise = sn76489_initialise,
	.finish = sn76489_finish,
	.free = sn76489_free,

	.ser_struct_data = &sn76489_ser_struct_data,
};

const struct partdb_entry sn76489_part = { .name = "SN76489", .description = "Texas Instruments | SN76489 DCSG", .funcs = &sn76489_funcs };

static struct part *sn76489_allocate(void) {
	struct SN76489_private *csg_ = part_new(sizeof(*csg_));
	struct SN76489 *csg = &csg_->public;
	struct part *p = &csg->part;

	*csg_ = (struct SN76489_private){0};

	csg_->frequency[0] = csg_->counter[0] = 0x001;
	csg_->frequency[1] = csg_->counter[1] = 0x400;
	csg_->frequency[2] = csg_->counter[2] = 0x400;
	csg_->frequency[3] = csg_->counter[3] = 0x010;
	for (int c = 0; c < 4; c++) {
		csg_->amplitude[c][1] = attenuation[4];
	}
	csg_->noise_lfsr = 0x4000;

	return p;
}

static void sn76489_initialise(struct part *p, void *options) {
	(void)options;
	struct SN76489_private *csg_ = (struct SN76489_private *)p;
	struct SN76489 *csg = &csg_->public;

	sn76489_configure(csg, 4000000, 48000, 14318180, 0);
}

static _Bool sn76489_finish(struct part *p) {
	struct SN76489_private *csg_ = (struct SN76489_private *)p;

	// 76489 needs 32 cycles of its reference clock between writes.
	// Compute this (approximately) wrt system "ticks".
	float readyticks = (32.0 * csg_->tickrate) / (csg_->refrate << 4);
	csg_->readyticks = (int)readyticks;

	return 1;
}

static void sn76489_free(struct part *p) {
	struct SN76489_private *csg_ = (struct SN76489_private *)p;
	if (csg_->filter) {
		filter_iir_free(csg_->filter);
	}
}

static _Bool sn76489_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct SN76489_private *csg_ = sptr;
	switch (tag) {
	case SN76489_SER_REG_VAL:
		for (int i = 0; i < 8; i++) {
			unsigned v = ser_read_uint16(sh);
			update_reg(csg_, i, v);
		}
		return 1;
	case SN76489_SER_COUNTER:
		for (int i = 0; i < 4; i++) {
			csg_->counter[i] = ser_read_uint16(sh);
		}
		return 1;
	case SN76489_SER_STATE:
		for (int i = 0; i < 4; i++) {
			csg_->state[i] = ser_read_uint8(sh);
		}
		return 1;
	default:
		return 0;
	}
}

static _Bool sn76489_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct SN76489_private *csg_ = sptr;
	switch (tag) {
	case SN76489_SER_REG_VAL:
		ser_write_tag(sh, tag, 8*2);
		for (int i = 0; i < 8; i++) {
			ser_write_uint16_untagged(sh, csg_->reg_val[i]);
		}
		ser_write_close_tag(sh);
		return 1;
	case SN76489_SER_COUNTER:
		ser_write_tag(sh, tag, 4*2);
		for (int i = 0; i < 4; i++) {
			ser_write_uint16_untagged(sh, csg_->counter[i]);
		}
		ser_write_close_tag(sh);
		return 1;
	case SN76489_SER_STATE:
		ser_write_tag(sh, tag, 4);
		for (int i = 0; i < 4; i++) {
			ser_write_uint8_untagged(sh, csg_->state[i]);
		}
		ser_write_close_tag(sh);
		return 1;
	default:
		return 0;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// C integer type-safe delta between two unsigned values that may overflow.
// Depends on 2's-complement behaviour (guaranteed by C99 spec where types are
// available).

static int tick_delta(uint32_t t0, uint32_t t1) {
        uint32_t dt = t0 - t1;
        return *(int32_t *)&dt;
}

void sn76489_configure(struct SN76489 *csg, int refrate, int framerate, int tickrate,
		       uint32_t tick) {
	struct SN76489_private *csg_ = (struct SN76489_private *)csg;

	csg->ready = 1;
	csg_->refrate = refrate >> 4;
	csg_->framerate = framerate;
	csg_->tickrate = tickrate;
	csg_->last_fragment_tick = tick;

	if (csg_->filter) {
		filter_iir_free(csg_->filter);
	}
	csg_->filter = filter_iir_new(FILTER_BU|FILTER_LP, 3, 250000, framerate/2, 0);
}

static _Bool is_ready(struct SN76489 *csg, uint32_t tick) {
	struct SN76489_private *csg_ = (struct SN76489_private *)csg;
	if (csg->ready)
		return 1;
	int dt = tick_delta(tick, csg_->last_write_tick);
	if (dt > csg_->readyticks)
		return (csg->ready = 1);
	return 0;
}

static void update_reg(struct SN76489_private *csg_, unsigned reg_sel, unsigned reg_val) {
	csg_->reg_val[reg_sel] = reg_val;
	unsigned c = reg_sel >> 1;
	if (reg_sel & 1) {
		csg_->amplitude[c][1] = attenuation[reg_val];
		_Bool state = csg_->state[c];
		csg_->level[c] = csg_->amplitude[c][state];
	} else {
		if (c < 3) {
			csg_->frequency[c] = reg_val ? reg_val : 0x400;
		} else {
			// noise channel is special
			csg_->noise_white = reg_val & 0x04;
			csg_->noise_tone3 = (reg_val & 3) == 3;
			switch (reg_val & 3) {
			case 0:
				csg_->frequency[3] = 0x10;
				break;
			case 1:
				csg_->frequency[3] = 0x20;
				break;
			case 2:
				csg_->frequency[3] = 0x40;
				break;
			default:
				break;
			}
			// always reset shift register
			csg_->noise_lfsr = 0x4000;
		}
	}
}

void sn76489_write(struct SN76489 *csg, uint32_t tick, uint8_t D) {
	struct SN76489_private *csg_ = (struct SN76489_private *)csg;

	if (!is_ready(csg, tick)) {
		return;
	}
	csg->ready = 0;
	csg_->last_write_tick = tick;

	unsigned reg_sel;
	unsigned mask;
	unsigned val;

	if (!(D & 0x80)) {
		// Data
		reg_sel = csg_->reg_sel;
		if (!(reg_sel & 1)) {
			// Tone/noise
			mask = 0x000f;
			val = (D & 0x3f) << 4;
		} else {
			// Attenuation
			mask = 0;  // ignored
			val = D & 0x0f;
		}
	} else {
		// Latch register + data
		reg_sel = csg_->reg_sel = (D >> 4) & 0x07;
		mask = 0x03f0;  // ignored for attenuation
		val = D & 0x0f;
	}

	unsigned reg_val = (csg_->reg_val[reg_sel] & mask) | val;
	update_reg(csg_, reg_sel, reg_val);
}

float sn76489_get_audio(void *sptr, uint32_t tick, int nframes, float *buf) {
	struct SN76489_private *csg_ = sptr;
	struct SN76489 *csg = &csg_->public;

	// tick counter may overflow betweeen writes.  as this function is
	// called often, this should ensure ready signal is updated correctly.
	(void)is_ready(csg, tick);

	int nticks = csg_->nticks + tick_delta(tick, csg_->last_fragment_tick);
	csg_->last_fragment_tick = tick;

	float output = csg_->filter->output;
	float new_output = output;

	// if previous call overran
	if (csg_->overrun && nframes > 0) {
		if (buf) {
			*(buf++) = output;
		}
		nframes--;
		csg_->overrun = 0;
	}

	while (nticks > 0) {

		// framerate will *always* be less than refrate, so this is a
		// simple test.  allow for 1 overrun sample.
		csg_->frameerror += csg_->framerate;
		if (csg_->frameerror >= csg_->refrate) {
			csg_->frameerror -= csg_->refrate;
			if (nframes > 0) {
				if (buf) {
					*(buf++) = output;
				}
				nframes--;
			} else {
				csg_->overrun = 1;
			}
		}

		// tickrate may be higher than refrate: calculate remainder.
		csg_->tickerror += csg_->tickrate;
		int dtick = csg_->tickerror / csg_->refrate;
		if (dtick > 0) {
			nticks -= dtick;
			csg_->tickerror -= (dtick * csg_->refrate);
		}

		// noise is either clocked by independent frequency select, or
		// by the output of tone generator 3
		_Bool noise_clock = 0;

		// tone generators 1, 2, 3
		for (int c = 0; c < 3; c++) {
			_Bool state = csg_->state[c] & 1;
			if (--csg_->counter[c] == 0) {
				state = !state;
				csg_->counter[c] = csg_->frequency[c];
				csg_->state[c] = state;
				csg_->level[c] = csg_->amplitude[c][state];
				if (c == 2 && csg_->noise_tone3) {
					// noise channel clocked from tone3
					noise_clock = state;
				}
			}
		}

		if (!csg_->noise_tone3) {
			// noise channel clocked independently
			if (--csg_->counter[3] == 0) {
				csg_->nstate = !csg_->nstate;
				csg_->counter[3] = csg_->frequency[3];
				noise_clock = csg_->nstate;
			}
		}

		if (noise_clock) {
			// input transition to high clocks the LFSR
			csg_->noise_lfsr = (csg_->noise_lfsr >> 1) |
			                   ((unsigned)(csg_->noise_white
					     ? u32_parity(csg_->noise_lfsr & 0x0003)
					     : (csg_->noise_lfsr & 1)) << 14);
			_Bool state = csg_->noise_lfsr & 1;
			csg_->state[3] = state;
			csg_->level[3] = csg_->amplitude[3][state];
		}

		// sum the output channels
		new_output = csg_->level[0] + csg_->level[1] +
		             csg_->level[2] + csg_->level[3];

		output = filter_iir_apply(csg_->filter, new_output);
	}

	csg_->nticks = nticks;

	// in case of underrun
	if (buf) {
		while (nframes > 0) {
			*(buf++) = output;
			nframes--;
		}
	}

	// return final unfiltered output value
	return new_output;
}
