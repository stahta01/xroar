/** \file
 *
 *  \brief Tandy Colour Computer additional support.
 *
 *  \copyright Copyright 2026 Ciaran Anscomb
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

#ifndef XROAR_DRAGON_COCO_H_
#define XROAR_DRAGON_COCO_H_

struct dragon;

// CoCo common routines.  Also used by deluxecoco.c.

// Tie PIA lines according to RAM configuration
void coco_pia_configuration(struct dragon *md);

#endif
