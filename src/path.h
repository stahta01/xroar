/*

File path searching

Copyright 2009-2020 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_PATH_H_
#define XROAR_PATH_H_

#include "sds.h"

// Interpolate variables into a path element or filename (only considers
// leading "~/" for now).

sds path_interp(const char *filename);

// Try to find regular file within one of the directories supplied.  Returns
// allocated memory containing full path to file if found, NULL otherwise.
// Directory separator occuring within filename just causes that one file to be
// checked.

sds find_in_path(const char *path, const char *filename);

#endif
