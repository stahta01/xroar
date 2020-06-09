/*

Video ouput modules & interfaces

Copyright 2003-2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "module.h"
#include "vo.h"

extern struct module vo_null_module;
static struct module * const default_vo_module_list[] = {
	&vo_null_module,
	NULL
};

struct module * const *vo_module_list = default_vo_module_list;

struct xconfig_enum vo_ntsc_phase_list[] = {
	{ XC_ENUM_INT("none", VO_PHASE_OFF, "None") },
	{ XC_ENUM_INT("blue-red", VO_PHASE_KBRW, "Blue-red") },
	{ XC_ENUM_INT("red-blue", VO_PHASE_KRBW, "Red-blue") },
	{ XC_ENUM_END() }
};

extern inline int clamp_uint8(int v);
