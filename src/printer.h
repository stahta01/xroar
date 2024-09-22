/** \file
 *
 *  \brief Printing to file or pipe
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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

#ifndef XROAR_PRINTER_H_
#define XROAR_PRINTER_H_

#include "delegate.h"

enum {
	PRINTER_DESTINATION_NONE,
	PRINTER_DESTINATION_FILE,
	PRINTER_DESTINATION_PIPE,
};

struct printer_interface {
	DELEGATE_T1(void, bool) signal_ack;
};

struct printer_interface *printer_interface_new(void);
void printer_interface_free(struct printer_interface *pi);
void printer_reset(struct printer_interface *pi);

void printer_flush(struct printer_interface *pi);
void printer_strobe(struct printer_interface *pi, _Bool strobe, int data);
_Bool printer_busy(struct printer_interface *pi);

#endif
