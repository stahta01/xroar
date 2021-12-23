/** \file
 *
 *  \brief Becker port support.
 *
 *  \copyright Copyright 2012-2021 Ciaran Anscomb
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
 *
 *  The "becker port" is an IP version of the usually-serial DriveWire
 *  protocol.
 */

#ifndef XROAR_BECKER_H_
#define XROAR_BECKER_H_

#include <stdint.h>

#define BECKER_IP_DEFAULT "127.0.0.1"
#define BECKER_PORT_DEFAULT "65504"

struct becker;

#ifdef WANT_BECKER

struct becker *becker_open(void);
void becker_close(struct becker *b);
void becker_reset(struct becker *b);
uint8_t becker_read_status(struct becker *b);
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
