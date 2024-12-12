/** \file
 *
 *  \brief RAM.
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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "xalloc.h"

#include "logging.h"
#include "part.h"
#include "ram.h"
#include "serialise.h"

static void recalculate_bank_size(struct ram *ram);

#define RAM_SER_NBANKS (2)
#define RAM_SER_D (7)

static struct ser_struct ser_struct_ram[] = {
	SER_ID_STRUCT_ELEM(1, struct ram, d_width),
	SER_ID_STRUCT_UNHANDLED(RAM_SER_NBANKS),
	SER_ID_STRUCT_ELEM(3, struct ram, organisation),
	// Bank data must come after all the bank size setup above
	SER_ID_STRUCT_UNHANDLED(RAM_SER_D),
};

#define RAM_SER_D_DATA (1)

static _Bool ram_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool ram_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data ram_ser_struct_data = {
	.elems = ser_struct_ram,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_ram),
	.read_elem = ram_read_elem,
	.write_elem = ram_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// RAM part creation

static struct part *ram_allocate(void);
static void ram_initialise(struct part *p, void *options);
static _Bool ram_finish(struct part *p);
static void ram_free(struct part *p);

static const struct partdb_entry_funcs ram_funcs = {
	.allocate = ram_allocate,
	.initialise = ram_initialise,
	.finish = ram_finish,
	.free = ram_free,

	.ser_struct_data = &ram_ser_struct_data,
};

const struct partdb_entry ram_part = { .name = "ram", .description = "RAM", .funcs = &ram_funcs };

static struct part *ram_allocate(void) {
	struct ram *ram = part_new(sizeof(*ram));
	struct part *p = &ram->part;
	*ram = (struct ram){0};
	return p;
}

static void ram_initialise(struct part *p, void *options) {
	struct ram *ram = (struct ram *)p;
	const struct ram_config *config = options;
	assert(ram != NULL);
	assert(config != NULL);
	ram->d_width = config->d_width;
	ram->organisation = config->organisation;
}

static _Bool ram_finish(struct part *p) {
	struct ram *ram = (struct ram *)p;
	size_t old_nelems = ram->bank_nelems;
	recalculate_bank_size(ram);
	if (old_nelems > 0 && old_nelems != ram->bank_nelems) {
		return 0;
	}
	return 1;
}

static void ram_free(struct part *p) {
	struct ram *ram = (struct ram *)p;

	if (ram->d) {
		for (unsigned i = 0; i < ram->nbanks; i++) {
			if (ram->d[i])
				free(ram->d[i]);
		}
		free(ram->d);
	}
}

static void deserialise_bank(struct ser_handle *sh, struct ram *ram, unsigned bank) {
	int tag;
        while (!ser_error(sh) && (tag = ser_read_tag(sh)) > 0) {
		switch (tag) {
		case RAM_SER_D_DATA:
			{
				if (ram->d[bank]) {
					ser_set_error(sh, ser_error_format);
					return;
				}
				size_t nelems = 0;
				switch (ram->d_width) {
				case 8: default:
					nelems = ser_read_array_uint8(sh, (uint8_t **)&ram->d[bank], 0);
					break;
				case 16:
					nelems = ser_read_array_uint16(sh, (uint16_t **)&ram->d[bank], 0);
					break;
				}
				if (ram->bank_nelems > 0 && nelems != ram->bank_nelems) {
					ser_set_error(sh, ser_error_format);
					return;
				}
				ram->bank_nelems = nelems;
			}
			break;
		}
	}
}

static _Bool ram_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct ram *ram = sptr;
	switch (tag) {
	case RAM_SER_NBANKS:
		{
			unsigned nbanks = ser_read_vuint32(sh);
			// XXX arbitrary hard limit of 64 banks here
			if (nbanks > 64 || ram->d)
				ser_set_error(sh, ser_error_format);
			if (ser_error(sh))
				return 0;
			ram->d = xmalloc(nbanks * sizeof(*ram->d));
			for (unsigned i = 0; i < nbanks; i++) {
				ram->d[i] = NULL;
			}
			ram->nbanks = nbanks;
		}
		break;

	case RAM_SER_D:
		{
			unsigned bank = ser_read_vuint32(sh);
			if ((ram->d_width != 8 && ram->d_width != 16)
			    || bank >= ram->nbanks || !ram->d) {
				ser_set_error(sh, ser_error_format);
				return 0;
			}
			deserialise_bank(sh, ram, bank);
		}
		break;

	default:
		return 0;
	}
	return 1;
}

static _Bool ram_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct ram *ram = sptr;
	switch (tag) {
	case RAM_SER_NBANKS:
		ser_write_vuint32(sh, tag, ram->nbanks);
		break;

	case RAM_SER_D:
		for (unsigned i = 0; i < ram->nbanks; i++) {
			if (ram->d[i]) {
				switch (ram->d_width) {
				case 8: default:
					ser_write_open_vuint32(sh, RAM_SER_D, i);
					ser_write_array_uint8(sh, RAM_SER_D_DATA, (uint8_t *)ram->d[i], ram->bank_nelems);
					ser_write_close_tag(sh);
					break;

				case 16:
					ser_write_open_vuint32(sh, RAM_SER_D, i);
					ser_write_array_uint16(sh, RAM_SER_D_DATA, (uint16_t *)ram->d[i], ram->bank_nelems);
					ser_write_close_tag(sh);
					break;
				}
			}
		}
		break;

	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Calculate bank size in elements (not bytes).

static void recalculate_bank_size(struct ram *ram) {
	// Note: organisation encoding allows up to 64 address bits, but no
	// more than 24 bits (16M elements) are accepted for now.
	unsigned addr_bits = RAM_ORG_A(ram->organisation);
	if (addr_bits > 24) {
		ram->bank_nelems = 0;
		return;
	}

	unsigned row_bits = RAM_ORG_R(ram->organisation);
	unsigned col_bits = addr_bits - row_bits;

	unsigned col_shift = RAM_ORG_CS(ram->organisation);
	if (col_shift > row_bits) {
		ram->bank_nelems = 0;
		return;
	}
	ram->row_mask = (1 << row_bits) - 1;
	ram->col_mask = col_bits ? (((1 << col_bits) - 1) << col_shift) : 0;
	ram->col_shift = row_bits - col_shift;

	ram->bank_nelems = (1 << addr_bits);
}

static size_t ram_bank_nbytes(const struct ram *ram) {
	if (!ram) {
		return 0;
	}
	size_t nbytes = ram->bank_nelems;
	if (ram->d_width == 16) {
		nbytes *= 2;
	}
	return nbytes;
}

void ram_add_bank(struct ram *ram, unsigned bank) {
	//recalculate_bank_size(ram);
	assert(ram != NULL);
	assert(ram->bank_nelems != 0);
	if (bank >= ram->nbanks) {
		ram->d = xrealloc(ram->d, (bank + 1) * sizeof(*ram->d));
		while (ram->nbanks <= bank) {
			ram->d[ram->nbanks++] = NULL;
		}
	}
	size_t nbytes = ram_bank_nbytes(ram);
	if (!ram->d[bank] && nbytes > 0) {
		ram->d[bank] = xmalloc(nbytes);
	}
}

void ram_clear(struct ram *ram, int method) {
	size_t nbytes = ram_bank_nbytes(ram);
	if (nbytes == 0)
		return;

	unsigned val = 0x00;
	unsigned tst = 0xff;
	if (method == ram_init_clear) {
		val = 0;
		tst = 0;
	}
	if (method == ram_init_set) {
		tst = 0;
	}

	for (unsigned bank = 0; bank < ram->nbanks; bank++) {
		uint8_t *dst = (uint8_t *)ram->d[bank];
		if (!dst)
			continue;
		if (method == ram_init_random) {
			for (size_t loc = 0; loc < nbytes; loc++) {
				dst[loc] = rand();
			}
		} else {
			for (size_t loc = 0; loc < nbytes; loc += 4) {
				dst[loc] = val;
				dst[loc+1] = val;
				dst[loc+2] = val;
				dst[loc+3] = val;
				if ((loc & tst) != 0)
					val ^= 0xff;
			}
		}
	}
}

// Read data from serialisation handle into RAM bank only if that bank is
// present.

void ram_ser_read_bank(struct ram *ram, struct ser_handle *sh, unsigned bank) {
	if (!ram || ram->bank_nelems == 0 || !ram->d || !ram->d[bank])
		return;

	size_t s_nelems = ser_data_length(sh);
	if (ram->d_width == 16)
		s_nelems /= 2;

	if (s_nelems == 0)
		return;
	if (s_nelems > ram->bank_nelems)
		s_nelems = ram->bank_nelems;

	switch (ram->d_width) {
	case 8: default:
		{
			uint8_t *dst = ram->d[bank];
			(void)ser_read_array_uint8(sh, &dst, s_nelems);
		}
		break;

	case 16:
		{
			uint16_t *dst = ram->d[bank];
			(void)ser_read_array_uint16(sh, &dst, s_nelems);
		}
		break;
	}
}

// Read data from serialisation handle into each present RAM bank in turn.

void ram_ser_read(struct ram *ram, struct ser_handle *sh) {
	if (!ram)
		return;

	for (unsigned bank = 0; bank < ram->nbanks; bank++) {
		ram_ser_read_bank(ram, sh, bank);
	}
}

extern inline uint8_t *ram_a8(struct ram *ram, unsigned bank, unsigned row, unsigned col);
extern inline uint16_t *ram_a16(struct ram *ram, unsigned bank, unsigned row, unsigned col);
extern inline void ram_d8(struct ram *ram, _Bool RnW, unsigned bank,
			  unsigned row, unsigned col, uint8_t *d);
extern inline void ram_d16(struct ram *ram, _Bool RnW, unsigned bank,
			   unsigned row, unsigned col, uint16_t *d);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

unsigned ram_report(struct ram *ram, const char *par, const char *name) {
	unsigned nbanks = ram ? ram->nbanks : 0;
	unsigned bank_k = ram ? ram->bank_nelems / 1024 : 0;
	unsigned k = nbanks * bank_k;
	LOG_PAR_MOD_DEBUG(1, par, "ram", "%u bank%s * %uK = %uK %s\n", nbanks,
			  (nbanks == 1) ? "" : "s", bank_k, k, name);
	return k;
}
