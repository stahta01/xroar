/*

C locale character handling

Copyright 2014 Ciaran Anscomb

This file is part of Portalib.

Portalib is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version.

See COPYING.LGPL-2.1 for redistribution conditions.

*/

/*

A small subset of ctype.h functions that act as if the locale were 'C'.
See Gnulib for a far more complete implementation that also handles edge
cases like non-ASCII-compatible chars in the execution environment.

*/

#ifndef PORTALIB_C_CTYPE_H_
#define PORTALIB_C_CTYPE_H_

#include <stdbool.h>

int c_tolower(int c);
int c_toupper(int c);

static inline bool c_islower(int c) {
	return (c >= 'a' && c <= 'z');
}

static inline bool c_isupper(int c) {
	return (c >= 'A' && c <= 'Z');
}

#endif
