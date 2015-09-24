/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2015  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_IDECART_H_
#define XROAR_IDECART_H_

struct cart_config;
struct cart;

struct cart *idecart_new(struct cart_config *cc);

#endif  /* XROAR_IDECART_H_ */
