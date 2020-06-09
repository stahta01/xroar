/*

Cocoa file requester module for Mac OS X

Copyright 2011-2019 Ciaran Anscomb

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

#import <Cocoa/Cocoa.h>

#include "xalloc.h"

#include "module.h"

static void *filereq_cocoa_new(void *cfg);

struct module filereq_cocoa_module = {
	.name = "cocoa", .description = "Cocoa file requester",
	.new = filereq_cocoa_new
};

extern int cocoa_super_all_keys;

struct cocoa_filereq_interface {
	struct filereq_interface public;

	char *filename;
};

static void filereq_cocoa_free(void *sptr);
static char *load_filename(void *sptr, char const * const *extensions);
static char *save_filename(void *sptr, char const * const *extensions);

static void *filereq_cocoa_new(void *cfg) {
	(void)cfg;
	struct cocoa_filereq_interface *frcocoa = xmalloc(sizeof(*frcocoa));
	*frcocoa = (struct cocoa_filereq_interface){0};
	frcocoa->public.free = DELEGATE_AS0(void, filereq_cocoa_free, frcocoa);
	frcocoa->public.load_filename = DELEGATE_AS1(charp, charcpcp, load_filename, frcocoa);
	frcocoa->public.save_filename = DELEGATE_AS1(charp, charcpcp, save_filename, frcocoa);
	return frcocoa;
}

static void filereq_cocoa_free(void *sptr) {
	struct cocoa_filereq_interface *frcocoa = sptr;
	if (frcocoa->filename) {
		free(frcocoa->filename);
	}
	free(frcocoa);
}

/* Assuming filenames are UTF8 strings seems to do the job */

static char *load_filename(void *sptr, char const * const *extensions) {
	struct cocoa_filereq_interface *frcocoa = sptr;
	NSWindow *keyWindow = [[NSApplication sharedApplication] keyWindow];
	NSOpenPanel *dialog = [NSOpenPanel openPanel];
	(void)extensions;
	cocoa_super_all_keys = 1;
	if (frcocoa->filename) {
		free(frcocoa->filename);
		frcocoa->filename = NULL;
	}
	if ([dialog runModal] == NSModalResponseOK) {
		frcocoa->filename = xstrdup([[[[dialog URLs] objectAtIndex:0] path] UTF8String]);
	}
	cocoa_super_all_keys = 0;
	[keyWindow makeKeyAndOrderFront:nil];
	return frcocoa->filename;
}

static char *save_filename(void *sptr, char const * const *extensions) {
	struct cocoa_filereq_interface *frcocoa = sptr;
	NSWindow *keyWindow = [[NSApplication sharedApplication] keyWindow];
	NSSavePanel *dialog = [NSSavePanel savePanel];
	(void)extensions;
	cocoa_super_all_keys = 1;
	if (frcocoa->filename) {
		free(frcocoa->filename);
		frcocoa->filename = NULL;
	}
	if ([dialog runModal] == NSModalResponseOK) {
		frcocoa->filename = xstrdup([[[dialog URL] path] UTF8String]);
	}
	cocoa_super_all_keys = 0;
	[keyWindow makeKeyAndOrderFront:nil];
	return frcocoa->filename;
}
