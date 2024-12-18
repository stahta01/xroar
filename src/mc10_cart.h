/** \file
 *
 *  \brief MC-10 cartridge support.
 *
 *  \copyright Copyright 2005-2024 Ciaran Anscomb
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

#ifndef XROAR_MC10_CART_H_
#define XROAR_MC10_CART_H_

#include <stdint.h>

#include "delegate.h"

#include "cart.h"

struct part;
struct ser_struct_data;

struct mc10_cart {
	struct cart cart;

	// Read & write cycles.  Called every cycle before decode.  If SEL
	// is not asserted, called again when cartridge IO (P2) or ROM (R2)
	// areas are accessed.
	uint8_t (*read)(struct mc10_cart *c, uint16_t A, uint8_t D);
	uint8_t (*write)(struct mc10_cart *c, uint16_t A, uint8_t D);

	// Cartridge asserts this to inhibit usual address decode by host.
	_Bool SEL;

	// Ways for the cartridge to signal interrupt events to the host.
	DELEGATE_T1(void, bool) signal_nmi;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern const struct ser_struct_data mc10_cart_ser_struct_data;

_Bool mc10_cart_finish(struct part *p);
_Bool mc10_cart_is_a(struct part *p, const char *name);

#endif
