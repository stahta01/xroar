/** \file
 *
 *  \brief CocoMEM Jr. support.
 *
 *  \copyright Copyright 2026 Jim Brain
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 */

#include "top-config.h"

#include "dragon/dragon.h"

#include "mc6821.h"
#include "mc6883.h"
#include "mc6809/mc6809.h"
#include "xroar.h"

#define JIM_DEBUG 1

/* TODO: consider wrapping this up as a "part", that way data can be allocated
 * and freed as part of machine creation */

// TODO Need to hook the reset action to clear MMU flag.
static uint8_t dat[2][8];
static uint8_t init0;
static uint8_t init1;
static _Bool jim_init;
static uint8_t mem[2048 * 1024];

void cocomem_jr_cpu_cycle(void *sptr, _Bool RnW, uint16_t A) {

	if (!jim_init) {  // this needs moved to some init function
		init0 = 0x80;
		jim_init = 1;
	}

	struct dragon *md = sptr;
	uint8_t data;
	_Bool ignore_sam = 0;

	// Check traps based on untransformed address
	dragon_check_traps(md, RnW, A);

	if (A >= 0xffa0 && A <= 0xffaf) { // MMU registers
		if (RnW)
			data = dat[(A & 0x08) >> 3][A & 0x07];
		else
			dat[(A & 0x08) >> 3][A & 0x07] = md->CPU->D;
		ignore_sam = 1;
	} else if (A == 0xff90) {
		if (RnW)
			data = init0;
		else
			init0 = md->CPU->D;
		ignore_sam = 1;
	} else if (A == 0xff91) {
		if (RnW)
			data = init1;
		else
			init1 = md->CPU->D;
		ignore_sam = 1;
	} else if (init0 & 0x40) { // MMU enabled
#ifdef JIM_DEBUG
		if (A >= 0xfff2) {
			for (uint8_t i=0; i<8; i++) {
				printf("%2.2x, ",dat[init1 & 1][i]);
			}
			printf("\n");
		}
#endif
		uint16_t a = A & 0x1ff;
		uint8_t bank;
		uint8_t b2;
		_Bool ffxx = (A & 0xff00) == 0xff00;
		_Bool altvec = ((A & 0xffe0) == 0xffe0) && !(init0 & 0x80);
		_Bool io = ffxx && !altvec;
		if (((A & 0xff00) == 0xfe00) && ((init0 & 0x08) == 0x08)
		    || ffxx) { // pin CRM page and io, including vectors
			bank = 0x3f;
		} else {
			bank = dat[init1 & 1][A >> 13];
		}
		b2 = bank;
		// invert bits 3,4,5 so $38 -> 00, $3f->07
		bank = (bank & 0xc7)
			| (bank & 0x20 ? 0 : 0x20)
			| (bank & 0x10 ? 0 : 0x10)
			| (bank & 0x08 ? 0 : 0x08);
		_Bool cocoram = ((bank & 0xf8) == 0);
		_Bool int_mem = (!cocoram
				 || (cocoram
				     && ((bank & 0x07) == 0x07)
				     && ((A & 0x1e00) == 0x1e00)
				     && !io
				    )
				);
#ifdef JIM_DEBUG
		if ((A >= 0xfeee) && (A <= 0xfeff) || (A >= 0xfff2) && (A <= 0xffff))
			printf("SPEC RAM=%d,ffxx=%d,altvec=%d,io=%d,bank=%d,b2=%d\n",cocoram,ffxx,altvec,io, bank, b2);
#endif
		if (int_mem) {
#ifdef JIM_DEBUG
			if(A != 0xffff)
				printf("int mem %4.4X, RAM=%d,ffxx=%d,altvec=%d,io=%d,bank=%d,b2=%d,addy=%4.4X,D=%2.2X\n",A,cocoram,ffxx,altvec,io, bank, b2, (uint32_t)bank << 13 | a,mem[(uint32_t)bank << 13 | a] );
#endif
			if (RnW)
				data = mem[(uint32_t)bank << 13 | a];
			else
				mem[(uint32_t)bank << 13 | a] = md->CPU->D;
			ignore_sam = 1;
		} else {
			//printf("ext mem %4.4X, RAM=%d,ffxx=%d,altvec=%d,io=%d,bank=%d,b2=%d\n",A,cocoram,ffxx,altvec,io, bank, b2);
		}
	}

	// SAM decode / timing
	int ncycles;
	if (ignore_sam) {
		// switch SAM to a read of ffff
		ncycles = md->SAM->mem_cycle(md->SAM, 1 , 0xffff);
	} else {
		// use SAM functionality
		ncycles = md->SAM->mem_cycle(md->SAM, RnW, A);
	}

	// Advance clock, collect IRQs
	if (!md->clock_inhibit) {
		dragon_advance_clock(md, ncycles);
		MC6809_IRQ_SET(md->CPU, md->PIA0->a.irq || md->PIA0->b.irq);
		MC6809_FIRQ_SET(md->CPU, md->PIA1->a.irq || md->PIA1->b.irq);
	}

	// Common cycle handling
	dragon_cpu_cycle(md, RnW, A, md->SAM->Zrow, md->SAM->Zcol);

	// Ensure CocoMEM RAM is used for reads where appropriate
	if (ignore_sam && RnW) {
		md->CPU->D = data;
	}
}
