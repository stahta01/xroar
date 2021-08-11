/** \file
 *
 *  \brief Missing string handling functions.
 *
 *  \copyright Copyright 2014-2018 Ciaran Anscomb
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
 *  Supplies prototypes for string utility functions that were found to be
 *  missing in the target's libc.
 *
 *  It is still necessary to define the appropriate feature macro (e.g.,
 *  _BSD_SOURCE) before inclusion to get the right set of prototypes.
 */

#ifndef PORTALIB_PL_STRING_H_
#define PORTALIB_PL_STRING_H_

#ifdef _BSD_SOURCE

#ifndef HAVE_STRNLEN
size_t strnlen(const char *, size_t);
#endif

#ifndef HAVE_STRSEP
char *strsep(char **, const char *);
#endif

#endif

#endif
