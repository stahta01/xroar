/*

Null video output module

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

#include <stdint.h>
#include <stdlib.h>

#include "xalloc.h"

#include "module.h"
#include "vo.h"

static void *new(void *cfg);

struct module vo_null_module = {
	.name = "null", .description = "No video",
	.new = new,
};

static void null_free(void *sptr);
static void no_op(void *sptr);
static void no_op_render(void *sptr, uint8_t const *scanline_data,
			 struct ntsc_burst *burst, unsigned phase);

static void *new(void *cfg) {
	(void)cfg;
	struct vo_interface *vo = xmalloc(sizeof(*vo));
	*vo = (struct vo_interface){0};

	vo->free = DELEGATE_AS0(void, null_free, vo);
	vo->vsync = DELEGATE_AS0(void, no_op, vo);
	vo->render_scanline = DELEGATE_AS3(void, uint8cp, ntscburst, unsigned, no_op_render, vo);

	return vo;
}

static void null_free(void *sptr) {
	struct vo_interface *vo = sptr;
	free(vo);
}

static void no_op(void *sptr) {
	(void)sptr;
}

static void no_op_render(void *sptr, uint8_t const *scanline_data,
			 struct ntsc_burst *burst, unsigned phase) {
	(void)sptr;
	(void)scanline_data;
	(void)burst;
	(void)phase;
}
