/** \file
 *
 *  \brief Emulation of MOOH memory & SPI board.
 *
 *  \copyright Copyright 2016-2018 Tormod Volden
 *
 *  \copyright Copyright 2018-2024 Ciaran Anscomb
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "becker.h"
#include "cart.h"
#include "logging.h"
#include "part.h"
#include "ram.h"
#include "rombank.h"
#include "serialise.h"
#include "spi65.h"
#include "xroar.h"

/* Number of 8KB mappable RAM pages in cartridge */
#define MEMPAGES 0x40
#define TASK_MASK 0x3F  /* 6 bit task registers */

struct mooh {
	struct cart cart;
	struct spi65 *spi65;
	struct ram *extmem;
	_Bool mmu_enable;
	_Bool crm_enable;
	uint8_t taskreg[8][2];
	uint8_t task;
	uint8_t rom_conf;
	struct becker *becker;
	_Bool crt9128_to_stderr;  // console output to stderr if set
	uint8_t crt9128_reg_addr;
	int msgr_client_id;  // messenger client id
};

#define MOOH_SER_EXTMEM  (2)
#define MOOH_SER_TASKREG (5)

static const struct ser_struct ser_struct_mooh[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
	SER_ID_STRUCT_UNHANDLED(MOOH_SER_EXTMEM),
	SER_ID_STRUCT_ELEM(3, struct mooh, mmu_enable),
	SER_ID_STRUCT_ELEM(4, struct mooh, crm_enable),
	SER_ID_STRUCT_UNHANDLED(MOOH_SER_TASKREG),
	SER_ID_STRUCT_ELEM(6, struct mooh, task),
	SER_ID_STRUCT_ELEM(7, struct mooh, rom_conf),
	SER_ID_STRUCT_ELEM(8, struct mooh, crt9128_to_stderr),
};

static _Bool mooh_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool mooh_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data mooh_ser_struct_data = {
	.elems = ser_struct_mooh,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mooh),
	.read_elem = mooh_read_elem,
	.write_elem = mooh_write_elem,
};

static struct xconfig_option const mooh_options[] = {
	{ XCO_SET_BOOL("mooh-crt9128-stderr", struct mooh, crt9128_to_stderr) },
	{ XC_OPT_END() }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mooh_config_complete(struct cart_config *);
static void mooh_ui_set_hd_filename(void *, int tag, void *smsg);

static void mooh_reset(struct cart *c, _Bool hard);
static uint8_t mooh_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t mooh_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void mooh_detach(struct cart *c);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MOOH part creation

static struct part *mooh_allocate(void);
static void mooh_initialise(struct part *p, void *options);
static _Bool mooh_finish(struct part *p);
static void mooh_free(struct part *p);

static const struct partdb_entry_funcs mooh_funcs = {
	.allocate = mooh_allocate,
	.initialise = mooh_initialise,
	.finish = mooh_finish,
	.free = mooh_free,

	.ser_struct_data = &mooh_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct cart_partdb_entry mooh_part = { .partdb_entry = { .name = "mooh", .description = "Tormod Volden | MOOH memory cartridge", .funcs = &mooh_funcs }, .config_complete = mooh_config_complete };

static struct part *mooh_allocate(void) {
	struct mooh *n = part_new(sizeof(*n));
	struct cart *c = &n->cart;
	struct part *p = &c->part;

	*n = (struct mooh){0};

	cart_rom_init(c);

	c->read = mooh_read;
	c->write = mooh_write;
	c->reset = mooh_reset;
	c->detach = mooh_detach;

	return p;
}

static struct ram *mooh_create_ram(void) {
	struct ram_config ram_config = {
		.d_width = 8,
		.organisation = RAM_ORG(13, 13, 0),
	};
	struct ram *ram = (struct ram *)part_create("ram", &ram_config);
	for (unsigned i = 0; i < MEMPAGES; ++i) {
		ram_add_bank(ram, i);
	}
	return ram;
}

static void mooh_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	struct mooh *n = (struct mooh *)p;
	struct cart *c = &n->cart;

	cart_rom_initialise(p, options);

	// MOOH-specific options
	xconfig_parse_list_struct(mooh_options, cc->opts, n);

	// RAM
	struct ram *ram = mooh_create_ram();
	part_add_component(p, (struct part *)ram, "EXTMEM");

	// 65SPI/B for interfacing to SD card
	struct spi65 *spi65 = (struct spi65 *)part_create("65SPI-B", NULL);
	part_add_component(&c->part, (struct part *)spi65, "SPI65");
}

static _Bool mooh_finish(struct part *p) {
	struct mooh *n = (struct mooh *)p;
	struct cart *c = &n->cart;

	// Find attached parts
	n->spi65 = (struct spi65 *)part_component_by_id_is_a(p, "SPI65", "65SPI-B");
	n->extmem = (struct ram *)part_component_by_id_is_a(p, "EXTMEM", "ram");

	// Check all required parts are attached
	if (!n->spi65 || !n->extmem) {
		return 0;
	}

	if (!cart_rom_finish(p)) {
		return 0;
	}

	// Join the ui messenger groups we're interested in
	n->msgr_client_id = messenger_client_register();
	ui_messenger_join_group(n->msgr_client_id, ui_tag_hd_filename, MESSENGER_NOTIFY_DELEGATE(mooh_ui_set_hd_filename, n));

	// RAM
	ram_report(n->extmem, "mooh", "extended RAM");

	if (c->config->becker_port) {
		n->becker = becker_open();
	}

	return 1;
}

static void mooh_free(struct part *p) {
	struct mooh *n = (struct mooh *)p;
	becker_close(n->becker);
	messenger_client_unregister(n->msgr_client_id);
	cart_rom_free(p);
}

static _Bool mooh_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct mooh *n = sptr;
	struct part *p = &n->cart.part;
	size_t length = ser_data_length(sh);
	switch (tag) {
	case MOOH_SER_EXTMEM:
		if (length != (MEMPAGES * 0x2000)) {
			LOG_MOD_WARN("mooh", "deserialise: RAM size mismatch\n");
			return 0;
		}
		{
			part_free(part_component_by_id_is_a(p, "EXTMEM", "ram"));
			struct ram *ram = mooh_create_ram();
			part_add_component(p, (struct part *)ram, "EXTMEM");
			ram_ser_read(ram, sh);
		}
		break;
	case MOOH_SER_TASKREG:
		ser_read(sh, n->taskreg, sizeof(n->taskreg));
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool mooh_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct mooh *n = sptr;
	switch (tag) {
	case MOOH_SER_EXTMEM:
		// no-op: RAM is now a sub-component
		break;
	case MOOH_SER_TASKREG:
		ser_write(sh, tag, n->taskreg, sizeof(n->taskreg));
		break;
	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void mooh_config_complete(struct cart_config *cc) {
	// Default ROM
	if (!cc->rom_dfn && !cc->rom) {
		cc->rom = xstrdup("sdbdos");
	}
}

static void mooh_ui_set_hd_filename(void *sptr, int tag, void *smsg) {
        struct mooh *n = sptr;
        struct ui_state_message *uimsg = smsg;
        assert(tag == ui_tag_hd_filename);

	int drive = uimsg->value;

	if (drive != 0) {
		return;
	}

	spi65_remove_device(n->spi65, 0);

	if (uimsg->data) {
		// XXX can i make the options passed to part_create const?
		char *filename = xstrdup((const char *)uimsg->data);
		struct spi65_device *sdcard = (struct spi65_device *)part_create("SPI-SDCARD", filename);
		spi65_add_device(n->spi65, sdcard, 0);
		free(filename);
	}
}

static void mooh_reset(struct cart *c, _Bool hard) {
	struct mooh *n = (struct mooh *)c;
	int i;

	cart_rom_reset(c, hard);

	if (hard) {
		// SRAM, so assume a random pattern on startup
		ram_clear(n->extmem, ram_init_random);
	}

	n->mmu_enable = 0;
	n->crm_enable = 0;
	n->task = 0;
	for (i = 0; i < 8; i++)
		n->taskreg[i][0] = n->taskreg[i][1] = 0xFF & TASK_MASK;

	n->rom_conf = 0;
	if (n->becker)
		becker_reset(n->becker);
	n->crt9128_reg_addr = 0;

	spi65_reset(n->spi65);
}

static void mooh_detach(struct cart *c) {
	struct mooh *n = (struct mooh *)c;
	if (n->becker)
		becker_reset(n->becker);
	cart_rom_detach(c);
}

static uint8_t mooh_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct mooh *n = (struct mooh *)c;

	c->EXTMEM = 0;

        if (R2) {
		if (n->rom_conf & 8)
			rombank_d8(c->ROM, ((n->rom_conf & 6) << 13) | (A & 0x3fff), &D);
		else
			rombank_d8(c->ROM, ((n->rom_conf & 7) << 13) | (A & 0x1fff), &D);
		return D;
	}

	if ((A & 0xFFFC) == 0xFF6C)
		return spi65_read(n->spi65, A & 3);

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
		int segment = A >> 13;
		int offset = A & 0x1FFF;
		int bank;
		int crm;
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

		if (bank != 0x3F || crm || (A & 0xE000) == 0xE000 ) {
			c->EXTMEM = 1;
			ram_d8(n->extmem, 1, bank, offset, 0, &D);
			return D;
		}
	}
	if (P2 && n->becker) {
		switch (A & 3) {
		case 0x1:
			return becker_read_status(n->becker);
		case 0x2:
			return becker_read_data(n->becker);
		default:
			break;
		}
	}
	return D;
}

static uint8_t mooh_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct mooh *n = (struct mooh *)c;

	c->EXTMEM = 0;

        if (R2) {
		if (n->rom_conf & 8)
			rombank_d8(c->ROM, ((n->rom_conf & 6) << 13) | (A & 0x3fff), &D);
		else
			rombank_d8(c->ROM, ((n->rom_conf & 7) << 13) | (A & 0x1fff), &D);
		return D;
	}

	if (A == 0xFF64 && (n->rom_conf & 16) == 0)
		n->rom_conf = D & 31;

	if ((A & 0xFFFC) == 0xFF6C)
		spi65_write(n->spi65, A & 3, D);

	/* poor man's CRT9128 Wordpak emulation */
	if (A == 0xFF7D) {
		n->crt9128_reg_addr = D;
	}
#ifndef HAVE_WASM
	if (n->crt9128_to_stderr && A == 0xFF7C && n->crt9128_reg_addr == 0x0d) {
		fprintf(stderr, "%c", D);
	}
#endif

	if ((A & 0xFFF0) == 0xFFA0) {
		n->taskreg[A & 7][(A & 8) >> 3] = D & TASK_MASK;
	} else if (A == 0xFF90) {
		n->crm_enable = (D & 8) >> 3;
		n->mmu_enable = (D & 64) >> 6;
	} else if (A == 0xFF91) {
		n->task = D & 1;
	} else if (n->mmu_enable && (A < 0xFF00 || (A >= 0xFFF0 && n->crm_enable))) {
		int segment = A >> 13;
		int offset = A & 0x1FFF;
		int bank;
		int crm;
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

		if (bank != 0x3F || crm || (A & 0xE000) == 0xE000) {
			ram_d8(n->extmem, 0, bank, offset, 0, &D);
			c->EXTMEM = 1;
		}
	}
	if (P2 && n->becker) {
		switch (A & 3) {
		case 0x2:
			becker_write_data(n->becker, D);
			break;
		default:
			break;
		}
	}

	return D;
}
