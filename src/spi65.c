/** \file
 *
 *  \brief "65SPI" SPI interface.
 *
 *  \copyright Copyright 2018 Tormod Volden
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

// Sources:
//     65SPI/B
//         http://www.6502.org/users/andre/spi65b/

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>

#include "array.h"
#include "delegate.h"

#include "logging.h"
#include "serialise.h"
#include "spi65.h"
#include "part.h"

#define SPIDATA 0
#define SPICTRL 1
#define SPISTATUS 1
#define SPICLK 2
#define SPISIE 3

#define SPICTRL_TC  0x80
#define SPICTRL_FRX 0x10

#define SPI_NDEVICES (4)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct spi65_private {
	struct spi65 public;

	// 65SPI internal registers
	uint8_t reg_data_in;   // read by host
	uint8_t reg_data_out;  // written by host
	uint8_t status;
	uint8_t clkdiv;
	uint8_t ss_ie;

	// Attached devices
	struct spi65_device *device[SPI_NDEVICES];
};

static const struct ser_struct ser_struct_spi65[] = {
	SER_ID_STRUCT_ELEM(1, struct spi65_private, reg_data_in),
	SER_ID_STRUCT_ELEM(2, struct spi65_private, reg_data_out),
	SER_ID_STRUCT_ELEM(3, struct spi65_private, status),
	SER_ID_STRUCT_ELEM(4, struct spi65_private, clkdiv),
	SER_ID_STRUCT_ELEM(5, struct spi65_private, ss_ie),
};

static const struct ser_struct_data spi65_ser_struct_data = {
	.elems = ser_struct_spi65,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_spi65),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// 65SPI-B part creation

static struct part *spi65_allocate(void);
static _Bool spi65_finish(struct part *p);

static const struct partdb_entry_funcs spi65_funcs = {
	.allocate = spi65_allocate,
	.finish = spi65_finish,

	.ser_struct_data = &spi65_ser_struct_data,
};

const struct partdb_entry spi65_part = { .name = "65SPI-B", .description = "Rictor, Fachat | 65SPI-B interface", .funcs = &spi65_funcs };

static struct part *spi65_allocate(void) {
	struct spi65_private *spi65p = part_new(sizeof(*spi65p));
	struct part *p = &spi65p->public.part;

	*spi65p = (struct spi65_private){0};

	return p;
}

static _Bool spi65_finish(struct part *p) {
	struct spi65_private *spi65p = (struct spi65_private *)p;

	// Find attached devices
	char id[6];
	for (unsigned i = 0; i < SPI_NDEVICES; ++i) {
		snprintf(id, sizeof(id), "slot%u", i);
		struct spi65_device *device = (struct spi65_device *)part_component_by_id_is_a(p, id, "spi-device");
		if (device) {
			spi65p->device[i] = device;
		} else {
			spi65p->device[i] = NULL;
		}
	}

	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void spi65_add_device(struct spi65 *spi65, struct spi65_device *device, unsigned slot) {
	struct spi65_private *spi65p = (struct spi65_private *)spi65;
	char id[6];
	if (slot >= SPI_NDEVICES)
		return;
	spi65_remove_device(spi65, slot);
	snprintf(id, sizeof(id), "slot%u", slot);
	part_add_component(&spi65->part, &device->part, id);
	spi65_finish(&spi65p->public.part);
}

void spi65_remove_device(struct spi65 *spi65, unsigned slot) {
	struct spi65_private *spi65p = (struct spi65_private *)spi65;
	if (slot >= SPI_NDEVICES)
		return;
	if (spi65p->device[slot]) {
		part_free(&spi65p->device[slot]->part);
		// spi65_finish() called below will clear that slot
	}
	spi65_finish(&spi65p->public.part);
}

uint8_t spi65_read(struct spi65 *spi65, uint8_t reg) {
	struct spi65_private *spi65p = (struct spi65_private *)spi65;
	uint8_t value = 0;

	switch (reg) {
	case SPIDATA:
		value = spi65p->reg_data_in;
		LOG_MOD_DEBUG(3, "spi65", "Reading SPI DATA");
		spi65p->status &= ~SPICTRL_TC; /* clear TC on read */
		/* reading triggers SPI transfer in FRX mode */
		if (spi65p->status & SPICTRL_FRX) {
			for (int i = 0; i < 4; i++) {
				if (spi65p->device[i]) {
					if ((spi65p->ss_ie & (1 << i)) == 0) {
						spi65p->reg_data_in = DELEGATE_CALL(spi65p->device[i]->transfer, spi65p->reg_data_out, 1);
					} else {
						(void)DELEGATE_CALL(spi65p->device[i]->transfer, spi65p->reg_data_out, 0);
					}
				}
			}
		}
		break;
	case SPISTATUS:
		LOG_MOD_DEBUG(3, "spi65", "Reading SPI STATUS");
		value = spi65p->status;
		spi65p->status |= SPICTRL_TC; // complete next time
		break;
	case SPICLK:
		LOG_MOD_DEBUG(3, "spi65", "Reading SPI CLK");
		value = spi65p->clkdiv;
		break;
	case SPISIE:
		LOG_MOD_DEBUG(3, "spi65", "Reading SPI SIE");
		value = spi65p->ss_ie;
		break;
	default: /* only for compiler happiness */
		break;
	}
	LOG_DEBUG(3, "\t\t <- %02x\n", value);
	return value;
}

void spi65_write(struct spi65 *spi65, uint8_t reg, uint8_t value) {
	struct spi65_private *spi65p = (struct spi65_private *)spi65;
	switch (reg) {
	case SPIDATA:
		LOG_MOD_DEBUG(3, "spi65", "Writing SPI DATA");
		spi65p->reg_data_out = value;
		/* writing triggers SPI transfer */
		for (int i = 0; i < 4; i++) {
			if (spi65p->device[i]) {
				if ((spi65p->ss_ie & (1 << i)) == 0) {
					spi65p->reg_data_in = DELEGATE_CALL(spi65p->device[i]->transfer, spi65p->reg_data_out, 1);
				} else {
					(void)DELEGATE_CALL(spi65p->device[i]->transfer, spi65p->reg_data_out, 0);
				}
			}
		}
		spi65p->status &= ~SPICTRL_TC;
		break;
	case SPICTRL:
		LOG_MOD_DEBUG(3, "spi65", "Writing SPI CONTROL");
		spi65p->status = (value & ~0xa0) | (spi65p->status & 0xa0);
		break;
	case SPICLK:
		LOG_MOD_DEBUG(3, "spi65", "Writing SPI CLK");
		spi65p->clkdiv = value;
		break;
	case SPISIE:
		LOG_MOD_DEBUG(3, "spi65", "Writing SPI SIE");
		spi65p->ss_ie = value;
		break;
	default: /* only for compiler happiness */
		break;
	}
	LOG_DEBUG(3, "\t -> %02x\n", value);
}

void spi65_reset(struct spi65 *spi65) {
	struct spi65_private *spi65p = (struct spi65_private *)spi65;
	spi65p->reg_data_in = 0xff; /* TODO verify */
	spi65p->reg_data_out = 0;
	spi65p->status = 0;
	spi65p->status = 0;
	spi65p->clkdiv = 0;
	spi65p->ss_ie = 0x0f; /* slave selects high = inactive */

	for (int i = 0; i < SPI_NDEVICES; i++) {
		if (spi65p->device[i]) {
			DELEGATE_SAFE_CALL(spi65p->device[i]->reset);
		}
	}
}
