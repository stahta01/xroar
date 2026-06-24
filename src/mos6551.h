/** \file
 *
 *  \brief MOS 6551 Asynchronous Communication Interface Adapter.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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
 *
 *  This does _NOT_ yet implement any ACIA functionality.  Instead it just
 *  provides a dummy interface to allow machines that depend on it being
 *  present to work.
 */

#ifndef XROAR_MOS6551_H_
#define XROAR_MOS6551_H_

#include <stdint.h>

#include "part.h"

struct MOS6551 {
	struct part part;

	bool IRQ;

	uint8_t status_reg;
	uint8_t command_reg;
	uint8_t control_reg;
};

// Hardware reset
void mos6551_reset(struct MOS6551 *);

// CPU interface
void mos6551_access(void *, bool RnW, unsigned A, uint8_t *D);

#endif
