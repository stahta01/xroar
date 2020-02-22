/*

Virtual floppy disks

Copyright 2003-2017 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_VDISK_H_
#define XROAR_VDISK_H_

#include <stdint.h>

#include "xroar.h"

#define VDISK_DOUBLE_DENSITY (0x8000)
#define VDISK_SINGLE_DENSITY (0x0000)

enum vdisk_err {
	vdisk_ok = 0,
	vdisk_err_internal,
	vdisk_err_bad_geometry,  // invalid cylinder or head
	vdisk_err_too_many_sectors,
	vdisk_err_track_missing,  // may be valid
	vdisk_err_sector_not_found,
	vdisk_err_idam_not_found,
	vdisk_err_dam_not_found,
	vdisk_err_bad_sector_size,
	vdisk_err_bad_idam,
	vdisk_err_bad_ssize_code,
	vdisk_err_idam_crc,
	vdisk_err_data_crc,
	vdisk_err_max
};

/*
 * If write_back is not set, the image file will not be updated when the disk
 * is ejected or flushed.
 *
 * If write_protect is set, the FDC will not be permitted to write to the disk
 * image.
 *
 * The actual track data is organised by disk side - this makes dynamically
 * expanding disks easier.
 */

struct vdisk {
	unsigned ref_count;

	enum xroar_filetype filetype;
	char *filename;
	_Bool write_back;
	_Bool write_protect;
	unsigned num_cylinders;
	unsigned num_heads;
	unsigned track_length;
	uint8_t **side_data;
	/* format specific data, kept only for use when rewriting: */
	union {
		struct {
			int extra_length;
			int filename_length;
			uint8_t *extra;
		} vdk;
		struct {
			_Bool headerless_os9;
		} jvc;
	} fmt;
};

/*
 * struct vdisk_ctx used to track various data while manipulating a disk image.
 */

struct vdisk_ctx {
	struct vdisk *disk;

	unsigned cyl;
	unsigned head;
	uint16_t *idam_data;
	uint8_t *track_data;

	_Bool dden;
	unsigned head_pos;
	unsigned crc;

	_Bool idam_crc_error;
	_Bool data_crc_error;
};

struct vdisk_idam {
	_Bool valid;
	unsigned cyl;
	unsigned side;
	unsigned sector;
	unsigned ssize_code;
	unsigned crc;
};

enum vdisk_density {
	vdisk_density_unknown,
	vdisk_density_single,
	vdisk_density_double,
	vdisk_density_mixed,
};

// vdisk_get_disk_info() populates this struct:

struct vdisk_info {
	unsigned num_cylinders;
	unsigned num_heads;
	unsigned num_sectors;
	unsigned first_sector_id;
	int ssize_code;  // 0-3, or -1 if more than one size detected
	enum vdisk_density density;
};

const char *vdisk_strerror(int errnum);

// Set interleave of subsequently formatted disks.
void vdisk_set_interleave(int density, int interleave);

struct vdisk *vdisk_new(unsigned data_rate, unsigned rpm);
struct vdisk *vdisk_ref(struct vdisk *disk);
void vdisk_unref(struct vdisk *disk);

struct vdisk *vdisk_load(const char *filename);
int vdisk_save(struct vdisk *disk, _Bool force);

/*
 * These both return a pointer to the beginning of the specified track's IDAM
 * list (followed by the track data).  vdisk_extend_disk() is called before
 * writing, as it additionally extends the disk if cyl or head exceed the
 * currently configured values.  NULL return indicates error.
 */

void *vdisk_track_base(struct vdisk const *disk, unsigned cyl, unsigned head);
void *vdisk_extend_disk(struct vdisk *disk, unsigned cyl, unsigned head);

struct vdisk_ctx *vdisk_ctx_new(struct vdisk *disk);
void vdisk_ctx_free(struct vdisk_ctx *ctx);

_Bool vdisk_ctx_seek(struct vdisk_ctx *ctx, _Bool extend, unsigned cyl, unsigned head);

_Bool vdisk_format_track(struct vdisk_ctx *ctx, _Bool dden,
			 unsigned cyl, unsigned head,
			 unsigned nsectors, unsigned first_sector, unsigned ssize_code);
_Bool vdisk_format_disk(struct vdisk_ctx *ctx, _Bool dden,
			unsigned ncyls, unsigned nheads,
			unsigned nsectors, unsigned first_sector, unsigned ssize_code);

_Bool vdisk_set_track(struct vdisk_ctx *ctx, _Bool extend, unsigned cyl, unsigned head);

_Bool vdisk_write_sector(struct vdisk_ctx *ctx, unsigned cyl, unsigned head,
			 unsigned sector, unsigned sector_length, uint8_t *buf);
_Bool vdisk_read_sector(struct vdisk_ctx *ctx, unsigned cyl, unsigned head,
			unsigned sector, unsigned sector_length, uint8_t *buf);

// Return information about a particular IDAM.

_Bool vdisk_read_idam(struct vdisk_ctx *ctx, struct vdisk_idam *vidam,
		      unsigned cyl, unsigned head, unsigned idam);

// Scan disk and return information about its structure.

_Bool vdisk_get_info(struct vdisk_ctx *ctx, struct vdisk_info *vinfo);

#endif
