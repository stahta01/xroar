/** \file
 *
 *  \brief iMMUnity support.
 *
 *  \copyright Copyright 2026 Jim Brain
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

#include <assert.h>

#include "array.h"

#include "dragon/dragon.h"
#include "dragon/immunity.h"
#include "logging.h"
#include "mc6809/mc6809.h"
#include "mc6821.h"
#include "mc6883.h"
#include "part.h"
#include "ram.h"
#include "serialise.h"
#include "xroar.h"

//#define JIM_DEBUG 1

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define IMMUNITY_SER_DAT (1)

static const struct ser_struct ser_struct_immunity[] = {
	SER_ID_STRUCT_UNHANDLED(IMMUNITY_SER_DAT),
	SER_ID_STRUCT_ELEM(2, struct immunity, init0),
	SER_ID_STRUCT_ELEM(3, struct immunity, init1),
};

static bool immunity_read_elem(void *sptr, struct ser_handle *sh, int tag);
static bool immunity_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data immunity_ser_struct_data = {
        .elems = ser_struct_immunity,
        .num_elems = ARRAY_N_ELEMENTS(ser_struct_immunity),
	.read_elem = immunity_read_elem,
	.write_elem = immunity_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct part *immunity_allocate(void);
static void immunity_initialise(struct part *p, void *options);
static bool immunity_finish(struct part *p);

static const struct partdb_entry_funcs immunity_funcs = {
	.allocate = immunity_allocate,
	.initialise = immunity_initialise,
	.finish = immunity_finish,

	.ser_struct_data = &immunity_ser_struct_data,
};

const struct partdb_entry immunity_part = { .name = "immunity", .description = "Jim Brain | iMMUnity", .funcs = &immunity_funcs };

static const struct debug_feature immunity_feature;
static const struct debug_feature *features[] = { &immunity_feature };
static uint32_t immunity_get_register(void *sptr, int n);
static void immunity_set_register(void *sptr, int n, uint32_t v);
#ifdef WANT_GDB_TARGET
static int immunity_get_register_composite(void *sptr, int n, unsigned dsize, uint8_t *dest);
static int immunity_set_register_composite(void *sptr, int n,
					   unsigned ssize, const uint8_t *src);
#endif

static struct part *immunity_allocate(void) {
	struct immunity *cj = part_new(sizeof(*cj));
	struct part *p = &cj->part;
	*cj = (struct immunity){0};

	cj->init0 = 0x80;

	cj->debug.part.nfeatures = ARRAY_N_ELEMENTS(features);
	cj->debug.part.feature = features;

	cj->debug.part.get_register = DELEGATE_AS1(uint32, int, immunity_get_register, cj);
	cj->debug.part.set_register = DELEGATE_AS2(void, int, uint32, immunity_set_register, cj);
#ifdef WANT_GDB_TARGET
	cj->debug.part.get_register_composite = DELEGATE_AS3(int, int, unsigned, uint8p, immunity_get_register_composite, cj);
	cj->debug.part.set_register_composite = DELEGATE_AS3(int, int, unsigned, cuint8p, immunity_set_register_composite, cj);
#endif

	return p;
}

static void immunity_initialise(struct part *p, void *options) {
	assert(p != NULL);
	(void)options;

	struct ram_config ram_config = {
		.d_width = 8,
		.organisation = RAM_ORG(21, 21, 0),
	};
	struct ram *mem = (struct ram *)part_create("ram", &ram_config);
	ram_add_bank(mem, 0);
	part_add_component(p, (struct part *)mem, "EXTMEM");
}

static bool immunity_finish(struct part *p) {
	struct immunity *cj = (struct immunity *)p;
	cj->mem = (struct ram *)part_component_by_id_is_a(p, "EXTMEM", "ram");
	ram_report(cj->mem, "immunity", "extended RAM");
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static bool immunity_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct immunity *cj = sptr;
	switch (tag) {
	case IMMUNITY_SER_DAT:
		for (int j = 0; j < 2; ++j) {
			for (int i = 0; i < 8; ++i) {
				cj->dat[j][i] = ser_read_uint8(sh);
			}
		}
		break;

	default:
		return 0;
	}
	return 1;
}

static bool immunity_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct immunity *cj = sptr;
	switch (tag) {
	case IMMUNITY_SER_DAT:
		ser_write_tag(sh, tag, 16);
		for (int j = 0; j < 2; ++j) {
			for (int i = 0; i < 8; ++i) {
				ser_write_uint8_untagged(sh, cj->dat[j][i]);
			}
		}
		ser_write_close_tag(sh);
		break;

	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void immunity_reset(struct immunity *cj, bool hard) {
	if (hard) {
		ram_clear(cj->mem, ram_init_random);
	}
	cj->init0 = 0x80;
}

void immunity_cpu_cycle(void *sptr, bool RnW, uint16_t A) {
	struct immunity *cj = sptr;
	struct dragon *md = cj->dragon;
	uint8_t data = md->CPU->D;
	bool ignore_sam = 0;
	uint32_t addr = (uint32_t)A;

	// Check watchpoints based on untransformed address
	bp_check_watchpoints(&md->watchpoint_set, RnW, A);

	if (A >= 0xffa0 && A <= 0xffaf) { // MMU registers
		if (RnW)
			data = cj->dat[(A & 0x08) >> 3][A & 0x07];
		else
			cj->dat[(A & 0x08) >> 3][A & 0x07] = data;
		ignore_sam = 1;
	} else if (A == 0xff90) {         // init0
		if (RnW)
			data = cj->init0;
		else
			cj->init0 = data;
		ignore_sam = 1;
	} else if (A == 0xff91) {         // init1
		if (RnW)
			data = cj->init1;
		else
			cj->init1 = data;
		ignore_sam = 1;
	} else if (cj->init0 & 0x40) { // MMU enabled
#ifdef JIM_DEBUG
    // print out current task mapping
		if (A >= 0xfff2 && A < 0xffff) {
			for (uint8_t i=0; i<8; i++) {
				printf("%2.2x, ",cj->dat[cj->init1 & 1][i]);
			}
			printf("\n");
		}
#endif
		uint8_t bank;
#ifdef JIM_DEBUG
		uint8_t b2;
#endif
		bool ffxx = (A & 0xff00) == 0xff00;
		bool altvec = ((A & 0xffe0) == 0xffe0) && !(cj->init0 & 0x80);
		bool io = ffxx && !altvec;
		if ((((A & 0xff00) == 0xfe00) && ((cj->init0 & 0x08) == 0x08))
		    || ffxx) { // pin CRM page and io, including vectors
			bank = 0x3f;
		} else {
			bank = cj->dat[cj->init1 & 1][A >> 13];
		}
#ifdef JIM_DEBUG
		b2 = bank;
#endif
		// invert bits 3,4,5 so $38 -> 00, $3f->07
		bank = (bank & 0xc7)
			| (bank & 0x20 ? 0 : 0x20)
			| (bank & 0x10 ? 0 : 0x10)
			| (bank & 0x08 ? 0 : 0x08);
		bool cocoram = (bank < 8);
		bool int_mem = (!cocoram
				 || (cocoram
				     && ((bank & 0x07) == 0x07)
				     && ((A & 0x1e00) == 0x1e00)
				     && !io
				    )
				);
    // compose full 21 bit address
    addr = (uint32_t)bank << 13 | (A & 0x1fff);
#ifdef JIM_DEBUG
		if ((A >= 0xfeee) && (A <= 0xfeff) || (A >= 0xfff2) && (A <= 0xffff))
			printf("spc ram=%d,ffxx=%d,altvec=%d,io=%d,bank=%2.2X,b2=%2.2X\n",cocoram, ffxx, altvec, io, bank, b2);
#endif
		if (int_mem) {
#ifdef JIM_DEBUG
			if(A != 0xffff) {
				uint8_t tmp_D = 0;
				ram_d8(cj->mem, 1, 0, addr, 0, &tmp_D);
				printf("int ram %4.4X, RAM=%d,ffxx=%d,altvec=%d,io=%d,bank=%2.2X,b2=%2.2X,addy=%4.4X,D=%2.2X\n", A, cocoram, ffxx, altvec, io, bank, b2, addr, tmp_D);
			}
#endif
			ram_d8(cj->mem, RnW, 0, addr, 0, &data);
			ignore_sam = 1;
		} else {
#ifdef JIM_DEBUG
			//printf("ext mem %4.4X, RAM=%d,ffxx=%d,altvec=%d,io=%d,bank=%d,b2=%d\n",A,cocoram,ffxx,altvec,io, bank, b2);
#endif
		}
	}

	// SAM decode / timing
	int ncycles;
	if (ignore_sam) {
		// switch SAM to a read of ffff
		ncycles = md->SAM->mem_cycle(md->SAM, 1 , 0xffff);
	} else {
		// use SAM functionality
		ncycles = md->SAM->mem_cycle(md->SAM, RnW, addr);
	}

	// Advance clock, collect IRQs
	if (!md->clock_inhibit) {
		dragon_advance_clock(md, ncycles);
		MC6809_IRQ_SET(md->CPU, md->PIA0->a.irq || md->PIA0->b.irq);
		MC6809_FIRQ_SET(md->CPU, md->PIA1->a.irq || md->PIA1->b.irq);
	}

	// Common cycle handling
	dragon_cpu_cycle(md, RnW, A, md->SAM->Zrow, md->SAM->Zcol);

	// Ensure iMMUnity RAM is used for reads where appropriate
	if (ignore_sam && RnW) {
		md->CPU->D = data;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Combine INIT0 and INIT1 into a 16-bit register
static const struct debug_feature_field feature_type_init_fields[] = {
	{ .name = "PIN", .start = 11, .end = 11, .type = &debug_feature_type_uint8 },
	{ .name = "MMUEN", .start = 14, .end = 14, .type = &debug_feature_type_uint8 },
	{ .name = "ALTVEC", .start = 15, .end = 15, .type = &debug_feature_type_uint8 },

	{ .name = "TR", .start = 0, .end = 0, .type = &debug_feature_type_uint8 },
};

static const struct debug_feature_type feature_type_init = {
	.type = debug_feature_base_type_struct,
	.id = "init",
	.size = 2,
	.as_struct = {
		.nfields = ARRAY_N_ELEMENTS(feature_type_init_fields),
		.field = feature_type_init_fields
	}
};

static const struct debug_feature_type feature_type_page_bank = {
	.type = debug_feature_base_type_vector,
	.id = "page_bank",
	.size = 8,
	.as_vector = {
		.nelems = 8,
		.type = &debug_feature_type_uint8
	}
};

static const struct debug_feature_type feature_type_page = {
	.type = debug_feature_base_type_vector,
	.id = "page",
	.size = 16,
	.as_vector = {
		.nelems = 2,
		.type = &feature_type_page_bank
	}
};

static const struct debug_feature_type *immunity_feature_types[] = {
	&feature_type_init,
	&feature_type_page_bank,
	&feature_type_page,
};

static const struct debug_feature_reg immunity_feature_regs[] = {
	{ .name = "init", .bitsize = 16, .type = &feature_type_init, .group = "immunity" },
	{ .name = "page", .bitsize = 128, .type = &feature_type_page, .group = "immunity" },
};

static const struct debug_feature immunity_feature = {
	"uk.org.6809.gdb.immunity",
	.ntypes = ARRAY_N_ELEMENTS(immunity_feature_types), .type = immunity_feature_types,
	.nregs = ARRAY_N_ELEMENTS(immunity_feature_regs), .reg = immunity_feature_regs
};

static uint32_t immunity_get_register(void *sptr, int n) {
	struct immunity *cj = sptr;

	uint32_t v = (uint32_t)-1;

	switch (n) {
	default:
		LOG_MOD_ERROR("immunity", "invalid simple register read: %d\n", n);
		break;

	case 0:
		// INIT (0+1 combined)
		v = (cj->init0 << 8) | cj->init1;
		break;
	}

	return v;
}

static void immunity_set_register(void *sptr, int n, uint32_t v) {
	struct immunity *cj = sptr;

	switch (n) {
	default:
		LOG_MOD_ERROR("immunity", "invalid simple register write: %d\n", n);
		exit(EXIT_FAILURE);

	case 0:
		// INIT (0+1 combined)
		cj->init0 = (v >> 8) & 0xff;
		cj->init1 = v & 0xff;
		break;

	}
}

#ifdef WANT_GDB_TARGET

static int immunity_get_register_composite(void *sptr, int n, unsigned dsize, uint8_t *dest) {
	struct immunity *cj = sptr;
	// error out for any register where get_register() should have been called
	switch (n) {
	default:
		LOG_MOD_ERROR("immunity", "invalid composite register read: %d\n", n);
		exit(EXIT_FAILURE);
	case 1:
		// PAGE
		assert(dsize >= 16);
		for (int i = 0; i < 2; ++i) {
			for (int j = 0; j < 8; ++j) {
				dest[i*8+j] = cj->dat[i][j];
			}
		}
		return 16;
	}
}

static int immunity_set_register_composite(void *sptr, int n,
					   unsigned ssize, const uint8_t *src) {
	struct immunity *cj = sptr;
	// error out for any register where set_register() should have been called
	switch (n) {
	default:
		LOG_MOD_ERROR("immunity", "invalid composite register write: %d\n", n);
		exit(EXIT_FAILURE);
	case 1:
		// PAGE
		assert(ssize >= 16);
		for (int i = 0; i < 2; ++i) {
			for (int j = 0; j < 8; ++j) {
				cj->dat[i][j] = src[i*8+j];
			}
		}
		return 16;
	}
}

#endif
