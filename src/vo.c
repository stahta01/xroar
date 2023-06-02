/** \file
 *
 *  \brief Video ouput modules & interfaces.
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
 */

#include "top-config.h"

#include <stdlib.h>

#include "delegate.h"
#include "xalloc.h"

#include "module.h"
#include "vo.h"
#include "vo_render.h"
#include "xconfig.h"

extern struct module vo_null_module;
static struct module * const default_vo_module_list[] = {
	&vo_null_module,
	NULL
};

struct module * const *vo_module_list = default_vo_module_list;

struct xconfig_enum vo_cmp_ccr_list[] = {
	{ XC_ENUM_INT("none", VO_CMP_CCR_PALETTE, "no cross-colour") },
	{ XC_ENUM_INT("simple", VO_CMP_CCR_2BIT, "four colour palette") },
	{ XC_ENUM_INT("5bit", VO_CMP_CCR_5BIT, "5-bit lookup table") },
	{ XC_ENUM_INT("partial", VO_CMP_CCR_PARTIAL, "partial simulated NTSC-only") },
	{ XC_ENUM_INT("simulated", VO_CMP_CCR_SIMULATED, "simulated filtered analogue") },
	{ XC_ENUM_END() }
};

struct xconfig_enum vo_pixel_fmt_list[] = {
	{ XC_ENUM_INT("rgba8", VO_RENDER_FMT_RGBA8, "RGBA 8-8-8-8, byte order") },
	{ XC_ENUM_INT("argb8", VO_RENDER_FMT_ARGB8, "ARGB 8-8-8-8, byte order") },
	{ XC_ENUM_INT("bgra8", VO_RENDER_FMT_BGRA8, "BGRA 8-8-8-8, byte order") },
	{ XC_ENUM_INT("abgr8", VO_RENDER_FMT_ABGR8, "ABGR 8-8-8-8, byte order") },
	{ XC_ENUM_INT("rgb565", VO_RENDER_FMT_RGB565, "RGB 5-6-5") },
	{ XC_ENUM_INT("rgba4", VO_RENDER_FMT_RGBA4, "RGBA 4-4-4") },
	{ XC_ENUM_INT("rgba32", VO_RENDER_FMT_RGBA32, "RGBA 8-8-8-8, host 32-bit packing") },
	{ XC_ENUM_INT("argb32", VO_RENDER_FMT_ARGB32, "ARGB 8-8-8-8, host 32-bit packing") },
	{ XC_ENUM_INT("bgra32", VO_RENDER_FMT_BGRA32, "BGRA 8-8-8-8, host 32-bit packing") },
	{ XC_ENUM_INT("abgr32", VO_RENDER_FMT_ABGR32, "ABGR 8-8-8-8, host 32-bit packing") },
	{ XC_ENUM_END() }
};

const uint8_t vo_cmp_lut_2bit[2][4][3] = {
	{
		{ 0x00, 0x00, 0x00 },
		{ 0x00, 0x80, 0xff },
		{ 0xff, 0x80, 0x00 },
		{ 0xff, 0xff, 0xff },
	}, {
		{ 0x00, 0x00, 0x00 },
		{ 0xff, 0x80, 0x00 },
		{ 0x00, 0x80, 0xff },
		{ 0xff, 0xff, 0xff }
	}
};

const uint8_t vo_cmp_lut_5bit[2][32][3] = {
	{
		{ 0x00, 0x00, 0x00 },
		{ 0x00, 0x00, 0x00 },
		{ 0x00, 0x32, 0x78 },
		{ 0x00, 0x28, 0x00 },
		{ 0xff, 0x8c, 0x64 },
		{ 0xff, 0x8c, 0x64 },
		{ 0xff, 0xd2, 0xff },
		{ 0xff, 0xf0, 0xc8 },
		{ 0x00, 0x32, 0x78 },
		{ 0x00, 0x00, 0x3c },
		{ 0x00, 0x80, 0xff },
		{ 0x00, 0x80, 0xff },
		{ 0xd2, 0xff, 0xd2 },
		{ 0xff, 0xff, 0xff },
		{ 0x64, 0xf0, 0xff },
		{ 0xff, 0xff, 0xff },
		{ 0x3c, 0x00, 0x00 },
		{ 0x3c, 0x00, 0x00 },
		{ 0x00, 0x00, 0x00 },
		{ 0x00, 0x28, 0x00 },
		{ 0xff, 0x80, 0x00 },
		{ 0xff, 0x80, 0x00 },
		{ 0xff, 0xff, 0xff },
		{ 0xff, 0xf0, 0xc8 },
		{ 0x28, 0x00, 0x28 },
		{ 0x28, 0x00, 0x28 },
		{ 0x00, 0x80, 0xff },
		{ 0x00, 0x80, 0xff },
		{ 0xff, 0xf0, 0xc8 },
		{ 0xff, 0xf0, 0xc8 },
		{ 0xff, 0xff, 0xff },
		{ 0xff, 0xff, 0xff },
	}, {
		{ 0x00, 0x00, 0x00 },
		{ 0x00, 0x00, 0x00 },
		{ 0xb4, 0x3c, 0x1e },
		{ 0x28, 0x00, 0x28 },
		{ 0x46, 0xc8, 0xff },
		{ 0x46, 0xc8, 0xff },
		{ 0xd2, 0xff, 0xd2 },
		{ 0x64, 0xf0, 0xff },
		{ 0xb4, 0x3c, 0x1e },
		{ 0x3c, 0x00, 0x00 },
		{ 0xff, 0x80, 0x00 },
		{ 0xff, 0x80, 0x00 },
		{ 0xff, 0xd2, 0xff },
		{ 0xff, 0xff, 0xff },
		{ 0xff, 0xf0, 0xc8 },
		{ 0xff, 0xff, 0xff },
		{ 0x00, 0x00, 0x3c },
		{ 0x00, 0x00, 0x3c },
		{ 0x00, 0x00, 0x00 },
		{ 0x28, 0x00, 0x28 },
		{ 0x00, 0x80, 0xff },
		{ 0x00, 0x80, 0xff },
		{ 0xff, 0xff, 0xff },
		{ 0x64, 0xf0, 0xff },
		{ 0x00, 0x28, 0x00 },
		{ 0x00, 0x28, 0x00 },
		{ 0xff, 0x80, 0x00 },
		{ 0xff, 0x80, 0x00 },
		{ 0x64, 0xf0, 0xff },
		{ 0x64, 0xf0, 0xff },
		{ 0xff, 0xff, 0xff },
		{ 0xff, 0xff, 0xff },
	}
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Allocates at least enough space for (struct vo_interface)

void *vo_interface_new(size_t isize) {
	if (isize < sizeof(struct vo_interface))
		isize = sizeof(struct vo_interface);
	struct vo_interface *vo = xmalloc(isize);
	*vo = (struct vo_interface){0};
	return vo;
}

// Calls free() delegate then frees structure

void vo_free(void *sptr) {
	struct vo_interface *vo = sptr;
	DELEGATE_SAFE_CALL(vo->free);
	free(vo);
}

// Set renderer and use its contents to prepopulate various delegates.  Call
// this before overriding any locally in video modules.

void vo_set_renderer(struct vo_interface *vo, struct vo_render *vr) {
	vo->renderer = vr;

	// Used by UI to adjust viewing parameters
	vo->set_active_area = DELEGATE_AS4(void, int, int, int, int, vo_render_set_active_area, vr);
	vo->set_brightness = DELEGATE_AS1(void, int, vo_render_set_brightness, vr);
	vo->set_contrast = DELEGATE_AS1(void, int, vo_render_set_contrast, vr);
	vo->set_saturation = DELEGATE_AS1(void, int, vo_render_set_saturation, vr);
	vo->set_hue = DELEGATE_AS1(void, int, vo_render_set_hue, vr);
	vo->set_cmp_phase = DELEGATE_AS1(void, int, vo_render_set_cmp_phase, vr);

	// Used by machine to configure video output
	vo->set_cmp_lead_lag = DELEGATE_AS2(void, float, float, vo_render_set_cmp_lead_lag, vr);
	vo->palette_set_ybr = DELEGATE_AS4(void, uint8, float, float, float, vo_render_set_cmp_palette, vr);
	vo->palette_set_rgb = DELEGATE_AS4(void, uint8, float, float, float, vo_render_set_rgb_palette, vr);
	vo->set_cmp_burst = DELEGATE_AS2(void, unsigned, int, vo_render_set_cmp_burst, vr);
	vo->set_cmp_burst_br = DELEGATE_AS3(void, unsigned, float, float, vo_render_set_cmp_burst_br, vr);
	vo->set_cmp_phase_offset = DELEGATE_AS1(void, int, vo_render_set_cmp_phase_offset, vr);

	// Used by machine to render video
	vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vr->render_cmp_palette, vr);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Housekeeping after selecting TV input

static void update_render_parameters(struct vo_interface *vo) {
	struct vo_render *vr = vo->renderer;
	if (!vr)
		return;

	// RGB is always palette-based
	if (vo->signal == VO_SIGNAL_RGB) {
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vr->render_rgb_palette, vr);
		return;
	}

	// As is S-Video, though it uses the composite palette
	if (vo->signal == VO_SIGNAL_SVIDEO) {
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vr->render_cmp_palette, vr);
		return;
	}

	// Composite video has more options
	switch (vo->cmp_ccr) {
	case VO_CMP_CCR_PALETTE:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vr->render_cmp_palette, vr);
		break;
	case VO_CMP_CCR_2BIT:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vr->render_cmp_2bit, vr);
		break;
	case VO_CMP_CCR_5BIT:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vr->render_cmp_5bit, vr);
		break;
	case VO_CMP_CCR_PARTIAL:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vo_render_cmp_partial, vr);
		break;
	case VO_CMP_CCR_SIMULATED:
		vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, vo_render_cmp_simulated, vr);
		break;
	}
}

// Select input signal

void vo_set_signal(struct vo_interface *vo, int signal) {
	vo->signal = signal;
	update_render_parameters(vo);
}

// Select cross-colour renderer

void vo_set_cmp_ccr(struct vo_interface *vo, int ccr) {
	vo->cmp_ccr = ccr;
	update_render_parameters(vo);
}

extern inline void vo_set_cmp_fs(struct vo_interface *vo, _Bool notify, int value);
extern inline void vo_set_cmp_fsc(struct vo_interface *vo, _Bool notify, int value);
extern inline void vo_set_cmp_system(struct vo_interface *vo, _Bool notify, int value);
extern inline void vo_set_cmp_colour_killer(struct vo_interface *vo, _Bool notify, _Bool value);

extern inline void vo_vsync(struct vo_interface *vo, _Bool draw);
extern inline void vo_refresh(struct vo_interface *vo);
