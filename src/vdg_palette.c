/*

VDG measured voltage "palette"s

Copyright 2011-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "vdg_palette.h"

static struct vdg_palette palette_templates[] = {

	/* The "typical" figures from the VDG data sheet */
	{
		.name = "ideal",
		.description = "Typical values from VDG data sheet",
		.sync_y = 1.000,
		.blank_y = 0.770,
		.white_y = 0.420,
		.black_level = 0.,
		.rgb_black_level = 0.,
		.palette = {
			{ .y = 0.540, .chb = 1.50, .b = 1.00, .a = 1.00 },
			{ .y = 0.420, .chb = 1.50, .b = 1.00, .a = 1.50 },
			{ .y = 0.650, .chb = 1.50, .b = 2.00, .a = 1.50 },
			{ .y = 0.650, .chb = 1.50, .b = 1.50, .a = 2.00 },
			{ .y = 0.420, .chb = 1.50, .b = 1.50, .a = 1.50 },
			{ .y = 0.540, .chb = 1.50, .b = 1.50, .a = 1.00 },
			{ .y = 0.540, .chb = 1.50, .b = 2.00, .a = 2.00 },
			{ .y = 0.540, .chb = 1.50, .b = 1.00, .a = 2.00 },
			{ .y = 0.720, .chb = 1.50, .b = 1.50, .a = 1.50 },
			{ .y = 0.720, .chb = 1.50, .b = 1.00, .a = 1.00 },
			{ .y = 0.720, .chb = 1.50, .b = 1.00, .a = 2.00 },
			{ .y = 0.420, .chb = 1.50, .b = 1.00, .a = 2.00 },
		}
	},

	/* Real Dragon 64 */
	{
		.name = "dragon64",
		.description = "Measured from a real Dragon 64",
		.sync_y = 0.890,
		.blank_y = 0.725,
		.white_y = 0.430,
		.black_level = 0.,
		.rgb_black_level = 0.,
		.palette = {
			{ .y = 0.525, .chb = 1.42, .b = 0.87, .a = 0.94 },
			{ .y = 0.430, .chb = 1.40, .b = 0.86, .a = 1.41 },
			{ .y = 0.615, .chb = 1.38, .b = 1.71, .a = 1.38 },
			{ .y = 0.615, .chb = 1.34, .b = 1.28, .a = 1.83 },
			{ .y = 0.430, .chb = 1.35, .b = 1.28, .a = 1.35 },
			{ .y = 0.525, .chb = 1.36, .b = 1.29, .a = 0.96 },
			{ .y = 0.525, .chb = 1.37, .b = 1.70, .a = 1.77 },
			{ .y = 0.525, .chb = 1.40, .b = 0.85, .a = 1.86 },
			{ .y = 0.680, .chb = 1.35, .b = 1.28, .a = 1.35 },
			{ .y = 0.680, .chb = 1.42, .b = 0.87, .a = 0.94 },
			{ .y = 0.680, .chb = 1.40, .b = 0.85, .a = 1.86 },
			{ .y = 0.430, .chb = 1.40, .b = 0.85, .a = 1.86 },
		}
	},

};

static int num_palettes = ARRAY_N_ELEMENTS(palette_templates);

/**************************************************************************/

int vdg_palette_count(void) {
	return num_palettes;
}

struct vdg_palette *vdg_palette_index(int i) {
	if (i < 0 || i >= num_palettes) {
		return NULL;
	}
	return &palette_templates[i];
}

struct vdg_palette *vdg_palette_by_name(const char *name) {
	int count, i;
	if (!name) return NULL;
	count = vdg_palette_count();
	for (i = 0; i < count; i++) {
		struct vdg_palette *vp = vdg_palette_index(i);
		if (!vp)
			return NULL;
		if (0 == strcmp(vp->name, name)) {
			return vp;
		}
	}
	return NULL;
}

/* ---------------------------------------------------------------------- */

/* Map Y'U'V' from palette to pixel value */
void vdg_palette_RGB(struct vdg_palette *vp, int colour,
                     float *Rout, float *Gout, float *Bout) {
	float blank_y = vp->blank_y;
	float white_y = vp->white_y;
	float black_level = vp->black_level;
	float rgb_black_level = vp->rgb_black_level;
	float y = vp->palette[colour].y;
	float chb = vp->palette[colour].chb;
	float b_y = vp->palette[colour].b - chb;
	float r_y = vp->palette[colour].a - chb;

	float scale_y = 1. / (blank_y - white_y);
	y = black_level + (blank_y - y) * scale_y;

	float u = 0.493 * b_y;
	float v = 0.877 * r_y;
	float r = 1.0 * y + 0.000 * u + 1.140 * v;
	float g = 1.0 * y - 0.396 * u - 0.581 * v;
	float b = 1.0 * y + 2.029 * u + 0.000 * v;
	float mlaw = 2.2;

	/* These values directly relate to voltages fed to a modulator which,
	 * I'm assuming, does nothing further to correct for the non-linearity
	 * of the display device.  Therefore, these can be considered "gamma
	 * corrected" values, and to work with them in linear RGB, we need to
	 * undo the assumed characteristics of the display.  NTSC was
	 * originally defined differently, but most SD televisions that people
	 * will have used any time recently are probably close to Rec. 601, so
	 * use that transfer function:
	 *
	 * L = V/4.5                        for V <  0.081
	 * L = ((V + 0.099) / 1.099) ^ 2.2  for V >= 0.081
	 *
	 * Note: the same transfer function is specified for Rec. 709.
	 */

	if (r < (0.018 * 4.5)) {
		r = r / 4.5;
	} else {
		r = powf((r+0.099)/(1.+0.099), mlaw);
	}
	if (g < (0.018 * 4.5)) {
		g = g / 4.5;
	} else {
		g = powf((g+0.099)/(1.+0.099), mlaw);
	}
	if (b < (0.018 * 4.5)) {
		b = b / 4.5;
	} else {
		b = powf((b+0.099)/(1.+0.099), mlaw);
	}

	*Rout = r + rgb_black_level;
	*Gout = g + rgb_black_level;
	*Bout = b + rgb_black_level;

	if (*Rout < 0.0) { *Rout = 0.0; } if (*Rout > 1.0) { *Rout = 1.0; }
	if (*Gout < 0.0) { *Gout = 0.0; } if (*Gout > 1.0) { *Gout = 1.0; }
	if (*Bout < 0.0) { *Bout = 0.0; } if (*Bout > 1.0) { *Bout = 1.0; }
}
