/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2016  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_VO_OPENGL_H_
#define XROAR_VO_OPENGL_H_

/* OpenGL code is common to several video modules.  All the stuff that's not
 * toolkit-specific goes in here. */

#include <stdint.h>

extern int vo_opengl_x, vo_opengl_y;
extern int vo_opengl_w, vo_opengl_h;

_Bool vo_opengl_init(void);
void vo_opengl_shutdown(void);
void vo_opengl_alloc_colours(void);
void vo_opengl_refresh(void);
void vo_opengl_vsync(struct vo_module *vo);
void vo_opengl_set_window_size(unsigned w, unsigned h);
void vo_opengl_render_scanline(struct vo_module *vo, uint8_t const *data, struct ntsc_burst *burst, unsigned phase);
void vo_opengl_set_vo_cmp(struct vo_module *vo, int mode);

#endif  /* XROAR_VO_OPENGL_H_ */
