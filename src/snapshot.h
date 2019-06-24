/*

Snapshotting of emulated system

Copyright 2003-2006 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_SNAPSHOT_H_
#define XROAR_SNAPSHOT_H_

int write_snapshot(const char *filename);
int read_snapshot(const char *filename);

#endif
