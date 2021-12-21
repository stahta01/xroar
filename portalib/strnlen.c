/** \file
 *
 *  \brief strnlen() implementation.
 *
 *  \copyright Copyright 2018 Ciaran Anscomb
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

#ifndef HAVE_STRNLEN
size_t strnlen(const char *str, size_t s) {
	// don't just use strlen() and compare, Linux manpage says "looks only
	// at the first [s] characters in the string pointed to by [str] and
	// never beyond [str]+[s]"
	const char *e = memchr(str, 0, s);
	if (e)
		return e - str;
	return s;
}
#endif
