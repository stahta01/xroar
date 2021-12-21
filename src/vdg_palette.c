/** \file
 *
 *  \brief VDG measured voltage "palette"s
 *
 *  \copyright Copyright 2011-2021 Ciaran Anscomb
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
 */

#include "top-config.h"

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
