/** \file
 *
 *  \brief Screenshots.
 *
 *  \copyright Copyright 2023-2024 Ciaran Anscomb
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

#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_PNG
#include <png.h>
#endif

#include "xalloc.h"

#include "logging.h"
#include "screenshot.h"
#include "vo.h"
#include "vo_render.h"

#ifdef HAVE_PNG

static jmp_buf jmpbuf;

int screenshot_write_png(const char *filename, struct vo_interface *vo) {
	if (!vo || !vo->renderer) {
		LOG_MOD_ERROR("screenshot/png", "no video initialised");
		return -1;
	}

	int width = vo->renderer->viewport.w;
	int height = vo->renderer->viewport.h;

	FILE *f = fopen(filename, "wb");
	if (!f) {
		LOG_MOD_WARN("screenshot/png", "%s: %s\n", filename, strerror(errno));
		return -1;
	}

	const int sample_depth = 8;

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		LOG_MOD_ERROR("screenshot/png", "png_create_write_struct() failed");
		fclose(f);
		return -2;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		LOG_MOD_ERROR("screenshot/png", "png_create_info_struct() failed");
		png_destroy_write_struct(&png_ptr, NULL);
		fclose(f);
		return -2;
	}

	uint8_t *line = xmalloc(width * 3);

	// libpng error handling
	if (setjmp(jmpbuf)) {
		LOG_MOD_ERROR("screenshot/png", "error writing PNG");
		png_destroy_write_struct(&png_ptr, &info_ptr);
		free(line);
		fclose(f);
		return -3;
	}

	// set up and write header
	png_init_io(png_ptr, f);
	png_set_IHDR(png_ptr, info_ptr, width, height * 2, sample_depth, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_uint_32 res_x = 1;
	png_uint_32 res_y = 1;
	int unit_type = PNG_RESOLUTION_UNKNOWN;
	if (vo->renderer->is_60hz) {
		res_x = 6;
		res_y = 5;
	}
	png_set_pHYs(png_ptr, info_ptr, res_x, res_y, unit_type);
	png_write_info(png_ptr, info_ptr);

	// write image data
	for (int j = 0; j < height; j++) {
		memset(line, 0, 3 * width);
		vo->renderer->line_to_rgb(vo->renderer, j, line);
		png_write_row(png_ptr, line);
		png_write_row(png_ptr, line);
	}

	// finish
	png_write_end(png_ptr, NULL);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	free(line);
	fclose(f);
	return 0;
}
#endif
