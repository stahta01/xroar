/** \file
 *
 *  \brief Missing endian.h.
 *
 *  \copyright Copyright 2014 Ciaran Anscomb
 *
 *  \licenseblock This file is part of Portalib.
 *
 *  Portalib is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by the Free
 *  Software Foundation; either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  See COPYING.LGPL and COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 *
 *  If endian.h was not found by configure, it should have performed a test or
 *  made an assumption, and HAVE_BIG_ENDIAN will be defined accordingly.
 */

#ifndef PORTALIB_PL_ENDIAN_H_
#define PORTALIB_PL_ENDIAN_H_

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif

#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN    4321
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif

#ifndef __BYTE_ORDER
#ifdef HAVE_BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
#endif

#endif
