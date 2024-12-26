/** \file
 *
 *  \brief Audio files as tape images.
 *
 *  \copyright Copyright 2006-2024 Ciaran Anscomb
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "events.h"
#include "fs.h"
#include "logging.h"
#include "sndfile_compat.h"
#include "tape.h"

#define BLOCK_LENGTH (512)

struct tape_sndfile {
	SF_INFO info;
	SNDFILE *fd;
	_Bool writing;
	int cycles_per_frame;
	float *block;
	sf_count_t block_length;
	sf_count_t cursor;
	int cycles_to_write;
	float pan;
	float hysteresis;
};

static void sndfile_close(struct tape *t);
static long sndfile_tell(struct tape const *t);
static int sndfile_seek(struct tape *t, long offset, int whence);
static int sndfile_to_ms(struct tape const *t, long pos);
static long sndfile_ms_to(struct tape const *t, int ms);
static int sndfile_pulse_in(struct tape *t, int *pulse_width);
static int sndfile_sample_out(struct tape *t, uint8_t sample, int length);
static void sndfile_motor_off(struct tape *t);
static void sndfile_set_panning(struct tape *, float pan);
static void sndfile_set_hysteresis(struct tape *, float hysteresis);

struct tape_module tape_sndfile_module = {
	.close = sndfile_close, .tell = sndfile_tell, .seek = sndfile_seek,
	.to_ms = sndfile_to_ms, .ms_to = sndfile_ms_to,
	.pulse_in = sndfile_pulse_in, .sample_out = sndfile_sample_out,
	.motor_off = sndfile_motor_off,
	.set_panning = sndfile_set_panning,
	.set_hysteresis = sndfile_set_hysteresis,
};

struct tape *tape_sndfile_open(const char *filename, const char *mode, int rate) {
	SF_INFO sf_info;
	SNDFILE *sfd = NULL;
	_Bool writing = (mode[0] == 'w');

	sf_info.format = 0;
	if (writing) {
		sf_info.samplerate = rate;
		sf_info.channels = 1;
		sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_U8;
		sfd = sf_open(filename, SFM_RDWR, &sf_info);
	} else {
		sfd = sf_open(filename, SFM_READ, &sf_info);
	}
	if (!sfd) {
		sf_info.samplerate = 0;
		sf_info.channels = 0;
		LOG_MOD_WARN("tape/sndfile", "libsndfile error: %s\n", sf_strerror(NULL));
		return NULL;
	}
	if (sf_info.samplerate == 0 || sf_info.channels < 1) {
		sf_close(sfd);
		LOG_MOD_WARN("tape/sndfile", "bad samplerate or channel count in audio file.\n");
		return NULL;
	}

	struct tape *t = tape_new();
	t->module = &tape_sndfile_module;
	struct tape_sndfile *sndfile = xmalloc(sizeof(*sndfile));
	*sndfile = (struct tape_sndfile){0};
	t->data = sndfile;

	// initialise sndfile
	sndfile->fd = sfd;
	memcpy(&sndfile->info, &sf_info, sizeof(sndfile->info));
	sndfile->writing = writing;
	sndfile->cycles_per_frame = EVENT_TICK_RATE / sndfile->info.samplerate;
	sndfile->block = xmalloc(BLOCK_LENGTH * sizeof(*sndfile->block) * sndfile->info.channels);
	sndfile->block_length = 0;
	sndfile->cursor = 0;
	sndfile->pan = 0.5;

	// find size
	long size = sf_seek(sndfile->fd, 0, SEEK_END);
	if (size >= 0) {
		t->size = size;
		t->offset = size;
	}

	// rewind to start if not writing (else append)
	if (!writing) {
		sf_seek(sndfile->fd, 0, SEEK_SET);
		t->offset = 0;
	}

	return t;
}

static void sndfile_close(struct tape *t) {
	struct tape_sndfile *sndfile = t->data;
	sndfile_motor_off(t);
	free(sndfile->block);
	sf_close(sndfile->fd);
	free(sndfile);
	tape_free(t);
}

static long sndfile_tell(struct tape const *t) {
	return t->offset;
}

static int sndfile_seek(struct tape *t, long offset, int whence) {
	struct tape_sndfile *sndfile = t->data;
	sf_count_t new_offset = sf_seek(sndfile->fd, offset, whence);
	if (new_offset == -1)
		return -1;
	t->offset = new_offset;
	sndfile->block_length = 0;
	sndfile->cursor = 0;
	return 0;
}

static int sndfile_to_ms(struct tape const *t, long pos) {
	struct tape_sndfile *sndfile = t->data;
	float ms = (float)pos * 1000. / (float)sndfile->info.samplerate;
	return (int)ms;
}

static long sndfile_ms_to(struct tape const *t, int ms) {
	struct tape_sndfile *sndfile = t->data;
	float pos = (float)ms * (float)sndfile->info.samplerate / 1000.;
	return (long)pos;
}

static sf_count_t read_sample(struct tape_sndfile *sndfile, float *s) {
	if (sndfile->cursor >= sndfile->block_length) {
		sndfile->block_length = sf_readf_float(sndfile->fd, sndfile->block, BLOCK_LENGTH);
		sndfile->cursor = 0;
	}
	if (sndfile->cursor >= sndfile->block_length) {
		return 0;
	}
	if (sndfile->info.channels == 2) {
		float s0 = sndfile->block[sndfile->cursor * 2];
		float s1 = sndfile->block[sndfile->cursor * 2 + 1];
		*s = (s0 * (1.0 - sndfile->pan)) + (s1 * sndfile->pan);
	} else {
		*s = sndfile->block[sndfile->cursor * sndfile->info.channels];
	}
	sndfile->cursor++;
	return 1;
}

static int sndfile_pulse_in(struct tape *t, int *pulse_width) {
	struct tape_sndfile *sndfile = t->data;
	float sample;
	if (!read_sample(sndfile, &sample))
		return -1;
	t->offset++;
	int sign = (sample < 0);
	unsigned length = sndfile->cycles_per_frame;
	while (read_sample(sndfile, &sample)) {
		if (sign && sample >= sndfile->hysteresis) {
			sndfile->cursor--;
			break;
		} else if (!sign && sample <= -sndfile->hysteresis) {
			sndfile->cursor--;
			break;
		}
		t->offset++;
		length += sndfile->cycles_per_frame;
		if (length > EVENT_MS(500))
			break;
	}
	*pulse_width = length;
	return sign;
}

/* Writing */

static int write_sample(struct tape *t, float s) {
	struct tape_sndfile *sndfile = t->data;
	float *dest = sndfile->block + (sndfile->block_length * sndfile->info.channels);
	int i;
	/* write frame */
	for (i = 0; i < sndfile->info.channels; i++) {
		*(dest++) = s;
	}
	sndfile->block_length++;
	/* write full blocks */
	if (sndfile->block_length >= BLOCK_LENGTH) {
		sf_count_t written = sf_writef_float(sndfile->fd, sndfile->block, sndfile->block_length);
		if (written >= 0)
			t->offset += written;
		sndfile->block_length = 0;
		if (written < sndfile->block_length) {
			return 0;
		}
	}
	sndfile->cursor = sndfile->block_length;
	return 1;
}

static int sndfile_sample_out(struct tape *t, uint8_t sample, int length) {
	struct tape_sndfile *sndfile = t->data;
	float sample_out = ((float)sample - 128.) / 181.;
	sndfile->cycles_to_write += length;
	while (sndfile->cycles_to_write > sndfile->cycles_per_frame) {
		sndfile->cycles_to_write -= sndfile->cycles_per_frame;
		if (write_sample(t, sample_out) < 1)
			return -1;
	}
	return 0;
}

static void sndfile_motor_off(struct tape *t) {
	struct tape_sndfile *sndfile = t->data;
	if (!sndfile->writing) return;
	/* flush output block */
	if (sndfile->block_length > 0) {
		sf_count_t written = sf_writef_float(sndfile->fd, sndfile->block, sndfile->block_length);
		if (written >= 0)
			t->offset += written;
		sndfile->block_length = 0;
		sndfile->cursor = 0;
	}
	sf_write_sync(sndfile->fd);
}

static void sndfile_set_panning(struct tape *t, float pan) {
	struct tape_sndfile *sndfile = t->data;
	sndfile->pan = pan;
}

static void sndfile_set_hysteresis(struct tape *t, float hysteresis) {
	struct tape_sndfile *sndfile = t->data;
	sndfile->hysteresis = hysteresis / 100.0;
}
