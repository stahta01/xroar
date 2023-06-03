/** \file
 *
 *  \brief ROM CRC database.
 *
 *  \copyright Copyright 2012-2020 Ciaran Anscomb
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

#ifndef XROAR_CRCLIST_H_
#define XROAR_CRCLIST_H_

#include <stdint.h>
#include <stdio.h>

struct sdsx_list;

// Assign a crclist.  Overwrites any existing list with provided name.
void crclist_assign(const char *name, struct sdsx_list *values);

/* Attempt to find a CRC image.  If name starts with '@', search the named
 * list for the first accessible entry, otherwise search for a single entry. */
int crclist_match(const char *name, uint32_t crc);

/* Print a list of defined CRC lists to stdout */
void crclist_print_all(FILE *f);
/* Print list and exit */
void crclist_print(void);

/* Tidy up */
void crclist_shutdown(void);

#endif
