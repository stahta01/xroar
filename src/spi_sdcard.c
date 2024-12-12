/** \file
 *
 *  \brief Minimal emulation of an SDHC card in SPI mode.
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

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "delegate.h"
#include "xalloc.h"

#include "blockdev.h"
#include "logging.h"
#include "part.h"
#include "serialise.h"
#include "spi65.h"
#include "ui.h"

/* Our own defined states, not per specification */
enum sd_states { STBY, CMDFRAME, RESP, RESP_R7, SENDCSD,
		 TOKEN, SBLKREAD, RTOKEN, SBLKWRITE, DATARESP };

static const char *state_dbg_desc[] = {
	"STBY", "CFRM", "RESP", "RESP7",
	"SNCSD", "TOKEN", "SBLRD", "RTOKN",
	"SBLWR", "DATAR"
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct spi_sdcard {
	struct spi65_device spi65_device;

	// Backing image
	char *imagefile;
	struct blkdev *bd;

	// SD card registers
	unsigned state_sd;
	unsigned current_cmd;
	unsigned cmdcount;
	uint8_t cmdarg[6];
	uint8_t blkbuf[512];
	uint32_t address;
	unsigned blkcount;
	unsigned respcount;
	unsigned csdcount;
	_Bool idle_state;
	_Bool acmd;
};

#define SPI_SDCARD_SER_CMDARG (5)
#define SPI_SDCARD_SER_BLKBUF (6)

static const struct ser_struct ser_struct_spi_sdcard[] = {
	SER_ID_STRUCT_ELEM(1, struct spi_sdcard, imagefile),
	SER_ID_STRUCT_ELEM(2, struct spi_sdcard, state_sd),
	SER_ID_STRUCT_ELEM(3, struct spi_sdcard, current_cmd),
	SER_ID_STRUCT_ELEM(4, struct spi_sdcard, cmdcount),
	SER_ID_STRUCT_UNHANDLED(SPI_SDCARD_SER_CMDARG),
	SER_ID_STRUCT_UNHANDLED(SPI_SDCARD_SER_BLKBUF),
	SER_ID_STRUCT_ELEM(7, struct spi_sdcard, address),
	SER_ID_STRUCT_ELEM(8, struct spi_sdcard, blkcount),
	SER_ID_STRUCT_ELEM(9, struct spi_sdcard, respcount),
	SER_ID_STRUCT_ELEM(10, struct spi_sdcard, csdcount),
	SER_ID_STRUCT_ELEM(11, struct spi_sdcard, idle_state),
	SER_ID_STRUCT_ELEM(12, struct spi_sdcard, acmd),
};

static _Bool spi_sdcard_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool spi_sdcard_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data spi_sdcard_ser_struct_data = {
	.elems = ser_struct_spi_sdcard,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_spi_sdcard),
	.read_elem = spi_sdcard_read_elem,
	.write_elem = spi_sdcard_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define MY_OCR 0x40300000 /* big endian */
// static const uint8_t ocr[4] = { 0x40, 0x30, 0x00, 0x00 };
static const uint8_t csd[16] = { 0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00, 0x00,
			  0x39, 0xb7, 0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01 };

#define APP_FLAG 0x100 /* our flag */
#define CMD(x) (0x40 | x)
#define ACMD(x) (APP_FLAG | CMD(x))

static uint8_t spi_sdcard_transfer(void *sptr, uint8_t data_out, _Bool ss_active);
static void spi_sdcard_reset(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// SPI-SD creation

static struct part *spi_sdcard_allocate(void);
static void spi_sdcard_initialise(struct part *p, void *options);
static _Bool spi_sdcard_finish(struct part *p);
static void spi_sdcard_free(struct part *p);

static _Bool spi_sdcard_is_a(struct part *p, const char *name);

static const struct partdb_entry_funcs spi_sdcard_funcs = {
	.allocate = spi_sdcard_allocate,
	.initialise = spi_sdcard_initialise,
	.finish = spi_sdcard_finish,
	.free = spi_sdcard_free,

	.ser_struct_data = &spi_sdcard_ser_struct_data,

	.is_a = spi_sdcard_is_a,
};

const struct partdb_entry spi_sdcard_part = { .name = "SPI-SDCARD", .description = "SPI SD card interface", .funcs = &spi_sdcard_funcs };

static struct part *spi_sdcard_allocate(void) {
	struct spi_sdcard *sdcard = part_new(sizeof(*sdcard));
	struct part *p = &sdcard->spi65_device.part;

	*sdcard = (struct spi_sdcard){0};

	sdcard->spi65_device.transfer = DELEGATE_AS2(uint8, uint8, bool, spi_sdcard_transfer, sdcard);
	sdcard->spi65_device.reset = DELEGATE_AS0(void, spi_sdcard_reset, sdcard);

	return p;
}

static void spi_sdcard_initialise(struct part *p, void *options) {
	struct spi_sdcard *sdcard = (struct spi_sdcard *)p;
	if (options) {
		sdcard->imagefile = xstrdup((const char *)options);
	}
}

static _Bool spi_sdcard_finish(struct part *p) {
	struct spi_sdcard *sdcard = (struct spi_sdcard *)p;
	if (!sdcard->imagefile)
		return 0;
	sdcard->bd = bd_open(sdcard->imagefile);
	ui_update_state(-1, ui_tag_hd_filename, 0, sdcard->bd ? sdcard->imagefile : NULL);
	if (!sdcard->bd) {
		return 0;
	}
	return 1;
}

static void spi_sdcard_free(struct part *p) {
	struct spi_sdcard *sdcard = (struct spi_sdcard *)p;
	if (sdcard->bd)
		bd_close(sdcard->bd);
	if (sdcard->imagefile)
		free(sdcard->imagefile);
}

static _Bool spi_sdcard_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct spi_sdcard *sdcard = sptr;
	switch (tag) {
	case SPI_SDCARD_SER_CMDARG:
		ser_read(sh, sdcard->cmdarg, sizeof(sdcard->cmdarg));
		break;
	case SPI_SDCARD_SER_BLKBUF:
		ser_read(sh, sdcard->blkbuf, sizeof(sdcard->blkbuf));
		break;
	default:
		return 0;
		break;
	}
	return 1;
}

static _Bool spi_sdcard_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct spi_sdcard *sdcard = sptr;
	switch (tag) {
	case SPI_SDCARD_SER_CMDARG:
		ser_write(sh, tag, sdcard->cmdarg, sizeof(sdcard->cmdarg));
		break;
	case SPI_SDCARD_SER_BLKBUF:
		ser_write(sh, tag, sdcard->blkbuf, sizeof(sdcard->blkbuf));
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool spi_sdcard_is_a(struct part *p, const char *name) {
	(void)p;
	return strcmp(name, "spi-device") == 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void read_image(struct spi_sdcard *sdcard, uint8_t *buffer, uint32_t lba) {
	if (!bd_read_lsn(sdcard->bd, lba, buffer, 512)) {
		LOG_MOD_WARN("spi/sdcard", "%s: read failed\n", sdcard->imagefile);
	}
}

static void write_image(struct spi_sdcard *sdcard, uint8_t *buffer, uint32_t lba) {
	if (!bd_write_lsn(sdcard->bd, lba, buffer, 512)) {
		LOG_MOD_WARN("spi/sdcard", "%s: write failed\n", sdcard->imagefile);
	}
}

static uint8_t spi_sdcard_transfer(void *sptr, uint8_t data_out, _Bool ss_active) {
	struct spi_sdcard *sdcard = sptr;
	enum sd_states next = sdcard->state_sd;
	uint8_t data_in = 0xff;

	LOG_MOD_DEBUG(3, "spi/sdcard", "[%s]\t -> %02x ", state_dbg_desc[sdcard->state_sd], data_out);

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
