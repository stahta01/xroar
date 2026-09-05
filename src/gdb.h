/* GDB protocol support.
 *
 * Copyright 2013-2026 Ciaran Anscomb
 *
 * This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 * XRoar is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * See COPYING.GPL for redistribution conditions.
 */

#ifndef XROAR_GDB_H_
#define XROAR_GDB_H_

#define GDB_IP_DEFAULT "127.0.0.1"
#define GDB_PORT_DEFAULT "65520"

struct machine;

enum gdb_run_state {
	gdb_run_state_running = 0,
	gdb_run_state_stopped,
	gdb_run_state_single_step,
};

struct gdb_interface;

struct gdb_interface *gdb_interface_new(const char *hostname, const char *portname, struct machine *m);
void gdb_interface_free(struct gdb_interface *gi);

// Lock the run_state mutex and return true if ready to run.

int gdb_run_lock(struct gdb_interface *gi);

// Unlock the run_state mutex.

void gdb_run_unlock(struct gdb_interface *gi);

// Called by machine after receiving a signal.

void gdb_stop(struct gdb_interface *gi, int sig);

// Called by machine to acknowledge single-step.

void gdb_single_step(struct gdb_interface *gi);

#endif
