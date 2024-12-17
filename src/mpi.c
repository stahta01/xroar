/** \file
 *
 *  \brief Multi-Pak Interface (MPI) support.
 *
 *  \copyright Copyright 2014-2024 Ciaran Anscomb
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
 *  \par Sources
 *
 *  - http://worldofdragon.org/index.php?title=RACE_Computer_Expansion_Cage
 *
 *  Also supports the RACE Computer Expansion Cage - similar to the MPI, but
 *  with some slightly different behaviour:
 *
 *  - No separate IO select.
 *
 *  - Select register is at $FEFF.
 *
 *  - Reading $FEFF does same as writing (reference suggests it sets slot to
 *    '2', but my guess is that this just happened to be on the data bus at the
 *    time it was tested (confirmed for PEEK).
 */

#include "top-config.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "becker.h"
#include "cart.h"
#include "delegate.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"

#define MPI_TYPE_TANDY (0)
#define MPI_TYPE_RACE  (1)

struct mpi;

struct mpi_slot {
	struct mpi *mpi;
	int id;
	struct cart *cart;
};

struct mpi {
	struct cart cart;
	_Bool is_race;
	_Bool switch_enable;
	unsigned cts_route;
	unsigned p2_route;
	unsigned firq_state;
	unsigned nmi_state;
	unsigned halt_state;
	struct mpi_slot slot[4];
};

static const struct ser_struct ser_struct_mpi[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_ELEM(2, struct mpi, switch_enable),
	SER_ID_STRUCT_ELEM(3, struct mpi, cts_route),
	SER_ID_STRUCT_ELEM(4, struct mpi, p2_route),
	SER_ID_STRUCT_ELEM(5, struct mpi, firq_state),
	SER_ID_STRUCT_ELEM(6, struct mpi, nmi_state),
	SER_ID_STRUCT_ELEM(7, struct mpi, halt_state),
};

static const struct ser_struct_data mpi_ser_struct_data = {
	.elems = ser_struct_mpi,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mpi),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Protect against chained MPI initialisation */

static _Bool mpi_active = 0;

/* Handle signals from cartridges */
static void mpi_set_firq(void *, _Bool);
static void mpi_set_nmi(void *, _Bool);
static void mpi_set_halt(void *, _Bool);

static void mpi_attach(struct cart *c);
static void mpi_detach(struct cart *c);
static void mpi_free(struct part *p);
static uint8_t mpi_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t mpi_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void mpi_reset(struct cart *c, _Bool hard);
static _Bool mpi_has_interface(struct cart *c, const char *ifname);
static void mpi_attach_interface(struct cart *c, const char *ifname, void *intf);

static void select_slot(struct mpi *, unsigned D);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MPI part creation

static struct part *mpi_allocate(void);
static void mpi_initialise(struct part *p, void *options);
static _Bool mpi_finish(struct part *p);
static void mpi_free(struct part *p);

static const struct partdb_entry_funcs mpi_funcs = {
	.allocate = mpi_allocate,
	.initialise = mpi_initialise,
	.finish = mpi_finish,
	.free = mpi_free,

	.ser_struct_data = &mpi_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry mpi_part = { .partdb_entry = { .name = "mpi", .description = "Tandy | Multi-Pak Interface", .funcs = &mpi_funcs } };
const struct cart_partdb_entry race_part = { .partdb_entry = { .name = "mpi-race", .description = "RACE | Computer Expansion Cage", .funcs = &mpi_funcs } };

static struct part *mpi_allocate(void) {
	struct mpi *mpi = part_new(sizeof(*mpi));
	struct cart *c = &mpi->cart;
	struct part *p = &c->part;

	*mpi = (struct mpi){0};

	c->attach = mpi_attach;
	c->detach = mpi_detach;
	c->read = mpi_read;
	c->write = mpi_write;
	c->reset = mpi_reset;
	c->signal_firq = DELEGATE_DEFAULT1(void, bool);
	c->signal_nmi = DELEGATE_DEFAULT1(void, bool);
	c->signal_halt = DELEGATE_DEFAULT1(void, bool);
	c->has_interface = mpi_has_interface;
	c->attach_interface = mpi_attach_interface;

	for (int i = 0; i < 4; i++) {
		mpi->slot[i].mpi = mpi;
		mpi->slot[i].id = i;
		mpi->slot[i].cart = NULL;
	}

	return p;
}

static void mpi_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	struct mpi *mpi = (struct mpi *)p;
	struct cart *c = &mpi->cart;

	c->config = cc;

	mpi->is_race = options && (strcmp(cc->type, "mpi-race") == 0);
	mpi->switch_enable = mpi->is_race ? 0 : 1;

	char id[6];
	for (unsigned i = 0; i < 4; ++i) {
		snprintf(id, sizeof(id), "slot%u", i);
		if (cc->mpi.slot_cart_name[i]) {
			part_add_component(&c->part, (struct part *)cart_create(cc->mpi.slot_cart_name[i]), id);
		}
	}

	int initial_slot = cc->mpi.initial_slot;
	if (initial_slot < 0 || initial_slot > 3 || mpi->is_race)
		initial_slot = 0;

	select_slot(mpi, (initial_slot << 4) | initial_slot);
}

static _Bool mpi_finish(struct part *p) {
	struct mpi *mpi = (struct mpi *)p;

	// Find attached cartridges
	char id[6];
	for (unsigned i = 0; i < 4; ++i) {
		snprintf(id, sizeof(id), "slot%u", i);
		struct cart *c2 = (struct cart *)part_component_by_id_is_a(p, id, "dragon-cart");
		if (c2) {
			c2->signal_firq = DELEGATE_AS1(void, bool, mpi_set_firq, &mpi->slot[i]);
			c2->signal_nmi = DELEGATE_AS1(void, bool, mpi_set_nmi, &mpi->slot[i]);
			c2->signal_halt = DELEGATE_AS1(void, bool, mpi_set_halt, &mpi->slot[i]);
			mpi->slot[i].cart = c2;
		} else {
			mpi->slot[i].cart = NULL;
		}
	}

	return 1;
}

static void mpi_free(struct part *p) {
	(void)p;
	mpi_active = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mpi_reset(struct cart *c, _Bool hard) {
	struct cart_config *cc = c->config;
	struct mpi *mpi = (struct mpi *)c;
	mpi->firq_state = 0;
	mpi->nmi_state = 0;
	mpi->halt_state = 0;
	for (int i = 0; i < 4; i++) {
		struct cart *c2 = mpi->slot[i].cart;
		if (c2 && c2->reset) {
			c2->reset(c2, hard);
		}
	}
	mpi->cart.EXTMEM = 0;

	int initial_slot = cc->mpi.initial_slot;
	if (initial_slot < 0 || initial_slot > 3 || mpi->is_race)
		initial_slot = 0;

	select_slot(mpi, (initial_slot << 4) | initial_slot);
}

static void mpi_attach(struct cart *c) {
	struct mpi *mpi = (struct mpi *)c;
	for (int i = 0; i < 4; i++) {
		if (mpi->slot[i].cart && mpi->slot[i].cart->attach) {
			mpi->slot[i].cart->attach(mpi->slot[i].cart);
		}
	}
}

static void mpi_detach(struct cart *c) {
	struct mpi *mpi = (struct mpi *)c;
	for (int i = 0; i < 4; i++) {
		if (mpi->slot[i].cart && mpi->slot[i].cart->detach) {
			mpi->slot[i].cart->detach(mpi->slot[i].cart);
		}
	}
}

static _Bool mpi_has_interface(struct cart *c, const char *ifname) {
	struct mpi *mpi = (struct mpi *)c;
	for (int i = 0; i < 4; i++) {
		struct cart *c2 = mpi->slot[i].cart;
		if (c2 && c2->has_interface) {
			if (c2->has_interface(c2, ifname))
				return 1;
		}
	}
	return 0;
}

static void mpi_attach_interface(struct cart *c, const char *ifname, void *intf) {
	struct mpi *mpi = (struct mpi *)c;
	for (int i = 0; i < 4; i++) {
		struct cart *c2 = mpi->slot[i].cart;
		if (c2 && c2->has_interface) {
			if (c2->has_interface(c2, ifname)) {
				c2->attach_interface(c2, ifname, intf);
				return;
			}
		}
	}
}

static void debug_cart_name(const struct cart *c) {
	if (!c) {
		LOG_PRINT("<empty>");
	} else if (!c->config) {
		LOG_PRINT("<unknown>");
	} else if (c->config->description) {
		LOG_PRINT("%s", c->config->description);
	} else {
		LOG_PRINT("%s", c->config->name);
	}
}

static void select_slot(struct mpi *mpi, unsigned D) {
	mpi->cts_route = (D >> 4) & 3;
	mpi->p2_route = D & 3;
	if (logging.level >= 2) {
		LOG_MOD_PRINT("mpi", "selected: %02x: ROM=", D & 0x33);
		debug_cart_name(mpi->slot[mpi->cts_route].cart);
		LOG_PRINT(", IO=");
		debug_cart_name(mpi->slot[mpi->p2_route].cart);
		LOG_PRINT("\n");
	}
	DELEGATE_CALL(mpi->cart.signal_firq, mpi->firq_state & (1 << mpi->cts_route));
}

void mpi_switch_slot(struct cart *c, unsigned slot) {
	struct mpi *mpi = (struct mpi *)c;
	if (!mpi || !mpi->switch_enable)
		return;
	if (slot > 3)
		return;
	select_slot(mpi, (slot << 4) | slot);
}

static uint8_t mpi_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct mpi *mpi = (struct mpi *)c;
	mpi->cart.EXTMEM = 0;
	if (!mpi->is_race) {
		if (A == 0xff7f) {
			return (mpi->cts_route << 4) | mpi->p2_route;
		}
	} else {
		if (A == 0xfeff) {
			// Same as writing!  Uses whatever happened to be on
			// the data bus.
			select_slot(mpi, ((D & 3) << 4) | (D & 3));
			return D;
		}
	}
	if (P2) {
		struct cart *p2c = mpi->slot[mpi->p2_route].cart;
		if (p2c) {
			D = p2c->read(p2c, A, 1, R2, D);
		}
	}
	if (R2) {
		struct cart *r2c = mpi->slot[mpi->cts_route].cart;
		if (r2c) {
			D = r2c->read(r2c, A, P2, 1, D);
		}
	}
	if (!P2 && !R2) {
		for (unsigned i = 0; i < 4; i++) {
			if (mpi->slot[i].cart) {
				D = mpi->slot[i].cart->read(mpi->slot[i].cart, A, 0, 0, D);
				mpi->cart.EXTMEM |= mpi->slot[i].cart->EXTMEM;
			}
		}
	}
	return D;
}

static uint8_t mpi_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct mpi *mpi = (struct mpi *)c;
	mpi->cart.EXTMEM = 0;
	if (!mpi->is_race) {
		if (A == 0xff7f) {
			mpi->switch_enable = 0;
			select_slot(mpi, D);
			return D;
		}
	} else {
		if (A == 0xfeff) {
			mpi->switch_enable = 0;
			select_slot(mpi, ((D & 3) << 4) | (D & 3));
			return D;
		}
	}
	if (P2) {
		struct cart *p2c = mpi->slot[mpi->p2_route].cart;
		if (p2c) {
			D = p2c->write(p2c, A, 1, R2, D);
		}
	}
	if (R2) {
		struct cart *r2c = mpi->slot[mpi->cts_route].cart;
		if (r2c) {
			D = r2c->write(r2c, A, P2, 1, D);
		}
	}
	if (!P2 && !R2) {
		for (unsigned i = 0; i < 4; i++) {
			if (mpi->slot[i].cart) {
				D = mpi->slot[i].cart->write(mpi->slot[i].cart, A, 0, 0, D);
				mpi->cart.EXTMEM |= mpi->slot[i].cart->EXTMEM;
			}
		}
	}
	return D;
}

// FIRQ line is treated differently.

static void mpi_set_firq(void *sptr, _Bool value) {
	struct mpi_slot *ms = sptr;
	struct mpi *mpi = ms->mpi;
	unsigned firq_bit = 1 << ms->id;
	if (value) {
		mpi->firq_state |= firq_bit;
	} else {
		mpi->firq_state &= ~firq_bit;
	}
	DELEGATE_CALL(mpi->cart.signal_firq, mpi->firq_state & (1 << mpi->cts_route));
}

static void mpi_set_nmi(void *sptr, _Bool value) {
	struct mpi_slot *ms = sptr;
	struct mpi *mpi = ms->mpi;
	unsigned nmi_bit = 1 << ms->id;
	if (value) {
		mpi->nmi_state |= nmi_bit;
	} else {
		mpi->nmi_state &= ~nmi_bit;
	}
	DELEGATE_CALL(mpi->cart.signal_nmi, mpi->nmi_state);
}

static void mpi_set_halt(void *sptr, _Bool value) {
	struct mpi_slot *ms = sptr;
	struct mpi *mpi = ms->mpi;
	unsigned halt_bit = 1 << ms->id;
	if (value) {
		mpi->halt_state |= halt_bit;
	} else {
		mpi->halt_state &= ~halt_bit;
	}
	DELEGATE_CALL(mpi->cart.signal_halt, mpi->halt_state);
}
