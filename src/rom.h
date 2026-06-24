/** \file
 *
 *  \brief ROM metadata.
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

#ifndef XROAR_ROM_H_
#define XROAR_ROM_H_

struct rom_meta {
	uint32_t crc32;
	uint32_t size;
	char *description;
	char *symtab;   // symbol table name
	char *part;     // part this ROM goes in
	char *machine;  // machine required (where != part)
	char *cart;     // cart required (where != part)
	uint8_t bank, slot;
	bool no_autorun;
};

// TODO: add command-line options and functions here to allow user to populate
// the dynamic list

struct rom_meta *rom_meta_by_crc32(uint32_t crc32, uint32_t size);

void rom_meta_remove_all(void);

#endif
