/*

"65SPI" SPI interface

Copyright 2018 Tormod Volden
Copyright 2018 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cart.h"

uint8_t spi_sdcard_transfer(uint8_t data_out, int ss_active);
void spi_sdcard_reset();

#define SPIDATA 0
#define SPICTRL 1
#define SPISTATUS 1
#define SPICLK 2
#define SPISIE 3

#define SPICTRL_TC  0x80
#define SPICTRL_FRX 0x10

/* 65SPI internal registers */
// struct me
static uint8_t reg_data_in; /* read by host */
static uint8_t reg_data_out; /* written by host */
static uint8_t status;
static uint8_t clkdiv;
static uint8_t ss_ie;

uint8_t spi65_read(uint8_t reg)
{
	uint8_t value = 0;

	switch (reg) {
	case SPIDATA:
		value = reg_data_in;
		// fprintf(stderr, "Reading SPI DATA");
		status &= ~SPICTRL_TC; /* clear TC on read */
		/* reading triggers SPI transfer in FRX mode */
		if (status & SPICTRL_FRX) {
			reg_data_in = spi_sdcard_transfer(reg_data_out, (ss_ie & 0x01) == 0);
		}
		break;
	case SPISTATUS:
		// fprintf(stderr, "Reading SPI STATUS");
		value = status;
		status |= SPICTRL_TC; // complete next time
		break;
	case SPICLK:
		// fprintf(stderr, "Reading SPI CLK");
		value = clkdiv;
		break;
	case SPISIE:
		// fprintf(stderr, "Reading SPI SIE");
		value = ss_ie;
		break;
	default: /* only for compiler happiness */
		break;
	}
	// fprintf(stderr, "\t\t <- %02x\n", value);
	return value;
}

void spi65_write(uint8_t reg, uint8_t value)
{
	switch (reg) {
	case SPIDATA:
		// fprintf(stderr, "Writing SPI DATA");
		reg_data_out = value;
		/* writing triggers SPI transfer */
		reg_data_in = spi_sdcard_transfer(reg_data_out, (ss_ie & 0x01) == 0);
		status &= ~SPICTRL_TC;
		break;
	case SPICTRL:
		// fprintf(stderr, "Writing SPI CONTROL");
		status = (value & ~0xA0) | (status & 0xA0);
		break;
	case SPICLK:
		// fprintf(stderr, "Writing SPI CLK");
		clkdiv = value;
		break;
	case SPISIE:
		// fprintf(stderr, "Writing SPI SIE");
		ss_ie = value;
		break;
	default: /* only for compiler happiness */
		break;
	}
	// fprintf(stderr, "\t -> %02x\n", value);
}

void spi65_reset(void)
{
	reg_data_in = 0xFF; /* TODO verify */
	reg_data_out = 0;
	status = 0;
	status = 0;
	clkdiv = 0;
	ss_ie = 0x0F; /* slave selects high = inactive */

	spi_sdcard_reset();
}
