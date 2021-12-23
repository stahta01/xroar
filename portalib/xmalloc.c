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
 */

#include "top-config.h"

// for strnlen
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pl-string.h"
#include "xalloc.h"

void *xmalloc(size_t s) {
	void *mem = malloc(s);
	if (!mem) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	return mem;
}

void *xzalloc(size_t s) {
	void *mem = xmalloc(s);
	memset(mem, 0, s);
	return mem;
}

void *xrealloc(void *p, size_t s) {
	void *mem = realloc(p, s);
	if (!mem && s != 0) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	return mem;
}

void *xmemdup(const void *p, size_t s) {
	return memcpy(xmalloc(s), p, s);
}

char *xstrdup(const char *str) {
	return xmemdup(str, strlen(str) + 1);
}

char *xstrndup(const char *str, size_t s) {
	size_t len = strnlen(str, s);
	char *r = xmalloc(len+1);
	memcpy(r, str, len);
	r[len] = 0;
	return r;
}
