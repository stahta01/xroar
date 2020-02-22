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

#ifndef XROAR_WASM_H_
#define XROAR_WASM_H_

struct machine_config;
struct cart_config;

// Called once per frame.
void wasm_ui_run(void *sptr);

// UI state update handler.
void wasm_ui_set_state(void *sptr, int tag, int value, const void *data);

// Hooks into xroar_set_machine() and xroar_set_cart() to asyncify.
_Bool wasm_ui_prepare_machine(struct machine_config *mc);
_Bool wasm_ui_prepare_cartridge(struct cart_config *cc);

// Async browser interfaces to certain functions.
void wasm_set_machine_cart(const char *machine, const char *cart, const char *cart_rom, const char *cart_rom2);
void wasm_load_file(const char *filename, int autorun);
void wasm_set_joystick(int port, const char *value);
void wasm_queue_basic(const char *string);

#endif
