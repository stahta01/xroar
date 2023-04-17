/** \file
 *
 *  \brief Video renderer generic operations.
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
 *  This file contains templates of generic scanline rendering routines.  It is
 *  included into vo_render.c multiple times to define a set of renderers
 *  handling pixels of different types.
 */

// Before including this file, define:
//
// VR_PTYPE as renderer pixel type (eg uint16_t)
//
// VR_SUFFIX as renderer name (eg uint16, in case the pixel type isn't
// something that can form part of a symbol name).

#ifndef VR_PTYPE
#error "VR_PTYPE must be defined"
#endif

#ifndef VR_SUFFIX
#error "VR_SUFFIX must be defined"
#endif

#ifdef TNAME_
#undef TNAME__
#undef TNAME_
#endif
#define TNAME__(f,s) f ## _ ## s
#define TNAME_(f,s) TNAME__(f,s)
#ifdef TNAME
#undef TNAME
#endif
#define TNAME(f) TNAME_(f, VR_SUFFIX)

typedef VR_PTYPE (*TNAME(map_rgb_func))(struct vo_render *, int, int, int);

struct TNAME(vo_render) {
	struct vo_render generic;

	struct {
		VR_PTYPE palette[256];
		VR_PTYPE cc_2bit[2][4];
		VR_PTYPE cc_5bit[2][32];
	} cmp;

	struct {
		VR_PTYPE palette[256];
	} rgb;

	TNAME(map_rgb_func) map_rgb;
};

static void TNAME(set_palette_entry)(void *sptr, int palette, int index,
				     int R, int G, int B);

static void TNAME(render_cmp_palette)(void *sptr, unsigned burstn,
				      unsigned npixels, uint8_t const *data);
static void TNAME(render_rgb_palette)(void *sptr, unsigned burstn,
				      unsigned npixels, uint8_t const *data);
static void TNAME(render_cmp_2bit)(void *sptr, unsigned burstn,
				   unsigned npixels, uint8_t const *data);
static void TNAME(render_cmp_5bit)(void *sptr, unsigned burstn,
				   unsigned npixels, uint8_t const *data);
static void TNAME(next_line)(struct vo_render *vr);

// Create an instance of the renderer for this basic type using the specified
// colour mapping function.

struct vo_render *TNAME(renderer_new)(TNAME(map_rgb_func) map_rgb) {
	struct TNAME(vo_render) *vrt = xmalloc(sizeof(*vrt));
	*vrt = (struct TNAME(vo_render)){0};
	struct vo_render *vr = &vrt->generic;

	vrt->map_rgb = map_rgb;
	vr->set_palette_entry = TNAME(set_palette_entry);
	vr->render_cmp_palette = TNAME(render_cmp_palette);
	vr->render_rgb_palette = TNAME(render_rgb_palette);
	vr->render_cmp_2bit = TNAME(render_cmp_2bit);
	vr->render_cmp_5bit = TNAME(render_cmp_5bit);
	vr->next_line = TNAME(next_line);

	return vr;
}

static void TNAME(set_palette_entry)(void *sptr, int palette, int index,
				     int R, int G, int B) {
	struct TNAME(vo_render) *vrt = sptr;
	struct vo_render *vr = &vrt->generic;

	VR_PTYPE colour = vrt->map_rgb(vr, R, G, B);

	switch (palette) {
	case VO_RENDER_PALETTE_CMP:
		vrt->cmp.palette[index & 0xff] = colour;
		break;
	case VO_RENDER_PALETTE_RGB:
		vrt->rgb.palette[index & 0xff] = colour;
		break;
	case VO_RENDER_PALETTE_CMP_2BIT:
		vrt->cmp.cc_2bit[(index>>2)&1][index&3] = colour;
		break;
	case VO_RENDER_PALETTE_CMP_5BIT:
		vrt->cmp.cc_5bit[(index>>5)&1][index&31] = colour;
		break;
	default:
		break;
	}
}

// Variants of render_line with different CPU/accuracy tradeoffs

// Render line using composite palette

static void TNAME(render_cmp_palette)(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct TNAME(vo_render) *vrt = sptr;
	struct vo_render *vr = &vrt->generic;
	(void)burstn;
	(void)npixels;

	if (!data ||
	    vr->scanline < vr->viewport.y ||
	    vr->scanline >= (vr->viewport.y + vr->viewport.h)) {
		vr->scanline++;
		return;
	}

	uint8_t const *src = data + vr->viewport.x;
	VR_PTYPE *dest = vr->pixel;
	for (int i = vr->viewport.w >> 2; i; i--) {
		uint8_t c0 = *(src++);
		uint8_t c1 = *(src++);
		uint8_t c2 = *(src++);
		uint8_t c3 = *(src++);
		VR_PTYPE p0 = vrt->cmp.palette[c0];
		VR_PTYPE p1 = vrt->cmp.palette[c1];
		VR_PTYPE p2 = vrt->cmp.palette[c2];
		VR_PTYPE p3 = vrt->cmp.palette[c3];
		*(dest++) = p0;
		*(dest++) = p1;
		*(dest++) = p2;
		*(dest++) = p3;
	}
	vr->pixel = (VR_PTYPE *)vr->pixel + vr->buffer_pitch;
	vr->scanline++;
}

// Render line using RGB palette

static void TNAME(render_rgb_palette)(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct TNAME(vo_render) *vrt = sptr;
	struct vo_render *vr = &vrt->generic;
	(void)burstn;
	(void)npixels;

	if (!data ||
	    vr->scanline < vr->viewport.y ||
	    vr->scanline >= (vr->viewport.y + vr->viewport.h)) {
		vr->scanline++;
		return;
	}

	uint8_t const *src = data + vr->viewport.x;
	VR_PTYPE *dest = vr->pixel;
	for (int i = vr->viewport.w >> 2; i; i--) {
		uint8_t c0 = *(src++);
		uint8_t c1 = *(src++);
		uint8_t c2 = *(src++);
		uint8_t c3 = *(src++);
		VR_PTYPE p0 = vrt->rgb.palette[c0];
		VR_PTYPE p1 = vrt->rgb.palette[c1];
		VR_PTYPE p2 = vrt->rgb.palette[c2];
		VR_PTYPE p3 = vrt->rgb.palette[c3];
		*(dest++) = p0;
		*(dest++) = p1;
		*(dest++) = p2;
		*(dest++) = p3;
	}
	vr->pixel = (VR_PTYPE *)vr->pixel + vr->buffer_pitch;
	vr->scanline++;
}

// Render artefact colours using simple 2-bit LUT.

static void TNAME(render_cmp_2bit)(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct TNAME(vo_render) *vrt = sptr;
	struct vo_render *vr = &vrt->generic;
	(void)burstn;
	(void)npixels;

	if (!data ||
	    vr->scanline < vr->viewport.y ||
	    vr->scanline >= (vr->viewport.y + vr->viewport.h)) {
		vr->scanline++;
		return;
	}

	uint8_t const *src = data + vr->viewport.x;
	VR_PTYPE *dest = vr->pixel;
	unsigned p = !(vr->cmp.phase & 2);
	for (int i = vr->viewport.w >> 2; i; i--) {
		VR_PTYPE p0, p1, p2, p3;
		uint8_t c0 = *src;
		uint8_t c2 = *(src + 2);
		if (vr->cmp.is_black_or_white[c0] && vr->cmp.is_black_or_white[c2]) {
			unsigned aindex = (vr->cmp.is_black_or_white[c0] << 1) | (vr->cmp.is_black_or_white[c2] & 1);
			p0 = p1 = p2 = p3 = vrt->cmp.cc_2bit[p][aindex & 3];
		} else {
			uint8_t c1 = *(src+1);
			uint8_t c3 = *(src+3);
			p0 = vrt->cmp.palette[c0];
			p1 = vrt->cmp.palette[c1];
			p2 = vrt->cmp.palette[c2];
			p3 = vrt->cmp.palette[c3];
		}
		src += 4;
		*(dest++) = p0;
		*(dest++) = p1;
		*(dest++) = p2;
		*(dest++) = p3;
	}
	vr->pixel = (VR_PTYPE *)vr->pixel + vr->buffer_pitch;
	vr->scanline++;
}

// Render artefact colours using 5-bit LUT.  Only explicitly black or white
// runs of pixels are considered to contribute to artefact colours, otherwise
// they are passed through from the palette.

static void TNAME(render_cmp_5bit)(void *sptr, unsigned burstn, unsigned npixels, uint8_t const *data) {
	struct TNAME(vo_render) *vrt = sptr;
	struct vo_render *vr = &vrt->generic;
	(void)burstn;
	(void)npixels;

	if (!data ||
	    vr->scanline < vr->viewport.y ||
	    vr->scanline >= (vr->viewport.y + vr->viewport.h)) {
		vr->scanline++;
		return;
	}

	uint8_t const *src = data + vr->viewport.x;
	VR_PTYPE *dest = vr->pixel;
	unsigned p = !(vr->cmp.phase & 2);
	unsigned ibwcount = 0;
	unsigned aindex = 0;
	uint8_t ibw0 = vr->cmp.is_black_or_white[*(src-6)];
	uint8_t ibw1 = vr->cmp.is_black_or_white[*(src-2)];
	if (ibw0 && ibw1) {
		ibwcount = 7;
		aindex = (ibw0 & 1) ? 14 : 0;
		aindex |= (ibw1 & 1) ? 1 : 0;
	}
	for (int i = vr->viewport.w >> 2; i; i--) {
		VR_PTYPE p0, p1, p2, p3;

		uint8_t ibw2 = vr->cmp.is_black_or_white[*(src+2)];
		uint8_t ibw4 = vr->cmp.is_black_or_white[*(src+4)];
		uint8_t ibw6 = vr->cmp.is_black_or_white[*(src+6)];

		ibwcount = ((ibwcount << 1) | (ibw2 >> 1)) & 7;
		aindex = ((aindex << 1) | (ibw4 & 1));
		if (ibwcount == 7) {
			p0 = p1 = vrt->cmp.cc_5bit[p][aindex & 31];
		} else {
			uint8_t c0 = *src;
			uint8_t c1 = *(src+1);
			p0 = vrt->cmp.palette[c0];
			p1 = vrt->cmp.palette[c1];
		}

		ibwcount = ((ibwcount << 1) | (ibw4 >> 1)) & 7;
		aindex = ((aindex << 1) | (ibw6 & 1));
		if (ibwcount == 7) {
			p2 = p3 = vrt->cmp.cc_5bit[!p][aindex & 31];
		} else {
			uint8_t c2 = *(src+2);
			uint8_t c3 = *(src+3);
			p2 = vrt->cmp.palette[c2];
			p3 = vrt->cmp.palette[c3];
		}

		src += 4;
		*(dest++) = p0;
		*(dest++) = p1;
		*(dest++) = p2;
		*(dest++) = p3;
	}
	vr->pixel = (VR_PTYPE *)vr->pixel + vr->buffer_pitch;
	vr->scanline++;
}

// Advance pixel pointer to next line in buffer

static void TNAME(next_line)(struct vo_render *vr) {
	vr->pixel = (VR_PTYPE *)vr->pixel + vr->buffer_pitch;
	vr->scanline++;
}
