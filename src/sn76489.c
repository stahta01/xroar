/*

TI SN76489 sound chip

Copyright 2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

/* Sources:
 *     SN76489AN data sheet
 *
 *     SMS Power!  SN76489 - Development
 *         http://www.smspower.org/Development/SN76489
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>

#include "xalloc.h"

#include "part.h"
#include "sn76489.h"

// Butterworth IIR, order 3, fs 250kHz, -3dB at 20kHz.  Generated here:
// https://www-users.cs.york.ac.uk/~fisher/mkfilter/

// This very much assumes refclk of 4MHz, which is fine for this purpose, but
// beware if you're using this code elsewhere.

#define IIR_GAIN (9.820696921e+01)
#define IIR_Z0 (1.0)
#define IIR_Z1 (3.0)
#define IIR_Z2 (3.0)
#define IIR_Z3 (1.0)
#define IIR_P0 (0.3617959282)
#define IIR_P1 (-1.4470540195)
#define IIR_P2 (2.0037974774)

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

// attenuation lookup table, 10 ^ (-i / 10)
static const float attenuation[16] = {
	1.000000/4.0, 0.794328/4.0, 0.630957/4.0, 0.501187/4.0,
	0.398107/4.0, 0.316228/4.0, 0.251189/4.0, 0.199526/4.0,
	0.158489/4.0, 0.125893/4.0, 0.100000/4.0, 0.079433/4.0,
	0.063096/4.0, 0.050119/4.0, 0.039811/4.0, 0.000000/4.0 };

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
	float output;
	float xv1, xv2, xv3;
	float yv1, yv2;
};

static void sn76489_free(struct part *p);

// C integer type-safe delta between two unsigned values that may overflow.
// Depends on 2's-complement behaviour (guaranteed by C99 spec where types are
// available).

static int tick_delta(uint32_t t0, uint32_t t1) {
        uint32_t dt = t0 - t1;
        return *(int32_t *)&dt;
}

struct SN76489 *sn76489_new(int refrate, int framerate, int tickrate, uint32_t tick) {
	struct SN76489_private *csg_ = part_new(sizeof(*csg_));
	struct SN76489 *csg = &csg_->public;
	*csg_ = (struct SN76489_private){0};
	part_init(&csg->part, "SN76489");
	csg->part.free = sn76489_free;

	csg->ready = 1;
	csg_->refrate = refrate >> 4;
	csg_->framerate = framerate;
	csg_->tickrate = tickrate;
	csg_->last_fragment_tick = tick;

	csg_->frequency[0] = csg_->counter[0] = 0x001;
	csg_->frequency[1] = csg_->counter[1] = 0x400;
	csg_->frequency[2] = csg_->counter[2] = 0x400;
	csg_->frequency[3] = csg_->counter[3] = 0x010;
	for (int c = 0; c < 4; c++) {
		csg_->amplitude[c][1] = attenuation[4];
	}
	csg_->noise_lfsr = 0x4000;

	// 76489 needs 32 cycles of its reference clock between writes.
	// Compute this (approximately) wrt system "ticks".
	float readyticks = (32.0 * tickrate) / refrate;
	csg_->readyticks = (int)readyticks;

	return csg;
}

static void sn76489_free(struct part *p) {
	(void)p;
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
	unsigned c = reg_sel >> 1;
	csg_->reg_val[reg_sel] = reg_val;

	if (reg_sel & 1) {
		csg_->amplitude[c][1] = attenuation[reg_val];
		_Bool state = csg_->state[c];
		csg_->level[c] = csg_->amplitude[c][state];
	} else {
		if (c < 3) {
			if (reg_val == 0) {
				csg_->frequency[c] = 0x400;
			} else {
				csg_->frequency[c] = reg_val;
			}
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

#ifdef HAVE___BUILTIN_PARITY

#define parity(v) (unsigned)__builtin_parity(v)

#else

static unsigned parity(unsigned val) {
	val ^= val >> 8;
	val ^= val >> 4;
	val ^= val >> 2;
	val ^= val >> 1;
	return val & 1;
}

#endif

float sn76489_get_audio(void *sptr, uint32_t tick, int nframes, float *buf) {
	struct SN76489_private *csg_ = sptr;
	struct SN76489 *csg = &csg_->public;

	// tick counter may overflow betweeen writes.  as this function is
	// called often, this should ensure ready signal is updated correctly.
	(void)is_ready(csg, tick);

	int nticks = csg_->nticks + tick_delta(tick, csg_->last_fragment_tick);
	csg_->last_fragment_tick = tick;

	// last output value
	float output = csg_->output;
	float new_output = output;
	// butterworth filter state
	float xv1 = csg_->xv1;
	float xv2 = csg_->xv2;
	float xv3 = csg_->xv3;
	float yv1 = csg_->yv1;
	float yv2 = csg_->yv2;

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
			                   (csg_->noise_white
					    ? parity(csg_->noise_lfsr & 0x0003) << 14
					    : (csg_->noise_lfsr & 1) << 14);
			_Bool state = csg_->noise_lfsr & 1;
			csg_->state[3] = state;
			csg_->level[3] = csg_->amplitude[3][state];
		}

		// sum the output channels
		new_output = csg_->level[0] + csg_->level[1] +
		             csg_->level[2] + csg_->level[3];

		// apply butterworth low-pass filter
		float xv0 = xv1;
		xv1 = xv2;
		xv2 = xv3;
		xv3 = new_output / IIR_GAIN;
		float yv0 = yv1;
		yv1 = yv2;
		yv2 = output;
		output = IIR_Z0*xv0 + IIR_Z1*xv1 + IIR_Z2*xv2 + IIR_Z3*xv3
		         + IIR_P0*yv0 + IIR_P1*yv1 + IIR_P2*yv2;

	}

	csg_->nticks = nticks;

	// in case of underrun
	if (buf) {
		while (nframes > 0) {
			*(buf++) = output;
			nframes--;
		}
	}

	csg_->output = output;
	csg_->xv1 = xv1;
	csg_->xv2 = xv2;
	csg_->xv3 = xv3;
	csg_->yv1 = yv1;
	csg_->yv2 = yv2;

	// return final unfiltered output value
	return new_output;
}
