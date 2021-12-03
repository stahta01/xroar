/** \file
 *
 *  \brief "65SPI" SPI interface
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

#ifndef XROAR_SPI65_H_
#define XROAR_SPI65_H_

#include <stdint.h>

#include "delegate.h"

#include "part.h"

struct spi65 {
	struct part part;
};

struct spi65_device {
	struct part part;
	DELEGATE_T2(uint8, uint8, bool) transfer;
	DELEGATE_T0(void) reset;
};

void spi65_add_device(struct spi65 *spi65, struct spi65_device *device, unsigned slot);
void spi65_remove_device(struct spi65 *spi65, unsigned slot);

uint8_t spi65_read(struct spi65 *spi65, uint8_t reg);
void spi65_write(struct spi65 *spi65, uint8_t reg, uint8_t value);
void spi65_reset(struct spi65 *spi65);

#endif
