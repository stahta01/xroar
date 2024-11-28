/** \file
 *
 *  \brief AY-3-891x sound chip.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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
 *  - AY-3-891x data sheet
 *
 *  - https://github.com/lvd2/ay-3-8910_reverse_engineered.git [deathsoft]
 */

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "delegate.h"
#include "intfuncs.h"

#include "filter.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"
#include "ay891x.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct AY891X_ {
	struct AY891X ay891x;

	uint32_t last_fragment_tick;

	int refrate;  // reference clock rate
	int framerate;  // output rate
	int tickrate;  // system clock rate

	int frameerror;  // track refrate/framerate error
	int tickerror;  // track redrate/tickrate error
	_Bool overrun;  // carry sample from previous call
	int nticks;

	unsigned address;  // latched address
	uint8_t regs[16];  // raw register value (interpreted below)

	unsigned tone_period[3];  // Tone Period A-C
	_Bool tone_enable[3];

	unsigned noise_period;
	_Bool noise_enable[3];

	_Bool InOA;  // IO mode port A (1 = input)
	_Bool InOB;  // IO mode port B (1 = input)

	_Bool envelope_mode[3];
	float amplitude[3][2];
	unsigned envelope_period;
	_Bool envelope_hold;
	_Bool envelope_alt;
	_Bool envelope_att;
	_Bool envelope_cont;

	int tone_counter[3];  // current counter value
	_Bool tone_state[3];  // current output state (0/1, indexes amplitude)
	float level[3];  // set from amplitude[]

	int envelope_counter;
	unsigned envelope_level;

	// noise-specific state
	int noise_counter;
	_Bool noise_state;
	unsigned noise_lfsr;

	// low-pass filter state
	struct filter_iir *filter;
};

#define AY891X_SER_REG_VAL (2)
#define AY891X_SER_COUNTER (3)
#define AY891X_SER_STATE (4)

static struct ser_struct ser_struct_ay891x[] = {
	SER_ID_STRUCT_ELEM(1, struct AY891X_, address),
	SER_ID_STRUCT_UNHANDLED(AY891X_SER_REG_VAL),

	SER_ID_STRUCT_UNHANDLED(AY891X_SER_COUNTER),
	SER_ID_STRUCT_UNHANDLED(AY891X_SER_STATE),

	SER_ID_STRUCT_ELEM(5, struct AY891X_, envelope_counter),
	SER_ID_STRUCT_ELEM(6, struct AY891X_, envelope_level),

	SER_ID_STRUCT_ELEM(7, struct AY891X_, noise_counter),
	SER_ID_STRUCT_ELEM(8, struct AY891X_, noise_state),

	SER_ID_STRUCT_ELEM(9, struct AY891X_, noise_lfsr),
};

static _Bool ay891x_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool ay891x_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data ay891x_ser_struct_data = {
	.elems = ser_struct_ay891x,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_ay891x),
	.read_elem = ay891x_read_elem,
	.write_elem = ay891x_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Amplitude lookup table.  Normalised and divided by 3 so that all channels
// sum to 1.0.

static const float amplitude[16] = {
	0.000000/3.0, 0.007813/3.0, 0.011049/3.0, 0.015625/3.0,
	0.022097/3.0, 0.031250/3.0, 0.044194/3.0, 0.062500/3.0,
	0.088388/3.0, 0.125000/3.0, 0.176777/3.0, 0.250000/3.0,
	0.353553/3.0, 0.500000/3.0, 0.707107/3.0, 1.000000/3.0
};

static void update_reg(struct AY891X_ *psg_, unsigned address);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// AY891X part creation

static struct part *ay891x_allocate(void);
static void ay891x_initialise(struct part *p, void *options);
static _Bool ay891x_finish(struct part *p);
static void ay891x_free(struct part *p);

static const struct partdb_entry_funcs ay891x_funcs = {
	.allocate = ay891x_allocate,
	.initialise = ay891x_initialise,
	.finish = ay891x_finish,
	.free = ay891x_free,

	.ser_struct_data = &ay891x_ser_struct_data,
};

const struct partdb_entry ay891x_part = { .name = "AY891X", .description = "General Instrument | AY-3-891x PSG", .funcs = &ay891x_funcs };

static struct part *ay891x_allocate(void) {
	struct AY891X_ *psg_ = part_new(sizeof(*psg_));
	struct AY891X *psg = &psg_->ay891x;
	struct part *p = &psg->part;

	*psg_ = (struct AY891X_){0};

	psg->a.out_sink = 0xff;
	psg->a.in_sink = 0xff;
	psg->b.out_sink = 0xff;
	psg->b.in_sink = 0xff;

	for (int c = 0; c < 3; c++) {
		psg_->tone_period[c] = psg_->tone_counter[c] = 1;
		psg_->amplitude[c][1] = amplitude[0];
	}
	psg_->noise_period = psg_->noise_counter = 1;
	psg_->noise_lfsr = 0x4000;

	return p;
}

static void ay891x_initialise(struct part *p, void *options) {
	(void)options;
	struct AY891X_ *psg_ = (struct AY891X_ *)p;

	ay891x_configure(&psg_->ay891x, 4000000, 48000, 14318180, 0);
}

static _Bool ay891x_finish(struct part *p) {
	struct AY891X_ *psg_ = (struct AY891X_ *)p;
	(void)psg_;
	return 1;
}

static void ay891x_free(struct part *p) {
	struct AY891X_ *psg_ = (struct AY891X_ *)p;
	if (psg_->filter) {
		filter_iir_free(psg_->filter);
	}
}

static _Bool ay891x_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct AY891X_ *psg_ = sptr;
	switch (tag) {
	case AY891X_SER_REG_VAL:
		for (int i = 0; i < 16; i++) {
			psg_->regs[i] = ser_read_uint8(sh);
			update_reg(psg_, i);
		}
		return 1;
	case AY891X_SER_COUNTER:
		for (int i = 0; i < 3; i++) {
			psg_->tone_counter[i] = ser_read_uint16(sh);
		}
		return 1;
	case AY891X_SER_STATE:
		for (int i = 0; i < 3; i++) {
			psg_->tone_state[i] = ser_read_uint8(sh);
		}
		return 1;
	default:
		return 0;
	}
}

static _Bool ay891x_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct AY891X_ *psg_ = sptr;
	switch (tag) {
	case AY891X_SER_REG_VAL:
		ser_write_tag(sh, tag, 8*2);
		for (int i = 0; i < 16; i++) {
			ser_write_uint8_untagged(sh, psg_->regs[i]);
		}
		ser_write_close_tag(sh);
		return 1;
	case AY891X_SER_COUNTER:
		ser_write_tag(sh, tag, 3*2);
		for (int i = 0; i < 3; i++) {
			ser_write_uint16_untagged(sh, psg_->tone_counter[i]);
		}
		ser_write_close_tag(sh);
		return 1;
	case AY891X_SER_STATE:
		ser_write_tag(sh, tag, 3);
		for (int i = 0; i < 3; i++) {
			ser_write_uint8_untagged(sh, psg_->tone_state[i]);
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

void ay891x_configure(struct AY891X *psg, int refrate, int framerate, int tickrate,
		       uint32_t tick) {
	struct AY891X_ *psg_ = (struct AY891X_ *)psg;

	// For our operation, we divide refrate by 16.  Tone and noise
	// generator counters divide by 16, so we can use their period values
	// as-is.
	//
	// XXX However, that seems to result in tones an octave too low, so
	// bodge by actually only dividing refrate by 8, and multiplying
	// envelope periods by 2.

	psg_->refrate = refrate >> 3;
	psg_->framerate = framerate;
	psg_->tickrate = tickrate;
	psg_->last_fragment_tick = tick;

	if (psg_->filter) {
		filter_iir_free(psg_->filter);
	}
	psg_->filter = filter_iir_new(FILTER_BU|FILTER_LP, 3, 250000, framerate/2, 0);
}

static void update_reg(struct AY891X_ *psg_, unsigned address) {
	struct AY891X *psg = &psg_->ay891x;

	switch (address) {

	case 0x0: // Channel A Tone Period, 8-BIT Fine Tune
	case 0x1: // Channel A Tone Period, 4-BIT Course Tune
		psg_->tone_period[0] = ((psg_->regs[1] & 0xf) << 8) | psg_->regs[0];
		if (psg_->tone_period[0] == 0)
			psg_->tone_period[0] = 1;
		break;

	case 0x2: // Channel B Tone Period, 8-BIT Fine Tune
	case 0x3: // Channel B Tone Period, 4-BIT Course Tune
		psg_->tone_period[1] = ((psg_->regs[3] & 0xf) << 8) | psg_->regs[2];
		if (psg_->tone_period[1] == 0)
			psg_->tone_period[1] = 1;
		break;

	case 0x4: // Channel C Tone Period, 8-BIT Fine Tune
	case 0x5: // Channel C Tone Period, 4-BIT Course Tune
		psg_->tone_period[2] = ((psg_->regs[5] & 0xf) << 8) | psg_->regs[4];
		if (psg_->tone_period[2] == 0)
			psg_->tone_period[2] = 1;
		break;

	case 0x6: // 5-BIT Noise Period Control
		psg_->noise_period = psg_->regs[6] & 0x1f;
		if (psg_->noise_period == 0)
			psg_->noise_period = 1;
		break;

	case 0x7: // Enable
		psg_->tone_enable[0] = ~psg_->regs[7] & 0x01;
		psg_->tone_enable[1] = ~psg_->regs[7] & 0x02;
		psg_->tone_enable[2] = ~psg_->regs[7] & 0x04;
		psg_->noise_enable[0] = ~psg_->regs[7] & 0x08;
		psg_->noise_enable[1] = ~psg_->regs[7] & 0x10;
		psg_->noise_enable[2] = ~psg_->regs[7] & 0x20;
		// Switching port IO mode.  When switching from output to
		// input, pull-ups present the port as high.  When switching
		// from input to output, the old input value will have
		// overwritten the register.
		{
			_Bool new_InOA = ~psg_->regs[7] & 0x40;
			if (psg_->InOA != new_InOA) {
				psg_->InOA = new_InOA;
				if (new_InOA) {
					psg->a.out_sink = 0xff;
				} else {
					DELEGATE_SAFE_CALL(psg->a.data_preread);
					psg_->regs[0xe] = psg->a.in_sink;
					psg->a.out_sink = psg_->regs[0xe];
				}
				DELEGATE_SAFE_CALL(psg->a.data_postwrite);
			}
		}
		{
			_Bool new_InOB = ~psg_->regs[7] & 0x80;
			if (psg_->InOB != new_InOB) {
				psg_->InOB = new_InOB;
				if (new_InOB) {
					psg->b.out_sink = 0xff;
				} else {
					DELEGATE_SAFE_CALL(psg->b.data_preread);
					psg->b.out_sink = psg_->regs[0xf];
					psg_->regs[0xf] = psg->b.in_sink;
				}
				DELEGATE_SAFE_CALL(psg->b.data_postwrite);
			}
		}
		break;

	case 0x8: // Channel A Amplitude
		psg_->envelope_mode[0] = psg_->regs[0x8] & 0x10;
		if (!(psg_->regs[0x8] & 0x10))
			psg_->amplitude[0][1] = amplitude[psg_->regs[0x8] & 0xf];
		break;

	case 0x9: // Channel B Amplitude
		psg_->envelope_mode[1] = psg_->regs[0x9] & 0x10;
		if (!(psg_->regs[0x9] & 0x10))
			psg_->amplitude[1][1] = amplitude[psg_->regs[0x9] & 0xf];
		break;

	case 0xa: // Channel C Amplitude
		psg_->envelope_mode[2] = psg_->regs[0xa] & 0x10;
		if (!(psg_->regs[0xa] & 0x10))
			psg_->amplitude[2][1] = amplitude[psg_->regs[0xa] & 0xf];
		break;

	case 0xb: // Envelope Period, 8-BIT Fine Tune
	case 0xc: // Envelope Period, 8-BIT Course Tune
		psg_->envelope_period = (psg_->regs[0xc] << 8) | (psg_->regs[0xb]);
		psg_->envelope_period <<= 1;
		break;

	case 0xd: // Enevelope Shape/Cycle
		psg_->envelope_hold = psg_->regs[0xd] & 0x1;
		psg_->envelope_alt = psg_->regs[0xd] & 0x2;
		psg_->envelope_att = psg_->regs[0xd] & 0x4;
		psg_->envelope_cont = psg_->regs[0xd] & 0x8;
		psg_->envelope_level = psg_->envelope_att ? 0 : 15;
		psg_->envelope_counter = psg_->envelope_period;
		break;

	case 0xe: // 8-BIT PARALLEL I/O on Port A
		psg->a.out_sink = psg_->regs[0xe];
		DELEGATE_SAFE_CALL(psg->a.data_postwrite);
		break;

	case 0xf: // 8-BIT PARALLEL I/O on Port B
		psg->b.out_sink = psg_->regs[0xf];
		DELEGATE_SAFE_CALL(psg->b.data_postwrite);
		break;

	default:
		break;

	}
}

void ay891x_cycle(struct AY891X *psg, _Bool BDIR, _Bool BC1, uint8_t *D) {
	struct AY891X_ *psg_ = (struct AY891X_ *)psg;

	if (!BDIR && !BC1) {
		// Inactive
		return;
	}

	if (BDIR && BC1) {
		// Latch Address
		psg_->address = *D & 0xf;
		return;
	}

	if (!BDIR && BC1) {
		// Read
		switch (psg_->address) {
		case 0xe: // 8-BIT PARALLEL I/O on Port A
			if (psg_->InOA) {
				psg_->regs[0xe] = psg->a.in_sink;
			}
			break;

		case 0xf: // 8-BIT PARALLEL I/O on Port B
			if (psg_->InOB) {
				psg_->regs[0xf] = psg->b.in_sink;
			}
			break;

		default:
			break;
		}
		*D = psg_->regs[psg_->address];
		return;
	}

	// Write (BDIR && !BC1)
	psg_->regs[psg_->address] = *D;
	update_reg(psg_, psg_->address);
}

float ay891x_get_audio(void *sptr, uint32_t tick, int nframes, float *buf) {
	struct AY891X_ *psg_ = sptr;

	int nticks = psg_->nticks + tick_delta(tick, psg_->last_fragment_tick);
	psg_->last_fragment_tick = tick;

	float output = psg_->filter->output;
	float new_output = output;

	// if previous call overran
	if (psg_->overrun && nframes > 0) {
		if (buf) {
			*(buf++) = output;
		}
		nframes--;
		psg_->overrun = 0;
	}

	while (nticks > 0) {

		// framerate will *always* be less than refrate, so this is a
		// simple test.  allow for 1 overrun sample.
		psg_->frameerror += psg_->framerate;
		if (psg_->frameerror >= psg_->refrate) {
			psg_->frameerror -= psg_->refrate;
			if (nframes > 0) {
				if (buf) {
					*(buf++) = output;
				}
				nframes--;
			} else {
				psg_->overrun = 1;
			}
		}

		// tickrate may be higher than refrate: calculate remainder.
		psg_->tickerror += psg_->tickrate;
		int dtick = psg_->tickerror / psg_->refrate;
		if (dtick > 0) {
			nticks -= dtick;
			psg_->tickerror -= (dtick * psg_->refrate);
		}

		// noise generator
		psg_->noise_counter--;
		if (psg_->noise_counter <= 0) {
			psg_->noise_counter = psg_->noise_period;
			// 17-bit LFSR.  According to [deathsoft], shift in bit
			// is bits 16 and 13 XORed, ORed with what looks like a
			// parity calculation.  Including the parity gives it
			// way too short a period, so I've omitted it here, and
			// the result seems...  noisy.
			unsigned shift_in = ((psg_->noise_lfsr ^ (psg_->noise_lfsr >> 3)) & 1) << 16;
			psg_->noise_lfsr = shift_in | (psg_->noise_lfsr >> 1);
			psg_->noise_state = psg_->noise_lfsr & 1;
		}

		// tone generators A, B, C
		for (int c = 0; c < 3; c++) {
			if (--psg_->tone_counter[c] == 0) {
				psg_->tone_counter[c] = psg_->tone_period[c];
				psg_->tone_state[c] = !psg_->tone_state[c];
			}
			// mix tone with noise
			_Bool state = (psg_->tone_enable[c] && psg_->tone_state[c]) ||
			              (psg_->noise_state && psg_->noise_enable[c]);
			if (psg_->envelope_mode[c]) {
				unsigned level = state ? psg_->envelope_level : 0;
				psg_->level[c] = amplitude[level];
			} else {
				psg_->level[c] = psg_->amplitude[c][state];
			}
		}

		// envelope
		psg_->envelope_counter--;
		if (psg_->envelope_counter <= 0) {
			psg_->envelope_counter = psg_->envelope_period;
			if (psg_->envelope_att) {
				if (psg_->envelope_level == 15) {
					if (psg_->envelope_cont) {
						if (psg_->envelope_hold) {
							if (psg_->envelope_alt) {
								psg_->envelope_level = 0;
								psg_->envelope_att = 0;
							} else {
								psg_->envelope_level = 15;
							}
						} else {
							if (psg_->envelope_alt) {
								psg_->envelope_att = 0;
							} else {
								psg_->envelope_level = 0;
							}
						}
					}
				} else {
					psg_->envelope_level++;
				}
			} else {
				if (psg_->envelope_level == 0) {
					if (psg_->envelope_cont) {
						if (psg_->envelope_hold) {
							if (psg_->envelope_alt) {
								psg_->envelope_level = 15;
								psg_->envelope_att = 1;
							} else {
								psg_->envelope_level = 0;
							}
						} else {
							if (psg_->envelope_alt) {
								psg_->envelope_att = 1;
							} else {
								psg_->envelope_level = 15;
							}
						}
					} else {
						psg_->envelope_level = 0;
					}
				} else {
					psg_->envelope_level--;
				}
			}
		}

		// sum the output channels
		new_output = psg_->level[0] + psg_->level[1] + psg_->level[2];

		output = filter_iir_apply(psg_->filter, new_output);
	}

	psg_->nticks = nticks;

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
