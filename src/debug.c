/** \file
 *
 *  \brief Generic debug interface.
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

#include "top-config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"

#include "debug.h"
#include "logging.h"

// Some built-in composite types used in target descriptions

const struct debug_feature_type debug_feature_type_bool = {
	.type = debug_feature_base_type_uint,
	.id = "bool",
	.size = 1,
};

const struct debug_feature_type debug_feature_type_uint8 = {
	.type = debug_feature_base_type_uint,
	.id = "uint8",
	.size = 1,
};

const struct debug_feature_type debug_feature_type_uint16 = {
	.type = debug_feature_base_type_uint,
	.id = "uint16",
	.size = 2,
};

const struct debug_feature_type debug_feature_type_uint32 = {
	.type = debug_feature_base_type_uint,
	.id = "uint32",
	.size = 4,
};

const struct debug_feature_type debug_feature_type_code_ptr16 = {
	.type = debug_feature_base_type_uint,
	.id = "code_ptr",
	.size = 2,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct debug_target *debug_target_new(const char *architecture, enum debug_endian endian) {
        struct debug_target *target = xmalloc(sizeof(*target));
	*target = (struct debug_target){0};
	target->architecture = xstrdup(architecture);
	target->endian = endian;
	return target;
}

void debug_target_free(struct debug_target *target) {
	free(target->architecture);
	for (unsigned i = 0; i < target->nparts; ++i) {
		free(target->part_name[i]);
	}
	free(target->part);
	free(target->part_name);
	free(target->reg);
	free(target->reg_part);
	free(target->reg_size);
	free(target->reg_num);
	free(target);
}

void debug_target_add_part(struct debug_target *target, const char *name,
			   const struct debug_part *dpart) {
	int n = target->nparts++;
	target->part = xrealloc(target->part, target->nparts * sizeof(*target->part));
	target->part_name = xrealloc(target->part_name, target->nparts * sizeof(*target->part_name));
	target->part[n] = dpart;
	if (name)
		target->part_name[n] = xstrdup(name);
	else
		target->part_name[n] = NULL;
	unsigned reg_num = 0;  // within part
	for (unsigned i = 0; i < dpart->nfeatures; ++i) {
		const struct debug_feature *feature = dpart->feature[i];
		for (unsigned j = 0; j < feature->nregs; ++j) {
			unsigned r = target->nregs++;
			target->reg = xrealloc(target->reg, target->nregs * sizeof(*target->reg));
			target->reg_part = xrealloc(target->reg_part, target->nregs * sizeof(*target->reg_part));
			target->reg_size = xrealloc(target->reg_size, target->nregs * sizeof(*target->reg_size));
			target->reg_num = xrealloc(target->reg_num, target->nregs * sizeof(*target->reg_num));
			target->reg[r] = &feature->reg[j];
			target->reg_part[r] = dpart;
			target->reg_size[r] = (feature->reg[j].bitsize + 7) / 8;
			target->reg_num[r] = reg_num++;
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Helper to add a composite type definition to a target description

static sds debug_add_type_xml(sds xml, const struct debug_feature_type *type) {
	const char *typestr = NULL;
	switch (type->type) {
	case debug_feature_base_type_uint:
		return xml;
	case debug_feature_base_type_vector:
		xml = sdscatprintf(xml, "<vector id=\"%s\" count=\"%d\"",
				    type->id, type->as_vector.nelems);
		if (type->as_vector.type) {
			xml = sdscatprintf(xml, " type=\"%s\"", type->as_vector.type->id);
		}
		return sdscatprintf(xml, "/>");
	case debug_feature_base_type_flags:
		typestr = "flags";
		break;
	case debug_feature_base_type_struct:
		typestr = "struct";
		break;
	case debug_feature_base_type_union:
		typestr = "union";
		break;
	default:
		LOG_MOD_ERROR("debug", "unknown type while constructing target description\n");
		abort();
	}
	xml = sdscatprintf(xml, "<%s id=\"%s\" size=\"%u\">", typestr, type->id, type->size);
	for (unsigned i = 0; i < type->as_struct.nfields; ++i) {
		xml = sdscatprintf(xml, "<field name=\"%s\" start=\"%u\" end=\"%u\"",
				   type->as_struct.field[i].name,
				   type->as_struct.field[i].start,
				   type->as_struct.field[i].end);
		const struct debug_feature_type *ttype = type->as_struct.field[i].type;
		if (ttype && ttype->id) {
			xml = sdscatprintf(xml, " type=\"%s\"", ttype->id);
		}
		xml = sdscatprintf(xml, "/>");
	}
	return sdscatprintf(xml, "</%s>", typestr);
}

// Generate a complete GDB target description

sds debug_target_xml(struct debug_target *target) {
	sds xml = sdscatprintf(sdsempty(),
			       "<?xml version=\"1.0\"?>"
			       "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
			       "<target><architecture>%s</architecture>", target->architecture);

	// For each feature...
	for (unsigned i = 0; i < target->nparts; ++i) {
		const struct debug_part *dpart = target->part[i];
		const char *part_name = target->part_name[i];
		for (unsigned j = 0; j < dpart->nfeatures; ++j) {
			const struct debug_feature *feature = dpart->feature[j];

			xml = sdscatprintf(xml, "<feature name=\"%s\">", feature->name);

			// Emit the type definitions
			for (unsigned k = 0; k < feature->ntypes; ++k) {
				const struct debug_feature_type *type = feature->type[k];
				xml = debug_add_type_xml(xml, type);
			}

			// Then each register definition in turn
			for (unsigned k = 0; k < feature->nregs; ++k) {
				const struct debug_feature_reg *reg = &feature->reg[k];
				const char *name = reg->name ? reg->name : part_name;
				xml = sdscatprintf(xml, "<reg name=\"%s\" bitsize=\"%d\"", name, reg->bitsize);
				if (reg->type && reg->type->id) {
					xml = sdscatprintf(xml, " type=\"%s\"", reg->type->id);
				}
				// GDB docs say "If no group is specified, GDB
				// will not display the register in info
				// registers", but this appears to be false
				// - not specifying group is equivalent to
				// specifying group "general".
				if (reg->group) {
					xml = sdscatprintf(xml, " group=\"%s\"", reg->group);
				}
				xml = sdscatprintf(xml, "/>");
			}

			xml = sdscatprintf(xml, "</feature>");
		}
	}

	return sdscatprintf(xml, "</target>");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

int debug_register_by_name(struct debug_target *target, const char *name) {
	for (unsigned i = 0; i < target->nregs; ++i) {
		if (0 == strcmp(target->reg[i]->name, name)) {
			return i;
		}
	}
	return -1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

uint32_t debug_get_register(struct debug_target *target, int regno) {
	if (regno < 0 || (unsigned)regno >= target->nregs)
		return (uint32_t)-1;
	const struct debug_part *part = target->reg_part[regno];
	return DELEGATE_CALL(part->get_register, regno);
}

void debug_set_register(struct debug_target *target, int regno, uint32_t value) {
	if (regno < 0 || (unsigned)regno >= target->nregs)
		return;
	const struct debug_part *part = target->reg_part[regno];
	DELEGATE_CALL(part->set_register, regno, value);
}

static int value_to_buf(enum debug_endian endian, unsigned vsize, uint32_t value,
			size_t dsize, uint8_t *dst) {
	if (vsize > 4)
		return 0;
	if (dsize < (size_t)vsize)
		return 0;
	for (unsigned i = 0; i < vsize; ++i) {
		int shift;
		switch (endian) {
		case debug_endian_big: default: shift = (vsize - i - 1) * 8; break;
		case debug_endian_little: shift = i * 8;
		}
		int v = (value >> shift) & 0xff;
		*(dst++) = v;
		--dsize;
	}
	return vsize;
}

static int value_from_buf(enum debug_endian endian, size_t ssize, const uint8_t *src,
			    unsigned vsize, uint32_t *value) {
	if (vsize > 4)
		return 0;
	if (ssize < (size_t)vsize)
		return 0;
	bool valid = 1;
	uint32_t rval = 0;
	for (unsigned i = 0; i < vsize; ++i) {
		int v = *(src++);
		int shift;
		switch (endian) {
		case debug_endian_big: default: shift = (vsize - i - 1) * 8; break;
		case debug_endian_little: shift = i * 8;
		}
		rval |= (uint32_t)(v & 0xff) << shift;
		--ssize;
	}
	if (valid && value) {
		*value = rval;
	}
	return vsize;
}

int debug_get_register_composite(struct debug_target *target, int regno,
				 unsigned nbytes, uint8_t *buf) {
	if (regno < 0 || (unsigned)regno >= target->nregs)
		return 0;

	const struct debug_part *dpart = target->reg_part[regno];
	unsigned reg_size = target->reg_size[regno];
	unsigned reg_num = target->reg_num[regno];

	if (reg_size > 4) {
		assert(DELEGATE_DEFINED(dpart->get_register_composite));
		return DELEGATE_CALL(dpart->get_register_composite, reg_num, nbytes, buf);
	}
	assert(DELEGATE_DEFINED(dpart->get_register));
	// Else call the non-composite delegate and assemble result into buffer
	uint32_t value = DELEGATE_CALL(dpart->get_register, reg_num);
	return value_to_buf(target->endian, reg_size, value, nbytes, buf);
}

int debug_set_register_composite(struct debug_target *target, int regno,
				 unsigned nbytes, const uint8_t *buf) {
	if (regno < 0 || (unsigned)regno >= target->nregs)
		return 0;

	const struct debug_part *dpart = target->reg_part[regno];
	unsigned reg_size = target->reg_size[regno];
	unsigned reg_num = target->reg_num[regno];

	if (reg_size > 4) {
		assert(DELEGATE_DEFINED(dpart->set_register_composite));
		return DELEGATE_CALL(dpart->set_register_composite, reg_num, nbytes, buf);
	}
	assert(DELEGATE_DEFINED(dpart->set_register));
	// Else call the non-composite delegate and assemble result into buffer
	uint32_t value = 0;
	int n = value_from_buf(target->endian, nbytes, buf, reg_size, &value);
	if (n > 0)
		DELEGATE_CALL(dpart->set_register, reg_num, value);
	return n;
}
