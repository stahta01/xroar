/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2010  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef __TYPES_H__
#define __TYPES_H__

#include "config.h"
#include "portalib.h"

#ifdef HAVE_GP32
# include "gp32/types.h"
#endif

#ifdef HAVE_NDS
# include "nds/types.h"
#endif

#ifndef HAVE_GP32
#include <sys/types.h>
#include <inttypes.h>

typedef int32_t Cycle;
#endif

#endif  /* __TYPES_H__ */
