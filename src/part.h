/*

Parts & interfaces.

Copyright 2018-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

A _part_ is a required part of a device.  Typically, sub-parts are freed
recursively.

An _interface_ is a connection between parts.  One part hosts the interface and
returns a pointer when its get_intf() method is called.  This pointer is then
passed to the attach_intf() method of both parts to populate their fields.

*/

#ifndef XROAR_PART_H_
#define XROAR_PART_H_

#include <stdint.h>
#include <stdlib.h>

#include "xalloc.h"

struct slist;
struct intf;

// (struct part) and (struct intf) are designed to be extended.

struct part {
	char *name;

	// Called by part_free() after disconnecting all interfaces and
	// components.
	void (*free)(struct part *part);

	// If this part is a component of another.
	struct part *parent;

	// A list of sub-parts that form part of this one.
	struct slist *components;

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

};

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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// allocate a new part
void *part_new(size_t psize);

// part_init() sets up part metadata
void part_init(struct part *p, const char *name);

void part_free(struct part *p);
void part_add_component(struct part *p, struct part *c, const char *id);
void part_remove_component(struct part *p, struct part *c);
struct part *part_component_by_id(struct part *p, const char *id);

// likewise, intf_new() and intf_init0().
struct intf *intf_new(size_t isize);
// intf_init0() is so named as it should only be called by p0.
void intf_init0(struct intf *i, struct part *p0, void *p0_idata, const char *name);
void intf_free(struct intf *i);

_Bool intf_attach(struct part *p0, void *p0_idata,
		  struct part *p1, void *p1_idata, const char *intf_name);

void intf_detach(struct intf *i);

#endif
