/*

Memory allocation with checking

Copyright 2014 Ciaran Anscomb

This file is part of Portalib.

Portalib is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version.

See COPYING.LGPL-2.1 for redistribution conditions.

A small set of convenience functions that wrap standard system calls and
provide out of memory checking.  See Gnulib for a far more complete set.

*/

#ifndef PORTALIB_XALLOC_H_
#define PORTALIB_XALLOC_H_

#include <stddef.h>

void *xmalloc(size_t s);
void *xzalloc(size_t s);
void *xrealloc(void *p, size_t s);

void *xmemdup(const void *p, size_t s);
char *xstrdup(const char *str);

#endif
