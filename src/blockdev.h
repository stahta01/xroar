/* Block device abstraction.
 *
 * Copyright 2022-2026 Ciaran Anscomb
 *
 * This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 * XRoar is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * See COPYING.GPL for redistribution conditions.
 */

#ifndef XROAR_BLOCKDEV_H_
#define XROAR_BLOCKDEV_H_

#include <stdio.h>

// Built-in HD types for passing to bd_create().  Taken from Alan Cox's IDE
// code.

enum {
	BD_ACME_ROADRUNNER = 1, // 504MB classic IDE drive
	BD_ACME_COYOTE,         // 20MB early IDE drive
	BD_ACME_NEMESIS,        // 20MB LBA capable drive
	BD_ACME_ULTRASONICUS,   // 40MB LBA capable drive
	BD_ACME_ACCELLERATTI,   // 128MB LBA capable drive
	BD_ACME_ZIPPIBUS        // 256MB LBA capable drive
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Block device public information.

struct blkdev {
	FILE *fd;
};

// Open block device.
//
// name:    Filename of device to open.
//
// Returns new block device handle or NULL on error.

struct blkdev *bd_open(const char *name);

// Create block device.
//
// name:    Filename of device to open.
// hd_type: One of BD_ACME_*.
//
// Returns true on success.

bool bd_create(const char *name, int hd_type);

// Close block device.

void bd_close(struct blkdev *bd);

// Query block device information.

unsigned bd_sector_size(struct blkdev *bd);
unsigned bd_num_sectors(struct blkdev *bd);

// Seek to a particular LSN.

bool bd_seek_lsn(struct blkdev *bd, unsigned lsn);

// Read sector from current position.  Must follow a successful seek.

bool bd_read(struct blkdev *bd, void *buf, unsigned bufsize);

// Write sector to current position.  Must follow a successful seek.

bool bd_write(struct blkdev *bd, void *buf, unsigned bufsize);

// Read sector from block device in LSN mode.
//
// Returns true if read succeeded.

bool bd_read_lsn(struct blkdev *bd, unsigned lsn, void *buf, unsigned bufsize);

// Write sector to block device in LSN mode.
//
// Returns true if write succeeded.

bool bd_write_lsn(struct blkdev *bd, unsigned lsn, void *buf, unsigned bufsize);

// Read sector from block device in CHS mode.
//
// Returns true if read succeeded.

bool bd_read_chs(struct blkdev *bd, unsigned c, unsigned h, unsigned s,
		  void *buf, unsigned bufsize);

// Write sector from block device in CHS mode.
//
// Returns true if write succeeded.

bool bd_write_chs(struct blkdev *bd, unsigned c, unsigned h, unsigned s,
		   void *buf, unsigned bufsize);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Read IDE IDENTIFY DEVICE information.
//
// Returns true if read succeeded.
//
// Destination will be populated with an array of 256 16-bit words explicitly
// stored little-endian.

bool bd_ide_read_identify(struct blkdev *bd, void *buf, unsigned bufsize);

// Extract ASCII string from IDE IDENTIFY DEVICE structure.
//
// bd:      Block device structure.
// index:   Index into IDENTIFY DEVICE structure (in words).
// size:    Number of words to extract.
//
// Returns pointer to allocated string.

char *bd_ide_get_string(struct blkdev *bd, unsigned index, unsigned size);

// Update ASCII string within IDE IDENTIFY DEVICE structure.
//
// bd:      Block device structure.
// index:   Index into IDENTIFY DEVICE structure (in words).
// size:    Number of words to store.
// s:       Source string.

void bd_ide_set_string(struct blkdev *bd, unsigned index, unsigned size, char *s);

#endif
