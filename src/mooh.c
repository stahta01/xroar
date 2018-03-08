/*
 *  Emulation of MOOH memory & SPI board
 *
 *  Copyright 2016-2018 Tormod Volden
 *
 *  See COPYING.GPL for redistribution conditions.
 */

#include <xalloc.h>
#include <stdio.h>
#include "cart.h"
#include "becker.h"

/* Number of 8KB mappable RAM pages in cartridge */
#define MEMPAGES 0x40
#define TASK_MASK 0x3F	/* 6 bit task registers */

uint8_t spi65_read(uint8_t reg);
void spi65_write(uint8_t reg, uint8_t value);
void spi65_reset(void);

struct mooh {
	struct cart cart;
	uint8_t extmem[0x2000 * MEMPAGES];
	_Bool mmu_enable;
	_Bool crm_enable;
	uint8_t taskreg[8][2];
	uint8_t task;
	_Bool have_becker;
	char crt9128_reg_addr;
};

static struct cart *mooh_new(struct cart_config *);

struct cart_module cart_mooh_module = {
	.name = "mooh",
	.description = "mooh memory cartridge",
	.new = mooh_new,
};

static void mooh_reset(struct cart *c);
static uint8_t mooh_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void mooh_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void mooh_detach(struct cart *c);

struct cart *mooh_new(struct cart_config *cc) {
	struct mooh *n = xmalloc(sizeof(*n));
	*n = (struct mooh){0};
	struct cart *c = &n->cart;

	c->config = cc;
	cart_rom_init(c);
	c->read = mooh_read;
	c->write = mooh_write;
	c->reset = mooh_reset;
	c->detach = mooh_detach;

	n->have_becker = (cc->becker_port && becker_open());

	return c;
}

static void mooh_reset(struct cart *c) {
	struct mooh *n = (struct mooh *)c;
	int i;

	n->mmu_enable = 0;
	n->crm_enable = 0;
	n->task = 0;
	for (i = 0; i < 8; i++)
		n->taskreg[i][0] = n->taskreg[i][1] = 0xFF & TASK_MASK;
	if (n->have_becker)
		becker_reset();
	n->crt9128_reg_addr = 0;

	spi65_reset();
}

static void mooh_detach(struct cart *c) {
	struct mooh *n = (struct mooh *)c;
	if (n->have_becker)
		becker_close();
}

static uint8_t mooh_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct mooh *n = (struct mooh *)c;
	int segment;
	int offset;
	int bank;
	int crm;

	c->EXTMEM = 0;

        if (R2)
                return c->rom_data[A & 0x3fff];

	if ((A & 0xFFFC) == 0xFF6C)
		return spi65_read(A & 3);

	if ((A & 0xFFF0) == 0xFFA0) {
		return n->taskreg[A & 7][(A & 8) >> 3];
#if 0
	/* not implemented in MOOH fw 1 */
	} else if (A == 0xFF90) {
		return (n->crm_enable << 3) | (n->mmu_enable << 6);
	} else if (A == 0xFF91) {
		return n->task;
#endif
	} else if (n->mmu_enable && (A < 0xFF00 || (A >= 0xFFF0 && n->crm_enable))) {
		segment = A >> 13;
		offset = A & 0x1FFF;
		if (n->crm_enable && (A >> 8) == 0xFE) {
			crm = 1;
			bank = 0x3F; /* used for storing crm */
			offset |= 0x100; /* A8 high */
		} else if (n->crm_enable && A >= 0xFFF0) {
			crm = 1;
			bank = 0x3F;
		} else {
			crm = 0;
			bank = n->taskreg[segment][n->task];
		}

		if (bank != 0x3F || crm) {
			c->EXTMEM = 1;
			return n->extmem[bank * 0x2000 + offset];
		}
	}
	if (P2 && n->have_becker) {
		switch (A & 3) {
		case 0x1:
			return becker_read_status();
		case 0x2:
			return becker_read_data();
		default:
			break;
		}
	}
	return D;
}

static void mooh_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct mooh *n = (struct mooh *)c;
	int segment;
	int offset;
	int bank;
	int crm;

	(void)R2;
	c->EXTMEM = 0;


	if ((A & 0xFFFC) == 0xFF6C)
		spi65_write(A & 3, D);

	/* poor man's CRT9128 Wordpak emulation */
	if (A == 0xFF7D)
		n->crt9128_reg_addr = D;
	if (A == 0xFF7C && n->crt9128_reg_addr == 0x0d)
		fprintf(stderr, "%c", D);

	if ((A & 0xFFF0) == 0xFFA0) {
		n->taskreg[A & 7][(A & 8) >> 3] = D & TASK_MASK;
	} else if (A == 0xFF90) {
		n->crm_enable = (D & 8) >> 3;
		n->mmu_enable = (D & 64) >> 6;
	} else if (A == 0xFF91) {
		n->task = D & 1;
	} else if (n->mmu_enable && (A < 0xFF00 || (A >= 0xFFF0 && n->crm_enable))) {
		segment = A >> 13;
		offset = A & 0x1FFF;
		if (n->crm_enable && (A >> 8) == 0xFE) {
			crm = 1;
			bank = 0x3F; /* last 8K bank */
			offset |= 0x100; /* A8 high */
		} else if (n->crm_enable && A >= 0xFFF0) {
			crm = 1;
			bank = 0x3F;
		} else {
			crm = 0;
			bank = n->taskreg[segment][n->task];
		}

		if (bank != 0x3F || crm) {
			n->extmem[bank * 0x2000 + offset] = D;
			c->EXTMEM = 1;
		}
	}
	if (P2 && n->have_becker) {
		switch (A & 3) {
		case 0x2:
			becker_write_data(D);
			break;
		default:
			break;
		}
	}
}
