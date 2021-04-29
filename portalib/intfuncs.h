/*

Integer manipulations

Copyright 2021 Ciaran Anscomb

This file is part of Portalib.

Portalib is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

See COPYING.LGPL and COPYING.GPL for redistribution conditions.

*/

// Just the one macro for now.

#ifndef PORTALIB_INTFUNCS_H_
#define PORTALIB_INTFUNCS_H_

// Integer division with rounding

#define IDIV_ROUND(n,d) (((n)+((d)/2)) / (d))

// Integer compare suitable for passing to qsort()

int int_cmp(const void *a, const void *b);

// Calculate the mean of a set of integers

int int_mean(int *values, int nvalues);

// Split a set of integers into two and calculate the mean of each

void int_split_inplace(int *buffer, int nelems, int *lowmean, int *highmean);

// Same, but work on an allocated copy of the data

void int_split(const int *buffer, int nelems, int *lowmean, int *highmean);

// Clamp integer value to 8-bit unsigned range

inline int int_clamp_u8(int v) {
	if (v < 0) return 0;
	if (v > 255) return 255;
	return v;
}

#endif
