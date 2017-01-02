/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_MPI_H_
#define XROAR_MPI_H_

struct cart;

void mpi_switch_slot(struct cart *c, unsigned slot);

void mpi_set_initial(int slot);
void mpi_set_cart(int slot, const char *name);

#endif  /* XROAR_MPI_H_ */
