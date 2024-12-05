/** \file
 *
 *  \brief Parts & interfaces.
 *
 *  \copyright Copyright 2018-2021 Ciaran Anscomb
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
 * A _part_ is a required part of a device.  Typically, sub-parts are freed
 * recursively.
 *
 * An _interface_ is a connection between parts.  One part hosts the interface
 * and returns a pointer when its get_intf() method is called.  This pointer is
 * then passed to the attach_intf() method of both parts to populate their
 * fields.
 */

#ifndef XROAR_PART_H_
#define XROAR_PART_H_

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>

#include "xalloc.h"

// For now, we're not using the interface features...
#undef WANT_INTF

struct ser_handle;
struct ser_struct_data;
struct slist;
#ifdef WANT_INTF
struct intf;
#endif

/** \brief Part database entry functions
 *
 * Called by part_create() and part_deserialise().
 *
 * To allocate memory for the part, 'allocate' is called.
 *
 * Then, either part_create() calls 'initialise' to set up intial state, or
 * part_deserialise() populates the memory according to 'ser_struct_data'.
 * Either should end up creating and adding any required sub-parts.  Both
 * initialise 'partdb' within the part to point to its partdb entry.
 *
 * Finally, 'finish' is called, which is expected to find any sub-parts and set
 * up the connections between them.  If it returns false, a dependency wasn't
 * found, and the part is freed.
 *
 * Note: the 'options' argument passed to 'intialise' by part_create() is
 * replaced with the part name if NULL.
 */

struct partdb_entry_funcs {
	// Metadata about how to serialise/deserialise the part
	const struct ser_struct_data *ser_struct_data;

	// Allocate memory for the part, often also essential initialisation
	struct part *(* const allocate)(void);

	// Create any necessary sub-parts (not called by deserialise, as the
	// process of deserialisation creates these)
	void (* const initialise)(struct part *p, void *options);

	// Finish any internal setup once all sub-parts are available (either
	// by the call to initialise(), or through deserialisation)
	_Bool (* const finish)(struct part *part);

	// Any extra work needed before free()ing the allocated memory
	void (* const free)(struct part *part);

	// In addition to the part name (in the partdb entry), this returns
	// true for any other simple strings that this part identifies as
	_Bool (* const is_a)(struct part *p, const char *name);
};

/** \brief Part database mapping entry
 *
 * Maps a name to a set of part functions.  Multiple variants of a part can
 * then use the same general approach to creation.
 */

struct partdb_entry {
	const char *name;
	const char *description;
	const struct partdb_entry_funcs *funcs;
};

typedef _Bool (*partdb_match_func)(const struct partdb_entry *pe, void *mdata);
typedef void (*partdb_iter_func)(const struct partdb_entry *pe, void *idata);

// (struct part) and (struct intf) are designed to be extended.

struct part {
	const struct partdb_entry *partdb;

	// If this part is a component of another.
	struct part *parent;

	// A list of sub-parts that form part of this one.
	struct slist *components;

#ifdef WANT_INTF
	// A list of interfaces attached to this part.  When freeing a part,
	// this is traversed, and detach_intf() is called for any where part ==
	// intf->p0.
	struct slist *interfaces;

	// An interface joins two parts with an agreed-upon named structure.
	// p0 is the primary, and handles allocation of space for the interface
	// structure.  p1 is secondary, and will share access to the structure
	// allocated by p0.

	// get_intf() - called on p0 - returns the named interface from
	// the part or NULL if not supported.  The part-specific 'data' may
	// help identify a specific interface from a set.
	struct intf *(*get_intf)(struct part *part, const char *intf_name, void *idata);

	// intf_attach() will call p0->get_intf() on p0, populate the interface
	// with details about p1, then call p0->attach_intf().  p0 should call
	// p1->attach_intf() at an appropriate point in its own initialisation.
	// Returns true on success.
	_Bool (*attach_intf)(struct part *part, struct intf *intf);

	// As with attaching, intf_detach() will call p0->detach_intf(), which
	// should itself call p1->detach_intf().  After detaching, the
	// interface should be considered unusable until reacquired with
	// get_intf() (as it may have been freed, and require reallocating).
	void (*detach_intf)(struct part *part, struct intf *intf);
#endif

};

#ifdef WANT_INTF
struct intf {
	char *name;

	void (*free)(struct intf *intf);

	// Primary - controls the allocation of this (struct intf).
	struct part *p0;
	void *p0_idata;

	// Secondary - shares this (struct intf) with p0.
	struct part *p1;
	void *p1_idata;
};
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// find partdb entry by name
const struct partdb_entry *partdb_find_entry(const char *name);

// test type of a partdb entry
_Bool partdb_ent_is_a(const struct partdb_entry *pe, const char *is_a);

// test type of a partdb entry by name
_Bool partdb_is_a(const char *partname, const char *is_a);

// iterate over partdb, calling 'iter' for every entry for which 'match'
// returns true (or all entries if 'match' is NULL)
void partdb_foreach(partdb_match_func match, void *mdata, partdb_iter_func iter, void *idata);

// iterate over partdb, calling 'iter' for every entry for which 'is_a' is true
void partdb_foreach_is_a(partdb_iter_func iter, void *idata, const char *is_a);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// allocate a new part by name, looked up in partdb
struct part *part_create(const char *name, void *options);

// allocate a new part the old way
FUNC_ATTR_RETURNS_NONNULL void *part_new(size_t psize);

// part_init() sets up part metadata
void part_init(struct part *p, const char *name);

void part_free(struct part *p);
void part_add_component(struct part *p, struct part *c, const char *id);
void part_remove_component(struct part *p, struct part *c);
struct part *part_component_by_id(struct part *p, const char *id);
// same, but verify name with is_a()
struct part *part_component_by_id_is_a(struct part *p, const char *id, const char *name);

// test type of an already-created part
_Bool part_is_a(struct part *p, const char *is_a);

void part_serialise(struct part *p, struct ser_handle *sh);
struct part *part_deserialise(struct ser_handle *sh);

#ifdef WANT_INTF
// likewise, intf_new() and intf_init0().
struct intf *intf_new(size_t isize);
// intf_init0() is so named as it should only be called by p0.
void intf_init0(struct intf *i, struct part *p0, void *p0_idata, const char *name);
void intf_free(struct intf *i);

_Bool intf_attach(struct part *p0, void *p0_idata,
		  struct part *p1, void *p1_idata, const char *intf_name);

void intf_detach(struct intf *i);
#endif

#endif
