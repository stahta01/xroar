/*  Copyright 2015 Alan Cox
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "xalloc.h"

#include "cart.h"
#include "logging.h"
#include "xroar.h"

#include "becker.h"
#include "ide.h"

struct idecart {
  struct cart cart;
  struct ide_controller *controller;
  _Bool have_becker;
};


static void idecart_reset(struct cart *c) {
  struct idecart *ide = (struct idecart *)c;
  if (ide->have_becker)
    becker_reset();
  ide_reset_begin(ide->controller);
}

static void idecart_detach(struct cart *c) {
  struct idecart *ide = (struct idecart *)c;
  ide_free(ide->controller);
  if (ide->have_becker)
    becker_close();
  cart_rom_detach(c);
}

static void idecart_write(struct cart *c, uint16_t A, _Bool P2, uint8_t D) {
  struct idecart *ide = (struct idecart *)c;

  if (!P2)
    return;

  if (A == 0xFF58) {
    ide_write_latched(ide->controller, ide_data_latch, D);
    return;
  }
  if (A == 0xFF50) {
    ide_write_latched(ide->controller, ide_data, D);
    return;
  }
  if (A > 0xFF50 && A < 0xFF58) {
    ide_write_latched(ide->controller, (A - 0xFF50), D);
    return;
  }
  if (ide->have_becker) {
    if (A == 0xFF42)
      becker_write_data(D);
  }
}

static uint8_t idecart_read(struct cart *c, uint16_t A, _Bool P2, uint8_t D) {
  struct idecart *ide = (struct idecart *)c;

  uint8_t r = D;
  if (!P2)
    return c->rom_data[A & 0x3FFF];
  if (A == 0xFF58)
    r = ide_read_latched(ide->controller, ide_data_latch);
  else if (A == 0xFF50)
    r = ide_read_latched(ide->controller, ide_data);
  else if (A > 0xFF50 && A < 0xFF58)
    r = ide_read_latched(ide->controller, A - 0xFF50);
  /* Becker */
  else if (ide->have_becker) {
    if (A == 0xFF41)
      r = becker_read_status();
    else if (A == 0xFF42)
      r = becker_read_data();
  }
  return r;
}

static void idecart_init(struct idecart *ide) {
  struct cart *c = (struct cart *)ide;
  struct cart_config *cc = c->config;
  int fd;

  cart_rom_init(c);
  c->read = idecart_read;
  c->write = idecart_write;
  c->reset = idecart_reset;
  c->detach = idecart_detach;
  ide->have_becker = (cc->becker_port && becker_open());

  ide->controller = ide_allocate("ide0");
  if (ide->controller == NULL) {
    perror(NULL);
    exit(1);
  }
  fd = open("hd0.img", O_RDWR);
  if (fd == -1) {
    perror("hd0.img");
    return;
  }
  ide_attach(ide->controller, 0, fd);
  ide_reset_begin(ide->controller);
}

struct cart *idecart_new(struct cart_config *cc) {
  struct idecart *ide = xmalloc(sizeof(*ide));
  ide->cart.config = cc;
  idecart_init(ide);
  return &ide->cart;
}
