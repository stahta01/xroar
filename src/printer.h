/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_PRINTER_H_
#define XROAR_PRINTER_H_

#include "delegate.h"

struct machine;

struct printer_interface {
	DELEGATE_T1(void, bool) signal_ack;
};

struct printer_interface *printer_interface_new(struct machine *m);
void printer_interface_free(struct printer_interface *pi);
void printer_reset(struct printer_interface *pi);

void printer_open_file(struct printer_interface *pi, const char *filename);
void printer_open_pipe(struct printer_interface *pi, const char *command);
void printer_close(struct printer_interface *pi);

void printer_flush(struct printer_interface *pi);
void printer_strobe(struct printer_interface *pi, _Bool strobe, int data);
_Bool printer_busy(struct printer_interface *pi);

#endif
