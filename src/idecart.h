/*

"Glenside" IDE cartridge support

Copyright 2015 Alan Cox

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_IDECART_H_
#define XROAR_IDECART_H_

struct cart_config;
struct cart;

struct cart *idecart_new(struct cart_config *cc);

#endif
