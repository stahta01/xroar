/** \file
 *
 *  \brief SDL2 sound module.
 *
 *  \copyright Copyright 2015-2024 Ciaran Anscomb
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
 *  We now use SDL's queued audio interface.  When writing, we query how much
 *  is left in the queue, and if it's too much we wait a while for the queue to
 *  drain.
 */

#include "top-config.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_thread.h>

#include "c-strcase.h"
#include "xalloc.h"

#include "ao.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static void *new(void *cfg);

struct module ao_sdl_module = {
	.name = "sdl", .description = "SDL2 audio",
	.new = new,
};

struct ao_sdl2_interface {
	struct ao_interface public;

	SDL_AudioDeviceID device;
	SDL_AudioSpec audiospec;

	void *callback_buffer;
	_Bool shutting_down;

	unsigned frame_nbytes;

	unsigned nfragments;
	unsigned fragment_nbytes;

	// Now that the WASAPI driver isn't causing issues in Windows, we
	// can use SDL's queued audio interface for all builds.
	void *fragment_buffer;
	Uint32 qbytes_threshold;
	unsigned qdelay_divisor;
};

static void ao_sdl2_free(void *sptr);
static void *ao_sdl2_write_buffer(void *sptr, void *buffer);
#ifndef HAVE_WASM
static void *ao_sdl2_write_silence(void *sptr, void *buffer);
#endif

static void *new(void *cfg) {
	(void)cfg;
	SDL_AudioSpec desired;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
		LOG_MOD_SUB_ERROR("sdl", "audio", "failed to initialise\n");
		return NULL;
	}

	const char *driver_name = SDL_GetCurrentAudioDriver();
	LOG_MOD_SUB_DEBUG(3, "sdl", "audio", "using audio driver '%s'\n", driver_name);

	struct ao_sdl2_interface *aosdl = xmalloc(sizeof(*aosdl));
	*aosdl = (struct ao_sdl2_interface){0};
	struct ao_interface *ao = &aosdl->public;

	ao->free = DELEGATE_AS0(void, ao_sdl2_free, ao);

#ifdef HAVE_WASM
	// Lower default samplerate for the WebAssembly build
	unsigned rate = 22050;
#else
	unsigned rate = 48000;
#endif
	unsigned nchannels = 2;
	unsigned fragment_nframes;
	unsigned buffer_nframes;
	unsigned sample_nbytes;
	enum sound_fmt sample_fmt;

	if (xroar.cfg.ao.rate > 0)
		rate = xroar.cfg.ao.rate;

	if (xroar.cfg.ao.channels >= 1 && xroar.cfg.ao.channels <= 2)
		nchannels = xroar.cfg.ao.channels;

	aosdl->nfragments = 3;
	if (xroar.cfg.ao.fragments >= 0 && xroar.cfg.ao.fragments <= 64)
		aosdl->nfragments = xroar.cfg.ao.fragments;

	if (aosdl->nfragments == 0)
		aosdl->nfragments++;

	unsigned buf_nfragments = aosdl->nfragments ? aosdl->nfragments : 1;

	if (xroar.cfg.ao.fragment_ms > 0) {
		fragment_nframes = (rate * xroar.cfg.ao.fragment_ms) / 1000;
	} else if (xroar.cfg.ao.fragment_nframes > 0) {
		fragment_nframes = xroar.cfg.ao.fragment_nframes;
	} else {
		if (xroar.cfg.ao.buffer_ms > 0) {
			buffer_nframes = (rate * xroar.cfg.ao.buffer_ms) / 1000;
		} else if (xroar.cfg.ao.buffer_nframes > 0) {
			buffer_nframes = xroar.cfg.ao.buffer_nframes;
		} else {
			buffer_nframes = 1024 * buf_nfragments;
		}
		fragment_nframes = buffer_nframes / buf_nfragments;
	}

	desired.freq = rate;
	desired.channels = nchannels;
	desired.samples = fragment_nframes;
	desired.callback = NULL;
	desired.userdata = aosdl;

	switch (xroar.cfg.ao.format) {
	case SOUND_FMT_U8:
		desired.format = AUDIO_U8;
		break;
	case SOUND_FMT_S8:
		desired.format = AUDIO_S8;
		break;
	case SOUND_FMT_S16_BE:
		desired.format = AUDIO_S16MSB;
		break;
	case SOUND_FMT_S16_LE:
		desired.format = AUDIO_S16LSB;
		break;
	case SOUND_FMT_S16_HE:
		desired.format = AUDIO_S16SYS;
		break;
	case SOUND_FMT_S16_SE:
		if (AUDIO_S16SYS == AUDIO_S16LSB)
			desired.format = AUDIO_S16MSB;
		else
			desired.format = AUDIO_S16LSB;
		break;
	case SOUND_FMT_FLOAT:
	default:
		desired.format = AUDIO_F32SYS;
		break;
	}

	// First allow format changes, if format not explicitly specified
	int allowed_changes = 0;
	if (xroar.cfg.ao.format == SOUND_FMT_NULL) {
		allowed_changes = SDL_AUDIO_ALLOW_FORMAT_CHANGE;
	}
	aosdl->device = SDL_OpenAudioDevice(xroar.cfg.ao.device, 0, &desired, &aosdl->audiospec, allowed_changes);

	// Check the format is supported
	if (aosdl->device == 0) {
		LOG_MOD_SUB_DEBUG(3, "sdl", "audio", "first open audio failed: %s\n", SDL_GetError());
	} else {
		switch (aosdl->audiospec.format) {
		case AUDIO_U8: case AUDIO_S8:
		case AUDIO_S16LSB: case AUDIO_S16MSB:
		case AUDIO_F32SYS:
			break;
		default:
			LOG_MOD_SUB_DEBUG(3, "sdl", "audio", "first open audio returned unknown format: retrying\n");
			SDL_CloseAudioDevice(aosdl->device);
			aosdl->device = 0;
			break;
		}
	}

	// One last try, allowing any changes.  Check the format is sensible later.
	if (aosdl->device == 0) {
		aosdl->device = SDL_OpenAudioDevice(xroar.cfg.ao.device, 0, &desired, &aosdl->audiospec, SDL_AUDIO_ALLOW_ANY_CHANGE);
		if (aosdl->device == 0) {
			LOG_MOD_SUB_ERROR("sdl", "audio", "failed to open audio: %s\n", SDL_GetError());
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
			free(aosdl);
			return NULL;
		}
	}

	rate = aosdl->audiospec.freq;
	nchannels = aosdl->audiospec.channels;
	fragment_nframes = aosdl->audiospec.samples;

	switch (aosdl->audiospec.format) {
		case AUDIO_U8: sample_fmt = SOUND_FMT_U8; sample_nbytes = 1; break;
		case AUDIO_S8: sample_fmt = SOUND_FMT_S8; sample_nbytes = 1; break;
		case AUDIO_S16LSB: sample_fmt = SOUND_FMT_S16_LE; sample_nbytes = 2; break;
		case AUDIO_S16MSB: sample_fmt = SOUND_FMT_S16_BE; sample_nbytes = 2; break;
		case AUDIO_F32SYS: sample_fmt = SOUND_FMT_FLOAT; sample_nbytes = 4; break;
		default:
			LOG_MOD_SUB_WARN("sdl", "audio", "unhandled audio format 0x%x\n", aosdl->audiospec.format);
			goto failed;
	}

	buffer_nframes = fragment_nframes * buf_nfragments;
	aosdl->frame_nbytes = nchannels * sample_nbytes;
	aosdl->fragment_nbytes = fragment_nframes * aosdl->frame_nbytes;

	// If any more than (n-1) fragments (measured in bytes) are in
	// the queue, we will wait.
	aosdl->qbytes_threshold = aosdl->fragment_nbytes * (aosdl->nfragments - 1);
	aosdl->qdelay_divisor = aosdl->frame_nbytes * rate;

	aosdl->shutting_down = 0;
	aosdl->callback_buffer = NULL;

	aosdl->fragment_buffer = xmalloc(aosdl->fragment_nbytes);
	uint8_t zero = (aosdl->audiospec.format == AUDIO_U8) ? 0x80 : 0x00;
	memset(aosdl->fragment_buffer, zero, aosdl->fragment_nbytes);

	ao->sound_interface = sound_interface_new(NULL, sample_fmt, rate, nchannels, fragment_nframes);
	if (!ao->sound_interface) {
		LOG_MOD_SUB_ERROR("sdl", "audio", "failed to initialise: XRoar internal error\n");
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_sdl2_write_buffer, ao);
#ifndef HAVE_WASM
	ao->sound_interface->write_silence = DELEGATE_AS1(voidp, voidp, ao_sdl2_write_silence, ao);
#endif
	LOG_DEBUG(1, "\t%u frags * %u frames/frag = %u frames buffer (%.1fms)\n", buf_nfragments, fragment_nframes, buffer_nframes, (float)(buffer_nframes * 1000) / rate);

	SDL_PauseAudioDevice(aosdl->device, 0);
	return aosdl;

failed:
	if (aosdl) {
		SDL_CloseAudioDevice(aosdl->device);
		if (aosdl->fragment_buffer) {
			free(aosdl->fragment_buffer);
		}
		free(aosdl);
	}
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	return NULL;
}

static void ao_sdl2_free(void *sptr) {
	struct ao_sdl2_interface *aosdl = sptr;
	aosdl->shutting_down = 1;

	// no more audio
	SDL_PauseAudioDevice(aosdl->device, 1);

	SDL_CloseAudioDevice(aosdl->device);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);

	sound_interface_free(aosdl->public.sound_interface);

	if (aosdl->nfragments > 0) {
		free(aosdl->fragment_buffer);
	}

	free(aosdl);
}

static void *ao_sdl2_write_buffer(void *sptr, void *buffer) {
	struct ao_sdl2_interface *aosdl = sptr;
	(void)buffer;

	if (!aosdl->public.sound_interface->ratelimit) {
		return NULL;
	}

	// For WebAssembly, if there's too much audio already in the queue,
	// just purge it - doesn't happen much, due to the way Wasm runs.
	// Otherwise wait an appropriate amount of time for the queue to drain.

	Uint32 qbytes = SDL_GetQueuedAudioSize(aosdl->device);
	if (qbytes > aosdl->qbytes_threshold) {
#ifndef HAVE_WASM
		int ms = ((qbytes - aosdl->qbytes_threshold) * 1000) / aosdl->qdelay_divisor;
		if (ms >= 10) {
			SDL_Delay(ms);
		}
#else
		return NULL;
#endif
	}
	SDL_QueueAudio(aosdl->device, aosdl->fragment_buffer, aosdl->fragment_nbytes);
	return aosdl->fragment_buffer;

}

#ifndef HAVE_WASM
static void *ao_sdl2_write_silence(void *sptr, void *buffer) {
	struct ao_sdl2_interface *aosdl = sptr;
	(void)buffer;

	Uint32 qbytes = SDL_GetQueuedAudioSize(aosdl->device);
	if (qbytes < aosdl->qbytes_threshold) {
		SDL_QueueAudio(aosdl->device, aosdl->fragment_buffer, aosdl->fragment_nbytes);
	}
	return aosdl->fragment_buffer;
}
#endif
