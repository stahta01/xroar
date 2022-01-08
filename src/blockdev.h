/** \file
 *
 * \brief Block device abstraction.
 *
 * \copyright Copyright 2022 Ciaran Anscomb
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

/** Once a device has been accessed in LBA mode, it will usually then not be
 * possible to change its structure with accesses in CHS mode.
 */

enum bd_type {
	bd_type_floppy,
	bd_type_hd
};

/** \brief Block device profile.
 */

struct blkdev_profile {
	char *name;      /// profile name
	char *filename;  /// backing filename

	unsigned type;
};

/** \brief Fetch profile by name.
 *
 * Creates a new profile if not found, with filename equal to name.  Unless
 * added to the internal list with bd_profile_register(), this will not be
 * permanently configured, and you'll have to free it manually, either with
 * bd_profile_free(), or indirectly by closing the device that created it.
 */

struct blkdev_profile *bd_profile_by_name(const char *name);

/** \brief Add profile to internal list.
 */

void bd_profile_register(struct blkdev_profile *profile);

/** \brief Free profile.
 */

void bd_profile_free(struct blkdev_profile *profile);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/** \brief Block device public information.
 */

struct blkdev {
	struct blkdev_profile *profile;
	FILE *fd;
};

/** \brief Open block device.
 *
 * \param name      Profile name of device to open.
 *
 * \return New block device handle or NULL on error.
 *
 * If the profile is not found, an ephemeral profile will be created with
 * filename = name.
 */

struct blkdev *bd_open(const char *name);

/** \brief Close block device.
 *
 * If the device's profile is ephemeral, it will be freed.
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
