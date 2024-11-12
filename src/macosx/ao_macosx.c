/** \file
 *
 *  \brief CoreAudio sound module for Mac OS X.
 *
 *  \copyright Copyright 2005-2024 Ciaran Anscomb
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
 *  Core Audio processes audio in a separate thread, using a callback to
 *  request more data.  When the configured number of audio fragments
 *  (nfragments) is 1, write directly into the buffer provided by Core Audio.
 *  When nfragments > 1, maintain a queue of fragment buffers; the callback
 *  takes the next filled buffer from the queue and copies its data into place.
 */

#include "top-config.h"

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <CoreAudio/AudioHardware.h>

#include "xalloc.h"

#include "ao.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static void *new(void *cfg);

struct module ao_macosx_module = {
	.name = "macosx", .description = "Mac OS X audio",
	.new = new,
};

struct ao_macosx_interface {
	struct ao_interface public;


	AudioObjectID device;
#ifdef MAC_OS_X_VERSION_10_5
	AudioDeviceIOProcID aprocid;
#endif

	void *callback_buffer;
	_Bool shutting_down;

	unsigned nfragments;
	unsigned fragment_nbytes;

	pthread_mutex_t fragment_mutex;
	pthread_cond_t fragment_cv;
	void **fragment_buffer;
	unsigned fragment_queue_length;
	unsigned write_fragment;
	unsigned play_fragment;
};

static OSStatus callback(AudioDeviceID, const AudioTimeStamp *, const AudioBufferList *,
		const AudioTimeStamp *, AudioBufferList *, const AudioTimeStamp *, void *);
static OSStatus callback_1(AudioDeviceID, const AudioTimeStamp *, const AudioBufferList *,
		const AudioTimeStamp *, AudioBufferList *, const AudioTimeStamp *, void *);

static void ao_macosx_free(void *sptr);
static void *ao_macosx_write_buffer(void *sptr, void *buffer);

static void *new(void *cfg) {
	(void)cfg;
	struct ao_macosx_interface *aomacosx = xmalloc(sizeof(*aomacosx));
	*aomacosx = (struct ao_macosx_interface){0};
	struct ao_interface *ao = &aomacosx->public;

	ao->free = DELEGATE_AS0(void, ao_macosx_free, ao);

	AudioObjectPropertyAddress propertyAddress;
	AudioStreamBasicDescription deviceFormat;
	UInt32 propertySize;

	propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
	propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
	propertyAddress.mElement = kAudioObjectPropertyElementMaster;

	propertySize = sizeof(aomacosx->device);
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &propertySize, &aomacosx->device) != noErr) {
		LOG_MOD_ERROR("[macosx/audio]", "AudioObjectGetPropertyData() failed: kAudioHardwarePropertyDefaultOutputDevice\n");
		goto failed;
	}

	propertySize = sizeof(deviceFormat);
	propertyAddress.mSelector = kAudioDevicePropertyStreamFormat;
	propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
	propertyAddress.mElement = 0;
	if (AudioObjectGetPropertyData(aomacosx->device, &propertyAddress, 0, NULL, &propertySize, &deviceFormat) != noErr) {
		LOG_MOD_ERROR("[macosx/audio]", "AudioObjectGetPropertyData() failed: kAudioDevicePropertyStreamFormat\n");
		goto failed;
	}

	if (deviceFormat.mFormatID != kAudioFormatLinearPCM) {
		LOG_MOD_ERROR("[macosx/audio]", "audio format not kAudioFormatLinearPCM\n");
		goto failed;
	}
	if (!(deviceFormat.mFormatFlags & kLinearPCMFormatFlagIsFloat)) {
		LOG_MOD_ERROR("[macosx/audio]", "audio format not float\n");
		goto failed;
	}

	aomacosx->nfragments = 2;
	if (xroar.cfg.ao.fragments > 0 && xroar.cfg.ao.fragments <= 64)
		aomacosx->nfragments = xroar.cfg.ao.fragments;

	unsigned rate = deviceFormat.mSampleRate;
	unsigned nchannels = deviceFormat.mChannelsPerFrame;
	unsigned fragment_nframes;
	unsigned buffer_nframes;
	enum sound_fmt sample_fmt = SOUND_FMT_FLOAT;
	unsigned sample_nbytes = sizeof(float);
	unsigned frame_nbytes = nchannels * sample_nbytes;

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
			buffer_nframes = 1024;
		}
		fragment_nframes = buffer_nframes / aomacosx->nfragments;
	}

	UInt32 prop_buf_size = fragment_nframes * frame_nbytes;
	propertySize = sizeof(prop_buf_size);
	propertyAddress.mSelector = kAudioDevicePropertyBufferSize;
	if (AudioObjectSetPropertyData(aomacosx->device, &propertyAddress, 0, NULL, propertySize, &prop_buf_size) != kAudioHardwareNoError) {
		LOG_MOD_ERROR("[macosx/audio]", "AudioObjectSetPropertyData() failed: kAudioDevicePropertyBufferSize -> %u\n", prop_buf_size);
		goto failed;
	}
	fragment_nframes = prop_buf_size / frame_nbytes;

#ifdef MAC_OS_X_VERSION_10_5
	AudioDeviceCreateIOProcID(aomacosx->device, (aomacosx->nfragments == 1) ? callback_1 : callback, aomacosx, &aomacosx->aprocid);
#else
	AudioDeviceAddIOProc(aomacosx->device, (aomacosx->nfragments == 1) ? callback_1 : callback, aomacosx);
#endif

	buffer_nframes = fragment_nframes * aomacosx->nfragments;
	aomacosx->fragment_nbytes = fragment_nframes * nchannels * sample_nbytes;

	pthread_mutex_init(&aomacosx->fragment_mutex, NULL);
	pthread_cond_init(&aomacosx->fragment_cv, NULL);

	aomacosx->shutting_down = 0;
	aomacosx->fragment_queue_length = 0;
	aomacosx->write_fragment = 0;
	aomacosx->play_fragment = 0;
	aomacosx->callback_buffer = NULL;

	// allocate fragment buffers
	aomacosx->fragment_buffer = xmalloc(aomacosx->nfragments * sizeof(void *));
	if (aomacosx->nfragments > 1) {
		for (unsigned i = 0; i < aomacosx->nfragments; i++) {
			aomacosx->fragment_buffer[i] = xmalloc(aomacosx->fragment_nbytes);
		}
	}

	AudioDeviceStart(aomacosx->device, (aomacosx->nfragments == 1) ? callback_1 : callback);

	if (aomacosx->nfragments == 1) {
		pthread_mutex_lock(&aomacosx->fragment_mutex);
		while (aomacosx->callback_buffer == NULL) {
			pthread_cond_wait(&aomacosx->fragment_cv, &aomacosx->fragment_mutex);
		}
		aomacosx->fragment_buffer[0] = aomacosx->callback_buffer;
		aomacosx->callback_buffer = NULL;
		pthread_mutex_unlock(&aomacosx->fragment_mutex);
	}

	ao->sound_interface = sound_interface_new(aomacosx->fragment_buffer[0], sample_fmt, rate, nchannels, fragment_nframes);
	if (!ao->sound_interface) {
		LOG_MOD_ERROR("[macosx/audio]", "failed to initialise: XRoar internal error\n");
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_macosx_write_buffer, ao);
	LOG_DEBUG(1, "\t%u frags * %u frames/frag = %u frames buffer (%.1fms)\n", aomacosx->nfragments, fragment_nframes, buffer_nframes, (float)(buffer_nframes * 1000) / rate);

	return aomacosx;

failed:
	if (aomacosx) {
		if (aomacosx->fragment_buffer) {
			if (aomacosx->nfragments > 1) {
				for (unsigned i = 0; i < aomacosx->nfragments; i++) {
					free(aomacosx->fragment_buffer[i]);
				}
			}
			free(aomacosx->fragment_buffer);
		}
		free(aomacosx);
	}
	return NULL;
}

static void ao_macosx_free(void *sptr) {
	struct ao_macosx_interface *aomacosx = sptr;
	aomacosx->shutting_down = 1;

	// unblock audio thread
	pthread_mutex_lock(&aomacosx->fragment_mutex);
	aomacosx->fragment_queue_length = 1;
	pthread_cond_signal(&aomacosx->fragment_cv);
	pthread_mutex_unlock(&aomacosx->fragment_mutex);

	AudioDeviceStop(aomacosx->device, (aomacosx->nfragments == 1) ? callback_1 : callback);
#ifdef MAC_OS_X_VERSION_10_5
	AudioDeviceDestroyIOProcID(aomacosx->device, aomacosx->aprocid);
#else
	AudioDeviceRemoveIOProc(aomacosx->device, (aomacosx->nfragments == 1) ? callback_1 : callback);
#endif

	pthread_mutex_destroy(&aomacosx->fragment_mutex);
	pthread_cond_destroy(&aomacosx->fragment_cv);

	if (aomacosx->nfragments > 1) {
		for (unsigned i = 0; i < aomacosx->nfragments; i++) {
			free(aomacosx->fragment_buffer[i]);
		}
	}

	free(aomacosx->fragment_buffer);
	free(aomacosx);
}

static void *ao_macosx_write_buffer(void *sptr, void *buffer) {
	struct ao_macosx_interface *aomacosx = sptr;

	(void)buffer;

	pthread_mutex_lock(&aomacosx->fragment_mutex);

	/* For nfragments == 1, a non-NULL buffer means we've finished writing
	 * to the buffer provided by the callback.  Otherwise, one fragment
	 * buffer is now full.  Either way, signal the callback in case it is
	 * waiting for data to be available. */

	if (buffer) {
		aomacosx->write_fragment = (aomacosx->write_fragment + 1) % aomacosx->nfragments;
		aomacosx->fragment_queue_length++;
		pthread_cond_signal(&aomacosx->fragment_cv);
	}

	if (!aomacosx->public.sound_interface->ratelimit) {
		pthread_mutex_unlock(&aomacosx->fragment_mutex);
		return NULL;
	}

	if (aomacosx->nfragments == 1) {
		// for nfragments == 1, wait for callback to send buffer
		while (aomacosx->callback_buffer == NULL)
			pthread_cond_wait(&aomacosx->fragment_cv, &aomacosx->fragment_mutex);
		aomacosx->fragment_buffer[0] = aomacosx->callback_buffer;
		aomacosx->callback_buffer = NULL;
	} else {
		// for nfragments > 1, wait until a fragment buffer is available
		while (aomacosx->fragment_queue_length == aomacosx->nfragments)
			pthread_cond_wait(&aomacosx->fragment_cv, &aomacosx->fragment_mutex);
	}

	pthread_mutex_unlock(&aomacosx->fragment_mutex);
	return aomacosx->fragment_buffer[aomacosx->write_fragment];
}

static OSStatus callback(AudioDeviceID inDevice, const AudioTimeStamp *inNow,
		const AudioBufferList *inInputData,
		const AudioTimeStamp *inInputTime,
		AudioBufferList *outOutputData,
		const AudioTimeStamp *inOutputTime, void *defptr) {
	struct ao_macosx_interface *aomacosx = defptr;
	(void)inDevice;      /* unused */
	(void)inNow;         /* unused */
	(void)inInputData;   /* unused */
	(void)inInputTime;   /* unused */
	(void)inOutputTime;  /* unused */
	(void)defptr;        /* unused */

	if (aomacosx->shutting_down)
		return kAudioHardwareNoError;
	pthread_mutex_lock(&aomacosx->fragment_mutex);

	// wait until at least one fragment buffer is filled
	while (aomacosx->fragment_queue_length == 0)
		pthread_cond_wait(&aomacosx->fragment_cv, &aomacosx->fragment_mutex);

	// copy it to callback buffer
	memcpy(outOutputData->mBuffers[0].mData, aomacosx->fragment_buffer[aomacosx->play_fragment], aomacosx->fragment_nbytes);
	aomacosx->play_fragment = (aomacosx->play_fragment + 1) % aomacosx->nfragments;

	// signal main thread that a fragment buffer is available
	aomacosx->fragment_queue_length--;
	pthread_cond_signal(&aomacosx->fragment_cv);

	pthread_mutex_unlock(&aomacosx->fragment_mutex);
	return kAudioHardwareNoError;
}

static OSStatus callback_1(AudioDeviceID inDevice, const AudioTimeStamp *inNow,
		const AudioBufferList *inInputData,
		const AudioTimeStamp *inInputTime,
		AudioBufferList *outOutputData,
		const AudioTimeStamp *inOutputTime, void *defptr) {
	struct ao_macosx_interface *aomacosx = defptr;
	(void)inDevice;      /* unused */
	(void)inNow;         /* unused */
	(void)inInputData;   /* unused */
	(void)inInputTime;   /* unused */
	(void)inOutputTime;  /* unused */

	if (aomacosx->shutting_down)
		return kAudioHardwareNoError;
	pthread_mutex_lock(&aomacosx->fragment_mutex);

	// pass callback buffer to main thread
	aomacosx->callback_buffer = outOutputData->mBuffers[0].mData;
	pthread_cond_signal(&aomacosx->fragment_cv);

	// wait until main thread signals filled buffer
	while (aomacosx->fragment_queue_length == 0)
		pthread_cond_wait(&aomacosx->fragment_cv, &aomacosx->fragment_mutex);

	// set to 0 so next callback will wait
	aomacosx->fragment_queue_length = 0;

	pthread_mutex_unlock(&aomacosx->fragment_mutex);
	return kAudioHardwareNoError;
}
