/** \file
 *
 *  \brief Digital filters.
 *
 *  \copyright Copyright 1992 A.J. Fisher, University of York
 *
 *  \copyright Copyright 2008-2011 Nicolas Bourdaud
 *
 *  \copyright Copyright 2021-2023 Ciaran Anscomb
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 *
 *  Filter creation derived from A. J. Fisher's "mkfilter" tool.  Stripped back
 *  to only generate Butterworth low-pass filters.  Any errors introduced in
 *  the simplification are my fault...
 *
 *  https://github.com/university-of-york/cs-www-users-fisher
 *
 *  Windowed sinc filter creation derived from rtfilter by Nicolas Bourdaud.
 */

#include "top-config.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <complex.h>
#include <stdbool.h>

#include "xalloc.h"

#include "filter.h"

// IIR filters

// mkfilter -- given n, compute recurrence relation
// to implement Butterworth, Bessel or Chebyshev filter of order n

#undef  PI
#define PI          3.14159265358979323846  // Microsoft C++ does not define M_PI !
#define TWOPI       (2.0 * PI)
#define EPS         1e-10

static inline double chypot(double complex c) { return hypot(creal(c), cimag(c)); }
static inline double complex blt(double complex c) { return (2.0 + c) / (2.0 - c); }

/* currently unused:
static inline double catan2(double complex c) { return atan2(creal(c), cimag(c)); }
static inline double complex csqr(double complex z) { return z*z; }
*/

static void expand(const double complex *, int, double complex *);
static void multin(double complex, int, double complex *);
static double complex evaluate(const double complex *, int nz, const double complex *, int, double complex);
static double complex eval(const double complex *, int, double complex);

struct filter_iir *filter_iir_new(unsigned flags, int order, double fs, double f0, double f1) {
	(void)flags;  // unused for now as always butterworth low-pass
	(void)f1;  // unused for now as we only do low-pass
	double raw_alpha1 = f0 / fs;

	struct filter_iir *filter = xmalloc(sizeof(*filter));
	*filter = (struct filter_iir){0};

	filter->z = xmalloc((order+1) * sizeof(*filter->z));
	filter->p = xmalloc((order+1) * sizeof(*filter->p));
	filter->zv = xmalloc((order+1) * sizeof(*filter->zv));
	filter->pv = xmalloc((order+1) * sizeof(*filter->pv));

	double complex *s_poles = xmalloc((order+1) * sizeof(*s_poles));
	double complex *z_poles = xmalloc((order+1) * sizeof(*z_poles));
	double complex *z_zeros = xmalloc((order+1) * sizeof(*z_zeros));

	// compute S-plane poles for prototype LP filter
	int s_numpoles = 0;
	// Butterworth filter
	for (int i = 0; i < 2*order; i++) {
		double theta = (order & 1) ? (i*PI) / order : ((i+0.5)*PI) / order;
		double complex z = cos(theta)+sin(theta)*I;
		if (creal(z) < 0.0) {
			s_poles[s_numpoles++] = z;
		}
		assert(s_numpoles <= (order+1));
	}

	// for bilinear transform, perform pre-warp on alpha values
	double warped_alpha1 = tan(PI * raw_alpha1) / PI;

	// called for trad, not for -Re or -Pi
	double w1 = TWOPI * warped_alpha1;
	// transform prototype into appropriate filter type (lp/hp/bp/bs)
	for (int i = 0; i < s_numpoles; i++) {
		s_poles[i] = s_poles[i] * w1;
	}

	// given S-plane poles & zeros, compute Z-plane poles & zeros, by bilinear transform
	int z_numpoles = s_numpoles;
	int z_numzeros = 0;
	for (int i = 0; i < z_numpoles; i++) {
		z_poles[i] = blt(s_poles[i]);
	}
	while (z_numzeros < z_numpoles) {
		z_zeros[z_numzeros++] = -1.0;
		assert(z_numzeros < (order+1));
	}

	// given Z-plane poles & zeros, compute top & bot polynomials in Z, and
	// then recurrence relation
	double complex *topcoeffs = xmalloc((z_numzeros+1) * sizeof(*topcoeffs));
	double complex *botcoeffs = xmalloc((z_numzeros+1) * sizeof(*botcoeffs));
	expand(z_zeros, z_numzeros, topcoeffs);
	expand(z_poles, z_numpoles, botcoeffs);
	double complex dc_gain = evaluate(topcoeffs, z_numzeros, botcoeffs, z_numpoles, 1.0);
	for (int i = 0; i <= z_numzeros; i++) {
		filter->z[i] = +(creal(topcoeffs[i]) / creal(botcoeffs[z_numpoles]));
		filter->zv[i] = 0.0;
	}
	for (int i = 0; i <= z_numpoles; i++) {   // bad test?  should be <?
		filter->p[i] = -(creal(botcoeffs[i]) / creal(botcoeffs[z_numpoles]));
		filter->pv[i] = 0.0;
	}
	free(topcoeffs);
	free(botcoeffs);

	filter->dc_gain = chypot(dc_gain);
	filter->nz = z_numzeros + 1;
	filter->np = z_numpoles;

	free(s_poles);
	free(z_poles);
	free(z_zeros);

	return filter;
}

void filter_iir_free(struct filter_iir *filter) {
	free(filter->z);
	free(filter->p);
	free(filter);
}

static void expand(const double complex *pz, int npz, double complex *coeffs) {
	// compute product of poles or zeros as a polynomial of z
	coeffs[0] = 1.0;
	for (int i = 0; i < npz; i++) {
		coeffs[i+1] = 0.0;
	}
	for (int i = 0; i < npz; i++) {
		multin(pz[i], npz, coeffs);
	}
	// check computed coeffs of z^k are all real
	for (int i = 0; i < (npz + 1); i++) {
		if (fabs(cimag(coeffs[i])) > EPS) {
			fprintf(stderr, "filter: coeff of z^%d is not real\n", i);
			exit(1);
		}
	}
}

static void multin(double complex w, int npz, double complex *coeffs) {
	// multiply factor (z-w) into coeffs
	double complex nw = -w;
	for (int i = npz; i >= 1; i--) coeffs[i] = (nw * coeffs[i]) + coeffs[i-1];
	coeffs[0] = nw * coeffs[0];
}

static double complex evaluate(const double complex *topco, int nz, const double complex *botco, int np, double complex z) {
	// evaluate response, substituting for z
	return eval(topco, nz, z) / eval(botco, np, z);
}

static double complex eval(const double complex *coeffs, int npz, double complex z) {
	// evaluate polynomial in z, substituting for z
	double complex sum = 0.0;
	for (int i = npz; i >= 0; i--) {
		sum = (sum * z) + coeffs[i];
	}
	return sum;
}

extern inline float filter_iir_apply(struct filter_iir *filter, float value);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// FIR filters

// rtfilter -- ... a library written in C implementing realtime digital
// filtering functions ...

// This is only being added to support experimental code, and for now we're
// only interested in generating the list of coefficients.

struct filter_fir *filter_fir_lp_create(enum filter_window window, double fc, unsigned order) {
	struct filter_fir *filter = xmalloc(sizeof(*filter));
	filter->ntaps = (order * 2) + 1;
	filter->taps = xmalloc(filter->ntaps * sizeof(*filter->taps));

	// compute_fir_lowpass
	for (unsigned i = 0; i < filter->ntaps; i++) {
		if (i != order) {
			filter->taps[i] = sin(TWOPI * fc * (double)((int)i - (int)order)) / (double)((int)i - (int)order);
		} else {
			filter->taps[i] = TWOPI * fc;
		}
	}

	// apply_window
	double M = filter->ntaps - 1;
	switch (window) {
	case FILTER_WINDOW_RECTANGULAR:
	default:
		break;

	case FILTER_WINDOW_HAMMING:
		for (unsigned i = 0; i < filter->ntaps; i++) {
			filter->taps[i] *= 0.54 + 0.46 * cos(TWOPI * ((double)i / M  - 0.5));
		}
		break;

	case FILTER_WINDOW_BLACKMAN:
		for (unsigned i = 0; i < filter->ntaps; i++) {
			filter->taps[i] *= 0.42 + 0.5 * cos(TWOPI * ((double)i / M  - 0.5)) + 0.08 * cos(2.0 * TWOPI * ((double)i / M - 0.5));
		}
		break;
	}

	// normalize_fir
	double sum = 0.0;
	for (unsigned i = 0; i < filter->ntaps; i++) {
		sum += filter->taps[i];
	}
	for (unsigned i = 0; i < filter->ntaps; i++) {
		filter->taps[i] /= sum;
	}

	return filter;
}

struct filter_fir *filter_fir_hp_create(enum filter_window window, double fc, unsigned order) {
	// create lowpass filter first
	struct filter_fir *filter = filter_fir_lp_create(window, fc, order);

	// reverse lowpass filter
	for (unsigned i = 0; i < filter->ntaps; i++) {
		filter->taps[i] = -1.0 * filter->taps[i];
	}
	filter->taps[order] += 1.0;

	return filter;
}

void filter_fir_free(struct filter_fir *filter) {
	free(filter->taps);
	free(filter);
}
