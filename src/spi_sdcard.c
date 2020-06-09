/*

Minimal emulation of an SDHC card in SPI mode

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

/* Our own defined states, not per specification */
enum sd_states { STBY, CMDFRAME, RESP, RESP_R7, SENDCSD,
		 TOKEN, SBLKREAD, RTOKEN, SBLKWRITE, DATARESP };

/* const char *state_dbg_desc[] = { "STBY", "CFRM", "RESP", "RESP7",
	"SNCSD", "TOKEN", "SBLRD", "RTOKN", "SBLWR", "DATAR" }; */

/* SD card registers */
static enum sd_states state_sd;

/* struct me */
static enum sd_states current_cmd;
static int cmdcount;
static uint8_t cmdarg[6];
static uint8_t blkbuf[512];
static int32_t address;
static int blkcount;
static int respcount;
static int csdcount;
static int idle_state = 0;
static int acmd = 0;

#define MY_OCR 0x40300000 /* big endian */
// static const uint8_t ocr[4] = { 0x40, 0x30, 0x00, 0x00 };
static const uint8_t csd[16] = { 0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00, 0x00,
			  0x39, 0xb7, 0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01 };

#define APP_FLAG 0x100 /* our flag */
#define CMD(x) (0x40 | x)
#define ACMD(x) (APP_FLAG | CMD(x))

#define SDIMAGE "sdcard.img"

static void read_image(uint8_t *buffer, uint32_t lba)
{
		FILE *sd_image;

		sd_image = fopen(SDIMAGE, "rb");
		if (!sd_image)
		{
			fprintf(stderr, "Error opening SD card image %s\n", SDIMAGE);
			return;
		}
		fseek(sd_image, lba * 512, SEEK_SET);
		// fprintf(stderr, "\nReading SD card image %s at LBA %d\n", SDIMAGE, lba);
		if (fread(buffer, 512, 1, sd_image) != 1)
			fprintf(stderr, "Short read from SD card image %s\n", SDIMAGE);
		fclose(sd_image);
}

static void write_image(uint8_t *buffer, uint32_t lba)
{
		FILE *sd_image;

		sd_image = fopen(SDIMAGE, "r+b");
		if (!sd_image)
			fprintf(stderr, "Error opening SD card image %s\n", SDIMAGE);
		fseek(sd_image, lba * 512, SEEK_SET);
		// fprintf(stderr, "\nWriting SD card image %s at LBA %d\n", SDIMAGE, lba);
		if (fwrite(buffer, 512, 1, sd_image) != 1)
			fprintf(stderr, "Short write to SD card image %s\n", SDIMAGE);
		fclose(sd_image);
}

uint8_t spi_sdcard_transfer(uint8_t data_out, int ss_active)
{
	enum sd_states next = state_sd;
	uint8_t data_in = 0xFF;

	// fprintf(stderr, "[%s]\t -> %02x ", state_dbg_desc[state_sd], data_out);

	if (!ss_active) {
		next = STBY;
	}

	if (state_sd < CMDFRAME && ss_active && (data_out & 0xC0) == 0x40) {
		/* start of command frame */
		if (acmd)
			current_cmd = ACMD(data_out);
		else
			current_cmd = data_out;
		acmd = 0;
		cmdcount = 0;
		next = CMDFRAME;
	} else if (state_sd == CMDFRAME && ss_active) {
		/* inside command frame */
		cmdarg[cmdcount] = data_out;
		cmdcount++;
		if (cmdcount == 6) {
			address = (cmdarg[0] << 24) | (cmdarg[1] << 16) |
			      (cmdarg[2] << 8) | cmdarg[3];
			next = RESP;
		}
	} else if (state_sd == RESP) {
		next = STBY; /* default R1 response */
		if (current_cmd == CMD(0))         /* GO_IDLE_STATE */
			idle_state = 1;
		else if (current_cmd == ACMD(41))  /* APP_SEND_OP_COND */
			idle_state = 0;
		else if (current_cmd == CMD(55))   /* APP_CMD */
			acmd = 1;
		else if (current_cmd == CMD(17))   /* READ_SINGLE_BLOCK */
			next = TOKEN;
		else if (current_cmd == CMD(24))   /* WRITE_BLOCK */
			next = RTOKEN;
		else if (current_cmd == CMD(9))    /* SEND_CSD */
			next = TOKEN;
		else if (current_cmd == CMD(8)) {  /* SEND_IF_COND */
			next = RESP_R7;
			address = 0x1AA; /* voltage (use cmdarg?) */
			respcount = 0;
		} else if (current_cmd == CMD(58)) { /* READ_OCR */
			next = RESP_R7;
			address = MY_OCR; /* FIXME use ocr array */
			respcount = 0;
		}
		data_in = idle_state; /* signal Success + Idle State in R1 */
		// fprintf(stderr, " (%0x %d) ", current_cmd, idle_state);
	} else if (state_sd == RESP_R7) {
		data_in = (address >> ((3 - respcount) * 8)) & 0xFF;
		if (++respcount == 4)
			next = STBY;
	} else if (state_sd == TOKEN && current_cmd == CMD(9)) {
		csdcount = 0;
		data_in = 0xFE;
		next = SENDCSD;
	} else if (state_sd == TOKEN && current_cmd == CMD(17)) {
		read_image(blkbuf, address);
		blkcount = 0;
		data_in = 0xFE;
		next = SBLKREAD;
	} else if (state_sd == RTOKEN && current_cmd == CMD(24)) {
		if (data_out == 0xFE) {
			blkcount = 0;
			next = SBLKWRITE;
		}
	} else if (state_sd == SBLKREAD) {
		if (blkcount < 512)
			data_in = blkbuf[blkcount];
		else if (blkcount == 512)
			data_in = 0xAA; /* fake CRC 1 */
		else if (blkcount == 512 + 1) {
			data_in = 0xAA; /* fake CRC 2 */
			next = STBY;
		}
		blkcount++;
	} else if (state_sd == SBLKWRITE) {
		if (blkcount < 512)
			blkbuf[blkcount] = data_out;
		else if (blkcount == 512 + 1) { /* CRC ignored */
			write_image(blkbuf, address);
			next = DATARESP;
		}
		blkcount++;
	} else if (state_sd == DATARESP) {
		data_in = 0x05; /* Data Accepted */
		next = STBY;
	} else if (state_sd == SENDCSD) {
		if (csdcount < 16)
			data_in = csd[csdcount];
		else {
			data_in = 0xAA; /* fake CRC */
			next = STBY;
		}
		csdcount++;
	}
	// fprintf(stderr, " <- %02x\n", data_in);
	state_sd = next;
	return data_in;
}

void spi_sdcard_reset(void)
{
	state_sd = STBY;
}
