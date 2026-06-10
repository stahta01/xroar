/** \file
 *
 *  \brief Generic CPU debug interface.
 *
 *  \copyright Copyright 2021-2026 Ciaran Anscomb
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
 *  Common to all CPUs, provides hooks for breakpoints and debugging with GDB.
 */

#ifndef XROAR_DEBUG_CPU_H_
#define XROAR_DEBUG_CPU_H_

#include "delegate.h"

#include "part.h"

struct debug_cpu {
	// Part metadata
	struct part part;

	// Number of registers we can query through this interface
	unsigned num_registers;

	// Indices of registers with common special meaning
	int register_sp;
	int register_pc;

	// Query register size in bytes.  Returns 0 for non-existant registers.
	DELEGATE_T1(unsigned, int) register_size;

	// Get/set register values
	DELEGATE_T1(uint32, int) get_register;
	DELEGATE_T2(void, int, uint32) set_register;
};

#endif
