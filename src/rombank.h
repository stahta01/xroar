/** \file
 *
 *  \brief ROM bank support.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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
 * A ROM bank consists of a number of contiguous slots.  Each slot must be a
 * power of 2, as must the number of slots.  Any slot may be empty.  If a
 * shadowed region is required, create a bank with fewer slots.
 *
 * Usage:
 *
 * Load ROM image with rombank_load_image().  If a ROM image is larger than the
 * destination slot, it will fill subsequent slots in the bank.
 *
 * Calling rombank_reset() will reload the images.
 */

#ifndef XROAR_ROMBANK_H_
#define XROAR_ROMBANK_H_

#include <stdint.h>
#include <sys/types.h>

struct rombank_slot_file {
	char *filename;  // absolute path
	off_t offset;    // into file for this slot
	uint32_t crc32;
};

struct rombank {
	unsigned d_width;
	unsigned slot_size;
	unsigned nslots;

	struct rombank_slot_file *slot;
	uint32_t combined_crc32;

	unsigned sshift;
	unsigned smask;
	unsigned amask;
	void **d;
};

struct rombank *rombank_new(unsigned d_width, unsigned slot_size, unsigned nslots);
void rombank_free(struct rombank *);

// Report ROM bank contents

void rombank_report(struct rombank *, const char *par, const char *name);

// Verify ROM bank CRC.  Pass negative slot id to check combined CRC.  If force
// is set to true, the value pointed to by crc32 is not overwritten if the CRC
// did not check.

_Bool rombank_verify_crc(struct rombank *rb, const char *name, int slot,
			 const char *crclist, _Bool force, uint32_t *crc32);

// Load ROM image.

int rombank_load_image(struct rombank *, unsigned slot, const char *filename, off_t offset);

// Clear the ROM image in a single slot.

void rombank_clear_slot_image(struct rombank *, unsigned slot);

// Clear all ROM images.

void rombank_clear_all_slots(struct rombank *);

// Reload all images.

void rombank_reset(struct rombank *);

// Inline access functions.

inline uint8_t *rombank_a8(struct rombank *rb, unsigned a) {
	unsigned slot = (a >> rb->sshift) & rb->smask;
	if (!rb->d[slot])
		return NULL;
	return &(((uint8_t *)rb->d[slot])[a & rb->amask]);
}

inline uint16_t *rombank_a16(struct rombank *rb, unsigned a) {
	unsigned slot = (a >> rb->sshift) & rb->smask;
	if (!rb->d[slot])
		return NULL;
	return &(((uint16_t *)rb->d[slot])[a & rb->amask]);
}

inline void rombank_d8(struct rombank *rb, unsigned a, uint8_t *d) {
	uint8_t *p = rombank_a8(rb, a);
	if (!p)
		return;
	*d = *p;
}

inline void rombank_d16(struct rombank *rb, unsigned a, uint16_t *d) {
	uint16_t *p = rombank_a16(rb, a);
	if (!p)
		return;
	*d = *p;
}

#endif
