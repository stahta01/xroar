/** \file
 *
 *  \brief Windows console redirection.
 *
 *  \copyright Copyright 2017 Ciaran Anscomb
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
 *  Console redirection adapted from example by "luke" on stackoverflow.com.
 */

#ifndef __GUICON_H__
#define __GUICON_H__

void redirect_io_to_console(int max_lines);

#endif
