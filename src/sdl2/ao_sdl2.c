/*  Copyright 2003-2017 Ciaran Anscomb
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

/* SDL processes audio in a separate thread, using a callback to request more
 * data.  When the configured number of audio fragments (nfragments) is 1,
 * write directly into the buffer provided by SDL.  When nfragments > 1,
 * maintain a queue of fragment buffers; the callback takes the next filled
 * buffer from the queue and copies its data into place. */

#include "config.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_thread.h>

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

	unsigned nfragments;
	unsigned fragment_nbytes;

	SDL_mutex *fragment_mutex;
	SDL_cond *fragment_cv;
	void **fragment_buffer;
	unsigned fragment_queue_length;
	unsigned write_fragment;
	unsigned play_fragment;

	unsigned timeout_ms;
};

static void callback(void *, Uint8 *, int);
static void callback_1(void *, Uint8 *, int);

static void ao_sdl2_free(void *sptr);
static void *ao_sdl2_write_buffer(void *sptr, void *buffer);

static void *new(void *cfg) {
	(void)cfg;
	SDL_AudioSpec desired;

	if (!SDL_WasInit(SDL_INIT_NOPARACHUTE)) {
		if (SDL_Init(SDL_INIT_NOPARACHUTE) < 0) {
			LOG_ERROR("Failed to initialise SDL\n");
			return NULL;
		}
	}
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
		LOG_ERROR("Failed to initialise SDL audio\n");
		return NULL;
	}

	struct ao_sdl2_interface *aosdl = xmalloc(sizeof(*aosdl));
	*aosdl = (struct ao_sdl2_interface){0};
	struct ao_interface *ao = &aosdl->public;

	ao->free = DELEGATE_AS0(void, ao_sdl2_free, ao);

	unsigned rate = 48000;
	unsigned nchannels = 2;
	unsigned fragment_nframes;
	unsigned buffer_nframes;
	unsigned sample_nbytes;
	enum sound_fmt sample_fmt;

	if (xroar_cfg.ao_rate > 0)
		rate = xroar_cfg.ao_rate;

	if (xroar_cfg.ao_channels >= 1 && xroar_cfg.ao_channels <= 2)
		nchannels = xroar_cfg.ao_channels;

	/* My threading code doesn't seem to work on Windows, nfragments == 1
	 * is the only way to get reasonable audio. */
#ifdef WINDOWS32
	aosdl->nfragments = 1;
#else
	aosdl->nfragments = 2;
#endif
	if (xroar_cfg.ao_fragments > 0 && xroar_cfg.ao_fragments <= 64)
		aosdl->nfragments = xroar_cfg.ao_fragments;

	if (xroar_cfg.ao_fragment_ms > 0) {
		fragment_nframes = (rate * xroar_cfg.ao_fragment_ms) / 1000;
	} else if (xroar_cfg.ao_fragment_nframes > 0) {
		fragment_nframes = xroar_cfg.ao_fragment_nframes;
	} else {
		if (xroar_cfg.ao_buffer_ms > 0) {
			buffer_nframes = (rate * xroar_cfg.ao_buffer_ms) / 1000;
		} else if (xroar_cfg.ao_buffer_nframes > 0) {
			buffer_nframes = xroar_cfg.ao_buffer_nframes;
		} else {
			buffer_nframes = 1024;
		}
		fragment_nframes = buffer_nframes / aosdl->nfragments;
	}

	desired.freq = rate;
	desired.channels = nchannels;
	desired.samples = fragment_nframes;
	desired.callback = (aosdl->nfragments == 1) ? callback_1 : callback;
	desired.userdata = aosdl;

	switch (xroar_cfg.ao_format) {
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
	default:
		desired.format = AUDIO_S16SYS;
		break;
	case SOUND_FMT_S16_SE:
		if (AUDIO_S16SYS == AUDIO_S16LSB)
			desired.format = AUDIO_S16MSB;
		else
			desired.format = AUDIO_S16LSB;
		break;
	case SOUND_FMT_FLOAT:
		desired.format = AUDIO_F32SYS;
		break;
	}

	aosdl->device = SDL_OpenAudioDevice(xroar_cfg.ao_device, 0, &desired, &aosdl->audiospec, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if (aosdl->device == 0) {
		LOG_ERROR("Couldn't open audio: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		free(aosdl);
		return NULL;
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
			LOG_WARN("Unhandled audio format 0x%x.\n", aosdl->audiospec.format);
			goto failed;
	}
	aosdl->timeout_ms = (fragment_nframes * 2000) / rate;

	buffer_nframes = fragment_nframes * aosdl->nfragments;
	aosdl->fragment_nbytes = fragment_nframes * nchannels * sample_nbytes;

	aosdl->fragment_mutex = SDL_CreateMutex();
	aosdl->fragment_cv = SDL_CreateCond();

	aosdl->shutting_down = 0;
	aosdl->fragment_queue_length = 0;
	aosdl->write_fragment = 0;
	aosdl->play_fragment = 0;
	aosdl->callback_buffer = NULL;

	// allocate fragment buffers
	aosdl->fragment_buffer = xmalloc(aosdl->nfragments * sizeof(void *));
	if (aosdl->nfragments == 1) {
		aosdl->fragment_buffer[0] = NULL;
	} else {
		for (unsigned i = 0; i < aosdl->nfragments; i++) {
			aosdl->fragment_buffer[i] = xmalloc(aosdl->fragment_nbytes);
		}
	}

	ao->sound_interface = sound_interface_new(aosdl->fragment_buffer[0], sample_fmt, rate, nchannels, fragment_nframes);
	if (!ao->sound_interface) {
		LOG_ERROR("Failed to initialise SDL audio: XRoar internal error\n");
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_sdl2_write_buffer, ao);
	LOG_DEBUG(1, "\t%u frags * %u frames/frag = %u frames buffer (%.1fms)\n", aosdl->nfragments, fragment_nframes, buffer_nframes, (float)(buffer_nframes * 1000) / rate);

	SDL_PauseAudioDevice(aosdl->device, 0);
	return aosdl;

failed:
	if (aosdl) {
		SDL_CloseAudioDevice(aosdl->device);
		if (aosdl->fragment_buffer) {
			if (aosdl->nfragments > 1) {
				for (unsigned i = 0; i < aosdl->nfragments; i++) {
					free(aosdl->fragment_buffer[i]);
				}
			}
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

	// unblock audio thread
	SDL_LockMutex(aosdl->fragment_mutex);
	aosdl->fragment_queue_length = 1;
	SDL_CondSignal(aosdl->fragment_cv);
	SDL_UnlockMutex(aosdl->fragment_mutex);

	SDL_CloseAudioDevice(aosdl->device);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);

	SDL_DestroyCond(aosdl->fragment_cv);
	SDL_DestroyMutex(aosdl->fragment_mutex);

	sound_interface_free(aosdl->public.sound_interface);

	if (aosdl->nfragments > 1) {
		for (unsigned i = 0; i < aosdl->nfragments; i++) {
			free(aosdl->fragment_buffer[i]);
		}
	}

	free(aosdl->fragment_buffer);
	free(aosdl);
}

static void *ao_sdl2_write_buffer(void *sptr, void *buffer) {
	struct ao_sdl2_interface *aosdl = sptr;

	(void)buffer;

	SDL_LockMutex(aosdl->fragment_mutex);

	/* For nfragments == 1, a non-NULL buffer means we've finished writing
	 * to the buffer provided by the callback.  Otherwise, one fragment
	 * buffer is now full.  Either way, signal the callback in case it is
	 * waiting for data to be available. */

	if (buffer) {
		aosdl->write_fragment = (aosdl->write_fragment + 1) % aosdl->nfragments;
		aosdl->fragment_queue_length++;
		SDL_CondSignal(aosdl->fragment_cv);
	}

	if (xroar_noratelimit) {
		SDL_UnlockMutex(aosdl->fragment_mutex);
		return NULL;
	}

	if (aosdl->nfragments == 1) {
		// for nfragments == 1, wait for callback to send buffer
		while (aosdl->callback_buffer == NULL) {
			if (SDL_CondWaitTimeout(aosdl->fragment_cv, aosdl->fragment_mutex, aosdl->timeout_ms) == SDL_MUTEX_TIMEDOUT) {
				SDL_UnlockMutex(aosdl->fragment_mutex);
				return NULL;
			}
		}
		aosdl->fragment_buffer[0] = aosdl->callback_buffer;
		aosdl->callback_buffer = NULL;
	} else {
		// for nfragments > 1, wait until a fragment buffer is available
		while (aosdl->fragment_queue_length == aosdl->nfragments) {
			if (SDL_CondWaitTimeout(aosdl->fragment_cv, aosdl->fragment_mutex, aosdl->timeout_ms) == SDL_MUTEX_TIMEDOUT) {
				SDL_UnlockMutex(aosdl->fragment_mutex);
				return NULL;
			}
		}
	}

	SDL_UnlockMutex(aosdl->fragment_mutex);
	return aosdl->fragment_buffer[aosdl->write_fragment];
}

static void callback(void *userdata, Uint8 *stream, int len) {
	struct ao_sdl2_interface *aosdl = userdata;
	(void)len;  /* unused */
	if (aosdl->shutting_down)
		return;
	SDL_LockMutex(aosdl->fragment_mutex);

	// wait until at least one fragment buffer is filled
	while (aosdl->fragment_queue_length == 0) {
		if (SDL_CondWaitTimeout(aosdl->fragment_cv, aosdl->fragment_mutex, aosdl->timeout_ms) == SDL_MUTEX_TIMEDOUT) {
			memset(stream, 0, aosdl->fragment_nbytes);
			SDL_UnlockMutex(aosdl->fragment_mutex);
			return;
		}
	}

	// copy it to callback buffer
	memcpy(stream, aosdl->fragment_buffer[aosdl->play_fragment], aosdl->fragment_nbytes);
	aosdl->play_fragment = (aosdl->play_fragment + 1) % aosdl->nfragments;

	// signal main thread that a fragment buffer is available
	aosdl->fragment_queue_length--;
	SDL_CondSignal(aosdl->fragment_cv);

	SDL_UnlockMutex(aosdl->fragment_mutex);
}

static void callback_1(void *userdata, Uint8 *stream, int len) {
	struct ao_sdl2_interface *aosdl = userdata;
	(void)len;  /* unused */
	if (aosdl->shutting_down)
		return;
	SDL_LockMutex(aosdl->fragment_mutex);

	// pass callback buffer to main thread
	aosdl->callback_buffer = stream;
	SDL_CondSignal(aosdl->fragment_cv);

	// wait until main thread signals filled buffer
	while (aosdl->fragment_queue_length == 0) {
		if (SDL_CondWaitTimeout(aosdl->fragment_cv, aosdl->fragment_mutex, aosdl->timeout_ms) == SDL_MUTEX_TIMEDOUT) {
			memset(stream, 0, aosdl->fragment_nbytes);
			SDL_UnlockMutex(aosdl->fragment_mutex);
			return;
		}
	}

	// set to 0 so next callback will wait
	aosdl->fragment_queue_length = 0;

	SDL_UnlockMutex(aosdl->fragment_mutex);
}
