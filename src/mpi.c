/** \file
 *
 *  \brief Multi-Pak Interface (MPI) support.
 *
 *  \copyright Copyright 2014-2021 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "becker.h"
#include "cart.h"
#include "delegate.h"
#include "logging.h"
#include "mpi.h"
#include "part.h"
#include "serialise.h"

#define MPI_TYPE_TANDY (0)
#define MPI_TYPE_RACE  (1)

static struct cart *mpi_new(struct cart_config *);
static struct cart *race_new(struct cart_config *);

struct cart_module cart_mpi_module = {
	.name = "mpi",
	.description = "Multi-Pak Interface",
	.new = mpi_new,
};

struct cart_module cart_mpi_race_module = {
	.name = "race-cage",
	.description = "RACE Computer Expansion Cage",
	.new = race_new,
};

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
	SER_STRUCT_ELEM(struct mpi, cart, ser_type_unhandled), // 1
	SER_STRUCT_ELEM(struct mpi, switch_enable, ser_type_bool), // 2
	SER_STRUCT_ELEM(struct mpi, cts_route, ser_type_unsigned), // 3
	SER_STRUCT_ELEM(struct mpi, p2_route, ser_type_unsigned), // 4
	SER_STRUCT_ELEM(struct mpi, firq_state, ser_type_unsigned), // 5
	SER_STRUCT_ELEM(struct mpi, nmi_state, ser_type_unsigned), // 6
	SER_STRUCT_ELEM(struct mpi, halt_state, ser_type_unsigned), // 7
};
#define N_SER_STRUCT_MPI ARRAY_N_ELEMENTS(ser_struct_mpi)

#define MPI_SER_CART (1)

/* Protect against chained MPI initialisation */

static _Bool mpi_active = 0;

/* Slot configuration */

static char *slot_cart_name[4];
static unsigned initial_slot = 0;

/* Handle signals from cartridges */
static void mpi_set_firq(void *, _Bool);
static void mpi_set_nmi(void *, _Bool);
static void mpi_set_halt(void *, _Bool);

static void mpi_attach(struct cart *c);
static void mpi_detach(struct cart *c);
static void mpi_free(struct part *p);
static void mpi_serialise(struct part *p, struct ser_handle *sh);
static uint8_t mpi_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t mpi_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void mpi_reset(struct cart *c);
static _Bool mpi_has_interface(struct cart *c, const char *ifname);
static void mpi_attach_interface(struct cart *c, const char *ifname, void *intf);

static void select_slot(struct cart *c, unsigned D);

static _Bool mpi_finish(struct part *p) {
	struct mpi *mpi = (struct mpi *)p;

	// Find attached cartridges
	char id[6];
	for (int i = 0; i < 4; i++) {
		snprintf(id, sizeof(id), "slot%d", i);
		struct cart *c2 = (struct cart *)part_component_by_id_is_a(p, id, "cart");
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

static struct mpi *mpi_create(unsigned type) {
	// XXX this isn't going to work well with deserialisation
	if (mpi_active) {
		LOG_WARN("Chaining Multi-Pak Interfaces not supported.\n");
		return NULL;
	}
	mpi_active = 1;

	_Bool is_race = (type == MPI_TYPE_RACE);

	struct mpi *mpi = part_new(sizeof(*mpi));
	*mpi = (struct mpi){0};
	struct cart *c = &mpi->cart;
	part_init(&c->part, is_race ? "race-cage" : "mpi");
	c->part.free = mpi_free;
	c->part.serialise = mpi_serialise;
	c->part.finish = mpi_finish;

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

	mpi->is_race = is_race;
	mpi->switch_enable = is_race ? 0 : 1;

	for (int i = 0; i < 4; i++) {
		mpi->slot[i].mpi = mpi;
		mpi->slot[i].id = i;
		mpi->slot[i].cart = NULL;
	}

	if (!mpi->is_race) {
		select_slot(c, (initial_slot << 4) | initial_slot);
	} else {
		select_slot(c, 0);
	}

	return mpi;
}

static struct mpi *mpi_new_typed(struct cart_config *cc, unsigned type) {
	assert(cc != NULL);

	struct mpi *mpi = mpi_create(type);
	struct cart *c = &mpi->cart;
	struct part *p = &c->part;
	c->config = cc;

	char id[6];
	for (int i = 0; i < 4; i++) {
		snprintf(id, sizeof(id), "slot%d", i);
		if (slot_cart_name[i]) {
			part_add_component(&c->part, (struct part *)cart_new_named(slot_cart_name[i]), id);
		}
	}

	if (!mpi_finish(p)) {
		part_free(p);
		return NULL;
	}

	return mpi;
}

static struct cart *mpi_new(struct cart_config *cc) {
	struct mpi *mpi = mpi_new_typed(cc, MPI_TYPE_TANDY);
	return &mpi->cart;
}

static struct cart *race_new(struct cart_config *cc) {
	struct mpi *mpi = mpi_new_typed(cc, MPI_TYPE_RACE);
	return &mpi->cart;
}

static void mpi_serialise(struct part *p, struct ser_handle *sh) {
	struct mpi *mpi = (struct mpi *)p;
	for (int tag = 1; !ser_error(sh) && (tag = ser_write_struct(sh, ser_struct_mpi, N_SER_STRUCT_MPI, tag, mpi)) > 0; tag++) {
		switch (tag) {
		case MPI_SER_CART:
			cart_serialise(&mpi->cart, sh, tag);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	ser_write_close_tag(sh);
}

static struct part *mpi_deserialise_typed(unsigned type, struct ser_handle *sh) {
	struct mpi *mpi = mpi_create(type);
	int tag;
	while (!ser_error(sh) && (tag = ser_read_struct(sh, ser_struct_mpi, N_SER_STRUCT_MPI, mpi))) {
		switch (tag) {
		case MPI_SER_CART:
			cart_deserialise(&mpi->cart, sh);
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
	if (ser_error(sh)) {
		part_free((struct part *)mpi);
		return NULL;
	}
	return (struct part *)mpi;
}

struct part *mpi_deserialise(struct ser_handle *sh) {
	return mpi_deserialise_typed(MPI_TYPE_TANDY, sh);
}

struct part *race_deserialise(struct ser_handle *sh) {
	return mpi_deserialise_typed(MPI_TYPE_RACE, sh);
}

static void mpi_reset(struct cart *c) {
	struct mpi *mpi = (struct mpi *)c;
	mpi->firq_state = 0;
	mpi->nmi_state = 0;
	mpi->halt_state = 0;
	for (int i = 0; i < 4; i++) {
		struct cart *c2 = mpi->slot[i].cart;
		if (c2 && c2->reset) {
			c2->reset(c2);
		}
	}
	mpi->cart.EXTMEM = 0;
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

static void mpi_free(struct part *p) {
	(void)p;
	mpi_active = 0;
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

static void debug_cart_name(struct cart *c) {
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

static void select_slot(struct cart *c, unsigned D) {
	struct mpi *mpi = (struct mpi *)c;
	mpi->cts_route = (D >> 4) & 3;
	mpi->p2_route = D & 3;
	if (logging.level >= 2) {
		LOG_PRINT("MPI selected: %02x: ROM=", D & 0x33);
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
	select_slot(c, (slot << 4) | slot);
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
			select_slot(c, ((D & 3) << 4) | (D & 3));
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
			select_slot(c, D);
			return D;
		}
	} else {
		if (A == 0xfeff) {
			mpi->switch_enable = 0;
			select_slot(c, ((D & 3) << 4) | (D & 3));
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

/* Configure */

void mpi_set_initial(int slot) {
	if (slot < 0 || slot > 3) {
		LOG_WARN("MPI: Invalid slot '%d'\n", slot);
		return;
	}
	initial_slot = slot;
}

void mpi_set_cart(int slot, const char *name) {
	if (slot < 0 || slot > 3) {
		LOG_WARN("MPI: Invalid slot '%d'\n", slot);
		return;
	}
	if (slot_cart_name[slot]) {
		free(slot_cart_name[slot]);
	}
	slot_cart_name[slot] = xstrdup(name);
}

// parts management frees attached carts, but for now there's still some
// housekeeping to do:
void mpi_shutdown(void) {
	for (int i = 0; i < 4; i++) {
		if (slot_cart_name[i]) {
			free(slot_cart_name[i]);
			slot_cart_name[i] = NULL;
		}
	}
}
