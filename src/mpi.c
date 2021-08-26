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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "becker.h"
#include "cart.h"
#include "delegate.h"
#include "logging.h"
#include "mpi.h"
#include "part.h"

static struct cart *mpi_new(struct cart_config *);
static struct cart *race_new(struct cart_config *);

struct cart_module cart_mpi_module = {
	.name = "mpi",
	.description = "Multi-Pak Interface",
	.new = mpi_new,
};

struct cart_module cart_mpi_race_module = {
	.name = "mpi-race",
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
	_Bool switch_enable;
	_Bool is_race;
	int cts_route;
	int p2_route;
	unsigned firq_state;
	unsigned nmi_state;
	unsigned halt_state;
	struct mpi_slot slot[4];
};

/* Protect against chained MPI initialisation */

static _Bool mpi_active = 0;

/* Slot configuration */

static char *slot_cart_name[4];
static unsigned initial_slot = 0;

/* Handle signals from cartridges */
static void set_firq(void *, _Bool);
static void set_nmi(void *, _Bool);
static void set_halt(void *, _Bool);

static void mpi_attach(struct cart *c);
static void mpi_detach(struct cart *c);
static void mpi_free(struct part *p);
static uint8_t mpi_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t mpi_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void mpi_reset(struct cart *c);
static _Bool mpi_has_interface(struct cart *c, const char *ifname);
static void mpi_attach_interface(struct cart *c, const char *ifname, void *intf);

static void select_slot(struct cart *c, unsigned D);

static struct cart *mpi_new(struct cart_config *cc) {
	if (mpi_active) {
		LOG_WARN("Chaining Multi-Pak Interfaces not supported.\n");
		return NULL;
	}
	mpi_active = 1;

	struct mpi *m = part_new(sizeof(*m));
	*m = (struct mpi){0};
	struct cart *c = &m->cart;
	part_init(&c->part, "mpi");
	c->part.free = mpi_free;

	c->config = cc;

	c->attach = mpi_attach;
	c->detach = mpi_detach;

	c->read = mpi_read;
	c->write = mpi_write;
	c->reset = mpi_reset;

	c->signal_firq = DELEGATE_DEFAULT1(void, bool);
	c->signal_nmi = DELEGATE_DEFAULT1(void, bool);
	c->signal_halt = DELEGATE_DEFAULT1(void, bool);
	c->EXTMEM = 0;

	c->has_interface = mpi_has_interface;
	c->attach_interface = mpi_attach_interface;

	m->switch_enable = 1;
	m->cts_route = 0;
	m->p2_route = 0;
	m->firq_state = 0;
	m->nmi_state = 0;
	m->halt_state = 0;
	char id[] = { 's', 'l', 'o', 't', '0', 0 };
	for (int i = 0; i < 4; i++) {
		*(id+4) = '0' + i;
		m->slot[i].mpi = m;
		m->slot[i].id = i;
		m->slot[i].cart = NULL;
		if (slot_cart_name[i]) {
			struct cart *c2 = cart_new_named(slot_cart_name[i]);
			if (c2) {
				c2->signal_firq = DELEGATE_AS1(void, bool, set_firq, &m->slot[i]);
				c2->signal_nmi = DELEGATE_AS1(void, bool, set_nmi, &m->slot[i]);
				c2->signal_halt = DELEGATE_AS1(void, bool, set_halt, &m->slot[i]);
				m->slot[i].cart = c2;
				part_add_component(&c->part, (struct part *)c2, id);
			}
		}
	}
	select_slot(c, (initial_slot << 4) | initial_slot);

	return c;
}

static struct cart *race_new(struct cart_config *cc) {
	struct cart *c = mpi_new(cc);
	if (!c)
		return NULL;
	struct mpi *m = (struct mpi *)c;
	m->is_race = 1;
	m->switch_enable = 0;
	select_slot(c, 0);
	return c;
}

static void mpi_reset(struct cart *c) {
	struct mpi *m = (struct mpi *)c;
	m->firq_state = 0;
	m->nmi_state = 0;
	m->halt_state = 0;
	for (int i = 0; i < 4; i++) {
		struct cart *c2 = m->slot[i].cart;
		if (c2 && c2->reset) {
			c2->reset(c2);
		}
	}
	m->cart.EXTMEM = 0;
}

static void mpi_attach(struct cart *c) {
	struct mpi *m = (struct mpi *)c;
	for (int i = 0; i < 4; i++) {
		if (m->slot[i].cart && m->slot[i].cart->attach) {
			m->slot[i].cart->attach(m->slot[i].cart);
		}
	}
}

static void mpi_detach(struct cart *c) {
	struct mpi *m = (struct mpi *)c;
	for (int i = 0; i < 4; i++) {
		if (m->slot[i].cart && m->slot[i].cart->detach) {
			m->slot[i].cart->detach(m->slot[i].cart);
		}
	}
}

static void mpi_free(struct part *p) {
	(void)p;
	mpi_active = 0;
}

static _Bool mpi_has_interface(struct cart *c, const char *ifname) {
	struct mpi *m = (struct mpi *)c;
	for (int i = 0; i < 4; i++) {
		struct cart *c2 = m->slot[i].cart;
		if (c2 && c2->has_interface) {
			if (c2->has_interface(c2, ifname))
				return 1;
		}
	}
	return 0;
}

static void mpi_attach_interface(struct cart *c, const char *ifname, void *intf) {
	struct mpi *m = (struct mpi *)c;
	for (int i = 0; i < 4; i++) {
		struct cart *c2 = m->slot[i].cart;
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
	struct mpi *m = (struct mpi *)c;
	m->cts_route = (D >> 4) & 3;
	m->p2_route = D & 3;
	if (logging.level >= 2) {
		LOG_PRINT("MPI selected: %02x: ROM=", D & 0x33);
		debug_cart_name(m->slot[m->cts_route].cart);
		LOG_PRINT(", IO=");
		debug_cart_name(m->slot[m->p2_route].cart);
		LOG_PRINT("\n");
	}
	DELEGATE_CALL(m->cart.signal_firq, m->firq_state & (1 << m->cts_route));
}

void mpi_switch_slot(struct cart *c, unsigned slot) {
	struct mpi *m = (struct mpi *)c;
	if (!m || !m->switch_enable)
		return;
	if (slot > 3)
		return;
	select_slot(c, (slot << 4) | slot);
}

static uint8_t mpi_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct mpi *m = (struct mpi *)c;
	m->cart.EXTMEM = 0;
	if (!m->is_race) {
		if (A == 0xff7f) {
			return (m->cts_route << 4) | m->p2_route;
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
		struct cart *p2c = m->slot[m->p2_route].cart;
		if (p2c) {
			D = p2c->read(p2c, A, 1, R2, D);
		}
	}
	if (R2) {
		struct cart *r2c = m->slot[m->cts_route].cart;
		if (r2c) {
			D = r2c->read(r2c, A, P2, 1, D);
		}
	}
	if (!P2 && !R2) {
		for (unsigned i = 0; i < 4; i++) {
			if (m->slot[i].cart) {
				D = m->slot[i].cart->read(m->slot[i].cart, A, 0, 0, D);
				m->cart.EXTMEM |= m->slot[i].cart->EXTMEM;
			}
		}
	}
	return D;
}

static uint8_t mpi_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct mpi *m = (struct mpi *)c;
	m->cart.EXTMEM = 0;
	if (!m->is_race) {
		if (A == 0xff7f) {
			m->switch_enable = 0;
			select_slot(c, D);
			return D;
		}
	} else {
		if (A == 0xfeff) {
			m->switch_enable = 0;
			select_slot(c, ((D & 3) << 4) | (D & 3));
			return D;
		}
	}
	if (P2) {
		struct cart *p2c = m->slot[m->p2_route].cart;
		if (p2c) {
			D = p2c->write(p2c, A, 1, R2, D);
		}
	}
	if (R2) {
		struct cart *r2c = m->slot[m->cts_route].cart;
		if (r2c) {
			D = r2c->write(r2c, A, P2, 1, D);
		}
	}
	if (!P2 && !R2) {
		for (unsigned i = 0; i < 4; i++) {
			if (m->slot[i].cart) {
				D = m->slot[i].cart->write(m->slot[i].cart, A, 0, 0, D);
				m->cart.EXTMEM |= m->slot[i].cart->EXTMEM;
			}
		}
	}
	return D;
}

// FIRQ line is treated differently.

static void set_firq(void *sptr, _Bool value) {
	struct mpi_slot *ms = sptr;
	struct mpi *m = ms->mpi;
	unsigned firq_bit = 1 << ms->id;
	if (value) {
		m->firq_state |= firq_bit;
	} else {
		m->firq_state &= ~firq_bit;
	}
	DELEGATE_CALL(m->cart.signal_firq, m->firq_state & (1 << m->cts_route));
}

static void set_nmi(void *sptr, _Bool value) {
	struct mpi_slot *ms = sptr;
	struct mpi *m = ms->mpi;
	unsigned nmi_bit = 1 << ms->id;
	if (value) {
		m->nmi_state |= nmi_bit;
	} else {
		m->nmi_state &= ~nmi_bit;
	}
	DELEGATE_CALL(m->cart.signal_nmi, m->nmi_state);
}

static void set_halt(void *sptr, _Bool value) {
	struct mpi_slot *ms = sptr;
	struct mpi *m = ms->mpi;
	unsigned halt_bit = 1 << ms->id;
	if (value) {
		m->halt_state |= halt_bit;
	} else {
		m->halt_state &= ~halt_bit;
	}
	DELEGATE_CALL(m->cart.signal_halt, m->halt_state);
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
