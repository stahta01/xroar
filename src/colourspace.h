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
 *
 *  Collects together various aspects of colour handling:
 *
 *   * Defining RGB colour space in terms of CIE XYZ coordinates and white
 *     point.
 *
 *   * Transfer functions (usually a power law; "gamma correction").
 *
 *   * Alternate encodings for RGB colour space (e.g. Y'UV or Y'IQ).  Encodings
 *     tend to be of gamma corrected signals, and so they're tied to the colour
 *     space and transfer function.
 *
 *   * Device profiles grouping the above.
 *
 *  Provides functions for:
 *
 *   * 3x3 matrix manipulation.
 *
 *   * Calculating conversion matrices.
 *
 *   * Applying or reversing a transfer function.
 *
 *   * Converting between linear RGB colour space and CIE XYZ values.
 *
 *   * Converting between two devices.
 */

#ifndef XROAR_COLOURSPACE_H_
#define XROAR_COLOURSPACE_H_

struct cs_profile {
	const char *name;
	const char *description;
	float ybr_to_yuv[3][3];
	float yuv_to_ybr[3][3];
	float yuv_to_rgb[3][3];
	float rgb_to_yuv[3][3];
	float claw, mlaw;
	float poff, clim, slope;
	// z_ = 1 - (x_ + y_)
	float xr, yr;
	float xg, yg;
	float xb, yb;
	float xn, yn;
	_Bool init;
	float RGB_to_XYZ[3][3];
	float XYZ_to_RGB[3][3];
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct cs_profile *cs_profile_by_id(int id);
struct cs_profile *cs_profile_by_name(const char *name);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void cs_matrix_mul_3x3_ijk(const float matrix[][3], float i, float j, float k,
			   float *iout, float *jout, float *kout);

void cs_matrix_mul_3x3(const float a[][3], const float b[][3], float out[][3]);

void cs_clamp(float *x, float *y, float *z);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// Gamma correction
//

// Monitor gamma (R'G'B' to RGB)
void cs_mlaw(const struct cs_profile *csin, float r, float g, float b,
	     float *Rout, float *Gout, float *Bout);

// Monitor gamma (single component)
float cs_mlaw_1(const struct cs_profile *cs, float v);

// Invert monitor gamma (RGB to R'G'B')
void cs_inverse_mlaw(const struct cs_profile *csin, float R, float G, float B,
		     float *rout, float *gout, float *bout);

// Camera gamma (RGB to R'G'B') - may differ from inverse monitor gamma
void cs_claw(const struct cs_profile *csin, float R, float G, float B,
	     float *rout, float *gout, float *bout);

// Camera gamma (single component)
float cs_claw_1(const struct cs_profile *cs, float V);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
// Intra-colourspace conversions
//

// Convert RGB to XYZ
void cs1_RGB_to_XYZ(struct cs_profile *cs, float R, float G, float B,
		    float *Xout, float *Yout, float *Zout);

// Convert XYZ to RGB
void cs1_XYZ_to_RGB(struct cs_profile *cs, float X, float Y, float Z,
		    float *Rout, float *Gout, float *Bout);

// Convert Y'U'V' to R'G'B'
void cs1_yuv_to_rgb(struct cs_profile *cs, float y, float u, float v,
		    float *rout, float *gout, float *bout);

// Convert R'G'B' to Y'U'V'
void cs1_rgb_to_yuv(struct cs_profile *cs, float r, float g, float b,
		    float *yout, float *uout, float *vout);

//
// Inter-colourspace conversions
//

// Convert Y',U',V' to RGB
void cs2_yuv_to_RGB(struct cs_profile *csin, struct cs_profile *csout,
		    float y, float u, float v,
		    float *Rout, float *Gout, float *Bout);

// Convert Y',B'-Y',R'-Y' to R'G'B'
void cs2_ybr_to_rgb(struct cs_profile *csin, struct cs_profile *csout,
		    float y, float b_y, float r_y,
		    float *rout, float *gout, float *bout);

// Convert Y',B'-Y',R'-Y' to RGB
void cs2_ybr_to_RGB(struct cs_profile *csin, struct cs_profile *csout,
		    float y, float b_y, float r_y,
		    float *Rout, float *Gout, float *Bout);

// Convert R'G'B' to Y',U',V'
void cs2_rgb_to_yuv(struct cs_profile *csin, struct cs_profile *csout,
		    float r, float g, float b,
		    float *yout, float *uout, float *vout);

// Convert RGB to Y',U',V'
void cs2_RGB_to_yuv(struct cs_profile *csin, struct cs_profile *csout,
		    float R, float G, float B,
		    float *yout, float *uout, float *vout);

#endif
