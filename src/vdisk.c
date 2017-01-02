/*  Copyright 2003-2017 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * To avoid confusion, the position of the heads is referred to as the
 * 'cylinder' (often abbreviated to 'cyl').  The term 'track' refers only to
 * the data addressable within one cylinder by one head.  A 'side' is the
 * collection of all the tracks addressable by one head.
 */

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "xalloc.h"

#include "array.h"
#include "crc16.h"
#include "fs.h"
#include "logging.h"
#include "module.h"
#include "vdisk.h"
#include "xroar.h"

#define MAX_CYLINDERS (256)
#define MAX_HEADS (2)

static struct vdisk *vdisk_load_vdk(const char *filename);
static struct vdisk *vdisk_load_jvc(const char *filename);
static struct vdisk *vdisk_load_os9(const char *filename);
static struct vdisk *vdisk_load_dmk(const char *filename);
static int vdisk_save_vdk(struct vdisk *disk);
static int vdisk_save_jvc(struct vdisk *disk);
static int vdisk_save_dmk(struct vdisk *disk);

static struct {
	enum xroar_filetype filetype;
	struct vdisk *(* const load_func)(const char *);
	int (* const save_func)(struct vdisk *);
} const dispatch[] = {
	{ FILETYPE_VDK, vdisk_load_vdk, vdisk_save_vdk },
	{ FILETYPE_JVC, vdisk_load_jvc, vdisk_save_jvc },
	{ FILETYPE_OS9, vdisk_load_os9, vdisk_save_jvc },
	{ FILETYPE_DMK, vdisk_load_dmk, vdisk_save_dmk },
};

// Determines interleave of subsequently formatted disks.
static _Bool default_dragon_interleave = 1;

// "Standard" disk size.
static unsigned default_ncyls = 40;

void vdisk_default_interleave(_Bool dragon_interleave) {
	default_dragon_interleave = dragon_interleave;
}

void vdisk_default_ncyls(unsigned ncyls) {
	default_ncyls = ncyls;
}

struct vdisk *vdisk_blank_disk(unsigned ncyls, unsigned nheads,
			       unsigned track_length) {
	struct vdisk *disk;
	uint8_t **side_data;
	/* Ensure multiples of track_length will stay 16-bit aligned */
	if ((track_length % 2) != 0)
		track_length++;
	if (nheads < 1 || nheads > MAX_HEADS
			|| ncyls < 1 || ncyls > MAX_CYLINDERS
			|| track_length < 129 || track_length > 0x2940) {
		return NULL;
	}
	if (!(disk = malloc(sizeof(*disk))))
		return NULL;
	if (!(side_data = calloc(nheads, sizeof(*side_data))))
		goto cleanup;
	unsigned side_length = ncyls * track_length;
	for (unsigned i = 0; i < nheads; i++) {
		if (!(side_data[i] = calloc(side_length, 1)))
			goto cleanup_sides;
	}
	disk->filetype = FILETYPE_DMK;
	disk->filename = NULL;
	disk->fmt.vdk.extra_length = 0;
	disk->fmt.vdk.filename_length = 0;
	disk->fmt.vdk.extra = NULL;
	disk->write_back = xroar_cfg.disk_write_back;
	disk->write_protect = 0;
	disk->num_cylinders = ncyls;
	disk->num_heads = nheads;
	disk->track_length = track_length;
	disk->side_data = side_data;
	return disk;
cleanup_sides:
	for (unsigned i = 0; i < nheads; i++) {
		if (side_data[i])
			free(side_data[i]);
	}
	free(side_data);
cleanup:
	free(disk);
	return NULL;
}

void vdisk_destroy(struct vdisk *disk) {
	if (disk == NULL) return;
	if (disk->filename) {
		free(disk->filename);
		disk->filename = NULL;
	}
	if (disk->fmt.vdk.extra) {
		free(disk->fmt.vdk.extra);
		disk->fmt.vdk.extra_length = 0;
	}
	for (unsigned i = 0; i < disk->num_heads; i++) {
		if (disk->side_data[i])
			free(disk->side_data[i]);
	}
	free(disk->side_data);
	free(disk);
}

struct vdisk *vdisk_load(const char *filename) {
	if (filename == NULL) return NULL;
	enum xroar_filetype filetype = xroar_filetype_by_ext(filename);
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(dispatch); i++) {
		if (dispatch[i].filetype == filetype) {
			return dispatch[i].load_func(filename);
		}
	}
	LOG_WARN("No reader for virtual disk file type.\n");
	return NULL;
}

int vdisk_save(struct vdisk *disk, _Bool force) {
	int i;
	if (!disk)
		return -1;
	if (!force && !disk->write_back) {
		LOG_DEBUG(1, "Not saving disk file: write-back is disabled.\n");
		// This is the requested behaviour, so success:
		return 0;
	}
	if (disk->filename == NULL) {
		disk->filename = filereq_module->save_filename(NULL);
		if (disk->filename == NULL) {
			 LOG_WARN("No filename given: not writing disk file.\n");
			 return -1;
		}
		disk->filetype = xroar_filetype_by_ext(disk->filename);
	}
	for (i = 0; dispatch[i].filetype >= 0 && dispatch[i].filetype != disk->filetype; i++);
	if (dispatch[i].save_func == NULL) {
		LOG_WARN("No writer for virtual disk file type.\n");
		return -1;
	}
	int bf_len = strlen(disk->filename) + 5;
	char backup_filename[bf_len];
	// Rename old file to filename.bak if that .bak does not already exist
	snprintf(backup_filename, bf_len, "%s.bak", disk->filename);
	struct stat statbuf;
	if (stat(backup_filename, &statbuf) != 0) {
		rename(disk->filename, backup_filename);
	}
	return dispatch[i].save_func(disk);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * VDK header entry meanings taken from the source to PC-Dragon II:

 * [0..1]       Magic identified string "dk"
 * [2..3]       Header length (little-endian)
 * [4]          VDK version
 * [5]          VDK backwards compatibility version
 * [6]          Identity of file source ('P' - PC Dragon, 'X' - XRoar)
 * [7]          Version of file source
 * [8]          Number of cylinders
 * [9]          Number of heads
 * [10]         Flags
 * [11]         Compression flags (bits 0-2) and name length (bits 3-7)

 * PC-Dragon then reserves 31 bytes for a disk name, the number of significant
 * characters in which are indicated in the name length bitfield of byte 11.
 *
 * The only flag used by XRoar is bit 0 which indicates write protect.
 * Compressed data in VDK disk images is not supported.  XRoar will store extra
 * header bytes and rewrite them verbatim.
 */

static struct vdisk *vdisk_load_vdk(const char *filename) {
	struct vdisk *disk;
	ssize_t file_size;
	unsigned header_size;
	unsigned ncyls;
	unsigned nheads = 1;
	unsigned nsectors = 18;
	unsigned ssize_code = 1, ssize;
	_Bool write_protect;
	int vdk_filename_length;
	uint8_t buf[1024];
	FILE *fd;
	struct stat statbuf;
	if (stat(filename, &statbuf) != 0)
		return NULL;
	file_size = statbuf.st_size;
	if (!(fd = fopen(filename, "rb")))
		return NULL;
	if (fread(buf, 12, 1, fd) < 1) {
		LOG_WARN("Failed to read VDK header in '%s'\n", filename);
		fclose(fd);
		return NULL;
	}
	file_size -= 12;
	(void)file_size;  // TODO: check this matches what's going to be read
	if (buf[0] != 'd' || buf[1] != 'k') {
		fclose(fd);
		return NULL;
	}
	if ((buf[11] & 7) != 0) {
		LOG_WARN("Compressed VDK not supported: '%s'\n", filename);
		fclose(fd);
		return NULL;
	}
	header_size = (buf[2] | (buf[3]<<8)) - 12;
	ncyls = buf[8];
	nheads = buf[9];
	write_protect = buf[10] & 1;
	vdk_filename_length = buf[11] >> 3;
	uint8_t *vdk_extra = NULL;
	if (header_size > 0) {
		vdk_extra = malloc(header_size);
		if (!vdk_extra) {
			fclose(fd);
			return NULL;
		}
		if (fread(vdk_extra, header_size, 1, fd) < 1) {
			LOG_WARN("Failed to read VDK header in '%s'\n", filename);
			free(vdk_extra);
			fclose(fd);
			return NULL;
		}
	}
	ssize = 128 << ssize_code;
	disk = vdisk_blank_disk(ncyls, nheads, VDISK_LENGTH_5_25);
	if (!disk) {
		fclose(fd);
		return NULL;
	}
	disk->filetype = FILETYPE_VDK;
	disk->filename = xstrdup(filename);
	disk->write_protect = write_protect;
	disk->fmt.vdk.extra_length = header_size;
	disk->fmt.vdk.filename_length = vdk_filename_length;
	disk->fmt.vdk.extra = vdk_extra;

	if (vdisk_format_disk(disk, 1, nsectors, 1, ssize_code) != 0) {
		fclose(fd);
		vdisk_destroy(disk);
		return NULL;
	}
	LOG_DEBUG(1, "Loading VDK virtual disk: %uC %uH %uS (%u-byte)\n", ncyls, nheads, nsectors, ssize);
	for (unsigned cyl = 0; cyl < ncyls; cyl++) {
		for (unsigned head = 0; head < nheads; head++) {
			for (unsigned sector = 0; sector < nsectors; sector++) {
				if (fread(buf, ssize, 1, fd) < 1) {
					memset(buf, 0, ssize);
				}
				vdisk_update_sector(disk, cyl, head, sector + 1, ssize, buf);
			}
		}
	}
	fclose(fd);
	return disk;
}

static int vdisk_save_vdk(struct vdisk *disk) {
	uint8_t buf[1024];
	FILE *fd;
	if (disk == NULL)
		return -1;
	if (!(fd = fopen(disk->filename, "wb")))
		return -1;
	LOG_DEBUG(1, "Writing VDK virtual disk: %uC %uH (%u-byte)\n", disk->num_cylinders, disk->num_heads, disk->track_length);
	uint16_t header_length = 12;
	buf[0] = 'd';   // magic
	buf[1] = 'k';   // magic
	buf[4] = 0x10;  // VDK version
	buf[5] = 0x10;  // VDK backwards compatibility version
	buf[6] = 'X';   // file source - 'X' for XRoar
	buf[7] = 0;     // version of file source
	buf[8] = disk->num_cylinders;
	buf[9] = disk->num_heads;
	buf[10] = 0;    // flags
	buf[11] = disk->fmt.vdk.filename_length << 3;  // name length & compression flag
	if (disk->fmt.vdk.extra_length > 0)
		header_length += disk->fmt.vdk.extra_length;
	buf[2] = header_length & 0xff;
	buf[3] = header_length >> 8;
	fwrite(buf, 12, 1, fd);
	if (disk->fmt.vdk.extra_length > 0) {
		fwrite(disk->fmt.vdk.extra, disk->fmt.vdk.extra_length, 1, fd);
	}
	for (unsigned cyl = 0; cyl < disk->num_cylinders; cyl++) {
		for (unsigned head = 0; head < disk->num_heads; head++) {
			for (unsigned sector = 0; sector < 18; sector++) {
				vdisk_fetch_sector(disk, cyl, head, sector + 1, 256, buf);
				fwrite(buf, 256, 1, fd);
			}
		}
	}
	fclose(fd);
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * The JVC format (as used in Jeff Vavasour's emulators) is also known as DSK.
 * It consists of an optional header followed by a simple dump of the sector
 * data.  The header length is the file size modulo 256.  The header needn't be
 * large enough to contain all fields if they are to have their default value.
 * Potential header information, and the default values:

 * [0]  Sectors per track       18
 * [1]  Sides (1-2)             1
 * [2]  Sector size code (0-3)  1 (== 256 bytes)
 * [3]  First sector ID         1
 * [4]  Sector attribute flag   0

 * Sector size is 128 * 2 ^ (size code).  If the "sector attribute flag" is
 * non-zero, it indicates that each sector is preceded by an attribute byte,
 * containing the following information in its bitfields:

 * Bit 3        Set on CRC error
 * Bit 4        Set if sector not found
 * Bit 5        0 = Data Mark, 1 = Deleted Data Mark

 * The potential for 128-byte sectors, and for each sector to be one byte
 * larger would interfere with the "modulo 256" method of identifying header
 * size, so XRoar takes the following precautions:
 *
 * 1. Header is identified by file size modulo 128 instead of modulo 256.
 *
 * 2. If XRoar is expanded in the future to write sector attribute bytes,
 * padding bytes of zero should appear at the end of the file such that the
 * total file size modulo 128 remains equal to the amount of bytes in the
 * header.
 *
 * Some images are distributed with partial last tracks.  XRoar reads as much
 * of the track as is available.
 *
 * Some images seen in the wild are double-sided without containing header
 * information.  If it looks like such an image contains an OS-9 filesystem,
 * XRoar will try and extract geometry information from the first sector.  This
 * can be disabled with the "-no-disk-auto-os9" option, but if the filename
 * ends in ".os9", the check is performed regardless.
 */

static struct vdisk *do_load_jvc(const char *filename, _Bool auto_os9) {
	unsigned nsectors = 18;
	unsigned nheads = 1;
	unsigned ssize_code = 1;
	unsigned first_sector = 1;
	_Bool double_density = 1;
	_Bool sector_attr_flag = 0;
	_Bool headerless_os9 = 0;

	uint8_t buf[1024];
	FILE *fd;

	struct stat statbuf;
	if (stat(filename, &statbuf) != 0)
		return NULL;
	off_t file_size = statbuf.st_size;
	unsigned header_size = file_size % 128;
	file_size -= header_size;

	if (!(fd = fopen(filename, "rb")))
		return NULL;

	if (header_size > 0) {
		if (fread(buf, header_size, 1, fd) < 1) {
			LOG_WARN("Failed to read JVC header in '%s'\n", filename);
			fclose(fd);
			return NULL;
		}
		nsectors = buf[0];
		if (header_size >= 2)
			nheads = buf[1];
		if (header_size >= 3)
			ssize_code = buf[2] & 3;
		if (header_size >= 4)
			first_sector = buf[3];
		if (header_size >= 5)
			sector_attr_flag = buf[4];
	} else if (auto_os9) {
		/* read first sector & check it makes sense */
		if (fread(buf + 256, 256, 1, fd) < 1) {
			LOG_WARN("Failed to read from JVC '%s'\n", filename);
			fclose(fd);
			return NULL;
		}
		unsigned dd_tot = (buf[0x100+0] << 16) | (buf[0x100+1] << 8) | buf[0x100+2];
		off_t os9_file_size = dd_tot * 256;
		unsigned dd_tks = buf[0x100+0x03];
		uint8_t dd_fmt = buf[0x100+0x10];
		unsigned dd_fmt_sides = (dd_fmt & 1) + 1;
		unsigned dd_spt = (buf[0x100+0x11] << 8) | buf[0x100+0x12];

		if (os9_file_size >= file_size && dd_tks == dd_spt) {
			nsectors = dd_tks;
			nheads = dd_fmt_sides;
			headerless_os9 = 1;
		}
		fseek(fd, 0, SEEK_SET);
	}

	unsigned ssize = 128 << ssize_code;
	unsigned bytes_per_sector = ssize + sector_attr_flag;
	unsigned bytes_per_cyl = nsectors * bytes_per_sector * nheads;
	if (bytes_per_cyl == 0) {
		LOG_WARN("Bad JVC header in '%s'\n", filename);
		fclose(fd);
		return NULL;
	}
	unsigned ncyls = file_size / bytes_per_cyl;
	// Too many tracks is implausible, so assume this (single-sided) means
	// a 720K disk.
	if (ncyls >= 88 && nheads == 1) {
		ncyls <<= 1;
		nheads++;
		bytes_per_cyl = nsectors * bytes_per_sector * nheads;
		ncyls = file_size / bytes_per_cyl;
	}
	// if there is at least one more sector of data, allow an extra track
	if ((file_size % bytes_per_cyl) >= bytes_per_sector) {
		ncyls++;
	}
	if (xroar_cfg.disk_auto_sd && nsectors == 10)
		double_density = 0;

	struct vdisk *disk = vdisk_blank_disk(ncyls, nheads, VDISK_LENGTH_5_25);
	if (!disk) {
		fclose(fd);
		return NULL;
	}
	disk->filetype = FILETYPE_JVC;
	disk->filename = xstrdup(filename);
	disk->fmt.jvc.headerless_os9 = headerless_os9;
	if (vdisk_format_disk(disk, double_density, nsectors, first_sector, ssize_code) != 0) {
		fclose(fd);
		vdisk_destroy(disk);
		return NULL;
	}
	if (headerless_os9) {
		LOG_DEBUG(1, "Loading headerless OS-9 virtual disk: %uC %uH %uS (%u-byte)\n", ncyls, nheads, nsectors, ssize);
	} else {
		LOG_DEBUG(1, "Loading JVC virtual disk: %uC %uH %uS (%u-byte)\n", ncyls, nheads, nsectors, ssize);
	}
	for (unsigned cyl = 0; cyl < ncyls; cyl++) {
		for (unsigned head = 0; head < nheads; head++) {
			for (unsigned sector = 0; sector < nsectors; sector++) {
				unsigned attr;
				if (sector_attr_flag) {
					attr = fs_read_uint8(fd);
				}
				(void)attr;  // not used yet...
				if (fread(buf, ssize, 1, fd) < 1) {
					memset(buf, 0, ssize);
				}
				vdisk_update_sector(disk, cyl, head, sector + first_sector, ssize, buf);
			}
		}
	}
	fclose(fd);
	return disk;
}

static struct vdisk *vdisk_load_jvc(const char *filename) {
	return do_load_jvc(filename, xroar_cfg.disk_auto_os9);
}

static struct vdisk *vdisk_load_os9(const char *filename) {
	return do_load_jvc(filename, 1);
}

static int vdisk_save_jvc(struct vdisk *disk) {
	unsigned nsectors = 18;
	uint8_t buf[1024];
	FILE *fd;
	if (!disk)
		return -1;
	if (!(fd = fopen(disk->filename, "wb")))
		return -1;
	LOG_DEBUG(1, "Writing JVC virtual disk: %uC %uH (%u-byte)\n", disk->num_cylinders, disk->num_heads, disk->track_length);

	// TODO: scan the disk to potentially correct these assumptions
	buf[0] = nsectors;  // assumed
	buf[1] = disk->num_heads;
	buf[2] = 1;  // assumed 256 byte sectors
	buf[3] = 1;  // assumed first sector == 1
	buf[4] = 0;

	// don't write a header if OS-9 detection didn't find one
	unsigned header_size = 0;
	if (disk->num_heads != 1 && !disk->fmt.jvc.headerless_os9)
		header_size = 2;

	if (header_size > 0) {
		fwrite(buf, header_size, 1, fd);
	}

	for (unsigned cyl = 0; cyl < disk->num_cylinders; cyl++) {
		for (unsigned head = 0; head < disk->num_heads; head++) {
			for (unsigned sector = 0; sector < nsectors; sector++) {
				vdisk_fetch_sector(disk, cyl, head, sector + 1, 256, buf);
				fwrite(buf, 256, 1, fd);
			}
		}
	}
	fclose(fd);
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * DMK is the format use by David Keil's emulators.  It preserves far more of
 * the underlying disk format than VDK or JVC.  A 16-byte header is followed by
 * raw track data as it would be written by the disk controller, though minus
 * the clocking information.  A special table preceeding each track contains
 * the location of sector ID Address Marks.  Header information is as follows:

 * [0]          Write protect ($00 = write enable, $FF = write protect)
 * [1]          Number of cylinders
 * [2..3]       Track length including 128-byte IDAM table (little-endian)
 * [4]          Option flags
 * [5..11]      Reserved
 * [12..15]     Must be 0x000000.  0x12345678 flags a real drive (unsupported)

 * In the option flags byte, bit 4 indicates a single-sided disk if set.  Bit 6
 * flags single-density-only, and bit 7 indicates mixed density; both are
 * ignored by XRoar.
 *
 * Next follows track data, each track consisting of a 64-entry table of 16-bit
 * (little-endian) IDAM offsets.  These offsets are relative to the beginning
 * of the track data (and so include the size of the table itself).
 *
 * Because XRoar maintains separate ideas of write protect and write back, the
 * write protect flag is interpreted as write back instead - a value of $FF
 * will disable overwriting the disk image with changes made in memory.  A
 * separate header entry at offset 11 (last of the reserved bytes) is used to
 * indicate write protect instead.
 */

static struct vdisk *vdisk_load_dmk(const char *filename) {
	struct vdisk *disk;
	uint8_t header[16];
	ssize_t file_size;
	unsigned nheads;
	unsigned ncyls;
	unsigned track_length;
	FILE *fd;
	struct stat statbuf;

	if (stat(filename, &statbuf) != 0)
		return NULL;
	file_size = statbuf.st_size;
	if (!(fd = fopen(filename, "rb")))
		return NULL;

	if (fread(header, 16, 1, fd) < 1) {
		LOG_WARN("Failed to read DMK header in '%s'\n", filename);
		fclose(fd);
		return NULL;
	}
	ncyls = header[1];
	track_length = (header[3] << 8) | header[2];  // yes, little-endian!
	nheads = (header[4] & 0x10) ? 1 : 2;
	if (header[4] & 0x40)
		LOG_WARN("DMK is flagged single-density only\n");
	if (header[4] & 0x80)
		LOG_WARN("DMK is flagged density-agnostic\n");
	file_size -= 16;
	(void)file_size;  // TODO: check this matches what's going to be read
	disk = vdisk_blank_disk(ncyls, nheads, VDISK_LENGTH_5_25);
	if (disk == NULL) {
		fclose(fd);
		return NULL;
	}
	LOG_DEBUG(1, "Loading DMK virtual disk: %uC %uH (%u-byte)\n", ncyls, nheads, track_length);
	disk->filetype = FILETYPE_DMK;
	disk->filename = xstrdup(filename);
	disk->write_back = header[0] ? 0 : 1;
	if (header[11] == 0 || header[11] == 0xff) {
		disk->write_protect = header[11] ? 1 : 0;
	} else {
		disk->write_protect = !disk->write_back;
	}

	for (unsigned cyl = 0; cyl < ncyls; cyl++) {
		for (unsigned head = 0; head < nheads; head++) {
			uint16_t *idams = vdisk_extend_disk(disk, cyl, head);
			if (!idams) {
				fclose(fd);
				return NULL;
			}
			uint8_t *buf = (uint8_t *)idams + 128;
			for (unsigned i = 0; i < 64; i++) {
				idams[i] = fs_read_uint16_le(fd);
			}
			if (fread(buf, track_length - 128, 1, fd) < 1) {
				memset(buf, 0, track_length - 128);
			}
		}
	}
	fclose(fd);
	return disk;
}

static int vdisk_save_dmk(struct vdisk *disk) {
	uint8_t header[16];
	FILE *fd;
	if (!disk)
		return -1;
	if (!(fd = fopen(disk->filename, "wb")))
		return -1;
	LOG_DEBUG(1, "Writing DMK virtual disk: %uC %uH (%u-byte)\n", disk->num_cylinders, disk->num_heads, disk->track_length);
	memset(header, 0, sizeof(header));
	if (!disk->write_back)
		header[0] = 0xff;
	header[1] = disk->num_cylinders;
	header[2] = disk->track_length & 0xff;
	header[3] = (disk->track_length >> 8) & 0xff;
	if (disk->num_heads == 1)
		header[4] |= 0x10;
	header[11] = disk->write_protect ? 0xff : 0;
	fwrite(header, 16, 1, fd);
	for (unsigned cyl = 0; cyl < disk->num_cylinders; cyl++) {
		for (unsigned head = 0; head < disk->num_heads; head++) {
			uint16_t *idams = vdisk_track_base(disk, cyl, head);
			uint8_t *buf = (uint8_t *)idams + 128;
			int i;
			if (idams == NULL) continue;
			for (i = 0; i < 64; i++) {
				fs_write_uint16_le(fd, idams[i]);
			}
			fwrite(buf, disk->track_length - 128, 1, fd);
		}
	}
	fclose(fd);
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Returns a pointer to the beginning of the specified track.  Return type is
 * (void *) because track data is manipulated in 8-bit and 16-bit chunks.
 */

void *vdisk_track_base(struct vdisk const *disk, unsigned cyl, unsigned head) {
	if (disk == NULL || head >= disk->num_heads || cyl >= disk->num_cylinders) {
		return NULL;
	}
	return disk->side_data[head] + cyl * disk->track_length;
}

/*
 * Write routines call this instead of vdisk_track_base() - it will increase
 * disk size if required.  Returns same as above.
 */

void *vdisk_extend_disk(struct vdisk *disk, unsigned cyl, unsigned head) {
	if (!disk)
		return NULL;
	if (cyl >= MAX_CYLINDERS)
		return NULL;
	uint8_t **side_data = disk->side_data;
	unsigned nheads = disk->num_heads;
	unsigned ncyls = disk->num_cylinders;
	unsigned tlength = disk->track_length;
	if (head >= nheads) {
		nheads = head + 1;
	}
	if (cyl >= ncyls) {
		// Round amount of tracks up to the next nearest standard size.
		if (ncyls < default_ncyls && cyl >= ncyls)
			ncyls = default_ncyls;
		// Try for exactly 40 or 80 track, but allow 3 tracks more.
		if (ncyls < 40 && cyl >= ncyls)
			ncyls = 40;
		if (ncyls < 43 && cyl >= ncyls)
			ncyls = 43;
		if (ncyls < 80 && cyl >= ncyls)
			ncyls = 80;
		if (ncyls < 83 && cyl >= ncyls)
			ncyls = 83;
		// If that's still not enough, just increase to new size.
		if (cyl >= ncyls)
			ncyls = cyl + 1;
	}
	if (nheads > disk->num_heads || ncyls > disk->num_cylinders) {
		if (ncyls > disk->num_cylinders) {
			// Allocate and clear new tracks
			for (unsigned s = 0; s < disk->num_heads; s++) {
				uint8_t *new_side = xrealloc(side_data[s], ncyls * tlength);
				if (!new_side)
					return NULL;
				side_data[s] = new_side;
				for (unsigned t = disk->num_cylinders; t < ncyls; t++) {
					uint8_t *dest = new_side + t * tlength;
					memset(dest, 0, tlength);
				}
			}
			disk->num_cylinders = ncyls;
		}
		if (nheads > disk->num_heads) {
			// Increase size of side_data array
			side_data = xrealloc(side_data, nheads * sizeof(*side_data));
			if (!side_data)
				return NULL;
			disk->side_data = side_data;
			// Allocate new empty side data
			for (unsigned s = disk->num_heads; s < nheads; s++) {
				side_data[s] = calloc(ncyls, tlength);
				if (!side_data[s])
					return NULL;
			}
			disk->num_heads = nheads;
		}
	}
	return side_data[head] + cyl * tlength;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Helper functions for dealing with the in-memory disk images.
 */

/* DragonDOS gets pretty good performance using 2:1 interleave, RS-DOS is a bit
 * slower and needs 3:1. */

static const uint8_t ddos_sector_interleave[18] =
	{ 0, 9, 1, 10, 2, 11, 3, 12, 4, 13, 5, 14, 6, 15, 7, 16, 8, 17 };

static const uint8_t rsdos_sector_interleave[18] =
	{ 0, 6, 12, 1, 7, 13, 2, 8, 14, 3, 9, 15, 4, 10, 16, 5, 11, 17 };

/* Oddball single-density sector interleave from Delta: */

static const uint8_t delta_sector_interleave[18] =
	{ 2, 6, 3, 7, 0, 4, 8, 1, 5, 9 };

/* Keep track of these separately for direct access to memory image: */

static unsigned mem_track_length;
static uint8_t *mem_track_base;
static unsigned mem_offset;
static _Bool mem_double_density;
static uint16_t mem_crc;

/* Write 'repeat' bytes of 'data', update CRC */

static void write_bytes(unsigned repeat, uint8_t data) {
	assert(mem_offset >= 128);
	assert(mem_offset < mem_track_length);
	unsigned nbytes = mem_double_density ? 1 : 2;
	for ( ; repeat; repeat--) {
		for (unsigned i = nbytes; i; i--) {
			mem_track_base[mem_offset++] = data;
			if (mem_offset >= mem_track_length)
				mem_offset = 128;
		}
		mem_crc = crc16_byte(mem_crc, data);
	}
}

/* Write CRC, should leave mem_crc == 0 */

static void write_crc(void) {
	uint8_t hi = mem_crc >> 8;
	uint8_t lo = mem_crc & 0xff;
	write_bytes(1, hi);
	write_bytes(1, lo);
}

/* Read a byte, update CRC */

static uint8_t read_byte(void) {
	assert(mem_offset >= 128);
	assert(mem_offset < mem_track_length);
	int nbytes = mem_double_density ? 1 : 2;
	uint8_t data = mem_track_base[mem_offset];
	mem_crc = crc16_byte(mem_crc, data);
	for (int i = 0; i < nbytes; i++) {
		mem_offset++;
		if (mem_offset >= mem_track_length)
			mem_offset = 128;
	}
	return data;
}

/* Read CRC bytes and return 1 if valid */

static _Bool read_crc(void) {
	(void)read_byte();
	(void)read_byte();
	return mem_crc == 0;
}

int vdisk_format_track(struct vdisk *disk, _Bool double_density,
		       unsigned cyl, unsigned head,
		       unsigned nsectors, unsigned first_sector, unsigned ssize_code) {
	if (!disk || cyl > 255 || nsectors > 64 || ssize_code > 3)
		return -1;

	uint16_t *idams = vdisk_extend_disk(disk, cyl, head);
	unsigned idam = 0;
	unsigned ssize = 128 << ssize_code;

	mem_track_length = disk->track_length;
	mem_track_base = (uint8_t *)idams;
	mem_offset = 128;
	mem_double_density = double_density;

	const uint8_t *sect_interleave = NULL;
	if (double_density && nsectors == 18) {
		if (default_dragon_interleave)
			sect_interleave = ddos_sector_interleave;
		else
			sect_interleave = rsdos_sector_interleave;
	} else if (!double_density && nsectors == 10) {
		sect_interleave = delta_sector_interleave;
	}

	if (!double_density) {

		/* Single density */
		write_bytes(20, 0xff);
		for (unsigned sector = 0; sector < nsectors; sector++) {
			uint8_t sect = sect_interleave ? sect_interleave[sector] : sector;
			write_bytes(6, 0x00);
			mem_crc = CRC16_RESET;
			idams[idam++] = mem_offset | VDISK_SINGLE_DENSITY;
			write_bytes(1, 0xfe);
			write_bytes(1, cyl);
			write_bytes(1, head);
			write_bytes(1, sect + first_sector);
			write_bytes(1, ssize_code);
			write_crc();
			write_bytes(11, 0xff);
			write_bytes(6, 0x00);
			mem_crc = CRC16_RESET;
			write_bytes(1, 0xfb);
			write_bytes(ssize, 0xe5);
			write_crc();
			write_bytes(12, 0xff);
		}
		/* fill to end of disk */
		while (mem_offset != 128) {
			write_bytes(1, 0xff);
		}

	} else {

		/* Double density */
		write_bytes(54, 0x4e);
		write_bytes(9, 0x00);
		write_bytes(3, 0xc2);
		write_bytes(1, 0xfc);
		write_bytes(32, 0x4e);
		for (unsigned sector = 0; sector < nsectors; sector++) {
			uint8_t sect = sect_interleave ? sect_interleave[sector] : sector;
			write_bytes(8, 0x00);
			mem_crc = CRC16_RESET;
			write_bytes(3, 0xa1);
			idams[idam++] = mem_offset | VDISK_DOUBLE_DENSITY;
			write_bytes(1, 0xfe);
			write_bytes(1, cyl);
			write_bytes(1, head);
			write_bytes(1, sect + first_sector);
			write_bytes(1, ssize_code);
			write_crc();
			write_bytes(22, 0x4e);
			write_bytes(12, 0x00);
			mem_crc = CRC16_RESET;
			write_bytes(3, 0xa1);
			write_bytes(1, 0xfb);
			write_bytes(ssize, 0xe5);
			write_crc();
			write_bytes(24, 0x4e);
		}
		/* fill to end of disk */
		while (mem_offset != 128) {
			write_bytes(1, 0x4e);
		}

	}
	return 0;
}

int vdisk_format_disk(struct vdisk *disk, _Bool double_density,
		      unsigned nsectors, unsigned first_sector, unsigned ssize_code) {
	if (disk == NULL) return 0;
	for (unsigned cyl = 0; cyl < disk->num_cylinders; cyl++) {
		for (unsigned head = 0; head < disk->num_heads; head++) {
			int ret = vdisk_format_track(disk, double_density, cyl, head, nsectors, first_sector, ssize_code);
			if (ret != 0)
				return ret;
		}
	}
	return 0;
}

/*
 * Locate a sector on the disk by scanning the IDAM table, and update its data
 * from that provided.
 */

int vdisk_update_sector(struct vdisk *disk, unsigned cyl, unsigned head,
			unsigned sector, unsigned sector_length, uint8_t *buf) {
	uint16_t *idams;
	unsigned ssize, i;

	if (disk == NULL)
		return -1;
	idams = vdisk_extend_disk(disk, cyl, head);
	if (idams == NULL)
		return -1;
	mem_track_length = disk->track_length;
	mem_track_base = (uint8_t *)idams;
	for (i = 0; i < 64; i++) {
		mem_offset = idams[i] & 0x3fff;
		mem_double_density = idams[i] & VDISK_DOUBLE_DENSITY;
		mem_crc = CRC16_RESET;
		if (mem_double_density) {
			for (unsigned j = 3; j; j--)
				mem_crc = crc16_byte(mem_crc, 0xa1);
		}
		(void)read_byte();
		if (read_byte() == cyl && read_byte() == head && read_byte() == sector) {
			break;
		}
	}
	if (i >= 64)
		return -1;

	unsigned ssize_code = read_byte();
	if (ssize_code > 3)
		return -1;
	ssize = 128 << ssize_code;

	(void)read_crc();  // discard CRC result for now

	if (mem_double_density) {
		for (i = 0; i < 22; i++)
			(void)read_byte();
		write_bytes(12, 0x00);
		mem_crc = CRC16_RESET;
		write_bytes(3, 0xa1);
	} else {
		for (i = 0; i < 11; i++)
			(void)read_byte();
		write_bytes(6, 0x00);
		mem_crc = CRC16_RESET;
	}

	write_bytes(1, 0xfb);
	for (i = 0; i < sector_length; i++) {
		if (i < ssize)
			write_bytes(1, buf[i]);
	}
	// On-disk sector size may differ from provided sector_length
	for ( ; i < ssize; i++)
		write_bytes(1, 0x00);
	write_crc();
	write_bytes(1, 0xfe);

	return 0;
}

/*
 * Similarly, locate a sector and copy out its data.
 */

int vdisk_fetch_sector(struct vdisk const *disk, unsigned cyl, unsigned head,
		       unsigned sector, unsigned sector_length, uint8_t *buf) {
	uint16_t *idams;
	unsigned ssize, i;
	if (!disk)
		return -1;
	idams = vdisk_track_base(disk, cyl, head);
	if (!idams)
		return -1;
	mem_track_length = disk->track_length;
	mem_track_base = (uint8_t *)idams;
	for (i = 0; i < 64; i++) {
		mem_offset = idams[i] & 0x3fff;
		mem_double_density = idams[i] & VDISK_DOUBLE_DENSITY;
		mem_crc = CRC16_RESET;
		if (mem_double_density) {
			for (unsigned j = 3; j; j--)
				mem_crc = crc16_byte(mem_crc, 0xa1);
		}
		(void)read_byte();
		if (read_byte() == cyl && read_byte() == head && read_byte() == sector)
			break;
	}
	if (i >= 64) {
		memset(buf, 0, sector_length);
		return -1;
	}

	unsigned ssize_code = read_byte();
	if (ssize_code > 3)
		return -1;
	ssize = 128 << ssize_code;
	if (ssize > sector_length)
		ssize = sector_length;

	(void)read_crc();  // discard CRC result for now

	for (i = 0; i < 43; i++) {
		if (read_byte() == 0xfb)
			break;
	}
	if (i >= 43)
		return -1;
	for (i = 0; i < ssize; i++) {
		buf[i] = read_byte();
	}
	(void)read_crc();  // discard CRC result for now

	return 0;
}
