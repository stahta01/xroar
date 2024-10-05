/** \file
 *
 *  \brief WebAssembly (emscripten) support.
 *
 *  \copyright Copyright 2019-2024 Ciaran Anscomb
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

#ifdef HAVE_WASM

#ifndef XROAR_WASM_H_
#define XROAR_WASM_H_

#include <stdio.h>

#include "sdl2/common.h"

struct machine_config;
struct cart_config;

struct ui_wasm_interface {
	struct ui_sdl2_interface ui_sdl2_interface;

	// Top level messenger client id
	int msgr_client_id;

	double last_t;
	double tickerr;
};

// Finish the initialisation started in main().

void wasm_finish_init(struct ui_interface *ui);

// Fetch a file.  Locks the file to prevent simultaneous fetch attempts.  Won't
// re-fetch the same file (whether or not it succeeded).

void wasm_wget(const char *file);

// Try to ensure all ROM images required for a machine or cartridge are
// available.  Returns true if all ROMs are present, or at least a download has
// been attempted.  If any weren't already downloaded, submits wasm_wget()
// requests and returns false.

_Bool wasm_ui_prepare_machine(struct machine_config *mc);
_Bool wasm_ui_prepare_cartridge(struct cart_config *cc);

// Queue simple value-only message as an event

void wasm_queue_message_value_event(int tag, int value);

// UI message wrappers

void wasm_set_machine(int value);
void wasm_set_cartridge(int value);
void wasm_set_tape_playing(int value);
void wasm_set_fullscreen(int value);
void wasm_set_cmp_fs(int value);
void wasm_set_cmp_fsc(int value);
void wasm_set_cmp_system(int value);
void wasm_set_cmp_colour_killer(int value);
void wasm_set_ccr(int value);
void wasm_set_picture(int value);
void wasm_set_ntsc_scaling(int value);
void wasm_set_tv_input(int value);
void wasm_set_vdg_inverse(int value);
void wasm_set_brightness(int value);
void wasm_set_contrast(int value);
void wasm_set_saturation(int value);
void wasm_set_hue(int value);
void wasm_set_gain(float value);
void wasm_set_joystick_port(int port, int value);
void wasm_set_joystick_by_name(int port, const char *name);

// Browser interfaces to certain functions

void wasm_set_machine_cart(const char *machine, const char *cart, const char *cart_rom, const char *cart_rom2);
void wasm_load_file(const char *filename, int type, int drive);
void wasm_queue_basic(const char *string);
void wasm_resize(int w, int h);
void wasm_vdrive_flush(void);

#endif

#endif
