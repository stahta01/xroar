/** \file
 *
 *  \brief MC-10 cartridge support.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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

#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"

#include "cart.h"
#include "logging.h"
#include "machine.h"
#include "mc10_cart.h"
#include "part.h"
#include "serialise.h"
#include "xroar.h"

#define CART_SER_CART_CONFIG (1)

static const struct ser_struct ser_struct_mc10_cart[] = {
	SER_ID_STRUCT_UNHANDLED(CART_SER_CART_CONFIG),
	SER_ID_STRUCT_ELEM(2, struct mc10_cart, SEL),
};

static _Bool mc10_cart_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool mc10_cart_write_elem(void *sptr, struct ser_handle *sh, int tag);

// External; struct data nested by MC-10 carts:
const struct ser_struct_data mc10_cart_ser_struct_data = {
	.elems = ser_struct_mc10_cart,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc10_cart),
	.read_elem = mc10_cart_read_elem,
	.write_elem = mc10_cart_write_elem,
};

/**************************************************************************/

_Bool mc10_cart_finish(struct part *p) {
	(void)p;
	return 1;
}

_Bool mc10_cart_is_a(struct part *p, const char *name) {
	return strcmp(name, "mc10-cart") == 0 || cart_is_a(p, name);
}

static _Bool mc10_cart_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct cart *c = sptr;
	switch (tag) {
	case CART_SER_CART_CONFIG:
		c->config = cart_config_deserialise(sh);
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool mc10_cart_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct cart *c = sptr;
	switch (tag) {
	case CART_SER_CART_CONFIG:
		cart_config_serialise(c->config, sh, tag);
		break;
	default:
		return 0;
	}
	return 1;
}
