/** \file
 *
 *  \brief RGB colourspace conversions.
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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
#include <string.h>
#include <math.h>

#include <stdio.h>

#include "colourspace.h"

// A set of standard colour profiles
static struct cs_profile profiles[] = {
	{
		.name = "pal",
		.description = "PAL/SECAM",
		.ybr_to_yuv = {
			{ 1.0, 0.000, 0.000 },
			{ 0.0, 0.493, 0.000 },
			{ 0.0, 0.000, 0.877 }
		},
		.yuv_to_ybr = {
			{ 1.0, 0.0,    0.0 },
			{ 0.0, 2.0284, 0.0 },
			{ 0.0, 0.0,    1.14025 }
		},
		.yuv_to_rgb = {
			{ 1.0,  0.000,  1.140 },
			{ 1.0, -0.396, -0.581 },
			{ 1.0,  2.029,  0.000 }
		},
		.rgb_to_yuv = {
			{ 0.29895,  0.586572, 0.114481 },
			{-0.14734, -0.289094, 0.436431 },
			{ 0.61496, -0.514537, -0.100422 }
		},
		.claw = 0.45, .mlaw = 2.8,
		.poff = 0.099, .clim = 0.018, .slope = 4.5,
		.xr=0.64, .yr=0.33,
		.xg=0.29, .yg=0.60,
		.xb=0.15, .yb=0.06,
		.xn=0.312713, .yn=0.329016
	},

	{
		.name = "ntsc",
		.description = "NTSC",
		// this actually represents (Y',B'-Y',R'-Y') -> (Y',I',Q')
		.ybr_to_yuv = {
			{ 1.0,  0.00,  0.00 },
			{ 0.0, -0.27,  0.74 },
			{ 0.0,  0.41,  0.48 }
		},
		// and its inverse
		.yuv_to_ybr = {
			{ 1.0,  0.0,   0.0 },
			{ 0.0, -1.109, 1.709 },
			{ 0.0,  0.947, 0.624 }
		},
		// and this is actually Y'I'Q' -> R'G'B'
		.yuv_to_rgb = {
			{ 1.0,  0.956,  0.621 },
			{ 1.0, -0.272, -0.647 },
			{ 1.0, -1.105,  1.702 }
		},
		// and its inverse
		.rgb_to_yuv = {
			{ 0.299,  0.587,  0.114 },
			{ 0.596, -0.274, -0.321 },
			{ 0.211, -0.523,  0.312 }
		},
		.claw = 0.45, .mlaw = 2.2,
		.poff = 0.099, .clim = 0.018, .slope = 4.5,
		.xr=0.67, .yr=0.33,
		.xg=0.21, .yg=0.71,
		.xb=0.14, .yb=0.08,
		.xn=0.310063, .yn=0.316158
	},

	{
		.name = "smptec",
		.description = "SMPTE-C",
		.claw = 0.45, .mlaw = 2.2,
		.poff = 0.099, .clim = 0.018, .slope = 4.5,
		.xr=0.630, .yr=0.340,
		.xg=0.310, .yg=0.595,
		.xb=0.155, .yb=0.070,
		.xn=0.312713, .yn=0.329016
	},

	{
		.name = "srgb",
		.description = "sRGB",
		.claw = 1./2.4, .mlaw = 2.4,
		.poff = 0.055, .clim = 0.0031308, .slope = 12.92,
		.xr=0.64, .yr=0.33,
		.xg=0.30, .yg=0.60,
		.xb=0.15, .yb=0.06,
		.xn=0.312713, .yn=0.329016
	},

	{
		.name = "adobe1998",
		.description = "Adobe RGB (1998)",
		.mlaw = 563./256.,
		.xr = 0.64, .yr = 0.33,
		.xg = 0.21, .yg = 0.71,
		.xb = 0.15, .yb = 0.06,
		.xn = 0.3127, .yn = 0.3290,
	},

	{
		.name = "wide",
		.description = "Wide Gamut RGB",
		.mlaw = 563./256.,
		.xr=0.7347, .yr=0.2653,
		.xg=0.1152, .yg=0.8264,
		.xb=0.1566, .yb=0.0177,
		.xn=0.3457, .yn=0.3585
	},
};

#define NUM_PROFILES (int)(sizeof(profiles) / sizeof(struct cs_profile))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct cs_profile *cs_profile_by_id(int id) {
	// No id field, just map to array index
	if (id < 0 || id >= NUM_PROFILES) return NULL;
	return &profiles[id];
}

struct cs_profile *cs_profile_by_name(const char *name) {
	for (unsigned i = 0; i < NUM_PROFILES; i++) {
		if (0 == strcmp(name, profiles[i].name)) {
			return &profiles[i];
		}
	}
	return NULL;
}

#if 0
static void print_matrix_3x3(float m[][3]) {
	for (int j = 0; j < 3; j++) {
		printf("[ ");
		for (int i = 0; i < 3; i++) {
			printf("% f", m[j][i]);
			if (i < 2) {
				printf(", ");
			} else {
				printf("  ");
			}
		}
		printf("]\n");
	}
}
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*

Computing the matrices required for converting arbitrary linear RGB
colourspaces to and from CIE XYZ.

Derived from: Color spaces FAQ - David Bourgin
http://www.poynton.com/notes/Timo/colorspace-faq

From the FAQ (8.3 - CIE XYZ):

  |Xn|   |xr xg xb|   |ar|               |ar|   |Xn|   |xr xg xb| -1
  |Yn| = |yr yg yb| * |ag|   therefore   |ag| = |Yn| * |yr yg yb|
  |Zn|   |zr zg zb|   |ab|               |ab|   |Zn|   |zr zg zb|

The inverse of that matrix is quite complicated, but simplifies out to what's
done below to find ar, ag, ab.  From there the other matrix in the FAQ can be
used:

  |X|   |xr*ar xg*ag xb*ab|   |Red  |
  |Y| = |yr*ar yg*ag xb*ab| * |Green|
  |Z|   |zr*ar zg*ag xb*ab|   |Blue |

And inverting that square matrix gets us the reverse transform.  Again too
complex to show here, but simplifies to the values assigned to RGB_to_XYZ
below.

*/

// XXX why have i left this url here?
// http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html

static void matrix_invert_3x3(const float in[][3], float out[][3]);

static void create_xyz_rgb_matrix(struct cs_profile *p) {
	float xr = p->xr, yr = p->yr;
	float xg = p->xg, yg = p->yg;
	float xb = p->xb, yb = p->yb;
	float xn = p->xn, yn = p->yn;

	float Xr = xr / yr;
	float Yr = 1.;
	float Zr = (1. - xr - yr) / yr;

	float Xg = xg / yg;
	float Yg = 1.;
	float Zg = (1. - xg - yg) / yg;

	float Xb = xb / yb;
	float Yb = 1.;
	float Zb = (1. - xb - yb) / yb;

	float Xn = xn / yn;
	float Yn = 1.;
	float Zn = (1. - xn - yn) / yn;

	float xyz[3][3] = {
		{ Xr, Xg, Xb },
		{ Yr, Yg, Yb },
		{ Zr, Zg, Zb } };
	float xyz_[3][3];

	matrix_invert_3x3(xyz, xyz_);

	float Sr, Sg, Sb;

	cs_matrix_mul_3x3_ijk(xyz_, Xn, Yn, Zn, &Sr, &Sg, &Sb);

	p->RGB_to_XYZ[0][0] = Sr * Xr;
	p->RGB_to_XYZ[0][1] = Sg * Xg;
	p->RGB_to_XYZ[0][2] = Sb * Xb;

	p->RGB_to_XYZ[1][0] = Sr * Yr;
	p->RGB_to_XYZ[1][1] = Sg * Yg;
	p->RGB_to_XYZ[1][2] = Sb * Yb;

	p->RGB_to_XYZ[2][0] = Sr * Zr;
	p->RGB_to_XYZ[2][1] = Sg * Zg;
	p->RGB_to_XYZ[2][2] = Sb * Zb;

	matrix_invert_3x3(p->RGB_to_XYZ, p->XYZ_to_RGB);

	p->init = 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void cs_matrix_mul_3x3_ijk(const float matrix[][3], float i, float j, float k,
			   float *iout, float *jout, float *kout) {
	*iout = i * matrix[0][0] + j * matrix[0][1] + k * matrix[0][2];
	*jout = i * matrix[1][0] + j * matrix[1][1] + k * matrix[1][2];
	*kout = i * matrix[2][0] + j * matrix[2][1] + k * matrix[2][2];
}

// remember to reverse the order if you're combining matrices...

void cs_matrix_mul_3x3(const float a[][3], const float b[][3], float out[][3]) {
	out[0][0] = a[0][0]*b[0][0] + a[0][1]*b[1][0] + a[0][2]*b[2][0];
	out[0][1] = a[0][0]*b[0][1] + a[0][1]*b[1][1] + a[0][2]*b[2][1];
	out[0][2] = a[0][0]*b[0][2] + a[0][1]*b[1][2] + a[0][2]*b[2][2];

	out[1][0] = a[1][0]*b[0][0] + a[1][1]*b[1][0] + a[1][2]*b[2][0];
	out[1][1] = a[1][0]*b[0][1] + a[1][1]*b[1][1] + a[1][2]*b[2][1];
	out[1][2] = a[1][0]*b[0][2] + a[1][1]*b[1][2] + a[1][2]*b[2][2];

	out[2][0] = a[2][0]*b[0][0] + a[2][1]*b[1][0] + a[2][2]*b[2][0];
	out[2][1] = a[2][0]*b[0][1] + a[2][1]*b[1][1] + a[2][2]*b[2][1];
	out[2][2] = a[2][0]*b[0][2] + a[2][1]*b[1][2] + a[2][2]*b[2][2];
}

void cs_clamp(float *x, float *y, float *z) {
	*x = (*x < 0.0) ? 0.0 : ((*x > 1.0) ? 1.0 : *x);
	*y = (*y < 0.0) ? 0.0 : ((*y > 1.0) ? 1.0 : *y);
	*z = (*z < 0.0) ? 0.0 : ((*z > 1.0) ? 1.0 : *z);
}

static void matrix_invert_3x3(const float in[][3], float out[][3]) {
	float d = in[0][0]*(in[1][1]*in[2][2] - in[2][1]*in[1][2])
	        - in[0][1]*(in[1][0]*in[2][2] - in[1][2]*in[2][0])
	        + in[0][2]*(in[1][0]*in[2][1] - in[1][1]*in[2][0]);
	// Note, matrix inversion depends on the determinant being non-zero,
	// but for our defined matrices, this shouldn't ever come up...
	float id = 1. / d;

	out[0][0] = (in[1][1]*in[2][2] - in[2][1]*in[1][2]) * id;
	out[0][1] = (in[0][2]*in[2][1] - in[2][2]*in[0][1]) * id;
	out[0][2] = (in[0][1]*in[1][2] - in[1][1]*in[0][2]) * id;

	out[1][0] = (in[1][2]*in[2][0] - in[2][2]*in[1][0]) * id;
	out[1][1] = (in[0][0]*in[2][2] - in[2][0]*in[0][2]) * id;
	out[1][2] = (in[0][2]*in[1][0] - in[1][2]*in[0][0]) * id;

	out[2][0] = (in[1][0]*in[2][1] - in[2][0]*in[1][1]) * id;
	out[2][1] = (in[0][1]*in[2][0] - in[2][1]*in[0][0]) * id;
	out[2][2] = (in[0][0]*in[1][1] - in[1][0]*in[0][1]) * id;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// Gamma
//

void cs_mlaw(const struct cs_profile *cs, float r, float g, float b,
	     float *Rout, float *Gout, float *Bout) {
	*Rout = cs_mlaw_1(cs, r);
	*Gout = cs_mlaw_1(cs, g);
	*Bout = cs_mlaw_1(cs, b);
}

float cs_mlaw_1(const struct cs_profile *cs, float v) {
	if (v < (cs->clim * cs->slope)) {
		return v / cs->slope;
	} else {
		return powf((v+cs->poff)/(1.+cs->poff), cs->mlaw);
	}
}

// Apply inverse of monitor gamma
void cs_inverse_mlaw(const struct cs_profile *cs, float R, float G, float B,
		     float *rout, float *gout, float *bout) {
	if (R < cs->clim) {
		*rout = R * cs->slope;
	} else {
		*rout = ((1.+cs->poff)*powf(R, 1./cs->mlaw)) - cs->poff;
	}
	if (G < cs->clim) {
		*gout = G * cs->slope;
	} else {
		*gout = ((1.+cs->poff)*powf(G, 1./cs->mlaw)) - cs->poff;
	}
	if (B < cs->clim) {
		*bout = B * cs->slope;
	} else {
		*bout = ((1.+cs->poff)*powf(B, 1./cs->mlaw)) - cs->poff;
	}
}

// Apply camera gamma (may be different to inverse of monitor gamma)
void cs_claw(const struct cs_profile *cs, float R, float G, float B,
	     float *rout, float *gout, float *bout) {
	*rout = cs_claw_1(cs, R);
	*gout = cs_claw_1(cs, G);
	*bout = cs_claw_1(cs, B);
}

float cs_claw_1(const struct cs_profile *cs, float V) {
	if (V < cs->clim) {
		return V * cs->slope;
	} else {
		return ((1.+cs->poff)*powf(V, cs->claw)) - cs->poff;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// Simple conversions
//

void cs1_RGB_to_XYZ(struct cs_profile *cs, float R, float G, float B,
		    float *Xout, float *Yout, float *Zout) {
	if (!cs->init)
		create_xyz_rgb_matrix(cs);
	cs_matrix_mul_3x3_ijk(cs->RGB_to_XYZ, R,G,B, Xout,Yout,Zout);
}

void cs1_XYZ_to_RGB(struct cs_profile *cs, float X, float Y, float Z,
		    float *Rout, float *Gout, float *Bout) {
	if (!cs->init)
		create_xyz_rgb_matrix(cs);
	cs_matrix_mul_3x3_ijk(cs->XYZ_to_RGB, X,Y,Z, Rout,Gout,Bout);
}

void cs1_yuv_to_rgb(struct cs_profile *cs, float y, float u, float v,
		    float *rout, float *gout, float *bout) {
	cs_matrix_mul_3x3_ijk(cs->yuv_to_rgb, y,u,v, rout,gout,bout);
}

void cs1_rgb_to_yuv(struct cs_profile *cs, float r, float g, float b,
		    float *yout, float *uout, float *vout) {
	cs_matrix_mul_3x3_ijk(cs->rgb_to_yuv, r,g,b, yout, uout, vout);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// Inter-colourspace conversions
//

// Convert Y',U',V' in one colourspace to RGB in another colourspace
void cs2_yuv_to_RGB(struct cs_profile *csin, struct cs_profile *csout,
		    float y, float u, float v,
		    float *Rout, float *Gout, float *Bout) {
	float r, g, b, R, G, B, X, Y, Z;
	cs1_yuv_to_rgb(csin, y,u,v, &r,&g,&b);
	cs_mlaw(csin, r,g,b, &R,&G,&B);
	cs1_RGB_to_XYZ(csin, R,G,B, &X,&Y,&Z);
	//cs_clamp(&X,&Y,&Z);
	cs1_XYZ_to_RGB(csout, X,Y,Z, Rout,Gout,Bout);
}

// Convert R',G',B' in one colourspace to R'G'B' in another colourspace
void cs2_rgb_to_rgb(struct cs_profile *csin, struct cs_profile *csout,
		    float r, float g, float b,
		    float *rout, float *gout, float *bout) {
	float R, G, B, X, Y, Z;
	cs_mlaw(csin, r,g,b, &R,&G,&B);
	cs1_RGB_to_XYZ(csin, R,G,B, &X,&Y,&Z);
	//cs_clamp(&X,&Y,&Z);
	cs1_XYZ_to_RGB(csout, X,Y,Z, &R,&G,&B);
	cs_inverse_mlaw(csout, R,G,B, rout,gout,bout);
}

// Convert Y',B'-Y',R'-Y' in one colourspace to RGB in another colourspace
void cs2_ybr_to_RGB(struct cs_profile *csin, struct cs_profile *csout,
		    float y, float b_y, float r_y,
		    float *Rout, float *Gout, float *Bout) {
	float u, v;
	cs_matrix_mul_3x3_ijk(csin->ybr_to_yuv, y,b_y,r_y, &y,&u,&v);
	cs2_yuv_to_RGB(csin, csout, y,u,v, Rout,Gout,Bout);
}

// Convert Y',B'-Y',R'-Y' in one colourspace to R'G'B' in another colourspace
void cs2_ybr_to_rgb(struct cs_profile *csin, struct cs_profile *csout,
		    float y, float b_y, float r_y,
		    float *rout, float *gout, float *bout) {
	float R, G, B;
	cs2_ybr_to_RGB(csin, csout, y, b_y, r_y, &R, &G, &B);
	cs_inverse_mlaw(csout, R,G,B, rout,gout,bout);
}

// Convert RGB in one colourspace to Y',U',V' in another colourspace
void cs2_RGB_to_yuv(struct cs_profile *csin, struct cs_profile *csout,
		    float R, float G, float B,
		    float *yout, float *uout, float *vout) {
	float X, Y, Z, r, g, b;
	cs1_RGB_to_XYZ(csin, R,G,B, &X,&Y,&Z);
	//cs_clamp(&X,&Y,&Z);
	cs1_XYZ_to_RGB(csout, X,Y,Z, &R,&G,&B);
	cs_mlaw(csout, R,G,B, &r,&g,&b);
	cs1_rgb_to_yuv(csout, r,g,b, yout,uout,vout);
}

// Convert R'G'B' in one colourspace to Y',U',V' in another colourspace
void cs2_rgb_to_yuv(struct cs_profile *csin, struct cs_profile *csout,
		    float r, float g, float b,
		    float *yout, float *uout, float *vout) {
	float R, G, B;
	cs_inverse_mlaw(csin, r, g, b, &R, &G, &B);
	cs2_RGB_to_yuv(csin, csout, R, G, B, yout, uout, vout);
}

// Convert RGB in one colourspace to Y',B-Y',R-Y' in another colourspace
void cs2_RGB_to_ybr(struct cs_profile *csin, struct cs_profile *csout,
		    float R, float G, float B,
		    float *yout, float *b_yout, float *r_yout) {
	float y, u, v;
	cs2_RGB_to_yuv(csin, csout, R, G, B, &y, &u, &v);
	cs_matrix_mul_3x3_ijk(csout->yuv_to_ybr, y, u, v, yout, b_yout, r_yout);
}

// Convert R'G'B' in one colourspace to Y',B-Y',R-Y' in another colourspace
void cs2_rgb_to_ybr(struct cs_profile *csin, struct cs_profile *csout,
		    float r, float g, float b,
		    float *yout, float *b_yout, float *r_yout) {
	float y, u, v;
	cs2_rgb_to_yuv(csin, csout, r, g, b, &y, &u, &v);
	cs_matrix_mul_3x3_ijk(csout->yuv_to_ybr, y, u, v, yout, b_yout, r_yout);
}

#if 0
int main(int argc, char **argv) {

	if (argc < 2) {
		fprintf(stderr, "usage: %s space1 [space2]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (0 == strcmp(argv[1], "-l")) {
		for (int i = 0; i < NUM_PROFILES; i++) {
			printf("\t%-8s %s\n", profiles[i].name, profiles[i].description);
		}
		exit(EXIT_SUCCESS);
	}

	struct cs_profile *src = cs_profile_by_name(argv[1]);
	if (!src) {
		fprintf(stderr, "bad colourspace: %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	create_xyz_rgb_matrix(src);

	if (argc > 2) {
		struct cs_profile *dest = cs_profile_by_name(argv[2]);
		if (!dest) {
			fprintf(stderr, "bad colourspace: %s\n", argv[2]);
			exit(EXIT_FAILURE);
		}
		create_xyz_rgb_matrix(dest);

		float tmp[3][3];

		cs_matrix_mul_3x3(dest->XYZ_to_RGB, src->RGB_to_XYZ, tmp);
		printf("\n%s to %s:\n", argv[1], argv[2]);
		print_matrix_3x3(tmp);

		cs_matrix_mul_3x3(src->XYZ_to_RGB, dest->RGB_to_XYZ, tmp);
		printf("\n%s to %s:\n", argv[2], argv[1]);
		print_matrix_3x3(tmp);

		exit(EXIT_SUCCESS);
	}

	printf("\nRGB_to_XYZ %s:\n", argv[1]);
	print_matrix_3x3(src->RGB_to_XYZ);

	printf("\nXYZ_to_RGB %s:\n", argv[1]);
	print_matrix_3x3(src->XYZ_to_RGB);

	exit(EXIT_SUCCESS);
}
#endif
