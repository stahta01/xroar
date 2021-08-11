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

#ifndef XROAR_AO_H_
#define XROAR_AO_H_

#include <stdint.h>

#include "delegate.h"

struct module;
struct sound_interface;

struct ao_interface {
	DELEGATE_T0(void) free;
	struct sound_interface *sound_interface;
};

extern struct module * const *ao_module_list;

#endif
