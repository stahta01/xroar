/*

Generic OpenGL support for video output modules

Copyright 2012-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

OpenGL code is common to several video modules.  All the stuff that's
not toolkit-specific goes in here.

*/

#ifndef XROAR_VO_OPENGL_H_
#define XROAR_VO_OPENGL_H_

#include <stdint.h>

struct vo_interface;
struct vo_rect;

struct vo_interface *vo_opengl_new(struct vo_cfg *vo_cfg);
void vo_opengl_get_display_rect(struct vo_interface *vo, struct vo_rect *disp);

#endif
