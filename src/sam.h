/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2015  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_SAM_H_
#define XROAR_SAM_H_

#include <stdint.h>

#define EVENT_SAM_CYCLES(c) (c)

extern unsigned sam_S;
extern unsigned sam_Z;
extern _Bool sam_RAS;

#define sam_init()
void sam_reset(void);
int sam_cpu_cycle(_Bool RnW, unsigned A);
void sam_vdg_hsync(_Bool level);
void sam_vdg_fsync(_Bool level);
int sam_vdg_bytes(int nbytes, uint16_t *V, _Bool *valid);
void sam_set_register(unsigned int value);
unsigned int sam_get_register(void);

#endif  /* XROAR_SAM_H_ */
