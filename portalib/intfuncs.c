/** \file
 *
 *  \brief Integer manipulations.
 *
 *  \copyright Copyright 2021-2026 Ciaran Anscomb
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

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "intfuncs.h"
#include "xalloc.h"

extern inline int int_clamp_u8(int v);
extern inline _Bool u32_parity(uint32_t val);

// Integer compare suitable for passing to qsort()

int int_cmp(const void *a, const void *b) {
	const int *aa = a;
	const int *bb = b;
	if (*aa == *bb)
		return 0;
	if (*aa < *bb)
		return -1;
	return 1;
}

// Calculate the mean of a set of integers

int int_mean(const int *values, int nvalues) {
	float sum = 0.0;
	for (int i = 0; i < nvalues; i++) {
		sum += values[i];
	}
	return IDIV_ROUND(sum, nvalues);
}

// Split a set of integers into two and calculate the mean of each

void int_split_inplace(int *buffer, int nelems, int *lowmean, int *highmean) {
	// Sort input
	qsort(buffer, nelems, sizeof(int), int_cmp);
	// Use mean of all elements to determine split point
	int mean = int_mean(buffer, nelems);
	// Sets will be defined as indices low0..low1 and high0..high1
	int low0 = 0, low1 = 0;
	for ( ; low1 < nelems && buffer[low1] < mean; low1++);
	// Discard top and bottom 5% in each set
	int high0 = low1, high1 = nelems;
	int drop0 = (high1 - high0) / 20;
	high0 += drop0;
	high1 -= drop0;
	int nhigh = high1 - high0;
	int drop1 = (low1 - low0) / 20;
	low0 += drop1;
	low1 -= drop1;
	int nlow = low1 - low0;
	// Determine mean for each set
	*highmean = int_mean(buffer + high0, nhigh);
	*lowmean = int_mean(buffer + low0, nlow);
}

// Same, but work on an allocated copy of the data

void int_split(const int *buffer, int nelems, int *lowmean, int *highmean) {
	int *bufcopy = xmalloc(nelems * sizeof(int));
	memcpy(bufcopy, buffer, nelems * sizeof(int));
	int_split_inplace(bufcopy, nelems, lowmean, highmean);
	free(bufcopy);
}

// Next power-of-two

uint32_t u32_nextpow2(uint32_t v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

// Quantise positive integer against sorted list of options.
// Terminate list with a negative number.
// dfl is default if v not greater than any option.

int int_floor_list(int v, int dfl, ...) {
	va_list ap;
	int r = dfl;
	int val;
	va_start(ap, dfl);
	for (;;) {
		val = va_arg(ap, int);
		if (val < 0)
			break;
		if (v >= val)
			r = val;
	}
	va_end(ap);
	return r;
}
