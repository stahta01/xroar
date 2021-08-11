/** \file
 *
 *  \brief ROM filename database.
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

#ifndef XROAR_ROMLIST_H_
#define XROAR_ROMLIST_H_

#include <stdio.h>

#include "sds.h"

struct sdsx_list;

// Assign a romlist.  Overwrites any existing list with provided name.
void romlist_assign(const char *name, struct sdsx_list *values);

/* Attempt to find a ROM image.  If name starts with '@', search the named
 * list for the first accessible entry, otherwise search for a single entry. */
sds romlist_find(const char *name);

/* Print a list of defined ROM lists */
void romlist_print_all(FILE *f);
/* Print list and exit */
void romlist_print(void);

/* Tidy up */
void romlist_shutdown(void);

#endif
