/*

WebAssembly (emscripten) support

Copyright 2019 Ciaran Anscomb

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

#include <libgen.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <emscripten.h>

#include "sds.h"
#include "slist.h"
#include "xalloc.h"

#include "cart.h"
#include "events.h"
#include "fs.h"
#include "logging.h"
#include "keyboard.h"
#include "machine.h"
#include "romlist.h"
#include "vdisk.h"
#include "xroar.h"

// Currently tied to SDL2, as we need to poll SDL events.  Refactor soon...
#include <SDL.h>
#include "sdl2/common.h"

// Functions prefixed wasm_ui_ are called from UI to interact with the browser
// environment.  Other functions prefixed wasm_ are exported and called from
// the JavaScript support code.

// Flag pending downloads.  Emulator will not run while waiting for files.
static int wasm_waiting_files = 0;

static _Bool done_first_frame = 0;
static double last_t;
static double tickerr = 0.;

// The WebAssembly main "loop" - really called once per frame by the browser.
// For normal operation, we calculate elapsed time since the last frame and run
// the emulation for that long.  If we're waiting on downloads though, we just
// immediately return control to the emscripten environment to handle the
// asynchronous messaging until everything is ready.

void wasm_ui_run(void *sptr) {
	struct ui_interface *ui = sptr;
	(void)ui;

	// Calculate time delta since last call in milliseconds.
	double t = emscripten_get_now();
	double dt = t - last_t;
	last_t = t;

	// For the first call, we definitely don't have an accurate time delta,
	// so wait until the second frame...
	if (!done_first_frame) {
		done_first_frame = 1;
		return;
	}

	// Try and head off insane situations:
	if (dt < 0. || dt > 400.) {
		return;
	}

	// Don't run the emulator while there are pending downloads.
	if (wasm_waiting_files) {
		return;
	}

	// Calculate number of ticks to run based on time delta.
	tickerr += (14318180. * (dt / 1000.));
	int nticks = (int)tickerr;
	event_ticks last_tick = event_current_tick;

	// Poll SDL events (need to refactor this).
	run_sdl_event_loop(global_uisdl2);

	// Run emulator.
	xroar_run(nticks);

	// Record time offset based on actual number of ticks run.
	int dtick = event_current_tick - last_tick;
	tickerr -= (double)dtick;
}

// Wasm event handler relays information to web page handlers.

void wasm_ui_set_state(void *sptr, int tag, int value, const void *data) {
	struct ui_interface *ui = sptr;

	switch (tag) {

	// Hardware

	case ui_tag_machine:
		EM_ASM_({ ui_update_machine($0); }, value);
		break;

	case ui_tag_cartridge:
		EM_ASM_({ ui_update_cart($0); }, value);
		break;

	// Tape

	case ui_tag_tape_input_filename:
		if (data) {
			char *fn = xstrdup((char *)data);
			char *bn = basename(fn);
			EM_ASM_({ ui_update_tape_input_filename($0); }, bn);
			free(fn);
		} else {
			EM_ASM_({ ui_update_tape_input_filename($0); }, "");
		}
		break;

	// Disk

	case ui_tag_disk_data:
		{
			const struct vdisk *disk = (const struct vdisk *)data;
			if (disk) {
				char *fn = NULL, *bn = NULL;
				if (disk->filename) {
					fn = xstrdup(disk->filename);
					bn = basename(fn);
				}
				EM_ASM_({ ui_update_disk_info($0, $1, $2, $3, $4, $5); }, value, bn, disk->write_back, disk->write_protect, disk->num_cylinders, disk->num_heads);
				if (fn) {
					free(fn);
				}
			} else {
				EM_ASM_({ ui_update_disk_info($0, null, 0, 0, -1, 0); }, value);
			}
		}
		break;

	// Video

	case ui_tag_ccr:
		EM_ASM_({ ui_update_ccr($0); }, value);
		break;

	case ui_tag_cross_colour:
		EM_ASM_({ ui_update_cross_colour($0); }, value);
		break;

	default:
		break;
	}
}

// File fetching.  Locks files to prevent multiple attempts to fetch the same
// file, and deals with "stub" files (zero length preloaded equivalents only
// present to enable automatic machine configuration).  This half-baked
// approach to file locking is probably fine for our purposes.  It seems to
// work :)

static _Bool lock_fetch(const char *file) {
	sds lockfile = sdsnew(file);
	lockfile = sdscat(lockfile, ".lock");
	int fd = open(lockfile, O_CREAT|O_EXCL|O_WRONLY, 0666);
	sdsfree(lockfile);
	if (fd == -1) {
		return 0;
	}
	close(fd);
	wasm_waiting_files++;
	return 1;
}

static void unlock_fetch(const char *file) {
	sds lockfile = sdsnew(file);
	lockfile = sdscat(lockfile, ".lock");
	int fd = open(lockfile, O_RDONLY);
	if (fd == -1) {
		perror(NULL);
		sdsfree(lockfile);
		return;
	}
	close(fd);
	unlink(lockfile);
	sdsfree(lockfile);
	wasm_waiting_files--;
}

static void wasm_onload(const char *file) {
	unlock_fetch(file);
}

static void wasm_onerror(const char *file) {
	LOG_WARN("Error fetching '%s'\n", file);
	unlock_fetch(file);
}

static void wasm_wget(const char *file) {
	if (!file || *file == 0)
		return;

	// Ensure the destination directory exists.  Fine for MEMFS in the
	// sandbox, just don't use this anywhere important, I've barely given
	// it more than a few seconds of thought.
	char *filecp = xstrdup(file);
	char *dir = dirname(filecp);
	for (char *x = dir; *x; ) {
		while (*x == '/')
			x++;
		while (*x && *x != '/')
			x++;
		char old = *x;
		*x = 0;
		// Skip attempt to create if already exists as directory
		struct stat statbuf;
		if (stat(dir, &statbuf) == 0) {
			if (statbuf.st_mode & S_IFDIR) {
				*x = old;
				continue;
			}
		}
		// Anything we still can't deal with is a hard fail
		if (mkdir(dir, 0700) == -1) {
			perror(dir);
			free(filecp);
			return;
		}
		*x = old;
	}
	free(filecp);

	if (!lock_fetch(file)) {
		// Couldn't lock file - either it's already being downloaded,
		// or there was an error.  Either way, just bail.
		return;
	}

	FILE *fd;
	if ((fd = fopen(file, "rb"))) {
		if (fs_file_size(fd) > 0) {
			// File already exists - no need to fetch, so unlock
			// and return.
			fclose(fd);
			unlock_fetch(file);
			return;
		}
		fclose(fd);
		// File exists but is a zero-length stub: remove stub, then
		// fall through to fetch its replacement (still locked).
		unlink(file);
	}

	// Submit fetch.  Callbacks will unlock the fetch when done.  The more
	// full-featured Emscripten wget function would allow us to display
	// progress bars, etc., but nothing we'll ever fetch is really large
	// enough to justify that.
	emscripten_async_wget(file, file, wasm_onload, wasm_onerror);
}

// Set machine & its default cart

static void do_wasm_set_machine(void *sptr) {
	int id = (intptr_t)sptr;
	xroar_set_machine(1, id);
}

static void do_wasm_set_cartridge(void *sptr) {
	int id = (intptr_t)sptr;
	xroar_set_cart_by_id(1, id);
}

// Lookup ROM in romlist before trying to fetch it.

static void wasm_wget_rom(const char *rom) {
	char *tmp;
	if ((tmp = romlist_find(rom))) {
		wasm_wget(tmp);
	}
}

// xroar_set_machine() is redirected here in order to allow asynchronous fetching
// of a machine's ROMs.  If all the ROMs are present, 1 is returned and the normal
// code is allowed to proceed, otherwise an event is queued to call it again once
// all fetches have completed.

_Bool wasm_ui_prepare_machine(struct machine_config *mc) {
	if (!mc->nobas && mc->bas_rom) {
		wasm_wget_rom(mc->bas_rom);
	}
	if (!mc->noextbas && mc->extbas_rom) {
		wasm_wget_rom(mc->extbas_rom);
	}
	if (!mc->noaltbas && mc->altbas_rom) {
		wasm_wget_rom(mc->altbas_rom);
	}
	if (mc->ext_charset_rom) {
		wasm_wget_rom(mc->ext_charset_rom);
	}
	if (wasm_waiting_files == 0) {
		return 1;
	}
	event_queue_auto(&UI_EVENT_LIST, DELEGATE_AS0(void, do_wasm_set_machine, (void *)(intptr_t)mc->id), 0);
	return 0;
}

// Similarly, xroar_set_cart() redirects here.

_Bool wasm_ui_prepare_cartridge(struct cart_config *cc) {
	if (cc->rom) {
		wasm_wget_rom(cc->rom);
	}
	if (cc->rom2) {
		wasm_wget_rom(cc->rom2);
	}
	if (wasm_waiting_files == 0) {
		return 1;
	}
	event_queue_auto(&UI_EVENT_LIST, DELEGATE_AS0(void, do_wasm_set_cartridge, (void *)(intptr_t)cc->id), 0);
	return 0;
}

// Helper while loading software from the browser - prepare a specific machine
// with a specific default cartridge.

void wasm_set_machine_cart(const char *machine, const char *cart,
			   const char *cart_rom, const char *cart_rom2) {
	struct machine_config *mc = machine_config_by_name(machine);
	struct cart_config *cc = cart_config_by_name(cart);
	if (!mc)
		return;
	wasm_ui_prepare_machine(mc);
	if (mc->default_cart) {
		free(mc->default_cart);
		mc->default_cart = NULL;
	}
	mc->cart_enabled = 0;
	if (cc && cc->name) {
		mc->default_cart = xstrdup(cc->name);
		mc->cart_enabled = 1;
		if (cart_rom) {
			if (cc->rom) {
				free(cc->rom);
			}
			cc->rom = xstrdup(cart_rom);
		}
		if (cc->rom2) {
			free(cc->rom2);
			cc->rom2 = NULL;
		}
		if (cart_rom2) {
			cc->rom2 = xstrdup(cart_rom2);
		}
		wasm_ui_prepare_cartridge(cc);
	}
	event_queue_auto(&UI_EVENT_LIST, DELEGATE_AS0(void, do_wasm_set_machine, (void *)(intptr_t)mc->id), 0);
}

// Load (and optionally autorun) file from web

enum wasm_load_file_type {
	wasm_load_file_type_load = 0,
	wasm_load_file_type_run = 1,
	wasm_load_file_type_tape = 2,
	wasm_load_file_type_disk = 3,
};

struct wasm_event_load_file {
	char *filename;
	enum wasm_load_file_type type;
	int drive;
};

static void do_wasm_load_file(void *sptr) {
	struct wasm_event_load_file *ev = sptr;
	switch (ev->type) {
	case wasm_load_file_type_load:
	case wasm_load_file_type_run:
		xroar_load_file_by_type(ev->filename, ev->type);
		break;
	case wasm_load_file_type_tape:
		xroar_insert_input_tape_file(ev->filename);
		break;
	case wasm_load_file_type_disk:
		xroar_insert_disk_file(ev->drive, ev->filename);
		break;
	}
	free(ev->filename);
	free(ev);
}

void wasm_load_file(const char *filename, int type, int drive) {
	struct wasm_event_load_file *ev = xmalloc(sizeof(*ev));
	ev->filename = xstrdup(filename);
	ev->type = type;
	ev->drive = drive;
	wasm_wget(filename);
	event_queue_auto(&UI_EVENT_LIST, DELEGATE_AS0(void, do_wasm_load_file, ev), 0);
}

// Configure joystick ports

struct wasm_event_set_joystick {
	int port;
	char *value;
};

static void do_wasm_set_joystick(void *sptr) {
	struct wasm_event_set_joystick *ev = sptr;
	xroar_set_joystick(1, ev->port, ev->value);
	free(ev->value);
	free(ev);
}

void wasm_set_joystick(int port, const char *value) {
	struct wasm_event_set_joystick *ev = xmalloc(sizeof(*ev));
	ev->port = port;
	ev->value = xstrdup(value);
	event_queue_auto(&UI_EVENT_LIST, DELEGATE_AS0(void, do_wasm_set_joystick, ev), 0);
}

// Submit BASIC commands

static void do_wasm_queue_basic(void *sptr) {
	char *text = sptr;
	keyboard_queue_basic(xroar_keyboard_interface, text);
	free(text);
}

void wasm_queue_basic(const char *string) {
	char *text = xstrdup(string);
	event_queue_auto(&UI_EVENT_LIST, DELEGATE_AS0(void, do_wasm_queue_basic, text), 0);
}

// Update window size.  Browser handles knowing what size things should be,
// then informs us here.

void wasm_resize(int w, int h) {
	if (global_uisdl2 && global_uisdl2->vo_window) {
		SDL_SetWindowSize(global_uisdl2->vo_window, w, h);
	}
}
