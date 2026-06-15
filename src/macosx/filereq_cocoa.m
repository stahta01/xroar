/** \file
 *
 *  \brief Cocoa file requester module for Mac OS X.
 *
 *  \copyright Copyright 2011-2026 Ciaran Anscomb
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

#import <Cocoa/Cocoa.h>

#include "delegate.h"
#include "xalloc.h"

#include "module.h"
#include "ui.h"

static void *filereq_cocoa_new(void *cfg);

struct module filereq_cocoa_module = {
	.name = "cocoa", .description = "Cocoa file requester",
	.new = filereq_cocoa_new
};

extern int cocoa_super_all_keys;

struct cocoa_filereq_interface {
	struct filereq_interface public;
};

static void filereq_cocoa_free(void *sptr);
static char *load_filename(void *sptr, char const *title);
static char *save_filename(void *sptr, char const *title);

static void *filereq_cocoa_new(void *cfg) {
	(void)cfg;
	struct cocoa_filereq_interface *frcocoa = xmalloc(sizeof(*frcocoa));
	*frcocoa = (struct cocoa_filereq_interface){0};
	frcocoa->public.free = DELEGATE_AS0(void, filereq_cocoa_free, frcocoa);
	frcocoa->public.load_filename = DELEGATE_AS1(charp, charcp, load_filename, frcocoa);
	frcocoa->public.save_filename = DELEGATE_AS1(charp, charcp, save_filename, frcocoa);
	return frcocoa;
}

static void filereq_cocoa_free(void *sptr) {
	struct cocoa_filereq_interface *frcocoa = sptr;
	free(frcocoa);
}

/* Assuming filenames are UTF8 strings seems to do the job */

static char *load_filename(void *sptr, char const *title) {
	struct cocoa_filereq_interface *frcocoa = sptr;
	NSWindow *keyWindow = [[NSApplication sharedApplication] keyWindow];
	NSOpenPanel *dialog = [NSOpenPanel openPanel];
	NSString *ntitle = [[NSString alloc] initWithUTF8String:title];
	[dialog setTitle:ntitle];
	cocoa_super_all_keys = 1;
	char *filename = NULL;
	if ([dialog runModal] == NSModalResponseOK) {
		filename = xstrdup([[[[dialog URLs] objectAtIndex:0] path] UTF8String]);
	}
	cocoa_super_all_keys = 0;
	[ntitle release];
	[keyWindow makeKeyAndOrderFront:nil];
	return filename;
}

static char *save_filename(void *sptr, char const *title) {
	struct cocoa_filereq_interface *frcocoa = sptr;
	NSWindow *keyWindow = [[NSApplication sharedApplication] keyWindow];
	NSSavePanel *dialog = [NSSavePanel savePanel];
	NSString *ntitle = [[NSString alloc] initWithUTF8String:title];
	[dialog setTitle:ntitle];
	cocoa_super_all_keys = 1;
	char *filename = NULL;
	if ([dialog runModal] == NSModalResponseOK) {
		filename = xstrdup([[[dialog URL] path] UTF8String]);
	}
	cocoa_super_all_keys = 0;
	[ntitle release];
	[keyWindow makeKeyAndOrderFront:nil];
	return filename;
}
