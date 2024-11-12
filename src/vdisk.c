/** \file
 *
 *  \brief Virtual floppy disks.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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

/*
 * To avoid confusion, the position of the heads is referred to as the
 * 'cylinder' (often abbreviated to 'cyl').  The term 'track' refers only to
 * the data addressable within one cylinder by one head.  A 'side' is the
 * collection of all the tracks addressable by one head.
 */

#include "top-config.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "sds.h"
#include "xalloc.h"

#include "array.h"
#include "crc16.h"
#include "fs.h"
#include "logging.h"
#include "vdisk.h"
#include "xroar.h"

#define MAX_CYLINDERS (256)
#define MAX_HEADS (2)

static enum vdisk_err vdisk_errno;

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

// Configured interleave for single and double density
static int interleave_sd = 1;
static int interleave_dd = 1;

static const char *vdisk_errlist[] = {
	"No error",
	"Internal error",
	"Bad geometry",
	"Too many sectors",
	"Track missing",
	"Sector not found",
	"IDAM not found",
	"DAM not found",
	"Bad sector size",
	"Bad IDAM",
	"Bad sector size code",
	"IDAM CRC error",
	"Data CRC error",
	"Unknown error"
};

static const uint8_t vdk_header[12] = {
	0x64, 0x6b,  // 'dk' magic
	0x00, 0x00,  // header length, populated later
	0x10,  // VDK version
	0x10,  // VDK backwards compatibility version
	0x58,  // file source - 'X' for XRoar
	0x00,  // version of file source
	0x00,  // number of cylinders, populated later
	0x00,  // number of heads, populated later
	0x00,  // flags
	0x00,  // name length & compression flag, populated later
};

const char *vdisk_strerror(int errnum) {
	if (errnum < 0 || errnum > vdisk_err_max)
		errnum = vdisk_err_max;
	return vdisk_errlist[errnum];
}

void vdisk_set_interleave(int density, int interleave) {
	if (density == VDISK_SINGLE_DENSITY) {
		interleave_sd = interleave;
	} else {
		interleave_dd = interleave;
	}
}

struct vdisk *vdisk_new(unsigned track_length) {
	// sensible limits
	if (track_length < 0x0cc0)
		track_length = 0x0cc0;  // SD 300RPM
	if (track_length > 0x2940)
		track_length = 0x2940;  // HD 360RPM

	struct vdisk *disk = xmalloc(sizeof(*disk));
	*disk = (struct vdisk){0};
	disk->side_data = xmalloc(MAX_HEADS * sizeof(*disk->side_data));
	disk->ref_count = 1;
	for (unsigned i = 0; i < MAX_HEADS; i++)
		disk->side_data[i] = NULL;
	disk->filetype = FILETYPE_DMK;
	disk->write_back = xroar.cfg.disk.write_back;
	disk->track_length = track_length;
	return disk;
}

struct vdisk *vdisk_ref(struct vdisk *disk) {
	assert(disk != NULL);
	disk->ref_count++;
	return disk;
}

void vdisk_unref(struct vdisk *disk) {
	assert(disk != NULL);
	assert(disk->ref_count > 0);
	disk->ref_count--;
	if (disk->ref_count > 0)
		return;
	if (disk->filename)
		free(disk->filename);
	if (disk->fmt.vdk.extra)
		free(disk->fmt.vdk.extra);
	for (unsigned i = 0; i < MAX_HEADS; i++) {
		if (disk->side_data[i])
			free(disk->side_data[i]);
	}
	free(disk->side_data);
	free(disk);
}

// Call a particular loader according to type of image.
//
// As loaders will tend to use functions that modify the in-RAM image and flag
// it as dirty, they should clear the dirty flag before returning, but it's
// done here anyway.

struct vdisk *vdisk_load(const char *filename) {
	if (filename == NULL) return NULL;
	enum xroar_filetype filetype = xroar_filetype_by_ext(filename);
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(dispatch); i++) {
		if (dispatch[i].filetype == filetype) {
			struct vdisk *d = dispatch[i].load_func(filename);
			if (d)
				d->dirty = 0;
			return d;
		}
	}
	LOG_MOD_WARN("vdisk", "%s: no virtual disk reader for file type\n", filename);
	return NULL;
}

// Call a saver based on type of image.
//
// Similarly, this should clear the dirty flag, as RAM should now reflect the
// state of the image.

int vdisk_save(struct vdisk *disk) {
	if (!disk) {
		return -1;
	}
	if (!disk->filename) {
		LOG_MOD_WARN("vdisk", "attempting to save with no filename\n");
		return -1;
	}
	if (!disk->dirty) {
		LOG_MOD_DEBUG(3, "vdisk", "%s: unmodified; not saving\n", disk->filename);
		// Disk not modified, so success:
		return 0;
	}
	if (!disk->write_back) {
		LOG_MOD_DEBUG(1, "vdisk", "%s: write-back disabled; not saving\n", disk->filename);
		// This is the requested behaviour, so success:
		return 0;
	}

	int i;
	for (i = 0; dispatch[i].filetype >= 0 && dispatch[i].filetype != disk->filetype; ++i)
		;
	if (dispatch[i].save_func == NULL) {
		LOG_MOD_WARN("vdisk", "%s: no virtual disk writer for file type\n", disk->filename);
		return -1;
	}
	if (!disk->new_disk) {
		sds backup_filename = sdsnew(disk->filename);
		backup_filename = sdscat(backup_filename, ".bak");
		struct stat statbuf;
		if (stat(backup_filename, &statbuf) != 0) {
			rename(disk->filename, backup_filename);
		}
		sdsfree(backup_filename);
	}
	int r = dispatch[i].save_func(disk);
	if (r == 0)
		disk->dirty = 0;
	return r;
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

	if (stat(filename, &statbuf) != 0) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: %s\n", filename, strerror(errno));
		return NULL;
	}
	file_size = statbuf.st_size;
	if (!(fd = fopen(filename, "rb"))) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: %s\n", filename, strerror(errno));
		return NULL;
	}
	if (fread(buf, 12, 1, fd) < 1) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: failed to read header\n", filename);
		fclose(fd);
		return NULL;
	}
	file_size -= 12;
	(void)file_size;  // TODO: check this matches what's going to be read
	if (buf[0] != 'd' || buf[1] != 'k') {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: bad header\n", filename);
		fclose(fd);
		return NULL;
	}
	if (buf[5] > 0x10) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: VDK version 0x%02x not supported\n", filename, buf[5]);
		fclose(fd);
		return NULL;
	}
	if ((buf[11] & 7) != 0) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: compressed VDK not supported\n", filename);
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
		vdk_extra = xmalloc(header_size);
		if (fread(vdk_extra, header_size, 1, fd) < 1) {
			LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: read failed (header)\n", filename);
			free(vdk_extra);
			fclose(fd);
			return NULL;
		}
	}
	ssize = 128 << ssize_code;
	disk = vdisk_new(VDISK_TRACK_LENGTH_DD300);
	disk->filetype = FILETYPE_VDK;
	disk->filename = xstrdup(filename);
	disk->write_protect = write_protect;
	disk->fmt.vdk.extra_length = header_size;
	disk->fmt.vdk.filename_length = vdk_filename_length;
	disk->fmt.vdk.extra = vdk_extra;

	struct vdisk_ctx *ctx = vdisk_ctx_new(disk);

	if (!vdisk_format_disk(ctx, 1, ncyls, nheads, nsectors, 1, ssize_code)) {
		fclose(fd);
		vdisk_ctx_free(ctx);
		vdisk_unref(disk);
		return NULL;
	}
	LOG_MOD_SUB_DEBUG(1, "vdisk", "vdk", "%s: reading: %uC %uH %uS (%u-byte)\n", filename, ncyls, nheads, nsectors, ssize);
	for (unsigned cyl = 0; cyl < ncyls; cyl++) {
		for (unsigned head = 0; head < nheads; head++) {
			for (unsigned sector = 0; sector < nsectors; sector++) {
				if (fread(buf, ssize, 1, fd) < 1) {
					memset(buf, 0, ssize);
				}
				if (!vdisk_write_sector(ctx, cyl, head, sector + 1, ssize, buf)) {
					LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: vdisk error: C%u H%u S%u: %s\n", filename, cyl, head, sector+1, vdisk_strerror(vdisk_errno));
					fclose(fd);
					vdisk_ctx_free(ctx);
					vdisk_unref(disk);
					return NULL;
				}
			}
		}
	}
	fclose(fd);
	vdisk_ctx_free(ctx);
	disk->dirty = 0;
	return disk;
}

// Round disk size in cylinders up to the next "standard" size.

static unsigned standard_disk_size(unsigned ncyls) {
	if (ncyls <= 35)
		return 35;  // RS-DOS
	if (ncyls <= 36)
		return 36;  // RS-DOS with boot track
	if (ncyls <= 40)
		return 40;  // 40-track disk
	if (ncyls <= 43)
		return 43;  // 40-track disk with extra sectors
	if (ncyls <= 80)
		return 80;  // 80-track disk
	if (ncyls <= 83)
		return 83;  // 80-track disk with extra sectors
	// otherwise just go with what we're given
	return ncyls;
}

static int vdisk_save_vdk(struct vdisk *disk) {
	uint8_t buf[256];
	FILE *fd;
	if (disk == NULL) {
		return -1;
	}
	if (!(fd = fopen(disk->filename, "wb")))
		return -1;
	struct vdisk_ctx *ctx = vdisk_ctx_new(disk);

	// scan disk geometry
	struct vdisk_info vinfo;
	if (!vdisk_get_info(ctx, &vinfo)) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: internal geometry error: %s\n", disk->filename, vdisk_strerror(vdisk_errno));
		fclose(fd);
		vdisk_ctx_free(ctx);
		return -1;
	}
	if (vinfo.density == vdisk_density_mixed) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: not writing: mixed density unsupported\n", disk->filename);
		fclose(fd);
		vdisk_ctx_free(ctx);
		return -1;
	}
	if (vinfo.ssize_code != 1) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: not writing: %u byte sectors unsupported\n", disk->filename, 128U << vinfo.ssize_code);
		fclose(fd);
		vdisk_ctx_free(ctx);
		return -1;
	}
	if (vinfo.num_sectors != 18) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: not writing: %u sector tracks unsupported\n", disk->filename, vinfo.num_sectors);
		fclose(fd);
		vdisk_ctx_free(ctx);
		return -1;
	}

	unsigned ssize = 128 << vinfo.ssize_code;

	LOG_MOD_SUB_DEBUG(1, "vdisk", "vdk", "%s: writing: %uC %uH %uS (%u-byte)\n", disk->filename, vinfo.num_cylinders, vinfo.num_heads, vinfo.num_sectors, ssize);
	if (vinfo.first_sector_id != 1) {
		LOG_MOD_SUB_WARN("vdisk", "vdk", "%s: first sector id of %u may render image unreadable\n", disk->filename, vinfo.first_sector_id);
	}

	uint16_t header_length = 12;
	if (disk->fmt.vdk.extra_length > 0)
		header_length += disk->fmt.vdk.extra_length;
	memcpy(buf, vdk_header, sizeof(vdk_header));
	buf[2] = header_length & 0xff;
	buf[3] = header_length >> 8;
	buf[8] = vinfo.num_cylinders;
	buf[9] = vinfo.num_heads;
	buf[11] = disk->fmt.vdk.filename_length << 3;  // name length & compression flag
	fwrite(buf, 12, 1, fd);
	if (disk->fmt.vdk.extra_length > 0) {
		fwrite(disk->fmt.vdk.extra, disk->fmt.vdk.extra_length, 1, fd);
	}

	unsigned ncyls = standard_disk_size(vinfo.num_cylinders);
	for (unsigned cyl = 0; cyl < ncyls; cyl++) {
		for (unsigned head = 0; head < vinfo.num_heads; head++) {
			for (unsigned sector = 0; sector < 18; sector++) {
				vdisk_read_sector(ctx, cyl, head, sector + 1, 256, buf);
				fwrite(buf, 256, 1, fd);
			}
		}
	}
	vdisk_ctx_free(ctx);
	fclose(fd);
	disk->dirty = 0;
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
			LOG_MOD_SUB_WARN("vdisk", "jvc", "%s: failed to read header\n", filename);
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
			LOG_MOD_SUB_WARN("vdisk", "jvc", "%s: read failed (sector)\n", filename);
			fclose(fd);
			return NULL;
		}
		unsigned dd_tot = (buf[0x100+0] << 16) | (buf[0x100+1] << 8) | buf[0x100+2];
		off_t os9_file_size = dd_tot * 256;
		unsigned dd_tks = buf[0x100+0x03];
		uint8_t dd_fmt = buf[0x100+0x10];
		unsigned dd_fmt_sides = (dd_fmt & 1) + 1;
		unsigned dd_spt = (buf[0x100+0x11] << 8) | buf[0x100+0x12];

		if (os9_file_size >= file_size && dd_tks == dd_spt && dd_tks > 0) {
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
		LOG_MOD_SUB_WARN("vdisk", "jvc", "%s: bad header\n", filename);
		fclose(fd);
		return NULL;
	}
	unsigned ncyls = file_size / bytes_per_cyl;
	// Too many tracks is implausible, so assume this (single-sided) means
	// a 720K disk.
	if (ncyls >= 88 && nheads == 1) {
		nheads++;
		bytes_per_cyl = nsectors * bytes_per_sector * nheads;
		ncyls = file_size / bytes_per_cyl;
	}
	// if there is at least one more sector of data, allow an extra track
	if ((file_size % bytes_per_cyl) >= bytes_per_sector) {
		ncyls++;
	}
	if (xroar.cfg.disk.auto_sd && nsectors == 10)
		double_density = 0;

	struct vdisk *disk = vdisk_new(VDISK_TRACK_LENGTH_DD300);
	disk->filetype = FILETYPE_JVC;
	disk->filename = xstrdup(filename);
	disk->fmt.jvc.headerless_os9 = headerless_os9;
	struct vdisk_ctx *ctx = vdisk_ctx_new(disk);
	if (!vdisk_format_disk(ctx, double_density, ncyls, nheads, nsectors, first_sector, ssize_code)) {
		fclose(fd);
		vdisk_ctx_free(ctx);
		vdisk_unref(disk);
		return NULL;
	}
	if (headerless_os9) {
		LOG_MOD_SUB_DEBUG(1, "vdisk", "jvc", "%s: reading headerless OS-9 image: %uC %uH %uS (%u-byte)\n", filename, ncyls, nheads, nsectors, ssize);
	} else {
		LOG_MOD_SUB_DEBUG(1, "vdisk", "jvc", "%s: reading: %uC %uH %uS (%u-byte)\n", filename, ncyls, nheads, nsectors, ssize);
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
				if (!vdisk_write_sector(ctx, cyl, head, sector + first_sector, ssize, buf)) {
					LOG_MOD_SUB_WARN("vdisk", "jvc", "%s: vdisk error: C%u H%u S%u: %s\n", filename, cyl, head, sector+1, vdisk_strerror(vdisk_errno));
					fclose(fd);
					vdisk_ctx_free(ctx);
					vdisk_unref(disk);
					return NULL;
				}
			}
		}
	}
	fclose(fd);
	vdisk_ctx_free(ctx);
	disk->dirty = 0;
	return disk;
}

static struct vdisk *vdisk_load_jvc(const char *filename) {
	return do_load_jvc(filename, xroar.cfg.disk.auto_os9);
}

static struct vdisk *vdisk_load_os9(const char *filename) {
	return do_load_jvc(filename, 1);
}

static int vdisk_save_jvc(struct vdisk *disk) {
	uint8_t buf[1024];
	FILE *fd;
	if (!disk) {
		return -1;
	}
	if (!(fd = fopen(disk->filename, "wb")))
		return -1;
	struct vdisk_ctx *ctx = vdisk_ctx_new(disk);

	// scan disk geometry
	struct vdisk_info vinfo;
	if (!vdisk_get_info(ctx, &vinfo)) {
		LOG_MOD_SUB_WARN("vdisk", "jvc", "%s: internal geometry error: %s\n", disk->filename, vdisk_strerror(vdisk_errno));
		fclose(fd);
		vdisk_ctx_free(ctx);
		return -1;
	}
	if (vinfo.density == vdisk_density_mixed) {
		LOG_MOD_SUB_WARN("vdisk", "jvc", "%s: not writing: mixed density unsupported\n", disk->filename);
		fclose(fd);
		vdisk_ctx_free(ctx);
		return -1;
	}
	if (vinfo.ssize_code == -1) {
		LOG_MOD_SUB_WARN("vdisk", "jvc", "%s: not writing: mixed sector size unsupported\n", disk->filename);
		fclose(fd);
		vdisk_ctx_free(ctx);
		return -1;
	}

	// populate geometry information
	buf[0] = vinfo.num_sectors;
	buf[1] = vinfo.num_heads;
	buf[2] = vinfo.ssize_code;
	buf[3] = vinfo.first_sector_id;
	buf[4] = 0;  // sector attribute flag currently unused
	unsigned ssize = 128 << vinfo.ssize_code;

	LOG_MOD_SUB_DEBUG(1, "vdisk", "jvc", "%s: writing: %uC %uH %uS (%u-byte)\n", disk->filename, vinfo.num_cylinders, vinfo.num_heads, vinfo.num_sectors, ssize);

	// don't write a header if OS-9 detection didn't find one
	unsigned header_size = 0;
	if (vinfo.num_sectors != 18)
		header_size = 1;
	if (vinfo.num_heads != 1)
		header_size = 2;
	if (vinfo.ssize_code != 1)
		header_size = 3;
	if (vinfo.first_sector_id != 1)
		header_size = 4;

	if (disk->fmt.jvc.headerless_os9)
		header_size = 0;

	if (header_size > 0) {
		fwrite(buf, header_size, 1, fd);
	}

	unsigned ncyls = standard_disk_size(vinfo.num_cylinders);
	for (unsigned cyl = 0; cyl < ncyls; cyl++) {
		for (unsigned head = 0; head < vinfo.num_heads; head++) {
			for (unsigned sector = 0; sector < vinfo.num_sectors; sector++) {
				vdisk_read_sector(ctx, cyl, head, sector + vinfo.first_sector_id, ssize, buf);
				fwrite(buf, ssize, 1, fd);
			}
		}
	}
	fclose(fd);
	vdisk_ctx_free(ctx);
	disk->dirty = 0;
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
		LOG_MOD_SUB_WARN("vdisk", "dmk", "%s: failed to read header\n", filename);
		fclose(fd);
		return NULL;
	}
	ncyls = header[1];
	track_length = (header[3] << 8) | header[2];  // yes, little-endian!
	if (track_length < 0x0cc0 || track_length > 0x2940) {
		LOG_MOD_SUB_WARN("vdisk", "dmk", "%s: invalid track length %u\n", filename, track_length);
		fclose(fd);
		return NULL;
	}
	nheads = (header[4] & 0x10) ? 1 : 2;
	const char *density_message = "";
	if (header[4] & 0x40) {
		density_message = " (single-density)";
	}
	if (header[4] & 0x80) {
		density_message = " (mixed-density)";
	}
	file_size -= 16;
	(void)file_size;  // TODO: check this matches what's going to be read
	disk = vdisk_new(VDISK_TRACK_LENGTH_DD300);
	LOG_MOD_SUB_DEBUG(1, "vdisk", "dmk", "%s: reading: %uC %uH (%u-byte)%s\n", filename, ncyls, nheads, track_length, density_message);
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
	disk->dirty = 0;
	return disk;
}

static int vdisk_save_dmk(struct vdisk *disk) {
	uint8_t header[16];
	FILE *fd;
	if (!disk) {
		return -1;
	}
	if (!(fd = fopen(disk->filename, "wb"))) {
		return -1;
	}
	LOG_MOD_SUB_DEBUG(1, "vdisk", "dmk", "%s: writing: %uC %uH (%u-byte)\n", disk->filename, disk->num_cylinders, disk->num_heads, disk->track_length);
	memset(header, 0, sizeof(header));
	if (!disk->write_back) {
		header[0] = 0xff;
	}
	header[1] = disk->num_cylinders;
	header[2] = disk->track_length & 0xff;
	header[3] = (disk->track_length >> 8) & 0xff;
	if (disk->num_heads == 1) {
		header[4] |= 0x10;
	}
	header[11] = disk->write_protect ? 0xff : 0;
	fwrite(header, 16, 1, fd);
	for (unsigned cyl = 0; cyl < disk->num_cylinders; cyl++) {
		for (unsigned head = 0; head < disk->num_heads; head++) {
			uint16_t *idams = vdisk_track_base(disk, cyl, head);
			if (idams == NULL) continue;
			uint8_t *buf = (uint8_t *)idams + 128;
			int i;
			for (i = 0; i < 64; i++) {
				fs_write_uint16_le(fd, idams[i]);
			}
			fwrite(buf, disk->track_length - 128, 1, fd);
		}
	}
	fclose(fd);
	disk->dirty = 0;
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Helper functions for dealing with the in-memory disk images.
 */

struct vdisk_ctx *vdisk_ctx_new(struct vdisk *disk) {
	struct vdisk_ctx *ctx = xmalloc(sizeof(*ctx));
	*ctx = (struct vdisk_ctx){0};
	ctx->disk = vdisk_ref(disk);
	return ctx;
}

void vdisk_ctx_free(struct vdisk_ctx *ctx) {
	vdisk_unref(ctx->disk);
	free(ctx);
}

static _Bool vdisk_ctx_check(struct vdisk_ctx *ctx) {
	if (!ctx) {
		vdisk_errno = vdisk_err_internal;
		return 0;
	}
	if (!ctx->disk) {
		vdisk_errno = vdisk_err_internal;
		return 0;
	}
	return 1;
}

static _Bool vdisk_ctx_check_geometry(unsigned cyl, unsigned head) {
	if (cyl >= MAX_CYLINDERS || head >= MAX_HEADS) {
		vdisk_errno = vdisk_err_bad_geometry;
		return 0;
	}
	return 1;
}

_Bool vdisk_ctx_seek(struct vdisk_ctx *ctx, _Bool extend, unsigned cyl, unsigned head) {
	if (!vdisk_ctx_check(ctx))
		return 0;
	if (!vdisk_ctx_check_geometry(cyl, head))
		return 0;
	ctx->cyl = cyl;
	ctx->head = head;
	void *track_data = NULL;
	if (extend) {
		track_data = vdisk_extend_disk(ctx->disk, cyl, head);
	} else {
		track_data = vdisk_track_base(ctx->disk, cyl, head);
	}
	ctx->idam_data = track_data;
	ctx->track_data = track_data;
	ctx->head_pos = 128;
	return (track_data != NULL);
}

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
	assert(disk != NULL);
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
		ncyls = cyl + 1;
	}
	if (nheads > disk->num_heads || ncyls > disk->num_cylinders) {
		disk->dirty = 1;
		if (ncyls > disk->num_cylinders) {
			// Allocate and clear new tracks
			for (unsigned s = 0; s < disk->num_heads; s++) {
				uint8_t *new_side = xrealloc(side_data[s], ncyls * tlength);
				side_data[s] = new_side;
				for (unsigned t = disk->num_cylinders; t < ncyls; t++) {
					uint8_t *dest = new_side + t * tlength;
					memset(dest, 0, tlength);
				}
			}
			disk->num_cylinders = ncyls;
		}
		if (nheads > disk->num_heads) {
			// Allocate new empty side data
			for (unsigned s = disk->num_heads; s < nheads; s++) {
				side_data[s] = xzalloc(ncyls * tlength);
			}
			disk->num_heads = nheads;
		}
	}
	return side_data[head] + cyl * tlength;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Write 'repeat' bytes of 'data', update CRC */

static void write_bytes(struct vdisk_ctx *ctx, unsigned repeat, uint8_t data) {
	assert(ctx->head_pos >= 128);
	assert(ctx->head_pos < ctx->disk->track_length);
	struct vdisk *disk = ctx->disk;
	unsigned nbytes = ctx->dden ? 1 : 2;
	disk->dirty = 1;
	for ( ; repeat; repeat--) {
		for (unsigned i = nbytes; i; i--) {
			ctx->track_data[ctx->head_pos++] = data;
			if (ctx->head_pos >= ctx->disk->track_length)
				ctx->head_pos = 128;
		}
		ctx->crc = crc16_ccitt_byte(ctx->crc, data);
	}
}

/* Write CRC, should leave ctx->crc == 0 */

static void write_crc(struct vdisk_ctx *ctx) {
	uint8_t hi = ctx->crc >> 8;
	uint8_t lo = ctx->crc & 0xff;
	write_bytes(ctx, 1, hi);
	write_bytes(ctx, 1, lo);
}

/* Read a byte, update CRC */

static uint8_t read_byte(struct vdisk_ctx *ctx) {
	if (!ctx->track_data)
		return 0;
	assert(ctx->head_pos >= 128);
	assert(ctx->head_pos < ctx->disk->track_length);
	int nbytes = ctx->dden ? 1 : 2;
	uint8_t data = ctx->track_data[ctx->head_pos];
	ctx->crc = crc16_ccitt_byte(ctx->crc, data);
	for (int i = 0; i < nbytes; i++) {
		ctx->head_pos++;
		if (ctx->head_pos >= ctx->disk->track_length)
			ctx->head_pos = 128;
	}
	return data;
}

/* Read CRC bytes and return 1 if valid */

static _Bool read_crc(struct vdisk_ctx *ctx) {
	(void)read_byte(ctx);
	(void)read_byte(ctx);
	return ctx->crc == 0;
}

_Bool vdisk_format_track(struct vdisk_ctx *ctx, _Bool dden,
			 unsigned cyl, unsigned head,
			 unsigned nsectors, unsigned first_sector, unsigned ssize_code) {

	if (!vdisk_ctx_seek(ctx, 1, cyl, head))
		return 0;

	if (cyl > 255 || nsectors > 64 || ssize_code > 3) {
		vdisk_errno = vdisk_err_bad_geometry;
		return 0;
	}

	struct vdisk *disk = ctx->disk;

	vdisk_ctx_seek(ctx, 1, cyl, head);
	unsigned idam = 0;
	unsigned ssize = 128 << ssize_code;

	ctx->head_pos = 128;
	ctx->dden = dden;

	int interleave = dden ? interleave_dd : interleave_sd;
	int *sector_id = xmalloc(nsectors * sizeof(*sector_id));
	int idx = -interleave;
	for (unsigned i = 0; i < nsectors; i++)
		sector_id[i] = -1;
	for (unsigned i = 0; i < nsectors; i++) {
		idx = (idx + interleave) % nsectors;
		while (sector_id[idx] >= 0)
			idx = (idx + 1) % nsectors;
		sector_id[idx] = i;
	}

	if (!dden) {

		/* Single density */
		write_bytes(ctx, 20, 0xff);
		for (unsigned sector = 0; sector < nsectors; sector++) {
			int sect = sector_id[sector];
			write_bytes(ctx, 6, 0x00);
			ctx->crc = CRC16_CCITT_RESET;
			ctx->idam_data[idam++] = ctx->head_pos | VDISK_SINGLE_DENSITY;
			write_bytes(ctx, 1, 0xfe);
			write_bytes(ctx, 1, cyl);
			write_bytes(ctx, 1, head);
			write_bytes(ctx, 1, sect + first_sector);
			write_bytes(ctx, 1, ssize_code);
			write_crc(ctx);
			write_bytes(ctx, 11, 0xff);
			write_bytes(ctx, 6, 0x00);
			ctx->crc = CRC16_CCITT_RESET;
			write_bytes(ctx, 1, 0xfb);
			write_bytes(ctx, ssize, 0xe5);
			write_crc(ctx);
			write_bytes(ctx, 12, 0xff);
		}
		/* fill to end of disk */
		while (ctx->head_pos != 128) {
			write_bytes(ctx, 1, 0xff);
		}

	} else {

		// gap heuristics based on example 18 and 20 sector formats
		int gap = disk->track_length - ((ssize + 58) * nsectors) - 87;
		int pigap = 8 + (gap * 46) / 584;
		int gap2 = 16 + (gap * 76) / (584 * nsectors);
		int gap3 = 1 + (gap * 412) / (584 * nsectors);

		/* Double density */
		write_bytes(ctx, pigap, 0x4e);
		write_bytes(ctx, 9, 0x00);
		write_bytes(ctx, 3, 0xc2);
		write_bytes(ctx, 1, 0xfc);
		write_bytes(ctx, 32, 0x4e);
		for (unsigned sector = 0; sector < nsectors; sector++) {
			int sect = sector_id[sector];
			write_bytes(ctx, 8, 0x00);
			ctx->crc = CRC16_CCITT_RESET;
			write_bytes(ctx, 3, 0xa1);
			ctx->idam_data[idam++] = ctx->head_pos | VDISK_DOUBLE_DENSITY;
			write_bytes(ctx, 1, 0xfe);
			write_bytes(ctx, 1, cyl);
			write_bytes(ctx, 1, head);
			write_bytes(ctx, 1, sect + first_sector);
			write_bytes(ctx, 1, ssize_code);
			write_crc(ctx);
			write_bytes(ctx, gap2, 0x4e);
			write_bytes(ctx, 12, 0x00);
			ctx->crc = CRC16_CCITT_RESET;
			write_bytes(ctx, 3, 0xa1);
			write_bytes(ctx, 1, 0xfb);
			write_bytes(ctx, ssize, 0xe5);
			write_crc(ctx);
			write_bytes(ctx, gap3, 0x4e);
		}
		/* fill to end of disk */
		while (ctx->head_pos != 128) {
			write_bytes(ctx, 1, 0x4e);
		}

	}
	free(sector_id);
	return 1;
}

_Bool vdisk_format_disk(struct vdisk_ctx *ctx, _Bool dden,
			unsigned ncyls, unsigned nheads,
			unsigned nsectors, unsigned first_sector, unsigned ssize_code) {
	assert(ctx != NULL);
	assert(ctx->disk != NULL);
	for (unsigned cyl = 0; cyl < ncyls; cyl++) {
		for (unsigned head = 0; head < nheads; head++) {
			if (!vdisk_format_track(ctx, dden, cyl, head, nsectors, first_sector, ssize_code)) {
				LOG_MOD_WARN("vdisk", "track format failed: %s\n", vdisk_strerror(vdisk_errno));
				return 0;
			}
		}
	}
	return 1;
}

/*
 * Locate a sector on the disk by scanning the IDAM table, and update its data
 * from that provided.
 */

_Bool vdisk_write_sector(struct vdisk_ctx *ctx, unsigned cyl, unsigned head,
			 unsigned sector, unsigned ssize, uint8_t *buf) {

	struct vdisk_idam vidam;

	for (unsigned i = 0; i < 64; i++) {
		if (!vdisk_read_idam(ctx, &vidam, cyl, head, i))
			return 0;
		if (!vidam.valid)
			continue;

		// not currently testing that stored track number matches
		if (vidam.sector == sector) {
			ctx->idam_crc_error = (vidam.crc != 0);
			if (ctx->dden) {
				for (unsigned j = 0; j < 22; j++)
					(void)read_byte(ctx);
				write_bytes(ctx, 12, 0);
				ctx->crc = CRC16_CCITT_RESET;
				write_bytes(ctx, 3, 0xa1);
			} else {
				for (unsigned j = 0; j < 11; j++)
					(void)read_byte(ctx);
				write_bytes(ctx, 6, 0);
				ctx->crc = CRC16_CCITT_RESET;
			}
			write_bytes(ctx, 1, 0xfb);
			unsigned vseclen = 128 << vidam.ssize_code;
			unsigned j = 0;
			while (j < ssize && j < vseclen)
				write_bytes(ctx, 1, buf[j++]);
			while (j < vseclen)
				write_bytes(ctx, 1, 0);
			write_crc(ctx);
			ctx->data_crc_error = 0;
			write_bytes(ctx, 1, 0xfe);
			return 1;
		}
	}

	vdisk_errno = vdisk_err_sector_not_found;
	return 0;
}

/*
 * Similarly, locate a sector and copy out its data.
 */

_Bool vdisk_read_sector(struct vdisk_ctx *ctx, unsigned cyl, unsigned head,
			unsigned sector, unsigned ssize, uint8_t *buf) {

	struct vdisk_idam vidam;

	memset(buf, 0, ssize);
	for (unsigned i = 0; i < 64; i++) {
		if (!vdisk_read_idam(ctx, &vidam, cyl, head, i))
			return 0;
		if (!vidam.valid)
			continue;

		// not currently testing that stored track number matches
		if (vidam.sector == sector) {
			ctx->idam_crc_error = (vidam.crc != 0);
			unsigned j;
			for (j = 0; j < 43; j++) {
				if (read_byte(ctx) == 0xfb)
					break;
			}
			if (j >= 43) {
				vdisk_errno = vdisk_err_dam_not_found;
				return 0;
			}
			unsigned vseclen = 128 << vidam.ssize_code;
			j = 0;
			while (j < ssize && j < vseclen)
				buf[j++] = read_byte(ctx);
			while (j < vseclen)
				(void)read_byte(ctx);
			ctx->data_crc_error = !read_crc(ctx);
			return 1;
		}
	}

	vdisk_errno = vdisk_err_sector_not_found;
	return 0;
}

/*
 * Read numbered IDAM from specified track.  Returns true only if information
 * has been populated.  Doesn't verify data, but sets crc_error if necessary.
 */

_Bool vdisk_read_idam(struct vdisk_ctx *ctx, struct vdisk_idam *vidam,
		      unsigned cyl, unsigned head, unsigned idam) {

	if (cyl >= MAX_CYLINDERS || head >= MAX_HEADS || idam >= 64) {
		vdisk_errno = vdisk_err_internal;
		return 0;
	}

	if (!vdisk_ctx_seek(ctx, 0, cyl, head))
		return 0;

	if (ctx->idam_data[idam] == 0) {
		vidam->valid = 0;
		return 1;
	}

	unsigned head_pos = ctx->idam_data[idam] & 0x3fff;
	if (head_pos < 128 || head_pos >= ctx->disk->track_length) {
		vdisk_errno = vdisk_err_bad_idam;
		return 0;
	}
	ctx->dden = ctx->idam_data[idam] & VDISK_DOUBLE_DENSITY;
	ctx->head_pos = head_pos;

	(void)read_byte(ctx);  // skip idam byte
	ctx->crc = CRC16_CCITT_RESET;
	if (ctx->dden) {
		ctx->crc = crc16_ccitt_byte(ctx->crc, 0xa1);
		ctx->crc = crc16_ccitt_byte(ctx->crc, 0xa1);
		ctx->crc = crc16_ccitt_byte(ctx->crc, 0xa1);
	}
	vidam->valid = 1;
	vidam->cyl = read_byte(ctx);
	vidam->side = read_byte(ctx);
	vidam->sector = read_byte(ctx);
	vidam->ssize_code = read_byte(ctx);
	vidam->crc = read_byte(ctx) << 8;
	vidam->crc |= read_byte(ctx);

	return 1;
}

_Bool vdisk_get_info(struct vdisk_ctx *ctx, struct vdisk_info *vinfo) {
	if (!vdisk_ctx_check(ctx))
		return 0;
	struct vdisk *disk = ctx->disk;
	assert(vinfo != NULL);

	_Bool have_sden = 0;
	_Bool have_dden = 0;
	unsigned first_sector = 256;
	unsigned last_sector = 0;
	int ssize_code = -2;

	*vinfo = (struct vdisk_info){0};
	if (disk->num_cylinders == 0 || disk->num_heads == 0)
		return 1;

	for (unsigned c = 0; c < disk->num_cylinders; c++) {
		for (unsigned h = 0; h < disk->num_heads; h++) {
			if (!vdisk_ctx_seek(ctx, 0, c, h))
				return 0;
			for (unsigned i = 0; i < 64; i++) {
				struct vdisk_idam vidam;
				if (!vdisk_read_idam(ctx, &vidam, c, h, i))
					return 0;
				if (!vidam.valid)
					break;
				if (vidam.sector > last_sector)
					last_sector = vidam.sector;
				if (vidam.sector < first_sector)
					first_sector = vidam.sector;
				if (ctx->dden)
					have_dden = 1;
				else
					have_sden = 1;
				if (ssize_code == -2)
					ssize_code = vidam.ssize_code;
				else if (ssize_code != (int)vidam.ssize_code)
					ssize_code = -1;
			}
		}
	}

	if (last_sector <= first_sector) {
		vdisk_errno = vdisk_err_sector_not_found;
		return 0;
	}
	vinfo->num_cylinders = disk->num_cylinders;
	vinfo->num_heads = disk->num_heads;
	vinfo->num_sectors = (last_sector + 1) - first_sector;
	vinfo->first_sector_id = first_sector;
	vinfo->ssize_code = ssize_code;
	if (have_sden && have_dden) {
		vinfo->density  = vdisk_density_mixed;
	} else if (have_sden) {
		vinfo->density  = vdisk_density_single;
	} else if (have_dden) {
		vinfo->density  = vdisk_density_double;
	} else {
		vinfo->density  = vdisk_density_unknown;
	}

	return 1;
}
