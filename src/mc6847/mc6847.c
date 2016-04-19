/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "delegate.h"
#include "events.h"
#include "logging.h"
#include "mc6809.h"
#include "mc6847/font-6847.h"
#include "mc6847/font-6847t1.h"
#include "mc6847/mc6847.h"
#include "sam.h"
#include "vo.h"
#include "xroar.h"

struct ser_handle;

// Convert VDG pixels (half-cycles) to event ticks:
#define EVENT_VDG_PIXELS(c) EVENT_SAM_CYCLES((c))

enum vdg_render_mode {
	VDG_RENDER_SG,
	VDG_RENDER_CG,
	VDG_RENDER_RG,
};

struct MC6847_private {
	struct MC6847 public;

	/* Control lines */
	unsigned GM;
	_Bool nA_S;
	_Bool nA_G;
	_Bool EXT;
	_Bool CSS, CSSa, CSSb;
	_Bool inverted_text;

	/* Timing */
	struct event hs_fall_event;
	struct event hs_rise_event;
	event_ticks scanline_start;
	unsigned beam_pos;
	unsigned scanline;

	/* Data */
	uint8_t vram_g_data;
	uint8_t vram_sg_data;

	/* Output */
	int frame;  // frameskip counter

	/* Internal state */
	_Bool is_32byte;
	_Bool GM0;
	uint8_t s_fg_colour;
	uint8_t s_bg_colour;
	uint8_t fg_colour;
	uint8_t bg_colour;
	uint8_t cg_colours;
	uint8_t border_colour;
	uint8_t bright_orange;
	int vram_bit;
	enum vdg_render_mode render_mode;
	unsigned pal_padding;
	uint8_t pixel_data[VDG_LINE_DURATION];

	uint16_t vram[42];
	unsigned vram_index;
	unsigned vram_nbytes;

	/* Counters */
	unsigned lborder_remaining;
	unsigned vram_remaining;
	unsigned rborder_remaining;

	/* 6847T1 state */
	_Bool is_t1;
	_Bool inverse_text;
	_Bool text_border;
	uint8_t text_border_colour;
};

static void do_hs_fall(void *);
static void do_hs_rise(void *);
static void do_hs_fall_pal(void *);

static void render_scanline(struct MC6847_private *vdg);

#define SCANLINE(s) ((s) % VDG_FRAME_DURATION)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void do_hs_fall(void *data) {
	struct MC6847_private *vdg = data;
	// Finish rendering previous scanline
	if (vdg->frame == 0) {
		if (vdg->scanline < VDG_ACTIVE_AREA_START) {
			if (vdg->scanline == 0) {
				memset(vdg->pixel_data + VDG_LEFT_BORDER_START, vdg->border_colour, VDG_tAVB);
			}
			vo_module->render_scanline(vdg->pixel_data);
		} else if (vdg->scanline >= VDG_ACTIVE_AREA_START && vdg->scanline < VDG_ACTIVE_AREA_END) {
			render_scanline(vdg);
			vdg->public.row++;
			if (vdg->public.row > 11)
				vdg->public.row = 0;
			vo_module->render_scanline(vdg->pixel_data);
			vdg->beam_pos = VDG_LEFT_BORDER_START;
		} else if (vdg->scanline >= VDG_ACTIVE_AREA_END) {
			if (vdg->scanline == VDG_ACTIVE_AREA_END) {
				memset(vdg->pixel_data + VDG_LEFT_BORDER_START, vdg->border_colour, VDG_tAVB);
			}
			vo_module->render_scanline(vdg->pixel_data);
		}
	}

	// HS falling edge.
	DELEGATE_CALL1(vdg->public.signal_hs, 0);

	vdg->scanline_start = vdg->hs_fall_event.at_tick;
	// Next HS rise and fall
	vdg->hs_rise_event.at_tick = vdg->scanline_start + EVENT_VDG_PIXELS(VDG_HS_RISING_EDGE);
	vdg->hs_fall_event.at_tick = vdg->scanline_start + EVENT_VDG_PIXELS(VDG_LINE_DURATION);

	/* On PAL machines, the clock to the VDG is interrupted at two points
	 * in every frame to fake up some extra scanlines, padding the signal
	 * from 262 lines to 312 lines.  Dragons do not generate an HS-related
	 * interrupt signal during this time, CoCos do.  The positioning and
	 * duration of each interruption differs also. */

	if (vdg->public.is_pal) {
		if (vdg->public.is_dragon64) {
			if (vdg->scanline == SCANLINE(VDG_ACTIVE_AREA_END + 23)
			    || vdg->scanline == SCANLINE(VDG_ACTIVE_AREA_END + 31)) {
				vdg->hs_rise_event.at_tick += 25 * EVENT_VDG_PIXELS(VDG_PAL_PADDING_LINE);
				vdg->hs_fall_event.at_tick += 25 * EVENT_VDG_PIXELS(VDG_PAL_PADDING_LINE);
			}
		} else if (vdg->public.is_dragon32) {
			if (vdg->scanline == SCANLINE(VDG_ACTIVE_AREA_END + 23)
			    || vdg->scanline == SCANLINE(VDG_ACTIVE_AREA_END + 31)) {
				vdg->pal_padding = 25;
				vdg->hs_fall_event.delegate.func = do_hs_fall_pal;
			}
		} else if (vdg->public.is_coco) {
			if (vdg->scanline == SCANLINE(VDG_ACTIVE_AREA_END + 25)) {
				vdg->pal_padding = 26;
				vdg->hs_fall_event.delegate.func = do_hs_fall_pal;
			} else if (vdg->scanline == SCANLINE(VDG_ACTIVE_AREA_END + 47)) {
				vdg->pal_padding = 24;
				vdg->hs_fall_event.delegate.func = do_hs_fall_pal;
			}
		}
	}

	event_queue(&MACHINE_EVENT_LIST, &vdg->hs_rise_event);
	event_queue(&MACHINE_EVENT_LIST, &vdg->hs_fall_event);

	// Next scanline
	vdg->scanline = SCANLINE(vdg->scanline + 1);
	vdg->vram_nbytes = 0;
	vdg->vram_index = 0;
	vdg->vram_bit = 0;
	vdg->lborder_remaining = VDG_tLB;
	vdg->vram_remaining = vdg->is_32byte ? 32 : 16;
	vdg->rborder_remaining = VDG_tRB;

	if (vdg->scanline == VDG_ACTIVE_AREA_START) {
		vdg->public.row = 0;
	}

	if (vdg->scanline == VDG_ACTIVE_AREA_END) {
		// FS falling edge
		DELEGATE_CALL1(vdg->public.signal_fs, 0);
	}

	if (vdg->scanline == VDG_VBLANK_START) {
		// FS rising edge
		DELEGATE_CALL1(vdg->public.signal_fs, 1);
		vdg->frame--;
		if (vdg->frame < 0)
			vdg->frame = xroar_frameskip;
		if (vdg->frame == 0)
			vo_module->vsync();
	}

}

static void do_hs_rise(void *data) {
	struct MC6847_private *vdg = data;
	// HS rising edge.
	DELEGATE_CALL1(vdg->public.signal_hs, 1);
}

static void do_hs_fall_pal(void *data) {
	struct MC6847_private *vdg = data;
	// HS falling edge
	DELEGATE_CALL1(vdg->public.signal_hs, 0);

	vdg->scanline_start = vdg->hs_fall_event.at_tick;
	// Next HS rise and fall
	vdg->hs_rise_event.at_tick = vdg->scanline_start + EVENT_VDG_PIXELS(VDG_HS_RISING_EDGE);
	vdg->hs_fall_event.at_tick = vdg->scanline_start + EVENT_VDG_PIXELS(VDG_LINE_DURATION);

	vdg->pal_padding--;
	if (vdg->pal_padding == 0)
		vdg->hs_fall_event.delegate.func = do_hs_fall;

	event_queue(&MACHINE_EVENT_LIST, &vdg->hs_rise_event);
	event_queue(&MACHINE_EVENT_LIST, &vdg->hs_fall_event);
}

static void render_scanline(struct MC6847_private *vdg) {
	unsigned beam_to = (event_current_tick - vdg->scanline_start) / EVENT_VDG_PIXELS(1);
	if (vdg->is_32byte && beam_to >= (VDG_tHBNK + 16)) {
		unsigned nbytes = (beam_to - VDG_tHBNK) >> 4;
		if (nbytes > 42)
			nbytes = 42;
		if (nbytes > vdg->vram_nbytes) {
			DELEGATE_CALL2(vdg->public.fetch_data, nbytes - vdg->vram_nbytes, vdg->vram + vdg->vram_nbytes);
			vdg->vram_nbytes = nbytes;
		}
	} else if (!vdg->is_32byte && beam_to >= (VDG_tHBNK + 32)) {
		unsigned nbytes = (beam_to - VDG_tHBNK) >> 5;
		if (nbytes > 22)
			nbytes = 22;
		if (nbytes > vdg->vram_nbytes) {
			DELEGATE_CALL2(vdg->public.fetch_data, nbytes - vdg->vram_nbytes, vdg->vram + vdg->vram_nbytes);
			vdg->vram_nbytes = nbytes;
		}
	}
	if (beam_to < VDG_LEFT_BORDER_START)
		return;

	if (vdg->beam_pos >= beam_to)
		return;
	uint8_t *pixel = vdg->pixel_data + vdg->beam_pos;

	while (vdg->lborder_remaining > 0) {
		*(pixel++) = vdg->border_colour;
		vdg->beam_pos++;
		if ((vdg->beam_pos & 15) == 0) {
			vdg->CSSa = vdg->CSS;
		}
		vdg->lborder_remaining--;
		if (vdg->beam_pos >= beam_to)
			return;
	}

	while (vdg->vram_remaining > 0) {
		if (vdg->vram_bit == 0) {
			uint16_t vdata = vdg->vram[vdg->vram_index++];
			vdg->vram_g_data = vdata & 0xff;
			vdg->vram_bit = 8;
			if (vdg->is_t1) {
				vdg->nA_S = vdata & 0x80;
			} else {
				vdg->nA_S = vdata & 0x200;
			}
			vdg->EXT = vdata & 0x400;

			vdg->CSSb = vdg->CSSa;
			vdg->CSSa = vdg->CSS;
			vdg->cg_colours = !vdg->CSSb ? VDG_GREEN : VDG_WHITE;
			vdg->text_border_colour = !vdg->CSSb ? VDG_GREEN : vdg->bright_orange;

			if (!vdg->nA_G && !vdg->nA_S) {
				_Bool INV;
				if (vdg->is_t1) {
					INV = vdg->EXT || (vdata & 0x40);
					INV ^= vdg->inverse_text;
					if (!vdg->EXT)
						vdg->vram_g_data |= 0x40;
					vdg->vram_g_data = font_6847t1[(vdg->vram_g_data&0x7f)*12 + vdg->public.row];
				} else {
					INV = vdata & 0x100;
					if (!vdg->EXT)
						vdg->vram_g_data = font_6847[(vdg->vram_g_data&0x3f)*12 + vdg->public.row];
				}
				if (INV ^ vdg->inverted_text)
					vdg->vram_g_data = ~vdg->vram_g_data;
			}

			if (!vdg->nA_G && vdg->nA_S) {
				vdg->vram_sg_data = vdg->vram_g_data;
				if (vdg->is_t1 || !vdg->EXT) {
					if (vdg->public.row < 6)
						vdg->vram_sg_data >>= 2;
					vdg->s_fg_colour = (vdg->vram_g_data >> 4) & 7;
				} else {
					if (vdg->public.row < 4)
						vdg->vram_sg_data >>= 4;
					else if (vdg->public.row < 8)
						vdg->vram_sg_data >>= 2;
					vdg->s_fg_colour = vdg->cg_colours + ((vdg->vram_g_data >> 6) & 3);
				}
				vdg->s_bg_colour = !vdg->nA_G ? VDG_BLACK : VDG_GREEN;
				vdg->vram_sg_data = ((vdg->vram_sg_data & 2) ? 0xf0 : 0) | ((vdg->vram_sg_data & 1) ? 0x0f : 0);
			}

			if (!vdg->nA_G) {
				vdg->render_mode = !vdg->nA_S ? VDG_RENDER_RG : VDG_RENDER_SG;
				vdg->fg_colour = !vdg->CSSb ? VDG_GREEN : vdg->bright_orange;
				vdg->bg_colour = !vdg->CSSb ? VDG_DARK_GREEN : VDG_DARK_ORANGE;
			} else {
				vdg->render_mode = vdg->GM0 ? VDG_RENDER_RG : VDG_RENDER_CG;
				vdg->fg_colour = !vdg->CSSb ? VDG_GREEN : VDG_WHITE;
				vdg->bg_colour = !vdg->CSSb ? VDG_DARK_GREEN : VDG_BLACK;
			}
		}

		uint8_t c0, c1;
		switch (vdg->render_mode) {
		case VDG_RENDER_SG:
			c0 = (vdg->vram_sg_data&0x80) ? vdg->s_fg_colour : vdg->s_bg_colour;
			c1 = (vdg->vram_sg_data&0x40) ? vdg->s_fg_colour : vdg->s_bg_colour;
			break;
		case VDG_RENDER_CG:
			c0 = c1 = vdg->cg_colours + ((vdg->vram_g_data & 0xc0) >> 6);
			break;
		case VDG_RENDER_RG:
			c0 = (vdg->vram_g_data&0x80) ? vdg->fg_colour : vdg->bg_colour;
			c1 = (vdg->vram_g_data&0x40) ? vdg->fg_colour : vdg->bg_colour;
			break;
		}
		if (vdg->is_32byte) {
			*(pixel) = *(pixel+1) = c0;
			*(pixel+2) = *(pixel+3) = c1;
			pixel += 4;
			vdg->beam_pos += 4;
		} else {
			*(pixel) = *(pixel+1) = *(pixel+2) = *(pixel+3) = c0;
			*(pixel+4) = *(pixel+5) = *(pixel+6) = *(pixel+7) = c1;
			pixel += 8;
			vdg->beam_pos += 8;
		}

		vdg->vram_bit -= 2;
		if (vdg->vram_bit == 0) {
			vdg->vram_remaining--;
		}
		vdg->vram_g_data <<= 2;
		vdg->vram_sg_data <<= 2;
		if (vdg->beam_pos >= beam_to)
			return;
	}

	while (vdg->rborder_remaining > 0) {
		if (vdg->beam_pos == VDG_RIGHT_BORDER_START) {
			vdg->CSSb = vdg->CSSa;
			vdg->text_border_colour = !vdg->CSSb ? VDG_GREEN : vdg->bright_orange;
		}
		vdg->border_colour = vdg->nA_G ? vdg->cg_colours : (vdg->text_border ? vdg->text_border_colour : VDG_BLACK);
		*(pixel++) = vdg->border_colour;
		vdg->beam_pos++;
		if ((vdg->beam_pos & 15) == 0) {
			vdg->CSSa = vdg->CSS;
		}
		vdg->rborder_remaining--;
		if (vdg->beam_pos >= beam_to)
			return;
	}

	// If a program switches to 32 bytes per line mid-scanline, the whole
	// scanline might not have been rendered:
	while (vdg->beam_pos < VDG_RIGHT_BORDER_END) {
		*(pixel++) = VDG_BLACK;
		vdg->beam_pos++;
	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void update_vdg(struct MC6847_private *vdg) {

	vdg->GM0 = vdg->GM & 1;
	vdg->bright_orange = vdg->is_t1 ? VDG_ORANGE : VDG_BRIGHT_ORANGE;
	vdg->inverse_text = vdg->is_t1 && (vdg->GM & 2);
	vdg->text_border = vdg->is_t1 && !vdg->inverse_text && (vdg->GM & 4);
	vdg->text_border_colour = !vdg->CSSb ? VDG_GREEN : vdg->bright_orange;
	vdg->cg_colours = !vdg->CSSb ? VDG_GREEN : VDG_WHITE;

	if (!vdg->nA_G) {
		vdg->render_mode = VDG_RENDER_RG;
		if (vdg->nA_S) {
			vdg->fg_colour = VDG_GREEN;
			vdg->bg_colour = VDG_DARK_GREEN;
		} else {
			vdg->fg_colour = !vdg->CSSb ? VDG_GREEN : vdg->bright_orange;
			vdg->bg_colour = !vdg->CSSb ? VDG_DARK_GREEN : VDG_DARK_ORANGE;
		}
		vdg->border_colour = vdg->text_border ? vdg->text_border_colour : VDG_BLACK;
	} else {
		vdg->render_mode = vdg->GM0 ? VDG_RENDER_RG : VDG_RENDER_CG;
		vdg->fg_colour = !vdg->CSSb ? VDG_GREEN : VDG_WHITE;
		vdg->bg_colour = !vdg->CSSb ? VDG_DARK_GREEN : VDG_BLACK;
		vdg->border_colour = vdg->cg_colours;
	}

}

struct MC6847 *mc6847_new(_Bool t1) {
	struct MC6847_private *vdg = xmalloc(sizeof(*vdg));
	*vdg = (struct MC6847_private){.public={0}};
	vdg->is_t1 = t1;
	vdg->beam_pos = VDG_LEFT_BORDER_START;
	vdg->public.signal_hs = DELEGATE_DEFAULT1(void, bool);
	vdg->public.signal_fs = DELEGATE_DEFAULT1(void, bool);
	vdg->public.fetch_data = DELEGATE_DEFAULT2(void, int, uint16p);
	event_init(&vdg->hs_fall_event, DELEGATE_AS0(void, do_hs_fall, vdg));
	event_init(&vdg->hs_rise_event, DELEGATE_AS0(void, do_hs_rise, vdg));
	update_vdg(vdg);
	return (struct MC6847 *)vdg;
}

void mc6847_free(struct MC6847 *vdgp) {
	struct MC6847_private *vdg = (struct MC6847_private *)vdgp;
	event_dequeue(&vdg->hs_fall_event);
	event_dequeue(&vdg->hs_rise_event);
	free(vdg);
}

void mc6847_reset(struct MC6847 *vdgp) {
	struct MC6847_private *vdg = (struct MC6847_private *)vdgp;
	vo_module->vsync();
	memset(vdg->pixel_data, VDG_BLACK, sizeof(vdg->pixel_data));
	vdg->beam_pos = VDG_LEFT_BORDER_START;
	vdg->frame = 0;
	vdg->scanline = 0;
	vdg->public.row = 0;
	vdg->scanline_start = event_current_tick;
	vdg->hs_fall_event.at_tick = event_current_tick + EVENT_VDG_PIXELS(VDG_LINE_DURATION);
	event_queue(&MACHINE_EVENT_LIST, &vdg->hs_fall_event);
	// 6847T1 doesn't appear to do bright orange:
	vdg->bright_orange = vdg->is_t1 ? VDG_ORANGE : VDG_BRIGHT_ORANGE;
	mc6847_set_mode(vdgp, 0);
	vdg->vram_index = 0;
	vdg->vram_bit = 0;
	vdg->lborder_remaining = VDG_tLB;
	vdg->vram_remaining = vdg->is_32byte ? 32 : 16;
	vdg->rborder_remaining = VDG_tRB;
}

void mc6847_set_inverted_text(struct MC6847 *vdgp, _Bool invert) {
	struct MC6847_private *vdg = (struct MC6847_private *)vdgp;
	vdg->inverted_text = invert;
}

void mc6847_set_mode(struct MC6847 *vdgp, unsigned mode) {
	struct MC6847_private *vdg = (struct MC6847_private *)vdgp;
	/* Render scanline so far before changing modes */
	if (vdg->frame == 0 && vdg->scanline >= VDG_ACTIVE_AREA_START && vdg->scanline < VDG_ACTIVE_AREA_END) {
		render_scanline(vdg);
	}

	vdg->GM = (mode >> 4) & 7;
	vdg->GM0 = mode & 0x10;
	vdg->CSS = mode & 0x08;
	_Bool new_nA_G = mode & 0x80;

	vdg->inverse_text = vdg->is_t1 && (vdg->GM & 2);
	vdg->text_border = vdg->is_t1 && !vdg->inverse_text && (vdg->GM & 4);
	vdg->text_border_colour = !vdg->CSSb ? VDG_GREEN : vdg->bright_orange;

	/* If switching from graphics to alpha/semigraphics */
	if (vdg->nA_G && !new_nA_G) {
		vdg->public.row = 0;
		vdg->render_mode = VDG_RENDER_RG;
		if (vdg->nA_S) {
			vdg->vram_g_data = 0x3f;
			vdg->fg_colour = VDG_GREEN;
			vdg->bg_colour = VDG_DARK_GREEN;
		} else {
			vdg->fg_colour = !vdg->CSSb ? VDG_GREEN : vdg->bright_orange;
			vdg->bg_colour = !vdg->CSSb ? VDG_DARK_GREEN : VDG_DARK_ORANGE;
		}
	}
	if (!vdg->nA_G && new_nA_G) {
		vdg->border_colour = vdg->cg_colours;
		vdg->fg_colour = !vdg->CSSb ? VDG_GREEN : VDG_WHITE;
		vdg->bg_colour = !vdg->CSSb ? VDG_DARK_GREEN : VDG_BLACK;
	}
	vdg->nA_G = new_nA_G;

	if (vdg->nA_G) {
		vdg->render_mode = vdg->GM0 ? VDG_RENDER_RG : VDG_RENDER_CG;
	} else {
		vdg->border_colour = vdg->text_border ? vdg->text_border_colour : VDG_BLACK;
	}

	vdg->is_32byte = !vdg->nA_G || !(vdg->GM == 0 || (vdg->GM0 && vdg->GM != 7));
}