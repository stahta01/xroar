/** \file
 *
 *  \brief ALSA sound module.
 *
 *  \copyright Copyright 2009-2016 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// fix asoundlib.h including time.h
#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <alsa/asoundlib.h>

#include "xalloc.h"

#include "ao.h"
#include "events.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static void *new(void *cfg);

struct module ao_alsa_module = {
	.name = "alsa", .description = "ALSA audio",
	.new = new,
};

struct ao_alsa_interface {
	struct ao_interface public;

	snd_pcm_t *pcm_handle;
	snd_pcm_uframes_t fragment_nframes;
	void *audio_buffer;
};

static void ao_alsa_free(void *sptr);
static void *ao_alsa_write_buffer(void *sptr, void *buffer);

static void *new(void *cfg) {
	(void)cfg;
	struct ao_alsa_interface *aoalsa = xmalloc(sizeof(*aoalsa));
	*aoalsa = (struct ao_alsa_interface){0};
	struct ao_interface *ao = &aoalsa->public;
	const char *errstr = NULL;

	ao->free = DELEGATE_AS0(void, ao_alsa_free, ao);

	const char *device = xroar_cfg.ao_device ? xroar_cfg.ao_device : "default";
	int err;
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_format_t format;

	switch (xroar_cfg.ao_format) {
	case SOUND_FMT_U8:
		format = SND_PCM_FORMAT_U8;
		break;
	case SOUND_FMT_S8:
		format = SND_PCM_FORMAT_S8;
		break;
	case SOUND_FMT_S16_BE:
		format = SND_PCM_FORMAT_S16_BE;
		break;
	case SOUND_FMT_S16_LE:
		format = SND_PCM_FORMAT_S16_LE;
		break;
	case SOUND_FMT_S16_HE: default:
		format = SND_PCM_FORMAT_S16;
		break;
	case SOUND_FMT_S16_SE:
		if (SND_PCM_FORMAT_S16 == SND_PCM_FORMAT_S16_LE)
			format = SND_PCM_FORMAT_S16_BE;
		else
			format = SND_PCM_FORMAT_S16_LE;
		break;
	case SOUND_FMT_FLOAT:
		format = SND_PCM_FORMAT_FLOAT;
		break;
	}
	unsigned nchannels = xroar_cfg.ao_channels;
	if (nchannels < 1 || nchannels > 2)
		nchannels = 2;

	unsigned rate;
	rate = (xroar_cfg.ao_rate > 0) ? xroar_cfg.ao_rate : 48000;

	if ((err = snd_pcm_open(&aoalsa->pcm_handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
		goto failed;

	if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0)
		goto failed;

	if ((err = snd_pcm_hw_params_any(aoalsa->pcm_handle, hw_params)) < 0)
		goto failed;

	if ((err = snd_pcm_hw_params_set_access(aoalsa->pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
		goto failed;

	if ((err = snd_pcm_hw_params_set_format(aoalsa->pcm_handle, hw_params, format)) < 0)
		goto failed;

	if ((err = snd_pcm_hw_params_set_rate_near(aoalsa->pcm_handle, hw_params, &rate, 0)) < 0)
		goto failed;

	if ((err = snd_pcm_hw_params_set_channels_near(aoalsa->pcm_handle, hw_params, &nchannels)) < 0)
		goto failed;

	aoalsa->fragment_nframes = 0;
	if (xroar_cfg.ao_fragment_ms > 0) {
		aoalsa->fragment_nframes = (rate * xroar_cfg.ao_fragment_ms) / 1000;
	} else if (xroar_cfg.ao_fragment_nframes > 0) {
		aoalsa->fragment_nframes = xroar_cfg.ao_fragment_nframes;
	}
	if (aoalsa->fragment_nframes > 0) {
		if ((err = snd_pcm_hw_params_set_period_size_near(aoalsa->pcm_handle, hw_params, &aoalsa->fragment_nframes, NULL)) < 0) {
			LOG_ERROR("ALSA: snd_pcm_hw_params_set_period_size_near() failed\n");
			goto failed;
		}
	}

	unsigned nfragments = 0;
	int nfragments_dir;
	if (xroar_cfg.ao_fragments > 0) {
		nfragments = xroar_cfg.ao_fragments;
	}
	if (nfragments > 0) {
		if ((err = snd_pcm_hw_params_set_periods_near(aoalsa->pcm_handle, hw_params, &nfragments, NULL)) < 0) {
			LOG_ERROR("ALSA: snd_pcm_hw_params_set_periods_near() failed\n");
			goto failed;
		}
	}

	snd_pcm_uframes_t buffer_nframes = 0;
	if (xroar_cfg.ao_buffer_ms > 0) {
		buffer_nframes = (rate * xroar_cfg.ao_buffer_ms) / 1000;
	} else if (xroar_cfg.ao_buffer_nframes > 0) {
		buffer_nframes = xroar_cfg.ao_buffer_nframes;
	}
	/* Pick a sensible default: */
	if (nfragments == 0 && buffer_nframes == 0)
		buffer_nframes = (rate * 20) / 1000;
	if (buffer_nframes > 0) {
		if ((err = snd_pcm_hw_params_set_buffer_size_near(aoalsa->pcm_handle, hw_params, &buffer_nframes)) < 0) {
			LOG_ERROR("ALSA: snd_pcm_hw_params_set_buffer_size_near() failed\n");
			goto failed;
		}
	}

	if ((err = snd_pcm_hw_params(aoalsa->pcm_handle, hw_params)) < 0) {
		LOG_ERROR("ALSA: snd_pcm_hw_params() failed\n");
		goto failed;
	}

	if (nfragments == 0) {
		if ((err = snd_pcm_hw_params_get_periods(hw_params, &nfragments, &nfragments_dir)) < 0) {
			LOG_ERROR("ALSA: snd_pcm_hw_params_get_periods() failed\n");
			goto failed;
		}
	}

	if (aoalsa->fragment_nframes == 0) {
		if ((err = snd_pcm_hw_params_get_period_size(hw_params, &aoalsa->fragment_nframes, NULL)) < 0) {
			LOG_ERROR("ALSA: snd_pcm_hw_params_get_period_size() failed\n");
			goto failed;
		}
	}

	if (buffer_nframes == 0) {
		if ((err = snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_nframes)) < 0) {
			LOG_ERROR("ALSA: snd_pcm_hw_params_get_buffer_size() failed\n");
			goto failed;
		}
	}

	snd_pcm_hw_params_free(hw_params);

	if ((err = snd_pcm_prepare(aoalsa->pcm_handle)) < 0) {
		LOG_ERROR("ALSA: snd_pcm_prepare() failed\n");
		goto failed;
	}

	enum sound_fmt buffer_fmt;
	unsigned sample_nbytes;
	switch (format) {
		case SND_PCM_FORMAT_S8:
			buffer_fmt = SOUND_FMT_S8;
			sample_nbytes = 1;
			break;
		case SND_PCM_FORMAT_U8:
			buffer_fmt = SOUND_FMT_U8;
			sample_nbytes = 1;
			break;
		case SND_PCM_FORMAT_S16_LE:
			buffer_fmt = SOUND_FMT_S16_LE;
			sample_nbytes = 2;
			break;
		case SND_PCM_FORMAT_S16_BE:
			buffer_fmt = SOUND_FMT_S16_BE;
			sample_nbytes = 2;
			break;
		case SND_PCM_FORMAT_FLOAT:
			buffer_fmt = SOUND_FMT_FLOAT;
			sample_nbytes = sizeof(float);
			break;
		default:
			errstr = "Unhandled audio format";
			goto failed;
	}

	unsigned buffer_size = aoalsa->fragment_nframes * nchannels * sample_nbytes;
	aoalsa->audio_buffer = xmalloc(buffer_size);
	ao->sound_interface = sound_interface_new(aoalsa->audio_buffer, buffer_fmt, rate, nchannels, aoalsa->fragment_nframes);
	if (!ao->sound_interface) {
		errstr = "XRoar internal error";
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_alsa_write_buffer, ao);
	LOG_DEBUG(1, "\t%u frags * %ld frames/frag = %ld frames buffer (%ldms)\n", nfragments, aoalsa->fragment_nframes, buffer_nframes, (buffer_nframes * 1000) / rate);

	/* snd_pcm_writei(aoalsa->pcm_handle, buffer, aoalsa->fragment_nframes); */
	return aoalsa;

failed:
	if (!errstr)
		errstr = snd_strerror(err);
	LOG_ERROR("Failed to initialise ALSA: %s\n", errstr);
	if (aoalsa) {
		if (aoalsa->audio_buffer)
			free(aoalsa->audio_buffer);
		free(aoalsa);
	}
	return NULL;
}

static void ao_alsa_free(void *sptr) {
	struct ao_alsa_interface *aoalsa = sptr;

	snd_pcm_close(aoalsa->pcm_handle);
	snd_config_update_free_global();
	sound_interface_free(aoalsa->public.sound_interface);
	free(aoalsa->audio_buffer);
	free(aoalsa);
}

static void *ao_alsa_write_buffer(void *sptr, void *buffer) {
	struct ao_alsa_interface *aoalsa = sptr;

	if (!aoalsa->public.sound_interface->ratelimit)
		return buffer;
	if (snd_pcm_writei(aoalsa->pcm_handle, buffer, aoalsa->fragment_nframes) < 0) {
		snd_pcm_prepare(aoalsa->pcm_handle);
		snd_pcm_writei(aoalsa->pcm_handle, buffer, aoalsa->fragment_nframes);
	}
	return buffer;
}
