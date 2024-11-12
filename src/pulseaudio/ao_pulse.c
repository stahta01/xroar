/** \file
 *
 *  \brief PulseAudio sound module.
 *
 *  \copyright Copyright 2010-2024 Ciaran Anscomb
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/error.h>
#include <pulse/gccmacro.h>
#include <pulse/simple.h>

#include "xalloc.h"

#include "ao.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static void *new(void *cfg);

struct module ao_pulse_module = {
	.name = "pulse", .description = "PulseAudio",
	.new = new,
};

struct ao_pulse_interface {
	struct ao_interface public;

	pa_simple *pa;
	size_t fragment_nbytes;
	void *audio_buffer;
};

static void ao_pulse_free(void *sptr);
static void *ao_pulse_write_buffer(void *sptr, void *buffer);

static void *new(void *cfg) {
	(void)cfg;
	struct ao_pulse_interface *aopulse = xmalloc(sizeof(*aopulse));
	*aopulse = (struct ao_pulse_interface){0};
	struct ao_interface *ao = &aopulse->public;

	ao->free = DELEGATE_AS0(void, ao_pulse_free, ao);

	const char *device = xroar.cfg.ao.device;
	pa_sample_spec ss = {
		.format = PA_SAMPLE_S16NE,
	};
	pa_buffer_attr ba = {
		.maxlength = -1,
		.minreq = -1,
		.prebuf = -1,
	};
	int error;

	unsigned rate = (xroar.cfg.ao.rate > 0) ? xroar.cfg.ao.rate : 48000;
	unsigned nchannels = xroar.cfg.ao.channels;
	if (nchannels < 1 || nchannels > 2)
		nchannels = 2;
	ss.rate = rate;
	ss.channels = nchannels;

	enum sound_fmt request_fmt;
	unsigned sample_nbytes;
	if (ss.format == PA_SAMPLE_U8) {
		request_fmt = SOUND_FMT_U8;
		sample_nbytes = 1;
	} else if (ss.format & PA_SAMPLE_S16LE) {
		request_fmt = SOUND_FMT_S16_LE;
		sample_nbytes = 2;
	} else if (ss.format & PA_SAMPLE_S16BE) {
		request_fmt = SOUND_FMT_S16_BE;
		sample_nbytes = 2;
	} else {
		LOG_MOD_WARN("pulse", "unhandled audio format");
		goto failed;
	}
	unsigned frame_nbytes = sample_nbytes * nchannels;

	/* PulseAudio abstracts a bit further, so fragments don't really come
	 * into it.  Use any specified value as "tlength". */

	int fragment_nframes;
	if (xroar.cfg.ao.fragment_ms > 0) {
		fragment_nframes = (rate * xroar.cfg.ao.fragment_ms) / 1000;
	} else if (xroar.cfg.ao.fragment_nframes > 0) {
		fragment_nframes = xroar.cfg.ao.fragment_nframes;
	} else if (xroar.cfg.ao.buffer_ms > 0) {
		fragment_nframes = (rate * xroar.cfg.ao.buffer_ms) / 1000;
	} else if (xroar.cfg.ao.buffer_nframes > 0) {
		fragment_nframes = xroar.cfg.ao.buffer_nframes;
	} else {
		fragment_nframes = 1024;
	}

	int nfragments = 2;
	if (xroar.cfg.ao.fragments > 0) {
		nfragments = xroar.cfg.ao.fragments;
	}
	ba.tlength = fragment_nframes * nfragments * frame_nbytes;

	aopulse->pa = pa_simple_new(NULL, "XRoar", PA_STREAM_PLAYBACK, device,
	                   "output", &ss, NULL, &ba, &error);
	if (!aopulse->pa) {
		LOG_MOD_ERROR("pulse", "failed to initialise: %s\n", pa_strerror(error));
		goto failed;
	}

	aopulse->fragment_nbytes = fragment_nframes * sample_nbytes * nchannels;
	aopulse->audio_buffer = xmalloc(aopulse->fragment_nbytes);
	ao->sound_interface = sound_interface_new(aopulse->audio_buffer, request_fmt, rate, nchannels, fragment_nframes);
	if (!ao->sound_interface) {
		LOG_MOD_ERROR("pulse", "failed to initialise: XRoar internal error\n");
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_pulse_write_buffer, ao);
	LOG_DEBUG(1, "\t%d frags * %d frames/frag = %d frames buffer (%ums)\n", nfragments, fragment_nframes, nfragments * fragment_nframes, (nfragments * fragment_nframes * 1000) / rate);
	return aopulse;

failed:
	if (aopulse->audio_buffer)
		free(aopulse->audio_buffer);
	free(aopulse);
	return NULL;
}

static void ao_pulse_free(void *sptr) {
	struct ao_pulse_interface *aopulse = sptr;

	int error;
	pa_simple_flush(aopulse->pa, &error);
	pa_simple_free(aopulse->pa);
	sound_interface_free(aopulse->public.sound_interface);
	free(aopulse->audio_buffer);
	free(aopulse);
}

static void *ao_pulse_write_buffer(void *sptr, void *buffer) {
	struct ao_pulse_interface *aopulse = sptr;

	int error;
	if (!aopulse->public.sound_interface->ratelimit)
		return buffer;
	pa_simple_write(aopulse->pa, buffer, aopulse->fragment_nbytes, &error);
	return buffer;
}
