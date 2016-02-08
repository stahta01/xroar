
#include <xalloc.h>
#include "cart.h"
#include "becker.h"

/* number of 32KB banks in memory cartridge: 1, 4 or 16 */
#define EXTBANKS 4
static uint8_t extmem[0x8000 * EXTBANKS];
static _Bool extmem_map;
static _Bool extmem_ty;
static uint8_t extmem_bank;
static _Bool have_becker;

static struct cart *nx32_new(struct cart_config *);

struct cart_module cart_nx32_module = {
	.name = "nx32",
	.description = "NX32 memory cartridge",
	.new = nx32_new,
};

static void nx32_reset(struct cart *c);
static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);

struct cart *nx32_new(struct cart_config *cc) {
	struct cart *c = xmalloc(sizeof(*c));

	c->config = cc;
	cart_rom_init(c);
	c->reset = nx32_reset;
	c->read = nx32_read;
	c->write = nx32_write;

	have_becker = (cc->becker_port && becker_open());

	return c;
}

static void nx32_reset(struct cart *c) {
	extmem_map = 0;
	extmem_ty = 0;
	extmem_bank = 0;
	if (have_becker)
		becker_reset();
}

static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	if (A > 0x7fff && A < 0xff00 && !extmem_ty && extmem_map) {
		c->EXTMEM = 1;
		return extmem[0x8000 * extmem_bank + (A & 0x7fff)];
	}
	c->EXTMEM = 0;
	if (P2 && have_becker) {
		switch (A & 3) {
		case 0x1:
			return becker_read_status();
		case 0x2:
			return becker_read_data();
		default:
			break;
		}
	}
	return 0;
}

static void nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	if ((A & ~1) == 0xFFDE) {
		extmem_ty = A & 1;
	} else if ((A & ~1) == 0xFFBE) {
		extmem_map = A & 1;
		extmem_bank = D & (EXTBANKS - 1);
	} else if (A > 0x7fff && A < 0xff00 && !extmem_ty && extmem_map) {
		extmem[0x8000 * extmem_bank + (A & 0x7fff)] = D;
		c->EXTMEM = 1;
		return;
	}
	c->EXTMEM = 0;
	if (P2 && have_becker) {
		switch (A & 3) {
		case 0x2:
			becker_write_data(D);
			break;
		default:
			break;
		}
	}
}
