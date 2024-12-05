/** \file
 *
 * \brief Block device abstraction.
 *
 * \copyright Copyright 2022-2024 Ciaran Anscomb
 *
 * \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 * XRoar is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * See COPYING.GPL for redistribution conditions.
 *
 * \endlicenseblock
 */

#include "top-config.h"

// for fseeko()
#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slist.h"
#include "xalloc.h"

#include "blockdev.h"
#include "fs.h"
#include "ide.h"
#include "logging.h"
#include "xroar.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/** \brief Block device private information.
 */

struct blkdev_private {
	struct blkdev blkdev;

	off_t filesize;  // total file size, including any offset
	size_t offset;  // bytes into image file of first sector
	unsigned sector_size;  // pad or truncate reads & writes to this
	unsigned num_sectors;  // total number of sectors in image

	_Bool valid_position;  // set after a successful seek

	struct {
		_Bool valid;  // 0 until something sets these parameters
		unsigned nheads;
		unsigned nsectors;
		unsigned sector_base;  // generally 0 or (more usual) 1
	} chs;

	struct {
		// IDE IDENTIFY DEVICE information.  This is stored in
		// native-endian format for easier manipulation, so must be
		// converted to little-endian by bd_read_identify().
		uint16_t identify[256];
	} ide;

};

static _Bool bd_ide_verify(struct blkdev_private *bdp);
static void bp_ide_identify_init(struct blkdev *bd);
static void bd_set_sector_size(struct blkdev_private *bdp, unsigned size);
static uint16_t from_le16(uint16_t v);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Open block device.

struct blkdev *bd_open(const char *name) {
	int filetype = xroar_filetype_by_ext(name);

	FILE *fd = fopen(name, "r+b");
	if (!fd) {
		LOG_MOD_WARN("blockdev", "%s: %s\n", name, strerror(errno));
		return NULL;
	}
	struct blkdev_private *bdp = xmalloc(sizeof(*bdp));
	struct blkdev *bd = &bdp->blkdev;
	*bdp = (struct blkdev_private){0};

	bd->fd = fd;
	bdp->filesize = fs_file_size(fd);

	switch (filetype) {
	case FILETYPE_IDE:
		if (bd_ide_verify(bdp)) {
			return bd;
		}
		// Lack of IDE headers is a fail for .ide files
		LOG_MOD_WARN("blockdev", "%s: IDE header not found\n", name);
		bd_close(bd);
		return NULL;

	case FILETYPE_VHD:
		// 256 bytes per sector
		bd_set_sector_size(bdp, 256);
		break;

	case FILETYPE_IMG:
	default:
		// 512 bytes per sector
		bd_set_sector_size(bdp, 512);
		break;
	}

	// If there were no IDE headers, populate required structures.
	bp_ide_identify_init(bd);
	return bd;
}

// Create new block device.

_Bool bd_create(const char *name, int hd_type) {
	int filetype = xroar_filetype_by_ext(name);

	int fd = open(name, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
	if (fd < 0) {
		LOG_MOD_WARN("blockdev", "unable to create: %s: %s\n", name, strerror(errno));
		return 0;
	}

	unsigned c, h, s;
	switch (hd_type) {
	default:
		hd_type = BD_ACME_NEMESIS;  // force default
		// fall through
	case BD_ACME_NEMESIS:
	case BD_ACME_COYOTE:
		c = 615;
		h = 4;
		s = 16;
		break;

	case BD_ACME_ACCELLERATTI:
		c = 1024;
		h = 16;
		s = 16;
		break;

	case BD_ACME_ZIPPIBUS:
		c = 1024;
		h = 16;
		s = 32;
		break;

	case BD_ACME_ULTRASONICUS:
		c = 977;
		h = 5;
		s = 16;
		break;
	}

	// Defer to IDE code to create images with the IDE magic header
	if (filetype == FILETYPE_IDE) {
		int r = ide_make_drive(hd_type, fd);
		if (r != 0) {
			LOG_MOD_SUB_WARN("blockdev", "ide", "unable to create: %s", name);
			if (r == -1) {
				LOG_PRINT(": %s", strerror(errno));
			}
			LOG_PRINT("\n");
			close(fd);
			return 0;
		}
		close(fd);
		return 1;
	}

	// Otherwise, create a raw sector image with no header
	size_t lsn = c * h * s;
	int ssize = 512;
	const char *sub = "img";

	// VHD images are 256 byte per sector; adjust accordingly:
	if (filetype == FILETYPE_VHD) {
		lsn *= 2;
		ssize = 256;
		sub = "vhd";
	}

	uint8_t sector[512];
	memset(sector, 0xe5, sizeof(sector));
	for (size_t i = 0; i < lsn; ++i) {
		if (write(fd, sector, ssize) != ssize) {
			LOG_MOD_SUB_WARN("blockdev", sub, "unable to create: %s: %s\n", name, strerror(errno));
			close(fd);
			return 0;
		}
	}
	close(fd);
	return 1;
}

// Close block device.

void bd_close(struct blkdev *bd) {
	fclose(bd->fd);
	free(bd);
}

// Seek to a particular LSN.

_Bool bd_seek_lsn(struct blkdev *bd, unsigned lsn) {
	assert(bd != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;
	// This should avoid overflows in lsn * sector_size
	bdp->valid_position = 0;
	if (lsn >= bdp->num_sectors) {
		return 0;
	}
	off_t offset = bdp->offset + lsn * bdp->sector_size;
	if (fseeko(bd->fd, offset, SEEK_SET) != 0)
		return 0;
	bdp->valid_position = 1;
	return 1;
}

// Read sector from current position.

_Bool bd_read(struct blkdev *bd, void *buf, unsigned bufsize) {
	assert(bd != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;

	if (bufsize > 0 && !buf) {
		return 0;
	}

	if (!bdp->valid_position) {
		return 0;
	}

	// Read
	unsigned nbytes = bufsize > bdp->sector_size ? bdp->sector_size : bufsize;
	if (fread(buf, nbytes, 1, bd->fd) < 1) {
		bdp->valid_position = 0;
		return 0;
	}

	// Pad caller-supplied buffer with zeroes
	while (nbytes < bufsize) {
		((uint8_t *)buf)[nbytes++] = 0;
	}

	return 1;
}

// Write sector to current position.

_Bool bd_write(struct blkdev *bd, void *buf, unsigned bufsize) {
	assert(bd != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;

	if (bufsize > 0 && !buf)
		return 0;

	if (!bdp->valid_position)
		return 0;

	// Write
	unsigned nbytes = bufsize > bdp->sector_size ? bdp->sector_size : bufsize;
	if (fwrite(buf, nbytes, 1, bd->fd) < 1) {
		bdp->valid_position = 0;
		return 0;
	}

	// Pad sector write with zeroes
	while (nbytes < bdp->sector_size) {
		if (fs_write_uint8(bd->fd, 0) < 0) {
			bdp->valid_position = 0;
			return 0;
		}
	}

	return 1;
}

// Read sector from block device in LBA mode.

_Bool bd_read_lsn(struct blkdev *bd, unsigned lsn, void *buf, unsigned bufsize) {
	bd_seek_lsn(bd, lsn);
	return bd_read(bd, buf, bufsize);
}

// Write sector to block device in LBA mode.

_Bool bd_write_lsn(struct blkdev *bd, unsigned lsn, void *buf, unsigned bufsize) {
	bd_seek_lsn(bd, lsn);
	return bd_write(bd, buf, bufsize);
}

// Read sector from block device in CHS mode.

_Bool bd_read_chs(struct blkdev *bd, unsigned c, unsigned h, unsigned s,
		  void *buf, unsigned bufsize) {
	assert(bd != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;
	if (!bdp->chs.valid)
		return 0;
	if (s >= bdp->chs.sector_base)
		s--;
	unsigned lsn = (((c * bdp->chs.nheads) + h) * bdp->chs.nsectors) + s;
	return bd_read_lsn(bd, lsn, buf, bufsize);
}

// Write sector from block device in CHS mode.

_Bool bd_write_chs(struct blkdev *bd, unsigned c, unsigned h, unsigned s,
		   void *buf, unsigned bufsize) {
	assert(bd != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;
	if (!bdp->chs.valid)
		return 0;
	if (s >= bdp->chs.sector_base)
		s--;
	unsigned lsn = (((c * bdp->chs.nheads) + h) * bdp->chs.nsectors) + s;
	return bd_write_lsn(bd, lsn, buf, bufsize);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Verify that an opened block device looks like an IDE image with header.
// Note: sets sector_size to 512.

static _Bool bd_ide_verify(struct blkdev_private *bdp) {
	struct blkdev *bd = &bdp->blkdev;

	// Buffer large enough for 512 bytes (organised as 256 16-bit
	// little-endian words) of IDENTIFY DEVICE information.
	bdp->offset = 0;
	bd_set_sector_size(bdp, 512);

	if (bdp->num_sectors < 2)
		return 0;

	// Test first 512 bytes for IDE magic.
	if (!bd_read_lsn(bd, 0, bdp->ide.identify, sizeof(bdp->ide.identify)))
		return 0;
	if (memcmp(bdp->ide.identify, ide_magic, 8) != 0) {
		return 0;
	}

	// Magic present - read the next 512 bytes as IDENTIFY DEVICE structure
	if (!bd_read(bd, bdp->ide.identify, sizeof(bdp->ide.identify)))
		return 0;

	// Byte swap if necessary
	for (unsigned i = 0; i < 256; i++)
		bdp->ide.identify[i] = from_le16(bdp->ide.identify[i]);

	// XXX
	//
	// What I expect to do here:
	//
	// - check data is internally consistent
	// - including: check ascii strings *are* all ascii
	// - report those strings to the user (or store them in struct
	//   blkdev so something else can)
	// - check sector counts sum to filesize

	// Ok, looks like IDE - update fields and return

	bdp->offset = 1024;
	bdp->num_sectors -= 2;

	uint16_t capabilities = bdp->ide.identify[49];
	if (capabilities & (1 << 9)) {
		//
	}

	return 1;
}

// Read IDENTIFY DEVICE information.  We store each 16-bit word in the native
// endianness, so this function explicitly converts it to little-endian, as
// expected by IDE drivers.  Supplied buffer must be large enough.

_Bool bd_ide_read_identify(struct blkdev *bd, void *buf, unsigned bufsize) {
	assert(bd != NULL);
	assert(buf != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;

	if (bufsize < 512) {
		return 0;
	}

	unsigned nwords = bufsize / 2;
	unsigned nbytes = 0;

	// Convert to little-endian
	for (unsigned i = 0; i < nwords; i++) {
		((uint8_t *)buf)[nbytes++] = bdp->ide.identify[i] & 0xff;
		((uint8_t *)buf)[nbytes++] = bdp->ide.identify[i] >> 8;
	}

	// Pad caller-supplied buffer with zeroes
	while (nbytes < bufsize) {
		((uint8_t *)buf)[nbytes++] = 0;
	}

	return 1;
}

// Initialise IDENTIFY DEVICE structure.

static void bp_ide_identify_init(struct blkdev *bd) {
	assert(bd != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;
	memset(bdp->ide.identify, 0, sizeof(bdp->ide.identify));

	bdp->ide.identify[0] = (1 << 15) | (1 << 6);  // Non-removable

	{
		char rbuf[21];
		// assume srand() called at startup
		snprintf(rbuf, sizeof(rbuf), "%04x%04x%04x%04x%04x",
			 rand() & 0xffff, rand() & 0xffff, rand() & 0xffff,
			 rand() & 0xffff, rand() & 0xffff);
		bd_ide_set_string(bd, 10, 10, rbuf);
	}

	bd_ide_set_string(bd, 23, 4, "A001.001");
	bd_ide_set_string(bd, 27, 20, "FAKE IDE BLOCK DEVICE");

	bdp->ide.identify[49] = (1 << 9);  // LBA
	bdp->ide.identify[51] = (240 << 8);  // PIO cycle time
	bdp->ide.identify[57] = bdp->num_sectors & 0xffff;
	bdp->ide.identify[58] = bdp->num_sectors >> 16;
	bdp->ide.identify[60] = bdp->ide.identify[57];
	bdp->ide.identify[61] = bdp->ide.identify[58];
}

// Return a new copy of an ASCII string within the IDE IDENTIFY DEVICE
// structure.  Invalid ASCII characters are converted to spaces.  Trailing
// spaces are stripped, and a NUL terminator byte is always included.

char *bd_ide_get_string(struct blkdev *bd, unsigned index, unsigned size) {
	assert(bd != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;

	// Allocate space for string + trailing NUL
	char *r = xmalloc((size * 2) + 1);

	// Extract string
	char *d = r;
	for (unsigned i = 0; i < size; i++) {
		if ((index + i) >= 256)
			break;
		uint16_t w = bdp->ide.identify[index + i];
		uint8_t c = w >> 8;
		if (c < 0x20 || (c > 0x7e))
			c = 0x20;
		*(d++) = c;
		c = w & 0xff;
		if (c < 0x20 || (c > 0x7e))
			c = 0x20;
		*(d++) = c;
	}
	*d = 0;

	// Strip trailing spaces
	for (; d > r && *(d-1) == 0x20; *(--d) = 0)
		;

	return r;
}

// Copy an ASCII string into the IDE IDENTIFY DEVICE structure.  Invalid ASCII
// characters are converted to spaces.  If a NUL terminator byte is encountered
// early, the remaining space is filled with spaces.

void bd_ide_set_string(struct blkdev *bd, unsigned index, unsigned size, char *s) {
	assert(bd != NULL);
	assert(s != NULL);
	struct blkdev_private *bdp = (struct blkdev_private *)bd;

	for (unsigned i = 0; i < size; i++) {
		if ((index + i) >= 256)
			break;
		uint8_t c0 = *s ? *(s++) : 0x20;
		uint8_t c1 = *s ? *(s++) : 0x20;
		if (c0 < 0x20 || c0 > 0x7e)
			c0 = 0x20;
		if (c1 < 0x20 || c1 > 0x7e)
			c1 = 0x20;
		bdp->ide.identify[index + i] = (c0 << 8) | c1;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Set sector size and recompute number of sectors

static void bd_set_sector_size(struct blkdev_private *bdp, unsigned size) {
	bdp->sector_size = size;
	bdp->num_sectors = (bdp->filesize - bdp->offset) / size;
}

// Convert 16-bit int from little-endian.  GCC optimises calls to this out
// completely at -O1 or above for (little-endian) x86.

static uint16_t from_le16(uint16_t v) {
	return *(uint8_t *)&v | (*((uint8_t *)&v+1) << 8);
}
