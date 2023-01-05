/** \file
 *
 *  \brief TCC1014 (GIME) support.
 *
 *  \copyright Copyright 2003-2023 Ciaran Anscomb
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
 *  \par Sources
 *  Sock's GIME register reference https://www.6809.org.uk/sock/gime.html
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

#include "top-config.h"

// Comment this out for debugging
#define GIME_DEBUG(...)

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "xalloc.h"

#include "delegate.h"
#include "events.h"
#include "logging.h"
#include "machine.h"
#include "part.h"
#include "serialise.h"
#include "tcc1014/font-gime.h"
#include "tcc1014/tcc1014.h"
#include "vo.h"
#include "xroar.h"

#ifndef GIME_DEBUG
#define GIME_DEBUG(...) LOG_PRINT(__VA_ARGS__)
#endif

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
	tcc1014_vstate_vsync,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct TCC1014_private {
	struct TCC1014 public;

	// Variant
	_Bool is_1986;

	// Timing
	struct event hs_fall_event;
	struct event hs_rise_event;
	struct event hs_border_event;
	struct event fs_fall_event;
	struct event fs_rise_event;
	event_ticks scanline_start;
	unsigned beam_pos;
	unsigned scanline;

	// Timer
	struct event timer_event;
	event_ticks timer_tick_base;
	int timer_counter;
	int timer_offset;  // 2 for 1986 GIME, 1 for 1987 GIME

	// Data
	uint8_t vram_g_data;
	uint8_t vram_sg_data;

	// Output
	int frame;  // frameskip counter

	// $FF22: PIA1B video control lines
	// XXX there may be a need for latch propagation as with the VDG, but
	// for now assume that VDG-compatible modes are simulated in a basic
	// fashion.
	_Bool vmode_direction;  // snooped direction register
	unsigned vmode;  // snooped data register (mode bits only)
	_Bool GnA;
	_Bool GM1;
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
	uint32_t B;  // Current VRAM address
	unsigned row;  // 0 <= row < nLPR
	unsigned Xoff;

	// Video resolution
	unsigned BPR;  // bytes per row
	unsigned row_stride;  // may be different from BPR
	unsigned resolution;  // horizontal resolution

	// Video timing
	unsigned field_duration;  // 312 (PAL) or 262 (NTSC)
	unsigned lTB;  // Top Border lines, from VRES
	unsigned lAA;  // Active Area lines, from VRES
	unsigned pVSYNC;  // Time between hsync fall and vsync fall/rise
	unsigned pLB;  // Left Border pixels, from VRES
	unsigned pRB;  // Right Border pixels, from VRES

	// Video state
	unsigned vstate;
	unsigned post_vblank_vstate;
	unsigned nTB;  // Top Border, from lTB or COCO
	unsigned nAA;  // Active Area, from lAA or COCO
	unsigned nLB;  // Left Border, from pLB or COCO
	unsigned nLPR;  // Lines Per Row, from LPR or COCO
	unsigned lcount;  // General scanline counter
	unsigned attr_fgnd;  // Text fg colour
	unsigned attr_bgnd;  // Text bg colour

	uint8_t border_colour;

	// Internal state
	_Bool SnA;
	uint8_t s_fg_colour;
	uint8_t s_bg_colour;
	uint8_t fg_colour;
	uint8_t bg_colour;
	uint8_t cg_colours;
	int vram_bit;
	enum vdg_render_mode render_mode;
	_Bool blink;

	// Unsafe warning: pixel_data[] *may* need to be 16 elements longer
	// than a full scanline.  16 is the maximum number of elements rendered
	// in render_scanline() between index checks.
	uint8_t pixel_data[TCC1014_LINE_DURATION+16];

	// Counters
	unsigned lborder_remaining;
	unsigned vram_remaining;
	unsigned rborder_remaining;
};

#define TCC1014_SER_REGISTERS   (24)
#define TCC1014_SER_MMU_BANKS   (25)
#define TCC1014_SER_PALETTE_REG (26)

static struct ser_struct ser_struct_tcc1014[] = {
	SER_ID_STRUCT_ELEM(1, ser_type_unsigned, struct TCC1014, S),
	SER_ID_STRUCT_ELEM(2, ser_type_uint32, struct TCC1014, Z),
	SER_ID_STRUCT_ELEM(3, ser_type_bool, struct TCC1014, RAS),

	SER_ID_STRUCT_ELEM(4, ser_type_bool, struct TCC1014, FIRQ),
	SER_ID_STRUCT_ELEM(5, ser_type_bool, struct TCC1014, IRQ),

	SER_ID_STRUCT_ELEM(6, ser_type_bool, struct TCC1014, IL0),
	SER_ID_STRUCT_ELEM(7, ser_type_bool, struct TCC1014, IL1),
	SER_ID_STRUCT_ELEM(8, ser_type_bool, struct TCC1014, IL2),

	SER_ID_STRUCT_ELEM(9, ser_type_event, struct TCC1014_private, hs_fall_event),
	SER_ID_STRUCT_ELEM(10, ser_type_event, struct TCC1014_private, hs_rise_event),
	SER_ID_STRUCT_ELEM(11, ser_type_event, struct TCC1014_private, hs_border_event),
	SER_ID_STRUCT_ELEM(12, ser_type_event, struct TCC1014_private, fs_fall_event),
	SER_ID_STRUCT_ELEM(13, ser_type_event, struct TCC1014_private, fs_rise_event),
	SER_ID_STRUCT_ELEM(14, ser_type_tick, struct TCC1014_private, scanline_start),
	SER_ID_STRUCT_ELEM(15, ser_type_unsigned, struct TCC1014_private, beam_pos),
	SER_ID_STRUCT_ELEM(16, ser_type_unsigned, struct TCC1014_private, scanline),

	SER_ID_STRUCT_ELEM(17, ser_type_event, struct TCC1014_private, timer_event),
	SER_ID_STRUCT_ELEM(18, ser_type_tick, struct TCC1014_private, timer_tick_base),
	SER_ID_STRUCT_ELEM(19, ser_type_int, struct TCC1014_private, timer_counter),

	SER_ID_STRUCT_ELEM(20, ser_type_uint8, struct TCC1014_private, vram_g_data),
	SER_ID_STRUCT_ELEM(21, ser_type_uint8, struct TCC1014_private, vram_sg_data),

	SER_ID_STRUCT_ELEM(22, ser_type_bool, struct TCC1014_private, vmode_direction),
	SER_ID_STRUCT_ELEM(23, ser_type_unsigned, struct TCC1014_private, vmode),

	SER_ID_STRUCT_UNHANDLED(TCC1014_SER_REGISTERS),
	SER_ID_STRUCT_UNHANDLED(TCC1014_SER_MMU_BANKS),
	SER_ID_STRUCT_UNHANDLED(TCC1014_SER_PALETTE_REG),
	SER_ID_STRUCT_ELEM(27, ser_type_uint16, struct TCC1014_private, SAM_register),

	SER_ID_STRUCT_ELEM(28, ser_type_unsigned, struct TCC1014_private, irq_state),
	SER_ID_STRUCT_ELEM(29, ser_type_unsigned, struct TCC1014_private, firq_state),

	SER_ID_STRUCT_ELEM(30, ser_type_bool, struct TCC1014_private, inverted_text),

	SER_ID_STRUCT_ELEM(31, ser_type_uint32, struct TCC1014_private, B),
	SER_ID_STRUCT_ELEM(32, ser_type_unsigned, struct TCC1014_private, row),
	SER_ID_STRUCT_ELEM(33, ser_type_unsigned, struct TCC1014_private, Xoff),

	SER_ID_STRUCT_ELEM(34, ser_type_unsigned, struct TCC1014_private, field_duration),
	SER_ID_STRUCT_ELEM(35, ser_type_unsigned, struct TCC1014_private, lTB),
	SER_ID_STRUCT_ELEM(36, ser_type_unsigned, struct TCC1014_private, lAA),
	SER_ID_STRUCT_ELEM(37, ser_type_unsigned, struct TCC1014_private, pVSYNC),
	SER_ID_STRUCT_ELEM(38, ser_type_unsigned, struct TCC1014_private, pLB),
	SER_ID_STRUCT_ELEM(39, ser_type_unsigned, struct TCC1014_private, pRB),

	SER_ID_STRUCT_ELEM(40, ser_type_unsigned, struct TCC1014_private, vstate),
	SER_ID_STRUCT_ELEM(41, ser_type_unsigned, struct TCC1014_private, post_vblank_vstate),
	SER_ID_STRUCT_ELEM(42, ser_type_unsigned, struct TCC1014_private, lcount),
	SER_ID_STRUCT_ELEM(43, ser_type_unsigned, struct TCC1014_private, attr_fgnd),
	SER_ID_STRUCT_ELEM(44, ser_type_unsigned, struct TCC1014_private, attr_bgnd),

	SER_ID_STRUCT_ELEM(45, ser_type_bool, struct TCC1014_private, SnA),
	SER_ID_STRUCT_ELEM(46, ser_type_uint8, struct TCC1014_private, s_fg_colour),
	SER_ID_STRUCT_ELEM(47, ser_type_uint8, struct TCC1014_private, s_bg_colour),
	SER_ID_STRUCT_ELEM(48, ser_type_int, struct TCC1014_private, vram_bit),
	SER_ID_STRUCT_ELEM(49, ser_type_bool, struct TCC1014_private, blink),

	SER_ID_STRUCT_ELEM(50, ser_type_unsigned, struct TCC1014_private, lborder_remaining),
	SER_ID_STRUCT_ELEM(51, ser_type_unsigned, struct TCC1014_private, vram_remaining),
	SER_ID_STRUCT_ELEM(52, ser_type_unsigned, struct TCC1014_private, rborder_remaining),

	SER_ID_STRUCT_ELEM(53, ser_type_bool, struct TCC1014_private, is_1986),
};

static _Bool tcc1014_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool tcc1014_write_elem(void *sptr, struct ser_handle *sh, int tag);

const struct ser_struct_data tcc1014_ser_struct_data = {
	.elems = ser_struct_tcc1014,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_tcc1014),
	.read_elem = tcc1014_read_elem,
	.write_elem = tcc1014_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Lines of top border.  Varies by mode and 50Hz/60Hz selection.  The
// transition to "infinite" lines is handled specially.  Measured.
static const unsigned VRES_LPF_lTB[2][4] = {
	{ 36, 34, 65535, 19 },
	{ 63, 59, 65535, 46 } };
static const unsigned VRES_LPF_lAA[4] = { 192, 200, 65535, 225 };
// I could have sworn I saw 201 lines on the scope, but that introduces
// glitching so back to 200 until I figure out what's going on.

// Time from HSYNC fall to VSYNC fall.  Varies by 32/40 mode.  Measured.
static const unsigned VRES_HRES_pVSYNC[2] = { 225, 161 };

// Left border duration.  Varies by 32/40 mode.  Measured.
static const unsigned VRES_HRES_pLB[2] = { 108, 44 };

// Right border duration similar.  Measured.
static const unsigned VRES_HRES_pRB[2] = { 124, 60 };

// Time from HSYNC fall to horizontal border interrupt.  32/40.  Measured.
static const unsigned VRES_HRES_pBRD[2] = { 760, 824 };

static const unsigned LPR_nLPR[8] = { 1, 1, 2, 8, 9, 10, 11, 65535 };
static const unsigned VSC_nLPR[16] = { 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 4, 3, 2, 1, 12 };
static const unsigned SAM_V_nLPR[8] = { 12, 1, 3, 2, 2, 1, 1, 1 };
static const unsigned VRES_HRES_BPR[8] = { 16, 20, 32, 40, 64, 80, 128, 160 };
static const unsigned VRES_HRES_BPR_TEXT[8] = { 32, 40, 32, 40, 64, 80, 64, 80 };
static const unsigned LPR_rowmask_TEXT[8] = { 0, 1, 2, 8, 9, 10, 11, 16 };

static void tcc1014_set_register(struct TCC1014_private *gime, unsigned reg, unsigned val);
static void update_from_sam_register(struct TCC1014_private *gime);

static void do_hs_fall(void *);
static void do_hs_rise(void *);
static void do_hs_border(void *);
static void do_fs_fall(void *);
static void do_fs_rise(void *);
static void update_timer(void *);
static void render_scanline(struct TCC1014_private *gime);
static void tcc1014_update_graphics_mode(struct TCC1014_private *gime);

#define SET_INTERRUPT(g,v) do { \
		(g)->irq_state |= ((v) & (g)->registers[2]); \
		(g)->firq_state |= ((v) & (g)->registers[3]); \
		(g)->public.IRQ = ((g)->registers[0] & 0x20) ? ((g)->irq_state & 0x3f) : 0; \
		(g)->public.FIRQ = ((g)->registers[0] & 0x10) ? ((g)->firq_state & 0x3f) : 0; \
	} while (0)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// TCC1014/GIME part creation

static struct part *tcc1014_allocate(void);
static void tcc1014_initialise(struct part *p, void *options);
static _Bool tcc1014_finish(struct part *p);
static void tcc1014_free(struct part *p);

static _Bool tcc1014_is_a(struct part *p, const char *name);

static const struct partdb_entry_funcs tcc1014_funcs = {
        .allocate = tcc1014_allocate,
        .initialise = tcc1014_initialise,
        .finish = tcc1014_finish,
        .free = tcc1014_free,

        .ser_struct_data = &tcc1014_ser_struct_data,

	.is_a = tcc1014_is_a,
};

const struct partdb_entry tcc1014_1986_part = { .name = "TCC1014-1986", .funcs = &tcc1014_funcs };
const struct partdb_entry tcc1014_1987_part = { .name = "TCC1014-1987", .funcs = &tcc1014_funcs };

static struct part *tcc1014_allocate(void) {
	struct TCC1014_private *gime = part_new(sizeof(*gime));
	struct part *p = &gime->public.part;

	*gime = (struct TCC1014_private){0};

	gime->B = 0x60400;
	gime->beam_pos = TCC1014_LEFT_BORDER_START;
	gime->public.cpu_cycle = DELEGATE_DEFAULT3(void, int, bool, uint16);
	gime->public.fetch_vram = DELEGATE_DEFAULT1(uint8, uint32);
	gime->public.signal_hs = DELEGATE_DEFAULT1(void, bool);
	gime->public.signal_fs = DELEGATE_DEFAULT1(void, bool);
	event_init(&gime->hs_fall_event, DELEGATE_AS0(void, do_hs_fall, gime));
	event_init(&gime->hs_rise_event, DELEGATE_AS0(void, do_hs_rise, gime));
	event_init(&gime->hs_border_event, DELEGATE_AS0(void, do_hs_border, gime));
	event_init(&gime->fs_fall_event, DELEGATE_AS0(void, do_fs_fall, gime));
	event_init(&gime->fs_rise_event, DELEGATE_AS0(void, do_fs_rise, gime));
	event_init(&gime->timer_event, DELEGATE_AS0(void, update_timer, gime));

	return p;
}

static void tcc1014_initialise(struct part *p, void *options) {
	struct TCC1014_private *gime = (struct TCC1014_private *)p;
	gime->is_1986 = options && (strcmp((char *)options, "TCC1014-1986") == 0);
}

static _Bool tcc1014_finish(struct part *p) {
	struct TCC1014_private *gime = (struct TCC1014_private *)p;

	gime->timer_offset = gime->is_1986 ? 2 : 1;

	if (gime->hs_fall_event.next == &gime->hs_fall_event)
		event_queue(&MACHINE_EVENT_LIST, &gime->hs_fall_event);
	if (gime->hs_rise_event.next == &gime->hs_rise_event)
		event_queue(&MACHINE_EVENT_LIST, &gime->hs_rise_event);
	if (gime->hs_border_event.next == &gime->hs_border_event)
		event_queue(&MACHINE_EVENT_LIST, &gime->hs_border_event);
	if (gime->fs_fall_event.next == &gime->fs_fall_event)
		event_queue(&MACHINE_EVENT_LIST, &gime->fs_fall_event);
	if (gime->fs_rise_event.next == &gime->fs_rise_event)
		event_queue(&MACHINE_EVENT_LIST, &gime->fs_rise_event);
	if (gime->timer_event.next == &gime->timer_event)
		event_queue(&MACHINE_EVENT_LIST, &gime->timer_event);

	update_from_sam_register(gime);

	for (int i = 0; i < 16; i++) {
		tcc1014_set_register(gime, i, gime->registers[i]);
	}

	return 1;
}

void tcc1014_free(struct part *p) {
	struct TCC1014_private *gime = (struct TCC1014_private *)p;
	event_dequeue(&gime->timer_event);
	event_dequeue(&gime->fs_rise_event);
	event_dequeue(&gime->fs_fall_event);
	event_dequeue(&gime->hs_border_event);
	event_dequeue(&gime->hs_rise_event);
	event_dequeue(&gime->hs_fall_event);
}

static _Bool tcc1014_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct TCC1014_private *gime = sptr;
	switch (tag) {
	case TCC1014_SER_REGISTERS:
		ser_read(sh, gime->registers, sizeof(gime->registers));
		break;
	case TCC1014_SER_MMU_BANKS:
		for (int i = 0; i < 16; i++) {
			gime->mmu_bank[i] = ser_read_uint8(sh) << 13;
		}
		break;
	case TCC1014_SER_PALETTE_REG:
		ser_read(sh, gime->palette_reg, sizeof(gime->palette_reg));
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool tcc1014_write_elem(void *sptr, struct ser_handle *sh, int tag) {
        struct TCC1014_private *gime = sptr;
	switch (tag) {
	case TCC1014_SER_REGISTERS:
		ser_write(sh, tag, gime->registers, sizeof(gime->registers));
		break;
	case TCC1014_SER_MMU_BANKS:
		ser_write_tag(sh, tag, 16);
		for (int i = 0; i < 16; i++) {
			ser_write_uint8_untagged(sh, gime->mmu_bank[i] >> 13);
		}
		ser_write_close_tag(sh);
		break;
	case TCC1014_SER_PALETTE_REG:
		ser_write(sh, tag, gime->palette_reg, sizeof(gime->palette_reg));
		break;
	default:
		return 0;
	}
        return 1;
}

static _Bool tcc1014_is_a(struct part *p, const char *name) {
	(void)p;
	return strcmp(name, "TCC1014") == 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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
	gime->vram_remaining = 32;
	gime->rborder_remaining = gime->pRB;
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
		if ((A & 0x10) == 0) {
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
			} else if (A == 0xff94 || A == 0xff95) {
				*gimep->CPUD = 0;
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
		GIME_DEBUG("GIME INIT0: COCO=%d MMUEN=%d IEN=%d FEN=%d MC3=%d MC2=%d MC1/0=%d\n", (val>>7)&1, (val>>6)&1, (val>>5)&1, (val>>4)&1, (val>>3)&1,(val>>2)&1,val&3);
		tcc1014_update_graphics_mode(gime);
		break;

	case 1:
		update_timer(gime);
		gime->TINS = val & 0x20;
		gime->TR = (val & 0x01) ? 8 : 0;
		GIME_DEBUG("GIME INIT1: MTYP=%d TINS=%d TR=%d\n", (val>>6)&1, (val>>5)&1, val&1);
		schedule_timer(gime);
		break;

	case 2:
		GIME_DEBUG("GIME IRQ:   TMR=%d HBORD=%d VBORD=%d SER=%d KBD=%d CART=%d\n", (val>>5)&1, (val>>4)&1, (val>>3)&1, (val>>2)&1, (val>>1)&1, val&1);
		break;

	case 3:
		GIME_DEBUG("GIME FIRQ:  TMR=%d HBORD=%d VBORD=%d SER=%d KBD=%d CART=%d\n", (val>>5)&1, (val>>4)&1, (val>>3)&1, (val>>2)&1, (val>>1)&1, val&1);
		break;

	case 4:
		{
			// Timer MSB
			unsigned timer_reset = ((gime->registers[4] & 0x0f) << 8) | gime->registers[5];
			gime->timer_counter = timer_reset + gime->timer_offset;
			schedule_timer(gime);
		}
		GIME_DEBUG("GIME TMRH:  TIMER=%d\n", (val<<8)|gime->registers[5]);
		break;

	case 5:
		// Timer LSB
		GIME_DEBUG("GIME TMRL:  TIMER=%d\n", (gime->registers[4]<<8)|val);
		break;

	case 8:
		gime->BP = val & 0x80;
		gime->BPI = val & 0x20;
		gime->MOCH = val & 0x10;
		gime->H50 = val & 0x08;
		gime->LPR = val & 7;
		gime->field_duration = gime->H50 ? 312 : 262;
		gime->lTB = VRES_LPF_lTB[gime->H50][gime->LPF];
		GIME_DEBUG("GIME VMODE: BP=%d BPI=%d MOCH=%d H50=%d (l=%d) LPR=%d (%d)\n", (val&0x80)?1:0, (val&0x20)?1:0, (val&0x10)?1:0, (val&8)?1:0, gime->field_duration, val&7, LPR_nLPR[gime->LPR]);
		tcc1014_update_graphics_mode(gime);
		break;

	case 9:
		gime->LPF = (val >> 5) & 3;
		gime->HRES = (val >> 2) & 7;
		gime->CRES = val & 3;
		gime->lAA = VRES_LPF_lAA[gime->LPF];
		gime->lTB = VRES_LPF_lTB[gime->H50][gime->LPF];
		gime->pVSYNC = VRES_HRES_pVSYNC[gime->HRES & 1];
		gime->pLB = VRES_HRES_pLB[gime->HRES & 1];
		gime->pRB = VRES_HRES_pRB[gime->HRES & 1];
		if (gime->lAA == 65535) {
			gime->post_vblank_vstate = (gime->vstate == tcc1014_vstate_active_area) ? tcc1014_vstate_active_area : tcc1014_vstate_bottom_border;
			if (gime->post_vblank_vstate == tcc1014_vstate_top_border) {
				gime->post_vblank_vstate = tcc1014_vstate_bottom_border;
			}
		} else {
			gime->post_vblank_vstate = tcc1014_vstate_top_border;
		}
		GIME_DEBUG("GIME VRES:  LPF=%d (lTB=%d lAA=%d) HRES=%d CRES=%d\n", (val>>5)&3, gime->lTB, gime->lAA, (val>>2)&7, val&3);
		tcc1014_update_graphics_mode(gime);
		break;

	case 0xa:
		gime->BRDR = val & 0x3f;
		GIME_DEBUG("GIME BRDR:  BRDR=%d\n", gime->BRDR);
		tcc1014_update_graphics_mode(gime);
		break;

	case 0xc:
		gime->VSC = val & 15;
		GIME_DEBUG("GIME VSC:   VSC=%d\n", val&15);
		tcc1014_update_graphics_mode(gime);
		break;

	case 0xd:
		gime->Y = (val << 11) | (gime->registers[0xe] << 3);
		GIME_DEBUG("GIME VOFFh: VOFF=%05x\n", (val<<11)|(gime->registers[0xe]<<3));
		break;

	case 0xe:
		gime->Y = (gime->registers[0xd] << 11) | (val << 3);
		GIME_DEBUG("GIME VOFFl: VOFF=%05x\n", (gime->registers[0xd]<<11)|(val<<3));
		break;

	case 0xf:
		gime->HVEN = val & 0x80;
		gime->X = (val & 0x7f) << 1;
		GIME_DEBUG("GIME HOFF:  HVEN=%d X=%d\n", gime->HVEN, gime->X);
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
			if (!gime->COCO) {
				gime->row = (gime->row + 1) & 15;
				if ((gime->row & LPR_rowmask_TEXT[gime->LPR]) == LPR_rowmask_TEXT[gime->LPR]) {
					gime->row = 0;
					gime->B += gime->row_stride;
				}
			} else {
				gime->row = (gime->row + 1) % gime->nLPR;
				if (gime->row == 0) {
					gime->B += gime->row_stride;
				}
			}
			gime->Xoff = gime->COCO ? 0 : gime->X;
		}
		gime->beam_pos = TCC1014_LEFT_BORDER_START;
		// Total bodge to fix PAL display!  I think really we need the
		// video module to know (either inferring or being told) that
		// the signal is PAL.
		if (!gime->H50 || gime->scanline > 26) {
			DELEGATE_CALL(gime->public.render_line, gime->pixel_data, gime->BPI);
		}
	}

	if (gime->COCO) {
		gime->row_stride = gime->BPR;
	} else if (gime->BP) {
		gime->row_stride = gime->HVEN ? 256 : gime->BPR;
	} else {
		gime->row_stride = gime->HVEN ? 256 : (gime->BPR << (gime->CRES & 1));
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
	gime->vram_bit = 0;
	gime->lborder_remaining = gime->pLB;
	gime->vram_remaining = gime->BPR;
	gime->rborder_remaining = gime->pRB;
	gime->scanline++;
	gime->lcount++;

	// Always check against this line three before field duration - could
	// hit this during active area or bottom border.
	if (gime->scanline >= gime->field_duration - 3) {
		gime->fs_fall_event.at_tick = gime->scanline_start + gime->pVSYNC;
		event_queue(&MACHINE_EVENT_LIST, &gime->fs_fall_event);
		gime->lcount = 0;
		gime->scanline = 0;
		gime->vstate = tcc1014_vstate_vsync;
		memset(gime->pixel_data, 0, sizeof(gime->pixel_data));
	} else switch (gime->vstate) {
	case tcc1014_vstate_vblank:
		if (gime->lcount >= TCC1014_TOP_BORDER_START) {
			gime->lcount = 0;
			gime->vstate = gime->post_vblank_vstate;
			memset(gime->pixel_data, gime->border_colour, sizeof(gime->pixel_data));
		}
		break;
	case tcc1014_vstate_top_border:
		memset(gime->pixel_data, gime->border_colour, sizeof(gime->pixel_data));
		if (gime->lcount >= gime->nTB) {
			if (!gime->COCO) {
				gime->row = gime->VSC;
				if ((gime->row & LPR_rowmask_TEXT[gime->LPR]) == LPR_rowmask_TEXT[gime->LPR]) {
					gime->row = 0;
				}
			} else {
				gime->row = 0;
			}
			gime->lcount = 0;
			gime->vstate = tcc1014_vstate_active_area;
		}
		break;
	case tcc1014_vstate_active_area:
		if (gime->lcount >= gime->nAA) {
			gime->lcount = 0;
			gime->vstate = tcc1014_vstate_bottom_border;
			memset(gime->pixel_data, gime->border_colour, sizeof(gime->pixel_data));
		}
		break;
	case tcc1014_vstate_bottom_border:
		memset(gime->pixel_data, gime->border_colour, sizeof(gime->pixel_data));
		break;
	case tcc1014_vstate_vsync:
		if (gime->lcount >= 4) {
			gime->fs_rise_event.at_tick = gime->scanline_start + gime->pVSYNC;
			event_queue(&MACHINE_EVENT_LIST, &gime->fs_rise_event);
			gime->B = gime->Y;
			if (gime->COCO) {
				gime->B = (gime->B & 0x701ff) | (gime->SAM_F << 9);
			}
			gime->vstate = tcc1014_vstate_vblank;
			gime->lcount = 0;
			gime->scanline = 0;
		}
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

static void do_fs_fall(void *sptr) {
	struct TCC1014_private *gime = sptr;
	// FS falling edge
	DELEGATE_CALL(gime->public.signal_fs, 0);
}

static void do_fs_rise(void *sptr) {
	struct TCC1014_private *gime = sptr;
	// FS rising edge
	DELEGATE_CALL(gime->public.signal_fs, 1);
}

static uint8_t fetch_byte_vram(struct TCC1014_private *gime) {
	// X offset appears to be dynamically added to current video address
	return DELEGATE_CALL(gime->public.fetch_vram, gime->B + (gime->Xoff++ & 0xff));
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

			if (gime->COCO) {
				unsigned font_row = gime->row & 0x0f;
				gime->SnA = vdata & 0x80;
				if (gime->GnA) {
					// Graphics mode
					gime->vram_g_data = vdata;
					gime->fg_colour = gime->CSS ? TCC1014_RGCSS1_1 : TCC1014_RGCSS0_1;
					gime->bg_colour = gime->CSS ? TCC1014_RGCSS1_0 : TCC1014_RGCSS0_0;
					gime->render_mode = gime->GM0 ? TCC1014_RENDER_RG : TCC1014_RENDER_CG;
				} else {
					if (gime->SnA) {
						// Semigraphics
						if (font_row < 6) {
							gime->vram_sg_data = vdata >> 2;
						} else {
							gime->vram_sg_data = vdata;
						}
						gime->s_fg_colour = (vdata >> 4) & 7;
						gime->s_bg_colour = TCC1014_RGCSS0_0;
						gime->render_mode = TCC1014_RENDER_SG;
					} else {
						// Alphanumeric
						_Bool INV = vdata & 0x40;
						INV ^= gime->GM1;  // 6847T1-compatible invert flag
						uint8_t c = vdata & 0x7f;
						if (c < 0x20) {
							c |= (gime->GM0 ? 0x60 : 0x40);
							INV ^= gime->GM0;
						} else if (c >= 0x60) {
							c ^= 0x40;
						}
						gime->vram_g_data = font_gime[c*12+font_row];

						// Handle UI-specified inverse text mode:
						if (INV ^ gime->inverted_text)
							gime->vram_g_data = ~gime->vram_g_data;
						gime->fg_colour = gime->CSS ? TCC1014_BRIGHT_ORANGE : TCC1014_BRIGHT_GREEN;
						gime->bg_colour = gime->CSS ? TCC1014_DARK_ORANGE : TCC1014_DARK_GREEN;
						gime->render_mode = TCC1014_RENDER_RG;
					}
				}

			} else {
				unsigned font_row = (gime->row + 1) & 0x0f;
				if (font_row > 11)
					font_row = 0;
				// CoCo 3 mode
				if (gime->BP) {
					// CoCo 3 graphics
					gime->vram_g_data = vdata;

				} else {
					// CoCo 3 text
					int c = vdata & 0x7f;
					gime->vram_g_data = font_gime[c*12+font_row];
					if (gime->CRES & 1) {
						uint8_t attr = fetch_byte_vram(gime);
						gime->attr_fgnd = 8 | ((attr >> 3) & 7);
						gime->attr_bgnd = attr & 7;
						if ((attr & 0x80) && gime->blink)
							gime->attr_fgnd = gime->attr_bgnd;
						if ((attr & 0x40) && (font_row == LPR_nLPR[gime->LPR]))
							gime->vram_g_data = 0xff;
					} else {
						gime->attr_fgnd = 1;
						gime->attr_bgnd = 0;
					}
				}
			}
		}

		uint8_t c0, c1, c2, c3;

		if (gime->COCO) {
			// CoCo 2 modes
			switch (gime->render_mode) {
			case TCC1014_RENDER_SG: default:
				c0 = c1 = c2 = c3 = gime->palette_reg[(gime->vram_sg_data&0x02) ? gime->s_fg_colour : gime->s_bg_colour];
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
			gime->vram_sg_data <<= 1;

		} else {
			// CoCo 3 modes
			uint8_t vdata = gime->vram_g_data;
			if (gime->BP) {
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
				c0 = gime->palette_reg[(vdata&0x80)?gime->attr_fgnd:gime->attr_bgnd];
				c1 = gime->palette_reg[(vdata&0x40)?gime->attr_fgnd:gime->attr_bgnd];
				c2 = gime->palette_reg[(vdata&0x20)?gime->attr_fgnd:gime->attr_bgnd];
				c3 = gime->palette_reg[(vdata&0x10)?gime->attr_fgnd:gime->attr_bgnd];
			}
			gime->vram_bit -= 4;
			gime->vram_g_data <<= 4;
		}

		// Render appropriate number of pixels
		switch (gime->resolution) {
		case 0:
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

		case 1:
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

		case 2:
			*(pixel) = c0;
			*(pixel+1) = c1;
			*(pixel+2) = c2;
			*(pixel+3) = c3;
			pixel += 4;
			gime->beam_pos += 4;
			break;

		case 3:
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
		*(pixel++) = gime->border_colour;
		*(pixel++) = gime->border_colour;
		gime->beam_pos += 2;
		gime->rborder_remaining -= 2;
		if (gime->beam_pos >= beam_to)
			return;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void tcc1014_set_inverted_text(struct TCC1014 *gimep, _Bool invert) {
	struct TCC1014_private *gime = (struct TCC1014_private *)gimep;
	gime->inverted_text = invert;
}

static void tcc1014_update_graphics_mode(struct TCC1014_private *gime) {
	// Render scanline so far before changing modes
	if (gime->frame == 0 && gime->vstate == tcc1014_vstate_active_area) {
		render_scanline(gime);
	}

	// Decode VDG-compatible mode setting
	gime->GnA = gime->vmode & 0x80;
	_Bool GM2 = gime->vmode & 0x40;
	gime->GM1 = gime->vmode & 0x20;
	gime->GM0 = gime->vmode & 0x10;
	gime->CSS = gime->vmode & 0x08;
	unsigned GM = (gime->vmode >> 4) & 7;

	if (gime->COCO) {
		// CoCo 1/2 compatibility mode

		// Bytes per row, render resolution
		if (!gime->GnA || !(GM == 0 || (gime->GM0 && GM != 7))) {
			gime->BPR = 32;
			gime->resolution = 1;
		} else {
			gime->BPR = 16;
			gime->resolution = 0;
		}

		// Line counts
		gime->nTB = gime->H50 ? 63 : 36;
		gime->nAA = 192;
		gime->nLB = 120 + (gime->H50 ? 25 : 0);
		gime->nLPR = gime->GnA ? SAM_V_nLPR[gime->SAM_V] : VSC_nLPR[gime->VSC];

		// Render mode, fixed colours
		gime->cg_colours = !gime->CSS ? TCC1014_GREEN : TCC1014_WHITE;
		if (!gime->GnA) {
			gime->render_mode = !gime->SnA ? TCC1014_RENDER_RG : TCC1014_RENDER_SG;
			gime->fg_colour = gime->CSS ? TCC1014_BRIGHT_ORANGE : TCC1014_BRIGHT_GREEN;
			gime->bg_colour = gime->CSS ? TCC1014_DARK_ORANGE : TCC1014_DARK_GREEN;
			_Bool text_border = !gime->GM1 && GM2;
			unsigned text_border_colour = gime->CSS ? 0x26 : 0x12;
			gime->border_colour = text_border ? text_border_colour : 0;
		} else {
			gime->render_mode = gime->GM0 ? TCC1014_RENDER_RG : TCC1014_RENDER_CG;
			gime->fg_colour = gime->CSS ? TCC1014_RGCSS1_1 : TCC1014_RGCSS0_1;
			gime->bg_colour = gime->CSS ? TCC1014_RGCSS1_0 : TCC1014_RGCSS0_0;
			gime->border_colour = gime->palette_reg[gime->cg_colours];
		}
	} else {
		// CoCo 3 extra graphics modes

		// Bytes per row, render resolution
		if (gime->BP) {
			gime->BPR = VRES_HRES_BPR[gime->HRES];
			gime->resolution = gime->HRES >> 1;
		} else {
			gime->BPR = VRES_HRES_BPR_TEXT[gime->HRES];
			gime->resolution = (gime->HRES & 4) ? 2 : 1;
		}

		// Line counts
		gime->nTB = gime->lTB;
		gime->nAA = gime->lAA;
		gime->nLB = gime->pLB + (gime->H50 ? 25 : 0);
		gime->nLPR = LPR_nLPR[gime->LPR];

		// Render mode, border colour
		gime->render_mode = TCC1014_RENDER_RG;
		gime->border_colour = gime->BRDR;
	}

}
