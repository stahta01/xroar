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

#ifndef XROAR_BLOCKDEV_H_
#define XROAR_BLOCKDEV_H_

#include <stdio.h>

/** Built-in HD types.  Taken from Alan Cox's IDE code.
 */
enum {
	BD_ACME_ROADRUNNER = 1, // 504MB classic IDE drive
	BD_ACME_COYOTE,         // 20MB early IDE drive
	BD_ACME_NEMESIS,        // 20MB LBA capable drive
	BD_ACME_ULTRASONICUS,   // 40MB LBA capable drive
	BD_ACME_ACCELLERATTI,   // 128MB LBA capable drive
	BD_ACME_ZIPPIBUS        // 256MB LBA capable drive
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/** \brief Block device public information.
 */

struct blkdev {
	FILE *fd;
};

/** \brief Open block device.
 *
 * \param name      Filename of device to open.
 *
 * \return New block device handle or NULL on error.
 */

struct blkdev *bd_open(const char *name);

/** \brief Create block device.
 *
 * \param name      Filename of device to open.
 * \param hd_type   One of BD_ACME_*.
 *
 * \return 1 on success, else 0.
 */

_Bool bd_create(const char *name, int hd_type);

/** \brief Close block device.
 */

void bd_close(struct blkdev *bd);

/** \brief Seek to a particular LSN.
 */

_Bool bd_seek_lsn(struct blkdev *bd, unsigned lsn);

/** \brief Read sector from current position.
 *
 * Must follow a successful seek.
 */

_Bool bd_read(struct blkdev *bd, void *buf, unsigned bufsize);

/** \brief Write sector to current position.
 *
 * Must follow a successful seek.
 */

_Bool bd_write(struct blkdev *bd, void *buf, unsigned bufsize);

/** \brief Read sector from block device in LSN mode.
 *
 * \return True if read succeeded.
 *
 * If the profile is ephemeral, it will be freed.
 */

_Bool bd_read_lsn(struct blkdev *bd, unsigned lsn, void *buf, unsigned bufsize);

/** \brief Write sector to block device in LSN mode.
 *
 * \return True if read succeeded.
 */

_Bool bd_write_lsn(struct blkdev *bd, unsigned lsn, void *buf, unsigned bufsize);

/** \brief Read sector from block device in CHS mode.
 *
 * \return True if read succeeded.
 */

_Bool bd_read_chs(struct blkdev *bd, unsigned c, unsigned h, unsigned s,
		  void *buf, unsigned bufsize);

/** \brief Write sector from block device in CHS mode.
 *
 * \return True if read succeeded.
 */

_Bool bd_write_chs(struct blkdev *bd, unsigned c, unsigned h, unsigned s,
		   void *buf, unsigned bufsize);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/** \brief Read IDE IDENTIFY DEVICE information.
 *
 * \return True if read succeeded.
 *
 * Destination will be populated with an array of 256 16-bit words explicitly
 * stored little-endian.
 */

_Bool bd_ide_read_identify(struct blkdev *bd, void *buf, unsigned bufsize);

/** \brief Extract ASCII string from IDE IDENTIFY DEVICE structure.
 *
 * \param bd        Block device structure.
 * \param index     Index into IDENTIFY DEVICE structure (in words).
 * \param size      Number of words to extract.
 *
 * \return          Pointer to allocated string.
 */

char *bd_ide_get_string(struct blkdev *bd, unsigned index, unsigned size);

/** \brief Update ASCII string within IDE IDENTIFY DEVICE structure.
 *
 * \param bd        Block device structure.
 * \param index     Index into IDENTIFY DEVICE structure (in words).
 * \param size      Number of words to store.
 * \param s         Source string.
 */

void bd_ide_set_string(struct blkdev *bd, unsigned index, unsigned size, char *s);

#endif
