/** \file
 *
 *  \brief Null sound module.
 *
 *  \copyright Copyright 2003-2016 Ciaran Anscomb
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

#ifndef HAVE_SDL2
// for struct timespec, gettimeofday, nanosleep
#define _POSIX_C_SOURCE 200112L
#endif

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SDL2
#include <SDL.h>
#else
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#endif

#include "xalloc.h"

#include "ao.h"
#include "events.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static void *new(void *cfg);

struct module ao_null_module = {
	.name = "null", .description = "No audio",
	.new = new,
};

struct ao_null_interface {
	struct ao_interface public;

	event_ticks last_pause_cycle;
	unsigned int last_pause_ms;
};

static unsigned int current_time(void);
static void sleep_ms(unsigned int ms);

static void ao_null_free(void *sptr);
static void *ao_null_write_buffer(void *sptr, void *buffer);

static void *new(void *cfg) {
	(void)cfg;
	struct ao_null_interface *aonull = xmalloc(sizeof(*aonull));
	*aonull = (struct ao_null_interface){0};
	struct ao_interface *ao = &aonull->public;

	ao->free = DELEGATE_AS0(void, ao_null_free, ao);

	ao->sound_interface = sound_interface_new(NULL, SOUND_FMT_NULL, 44100, 1, 1024);
	if (!ao->sound_interface) {
		free(aonull);
		return NULL;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_null_write_buffer, ao);
	aonull->last_pause_cycle = event_current_tick;
	aonull->last_pause_ms = current_time();
	return aonull;
}

static unsigned int current_time(void) {
#ifdef HAVE_SDL2
	return SDL_GetTicks();
#else
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return (tp.tv_sec % 1000) * 1000 + (tp.tv_usec / 1000);
#endif
}

static void sleep_ms(unsigned int ms) {
#ifdef HAVE_SDL2
	SDL_Delay(ms);
#else
	struct timespec elapsed, tv;
	elapsed.tv_sec = (ms) / 1000;
	elapsed.tv_nsec = ((ms) % 1000) * 1000000;
	do {
		errno = 0;
		tv.tv_sec = elapsed.tv_sec;
		tv.tv_nsec = elapsed.tv_nsec;
	} while (nanosleep(&tv, &elapsed) && errno == EINTR);
#endif
}

static void ao_null_free(void *sptr) {
	struct ao_null_interface *aonull = sptr;
	free(aonull);
}

static void *ao_null_write_buffer(void *sptr, void *buffer) {
	struct ao_null_interface *aonull = sptr;

	event_ticks elapsed_cycles = event_current_tick - aonull->last_pause_cycle;
	unsigned int expected_elapsed_ms = elapsed_cycles / EVENT_MS(1);
	unsigned int actual_elapsed_ms, difference_ms;
	actual_elapsed_ms = current_time() - aonull->last_pause_ms;
	difference_ms = expected_elapsed_ms - actual_elapsed_ms;
	if (difference_ms >= 10) {
		if (!aonull->public.sound_interface->ratelimit || difference_ms > 1000) {
			aonull->last_pause_ms = current_time();
			aonull->last_pause_cycle = event_current_tick;
		} else {
			sleep_ms(difference_ms);
			difference_ms = current_time() - aonull->last_pause_ms;
			aonull->last_pause_ms += difference_ms;
			aonull->last_pause_cycle += difference_ms * EVENT_MS(1);
		}
	}
	return buffer;
}
