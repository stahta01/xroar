/** \file
 *
 *  \brief Audio output modules & interfaces.
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

#include <stdlib.h>

#include "module.h"
#include "ao.h"

extern struct module ao_macosx_module;
extern struct module ao_sun_module;
extern struct module ao_oss_module;
//extern struct module ao_windows32_module;
extern struct module ao_pulse_module;
extern struct module ao_sdl_module;
extern struct module ao_alsa_module;
extern struct module ao_jack_module;
extern struct module ao_null_module;

static struct module * const default_ao_module_list[] = {
#ifdef HAVE_MACOSX_AUDIO
	&ao_macosx_module,
#endif
#ifdef HAVE_SUN_AUDIO
	&ao_sun_module,
#endif
#ifdef HAVE_OSS_AUDIO
	&ao_oss_module,
#endif
/*
#ifdef WINDOWS32
	&ao_windows32_module,
#endif
*/
#ifdef HAVE_PULSE
	&ao_pulse_module,
#endif
#if defined(HAVE_SDL2) || defined(HAVE_SDL)
	&ao_sdl_module,
#endif
#ifdef HAVE_ALSA_AUDIO
	&ao_alsa_module,
#endif
#ifdef HAVE_JACK_AUDIO
	&ao_jack_module,
#endif
#ifdef HAVE_NULL_AUDIO
	&ao_null_module,
#endif
	NULL
};

struct module * const *ao_module_list = default_ao_module_list;
