/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2016  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_VO_OPENGL_H_
#define XROAR_VO_OPENGL_H_

/* OpenGL code is common to several video modules.  All the stuff that's not
 * toolkit-specific goes in here. */

#include <stdint.h>

struct vo_interface;

extern int vo_opengl_x, vo_opengl_y;
extern int vo_opengl_w, vo_opengl_h;

struct vo_interface *vo_opengl_new(void);

#endif  /* XROAR_VO_OPENGL_H_ */
