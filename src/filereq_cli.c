/*

Command-line file requester

Copyright 2003-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "fs.h"
#include "logging.h"
#include "vo.h"
#include "xroar.h"

static char *get_filename(char const * const *extensions);

FileReqModule filereq_cli_module = {
	.common = { .name = "cli",
	            .description = "Command-line file requester" },
	.load_filename = get_filename, .save_filename = get_filename
};

static char fnbuf[256];

static char *get_filename(char const * const *extensions) {
	char *in, *cr;
	_Bool was_fullscreen;
	(void)extensions;  /* unused */

	was_fullscreen = xroar_vo_interface->is_fullscreen;
	if (xroar_vo_interface->set_fullscreen && was_fullscreen)
		xroar_vo_interface->set_fullscreen(0);
	printf("Filename? ");
	fflush(stdout);
	in = fgets(fnbuf, sizeof(fnbuf), stdin);
	if (xroar_vo_interface->set_fullscreen && was_fullscreen)
		xroar_vo_interface->set_fullscreen(1);
	if (!in)
		return NULL;
	cr = strrchr(fnbuf, '\n');
	if (cr)
		*cr = 0;
	return fnbuf;
}
