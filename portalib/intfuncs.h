/** \file
 *
 *  \brief Integer manipulations.
 *
 *  \copyright Copyright 2021-2024 Ciaran Anscomb
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

#ifndef PORTALIB_INTFUNCS_H_
#define PORTALIB_INTFUNCS_H_

#include "top-config.h"

#include <stdint.h>

// General-purpose 3-tuple

typedef struct { int x, y, z; } int_xyz;

// Integer division with rounding

#define IDIV_ROUND(n,d) (((n)+((d)/2)) / (d))

// Integer compare suitable for passing to qsort()

int int_cmp(const void *a, const void *b) FUNC_ATTR_PURE;

// Calculate the mean of a set of integers

int int_mean(const int *values, int nvalues) FUNC_ATTR_PURE;

// Split a set of integers into two and calculate the mean of each

void int_split_inplace(int *buffer, int nelems, int *lowmean, int *highmean);

// Same, but work on an allocated copy of the data

void int_split(const int *buffer, int nelems, int *lowmean, int *highmean);

// Clamp integer value to 8-bit unsigned range

inline int int_clamp_u8(int v) {
	return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

// Unsigned parity function (number of bits set)
//
// from https://graphics.stanford.edu/~seander/bithacks.html

inline _Bool u32_parity(uint32_t val) {
#ifdef HAVE___BUILTIN_PARITY
	return (_Bool)__builtin_parity(val);
#else
	val ^= val >> 16;
	val ^= val >> 8;
	val ^= val >> 4;
	return (0x6996 >> (val & 15)) & 1;
#endif
}

#endif
