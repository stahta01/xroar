/*

File operations

Copyright 2003-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _POSIX_C_SOURCE 200112L

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

/* Read a variable-length max 31-bit unsigned int. */

int fs_read_vuint31(FILE *fd) {
	int val0 = fs_read_uint8(fd);
	if (val0 < 0)
		return -1;
	int tmp = val0;
	int shift = 0;
	int mask = 0xff;
	int val1 = 0;
	while ((tmp & 0x80) == 0x80) {
		tmp <<= 1;
		shift += 8;
		mask >>= 1;
		int in = fs_read_uint8(fd);
		if (in < 0)
			return -1;
		if (shift > 24) {
			in &= 0x7f;
			mask = 0;  // ignore val0
			tmp = 0;  // no more
		}
		val1 = (val1 << 8) | in;
	}
	return ((val0 & mask) << shift) | val1;
}

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
