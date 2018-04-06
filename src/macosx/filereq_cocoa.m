/*

Cocoa file requester module for Mac OS X

Copyright 2011-2014 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

*/

#include "config.h"

#import <Cocoa/Cocoa.h>

#include "xalloc.h"

#include "module.h"

static char *load_filename(char const * const *extensions);
static char *save_filename(char const * const *extensions);

FileReqModule filereq_cocoa_module = {
	.common = { .name = "cocoa", .description = "Cocoa file requester" },
	.load_filename = load_filename,
	.save_filename = save_filename
};

extern int cocoa_super_all_keys;
static char *filename = NULL;

/* Assuming filenames are UTF8 strings seems to do the job */

static char *load_filename(char const * const *extensions) {
	NSOpenPanel *dialog = [NSOpenPanel openPanel];
	(void)extensions;
	cocoa_super_all_keys = 1;
	if (filename) {
		free(filename);
		filename = NULL;
	}
	if ([dialog runModal] == NSFileHandlingPanelOKButton) {
		filename = xstrdup([[[[dialog URLs] objectAtIndex:0] path] UTF8String]);
	}
	cocoa_super_all_keys = 0;
	return filename;
}

static char *save_filename(char const * const *extensions) {
	NSSavePanel *dialog = [NSSavePanel savePanel];
	(void)extensions;
	cocoa_super_all_keys = 1;
	if (filename) {
		free(filename);
		filename = NULL;
	}
	if ([dialog runModal] == NSFileHandlingPanelOKButton) {
		filename = xstrdup([[[dialog URL] path] UTF8String]);
	}
	cocoa_super_all_keys = 0;
	return filename;
}
