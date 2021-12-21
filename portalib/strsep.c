/** \file
 *
 *  \brief strsep() from musl libc v0.8.10.
 *
 *  \copyright Copyright 2005-2012 Rich Felker
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
 */

#include "top-config.h"

#include <string.h>

#include "pl-string.h"

#ifndef HAVE_STRSEP
char *strsep(char **str, const char *sep);

char *strsep(char **str, const char *sep) {
	char *s = *str, *end;
	if (!s) return NULL;
	end = s + strcspn(s, sep);
	if (*end) *end++ = 0;
	else end = 0;
	*str = end;
	return s;
}
#endif
