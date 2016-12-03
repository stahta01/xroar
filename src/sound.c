/*  Copyright 2003-2016 Ciaran Anscomb
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

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "delegate.h"
#include "pl-endian.h"
#include "xalloc.h"

#include "events.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "tape.h"
#include "xroar.h"

union sample_t {
	uint8_t as_int8[2];
	uint16_t as_int16[2];
	float as_float[2];
};

static void flush_frame(void *sptr);

/* XXX this still conflates general sound buffer management with
 * Dragon/CoCo-specific writing routines.  Need to separate the two. */

struct sound_interface_private {

	struct sound_interface public;

	struct event flush_event;

	/* Describes the buffer: */
	enum sound_fmt buffer_fmt;
	int buffer_nchannels;
	unsigned buffer_nframes;
	void *buffer;
	/* Current index into the buffer */
	unsigned buffer_frame;

	float output_level[2];
	union sample_t last_sample;
	event_ticks last_cycle;
	float ticks_per_frame;
	unsigned ticks_per_buffer;
	float error_f;

	// Computed by set_gain() or set_volume().  Defaults to -3 dBFS.
	unsigned gain;

	_Bool external_audio;

	_Bool sbs_enabled;
	_Bool sbs_level;
	_Bool mux_enabled;
	unsigned mux_source;
	float dac_level;
	float tape_level;
	float cart_level;
	float external_level[2];
};

enum sound_source {
	SOURCE_DAC,
	SOURCE_TAPE,
	SOURCE_CART,
	SOURCE_NONE,
	SOURCE_SINGLE_BIT,
	NUM_SOURCES
};

/* These are the absolute measured voltages on a real Dragon for audio output
 * for each source, each indicated by a scale and offset.  Getting these right
 * should mean that any transition of single bit or mux enable will produce the
 * right effect.  Primary index indicates source, secondary index is by:
 *
 * Secondary index into each array is by:
 * 2 - Single bit output enabled and high
 * 1 - Single bit output enabled and low
 * 0 - Single bit output disabled
 */

// Maximum measured voltage:
static const float full_scale_v = 4.7;

// Source gains
static const float source_gain_v[NUM_SOURCES][3] = {
	{ 4.5, 2.84, 3.4 },  // DAC
	{ 0.5, 0.4, 0.5 },  // Tape
	{ 0.0, 0.0, 0.0 },  // Cart
	{ 0.0, 0.0, 0.0 },  // None
	{ 0.0, 0.0, 0.0 }  // Single-bit
};

// Source offsets
static const float source_offset_v[NUM_SOURCES][3] = {
	{ 0.2, 0.18, 1.3 },  // DAC
	{ 2.05, 1.6, 2.35 },  // Tape
	{ 0.0, 0.0, 3.9 },  // Cart
	{ 0.0, 0.0, 0.01 },  // None
	{ 0.0, 0.0, 3.9 }  // Single-bit
};

struct sound_interface *sound_interface_new(void *buf, enum sound_fmt fmt, unsigned rate,
					    unsigned nchannels, unsigned nframes) {
	struct sound_interface_private *snd = xmalloc(sizeof(*snd));
	*snd = (struct sound_interface_private){0};

	snd->gain = 4935;  // -3 dBFS

	_Bool fmt_big_endian = 1;

	if (nchannels < 1 || nchannels > 2) {
		LOG_WARN("Invalid number of audio channels: disabling sound.");
		free(snd);
		return NULL;
	}

	if (fmt == SOUND_FMT_S16_BE) {
		fmt_big_endian = 1;
#if __BYTE_ORDER == __BIG_ENDIAN
			fmt = SOUND_FMT_S16_HE;
#else
			fmt = SOUND_FMT_S16_SE;
#endif
	} else if (fmt == SOUND_FMT_S16_LE) {
		fmt_big_endian = 0;
#if __BYTE_ORDER == __BIG_ENDIAN
			fmt = SOUND_FMT_S16_SE;
#else
			fmt = SOUND_FMT_S16_HE;
#endif
	} else if (fmt == SOUND_FMT_S16_HE) {
		fmt_big_endian = (__BYTE_ORDER == __BIG_ENDIAN);
	} else if (fmt == SOUND_FMT_S16_SE) {
		fmt_big_endian = !(__BYTE_ORDER == __BIG_ENDIAN);
	}

	(void)fmt_big_endian;  // suppress unused warning
	LOG_DEBUG(1, "\t");
	switch (fmt) {
	case SOUND_FMT_U8:
		LOG_DEBUG(1, "8-bit unsigned, ");
		break;
	case SOUND_FMT_S8:
		LOG_DEBUG(1, "8-bit signed, ");
		break;
	case SOUND_FMT_S16_HE:
	case SOUND_FMT_S16_SE:
		LOG_DEBUG(1, "16-bit signed %s-endian, ", fmt_big_endian ? "big" : "little" );
		break;
	case SOUND_FMT_FLOAT:
		LOG_DEBUG(1, "Floating point, ");
		break;
	case SOUND_FMT_NULL:
	default:
		fmt = SOUND_FMT_NULL;
		LOG_DEBUG(1, "No audio\n");
		break;
	}
	if (fmt != SOUND_FMT_NULL) {
		switch (nchannels) {
		case 1: LOG_DEBUG(1, "mono, "); break;
		case 2: LOG_DEBUG(1, "stereo, "); break;
		default: LOG_DEBUG(1, "%u channel, ", nchannels); break;
		}
		LOG_DEBUG(1, "%uHz\n", rate);
	}
	snd->output_level[0] = snd->output_level[1] = 0.0;

	snd->buffer = buf;
	snd->buffer_nframes = nframes;
	snd->buffer_fmt = fmt;
	snd->buffer_nchannels = nchannels;
	snd->ticks_per_frame = (float)EVENT_TICK_RATE / (float)rate;
	snd->ticks_per_buffer = snd->ticks_per_frame * nframes;
	snd->last_cycle = event_current_tick;

	event_init(&snd->flush_event, DELEGATE_AS0(void, flush_frame, snd));
	snd->flush_event.at_tick = event_current_tick + snd->ticks_per_buffer;
	event_queue(&MACHINE_EVENT_LIST, &snd->flush_event);

	return &snd->public;
}

void sound_interface_free(struct sound_interface *sndp) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	event_dequeue(&snd->flush_event);
	free(snd);
}

// -ve dB wrt 0dBFS
void sound_set_gain(struct sound_interface *sndp, double db) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	double v = pow(10., db / 20.);
	snd->gain = (unsigned)((32767. * v) / full_scale_v);
}

// linear scaling 0-100 (but allow up to 200)
void sound_set_volume(struct sound_interface *sndp, int v) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	if (v < 0) v = 0;
	if (v > 200) v = 200;
	snd->gain = (unsigned)((327.67 * (float)v) / full_scale_v);
}

static void fill_int8(struct sound_interface_private *snd, int nframes) {
	while (nframes > 0) {
		int count;
		if ((snd->buffer_frame + nframes) > snd->buffer_nframes)
			count = snd->buffer_nframes - snd->buffer_frame;
		else
			count = nframes;
		nframes -= count;
		if (snd->buffer) {
			uint8_t *ptr = (uint8_t *)snd->buffer + snd->buffer_frame * snd->buffer_nchannels;
			if (snd->buffer_nchannels == 1) {
				/* special case for single channel 8-bit */
				memset(ptr, snd->last_sample.as_int8[0], count);
			} else {
				for (int i = 0; i < count; i++) {
					for (int j = 0; j < snd->buffer_nchannels; j++) {
						*(ptr++) = snd->last_sample.as_int8[j];
					}
				}
			}
		}
		snd->buffer_frame += count;
		if (snd->buffer_frame >= snd->buffer_nframes) {
			snd->buffer = DELEGATE_CALL1(snd->public.write_buffer, snd->buffer);
			snd->buffer_frame = 0;
		}
	}
}

static void fill_int16(struct sound_interface_private *snd, int nframes) {
	while (nframes > 0) {
		int count;
		if ((snd->buffer_frame + nframes) > snd->buffer_nframes)
			count = snd->buffer_nframes - snd->buffer_frame;
		else
			count = nframes;
		nframes -= count;
		if (snd->buffer) {
			uint16_t *ptr = (uint16_t *)snd->buffer + snd->buffer_frame * snd->buffer_nchannels;
			for (int i = 0; i < count; i++) {
				for (int j = 0; j < snd->buffer_nchannels; j++) {
					*(ptr++) = snd->last_sample.as_int16[j];
				}
			}
		}
		snd->buffer_frame += count;
		if (snd->buffer_frame >= snd->buffer_nframes) {
			snd->buffer = DELEGATE_CALL1(snd->public.write_buffer, snd->buffer);
			snd->buffer_frame = 0;
		}
	}
}

static void fill_float(struct sound_interface_private *snd, int nframes) {
	while (nframes > 0) {
		int count;
		if ((snd->buffer_frame + nframes) > snd->buffer_nframes)
			count = snd->buffer_nframes - snd->buffer_frame;
		else
			count = nframes;
		nframes -= count;
		if (snd->buffer) {
			float *ptr = (float *)snd->buffer + snd->buffer_frame * snd->buffer_nchannels;
			for (int i = 0; i < count; i++) {
				for (int j = 0; j < snd->buffer_nchannels; j++) {
					*(ptr++) = snd->last_sample.as_float[j];
				}
			}
		}
		snd->buffer_frame += count;
		if (snd->buffer_frame >= snd->buffer_nframes) {
			snd->buffer = DELEGATE_CALL1(snd->public.write_buffer, snd->buffer);
			snd->buffer_frame = 0;
		}
	}
}

static void null_frames(struct sound_interface_private *snd, int nframes) {
	snd->buffer_frame += nframes;
	while (snd->buffer_frame >= snd->buffer_nframes) {
		snd->buffer = DELEGATE_CALL1(snd->public.write_buffer, snd->buffer);
		snd->buffer_frame -= snd->buffer_nframes;
	}
}

/* Fill sound buffer to current point in time, call sound module's
 * update() function if buffer is full. */
void sound_update(struct sound_interface *sndp) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	unsigned elapsed = (event_current_tick - snd->last_cycle);
	unsigned nframes = 0;
	if (elapsed <= (UINT_MAX/2)) {
		float nframes_f = elapsed / snd->ticks_per_frame;
		nframes = nframes_f;
		snd->error_f += (nframes_f - nframes);
		unsigned error = snd->error_f;
		nframes += error;
		snd->error_f -= error;
	}

	/* Update output samples */
	for (int i = 0; i < snd->buffer_nchannels; i++) {
		int output = snd->output_level[i] * snd->gain;
		if (output > 32767) {
			output = 32767;
		}
		if (output < -32767) {
			output = -32767;
		}
		switch (snd->buffer_fmt) {
		case SOUND_FMT_U8:
			snd->last_sample.as_int8[i] = (output >> 8) + 0x80;
			break;
		case SOUND_FMT_S8:
			snd->last_sample.as_int8[i] = output >> 8;
			break;
		case SOUND_FMT_S16_HE:
			snd->last_sample.as_int16[i] = output;
			break;
		case SOUND_FMT_S16_SE:
			snd->last_sample.as_int16[i] = (output & 0xff) << 8 | ((output >> 8) & 0xff);
			break;
		case SOUND_FMT_FLOAT:
			snd->last_sample.as_float[i] = (float)output / 32767.;
			break;
		default:
			break;
		}
	}

	/* Fill buffer */
	switch (snd->buffer_fmt) {
	case SOUND_FMT_U8:
	case SOUND_FMT_S8:
		fill_int8(snd, nframes);
		break;
	case SOUND_FMT_S16_HE:
	case SOUND_FMT_S16_SE:
		fill_int16(snd, nframes);
		break;
	case SOUND_FMT_FLOAT:
		fill_float(snd, nframes);
		break;
	default:
		null_frames(snd, nframes);
		break;
	}

	snd->last_cycle = event_current_tick;

	/* Mix internal sound sources to bus */
	float bus_level = 0.0;
	unsigned sindex = snd->sbs_enabled ? (snd->sbs_level ? 2 : 1) : 0;
	enum sound_source source;
	if (snd->mux_enabled) {
		source = snd->mux_source;
		switch (source) {
		case SOURCE_DAC:
			bus_level = snd->dac_level;
			break;
		case SOURCE_TAPE:
			bus_level = snd->tape_level;
			break;
		default:
		case SOURCE_CART:
		case SOURCE_NONE:
			bus_level = snd->cart_level;
			break;
		}
	} else {
		source = SOURCE_SINGLE_BIT;
		bus_level = 0.0;
	}
	bus_level = (bus_level * source_gain_v[source][sindex])
			 + source_offset_v[source][sindex];

	/* Feed back bus level to single bit pin */
	DELEGATE_SAFE_CALL1(snd->public.sbs_feedback, snd->sbs_enabled || bus_level >= 1.414);

	/* Mix bus & external sound */
	if (snd->external_audio) {
		snd->output_level[0] = snd->external_level[0]*full_scale_v + bus_level;
		snd->output_level[1] = snd->external_level[1]*full_scale_v + bus_level;
	} else {
		snd->output_level[0] = bus_level;
		snd->output_level[1] = bus_level;
	}
	/* Downmix to mono */
	if (snd->buffer_nchannels == 1)
		snd->output_level[0] = snd->output_level[0] + snd->output_level[1];

}

void sound_enable_external(struct sound_interface *sndp) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->external_audio = 1;
}

void sound_disable_external(struct sound_interface *sndp) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->external_audio = 0;
}

void sound_set_sbs(struct sound_interface *sndp, _Bool enabled, _Bool level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	if (snd->sbs_enabled == enabled && snd->sbs_level == level)
		return;
	snd->sbs_enabled = enabled;
	snd->sbs_level = level;
	sound_update(sndp);
}

void sound_set_mux_enabled(struct sound_interface *sndp, _Bool enabled) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	if (snd->mux_enabled == enabled)
		return;
	snd->mux_enabled = enabled;
	if (xroar_cfg.fast_sound)
		return;
	sound_update(sndp);
}

void sound_set_mux_source(struct sound_interface *sndp, unsigned source) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	if (snd->mux_source == source)
		return;
	snd->mux_source = source;
	if (!snd->mux_enabled)
		return;
	if (xroar_cfg.fast_sound)
		return;
	sound_update(sndp);
}

void sound_set_dac_level(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->dac_level = level;
	if (snd->mux_enabled && snd->mux_source == SOURCE_DAC)
		sound_update(sndp);
}

void sound_set_tape_level(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->tape_level = level;
	if (snd->mux_enabled && snd->mux_source == SOURCE_TAPE)
		sound_update(sndp);
}

void sound_set_cart_level(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->cart_level = level;
	if (snd->mux_enabled && snd->mux_source == SOURCE_CART)
		sound_update(sndp);
}

void sound_set_external_left(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->external_level[0] = level;
	if (snd->external_audio)
		sound_update(sndp);
}

void sound_set_external_right(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->external_level[1] = level;
	if (snd->external_audio)
		sound_update(sndp);
}

static void flush_frame(void *sptr) {
	struct sound_interface_private *snd = sptr;
	sound_update(&snd->public);
	snd->flush_event.at_tick += snd->ticks_per_buffer;
	event_queue(&MACHINE_EVENT_LIST, &snd->flush_event);
}
