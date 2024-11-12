/** \file
 *
 *  \brief Dragon sound interface.
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
 *  Audio modules provide a buffer to write into.  Sound interface provides
 *  Dragon/CoCo-specific means to write to it.
 */

#include "top-config.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "delegate.h"
#include "pl-endian.h"
#include "xalloc.h"

#include "events.h"
#include "logging.h"
#include "messenger.h"
#include "module.h"
#include "sound.h"
#include "tape.h"
#include "xroar.h"

static void flush_buffer(void *sptr);

struct sound_interface_private {

	struct sound_interface public;

	// Messenger client id
	int msgr_client_id;

	// Describes the mix & output buffers:
	unsigned buffer_nframes;
	float *mix_buffer;  // mix buffer
	int output_nchannels;
	enum sound_fmt output_fmt;
	void *output_buffer;  // final output may not be floats
	_Bool output_buffer_is_silent;

	// Current index into the buffer
	unsigned buffer_frame;

	// Track error dividing frames by ticks.
	int frameerror;
	event_ticks last_cycle;

	// Similarly track error dividing buffer by ticks.
	int buferror;

	// Schedule periodic flushes to the audio output module.
	struct event flush_event;

	float dac_level;
	float tape_level;

	// Audio circuit state
	struct {
		// Single-bit sound
		_Bool sbs_enabled;
		_Bool sbs_level;
		// Analogue multiplexer
		_Bool mux_enabled;
		unsigned mux_source;
		// External audio, potentially stereo
		float external[2];
	} current, next;

	// Inputs to audio mux.  Suitable for mixing to the output buffer, so
	// may have been filtered.
	float *mux_input[5];
	float *non_muxed_output;

	// The unfiltered "current" values of those inputs.  Suitable for
	// feeding back to the single bit sound pin as an input.
	float mux_input_raw[5];

	// Scale & offset to apply to the audio mux.  Intended to simulate the
	// changing characteristics when enabling or disabling single-bit
	// sound.
	float mux_gain;
	float bus_offset;

	// Last level seen on audio bus.
	float bus_level;

	// Overall gain to output buffer.  Computed by set_gain() or
	// set_volume().  Defaults to -3 dBFS.
	float gain;

};

enum sound_source {
	SOURCE_DAC,
	SOURCE_TAPE,
	SOURCE_CART,
	SOURCE_AY,  // Dragon Professional only
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
#define MAX_V (4.70)

// TODO: cart levels & gain are assumed here.

// Source gains
static const float source_gain_v[NUM_SOURCES][3] = {
	{ 4.50/MAX_V, 2.84/MAX_V, 3.40/MAX_V },  // DAC
	{ 0.50/MAX_V, 0.40/MAX_V, 0.50/MAX_V },  // Tape
	{ 4.70/MAX_V, 2.84/MAX_V, 3.40/MAX_V },  // Cart
	{ 4.70/MAX_V, 2.84/MAX_V, 3.40/MAX_V },  // AY
	{ 0.00/MAX_V, 0.00/MAX_V, 0.00/MAX_V },  // None
	{ 0.00/MAX_V, 0.00/MAX_V, 0.00/MAX_V }   // Single-bit
};

// Source offsets
static const float source_offset_v[NUM_SOURCES][3] = {
	{ 0.20/MAX_V, 0.18/MAX_V, 1.30/MAX_V },  // DAC
	{ 2.05/MAX_V, 1.60/MAX_V, 2.35/MAX_V },  // Tape
	{ 0.00/MAX_V, 0.18/MAX_V, 1.30/MAX_V },  // Cart
	{ 0.00/MAX_V, 0.18/MAX_V, 1.30/MAX_V },  // AY
	{ 0.00/MAX_V, 0.00/MAX_V, 0.00/MAX_V },  // None
	{ 0.00/MAX_V, 0.00/MAX_V, 3.90/MAX_V }   // Single-bit
};

static void sound_ui_set_gain(void *, int tag, void *smsg);

struct sound_interface *sound_interface_new(void *buf, enum sound_fmt fmt, unsigned rate,
					    unsigned nchannels, unsigned nframes) {
	struct sound_interface_private *snd = xmalloc(sizeof(*snd));
	*snd = (struct sound_interface_private){0};
	struct sound_interface *sndp = &snd->public;

	// Register with messenger
	snd->msgr_client_id = messenger_client_register();

	ui_messenger_preempt_group(snd->msgr_client_id, ui_tag_gain, MESSENGER_NOTIFY_DELEGATE(sound_ui_set_gain, snd));

	snd->gain = 0.7;  // -3dBFS

	_Bool fmt_big_endian = 1;

	if (nchannels < 1 || nchannels > 2) {
		LOG_MOD_WARN("sound", "invalid number of audio channels");
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
		LOG_DEBUG(1, "floating point, ");
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

	sndp->framerate = rate;
	snd->output_buffer = buf;
	if (fmt == SOUND_FMT_FLOAT) {
		// No need to convert floats, point mix buffer at output buffer.
		snd->mix_buffer = buf;
	} else {
		// Otherwise we need a staging area to mix float data.
		snd->mix_buffer = xmalloc(nframes * nchannels * sizeof(float));
	}
	snd->buffer_nframes = nframes;
	snd->output_fmt = fmt;
	snd->output_nchannels = nchannels;

	snd->current.mux_source = 0;
	snd->next.mux_source = 0;
	for (unsigned i = 0; i < 5; i++) {
		snd->mux_input[i] = xmalloc((nframes + 1) * sizeof(float));
		for (unsigned j = 0; j < (nframes + 1); j++) {
			snd->mux_input[i][j] = 0.0;
		}
	}
	snd->non_muxed_output = xmalloc((nframes + 1) * sizeof(float));
	for (unsigned j = 0; j < (nframes + 1); j++) {
		snd->non_muxed_output[j] = 0.0;
	}

	snd->last_cycle = event_current_tick;

	event_init(&snd->flush_event, DELEGATE_AS0(void, flush_buffer, snd));
	snd->flush_event.at_tick = event_current_tick;
	// process zero frames, but set up buffer flusher:
	flush_buffer(snd);

	return &snd->public;
}

void sound_interface_free(struct sound_interface *sndp) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	messenger_client_unregister(snd->msgr_client_id);
	event_dequeue(&snd->flush_event);
	if (snd->output_fmt != SOUND_FMT_FLOAT) {
		free(snd->mix_buffer);
	}
	free(snd->non_muxed_output);
	for (unsigned i = 0; i < 5; i++) {
		free(snd->mux_input[i]);
	}
	free(snd);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// convert buffer to desired output format and send it to audio module
static void send_buffer(struct sound_interface_private *snd) {
	int nsamples = snd->output_nchannels * snd->buffer_nframes;
	if (snd->output_buffer && snd->mix_buffer) {
		float *input = snd->mix_buffer;
		switch (snd->output_fmt) {
		case SOUND_FMT_U8: {
			int8_t *output = snd->output_buffer;
			for (int i = nsamples; i; i--)
				*(output++) = *(input++) * 0x7f + 0x80;
		} break;
		case SOUND_FMT_S8: {
			uint8_t *output = snd->output_buffer;
			for (int i = nsamples; i; i--)
				*(output++) = *(input++) * 0x7f;
		} break;
		case SOUND_FMT_S16_HE: {
			int16_t *output = snd->output_buffer;
			for (int i = nsamples; i; i--)
				*(output++) = *(input++) * 0x7fff;
		} break;
		case SOUND_FMT_S16_SE: {
			uint8_t *output = snd->output_buffer;
			for (int i = nsamples; i; i--) {
				int16_t v = *(input++) * 0x7fff;
				*(output++) = *((uint8_t *)&v+1);
				*(output++) = *(uint8_t *)&v;
			}
		} break;
		case SOUND_FMT_FLOAT: {
			// For float, mix buffer is pointed directly to output
			// buffer, as no conversion is necessary.
		} break;
		default:
			break;
		}
	}
	snd->output_buffer = DELEGATE_CALL(snd->public.write_buffer, snd->output_buffer);
	if (snd->output_fmt == SOUND_FMT_FLOAT) {
		// No need to convert floats, point mix buffer at output buffer.
		snd->mix_buffer = snd->output_buffer;
	}
	snd->buffer_frame = 0;
}

// Send "silence" if audio module needs to be kept up to date with queued audio

void sound_send_silence(struct sound_interface *sndp) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	// Only if the audio module needs it:
	if (!DELEGATE_DEFINED(snd->public.write_silence))
		return;

	if (snd->output_buffer && !snd->output_buffer_is_silent) {
		// Fill the output buffer with copies of its last sample
		int nsamples = snd->output_nchannels * snd->buffer_nframes;
		switch (snd->output_fmt) {
		case SOUND_FMT_U8:
		case SOUND_FMT_S8: {
			uint8_t *output = snd->output_buffer;
			uint8_t v = output[nsamples - 1];
			for (int i = nsamples - 1; i; i--)
				*(output++) = v;
		} break;
		case SOUND_FMT_S16_HE:
		case SOUND_FMT_S16_SE: {
			uint16_t *output = snd->output_buffer;
			uint16_t v = output[nsamples - 1];
			for (int i = nsamples - 1; i; i--)
				*(output++) = v;
		} break;
		case SOUND_FMT_FLOAT: {
			float *output = snd->output_buffer;
			float v = output[nsamples - 1];
			for (int i = nsamples; i; i--)
				*(output++) = v;
		} break;
		default:
			break;
		}
		// Flag so we don't have to do this every time
		snd->output_buffer_is_silent = 1;
	}

	snd->output_buffer = DELEGATE_CALL(snd->public.write_silence, snd->output_buffer);
	if (snd->output_fmt == SOUND_FMT_FLOAT) {
		// No need to convert floats, point mix buffer at output buffer.
		snd->mix_buffer = snd->output_buffer;
	}
	snd->buffer_frame = 0;
}

// Fill sound buffer to current point in time, sending to audio module when full.

void sound_update(struct sound_interface *sndp) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;

	unsigned nframes = 0;
	int64_t elapsed = event_tick_delta(event_current_tick, snd->last_cycle);
	if (elapsed > 0) {
		int64_t fe = snd->frameerror + elapsed * sndp->framerate;
		nframes = fe / EVENT_TICK_RATE;
		fe -= nframes * EVENT_TICK_RATE;
		snd->frameerror = fe;
	}
	snd->last_cycle = event_current_tick;

	// TODO: add a flag to the delegates to indicate whether result is
	// used.  may save some calls to sample-rate conversion / low-pass
	// filtering.

	_Bool mux_enabled = snd->current.mux_enabled;
	unsigned mux_source = snd->current.mux_source;
	if (!mux_enabled) {
		mux_source = SOURCE_NONE;
	}

	// Always run external sources so they're up to date, even though we'll
	// only use one of them.
	if (DELEGATE_DEFINED(sndp->get_tape_audio)) {
		if (mux_source == SOURCE_TAPE && sndp->ratelimit) {
			snd->mux_input_raw[SOURCE_TAPE] = DELEGATE_CALL(sndp->get_tape_audio, event_current_tick, nframes, snd->mux_input[SOURCE_TAPE]);
		} else {
			snd->mux_input_raw[SOURCE_TAPE] = DELEGATE_CALL(sndp->get_tape_audio, event_current_tick, nframes, NULL);
			if (mux_source == SOURCE_TAPE) {
				mux_source = SOURCE_NONE;
			}
		}
	} else if (mux_source == SOURCE_TAPE) {
		// Fill tape buffer.  This functionality should be pushed out into the
		// get_tape_audio delegate.
		for (unsigned i = 0; i < nframes; i++) {
			snd->mux_input[SOURCE_TAPE][i] = snd->mux_input_raw[SOURCE_TAPE];
		}
		snd->mux_input_raw[SOURCE_TAPE] = snd->tape_level;
	}

	if (DELEGATE_DEFINED(sndp->get_cart_audio)) {
		if (mux_source == SOURCE_CART && sndp->ratelimit) {
			snd->mux_input_raw[SOURCE_CART] = DELEGATE_CALL(sndp->get_cart_audio, event_current_tick, nframes, snd->mux_input[SOURCE_CART]);
		} else {
			snd->mux_input_raw[SOURCE_CART] = DELEGATE_CALL(sndp->get_cart_audio, event_current_tick, nframes, NULL);
			if (mux_source == SOURCE_CART) {
				mux_source = SOURCE_NONE;
			}
		}
	} else {
		snd->mux_input_raw[SOURCE_CART] = 0.0;
	}

	if (DELEGATE_DEFINED(sndp->get_ay_audio)) {
		if (mux_source == SOURCE_AY && sndp->ratelimit) {
			snd->mux_input_raw[SOURCE_AY] = DELEGATE_CALL(sndp->get_ay_audio, event_current_tick, nframes, snd->mux_input[SOURCE_AY]);
		} else {
			snd->mux_input_raw[SOURCE_AY] = DELEGATE_CALL(sndp->get_ay_audio, event_current_tick, nframes, NULL);
			if (mux_source == SOURCE_AY) {
				mux_source = SOURCE_NONE;
			}
		}
	} else {
		snd->mux_input_raw[SOURCE_AY] = 0.0;
	}

	float *non_muxed_output = NULL;
	if (DELEGATE_DEFINED(sndp->get_non_muxed_audio)) {
		non_muxed_output = snd->non_muxed_output;
		DELEGATE_CALL(sndp->get_non_muxed_audio, event_current_tick, nframes, non_muxed_output);
	}

	// Only fill DAC buffer if it's selected
	if (mux_source == SOURCE_DAC) {
		for (unsigned i = 0; i < nframes; i++) {
			snd->mux_input[SOURCE_DAC][i] = snd->mux_input_raw[SOURCE_DAC];
		}
	}
	snd->mux_input_raw[SOURCE_DAC] = snd->dac_level;

	// Select appropriate mux output, or none
	float *mux_output = snd->mux_input[mux_source];

	// Mix audio, send when buffer full
	while (nframes > 0) {
		int count;
		if ((snd->buffer_frame + nframes) > snd->buffer_nframes)
			count = snd->buffer_nframes - snd->buffer_frame;
		else
			count = nframes;
		nframes -= count;
		if (snd->output_buffer && snd->mix_buffer) {
			float *ptr = (float *)snd->mix_buffer + snd->buffer_frame * snd->output_nchannels;
			for (int i = 0; i < count; i++) {
				float mix_sample = (*(mux_output++) * snd->mux_gain) + snd->bus_offset;
				if (non_muxed_output) {
					mix_sample += *(non_muxed_output++);
				}
				for (int j = 0; j < snd->output_nchannels; j++) {
					*(ptr++) = (mix_sample + snd->current.external[j]) * snd->gain;
				}
			}
		}
		snd->buffer_frame += count;
		if (snd->buffer_frame >= snd->buffer_nframes) {
			send_buffer(snd);
		}
		snd->output_buffer_is_silent = 0;
	}

	// Now that audio has been dealt with up to the current point in time,
	// update sources.

	snd->current.sbs_enabled = snd->next.sbs_enabled;
	snd->current.sbs_level = snd->next.sbs_level;
	snd->current.mux_enabled = snd->next.mux_enabled;
	snd->current.mux_source = snd->next.mux_source;

	// Copy external audio, downmix to mono if necessary.
	if (snd->output_nchannels == 1) {
		snd->current.external[0] = snd->next.external[0] + snd->next.external[1];
	} else {
		snd->current.external[0] = snd->next.external[0];
		snd->current.external[1] = snd->next.external[1];
	}

	// Different configurations of single-bit sound have different effects
	// on the mux output.  Gain & DC offset updated here.
	float mux_output_raw;
	if (snd->current.mux_enabled) {
		unsigned sindex = snd->current.sbs_enabled ? (snd->current.sbs_level ? 2 : 1) : 0;
		mux_output_raw = snd->mux_input_raw[snd->current.mux_source];
		snd->mux_gain = source_gain_v[snd->current.mux_source][sindex];
		snd->bus_offset = source_offset_v[snd->current.mux_source][sindex];
	} else {
		mux_output_raw = 0.0;
		if (snd->current.sbs_enabled) {
			unsigned sindex = snd->current.sbs_level ? 2 : 1;
			snd->mux_gain = 0.0;
			snd->bus_offset = source_offset_v[SOURCE_SINGLE_BIT][sindex];
		} else {
			snd->bus_offset = snd->bus_level;
		}
	}

	// Feed back bus level to single bit pin.
	snd->bus_level = (mux_output_raw * snd->mux_gain) + snd->bus_offset;
	DELEGATE_SAFE_CALL(snd->public.sbs_feedback, snd->current.sbs_enabled || snd->bus_level >= 0.3);

}

// Rate limit control
void sound_set_ratelimit(struct sound_interface *sndp, _Bool ratelimit) {
	sndp->ratelimit = ratelimit;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void sound_ui_set_gain(void *sptr, int tag, void *smsg) {
	struct sound_interface_private *snd = sptr;
	struct ui_state_message *uimsg = smsg;
	assert(tag == ui_tag_gain);
	if (uimsg->data) {
		float db = *(float *)uimsg->data;
		float v;
		int vi;
		if (db < -49.9) {
			v = 0.;
			vi = 0;
			db = -50.;
		} else {
			v = powf(10., db / 20.);
			vi = (int)(v * 100.);
		}
		snd->gain = v;
		uimsg->value = vi;
		*(float *)uimsg->data = db;
	} else {
		int vi = uimsg->value;
		float v;
		static float db;
		if (vi <= 0) {
			vi = 0;
			v = 0.;
			db = -50.;
		} else {
			if (vi > 200) {
				vi = 200;
			}
			v = (float)vi / 100.;
			db = log10f(v) * 20.;
		}
		snd->gain = v;
		uimsg->value = vi;
		uimsg->data = &db;
	}
}

void sound_set_sbs(struct sound_interface *sndp, _Bool enabled, _Bool level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	if (snd->next.sbs_enabled == enabled && snd->next.sbs_level == level)
		return;
	snd->next.sbs_enabled = enabled;
	snd->next.sbs_level = level;
	sound_update(sndp);
}

void sound_set_mux_enabled(struct sound_interface *sndp, _Bool enabled) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	if (snd->next.mux_enabled == enabled)
		return;
	snd->next.mux_enabled = enabled;
	sound_update(sndp);
}

void sound_set_mux_source(struct sound_interface *sndp, unsigned source) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	if (snd->next.mux_source == source)
		return;
	snd->next.mux_source = source;
	if (!snd->next.mux_enabled)
		return;
	sound_update(sndp);
}

void sound_set_dac_level(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->dac_level = level;
	if (snd->next.mux_enabled && snd->next.mux_source == SOURCE_DAC)
		sound_update(sndp);
}

void sound_set_tape_level(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->tape_level = level;
	if (snd->next.mux_enabled && snd->next.mux_source == SOURCE_TAPE)
		sound_update(sndp);
}

void sound_set_external_left(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->next.external[0] = level;
	sound_update(sndp);
}

void sound_set_external_right(struct sound_interface *sndp, float level) {
	struct sound_interface_private *snd = (struct sound_interface_private *)sndp;
	snd->next.external[1] = level;
	sound_update(sndp);
}

static void flush_buffer(void *sptr) {
	struct sound_interface_private *snd = sptr;
	struct sound_interface *sndp = &snd->public;
	sound_update(&snd->public);
	// calculate exact number of cycles to end of buffer
	int64_t elapsed_frames = snd->buffer_nframes - snd->buffer_frame;
	int64_t fe = snd->buferror + elapsed_frames * EVENT_TICK_RATE;
	unsigned nticks = fe / sndp->framerate;
	fe -= nticks * sndp->framerate;
	snd->buferror = fe;
	snd->flush_event.at_tick = snd->last_cycle + nticks;
	event_queue(&MACHINE_EVENT_LIST, &snd->flush_event);
}
