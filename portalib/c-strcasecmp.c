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
 *  Assumes ASCII.
 */

#include "top-config.h"

#include <stddef.h>

#include "c-ctype.h"
#include "c-strcase.h"

int c_strcasecmp(const char *s1, const char *s2) {
	if (!s1 || !s2)
		return 0;
	while (*s1 && *s2 && (*s1 == *s2 || c_tolower(*s1) == c_tolower(*s2))) {
		s1++;
		s2++;
	}
	return c_tolower(*s1) - c_tolower(*s2);
}
