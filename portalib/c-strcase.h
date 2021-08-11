/** \file
 *
 *  \brief C locale string functions.
 *
 *  \copyright Copyright 2014 Ciaran Anscomb
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
 *  String functions that act as if the locale were 'C'.  See Gnulib for a far
 *  more complete implementation that also handles edge cases like
 *  non-ASCII-compatible chars in the execution environment.
 */

#ifndef PORTALIB_C_STRCASE_H_
#define PORTALIB_C_STRCASE_H_

#include <stddef.h>

int c_strcasecmp(const char *s1, const char *s2);
int c_strncasecmp(const char *s1, const char *s2, size_t n);

#endif
