
#include <xalloc.h>
#include "cart.h"
#include "becker.h"

/* number of 32KB banks in memory cartridge: 1, 4 or 16 */
#define EXTBANKS 4

static struct cart *nx32_new(struct cart_config *);

struct cart_module cart_nx32_module = {
	.name = "nx32",
	.description = "NX32 memory cartridge",
	.new = nx32_new,
};

struct nx32 {
	struct cart cart;
	uint8_t extmem[0x8000 * EXTBANKS];
	_Bool extmem_map;
	_Bool extmem_ty;
	uint8_t extmem_bank;
	_Bool have_becker;
};

static void nx32_reset(struct cart *c);
static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void nx32_detach(struct cart *c);

struct cart *nx32_new(struct cart_config *cc) {
	struct nx32 *n = xmalloc(sizeof(*n));
	*n = (struct nx32){0};
	struct cart *c = &n->cart;

	c->config = cc;
	cart_rom_init(c);
	c->read = nx32_read;
	c->write = nx32_write;
	c->reset = nx32_reset;
	c->detach = nx32_detach;

	n->have_becker = (cc->becker_port && becker_open());

	return c;
}

static void nx32_reset(struct cart *c) {
	struct nx32 *n = (struct nx32 *)c;
	n->extmem_map = 0;
	n->extmem_ty = 0;
	n->extmem_bank = 0;
	if (n->have_becker)
		becker_reset();
}

static void nx32_detach(struct cart *c) {
	struct nx32 *n = (struct nx32 *)c;
	if (n->have_becker)
		becker_close();
	cart_rom_detach(c);
}

static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct nx32 *n = (struct nx32 *)c;
	(void)R2;
	c->EXTMEM = 0;
	if (A > 0x7fff && A < 0xff00 && !n->extmem_ty && n->extmem_map) {
		c->EXTMEM = 1;
		return n->extmem[0x8000 * n->extmem_bank + (A & 0x7fff)];
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

static void nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct nx32 *n = (struct nx32 *)c;
	(void)R2;
	c->EXTMEM = 0;
	if ((A & ~1) == 0xFFDE) {
		n->extmem_ty = A & 1;
	} else if ((A & ~1) == 0xFFBE) {
		n->extmem_map = A & 1;
		n->extmem_bank = D & (EXTBANKS - 1);
		c->EXTMEM = 1;
	} else if (A > 0x7fff && A < 0xff00 && !n->extmem_ty && n->extmem_map) {
		n->extmem[0x8000 * n->extmem_bank + (A & 0x7fff)] = D;
		c->EXTMEM = 1;
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
