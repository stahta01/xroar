/** \file
 *
 *  \brief C array handling.
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
 */

// Just the one macro for now.

#ifndef PORTALIB_ARRAY_H_
#define PORTALIB_ARRAY_H_

#define ARRAY_N_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

#endif
