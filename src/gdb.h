/*
 * XRoar - a Dragon/Tandy Coco emulator
 * Copyright (C) 2003-2014  Ciaran Anscomb
 *
 * See COPYING.GPL for redistribution conditions.
 */

#ifndef XROAR_GDB_H_
#define XROAR_GDB_H_

#define GDB_IP_DEFAULT "127.0.0.1"
#define GDB_PORT_DEFAULT "65520"

int gdb_init(void);
void gdb_shutdown(void);

void gdb_handle_signal(int sig);

#endif  /* XROAR_GDB_H_ */
