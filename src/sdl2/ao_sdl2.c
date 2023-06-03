/** \file
 *
 *  \brief SDL2 sound module.
 *
 *  \copyright Copyright 2015-2022 Ciaran Anscomb
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
 *  SDL processes audio in a separate thread, using a callback to request more
 *  data.  When nfragments >= 1, maintain a queue of fragment buffers; the
 *  callback takes the next filled buffer from the queue and copies its data
 *  into place.
 *
 *  For the special case where nfragments is 0, XRoar will write directly into
 *  the buffer provided by SDL for the minimum latency.  This will require a
 *  fast CPU to fill the buffer in time, but may also conflict with vsync being
 *  enabled in video modules (which would cause other pauses at non-useful
 *  times).
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

#ifndef HAVE_WASM
	// For most platforms (not WebAssembly), maintain a queue of buffers.

	// A small amount of allocated memory to which the last frame is copied
	// (all channels, whatever sample size is appropriate).
	void *last_frame;

	// Locking between main thread and audio callback.
	SDL_mutex *fragment_mutex;
	SDL_cond *fragment_cv;

	// Maximum time to wait for a lock to be released before continuing
	// without a buffer.
	unsigned timeout_ms;

	// Allocated space for buffers.
	void **fragment_buffer;

	// Current fragment being written, for nfragments > 0 only.
	unsigned write_fragment;

	// Next fragment to be played, for nfragments > 0 only.
	unsigned play_fragment;

	// Number of buffers filled.  For nfragments == 0, used to indicate
	// that the SDL-provided buffer has been filled to the audio callback.
	unsigned fragment_queue_length;
#endif

#ifdef HAVE_WASM
	// Under WebAssembly, we use SDL's queued audio interface, which seem
	// to work better in that environment (while not working well at all
	// under Windows).
	void *fragment_buffer;
	Uint32 qbytes_threshold;
	unsigned qdelay_divisor;
#endif
};

#ifndef HAVE_WASM
static void callback(void *, Uint8 *, int);
static void callback_0(void *, Uint8 *, int);
#endif

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

	const char *driver_name = SDL_GetCurrentAudioDriver();

#ifdef WINDOWS32
	// Avoid using wasapi backend - it's buggy!
	if (c_strcasecmp("wasapi", driver_name) == 0) {
		_Bool have_driver = 0;
		for (int i = 0; i < SDL_GetNumAudioDrivers(); i++) {
			driver_name = SDL_GetAudioDriver(i);
			if (c_strcasecmp("wasapi", driver_name) != 0) {
				if (SDL_AudioInit(driver_name) == 0) {
					have_driver = 1;
					break;
				}
			}
		}
		if (!have_driver) {
			driver_name = "wasapi";
			if (SDL_AudioInit(driver_name) == 0) {
				LOG_WARN("Fallback to known problematic wasapi backend\n");
			} else {
				// shouldn't happen
				LOG_ERROR("Failed to initialise fallback SDL audio\n");
				SDL_QuitSubSystem(SDL_INIT_AUDIO);
				return NULL;
			}
		}
	}
#endif

	LOG_DEBUG(3, "SDL_GetCurrentAudioDriver(): %s\n", driver_name);

	struct ao_sdl2_interface *aosdl = xmalloc(sizeof(*aosdl));
	*aosdl = (struct ao_sdl2_interface){0};
	struct ao_interface *ao = &aosdl->public;

	ao->free = DELEGATE_AS0(void, ao_sdl2_free, ao);

#ifdef HAVE_WASM
	unsigned rate = 22050;
#else
	unsigned rate = 48000;
#endif
	unsigned nchannels = 2;
	unsigned fragment_nframes;
	unsigned buffer_nframes;
	unsigned sample_nbytes;
	enum sound_fmt sample_fmt;

	if (xroar_cfg.ao.rate > 0)
		rate = xroar_cfg.ao.rate;

	if (xroar_cfg.ao.channels >= 1 && xroar_cfg.ao.channels <= 2)
		nchannels = xroar_cfg.ao.channels;

	aosdl->nfragments = 3;
	if (xroar_cfg.ao.fragments >= 0 && xroar_cfg.ao.fragments <= 64)
		aosdl->nfragments = xroar_cfg.ao.fragments;
#ifdef HAVE_WASM
	// The special case where nfragments == 0 requires threads which we're
	// not using in Wasm, so never pick that.
	if (aosdl->nfragments == 0)
		aosdl->nfragments++;
#endif
	unsigned buf_nfragments = aosdl->nfragments ? aosdl->nfragments : 1;

	if (xroar_cfg.ao.fragment_ms > 0) {
		fragment_nframes = (rate * xroar_cfg.ao.fragment_ms) / 1000;
	} else if (xroar_cfg.ao.fragment_nframes > 0) {
		fragment_nframes = xroar_cfg.ao.fragment_nframes;
	} else {
		if (xroar_cfg.ao.buffer_ms > 0) {
			buffer_nframes = (rate * xroar_cfg.ao.buffer_ms) / 1000;
		} else if (xroar_cfg.ao.buffer_nframes > 0) {
			buffer_nframes = xroar_cfg.ao.buffer_nframes;
		} else {
			buffer_nframes = 1024 * buf_nfragments;
		}
		fragment_nframes = buffer_nframes / buf_nfragments;
	}

	desired.freq = rate;
	desired.channels = nchannels;
	desired.samples = fragment_nframes;
#ifdef HAVE_WASM
	desired.callback = NULL;
#else
	desired.callback = (aosdl->nfragments == 0) ? callback_0 : callback;
#endif
	desired.userdata = aosdl;

	switch (xroar_cfg.ao.format) {
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
	if (xroar_cfg.ao.format == SOUND_FMT_NULL) {
		allowed_changes = SDL_AUDIO_ALLOW_FORMAT_CHANGE;
	}
	aosdl->device = SDL_OpenAudioDevice(xroar_cfg.ao.device, 0, &desired, &aosdl->audiospec, allowed_changes);

	// Check the format is supported
	if (aosdl->device == 0) {
		LOG_DEBUG(3, "First open audio failed: %s\n", SDL_GetError());
	} else {
		switch (aosdl->audiospec.format) {
		case AUDIO_U8: case AUDIO_S8:
		case AUDIO_S16LSB: case AUDIO_S16MSB:
		case AUDIO_F32SYS:
			break;
		default:
			LOG_DEBUG(3, "First open audio returned unknown format: retrying\n");
			SDL_CloseAudioDevice(aosdl->device);
			aosdl->device = 0;
			break;
		}
	}

	// One last try, allowing any changes.  Check the format is sensible later.
	if (aosdl->device == 0) {
		aosdl->device = SDL_OpenAudioDevice(xroar_cfg.ao.device, 0, &desired, &aosdl->audiospec, SDL_AUDIO_ALLOW_ANY_CHANGE);
		if (aosdl->device == 0) {
			LOG_ERROR("Couldn't open audio: %s\n", SDL_GetError());
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
			LOG_WARN("Unhandled audio format 0x%x.\n", aosdl->audiospec.format);
			goto failed;
	}

	buffer_nframes = fragment_nframes * buf_nfragments;
	aosdl->frame_nbytes = nchannels * sample_nbytes;
	aosdl->fragment_nbytes = fragment_nframes * aosdl->frame_nbytes;

#ifndef HAVE_WASM
	aosdl->fragment_mutex = SDL_CreateMutex();
	aosdl->fragment_cv = SDL_CreateCond();
	aosdl->timeout_ms = (fragment_nframes * 2000) / rate;
	aosdl->write_fragment = aosdl->play_fragment = 0;
	aosdl->fragment_queue_length = 0;
#endif

#ifdef HAVE_WASM
	// If any more than (n-1) fragments (measured in bytes) are in
	// the queue, we will wait.
	aosdl->qbytes_threshold = aosdl->fragment_nbytes * (aosdl->nfragments - 1);
	aosdl->qdelay_divisor = aosdl->frame_nbytes * rate;
#endif

	aosdl->shutting_down = 0;
	aosdl->callback_buffer = NULL;

#ifdef HAVE_WASM
	aosdl->fragment_buffer = xmalloc(aosdl->fragment_nbytes);
#endif

#ifndef HAVE_WASM
	// allocate fragment buffers
	if (aosdl->nfragments == 0) {
		aosdl->fragment_buffer = NULL;
	} else {
		aosdl->fragment_buffer = xmalloc(aosdl->nfragments * sizeof(void *));
		for (unsigned i = 0; i < aosdl->nfragments; i++) {
			aosdl->fragment_buffer[i] = xmalloc(aosdl->fragment_nbytes);
		}
		aosdl->last_frame = xmalloc(aosdl->frame_nbytes);
		memset(aosdl->last_frame, 0, aosdl->frame_nbytes);
	}
#endif

	ao->sound_interface = sound_interface_new(NULL, sample_fmt, rate, nchannels, fragment_nframes);
	if (!ao->sound_interface) {
		LOG_ERROR("Failed to initialise SDL audio: XRoar internal error\n");
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_sdl2_write_buffer, ao);
	LOG_DEBUG(1, "\t%u frags * %u frames/frag = %u frames buffer (%.1fms)\n", buf_nfragments, fragment_nframes, buffer_nframes, (float)(buffer_nframes * 1000) / rate);

	SDL_PauseAudioDevice(aosdl->device, 0);
	return aosdl;

failed:
	if (aosdl) {
		SDL_CloseAudioDevice(aosdl->device);
		if (aosdl->fragment_buffer) {
#ifndef HAVE_WASM
			if (aosdl->nfragments > 0) {
				for (unsigned i = 0; i < aosdl->nfragments; i++) {
					free(aosdl->fragment_buffer[i]);
				}
			}
#endif
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

#ifndef HAVE_WASM
	// unblock audio thread
	SDL_LockMutex(aosdl->fragment_mutex);
	aosdl->fragment_queue_length = 1;
	SDL_CondSignal(aosdl->fragment_cv);
	SDL_UnlockMutex(aosdl->fragment_mutex);
#endif

	SDL_CloseAudioDevice(aosdl->device);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);

#ifndef HAVE_WASM
	if (aosdl->nfragments == 0) {
		SDL_DestroyCond(aosdl->fragment_cv);
		SDL_DestroyMutex(aosdl->fragment_mutex);
	}
#endif

	sound_interface_free(aosdl->public.sound_interface);

	if (aosdl->nfragments > 0) {
#ifndef HAVE_WASM
		for (unsigned i = 0; i < aosdl->nfragments; i++) {
			free(aosdl->fragment_buffer[i]);
		}
#endif
		free(aosdl->fragment_buffer);
	}

	free(aosdl);
}

static void *ao_sdl2_write_buffer(void *sptr, void *buffer) {
	struct ao_sdl2_interface *aosdl = sptr;

	(void)buffer;

#ifndef HAVE_WASM
	// The normal approach is to use mutexes so the callback can write
	// silence if there's no data available, and we can wait if all the
	// buffers are full.
	//
	// The queued audio approach worked fine under Linux, but appears to
	// have caused major popping under Windows.

	if (aosdl->nfragments == 0) {
		SDL_LockMutex(aosdl->fragment_mutex);

		/* For nfragments == 0, a non-NULL buffer means we've finished
		 * writing to the buffer provided by the callback.  Signal the
		 * callback in case it is waiting for data to be available. */

		if (buffer) {
			aosdl->fragment_queue_length = 1;
			SDL_CondSignal(aosdl->fragment_cv);
		}

		if (!aosdl->public.sound_interface->ratelimit) {
			SDL_UnlockMutex(aosdl->fragment_mutex);
			return NULL;
		}

		// wait for callback to send buffer
		while (aosdl->callback_buffer == NULL) {
			if (SDL_CondWaitTimeout(aosdl->fragment_cv, aosdl->fragment_mutex, aosdl->timeout_ms) == SDL_MUTEX_TIMEDOUT) {
				SDL_UnlockMutex(aosdl->fragment_mutex);
				return NULL;
			}
		}
		aosdl->fragment_buffer = aosdl->callback_buffer;
		aosdl->callback_buffer = NULL;

		SDL_UnlockMutex(aosdl->fragment_mutex);
		return aosdl->fragment_buffer;
	} else {
		if (!aosdl->public.sound_interface->ratelimit) {
			aosdl->play_fragment = 0;
			aosdl->write_fragment = 0;
			aosdl->fragment_queue_length = 0;
			return NULL;
		}
		SDL_LockMutex(aosdl->fragment_mutex);
		while (aosdl->fragment_queue_length == aosdl->nfragments) {
			if (SDL_CondWaitTimeout(aosdl->fragment_cv, aosdl->fragment_mutex, aosdl->timeout_ms) == SDL_MUTEX_TIMEDOUT) {
				SDL_UnlockMutex(aosdl->fragment_mutex);
				return NULL;
			}
		}
		aosdl->write_fragment = (aosdl->write_fragment + 1) % aosdl->nfragments;
		aosdl->fragment_queue_length++;
		SDL_UnlockMutex(aosdl->fragment_mutex);
		return aosdl->fragment_buffer[aosdl->write_fragment];
	}
#endif

#ifdef HAVE_WASM
	// For WebAssembly, use the queued audio interface instead.  There's no
	// waiting around on mutexes, which doesn't really work with Wasm.  If
	// there's too much audio already in the queue, just purge it - doesn't
	// happen much, again, due to the way Wasm runs.
	if (!aosdl->public.sound_interface->ratelimit) {
		return NULL;
	}
	Uint32 qbytes = SDL_GetQueuedAudioSize(aosdl->device);
	if (qbytes > aosdl->qbytes_threshold) {
		return NULL;
	}
	SDL_QueueAudio(aosdl->device, aosdl->fragment_buffer, aosdl->fragment_nbytes);
	return aosdl->fragment_buffer;
#endif

}

#ifndef HAVE_WASM

// Callback for nfragments > 0.

static void callback(void *userdata, Uint8 *stream, int len) {
	struct ao_sdl2_interface *aosdl = userdata;
	// Lock mutex
	SDL_LockMutex(aosdl->fragment_mutex);

	// If there's nothing in the queue, fill SDL's data area with copies of
	// the last frame
	if (aosdl->fragment_queue_length == 0 || (unsigned)len != aosdl->fragment_nbytes) {
		SDL_UnlockMutex(aosdl->fragment_mutex);
		while (len > 0) {
			memcpy(stream, aosdl->last_frame, aosdl->frame_nbytes);
			stream += aosdl->frame_nbytes;
			len -= aosdl->frame_nbytes;
		}
		return;
	}

	// Copy fragment where SDL wants it
	unsigned play_fragment = aosdl->play_fragment;
	void *fragment = aosdl->fragment_buffer[play_fragment];
	memcpy(stream, fragment, len);

	// Preserve last frame
	memcpy(aosdl->last_frame, fragment + len - aosdl->frame_nbytes, aosdl->frame_nbytes);

	// Bump play_fragment, decrement queue length
	aosdl->play_fragment = (play_fragment + 1) % aosdl->nfragments;
	aosdl->fragment_queue_length--;
	// Signal main thread to continue (if it was waiting)
	SDL_CondSignal(aosdl->fragment_cv);
	// Unlock mutex, done
	SDL_UnlockMutex(aosdl->fragment_mutex);
}

// Callback for nfragments == 0.  In this case we pass back SDL's data pointer
// to the main thread for it to fill and cond_wait until it's ready.  This is
// going to require quite a nippy CPU, but if you can fill the buffer in time,
// the latency's going to be nice and low.

static void callback_0(void *userdata, Uint8 *stream, int len) {
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

#endif
