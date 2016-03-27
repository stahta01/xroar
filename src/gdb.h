/*
 * XRoar - a Dragon/Tandy Coco emulator
 * Copyright (C) 2003-2016  Ciaran Anscomb
 *
 * See COPYING.GPL for redistribution conditions.
 */

#ifndef XROAR_GDB_H_
#define XROAR_GDB_H_

#define GDB_IP_DEFAULT "127.0.0.1"
#define GDB_PORT_DEFAULT "65520"

int gdb_init(const char *hostname, const char *portname);
void gdb_shutdown(void);

void gdb_handle_signal(int sig);

/* Debugging */

// connections
#define GDB_DEBUG_CONNECT (1 << 0)
// packets
#define GDB_DEBUG_PACKET (1 << 1)
// report bad checksums
#define GDB_DEBUG_CHECKSUM (1 << 2)
// queries and sets
#define GDB_DEBUG_QUERY (1 << 3)

void gdb_set_debug(unsigned);

#endif  /* XROAR_GDB_H_ */
