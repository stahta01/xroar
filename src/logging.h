/*

General logging framework

Copyright 2003-2021 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_LOGGING_H_
#define XROAR_LOGGING_H_

#include <stdint.h>

#ifndef LOGGING

#define LOG_DEBUG(...) do {} while (0)
#define LOG_PRINT(...) do {} while (0)
#define LOG_WARN(...) do {} while (0)
#define LOG_ERROR(...) do {} while (0)

#else

#include <stdio.h>

/* Log levels:
 * 0 - Quiet, 1 - Info, 2 - Events, 3 - Debug */

#define LOG_DEBUG(l,...) do { if (logging.level >= l) { printf(__VA_ARGS__); } } while (0)
#define LOG_PRINT(...) printf(__VA_ARGS__)
#define LOG_WARN(...) fprintf(stderr, "WARNING: " __VA_ARGS__)
#define LOG_ERROR(...) fprintf(stderr, "ERROR: " __VA_ARGS__)

#endif

// Category-based debugging

// FDC: state debug level mask (1 = commands, 2 = all)
#define LOG_FDC_STATE (3 << 0)
// FDC: dump sector data flag
#define LOG_FDC_DATA (1 << 2)
// FDC: dump becker data flag
#define LOG_FDC_BECKER (1 << 3)

// Files: binary files & hex record metadata
#define LOG_FILE_BIN (1 << 0)
// Files: binary files & hex record data
#define LOG_FILE_BIN_DATA (1 << 1)
// Files: tape autorun filename block metadata
#define LOG_FILE_TAPE_FNBLOCK (1 << 2)

// GDB: connections
#define LOG_GDB_CONNECT (1 << 0)
// GDB: packets
#define LOG_GDB_PACKET (1 << 1)
// GDB: report bad checksums
#define LOG_GDB_CHECKSUM (1 << 2)
// GDB: queries and sets
#define LOG_GDB_QUERY (1 << 3)

// UI: keyboard event debugging
#define LOG_UI_KBD_EVENT (1 << 0)

#define LOG_DEBUG_FDC(b,...) do { if (logging.debug_fdc & (b)) { printf(__VA_ARGS__); } } while (0)
#define LOG_DEBUG_FILE(b,...) do { if (logging.debug_file & (b)) { printf(__VA_ARGS__); } } while (0)
#define LOG_DEBUG_GDB(b,...) do { if (logging.debug_gdb & (b)) { printf(__VA_ARGS__); } } while (0)
#define LOG_DEBUG_UI(b,...) do { if (logging.debug_ui & (b)) { printf(__VA_ARGS__); } } while (0)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct logging {
	// General log level: 0=quiet, 1=info, 2=events, 3=debug
	int level;
	// Category-based debug flags
	unsigned debug_fdc;
	unsigned debug_file;
	unsigned debug_gdb;
	unsigned debug_ui;
	// Specific tracing
	_Bool trace_cpu;
};

extern struct logging logging;  // global log/debug flags

struct log_handle;

// close any open log
void log_close(struct log_handle **);

// hexdumps - pretty print blocks of data
void log_open_hexdump(struct log_handle **, const char *prefix);
void log_hexdump_set_addr(struct log_handle *, unsigned addr);
void log_hexdump_line(struct log_handle *);
void log_hexdump_byte(struct log_handle *, uint8_t b);
void log_hexdump_block(struct log_handle *, uint8_t *buf, unsigned len);
void log_hexdump_flag(struct log_handle *l);

#endif
