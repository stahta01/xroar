/*  Copyright 2003-2015 Ciaran Anscomb
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

/* Currently only nfragments == 1 is supported.  The architecture of JACK is
 * sufficiently different that new code will be needed to properly support
 * stereo, so nchannels == 1 too. */

#include "config.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <jack/jack.h>

#include "xalloc.h"

#include "logging.h"
#include "machine.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static _Bool init(void);
static void shutdown(void);
static void *write_buffer(void *buffer);

SoundModule sound_jack_module = {
	.common = { .name = "jack", .description = "JACK audio",
		    .init = init, .shutdown = shutdown },
	.write_buffer = write_buffer,
};

static jack_client_t *client;
static jack_port_t *output_port;

static float *callback_buffer;
static _Bool shutting_down;

static unsigned nfragments;

static pthread_mutex_t fragment_mutex;
static pthread_cond_t fragment_cv;
static float *fragment_buffer;
static unsigned fragment_queue_length;
static unsigned write_fragment;
static unsigned play_fragment;

static unsigned timeout_us;

static int callback_1(jack_nframes_t nframes, void *arg);

static _Bool init(void) {
	const char **ports;

	if ((client = jack_client_open("XRoar", 0, NULL)) == 0) {
		LOG_ERROR("Initialisation failed: JACK server not running?\n");
		return 0;
	}

	unsigned buffer_nframes;
	enum sound_fmt sample_fmt = SOUND_FMT_FLOAT;

	jack_set_process_callback(client, callback_1, 0);
	output_port = jack_port_register(client, "output0", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	if (jack_activate(client)) {
		LOG_ERROR("Initialisation failed: Cannot activate client\n");
		jack_client_close(client);
		return 0;
	}
	if ((ports = jack_get_ports(client, NULL, NULL, JackPortIsPhysical|JackPortIsInput)) == NULL) {
		LOG_ERROR("Cannot find any physical playback ports\n");
		jack_client_close(client);
		return 0;
	}
	/* connect up to 2 ports (stereo output) */
	for (int i = 0; i < 2 && ports[i]; i++) {
		if (jack_connect(client, jack_port_name(output_port), ports[i])) {
			LOG_ERROR("Cannot connect output ports\n");
			free(ports);
			jack_client_close(client);
			return 0;
		}
	}
	free(ports);
	jack_nframes_t rate = jack_get_sample_rate(client);
	jack_nframes_t fragment_nframes = jack_get_buffer_size(client);

	nfragments = 1;
	if (xroar_cfg.ao_fragments > 0 && xroar_cfg.ao_fragments <= 64)
		nfragments = xroar_cfg.ao_fragments;

	timeout_us = (fragment_nframes * 1500000) / rate;

	buffer_nframes = fragment_nframes * nfragments;

	pthread_mutex_init(&fragment_mutex, NULL);
	pthread_cond_init(&fragment_cv, NULL);

	shutting_down = 0;
	fragment_queue_length = 0;
	write_fragment = 0;
	play_fragment = 0;
	callback_buffer = NULL;

	fragment_buffer = NULL;

	sound_init(fragment_buffer, sample_fmt, rate, 1, fragment_nframes);
	LOG_DEBUG(1, "\t%u frags * %u frames/frag = %u frames buffer (%.1fms)\n", nfragments, fragment_nframes, buffer_nframes, (float)(buffer_nframes * 1000) / rate);

	return 1;
}

static void shutdown(void) {
	shutting_down = 1;

	// unblock audio thread
	pthread_mutex_lock(&fragment_mutex);
	fragment_queue_length = 1;
	pthread_cond_signal(&fragment_cv);
	pthread_mutex_unlock(&fragment_mutex);

	if (client)
		jack_client_close(client);
	client = NULL;

	pthread_cond_destroy(&fragment_cv);
	pthread_mutex_destroy(&fragment_mutex);
}

static void *write_buffer(void *buffer) {
	pthread_mutex_lock(&fragment_mutex);

	if (buffer) {
		write_fragment = (write_fragment + 1) % nfragments;
		fragment_queue_length++;
		pthread_cond_signal(&fragment_cv);
	}

	if (xroar_noratelimit) {
		pthread_mutex_unlock(&fragment_mutex);
		return NULL;
	}

	struct timeval tv;
	gettimeofday(&tv, NULL);
	tv.tv_usec += timeout_us;
	tv.tv_sec += (tv.tv_usec / 1000000);
	tv.tv_usec %= 1000000;
	struct timespec ts;
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;

	// for nfragments == 1, wait for callback to send buffer
	while (callback_buffer == NULL) {
		if (pthread_cond_timedwait(&fragment_cv, &fragment_mutex, &ts) == ETIMEDOUT) {
			pthread_mutex_unlock(&fragment_mutex);
			return NULL;
		}
	}
	fragment_buffer = callback_buffer;
	callback_buffer = NULL;

	pthread_mutex_unlock(&fragment_mutex);
	return fragment_buffer;;
}

static int callback_1(jack_nframes_t nframes, void *arg) {
	(void)arg;  /* unused */

	if (shutting_down)
		return -1;
	pthread_mutex_lock(&fragment_mutex);

	// pass callback buffer to main thread
	callback_buffer = (float *)jack_port_get_buffer(output_port, nframes);
	pthread_cond_signal(&fragment_cv);

	// wait until main thread signals filled buffer
	while (fragment_queue_length == 0)
		pthread_cond_wait(&fragment_cv, &fragment_mutex);

	// set to 0 so next callback will wait
	fragment_queue_length = 0;

	pthread_mutex_unlock(&fragment_mutex);
	return 0;
}
