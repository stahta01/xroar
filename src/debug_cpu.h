/** \file
 *
 *  \brief Generic CPU debug interface.
 *
 *  \copyright Copyright 2021 Ciaran Anscomb
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

	// Called just before instruction fetch if non-NULL
	DELEGATE_T0(void) instruction_hook;

	// Called after instruction is executed
	DELEGATE_T0(void) instruction_posthook;

	// Get program counter value.  Used by breakpoint hooks.
	DELEGATE_T0(unsigned) get_pc;

	// Set program counter value (JMP).
	DELEGATE_T1(void, unsigned) set_pc;
};

#endif
