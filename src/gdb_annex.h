/** \file
 *
 *  \brief GDB target description files.
 *
 *  \copyright Copyright 2026 Ciaran Anscomb
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

#ifndef XROAR_GDB_ANNEX_H_
#define XROAR_GDB_ANNEX_H_

#include <stdlib.h>

struct gdb_annex {
	const char *name;
	const char *data;
	size_t data_size;
};

extern struct gdb_annex gdb_annex_list[];

extern size_t num_gdb_annex;

#endif
