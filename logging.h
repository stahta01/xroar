/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2005  Ciaran Anscomb
 *
 *  See COPYING for redistribution conditions. */

#ifndef __LOGGING_H__
#define __LOGGING_H__

#include <stdio.h>

/* 0 - Silent, 1 - Title, 2 - Info, 3 - Details, 4 - Verbose, 5 - Silly */
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 3
#endif

#define LOG_DEBUG(l,...) if (DEBUG_LEVEL >= l) { fprintf(stderr, __VA_ARGS__); }
#define LOG_WARN(...) fprintf(stderr, "WARNING: " __VA_ARGS__);
#define LOG_ERROR(...) fprintf(stderr, "ERROR: " __VA_ARGS__);

#endif  /* __LOGGING_H__ */
