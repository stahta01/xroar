/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_FS_H_
#define XROAR_FS_H_

#include <stdio.h>

/* Some file handling convenience functions */

int fs_write_uint8(FILE *stream, int value);
int fs_write_uint16(FILE *stream, int value);
int fs_write_uint16_le(FILE *stream, int value);
int fs_read_uint8(FILE *stream);
int fs_read_uint16(FILE *stream);
int fs_read_uint16_le(FILE *stream);
int fs_read_vl_uint31(FILE *stream);  // variable-length uint31

/* Variable-length uint31 defined as:
 * 7-bit        0nnnnnnn
 * 14-bit       10nnnnnn nnnnnnnn
 * 21-bit       110nnnnn nnnnnnnn nnnnnnnn
 * 28-bit       1110nnnn nnnnnnnn nnnnnnnn nnnnnnnn
 * 31-bit       11110XXX Xnnnnnnn nnnnnnnn nnnnnnnn nnnnnnnn
 */

#endif  /* XROAR_FS_H_ */
