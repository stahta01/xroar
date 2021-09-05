/** \file
 *
 *  \brief TCC1014 (GIME) support.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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
 *
 *  Tandy CoCo 3 support is UNFINISHED and UNSUPPORTED, and much is KNOWN NOT
 *  TO WORK.  Please do not use except for testing.
 *
 *  \par Sources
 *  Sock's GIME register reference http://users.axess.com/twilight/sock/gime.html
 */

// The "border" interrupts appear to be accurately named - the IRQ line fall is
// coincident with the end of the active area, for both horizontal and vertical
// border interrupts.

// XXX PAL mode.
//
// At the moment I simply bodge 25 extra top/bottom border lines and set a
// longer field duration.  There is then another bodge to skip sending the
// first 25 scanlines to the video module.
//
// If interrupts are timed somewhere during these bodges, I'll have to rethink
// earlier than I want to!

#include "config.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "delegate.h"
#include "events.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "part.h"
#include "tcc1014/font-gime.h"
#include "tcc1014/tcc1014.h"
#include "vo.h"
#include "xroar.h"

struct ser_handle;

enum vdg_render_mode {
	TCC1014_RENDER_SG,
	TCC1014_RENDER_CG,
	TCC1014_RENDER_RG,
};

enum tcc1014_vstate {
	tcc1014_vstate_vblank,
	tcc1014_vstate_top_border,
	tcc1014_vstate_active_area,
	tcc1014_vstate_bottom_border,
};

struct TCC1014_private {
	struct TCC1014 public;

	/* Timing */
	struct event hs_fall_event;
	struct event hs_rise_event;
	struct event hs_border_event;
	event_ticks scanline_start;
	unsigned beam_pos;
	unsigned scanline;

	// Timer
	struct event timer_event;
	event_ticks timer_tick_base;
	int timer_counter;
	int timer_offset;  // 2 for 1986 GIME, 1 for 1987 GIME

	/* Data */
	uint8_t vram_g_data;
	uint8_t vram_sg_data;

	/* Output */
	int frame;  // frameskip counter

	// $FF22: PIA1B video control lines
	// XXX there may be a need for latch propagation as with the VDG, but
	// for now assume that VDG-compatible modes are simulated in a basic
	// fashion.
	_Bool vmode_direction;  // snooped direction register
	unsigned vmode;  // snooped data register (mode bits only)
	_Bool GnA;
	unsigned GM;
	_Bool GM0;
	_Bool CSS;

	// $FF90: Initialisation register 0 - INIT0
	// $FF91: Initialisation register 1 - INIT1
	// $FF92: Interrupt request enabled register - IRQENR
	// $FF93: Fast interrupt request enabled register - FIRQENR
	// $FF94: Timer register MSB
	// $FF95: Timer register LSB
	// $FF98: Video mode register - VMODE
	// $FF99: Video resolution register - VRES
	// $FF99: Video resolution register - VRES
	// $FF9A: Border colour register - BRDR
	// $FF9B: Disto bank select - VBANK
	// $FF9C: Vertical scroll register - VSC
	// $FF9D: Vertical offset register MSB
	// $FF9E: Vertical offset register LSB
	// $FF9F: Horizontal offset register
	uint8_t registers[16];

	// $FF90: Initialisation register 0 - INIT0
	_Bool COCO;  // 1=Color Computer Compatible
	_Bool MMUEN;  // 1=MMU Enabled (COCO = 0)
	_Bool MC3;  // 1=RAM at $FExx is constant
	_Bool MC2;  // 1=$FF4x external; 0=internal
	_Bool MC1;  // ROM map control
	_Bool MC0;  // ROM map control

	// $FF91: Initialisation register 1 - INIT1
	_Bool TINS;  // Timer source: 1=3.58MHz, 0=15.7kHz
	unsigned TR;  // MMU task select 0=task 1, 8=task 2

	// $FF98: Video mode register - VMODE
	_Bool BP;  // 1=Graphics; 0=Text
	_Bool BPI;  // 1=Composite phase invert
	_Bool MOCH;  // 1=Monochrome on composite out
	_Bool H50;  // 1=50Hz video; 0=60Hz video
	unsigned LPR;  // Lines Per Row: 1, 2, 8, 9, 10, 11 or 65535 (=infinite)

	// $FF99: Video resolution register - VRES
	unsigned LPF;  // Lines Per Field: 192, 200, 65535 (=infinite), 225
	unsigned HRES;  // Bytes Per Row: 16, 20, 32, 40, 64, 80, 128, 160
	unsigned CRES;  // Bits Per Pixel: 1, 2, 4, 0

	// $FF9A: Border colour register - BRDR
	uint8_t BRDR;

	// $FF9C: Vertical scroll register - VSC
	unsigned VSC;

	// $FF9D: Vertical offset register MSB
	// $FF9E: Vertical offset register LSB
	uint32_t Y;

	// $FF9F: Horizontal offset register
	_Bool HVEN;  // 1=Horizontal virtual screen enable (256 bytes per row)
	unsigned X;  // Horizontal offset

	// $FFA0-$FFA7: MMU bank registers (task one)
	// $FFA8-$FFAF: MMU bank registers (task two)
	uint32_t mmu_bank[16];

	// $FFB0-$FFBF: Colour palette registers
	uint8_t palette_reg[16];

	// $FFC0-$FFC5: SAM clear/set VDG mode
	// $FFC6-$FFD3: SAM clear/set VDG display offset
	// $FFD8/$FFD9: Clear/set MPU rate
	// $FFDE/$FFDF: Clear/set map type
	uint16_t SAM_register;

	// $FFC0-$FFC5: SAM clear/set VDG mode
	uint8_t SAM_V;

	// $FFC6-$FFD3: SAM clear/set VDG display offset
	uint16_t SAM_F;

	// $FFD8/$FFD9: Clear/set MPU rate
	_Bool R1;

	// $FFDE/$FFDF: Clear/set map type
	_Bool TY;

	unsigned irq_state;
	unsigned firq_state;

	// Flags
	_Bool inverted_text;

	// Video address
	uint32_t line_base;
	uint32_t B;  // Current VRAM address
	unsigned row;  // 0 <= row < nLPR

	// Video timing
	unsigned field_duration;  // 312 (PAL) or 262 (NTSC)
	unsigned lTB;  // Top Border lines, from VRES
	unsigned lAA;  // Active Area lines, from VRES
	unsigned pLB;  // Left Border pixels, from VRES

	// Video state
	enum tcc1014_vstate vstate;
	unsigned nTB;  // Top Border, from lTB or COCO
	unsigned nAA;  // Active Area, from lAA or COCO
	unsigned nLB;  // Left Border, from pLB or COCO
	unsigned nLPR;  // Lines Per Row, from LPR or COCO
	unsigned lcount;  // General scanline counter
	_Bool attr_blink;  // Text blink
	_Bool attr_undln;  // Text blink
	unsigned attr_fgnd;  // Text fg colour
	unsigned attr_bgnd;  // Text bg colour

	uint8_t border_colour;

	// Internal state
	_Bool SnA;
	_Bool is_32byte;
	uint8_t s_fg_colour;
	uint8_t s_bg_colour;
	uint8_t fg_colour;
	uint8_t bg_colour;
	uint8_t cg_colours;
	int vram_bit;
	enum vdg_render_mode render_mode;
	unsigned pal_padding;
	_Bool blink;

	/* Unsafe warning: pixel_data[] *may* need to be 16 elements longer
	 * than a full scanline.  16 is the maximum number of elements rendered
	 * in render_scanline() between index checks. */
	uint8_t pixel_data[TCC1014_LINE_DURATION+16];

	unsigned vram_nbytes;

	/* Counters */
	unsigned lborder_remaining;
	unsigned vram_remaining;
	unsigned rborder_remaining;

	// 6847T1-compatible state */
	_Bool inverse_text;
	_Bool text_border;
	uint8_t text_border_colour;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void tcc1014_free(struct part *p);

static void tcc1014_set_register(struct TCC1014_private *gime, unsigned reg, unsigned val);
static void update_from_sam_register(struct TCC1014_private *gime);

static const unsigned VMODE_LPR[8] = { 1, 1, 2, 8, 9, 10, 11, 65535 };

// Index LPF into top border and active area line counts.  In both cases, a
// transition to LPF=2 implies an infinite count (so just set really large).
static const unsigned VRES_LPF_lTB[4] = { 25, 21, 65535, 9 };  // top border - unsure...
static const unsigned VRES_LPF_lAA[4] = { 192, 200, 65535, 225 };

static const unsigned VRES_HRES_pLB[2] = { 120, 56 };  // left border
static const unsigned VRES_HRES_pBRD[2] = { 760, 824 };  // pixels until border interrupt
static const unsigned VSC_nLPR[16] = { 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 4, 3, 2, 1, 12 };
static const unsigned SAM_V_nLPR[8] = { 12, 1, 3, 2, 2, 1, 1, 1 };
static const unsigned VRES_HRES_BPR[8] = { 16, 20, 32, 40, 64, 80, 128, 160 };
static const unsigned VRES_HRES_BPR_TEXT[8] = { 32, 40, 32, 40, 64, 80, 64, 80 };

static void do_hs_fall(void *);
static void do_hs_rise(void *);
static void do_hs_border(void *);
static void update_timer(void *);
static void render_scanline(struct TCC1014_private *gime);
static void tcc1014_update_graphics_mode(struct TCC1014_private *gime);

#define SET_INTERRUPT(g,v) do { \
		(g)->irq_state |= ((v) & (g)->registers[2]); \
		(g)->firq_state |= ((v) & (g)->registers[3]); \
		(g)->public.IRQ = ((g)->registers[0] & 0x20) ? ((g)->irq_state & 0x3f) : 0; \
		(g)->public.FIRQ = ((g)->registers[0] & 0x10) ? ((g)->firq_state & 0x3f) : 0; \
	} while (0)

#define SCANLINE(s) ((s) % TCC1014_FRAME_DURATION)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct TCC1014 *tcc1014_new(int type) {
	_Bool is_1986 = (type == VDG_GIME_1986);
	struct TCC1014_private *gime = part_new(sizeof(*gime));
	*gime = (struct TCC1014_private){0};
	part_init((struct part *)gime, is_1986 ? "TCC1014-1986": "TCC1014-1987");
	gime->public.part.free = tcc1014_free;

	gime->public.cpu_cycle = DELEGATE_DEFAULT3(void, int, bool, uint16);
	gime->public.fetch_vram = DELEGATE_DEFAULT1(uint8, uint32);

	gime->timer_offset = (type == VDG_GIME_1986) ? 2 : 1;

	gime->line_base = 0x60400;
	gime->beam_pos = TCC1014_LEFT_BORDER_START;
	gime->public.signal_hs = DELEGATE_DEFAULT1(void, bool);
	gime->public.signal_fs = DELEGATE_DEFAULT1(void, bool);
	event_init(&gime->hs_fall_event, DELEGATE_AS0(void, do_hs_fall, gime));
	event_init(&gime->hs_rise_event, DELEGATE_AS0(void, do_hs_rise, gime));
	event_init(&gime->hs_border_event, DELEGATE_AS0(void, do_hs_border, gime));
	event_init(&gime->timer_event, DELEGATE_AS0(void, update_timer, gime));

	return (struct TCC1014 *)gime;
}

void tcc1014_free(struct part *p) {
	struct TCC1014_private *gime = (struct TCC1014_private *)p;
	event_dequeue(&gime->hs_border_event);
	event_dequeue(&gime->hs_fall_event);
	event_dequeue(&gime->hs_rise_event);
}

void tcc1014_set_sam_register(struct TCC1014 *gimep, unsigned val) {
	struct TCC1014_private *gime = (struct TCC1014_private *)gimep;
	gime->SAM_register = val;
	update_from_sam_register(gime);
}

static void update_from_sam_register(struct TCC1014_private *gime) {
	gime->TY = gime->SAM_register & 0x8000;
	gime->R1 = gime->SAM_register & 0x1000;
	gime->SAM_F = (gime->SAM_register >> 3) & 0x7f;
	gime->SAM_V = gime->SAM_register & 0x7;
	tcc1014_update_graphics_mode(gime);
}

void tcc1014_reset(struct TCC1014 *gimep) {
	struct TCC1014_private *gime = (struct TCC1014_private *)gimep;

	for (int i = 0; i < 16; i++) {
		tcc1014_set_register(gime, i, 0);
	}
	tcc1014_set_sam_register(gimep, 0);

	memset(gime->pixel_data, 0, sizeof(gime->pixel_data));
	gime->beam_pos = TCC1014_LEFT_BORDER_START;
	gime->frame = 0;
	gime->scanline = 0;
	gime->row = 0;
	gime->scanline_start = event_current_tick;
	gime->vmode = 0;
	gime->hs_fall_event.at_tick = event_current_tick + TCC1014_LINE_DURATION;
	event_queue(&MACHINE_EVENT_LIST, &gime->hs_fall_event);
	tcc1014_update_graphics_mode(gime);
	gime->vram_bit = 0;
	gime->lborder_remaining = gime->pLB;
	gime->vram_remaining = gime->is_32byte ? 32 : 16;
	gime->rborder_remaining = TCC1014_tRB;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void tcc1014_mem_cycle(void *sptr, _Bool RnW, uint16_t A) {
	struct TCC1014 *gimep = sptr;
	struct TCC1014_private *gime = (struct TCC1014_private *)gimep;

	gimep->S = 7;
	gimep->RAS = 0;

	// Address decoding

	if (A < 0x8000 || (gime->TY && A < 0xfe00)) {
		gimep->RAS = 1;
		if (gime->MMUEN) {
			unsigned bank = (A >> 13) | gime->TR;
			gimep->Z = gime->mmu_bank[bank] | (A & 0x1fff);
		} else {
			gimep->Z = 0x70000 | A;
		}

	} else if (A < 0xfe00) {
		if (!gime->MC1) {
			gimep->S = (A >= 0xc000) ? 1 : 0;
		} else {
			gimep->S = gime->MC0 ? 1 : 0;
		}

	} else if (A < 0xff00) {
		gimep->RAS = 1;
		if (gime->MC3 || !gime->MMUEN) {
			gimep->Z = 0x70000 | A;
		} else {
			gimep->Z = gime->mmu_bank[7] | (A & 0x1fff);
		}

	} else if (A < 0xff40) {
		gimep->S = 2;
		if (A == 0xff22 && !RnW) {
			// GIME snoops writes to $FF22
			if (gime->vmode_direction) {
				gime->vmode = *gimep->CPUD & 0xf8;
				tcc1014_update_graphics_mode(gime);
			}
		} else if (A == 0xff23 && !RnW) {
			// GIME snoops the data direction register too
			gime->vmode_direction = *gimep->CPUD & 0x04;
		}

	} else if (A < 0xff60) {
		if (gime->MC2 || A >= 0xff50) {
			gimep->S = 6;
		}

	} else if (A < 0xff90) {
		// NOP

	} else if (A < 0xffa0) {
		if (!RnW) {
			tcc1014_set_register(gime, A & 15, *gimep->CPUD);
		} else if (A < 0xff98) {
			if (A == 0xff92) {
				*gimep->CPUD = (*gimep->CPUD & ~0x3f) | gime->irq_state;
				gime->irq_state = 0;
			} else if (A == 0xff93) {
				*gimep->CPUD = (*gimep->CPUD & ~0x3f) | gime->firq_state;
				gime->firq_state = 0;
			} else {
				*gimep->CPUD = gime->registers[A & 0xf];
			}
		}

	} else if (A < 0xffb0) {
		if (!RnW) {
			gime->mmu_bank[A & 15] = (*gimep->CPUD & 0x3f) << 13;
		} else {
			*gimep->CPUD = (*gimep->CPUD & ~0x3f) | (gime->mmu_bank[A & 15] >> 13);
		}

	} else if (A < 0xffc0) {
		if (!RnW) {
			if (gime->frame == 0 && gime->vstate == tcc1014_vstate_active_area) {
				render_scanline(gime);
			}
			gime->palette_reg[A & 15] = *gimep->CPUD & 0x3f;
		} else {
			*gimep->CPUD = (*gimep->CPUD & ~0x3f) | gime->palette_reg[A & 15];
		}

	} else if (A < 0xffe0) {
		if (!RnW) {
			unsigned b = 1 << ((A >> 1) & 0x0f);
			if (A & 1) {
				gime->SAM_register |= b;
			} else {
				gime->SAM_register &= ~b;
			}
			update_from_sam_register(gime);
		}

	} else {
		gimep->S = 0;
	}

	// Interrupts based on external inputs.  This also updates IRQ/FIRQ
	// outputs based on enable registers which may have been changed.
	unsigned set_int = (gimep->IL1 ? 0x02 : 0) | (gimep->IL0 ? 0x01 : 0);
	SET_INTERRUPT(gime, set_int);

	int ncycles = gime->R1 ? 8 : 16;
	DELEGATE_CALL(gimep->cpu_cycle, ncycles, RnW, A);
}

static void schedule_timer(struct TCC1014_private *gime) {
	if (gime->TINS && gime->timer_counter > 0) {
		// TINS=1: 3.58MHz
		gime->timer_tick_base = event_current_tick;
		gime->timer_event.at_tick = event_current_tick + (gime->timer_counter << 2);
		event_queue(&MACHINE_EVENT_LIST, &gime->timer_event);
	} else {
		event_dequeue(&gime->timer_event);
	}
}

static void update_timer(void *sptr) {
	struct TCC1014_private *gime = sptr;
	if (gime->TINS) {
		// TINS=1: 3.58MHz
		int elapsed = (event_current_tick - gime->timer_tick_base) >> 2;
		gime->timer_counter -= elapsed;
	}
	if (gime->timer_counter <= 0) {
		gime->blink = !gime->blink;
		unsigned timer_reset = ((gime->registers[4] & 0x0f) << 8) | gime->registers[5];
		gime->timer_counter = timer_reset + gime->timer_offset;
		schedule_timer(gime);
		SET_INTERRUPT(gime, 0x20);
	}
}

static void tcc1014_set_register(struct TCC1014_private *gime, unsigned reg, unsigned val) {
	if (gime->frame == 0 && gime->vstate == tcc1014_vstate_active_area) {
		render_scanline(gime);
	}
	reg &= 15;
	gime->registers[reg] = val;
	switch (reg) {
	case 0:
		gime->COCO = val & 0x80;
		gime->MMUEN = val & 0x40;
		gime->MC3 = val & 0x08;
		gime->MC2 = val & 0x04;
		gime->MC1 = val & 0x02;
		gime->MC0 = val & 0x01;
		LOG_DEBUG(3, "GIME INIT0: COCO=%d MMUEN=%d IEN=%d FEN=%d MC3=%d MC2=%d MC1/0=%d\n", (val>>7)&1, (val>>6)&1, (val>>5)&1, (val>>4)&1, (val>>3)&1,(val>>2)&1,val&3);
		tcc1014_update_graphics_mode(gime);
		break;

	case 1:
		update_timer(gime);
		gime->TINS = val & 0x20;
		gime->TR = (val & 0x01) ? 8 : 0;
		LOG_DEBUG(3, "GIME INIT1: MTYP=%d TINS=%d TR=%d\n", (val>>6)&1, (val>>5)&1, val&1);
		schedule_timer(gime);
		break;

	case 2:
		LOG_DEBUG(3, "GIME IRQ:   TMR=%d HBORD=%d VBORD=%d SER=%d KBD=%d CART=%d\n", (val>>5)&1, (val>>4)&1, (val>>3)&1, (val>>2)&1, (val>>1)&1, val&1);
		break;

	case 3:
		LOG_DEBUG(3, "GIME FIRQ:  TMR=%d HBORD=%d VBORD=%d SER=%d KBD=%d CART=%d\n", (val>>5)&1, (val>>4)&1, (val>>3)&1, (val>>2)&1, (val>>1)&1, val&1);
		break;

	case 4:
		{
			// Timer MSB
			unsigned timer_reset = ((gime->registers[4] & 0x0f) << 8) | gime->registers[5];
			gime->timer_counter = timer_reset + gime->timer_offset;
			schedule_timer(gime);
		}
		LOG_DEBUG(3, "GIME TMRH:  TIMER=%d\n", (val<<8)|gime->registers[5]);
		break;

	case 5:
		// Timer LSB
		LOG_DEBUG(3, "GIME TMRL:  TIMER=%d\n", (gime->registers[4]<<8)|val);
		break;

	case 8:
		gime->BP = val & 0x80;
		gime->BPI = val & 0x20;
		gime->MOCH = val & 0x10;
		gime->H50 = val & 0x08;
		gime->LPR = VMODE_LPR[val & 7];
		gime->field_duration = gime->H50 ? 312 : 262;
		LOG_DEBUG(3, "GIME VMODE: BP=%d BPI=%d MOCH=%d H50=%d (l=%d) LPR=%d (%d)\n", (val&0x80)?1:0, (val&0x20)?1:0, (val&0x10)?1:0, (val&8)?1:0, gime->field_duration, val&7, gime->LPR);
		tcc1014_update_graphics_mode(gime);
		break;

	case 9:
		gime->LPF = (val >> 5) & 3;
		gime->HRES = (val >> 2) & 7;
		gime->CRES = val & 3;
		gime->lAA = VRES_LPF_lAA[gime->LPF];
		gime->lTB = VRES_LPF_lTB[gime->LPF];
		gime->pLB = VRES_HRES_pLB[gime->HRES & 1];
		tcc1014_update_graphics_mode(gime);
		LOG_DEBUG(3, "GIME VRES:  LPF=%d (lTB=%d lAA=%d) HRES=%d CRES=%d\n", (val>>5)&3, gime->lTB, gime->lAA, (val>>2)&7, val&3);
		tcc1014_update_graphics_mode(gime);
		break;

	case 0xa:
		gime->BRDR = val;
		tcc1014_update_graphics_mode(gime);
		break;

	case 0xc:
		gime->VSC = val & 15;
		LOG_DEBUG(3, "GIME VSC:   VSC=%d\n", val&15);
		tcc1014_update_graphics_mode(gime);
		break;

	case 0xd:
		gime->Y = (val << 11) | (gime->registers[0xe] << 3);
		LOG_DEBUG(3, "GIME VOFFh: VOFF=%05x\n", (val<<11)|(gime->registers[0xe]<<3));
		break;

	case 0xe:
		gime->Y = (gime->registers[0xd] << 11) | (val << 3);
		LOG_DEBUG(3, "GIME VOFFl: VOFF=%05x\n", (gime->registers[0xd]<<11)|(val<<3));
		break;

	case 0xf:
		gime->HVEN = val & 0x80;
		gime->X = (val & 0x7f) << 1;
		LOG_DEBUG(3, "GIME HOFF:  HVEN=%d X=%d\n", gime->HVEN, gime->X);
		tcc1014_update_graphics_mode(gime);
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void do_hs_fall(void *sptr) {
	struct TCC1014_private *gime = sptr;
	// Finish rendering previous scanline
	if (gime->frame == 0) {
		if (gime->vstate == tcc1014_vstate_active_area) {
			render_scanline(gime);
			gime->row++;
			if (gime->row >= gime->nLPR) {
				gime->row = 0;
				if (gime->HVEN) {
					gime->line_base += 256;
				} else {
					gime->line_base = gime->B;
				}
			}
			gime->B = gime->line_base;
		}
		gime->beam_pos = TCC1014_LEFT_BORDER_START;
		// Total bodge to fix PAL display!  I think really we need the
		// video module to know (either inferring or being told) that
		// the signal is PAL.
		if (!gime->H50 || gime->scanline > 25) {
			DELEGATE_CALL(gime->public.render_line, gime->pixel_data, gime->BPI);
		}
	}

	// HS falling edge.
	DELEGATE_CALL(gime->public.signal_hs, 0);

	gime->scanline_start = gime->hs_fall_event.at_tick;
	// Next HS rise and fall
	gime->hs_rise_event.at_tick = gime->scanline_start + TCC1014_HS_RISING_EDGE;
	gime->hs_fall_event.at_tick = gime->scanline_start + TCC1014_LINE_DURATION;
	gime->hs_border_event.at_tick = gime->scanline_start + VRES_HRES_pBRD[gime->HRES & 1];

	event_queue(&MACHINE_EVENT_LIST, &gime->hs_rise_event);
	event_queue(&MACHINE_EVENT_LIST, &gime->hs_fall_event);
	event_queue(&MACHINE_EVENT_LIST, &gime->hs_border_event);

	// Next scanline
	gime->vram_nbytes = 0;
	gime->vram_bit = 0;
	gime->lborder_remaining = gime->pLB;
	if (gime->COCO) {
		gime->vram_remaining = gime->is_32byte ? 32 : 16;
	} else if (gime->BP) {
		gime->vram_remaining = VRES_HRES_BPR[gime->HRES];
	} else {
		gime->vram_remaining = VRES_HRES_BPR_TEXT[gime->HRES];
	}
	gime->rborder_remaining = TCC1014_tRB;
	gime->scanline++;
	gime->lcount++;

	// Always check against field duration - could hit this during active
	// area or bottom border.
	if (gime->scanline == gime->field_duration - 4) {
		// FS falling edge
		DELEGATE_CALL(gime->public.signal_fs, 0);
		// clear output line to...  well should be vsync signal, but use black
		memset(gime->pixel_data, 0, sizeof(gime->pixel_data));
	} else if (gime->scanline == gime->field_duration) {
		// FS rising edge
		DELEGATE_CALL(gime->public.signal_fs, 1);
		gime->B = gime->Y;
		if (gime->COCO) {
			gime->B = (gime->B & 0x701ff) | (gime->SAM_F << 9);
		}
		gime->line_base = gime->B;
		gime->vstate = tcc1014_vstate_vblank;
		gime->lcount = 0;
		gime->scanline = 0;
	} else switch (gime->vstate) {
	case tcc1014_vstate_vblank:
		if (gime->lcount >= TCC1014_TOP_BORDER_START) {
			gime->lcount = 0;
			gime->vstate = tcc1014_vstate_top_border;
		}
		break;
	case tcc1014_vstate_top_border:
		memset(gime->pixel_data, gime->border_colour, sizeof(gime->pixel_data));
		if (gime->lcount >= gime->nTB) {
			gime->row = gime->COCO ? 0 : gime->VSC;
			gime->lcount = 0;
			gime->vstate = tcc1014_vstate_active_area;
		}
		break;
	case tcc1014_vstate_active_area:
		if (gime->lcount >= gime->nAA) {
			gime->lcount = 0;
			gime->vstate = tcc1014_vstate_bottom_border;
		}
		break;
	case tcc1014_vstate_bottom_border:
		memset(gime->pixel_data, gime->border_colour, sizeof(gime->pixel_data));
		break;
	default:
		break;
	}

}

static void do_hs_rise(void *sptr) {
	struct TCC1014_private *gime = sptr;
	// HS rising edge.
	DELEGATE_CALL(gime->public.signal_hs, 1);
}

static void do_hs_border(void *sptr) {
	struct TCC1014_private *gime = sptr;
	// Horizontal border.
	SET_INTERRUPT(gime, 0x10);
	if (!gime->TINS && gime->timer_counter > 0) {
		// TINS=0: 15.7kHz
		gime->timer_counter--;
		if (gime->timer_counter <= 0) {
			update_timer(gime);
		}
	}
	if (gime->vstate == tcc1014_vstate_active_area && gime->lcount == gime->nAA-1) {
		SET_INTERRUPT(gime, 0x08);
	}
}

static uint8_t fetch_byte_vram(struct TCC1014_private *gime) {
	// X offset appears to be dynamically added to current video address
	return DELEGATE_CALL(gime->public.fetch_vram, gime->X + gime->B++);
}

static void render_scanline(struct TCC1014_private *gime) {
	unsigned beam_to = event_current_tick - gime->scanline_start;
	if (beam_to < TCC1014_LEFT_BORDER_START)
		return;

	if (gime->beam_pos >= beam_to)
		return;
	uint8_t *pixel = gime->pixel_data + gime->beam_pos;

	while (gime->lborder_remaining > 0) {
		*(pixel++) = gime->border_colour;
		*(pixel++) = gime->border_colour;
		gime->beam_pos += 2;
		gime->lborder_remaining -= 2;
		if (gime->beam_pos >= beam_to)
			return;
	}

	while (gime->vram_remaining > 0) {
		if (gime->vram_bit == 0) {
			uint8_t vdata = fetch_byte_vram(gime);
			gime->vram_bit = 8;
			gime->vram_g_data = vdata;

			if (gime->COCO) {
				gime->SnA = vdata & 0x80;

				gime->cg_colours = !gime->CSS ? TCC1014_GREEN : TCC1014_WHITE;
				gime->text_border_colour = !gime->CSS ? TCC1014_GREEN : TCC1014_ORANGE;

				if (!gime->GnA && !gime->SnA) {
					_Bool INV = vdata & 0x40;
					INV ^= gime->inverse_text;
					int c = gime->vram_g_data & 0x3f;
					if (c < 0x20)
						c |= 0x40;
					gime->vram_g_data = font_gime[c*12+gime->row];
					if (INV ^ gime->inverted_text)
						gime->vram_g_data = ~gime->vram_g_data;
				}

				if (!gime->GnA && gime->SnA) {
					gime->vram_sg_data = gime->vram_g_data;
					if (gime->row < 6)
						gime->vram_sg_data >>= 2;
					gime->s_fg_colour = (gime->vram_g_data >> 4) & 7;
					gime->s_bg_colour = TCC1014_RGCSS0_0;
					gime->vram_sg_data = ((gime->vram_sg_data & 2) ? 0xf0 : 0) | ((gime->vram_sg_data & 1) ? 0x0f : 0);
				}

				if (!gime->GnA) {
					gime->render_mode = !gime->SnA ? TCC1014_RENDER_RG : TCC1014_RENDER_SG;
					gime->fg_colour = gime->CSS ? TCC1014_BRIGHT_ORANGE : TCC1014_BRIGHT_GREEN;
					gime->bg_colour = gime->CSS ? TCC1014_DARK_ORANGE : TCC1014_DARK_GREEN;
				} else {
					gime->render_mode = gime->GM0 ? TCC1014_RENDER_RG : TCC1014_RENDER_CG;
					gime->fg_colour = gime->CSS ? TCC1014_RGCSS1_1 : TCC1014_RGCSS0_1;
					gime->bg_colour = gime->CSS ? TCC1014_RGCSS1_0 : TCC1014_RGCSS0_0;
				}
			} else {
				// CoCo 3 mode
				if (gime->BP) {
					// CoCo 3 graphics

				} else {
					// CoCo 3 text
					uint8_t attr = fetch_byte_vram(gime);
					gime->attr_blink = attr & 0x80;
					gime->attr_undln = attr & 0x40;
					gime->attr_fgnd = 8 | ((attr >> 3) & 7);
					gime->attr_bgnd = attr & 7;
					if ((attr & 0x80) && gime->blink)
						gime->attr_fgnd = gime->attr_bgnd;
					int c = gime->vram_g_data & 0x7f;
					//if (c < 0x20)
						//c |= 0x40;
					gime->vram_g_data = font_gime[c*12+gime->row+1];
					if (gime->attr_undln && (gime->row+1) == gime->LPR)
						gime->vram_g_data = 0xff;
					gime->render_mode = TCC1014_RENDER_RG;
					gime->fg_colour = 13;
					gime->bg_colour = 12;
				}
			}
		}

		uint8_t c0, c1, c2, c3;
		unsigned HRES;

		if (gime->COCO) {
			// CoCo 2 modes
			switch (gime->render_mode) {
			case TCC1014_RENDER_SG: default:
				c0 = gime->palette_reg[(gime->vram_sg_data&0x80) ? gime->s_fg_colour : gime->s_bg_colour];
				c1 = gime->palette_reg[(gime->vram_sg_data&0x40) ? gime->s_fg_colour : gime->s_bg_colour];
				c2 = gime->palette_reg[(gime->vram_sg_data&0x20) ? gime->s_fg_colour : gime->s_bg_colour];
				c3 = gime->palette_reg[(gime->vram_sg_data&0x10) ? gime->s_fg_colour : gime->s_bg_colour];
				break;
			case TCC1014_RENDER_CG:
				c0 = c1 = gime->palette_reg[gime->cg_colours + ((gime->vram_g_data >> 6) & 3)];
				c2 = c3 = gime->palette_reg[gime->cg_colours + ((gime->vram_g_data >> 4) & 3)];
				break;
			case TCC1014_RENDER_RG:
				c0 = gime->palette_reg[(gime->vram_g_data&0x80) ? gime->fg_colour : gime->bg_colour];
				c1 = gime->palette_reg[(gime->vram_g_data&0x40) ? gime->fg_colour : gime->bg_colour];
				c2 = gime->palette_reg[(gime->vram_g_data&0x20) ? gime->fg_colour : gime->bg_colour];
				c3 = gime->palette_reg[(gime->vram_g_data&0x10) ? gime->fg_colour : gime->bg_colour];
				break;
			}
			gime->vram_bit -= 4;
			gime->vram_g_data <<= 4;
			gime->vram_sg_data <<= 4;
			if (gime->is_32byte) {
				HRES = 2;
			} else {
				HRES = 0;
			}

		} else {
			// CoCo 3 modes
			uint8_t vdata = gime->vram_g_data;
			if (gime->BP) {
				HRES = gime->HRES;
				switch (gime->CRES) {
				case 0: default:
					c0 = gime->palette_reg[(vdata>>7)&1];
					c1 = gime->palette_reg[(vdata>>6)&1];
					c2 = gime->palette_reg[(vdata>>5)&1];
					c3 = gime->palette_reg[(vdata>>4)&1];
					break;

				case 1:
					c0 = c1 = gime->palette_reg[(vdata>>6)&3];
					c2 = c3 = gime->palette_reg[(vdata>>4)&3];
					break;

				case 2: case 3:
					c0 = c1 = c2 = c3 = gime->palette_reg[(vdata>>4)&15];
					break;
				}

			} else {
				HRES = (gime->HRES & 4) ? 4 : 2;
				if (gime->CRES & 1) {
					c0 = gime->palette_reg[(vdata&0x80)?gime->attr_fgnd:gime->attr_bgnd];
					c1 = gime->palette_reg[(vdata&0x40)?gime->attr_fgnd:gime->attr_bgnd];
					c2 = gime->palette_reg[(vdata&0x20)?gime->attr_fgnd:gime->attr_bgnd];
					c3 = gime->palette_reg[(vdata&0x10)?gime->attr_fgnd:gime->attr_bgnd];
				} else {
					c0 = gime->palette_reg[(vdata&0x80)?gime->fg_colour:gime->bg_colour];
					c1 = gime->palette_reg[(vdata&0x40)?gime->fg_colour:gime->bg_colour];
					c2 = gime->palette_reg[(vdata&0x20)?gime->fg_colour:gime->bg_colour];
					c3 = gime->palette_reg[(vdata&0x10)?gime->fg_colour:gime->bg_colour];
				}
			}
			gime->vram_bit -= 4;
			gime->vram_g_data <<= 4;
		}

		// Render appropriate number of pixels
		switch (HRES) {
		case 0: case 1:
			*(pixel) = c0;
			*(pixel+1) = c0;
			*(pixel+2) = c0;
			*(pixel+3) = c0;
			*(pixel+4) = c1;
			*(pixel+5) = c1;
			*(pixel+6) = c1;
			*(pixel+7) = c1;
			*(pixel+8) = c2;
			*(pixel+9) = c2;
			*(pixel+10) = c2;
			*(pixel+11) = c2;
			*(pixel+12) = c3;
			*(pixel+13) = c3;
			*(pixel+14) = c3;
			*(pixel+15) = c3;
			pixel += 16;
			gime->beam_pos += 16;
			break;

		case 2: case 3:
			*(pixel) = c0;
			*(pixel+1) = c0;
			*(pixel+2) = c1;
			*(pixel+3) = c1;
			*(pixel+4) = c2;
			*(pixel+5) = c2;
			*(pixel+6) = c3;
			*(pixel+7) = c3;
			pixel += 8;
			gime->beam_pos += 8;
			break;

		case 4: case 5:
			*(pixel) = c0;
			*(pixel+1) = c1;
			*(pixel+2) = c2;
			*(pixel+3) = c3;
			pixel += 4;
			gime->beam_pos += 4;
			break;

		case 6: case 7:
			*(pixel) = c0;
			*(pixel+1) = c2;
			pixel += 2;
			gime->beam_pos += 2;
			break;
		}

		if (gime->vram_bit == 0) {
			gime->vram_remaining--;
		}
		if (gime->beam_pos >= beam_to)
			return;
	}

	while (gime->rborder_remaining > 0) {
		if (gime->beam_pos == TCC1014_RIGHT_BORDER_START) {
			gime->text_border_colour = !gime->CSS ? TCC1014_GREEN : TCC1014_ORANGE;
		}
		*(pixel++) = gime->border_colour;
		*(pixel++) = gime->border_colour;
		gime->beam_pos += 2;
		gime->rborder_remaining -= 2;
		if (gime->beam_pos >= beam_to)
			return;
	}

	// If a program switches to 32 bytes per line mid-scanline, the whole
	// scanline might not have been rendered:
	while (gime->beam_pos < TCC1014_RIGHT_BORDER_END) {
		*(pixel++) = 0;
		gime->beam_pos++;
	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void tcc1014_set_palette(struct TCC1014 *gimep) {
	struct TCC1014_private *gime = (struct TCC1014_private *)gimep;
	// clear the pixel buffer, as the way its data so far is interpreted
	// might change, and go out of bounds
	memset(gime->pixel_data, 0, sizeof(gime->pixel_data));
}

void tcc1014_set_inverted_text(struct TCC1014 *gimep, _Bool invert) {
	struct TCC1014_private *gime = (struct TCC1014_private *)gimep;
	gime->inverted_text = invert;
}

static void tcc1014_update_graphics_mode(struct TCC1014_private *gime) {
	/* Render scanline so far before changing modes */
	if (gime->frame == 0 && gime->vstate == tcc1014_vstate_active_area) {
		render_scanline(gime);
	}

	gime->GnA = gime->vmode & 0x80;
	gime->CSS = gime->vmode & 0x08;
	gime->GM = (gime->vmode >> 4) & 7;

	if (gime->COCO) {
		gime->nTB = 25 + (gime->H50 ? 25 : 0);
		gime->nAA = 192;
		gime->nLB = 120 + (gime->H50 ? 25 : 0);
		if (gime->GnA) {
			gime->nLPR = SAM_V_nLPR[gime->SAM_V];
		} else {
			gime->nLPR = VSC_nLPR[gime->VSC];
		}

	} else {
		gime->nTB = gime->lTB + (gime->H50 ? 25 : 0);
		gime->nAA = gime->lAA;
		gime->nLB = gime->pLB + (gime->H50 ? 25 : 0);
		gime->nLPR = gime->LPR;
	}

	gime->GM0 = gime->GM & 1;

	gime->inverse_text = gime->GM & 2;
	gime->text_border = !gime->inverse_text && (gime->GM & 4);
	gime->text_border_colour = !gime->CSS ? TCC1014_GREEN : TCC1014_ORANGE;

	if (gime->COCO) {
		if (!gime->GnA) {
			gime->render_mode = !gime->SnA ? TCC1014_RENDER_RG : TCC1014_RENDER_SG;
			gime->fg_colour = gime->CSS ? TCC1014_BRIGHT_ORANGE : TCC1014_BRIGHT_GREEN;
			gime->bg_colour = gime->CSS ? TCC1014_DARK_ORANGE : TCC1014_DARK_GREEN;
			gime->border_colour = 0;
		} else {
			gime->render_mode = gime->GM0 ? TCC1014_RENDER_RG : TCC1014_RENDER_CG;
			gime->fg_colour = gime->CSS ? TCC1014_RGCSS1_1 : TCC1014_RGCSS0_1;
			gime->bg_colour = gime->CSS ? TCC1014_RGCSS1_0 : TCC1014_RGCSS0_0;
			gime->border_colour = gime->palette_reg[gime->cg_colours];
		}
	} else {
		gime->render_mode = TCC1014_RENDER_RG;
		gime->border_colour = gime->BRDR;
	}

	gime->is_32byte = !gime->GnA || !(gime->GM == 0 || (gime->GM0 && gime->GM != 7));
}
