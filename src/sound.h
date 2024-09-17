/** \file
 *
 *  \brief Dragon sound interface.
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
 * Audio modules provide a buffer to write into.  Sound interface provides
 * Dragon/CoCo-specific means to write to it.
 */

#ifndef XROAR_SOUND_H_
#define XROAR_SOUND_H_

#include "delegate.h"

enum sound_fmt {
	SOUND_FMT_NULL,
	SOUND_FMT_U8,
	SOUND_FMT_S8,
	SOUND_FMT_S16_BE,
	SOUND_FMT_S16_LE,
	SOUND_FMT_S16_HE,  // host-endian
	SOUND_FMT_S16_SE,  // swapped-endian
	SOUND_FMT_FLOAT,
};

struct sound_interface {
	int framerate;  // output rate
	_Bool ratelimit;  // ratelimit
	DELEGATE_T1(void, bool) sbs_feedback;  // single-bit sound feedback
	DELEGATE_T3(float, uint32, int, floatp) get_non_muxed_audio;
	DELEGATE_T3(float, uint32, int, floatp) get_tape_audio;
	DELEGATE_T3(float, uint32, int, floatp) get_cart_audio;
	DELEGATE_T3(float, uint32, int, floatp) get_ay_audio;
	DELEGATE_T1(voidp, voidp) write_buffer;
	DELEGATE_T1(voidp, voidp) write_silence;
};

struct sound_interface *sound_interface_new(void *buf, enum sound_fmt fmt, unsigned rate,
					    unsigned nchannels, unsigned nframes);
void sound_interface_free(struct sound_interface *sndp);

void sound_update(struct sound_interface *sndp);
void sound_send_silence(struct sound_interface *);

// Rate limit control
void sound_set_ratelimit(struct sound_interface *sndp, _Bool ratelimit);

// Dragon/CoCo-specific manipulation
void sound_set_sbs(struct sound_interface *sndp, _Bool enabled, _Bool level);
void sound_set_mux_enabled(struct sound_interface *sndp, _Bool enabled);
void sound_set_mux_source(struct sound_interface *sndp, unsigned source);
void sound_set_dac_level(struct sound_interface *sndp, float level);
void sound_set_tape_level(struct sound_interface *sndp, float level);
void sound_set_external_left(struct sound_interface *sndp, float level);
void sound_set_external_right(struct sound_interface *sndp, float level);

#endif
