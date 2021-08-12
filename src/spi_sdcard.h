/** \file
 *
 *  \brief Minimal emulation of an SDHC card in SPI mode.
 *
 *  \copyright Copyright 2018 Tormod Volden
 *
 *  \copyright Copyright 2021 Ciaran Anscomb
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

#ifndef XROAR_SPI_SDCARD_H_
#define XROAR_SPI_SDCARD_H_

struct spi65_device;

struct spi65_device *spi_sdcard_new(const char *image);

#endif
