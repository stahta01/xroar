/** \file
 *
 *  \brief JACK sound module.
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
 *  Currently only nfragments == 1 is supported.  The architecture of JACK is
 *  sufficiently different that new code will be needed to properly support
 *  stereo, so nchannels == 1 too.
 */

#include "top-config.h"

// for struct timespec, gettimeofday
#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include <jack/jack.h>

#include "xalloc.h"

#include "ao.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static void *new(void *cfg);

struct module ao_jack_module = {
	.name = "jack", .description = "JACK audio",
	.new = new,
};

struct ao_jack_interface {
	struct ao_interface public;

	jack_client_t *client;
	jack_port_t *output_port;

	float *callback_buffer;
	_Bool shutting_down;

	unsigned nfragments;

	pthread_mutex_t fragment_mutex;
	pthread_cond_t fragment_cv;
	float *fragment_buffer;
	unsigned fragment_queue_length;
	unsigned write_fragment;

	unsigned timeout_us;
};

static int callback_1(jack_nframes_t nframes, void *arg);

static void ao_jack_free(void *sptr);
static void *ao_jack_write_buffer(void *sptr, void *buffer);

static void *new(void *cfg) {
	(void)cfg;
	struct ao_jack_interface *aojack = xmalloc(sizeof(*aojack));
	*aojack = (struct ao_jack_interface){0};
	struct ao_interface *ao = &aojack->public;

	ao->free = DELEGATE_AS0(void, ao_jack_free, ao);

	const char **ports;

	if ((aojack->client = jack_client_open("XRoar", 0, NULL)) == 0) {
		LOG_MOD_ERROR("jack", "initialisation failed: server not running?\n");
		goto failed;
	}

	unsigned buffer_nframes;
	enum sound_fmt sample_fmt = SOUND_FMT_FLOAT;

	jack_set_process_callback(aojack->client, callback_1, aojack);
	aojack->output_port = jack_port_register(aojack->client, "output0", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	if (jack_activate(aojack->client)) {
		LOG_MOD_ERROR("jack", "initialisation failed: cannot activate client\n");
		jack_client_close(aojack->client);
		goto failed;
	}
	if ((ports = jack_get_ports(aojack->client, NULL, NULL, JackPortIsPhysical|JackPortIsInput)) == NULL) {
		LOG_MOD_ERROR("jack", "cannot find any physical playback ports\n");
		jack_client_close(aojack->client);
		goto failed;
	}
	/* connect up to 2 ports (stereo output) */
	for (int i = 0; i < 2 && ports[i]; i++) {
		if (jack_connect(aojack->client, jack_port_name(aojack->output_port), ports[i])) {
			LOG_MOD_ERROR("jack", "cannot connect output ports\n");
			free(ports);
			jack_client_close(aojack->client);
			goto failed;
		}
	}
	free(ports);
	jack_nframes_t rate = jack_get_sample_rate(aojack->client);
	jack_nframes_t fragment_nframes = jack_get_buffer_size(aojack->client);

	aojack->nfragments = 1;
	if (xroar.cfg.ao.fragments > 0 && xroar.cfg.ao.fragments <= 64)
		aojack->nfragments = xroar.cfg.ao.fragments;

	aojack->timeout_us = (fragment_nframes * 1500000) / rate;

	buffer_nframes = fragment_nframes * aojack->nfragments;

	pthread_mutex_init(&aojack->fragment_mutex, NULL);
	pthread_cond_init(&aojack->fragment_cv, NULL);

	aojack->shutting_down = 0;
	aojack->fragment_queue_length = 0;
	aojack->write_fragment = 0;
	aojack->callback_buffer = NULL;

	aojack->fragment_buffer = NULL;

	ao->sound_interface = sound_interface_new(aojack->fragment_buffer, sample_fmt, rate, 1, fragment_nframes);
	if (!ao->sound_interface) {
		LOG_MOD_ERROR("jack", "failed to initialise: XRoar internal error\n");
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_jack_write_buffer, ao);
	LOG_DEBUG(1, "\t%u frags * %u frames/frag = %u frames buffer (%.1fms)\n", aojack->nfragments, fragment_nframes, buffer_nframes, (float)(buffer_nframes * 1000) / rate);

	return aojack;

failed:
	if (aojack) {
		free(aojack);
	}
	return NULL;
}

static void ao_jack_free(void *sptr) {
	struct ao_jack_interface *aojack = sptr;

	aojack->shutting_down = 1;

	// unblock audio thread
	pthread_mutex_lock(&aojack->fragment_mutex);
	aojack->fragment_queue_length = 1;
	pthread_cond_signal(&aojack->fragment_cv);
	pthread_mutex_unlock(&aojack->fragment_mutex);

	if (aojack->client)
		jack_client_close(aojack->client);
	aojack->client = NULL;

	pthread_cond_destroy(&aojack->fragment_cv);
	pthread_mutex_destroy(&aojack->fragment_mutex);
	sound_interface_free(aojack->public.sound_interface);
	free(aojack);
}

static void *ao_jack_write_buffer(void *sptr, void *buffer) {
	struct ao_jack_interface *aojack = sptr;

	pthread_mutex_lock(&aojack->fragment_mutex);

	if (buffer) {
		aojack->write_fragment = (aojack->write_fragment + 1) % aojack->nfragments;
		aojack->fragment_queue_length++;
		pthread_cond_signal(&aojack->fragment_cv);
	}

	if (!aojack->public.sound_interface->ratelimit) {
		pthread_mutex_unlock(&aojack->fragment_mutex);
		return NULL;
	}

	struct timeval tv;
	gettimeofday(&tv, NULL);
	tv.tv_usec += aojack->timeout_us;
	tv.tv_sec += (tv.tv_usec / 1000000);
	tv.tv_usec %= 1000000;
	struct timespec ts;
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;

	// for nfragments == 1, wait for callback to send buffer
	while (aojack->callback_buffer == NULL) {
		if (pthread_cond_timedwait(&aojack->fragment_cv, &aojack->fragment_mutex, &ts) == ETIMEDOUT) {
			pthread_mutex_unlock(&aojack->fragment_mutex);
			return NULL;
		}
	}
	aojack->fragment_buffer = aojack->callback_buffer;
	aojack->callback_buffer = NULL;

	pthread_mutex_unlock(&aojack->fragment_mutex);
	return aojack->fragment_buffer;;
}

static int callback_1(jack_nframes_t nframes, void *arg) {
	struct ao_jack_interface *aojack = arg;

	if (aojack->shutting_down)
		return -1;
	pthread_mutex_lock(&aojack->fragment_mutex);

	// pass callback buffer to main thread
	aojack->callback_buffer = (float *)jack_port_get_buffer(aojack->output_port, nframes);
	pthread_cond_signal(&aojack->fragment_cv);

	// wait until main thread signals filled buffer
	while (aojack->fragment_queue_length == 0)
		pthread_cond_wait(&aojack->fragment_cv, &aojack->fragment_mutex);

	// set to 0 so next callback will wait
	aojack->fragment_queue_length = 0;

	pthread_mutex_unlock(&aojack->fragment_mutex);
	return 0;
}
