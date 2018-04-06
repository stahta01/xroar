/*

Becker port support

Copyright 2012-2014 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version.

See COPYING.GPL for redistribution conditions.

The "becker port" is an IP version of the usually-serial DriveWire protocol.

*/

#ifndef XROAR_BECKER_H_
#define XROAR_BECKER_H_

#include <stdint.h>

#define BECKER_IP_DEFAULT "127.0.0.1"
#define BECKER_PORT_DEFAULT "65504"

_Bool becker_open(void);
void becker_close(void);
void becker_reset(void);
uint8_t becker_read_status(void);
uint8_t becker_read_data(void);
void becker_write_data(uint8_t D);

#endif
