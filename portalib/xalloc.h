/** \file
 *
 *  \brief Memory allocation with checking.
 *
 *  \copyright Copyright 2014-2021 Ciaran Anscomb
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
 *  A small set of convenience functions that wrap standard system calls and
 *  provide out of memory checking.  See Gnulib for a far more complete set.
 */

#ifndef PORTALIB_XALLOC_H_
#define PORTALIB_XALLOC_H_

#include "top-config.h"

#include <stddef.h>

void *xmalloc(size_t s) FUNC_ATTR_MALLOC FUNC_ATTR_RETURNS_NONNULL;
void *xzalloc(size_t s) FUNC_ATTR_MALLOC FUNC_ATTR_RETURNS_NONNULL;

// xrealloc() may legitimately return NULL if s == 0.

void *xrealloc(void *p, size_t s);

// xmemdup() is not flagged as a malloc-like function, as the result may
// contain pointers to valid objects.

void *xmemdup(const void *p, size_t s) FUNC_ATTR_RETURNS_NONNULL;

// Whereas string functions are expected only to be operating on strings.

char *xstrdup(const char *str) FUNC_ATTR_MALLOC FUNC_ATTR_RETURNS_NONNULL;
char *xstrndup(const char *str, size_t s) FUNC_ATTR_MALLOC FUNC_ATTR_RETURNS_NONNULL;

#endif
