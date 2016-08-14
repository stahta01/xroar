/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>

#include "xalloc.h"

#include "vo.h"

static void *new(void);

struct module vo_null_module = {
	.name = "null", .description = "No video",
	.new = new,
};

static void null_free(struct vo_interface *vo);
static void no_op(struct vo_interface *vo);
static void no_op_render(struct vo_interface *vo, uint8_t const *scanline_data,
			 struct ntsc_burst *burst, unsigned phase);

static void *new(void) {
	struct vo_interface *vo = xmalloc(sizeof(*vo));
	*vo = (struct vo_interface){0};

	vo->free = null_free;
	vo->vsync = no_op;
	vo->render_scanline = no_op_render;

	return vo;
}

static void null_free(struct vo_interface *vo) {
	free(vo);
}

static void no_op(struct vo_interface *vo) {
	(void)vo;
}

static void no_op_render(struct vo_interface *vo, uint8_t const *scanline_data,
			 struct ntsc_burst *burst, unsigned phase) {
	(void)vo;
	(void)scanline_data;
	(void)burst;
	(void)phase;
}
