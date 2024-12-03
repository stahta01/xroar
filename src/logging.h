/** \file
 *
 *  \brief General logging framework.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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

/* Log levels:
 * 0 - Quiet, 1 - Info, 2 - Events, 3 - Debug */

#ifndef XROAR_LOGGING_H_
#define XROAR_LOGGING_H_

#include <stdint.h>

#ifndef LOGGING

#define LOG_DEBUG(...) do {} while (0)
#define LOG_PRINT(...) do {} while (0)
#define LOG_WARN(...) do {} while (0)
#define LOG_ERROR(...) do {} while (0)

#define LOG_MOD_DEBUG(l,m,...) do {} while (0)
#define LOG_MOD_PRINT(m,...) do {} while (0)
#define LOG_MOD_WARN(m,...) do {} while (0)
#define LOG_MOD_ERROR(m,...) do {} while (0)

#define LOG_MOD_SUB_DEBUG(l,m,s,...) do {} while (0)
#define LOG_MOD_SUB_PRINT(m,s,...) do {} while (0)
#define LOG_MOD_SUB_WARN(m,s,...) do {} while (0)
#define LOG_MOD_SUB_ERROR(m,s,...) do {} while (0)

#define LOG_PAR_MOD_DEBUG(l,p,m,...) do {} while (0)
#define LOG_PAR_MOD_PRINT(p,m,...) do {} while (0)
#define LOG_PAR_MOD_WARN(p,m,...) do {} while (0)
#define LOG_PAR_MOD_ERROR(p,m,...) do {} while (0)

#define LOG_PAR_MOD_SUB_DEBUG(l,p,m,s,...) do {} while (0)
#define LOG_PAR_MOD_SUB_PRINT(p,m,s,...) do {} while (0)
#define LOG_PAR_MOD_SUB_WARN(p,m,s,...) do {} while (0)
#define LOG_PAR_MOD_SUB_ERROR(p,m,s,...) do {} while (0)

#else

#include <stdio.h>

// General log macros

#define LOG_DEBUG(l,...) do { if (logging.level >= l) { printf(__VA_ARGS__); } } while (0)
#define LOG_PRINT(...) printf(__VA_ARGS__)
#define LOG_WARN(...) fprintf(stderr, "WARNING: " __VA_ARGS__)
#define LOG_ERROR(...) fprintf(stderr, "ERROR: " __VA_ARGS__)

// Log with module name

#define LOG_MOD_DEBUG(l,m,...) do { if (logging.level >= l) { printf("[%s] ", (m)); printf(__VA_ARGS__); } } while (0)
#define LOG_MOD_PRINT(m,...) do { printf("[%s] ", (m)); printf(__VA_ARGS__); } while (0)
#define LOG_MOD_WARN(m,...) do { fprintf(stderr, "[%s] WARNING: ", (m)); fprintf(stderr, __VA_ARGS__); } while (0)
#define LOG_MOD_ERROR(m,...) do { fprintf(stderr, "[%s] ERROR: ", (m)); fprintf(stderr, __VA_ARGS__); } while (0)

// Log with module name, submodule name

#define LOG_MOD_SUB_DEBUG(l,m,s,...) do { if (logging.level >= l) { printf("[%s/%s] ", (m), (s)); printf(__VA_ARGS__); } } while (0)
#define LOG_MOD_SUB_PRINT(m,s,...) do { printf("[%s/%s] ", (m), (s)); printf(__VA_ARGS__); } while (0)
#define LOG_MOD_SUB_WARN(m,s,...) do { fprintf(stderr, "[%s/%s] WARNING: ", (m), (s)); fprintf(stderr, __VA_ARGS__); } while (0)
#define LOG_MOD_SUB_ERROR(m,s,...) do { fprintf(stderr, "[%s/%s] ERROR: ", (m), (s)); fprintf(stderr, __VA_ARGS__); } while (0)

// Log with parent, module name

#define LOG_PAR_MOD_DEBUG(l,p,m,...) do { if (logging.level >= l) { printf("[%s:%s] ", (p), (m)); printf(__VA_ARGS__); } } while (0)
#define LOG_PAR_MOD_PRINT(p,m,...) do { printf("[%s:%s] ", (p), (m)); printf(__VA_ARGS__); } while (0)
#define LOG_PAR_MOD_WARN(p,m,...) do { fprintf(stderr, "[%s:%s] WARNING: ", (p), (m)); fprintf(stderr, __VA_ARGS__); } while (0)
#define LOG_PAR_MOD_ERROR(p,m,...) do { fprintf(stderr, "[%s:%s] ERROR: ", (p), (m)); fprintf(stderr, __VA_ARGS__); } while (0)

// Log with parent, module name, submodule name

#define LOG_PAR_MOD_SUB_DEBUG(l,p,m,s,...) do { if (logging.level >= l) { printf("[%s:%s/%s] ", (p), (m), (s)); printf(__VA_ARGS__); } } while (0)
#define LOG_PAR_MOD_SUB_PRINT(p,m,s,...) do { printf("[%s:%s/%s] ", (p), (m), (s)); printf(__VA_ARGS__); } while (0)
#define LOG_PAR_MOD_SUB_WARN(p,m,s,...) do { fprintf(stderr, "[%s:%s/%s] WARNING: ", (p), (m), (s)); fprintf(stderr, __VA_ARGS__); } while (0)
#define LOG_PAR_MOD_SUB_ERROR(p,m,s,...) do { fprintf(stderr, "[%s:%s/%s] ERROR: ", (p), (m), (s)); fprintf(stderr, __VA_ARGS__); } while (0)

#endif

// Category-based debugging

// FDC: state debug level mask (1 = commands, 2 = all)
#define LOG_FDC_STATE (3 << 0)
// FDC: dump sector data flag
#define LOG_FDC_DATA (1 << 2)
// FDC: dump becker data flag
#define LOG_FDC_BECKER (1 << 3)
// FDC: general event debugging
#define LOG_FDC_EVENTS (1 << 4)

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
// UI: joystick motion debugging
#define LOG_UI_JS_MOTION (1 << 1)

#define LOG_DEBUG_FDC(b,...) do { if (logging.debug_fdc & (b)) { LOG_PRINT(__VA_ARGS__); } } while (0)
#define LOG_DEBUG_FILE(b,...) do { if (logging.debug_file & (b)) { LOG_PRINT(__VA_ARGS__); } } while (0)
#define LOG_DEBUG_GDB(b,...) do { if (logging.debug_gdb & (b)) { LOG_PRINT(__VA_ARGS__); } } while (0)
#define LOG_DEBUG_UI(b,...) do { if (logging.debug_ui & (b)) { LOG_PRINT(__VA_ARGS__); } } while (0)

#define LOG_MOD_DEBUG_FDC(b,m,...) do { if (logging.debug_fdc & (b)) { LOG_MOD_PRINT((m), __VA_ARGS__); } } while (0)
#define LOG_MOD_DEBUG_FILE(b,m,...) do { if (logging.debug_file & (b)) { LOG_MOD_PRINT((m), __VA_ARGS__); } } while (0)
#define LOG_MOD_DEBUG_GDB(b,m,...) do { if (logging.debug_gdb & (b)) { LOG_MOD_PRINT((m), __VA_ARGS__); } } while (0)
#define LOG_MOD_DEBUG_UI(b,m,...) do { if (logging.debug_ui & (b)) { LOG_MOD_PRINT((m), __VA_ARGS__); } } while (0)

#define LOG_MOD_SUB_DEBUG_FDC(b,m,s,...) do { if (logging.debug_fdc & (b)) { LOG_MOD_SUB_PRINT((m), (s), __VA_ARGS__); } } while (0)
#define LOG_MOD_SUB_DEBUG_FILE(b,m,s,...) do { if (logging.debug_file & (b)) { LOG_MOD_SUB_PRINT((m), (s), __VA_ARGS__); } } while (0)
#define LOG_MOD_SUB_DEBUG_GDB(b,m,s,...) do { if (logging.debug_gdb & (b)) { LOG_MOD_SUB_PRINT((m), (s), __VA_ARGS__); } } while (0)
#define LOG_MOD_SUB_DEBUG_UI(b,m,s,...) do { if (logging.debug_ui & (b)) { LOG_MOD_SUB_PRINT((m), (s), __VA_ARGS__); } } while (0)

#define LOG_PAR_MOD_DEBUG_FDC(b,p,m,...) do { if (logging.debug_fdc & (b)) { LOG_PAR_MOD_PRINT((p), (m), __VA_ARGS__); } } while (0)
#define LOG_PAR_MOD_DEBUG_FILE(b,p,m,...) do { if (logging.debug_file & (b)) { LOG_PAR_MOD_PRINT((p), (m), __VA_ARGS__); } } while (0)
#define LOG_PAR_MOD_DEBUG_GDB(b,p,m,...) do { if (logging.debug_gdb & (b)) { LOG_PAR_MOD_PRINT((p), (m), __VA_ARGS__); } } while (0)
#define LOG_PAR_MOD_DEBUG_UI(b,p,m,...) do { if (logging.debug_ui & (b)) { LOG_PAR_MOD_PRINT((p), (m), __VA_ARGS__); } } while (0)

#define LOG_PAR_MOD_SUB_DEBUG_FDC(b,p,m,s,...) do { if (logging.debug_fdc & (b)) { LOG_PAR_MOD_SUB_PRINT((p), (m), (s), __VA_ARGS__); } } while (0)
#define LOG_PAR_MOD_SUB_DEBUG_FILE(b,p,m,s,...) do { if (logging.debug_file & (b)) { LOG_PAR_MOD_SUB_PRINT((p), (m), (s), __VA_ARGS__); } } while (0)
#define LOG_PAR_MOD_SUB_DEBUG_GDB(b,p,m,s,...) do { if (logging.debug_gdb & (b)) { LOG_PAR_MOD_SUB_PRINT((p), (m), (s), __VA_ARGS__); } } while (0)
#define LOG_PAR_MOD_SUB_DEBUG_UI(b,p,m,s,...) do { if (logging.debug_ui & (b)) { LOG_PAR_MOD_SUB_PRINT((p), (m), (s), __VA_ARGS__); } } while (0)

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
	_Bool trace_cpu_timing;
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
