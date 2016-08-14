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

_Bool vo_opengl_init(struct vo_interface *vo);
void vo_opengl_shutdown(struct vo_interface *vo);
void vo_opengl_alloc_colours(struct vo_interface *vo);
void vo_opengl_refresh(struct vo_interface *vo);
void vo_opengl_vsync(struct vo_interface *vo);
void vo_opengl_set_window_size(struct vo_interface *vo, unsigned w, unsigned h);
void vo_opengl_render_scanline(struct vo_interface *vo, uint8_t const *data, struct ntsc_burst *burst, unsigned phase);
void vo_opengl_set_vo_cmp(struct vo_interface *vo, int mode);

#endif  /* XROAR_VO_OPENGL_H_ */
