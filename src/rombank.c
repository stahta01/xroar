/** \file
 *
 *  \brief ROM.
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
 */

#include "top-config.h"

// for fseeko()
#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "xalloc.h"

#include "crc32.h"
#include "crclist.h"
#include "fs.h"
#include "logging.h"
#include "rombank.h"

#ifdef HAVE_WASM
#include "wasm/wasm.h"
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct rombank *rombank_new(unsigned d_width, unsigned slot_size, unsigned nslots) {
	struct rombank *rb = xmalloc(sizeof(*rb));
	*rb = (struct rombank){0};

	rb->d_width = d_width;
	rb->slot_size = slot_size;
	rb->nslots = nslots;

	rb->sshift = 0;
	while ((1U << rb->sshift) < rb->slot_size) {
		++rb->sshift;
	}
	rb->slot_size = 1 << rb->sshift;
	rb->amask = rb->slot_size - 1;
	unsigned sbits = 0;
	while ((1U << sbits) < rb->nslots) {
		++sbits;
	}
	rb->nslots = 1 << sbits;
	rb->smask = rb->nslots - 1;

	rb->slot = xmalloc(rb->nslots * sizeof(*rb->slot));
	rb->d = xmalloc(rb->nslots * sizeof(*rb->d));
	for (unsigned i = 0; i < rb->nslots; i++) {
		rb->slot[i].filename = NULL;
		rb->slot[i].offset = 0;
		rb->slot[i].crc32 = CRC32_RESET;
		rb->d[i] = NULL;
	}
	rb->combined_crc32 = CRC32_RESET;

	return rb;
}

void rombank_free(struct rombank *rb) {
	if (!rb)
		return;
	for (unsigned i = 0; i < rb->nslots; i++) {
		if (rb->slot[i].filename) {
			free(rb->slot[i].filename);
		}
		if (rb->d[i]) {
			free(rb->d[i]);
		}
	}
	free(rb->slot);
	free(rb->d);
	free(rb);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Report ROM bank contents

void rombank_report(struct rombank *rb, const char *par, const char *name) {
	if (logging.level < 1)
		return;
	unsigned slot_k = rb->slot_size / 1024;
	LOG_PAR_MOD_PRINT(par, "rom", "%s (%u x %uK)\n", name, rb->nslots, slot_k);
	unsigned npopulated = 0;
	for (unsigned i = 0; i < rb->nslots; i++) {
		const char *filename = rb->slot[i].filename;
		const char *basename = filename;
		if (filename) {
			while (*filename) {
				if (*(filename + 1) != 0 &&
				    ((*filename == '/') || (*filename == '\\'))) {
					basename = filename + 1;
				}
				++filename;
			}
		}
		LOG_PRINT("\tSlot %3u: ", i);
		if (rb->d[i]) {
			LOG_PRINT("CRC32 0x%08x FILE %s", rb->slot[i].crc32, basename);
			if (rb->slot[i].offset > 0) {
				LOG_PRINT(" +0x%06lx ", (unsigned long)rb->slot[i].offset);
			}
			LOG_PRINT("\n");
			++npopulated;
		} else {
			LOG_PRINT("(unpopulated)\n");
		}
	}
	if (npopulated > 1) {
		LOG_PRINT("\tCombined: CRC32 0x%08x\n", rb->combined_crc32);
	}
}

// Verify ROM bank CRC.  Pass negative slot id to check combined CRC.

_Bool rombank_verify_crc(struct rombank *rb, const char *name, int slot,
			 const char *crclist, _Bool force, uint32_t *crc32) {
	_Bool present = 0;
	uint32_t check_crc32 = CRC32_RESET;
	if (slot < 0) {
		check_crc32 = rb->combined_crc32;
		// Check that any slot is populated
		for (unsigned i = 0; i < rb->nslots; i++) {
			if (rb->d[i]) {
				present = 1;
				break;
			}
		}
	} else if ((unsigned)slot < rb->nslots && rb->d[slot]) {
		check_crc32 = rb->slot[slot].crc32;
		present = 1;
	}

	_Bool valid = present && crclist_match(crclist, check_crc32);
	_Bool forced = present && !valid && force;

	if (forced) {
		LOG_DEBUG(1, "\t%s CRC32 forced to 0x%08x\n", name, *crc32);
		return 1;
	}

	*crc32 = check_crc32;

	if (valid) {
		LOG_DEBUG(1, "\t%s CRC32 valid\n", name);
		return 1;
	}

	LOG_DEBUG(1, "\t%s CRC32 INVALID\n", name);
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void recompute_crc32(struct rombank *);

// Load ROM image.  Returns number of slots loaded, or -1 on failure.

int rombank_load_image(struct rombank *rb, unsigned slot, const char *filename, off_t offset) {
	if (!filename) {
		return -1;
	}

	FILE *fd = fopen(filename, "rb");
	if (!fd) {
		LOG_MOD_DEBUG(2, "rom", "%s: %s\n", filename, strerror(errno));
		return -1;
	}

	off_t file_size = fs_file_size(fd);

	LOG_MOD_DEBUG(2, "rom", "opened: %s (%lld bytes)\n", filename, (long long)file_size);

	if (file_size > 256 && (file_size % 256) != 0) {
		offset += (file_size % 256);
	}

	if (file_size < 0 || offset >= file_size || fseeko(fd, offset, SEEK_SET) < 0) {
		fclose(fd);
		return -1;
	}
	file_size -= offset;

	while (file_size > 0 && slot < rb->nslots) {
		char *filename_dup = xstrdup(filename);
		if (rb->slot[slot].filename) {
			free(rb->slot[slot].filename);
		}
		rb->slot[slot].filename = filename_dup;
		rb->slot[slot].offset = offset;
		if (!rb->d[slot]) {
			rb->d[slot] = xmalloc(rb->slot_size);
		}
		memset(rb->d[slot], 0xff, rb->slot_size);
		size_t nread = fread(rb->d[slot], 1, rb->slot_size, fd);
		if (nread == 0) {
			break;
		}
		rb->slot[slot].crc32 = crc32_block(CRC32_RESET, rb->d[slot], rb->slot_size);
		file_size -= nread;
		offset += nread;
		++slot;
		if (nread < rb->slot_size) {
			break;
		}
	}
	recompute_crc32(rb);
	fclose(fd);

	return 1;
}

// Clear the ROM image in a single slot.

void rombank_clear_slot_image(struct rombank *rb, unsigned slot) {
	assert(rb != NULL);
	assert(rb->d != NULL);
	if (slot >= rb->nslots) {
		return;
	}
	rb->slot[slot].crc32 = CRC32_RESET;
	if (rb->d[slot]) {
		free(rb->d[slot]);
		rb->d[slot] = NULL;
	}
	recompute_crc32(rb);
}

// Clear all ROM images.

void rombank_clear_all_slots(struct rombank *rb) {
	for (unsigned i = 0; i < rb->nslots; i++) {
		rombank_clear_slot_image(rb, i);
	}
}

// Reload all images.

void rombank_reset(struct rombank *rb) {
	for (unsigned i = 0; i < rb->nslots; i++) {
		rombank_load_image(rb, i, rb->slot[i].filename, rb->slot[i].offset);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void recompute_crc32(struct rombank *rb) {
	assert(rb != NULL);
	assert(rb->d != NULL);
	rb->combined_crc32 = CRC32_RESET;
	for (unsigned i = 0; i < rb->nslots; i++) {
		rb->slot[i].crc32 = CRC32_RESET;
		if (rb->d[i]) {
			rb->slot[i].crc32 = crc32_block(CRC32_RESET, rb->d[i], rb->slot_size);
			rb->combined_crc32 = crc32_block(rb->combined_crc32, rb->d[i], rb->slot_size);
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Inline access functions.

extern inline uint8_t *rombank_a8(struct rombank *, unsigned a);
extern inline uint16_t *rombank_a16(struct rombank *, unsigned a);
extern inline void rombank_d8(struct rombank *, unsigned a, uint8_t *d);
extern inline void rombank_d16(struct rombank *, unsigned a, uint16_t *d);
