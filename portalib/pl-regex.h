/** \file
 *
 *  \brief Wrap regex.h.
 *
 *  \copyright Copyright 2020 Ciaran Anscomb
 *
 *  \licenseblock This file is part of Portalib.
 *
 *  Portalib is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by the Free
 *  Software Foundation; either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  See COPYING.LGPL and COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 *
 *  Attempt to include a POSIX-compatible header file, from:
 *
 * - TRE's compatible regex.h
 *
 * - System regex.h
 */

#ifndef PORTALIB_PL_REGEX_H_
#define PORTALIB_PL_REGEX_H_

#if defined(HAVE_TRE)

#include <tre/regex.h>

#elif defined(HAVE_REGEX_H)

#include <regex.h>

#endif

#endif
