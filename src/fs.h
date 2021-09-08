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

#ifndef XROAR_FS_H_
#define XROAR_FS_H_

#include <inttypes.h>
#include <stdio.h>
#include <sys/types.h>

off_t fs_file_size(FILE *fd);

// unlike ftruncate(), this leaves file position at new EOF
int fs_truncate(FILE *fd, off_t length);

// Writing basic integer types

int fs_write_uint8(FILE *fd, int value);
int fs_write_uint16(FILE *fd, int value);
int fs_write_uint16_le(FILE *fd, int value);
int fs_write_uint31(FILE *fd, int value);

// Reading basic integer types

int fs_read_uint8(FILE *fd);
int fs_read_uint16(FILE *fd);
int fs_read_uint16_le(FILE *fd);
int fs_read_uint31(FILE *fd);

// Variable-length 32-bit integers

int fs_sizeof_vuint32(uint32_t value);
int fs_write_vuint32(FILE *fd, uint32_t value);
uint32_t fs_read_vuint32(FILE *fd, int *nread);

int fs_sizeof_vint32(int32_t value);
int fs_write_vint32(FILE *fd, int32_t value);
int32_t fs_read_vint32(FILE *fd, int *nread);

/* vuint32 defined as:
 * 7-bit        0nnnnnnn
 * 14-bit       10nnnnnn nnnnnnnn
 * 21-bit       110nnnnn nnnnnnnn nnnnnnnn
 * 28-bit       1110nnnn nnnnnnnn nnnnnnnn nnnnnnnn
 * 32-bit       1111XXXX nnnnnnnn nnnnnnnn nnnnnnnn nnnnnnnn
 *
 * vint32 is transformed into a vuint32 for writing by complementing negative
 * numbers and moving sign to bit0 for more efficient encoding.
 */

// Wrap getcwd(), automatically allocating a buffer.  May still return NULL for
// other reasons, so check errno (check getcwd manpage).
char *fs_getcwd(void);

#endif
