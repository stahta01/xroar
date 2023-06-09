/** \file
 *
 *  \brief Multi-Pak Interface (MPI) support.
 *
 *  \copyright Copyright 2014-2019 Ciaran Anscomb
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

#ifndef XROAR_MPI_H_
#define XROAR_MPI_H_

struct cart;

void mpi_switch_slot(struct cart *c, unsigned slot);

void mpi_set_initial(int slot);
void mpi_set_cart(int slot, const char *name);
void mpi_shutdown(void);

#endif
