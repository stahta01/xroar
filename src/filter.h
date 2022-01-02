/** \file
 *
 *  \brief Digital filters.
 *
 *  \copyright Copyright 1992 A.J. Fisher, University of York
 *
 *  \copyright Copyright 2021 Ciaran Anscomb
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
 */

#ifndef XROAR_FILTER_H_
#define XROAR_FILTER_H_

struct filter {
	float dc_gain;  // gain at DC
	int nz, np;     // number of zeroes, poles
	float *z, *p;  // zeroes, poles
	float *zv, *pv;  // last n values
	float output;
};

#define FILTER_BU (1 << 0)
#define FILTER_LP (1 << 4)

struct filter *filter_new(unsigned flags, int order, double fs, double f0, double f1);
void filter_free(struct filter *filter);

inline float filter_apply(struct filter *filter, float value) {
	for (int i = 0; i < filter->nz-1; i++)
		filter->zv[i] = filter->zv[i+1];
	filter->zv[filter->nz-1] = value / filter->dc_gain;
	for (int i = 0; i < filter->np-1; i++)
		filter->pv[i] = filter->pv[i+1];
	filter->pv[filter->np-1] = filter->output;

	float output = 0.0;
	for (int i = 0; i < filter->nz; i++)
		output += filter->z[i] * filter->zv[i];
	for (int i = 0; i < filter->np; i++)
		output += filter->p[i] * filter->pv[i];
	filter->output = output;

	return output;
}

#endif
