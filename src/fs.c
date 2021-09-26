/** \file
 *
 *  \brief File operations.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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

#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "xalloc.h"

#include "fs.h"

// POSIX way to find file size.  errno set as appropriate.

off_t fs_file_size(FILE *fd) {
	int rfd = fileno(fd);
	if (rfd == -1)
		return -1;
	struct stat stat_buf;
	if (fstat(rfd, &stat_buf) == -1)
		return -1;
	return stat_buf.st_size;
}

// POSIX says operations on the file descriptor associated with a stream are ok
// if you fflush() first and fseek() afterwards.  Every call in this sets errno
// if necessary, so caller can check errno on failure too.

int fs_truncate(FILE *fd, off_t length) {
	int fno = fileno(fd);
	if (fno < 0)
		return -1;
	if (fflush(fd) != 0)
		return -1;
	if (ftruncate(fno, length) < 0)
		return -1;
	return fseeko(fd, length, SEEK_SET);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Writing basic integer types

int fs_write_uint8(FILE *fd, int value) {
	value &= 0xff;
	return fputc(value, fd) == value;
}

int fs_write_uint16(FILE *fd, int value) {
	uint8_t out[2];
	out[0] = value >> 8;
	out[1] = value;
	return fwrite(out, 1, 2, fd);
}

int fs_write_uint16_le(FILE *fd, int value) {
	uint8_t out[2];
	out[0] = value;
	out[1] = value >> 8;
	return fwrite(out, 1, 2, fd);
}

int fs_write_uint31(FILE *fd, int value) {
	uint8_t out[4];
	out[0] = value >> 24;
	out[1] = value >> 16;
	out[2] = value >> 8;
	out[3] = value;
	return fwrite(out, 1, 4, fd);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Reading basic integer types

int fs_read_uint8(FILE *fd) {
	return fgetc(fd);
}

int fs_read_uint16(FILE *fd) {
	uint8_t in[2];
	if (fread(in, 1, 2, fd) < 2)
		return -1;
	return (in[0] << 8) | in[1];
}

int fs_read_uint16_le(FILE *fd) {
	uint8_t in[2];
	if (fread(in, 1, 2, fd) < 2)
		return -1;
	return (in[1] << 8) | in[0];
}

int fs_read_uint31(FILE *fd) {
	uint8_t in[4];
	if (fread(in, 1, 4, fd) < 4)
		return -1;
	if (in[0] & 0x80)
		return -1;
	return (in[0] << 24) | (in[1] << 16) | (in[2] << 8) | in[3];
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Variable-length unsigned 32-bit integers

int fs_sizeof_vuint32(uint32_t value) {
	if (value <= 0x7f)
		return 1;
	if (value <= 0x3fff)
		return 2;
	if (value <= 0x1fffff)
		return 3;
	if (value <= 0xfffffff)
		return 4;
	return 5;
}

int fs_write_vuint32(FILE *fd, uint32_t value) {
	switch (fs_sizeof_vuint32(value)) {
	default:
		break;
	case 1:
		return fs_write_uint8(fd, value);
	case 2:
		return fs_write_uint16(fd, value | 0x8000);
	case 3:
		if (fs_write_uint8(fd, 0xc0 | (value >> 16)) != 1) {
			return -1;
		}
		if (fs_write_uint16(fd, value & 0xffff) != 2) {
			return -1;
		}
		return 3;
	case 4:
		if (fs_write_uint16(fd, 0xe000 | (value >> 16)) != 2) {
			return -1;
		}
		if (fs_write_uint16(fd, value & 0xffff) != 2) {
			return -1;
		}
		return 4;
	case 5:
		if (fs_write_uint8(fd, 0xf0) != 1) {
			return -1;
		}
		if (fs_write_uint16(fd, value >> 16) != 2) {
			return -1;
		}
		if (fs_write_uint16(fd, value & 0xfff) != 2) {
			return -1;
		}
		return 5;
	}
	return -1;
}

uint32_t fs_read_vuint32(FILE *fd, int *nread) {
	int byte0 = fs_read_uint8(fd);
	if (byte0 < 0) {
		if (nread)
			*nread = -1;
		return 0;
	}
	uint32_t v = byte0;
	uint32_t mask = 0x7f;
	int nbytes;
	for (nbytes = 1; nbytes < 5; nbytes++) {
		if ((byte0 & 0x80) == 0)
			break;
		byte0 <<= 1;
		int byte = fs_read_uint8(fd);
		if (byte < 0) {
			if (nread)
				*nread = -1;
			return 0;
		}
		mask = (mask << 7) | 0x7f;
		v = (v << 8) | byte;
	}
	if (nread)
		*nread = nbytes;
	return v & mask;
}

// Variable-length signed 32-bit integers

int fs_sizeof_vint32(int32_t value) {
	uint32_t v = value;
	if (value < 0) {
		v = ((~v) << 1) | 1;
	} else {
		v <<= 1;
	}
	return fs_sizeof_vuint32(v);
}

int fs_write_vint32(FILE *fd, int32_t value) {
	uint32_t v = value;
	if (value < 0) {
		v = ((~v) << 1) | 1;
	} else {
		v <<= 1;
	}
	return fs_write_vuint32(fd, v);
}

int32_t fs_read_vint32(FILE *fd, int *nread) {
	int nbytes = 0;
	uint32_t uv = fs_read_vuint32(fd, &nbytes);
	int32_t v = 0;
	if (nbytes > 0) {
		if ((uv & 1) == 1) {
			v = -(int)(uv >> 1) - 1;
		} else {
			v = uv >> 1;
		}
	}
	if (nread)
		*nread = nbytes;
	return v;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

char *fs_getcwd(void) {
	size_t buflen = 4096;
	char *buf = xmalloc(buflen);
	while (1) {
		char *cwd = getcwd(buf, buflen);
		if (cwd) {
			return cwd;
		}
		if (errno != ERANGE) {
			free(buf);
			return NULL;
		}
		buflen += 1024;
		buf = xrealloc(buf, buflen);
	}
}
