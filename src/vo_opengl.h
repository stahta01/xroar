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
struct vo_rect;

struct vo_interface *vo_opengl_new(void *cfg);
void vo_opengl_get_display_rect(struct vo_interface *vo, struct vo_rect *disp);

#endif  /* XROAR_VO_OPENGL_H_ */
