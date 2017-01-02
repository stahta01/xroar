/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_SAM_H_
#define XROAR_SAM_H_

#include <stdint.h>

#include "delegate.h"

#define EVENT_SAM_CYCLES(c) (c)

struct MC6883 {
	unsigned S;
	unsigned Z;
	unsigned V;
	_Bool RAS;
	DELEGATE_T3(void, int, bool, uint16) cpu_cycle;
};

struct MC6883 *sam_new(void);
void sam_free(struct MC6883 *);

void sam_reset(struct MC6883 *);
void sam_mem_cycle(void *, _Bool RnW, uint16_t A);
void sam_vdg_hsync(struct MC6883 *, _Bool level);
void sam_vdg_fsync(struct MC6883 *, _Bool level);
int sam_vdg_bytes(struct MC6883 *, int nbytes);
void sam_set_register(struct MC6883 *, unsigned int value);
unsigned int sam_get_register(struct MC6883 *);

#endif  /* XROAR_SAM_H_ */
