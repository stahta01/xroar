/** \file
 *
 *  \brief Minimal emulation of an SDHC card in SPI mode.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <stdio.h>

#include "delegate.h"
#include "xalloc.h"

#include "logging.h"
#include "spi65.h"

/* Our own defined states, not per specification */
enum sd_states { STBY, CMDFRAME, RESP, RESP_R7, SENDCSD,
		 TOKEN, SBLKREAD, RTOKEN, SBLKWRITE, DATARESP };

static const char *state_dbg_desc[] = {
	"STBY", "CFRM", "RESP", "RESP7",
	"SNCSD", "TOKEN", "SBLRD", "RTOKN",
	"SBLWR", "DATAR"
};

struct spi_sdcard {
	struct spi65_device spi65_device;

	// Backing image filename
	const char *imagefile;

	// SD card registers
	enum sd_states state_sd;
	enum sd_states current_cmd;
	int cmdcount;
	uint8_t cmdarg[6];
	uint8_t blkbuf[512];
	int32_t address;
	int blkcount;
	int respcount;
	int csdcount;
	int idle_state;
	int acmd;
};

#define MY_OCR 0x40300000 /* big endian */
// static const uint8_t ocr[4] = { 0x40, 0x30, 0x00, 0x00 };
static const uint8_t csd[16] = { 0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00, 0x00,
			  0x39, 0xb7, 0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01 };

#define APP_FLAG 0x100 /* our flag */
#define CMD(x) (0x40 | x)
#define ACMD(x) (APP_FLAG | CMD(x))

static uint8_t spi_sdcard_transfer(void *sptr, uint8_t data_out, _Bool ss_active);
static void spi_sdcard_reset(void *sptr);

struct spi65_device *spi_sdcard_new(const char *image) {
	struct spi_sdcard *sdcard = part_new(sizeof(*sdcard));
	*sdcard = (struct spi_sdcard){0};
	part_init(&sdcard->spi65_device.part, "SPI-SDCARD");
	sdcard->imagefile = xstrdup(image);
	sdcard->spi65_device.transfer = DELEGATE_AS2(uint8, uint8, bool, spi_sdcard_transfer, sdcard);
	sdcard->spi65_device.reset = DELEGATE_AS0(void, spi_sdcard_reset, sdcard);
	return (struct spi65_device *)sdcard;
}

static void read_image(struct spi_sdcard *sdcard, uint8_t *buffer, uint32_t lba) {
	FILE *sd_image = fopen(sdcard->imagefile, "rb");
	if (!sd_image) {
		LOG_WARN("SPI/SDCARD/READ: Error opening SD card image %s\n", sdcard->imagefile);
		return;
	}
	fseek(sd_image, lba * 512, SEEK_SET);
	LOG_DEBUG(3, "Reading SD card image %s at LBA %d\n", sdcard->imagefile, lba);
	if (fread(buffer, 512, 1, sd_image) != 1) {
		LOG_WARN("SPI/SDCARD/READ: Short read from SD card image %s\n", sdcard->imagefile);
	}
	fclose(sd_image);
}

static void write_image(struct spi_sdcard *sdcard, uint8_t *buffer, uint32_t lba) {
	FILE *sd_image = fopen(sdcard->imagefile, "r+b");
	if (!sd_image) {
		LOG_WARN("SPI/SDCARD/WRITE: Error opening SD card image %s\n", sdcard->imagefile);
		return;
	}
	fseek(sd_image, lba * 512, SEEK_SET);
	LOG_DEBUG(3, "Writing SD card image %s at LBA %d\n", sdcard->imagefile, lba);
	if (fwrite(buffer, 512, 1, sd_image) != 1) {
		LOG_WARN("SPI/SDCARD/WRITE: Short write to SD card image %s\n", sdcard->imagefile);
	}
	fclose(sd_image);
}

static uint8_t spi_sdcard_transfer(void *sptr, uint8_t data_out, _Bool ss_active) {
	struct spi_sdcard *sdcard = sptr;
	enum sd_states next = sdcard->state_sd;
	uint8_t data_in = 0xff;

	LOG_DEBUG(3, "[%s]\t -> %02x ", state_dbg_desc[sdcard->state_sd], data_out);

	if (!ss_active) {
		next = STBY;
	}

	if (sdcard->state_sd < CMDFRAME && ss_active && (data_out & 0xc0) == 0x40) {
		/* start of command frame */
		if (sdcard->acmd)
			sdcard->current_cmd = ACMD(data_out);
		else
			sdcard->current_cmd = data_out;
		sdcard->acmd = 0;
		sdcard->cmdcount = 0;
		next = CMDFRAME;
	} else if (sdcard->state_sd == CMDFRAME && ss_active) {
		/* inside command frame */
		sdcard->cmdarg[sdcard->cmdcount] = data_out;
		sdcard->cmdcount++;
		if (sdcard->cmdcount == 6) {
			sdcard->address = (sdcard->cmdarg[0] << 24) | (sdcard->cmdarg[1] << 16) |
			          (sdcard->cmdarg[2] << 8) | sdcard->cmdarg[3];
			next = RESP;
		}
	} else if (sdcard->state_sd == RESP) {
		next = STBY; /* default R1 response */
		if (sdcard->current_cmd == CMD(0))         /* GO_IDLE_STATE */
			sdcard->idle_state = 1;
		else if (sdcard->current_cmd == ACMD(41))  /* APP_SEND_OP_COND */
			sdcard->idle_state = 0;
		else if (sdcard->current_cmd == CMD(55))   /* APP_CMD */
			sdcard->acmd = 1;
		else if (sdcard->current_cmd == CMD(17))   /* READ_SINGLE_BLOCK */
			next = TOKEN;
		else if (sdcard->current_cmd == CMD(24))   /* WRITE_BLOCK */
			next = RTOKEN;
		else if (sdcard->current_cmd == CMD(9))    /* SEND_CSD */
			next = TOKEN;
		else if (sdcard->current_cmd == CMD(8)) {  /* SEND_IF_COND */
			next = RESP_R7;
			sdcard->address = 0x1aa; /* voltage (use sdcard->cmdarg?) */
			sdcard->respcount = 0;
		} else if (sdcard->current_cmd == CMD(58)) { /* READ_OCR */
			next = RESP_R7;
			sdcard->address = MY_OCR; /* FIXME use ocr array */
			sdcard->respcount = 0;
		}
		data_in = sdcard->idle_state; /* signal Success + Idle State in R1 */
		LOG_DEBUG(3, " (%0x %d) ", sdcard->current_cmd, sdcard->idle_state);
	} else if (sdcard->state_sd == RESP_R7) {
		data_in = (sdcard->address >> ((3 - sdcard->respcount) * 8)) & 0xff;
		if (++sdcard->respcount == 4)
			next = STBY;
	} else if (sdcard->state_sd == TOKEN && sdcard->current_cmd == CMD(9)) {
		sdcard->csdcount = 0;
		data_in = 0xfe;
		next = SENDCSD;
	} else if (sdcard->state_sd == TOKEN && sdcard->current_cmd == CMD(17)) {
		read_image(sdcard, sdcard->blkbuf, sdcard->address);
		sdcard->blkcount = 0;
		data_in = 0xfe;
		next = SBLKREAD;
	} else if (sdcard->state_sd == RTOKEN && sdcard->current_cmd == CMD(24)) {
		if (data_out == 0xfe) {
			sdcard->blkcount = 0;
			next = SBLKWRITE;
		}
	} else if (sdcard->state_sd == SBLKREAD) {
		if (sdcard->blkcount < 512)
			data_in = sdcard->blkbuf[sdcard->blkcount];
		else if (sdcard->blkcount == 512)
			data_in = 0xaa; /* fake CRC 1 */
		else if (sdcard->blkcount == 512 + 1) {
			data_in = 0xaa; /* fake CRC 2 */
			next = STBY;
		}
		sdcard->blkcount++;
	} else if (sdcard->state_sd == SBLKWRITE) {
		if (sdcard->blkcount < 512)
			sdcard->blkbuf[sdcard->blkcount] = data_out;
		else if (sdcard->blkcount == 512 + 1) { /* CRC ignored */
			write_image(sdcard, sdcard->blkbuf, sdcard->address);
			next = DATARESP;
		}
		sdcard->blkcount++;
	} else if (sdcard->state_sd == DATARESP) {
		data_in = 0x05; /* Data Accepted */
		next = STBY;
	} else if (sdcard->state_sd == SENDCSD) {
		if (sdcard->csdcount < 16)
			data_in = csd[sdcard->csdcount];
		else {
			data_in = 0xaa; /* fake CRC */
			next = STBY;
		}
		sdcard->csdcount++;
	}
	LOG_DEBUG(3, " <- %02x\n", data_in);
	sdcard->state_sd = next;
	return data_in;
}

static void spi_sdcard_reset(void *sptr) {
	struct spi_sdcard *sdcard = sptr;
	sdcard->state_sd = STBY;
}
