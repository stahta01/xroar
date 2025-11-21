/** \file
 *
 *  \brief SDL3 sound module.
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
 *
 *  We use SDL3's queued audio interface.  When writing, we query how much is
 *  left in the queue, and if it's too much we wait a while for the queue to
 *  drain.
 */

#include "top-config.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "c-strcase.h"
#include "xalloc.h"

#include "ao.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static void *new(void *cfg);

struct module ao_sdl_module = {
	.name = "sdl", .description = "SDL3 audio",
	.new = new,
};

struct ao_sdl3_interface {
	struct ao_interface public;

	SDL_AudioStream *device;
	SDL_AudioSpec audiospec;

	void *callback_buffer;
	_Bool shutting_down;

	unsigned frame_nbytes;

	unsigned nfragments;
	unsigned fragment_nbytes;

	// Now that the WASAPI driver isn't causing issues in Windows, we
	// can use SDL's queued audio interface for all builds.
	void *fragment_buffer;
	int qbytes_threshold;
	unsigned qdelay_divisor;
};

static void ao_sdl3_free(void *sptr);
static void *ao_sdl3_write_buffer(void *sptr, void *buffer);
#ifndef HAVE_WASM
static void *ao_sdl3_write_silence(void *sptr, void *buffer);
#endif

static void *new(void *cfg) {
	(void)cfg;

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		LOG_MOD_SUB_ERROR("sdl", "audio", "failed to initialise\n");
		return NULL;
	}

	const char *driver_name = SDL_GetCurrentAudioDriver();
	LOG_MOD_SUB_DEBUG(3, "sdl", "audio", "using audio driver '%s'\n", driver_name);

	struct ao_sdl3_interface *aosdl = xmalloc(sizeof(*aosdl));
	*aosdl = (struct ao_sdl3_interface){0};
	struct ao_interface *ao = &aosdl->public;

	ao->free = DELEGATE_AS0(void, ao_sdl3_free, ao);

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

	aosdl->audiospec.freq = rate;
	aosdl->audiospec.channels = nchannels;
	//aosdl->audiospec.samples = fragment_nframes;
	//aosdl->audiospec.callback = NULL;
	//aosdl->audiospec.userdata = aosdl;

	switch (xroar.cfg.ao.format) {
	case SOUND_FMT_U8:
		aosdl->audiospec.format = SDL_AUDIO_U8;
		break;
	case SOUND_FMT_S8:
		aosdl->audiospec.format = SDL_AUDIO_S8;
		break;
	case SOUND_FMT_S16_BE:
		aosdl->audiospec.format = SDL_AUDIO_S16BE;
		break;
	case SOUND_FMT_S16_LE:
		aosdl->audiospec.format = SDL_AUDIO_S16LE;
		break;
	case SOUND_FMT_S16_HE:
		aosdl->audiospec.format = SDL_AUDIO_S16;
		break;
	case SOUND_FMT_S16_SE:
		if (SDL_AUDIO_S16 == SDL_AUDIO_S16LE)
			aosdl->audiospec.format = SDL_AUDIO_S16BE;
		else
			aosdl->audiospec.format = SDL_AUDIO_S16LE;
		break;
	case SOUND_FMT_FLOAT:
	default:
		aosdl->audiospec.format = SDL_AUDIO_F32;
		break;
	}

	SDL_AudioDeviceID id = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
	if (xroar.cfg.ao.device) {
		int count;
		SDL_AudioDeviceID *id_list = SDL_GetAudioPlaybackDevices(&count);
		for (int i = 0; i < count; ++i) {
			const char *name = SDL_GetAudioDeviceName(id_list[i]);
			if (0 == c_strcasecmp(name, xroar.cfg.ao.device)) {
				id = id_list[i];
				break;
			}
		}
		SDL_free(id_list);
	}

	aosdl->device = SDL_OpenAudioDeviceStream(id, &aosdl->audiospec, NULL, NULL);

	if (!aosdl->device) {
		LOG_MOD_SUB_ERROR("sdl", "audio", "failed to open audio: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		free(aosdl);
		return NULL;
	}

	switch (aosdl->audiospec.format) {
		case SDL_AUDIO_U8: sample_fmt = SOUND_FMT_U8; sample_nbytes = 1; break;
		case SDL_AUDIO_S8: sample_fmt = SOUND_FMT_S8; sample_nbytes = 1; break;
		case SDL_AUDIO_S16LE: sample_fmt = SOUND_FMT_S16_LE; sample_nbytes = 2; break;
		case SDL_AUDIO_S16BE: sample_fmt = SOUND_FMT_S16_BE; sample_nbytes = 2; break;
		case SDL_AUDIO_F32: sample_fmt = SOUND_FMT_FLOAT; sample_nbytes = 4; break;
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
	uint8_t zero = (aosdl->audiospec.format == SDL_AUDIO_U8) ? 0x80 : 0x00;
	memset(aosdl->fragment_buffer, zero, aosdl->fragment_nbytes);

	ao->sound_interface = sound_interface_new(NULL, sample_fmt, rate, nchannels, fragment_nframes);
	if (!ao->sound_interface) {
		LOG_MOD_SUB_ERROR("sdl", "audio", "failed to initialise: XRoar internal error\n");
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_sdl3_write_buffer, ao);
#ifndef HAVE_WASM
	ao->sound_interface->write_silence = DELEGATE_AS1(voidp, voidp, ao_sdl3_write_silence, ao);
#endif
	LOG_DEBUG(1, "\t%u frags * %u frames/frag = %u frames buffer (%.1fms)\n", buf_nfragments, fragment_nframes, buffer_nframes, (float)(buffer_nframes * 1000) / rate);

	SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(aosdl->device));
	return aosdl;

failed:
	if (aosdl) {
		SDL_DestroyAudioStream(aosdl->device);
		free(aosdl->fragment_buffer);
		free(aosdl);
	}
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	return NULL;
}

static void ao_sdl3_free(void *sptr) {
	struct ao_sdl3_interface *aosdl = sptr;
	aosdl->shutting_down = 1;

	// no more audio
	SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(aosdl->device));

	SDL_DestroyAudioStream(aosdl->device);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);

	sound_interface_free(aosdl->public.sound_interface);

	if (aosdl->nfragments > 0) {
		free(aosdl->fragment_buffer);
	}

	free(aosdl);
}

static void *ao_sdl3_write_buffer(void *sptr, void *buffer) {
	struct ao_sdl3_interface *aosdl = sptr;
	(void)buffer;

	if (!aosdl->public.sound_interface->ratelimit) {
		return NULL;
	}

	// For WebAssembly, if there's too much audio already in the queue,
	// just purge it - doesn't happen much, due to the way Wasm runs.
	// Otherwise wait an appropriate amount of time for the queue to drain.

	int qbytes = SDL_GetAudioStreamQueued(aosdl->device);
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
	SDL_PutAudioStreamData(aosdl->device, aosdl->fragment_buffer, aosdl->fragment_nbytes);
	return aosdl->fragment_buffer;

}

#ifndef HAVE_WASM
static void *ao_sdl3_write_silence(void *sptr, void *buffer) {
	struct ao_sdl3_interface *aosdl = sptr;
	(void)buffer;

	int qbytes = SDL_GetAudioStreamQueued(aosdl->device);
	if (qbytes < aosdl->qbytes_threshold) {
		SDL_PutAudioStreamData(aosdl->device, aosdl->fragment_buffer, aosdl->fragment_nbytes);
	}
	return aosdl->fragment_buffer;
}
#endif
