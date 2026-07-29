/** \file
 *
 *  \brief Generic CPU debug interface.
 *
 *  \copyright Copyright 2021-2026 Ciaran Anscomb
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
 *  Common to all CPUs, provides hooks for breakpoints and debugging with GDB.
 */

#ifndef XROAR_DEBUG_H_
#define XROAR_DEBUG_H_

#include "delegate.h"
#include "sds.h"

#include "part.h"

// For passing to get/set_flag
enum debug_cpu_flag {
	debug_cpu_flag_carry,
	debug_cpu_flag_overflow,
	debug_cpu_flag_zero,
	debug_cpu_flag_negative,
};

enum debug_endian {
	debug_endian_big = 0,
	debug_endian_little,
};

// Common CPU metadata, some of which is used to create a debug target.
// Also provides flag manipulation delegates.

struct debug_cpu {
	// GDB CPU architecture
	const char *architecture;
	enum debug_endian endian;

	// Indices of registers with common special meaning
	int register_ps;
	int register_sp;
	int register_pc;

	// Get/set processor flags
	DELEGATE_T1(bool, int) get_flag;
	DELEGATE_T2(void, int, bool) set_flag;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// A part may expose registers or pseudo-registers.  These structures allow us
// to specify how we interact with GDB to read or write them.

enum debug_feature_base_type {
	// GDB base types:
	debug_feature_base_type_uint,
	// Composite types:
	debug_feature_base_type_vector,
	debug_feature_base_type_flags,
	debug_feature_base_type_struct,
	debug_feature_base_type_union,
};

struct debug_feature_type;

struct debug_feature_field {
	const char *name;
	unsigned start;
	unsigned end;
	const struct debug_feature_type *type;
};

struct debug_feature_type {
	enum debug_feature_base_type type;
	const char *id;
	unsigned size;
	union {
		// vector: number of elements, and which type
		struct {
			unsigned nelems;
			const struct debug_feature_type *type;
		} as_vector;
		// flags, struct, union: list of types
		struct {
			unsigned nfields;
			const struct debug_feature_field *field;
		} as_struct;
	};
};

extern const struct debug_feature_type debug_feature_type_bool;
extern const struct debug_feature_type debug_feature_type_uint8;
extern const struct debug_feature_type debug_feature_type_uint16;
extern const struct debug_feature_type debug_feature_type_uint32;
extern const struct debug_feature_type debug_feature_type_code_ptr16;

struct debug_feature_reg {
	const char *name;
	unsigned bitsize;
	const struct debug_feature_type *type;
	const char *group;
};

struct debug_feature {
	const char *name;
	unsigned ntypes;
	const struct debug_feature_type **type;
	unsigned nregs;
	const struct debug_feature_reg *reg;
};

struct debug_part {
	unsigned nfeatures;
	const struct debug_feature **feature;

	// Get/set register values
	DELEGATE_T1(uint32, int) get_register;  // regno, -> value
	DELEGATE_T2(void, int, uint32) set_register;  // regno, value

#ifdef WANT_GDB_TARGET
	// Get/set composite registers; r, buffer_size, buffer
	DELEGATE_T3(int, int, unsigned, uint8p) get_register_composite;
	DELEGATE_T3(int, int, unsigned, cuint8p) set_register_composite;
#endif
};

// A machine will create a debug target based on the primary architecture
// provided by the CPU, optionally adding other parts.
//
// Full register names are formed from part name, register name, field names.
// Part names and register names can be NULL.  e.g. the CPU should be added
// with a NULL part name so that its registers appear at the top of the
// namespace.  Parts would usually be added with a unique part name and contain
// only one composite register with a NULL name itself (thus not forming part
// of the full name).

struct debug_target {
	char *architecture;
	enum debug_endian endian;

	unsigned nparts;
	const struct debug_part **part;
	char **part_name;

	// Cached information:
	unsigned nregs;  // total across all features
	const struct debug_feature_reg **reg;
	const struct debug_part **reg_part;
	unsigned *reg_size;  // in bytes (computed from reg[]->bitsize)
	unsigned *reg_num;  // within part
};

// Create a new debug target with specified architecture and endianness.

struct debug_target *debug_target_new(const char *architecture, enum debug_endian endian);

// Free debug target.

void debug_target_free(struct debug_target *target);

// Add a part to a target.  Updates the cached register information.

void debug_target_add_part(struct debug_target *target, const char *name,
			   const struct debug_part *dpart);

// Generate an XML target description for GDB.

#ifdef WANT_GDB_TARGET
sds debug_target_xml(struct debug_target *target);
#endif

int debug_register_by_name(struct debug_target *target, const char *name);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// These helpers will look up the appropriate register within a target and call
// the appropriate delegates.  They make certain assumptions about the delegates
// provided in a feature set:
//
// debug_{get,set}_register_composite() deal only in whole registers, whatever
// their structure.  The part a specified register is contatained within is
// determined from the target data, and then:
//
// - if {get,set}_register_composite delegates are provided, they are called directly
// - otherwise, the register's type definition is traversed, and
//   {get,set}_register is called at the level where a register or field can be
//   represented in <= 32 bits.

uint32_t debug_get_register(struct debug_target *target, int regno);

void debug_set_register(struct debug_target *target, int regno, uint32_t value);

#ifdef WANT_GDB_TARGET
int debug_get_register_composite(struct debug_target *target, int regno,
				 unsigned nbytes, uint8_t *buf);

int debug_set_register_composite(struct debug_target *target, int regno,
				 unsigned nbytes, const uint8_t *buf);
#endif

#endif

