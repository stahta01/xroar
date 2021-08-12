/** \file
 *
 *  \brief "65SPI" SPI interface.
 *
 *  \copyright Copyright 2018 Tormod Volden
 *
 *  \copyright Copyright 2018-2021 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>

#include "delegate.h"

#include "logging.h"
#include "spi65.h"

#define SPIDATA 0
#define SPICTRL 1
#define SPISTATUS 1
#define SPICLK 2
#define SPISIE 3

#define SPICTRL_TC  0x80
#define SPICTRL_FRX 0x10

#define SPI_NDEVICES (4)

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

struct spi65 *spi65_new(void) {
	struct spi65_private *spi65p = part_new(sizeof(*spi65p));
	*spi65p = (struct spi65_private){0};
	part_init(&spi65p->public.part, "65SPI-B");
	return &spi65p->public;
}

void spi65_add_device(struct spi65 *spi65, struct spi65_device *device, unsigned slot) {
	struct spi65_private *spi65p = (struct spi65_private *)spi65;
	char id[] = { 's', 'l', 'o', 't', '0', 0 };
	if (slot >= SPI_NDEVICES)
		return;
	spi65_remove_device(spi65, slot);
	spi65p->device[slot] = device;
	id[4] = '0' + slot;
	part_add_component(&spi65->part, &device->part, id);
}

void spi65_remove_device(struct spi65 *spi65, unsigned slot) {
	struct spi65_private *spi65p = (struct spi65_private *)spi65;
	if (slot >= SPI_NDEVICES)
		return;
	if (spi65p->device[slot]) {
		part_remove_component(&spi65->part, &spi65p->device[slot]->part);
	}
}

uint8_t spi65_read(struct spi65 *spi65, uint8_t reg) {
	struct spi65_private *spi65p = (struct spi65_private *)spi65;
	uint8_t value = 0;

	switch (reg) {
	case SPIDATA:
		value = spi65p->reg_data_in;
		LOG_DEBUG(3, "Reading SPI DATA");
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
		LOG_DEBUG(3, "Reading SPI STATUS");
		value = spi65p->status;
		spi65p->status |= SPICTRL_TC; // complete next time
		break;
	case SPICLK:
		LOG_DEBUG(3, "Reading SPI CLK");
		value = spi65p->clkdiv;
		break;
	case SPISIE:
		LOG_DEBUG(3, "Reading SPI SIE");
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
		LOG_DEBUG(3, "Writing SPI DATA");
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
		LOG_DEBUG(3, "Writing SPI CONTROL");
		spi65p->status = (value & ~0xa0) | (spi65p->status & 0xa0);
		break;
	case SPICLK:
		LOG_DEBUG(3, "Writing SPI CLK");
		spi65p->clkdiv = value;
		break;
	case SPISIE:
		LOG_DEBUG(3, "Writing SPI SIE");
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
