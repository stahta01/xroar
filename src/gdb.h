/*
 * XRoar - a Dragon/Tandy Coco emulator
 * Copyright (C) 2003-2017  Ciaran Anscomb
 *
 * See COPYING.GPL for redistribution conditions.
 */

#ifndef XROAR_GDB_H_
#define XROAR_GDB_H_

#define GDB_IP_DEFAULT "127.0.0.1"
#define GDB_PORT_DEFAULT "65520"

struct bp_session;
struct machine;

enum gdb_run_state {
	gdb_run_state_running = 0,
	gdb_run_state_stopped,
	gdb_run_state_single_step,
};

struct gdb_interface;

struct gdb_interface *gdb_interface_new(const char *hostname, const char *portname, struct machine *m, struct bp_session *bp_session);
void gdb_interface_free(struct gdb_interface *gi);

int gdb_run_lock(struct gdb_interface *gi);
void gdb_run_unlock(struct gdb_interface *gi);
void gdb_stop(struct gdb_interface *gi, int sig);
void gdb_single_step(struct gdb_interface *gi);
_Bool gdb_signal_lock(struct gdb_interface *gi, int sig);

/* Debugging */

// connections
#define GDB_DEBUG_CONNECT (1 << 0)
// packets
#define GDB_DEBUG_PACKET (1 << 1)
// report bad checksums
#define GDB_DEBUG_CHECKSUM (1 << 2)
// queries and sets
#define GDB_DEBUG_QUERY (1 << 3)

void gdb_set_debug(struct gdb_interface *gi, unsigned);

#endif  /* XROAR_GDB_H_ */
