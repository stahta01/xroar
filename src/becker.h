/* Becker port support.
 *
 * Copyright 2012-2021 Ciaran Anscomb
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

/* The "becker port" is a port-mapped version of the usually-serial DriveWire
 * protocol.  Originally created for the CoCo3FPGA, it found use in emulators
 * over IP.
 */

#ifndef XROAR_BECKER_H_
#define XROAR_BECKER_H_

#include <stdint.h>
#include <stdlib.h>

#define BECKER_IP_DEFAULT "127.0.0.1"
#define BECKER_PORT_DEFAULT "65504"

struct becker;

#ifdef WANT_BECKER

// Connect to the globally-configured server and if successful, return a handle
// used to send and receive data.

struct becker *becker_open(void);

// Disconnect and free handle.

void becker_close(struct becker *b);

// Doesn't affect the connection, but resets logging in a useful way.

void becker_reset(struct becker *b);

// Read status port.  Bit 1 is set if there is any data to be read.

uint8_t becker_read_status(struct becker *b);

// I/O

uint8_t becker_read_data(struct becker *b);
void becker_write_data(struct becker *b, uint8_t D);

#else

inline struct becker *becker_open(void) { return NULL; }
inline void becker_close(struct becker *b) { (void)b; }
inline void becker_reset(struct becker *b) { (void)b; }
inline uint8_t becker_read_status(struct becker *b) { (void)b; return 0; }
inline uint8_t becker_read_data(struct becker *b) { (void)b; return 0; }
inline void becker_write_data(struct becker *b, uint8_t D) { (void)b; (void)D; }

#endif

#endif
