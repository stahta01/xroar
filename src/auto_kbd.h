/* Automatic keyboard entry.
 *
 * Copyright 2023 Ciaran Anscomb
 *
 * This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 * XRoar is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * See COPYING.GPL for redistribution conditions.
 */

/* Automatically "types" the contents of strings or file into a machine.
 *
 * Currently implemented using machine breakpoints dependent on known BASIC
 * ROM.  Could in future fail over to typing at a reasonable rate.
 *
 * Any string or file submitted will be queued and typed in turn.
 */

#ifndef XROAR_AUTO_KBD_H_
#define XROAR_AUTO_KBD_H_

#include "delegate.h"
#include "sds.h"

#include "dkbd.h"

struct machine;

struct auto_kbd;

// Create a new auto_kbd, using the supplied machine.

struct auto_kbd *auto_kbd_new(struct machine *m);

// Free auto_kbd.

void auto_kbd_free(struct auto_kbd *ak);

// Queue pre-parsed string to be typed.

void ak_type_string_len(struct auto_kbd *ak, const char *str, size_t len);
void ak_type_sds(struct auto_kbd *ak, sds s);

// Queue string to be parsed for escape characters then typed.

void ak_parse_type_string(struct auto_kbd *ak, const char *str);

// Queue typing a whole file.

void ak_type_file(struct auto_kbd *ak, const char *filename);

#endif
