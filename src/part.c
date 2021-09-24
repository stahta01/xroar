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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// Comment this out for debugging
#define PART_DEBUG(...)

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "slist.h"
#include "xalloc.h"

#include "logging.h"
#include "part.h"
#include "serialise.h"

#ifndef PART_DEBUG
#define PART_DEBUG(...) LOG_PRINT(__VA_ARGS__)
#endif

#define PART_SER_PART (1)
#define PART_SER_DATA (2)

struct part_component {
	char *id;
	struct part *p;
};

struct partdb_entry {
	const char *name;
	struct part *(* const deserialise)(struct ser_handle *sh);
};

struct partdb_entry partdb[] = {
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct partdb_entry *partdb_find_entry(const char *name) {
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(partdb); i++) {
		if (strcmp(partdb[i].name, name) == 0) {
			return &partdb[i];
		}
	}
	return NULL;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void *part_new(size_t psize) {
	void *m = xmalloc(psize < sizeof(struct part) ? sizeof(struct part) : psize);
	struct part *p = m;
	*p = (struct part){0};
	PART_DEBUG("part_new() = %p\n", p);
	return m;
}

void part_init(struct part *p, const char *name) {
	p->name = xstrdup(name);
	PART_DEBUG("part_init(%p) '%s'\n", p, name);
}

void part_free(struct part *p) {
	if (!p)
		return;

	PART_DEBUG("part_free(%p) '%s'\n", p, p->name);

	if (p->parent) {
		part_remove_component(p->parent, p);
		p->parent = NULL;
	}

	// part-specific free() called first as it may have to do stuff
	// before interfaces & components are destroyed.  mustn't actually free
	// the structure itself.
	if (p->free) {
		p->free(p);
	}

#ifdef WANT_INTF
	slist_free_full(p->interfaces, (slist_free_func)intf_free);
#endif

	// slist_free_full() does not permit freeing functions to modify the list,
	// so as that may happen, free components manually:
	while (p->components) {
		struct part_component *pc = p->components->data;
		struct part *c = pc->p;
		p->components = slist_remove(p->components, pc);
		free(pc->id);
		free(pc);
		part_free(c);
	}

	if (p->name) {
		free(p->name);
		p->name = NULL;
	}
	free(p);
}

// Add a subcomponent with a specified id.
void part_add_component(struct part *p, struct part *c, const char *id) {
	assert(p != NULL);
	if (c == NULL)
		return;
	PART_DEBUG("part_add_component('%s', '%s', '%s')\n", p->name, c->name, id);
	struct part_component *pc = xmalloc(sizeof(*pc));
	pc->id = xstrdup(id);
	pc->p = c;
	p->components = slist_prepend(p->components, pc);
	c->parent = p;
}

void part_remove_component(struct part *p, struct part *c) {
	assert(p != NULL);
	PART_DEBUG("part_remove_component('%s', '%s')\n", p->name, c->name);
	for (struct slist *ent = p->components; ent; ent = ent->next) {
		struct part_component *pc = ent->data;
		if (pc->p == c) {
			p->components = slist_remove(p->components, pc);
			free(pc->id);
			free(pc);
			return;
		}
	}

}

struct part *part_component_by_id(struct part *p, const char *id) {
	assert(p != NULL);
	for (struct slist *ent = p->components; ent; ent = ent->next) {
		struct part_component *pc = ent->data;
		if (0 == strcmp(pc->id, id)) {
			return pc->p;
		}
	}
	return NULL;
}

struct part *part_component_by_id_is_a(struct part *p, const char *id, const char *name) {
	struct part *c = part_component_by_id(p, id);
	if (!c)
		return NULL;
	if (!name || part_is_a(c, name))
		return c;
	return NULL;
}

_Bool part_is_a(struct part *p, const char *name) {
	if (!p)
		return 0;
	if (strcmp(p->name, name) == 0)
		return 1;
	return p->is_a ? p->is_a(p, name) : 0;
}

void part_serialise(struct part *p, struct ser_handle *sh) {
	if (!p)
		return;
	assert(p->name != NULL);

	// Check that we would be able to deserialise this part.  This is
	// mostly to catch missed entries in the partb during development.
	struct partdb_entry *ent = partdb_find_entry(p->name);
	if (!ent) {
		LOG_WARN("PART: can't serialise '%s'\n", p->name);
		ser_set_error(sh, ser_error_format);
		return;
	}

	ser_write_open_string(sh, PART_SER_DATA, p->name);
	p->serialise(p, sh);
	for (struct slist *iter = p->components; iter; iter = iter->next) {
		struct part_component *pc = iter->data;
		ser_write_open_string(sh, PART_SER_PART, pc->id);
		part_serialise(pc->p, sh);
	}
	ser_write_close_tag(sh);
}

struct part *part_deserialise(struct ser_handle *sh) {
	struct part *p = NULL;
	int tag;
	while ((tag = ser_read_tag(sh)) > 0) {
		switch (tag) {
		case PART_SER_DATA:
			{
				char *name = ser_read_string(sh);
				if (name) {
					struct partdb_entry *ent = partdb_find_entry(name);
					free(name);
					if (!ent) {
						LOG_WARN("PART: can't deserialise '%s'\n", name);
						ser_set_error(sh, ser_error_format);
						return NULL;
					}
					p = ent->deserialise(sh);
				}
			}
			break;
		case PART_SER_PART:
			{
				if (!p) {
					LOG_DEBUG(3, "part_deserialise(): DATA must come before sub-PARTs\n");
					ser_set_error(sh, ser_error_format);
					part_free(p);
					return NULL;
				}
				char *id = ser_read_string(sh);
				if (!id) {
					LOG_DEBUG(3, "part_deserialise(): bad subpart for '%s'\n", p->name);
					ser_set_error(sh, ser_error_format);
					part_free(p);
					return NULL;
				}
				struct part *c = part_deserialise(sh);
				if (!c) {
					LOG_DEBUG(3, "part_deserialise(): failed to deserialise '%s' for '%s'\n", id, p->name);
					free(id);
					part_free(p);
					return NULL;
				}
				part_add_component(p, c, id);
				free(id);
			}
			break;
		default:
			break;
		}
	}

	if (!p)
		return NULL;

	assert(p->finish != NULL);
	if (!p->finish(p)) {
		LOG_DEBUG(3, "part_deserialise(): failed to finalise '%s'\n", p->name);
		part_free(p);
		return 0;
	}

	return p;
}

#ifdef WANT_INTF
// Helper for parts that need to allocate space for an interface.
struct intf *intf_new(size_t isize) {
	if (isize < sizeof(struct intf))
		isize = sizeof(struct intf);
	struct intf *i = xmalloc(isize);
	*i = (struct intf){0};
	return i;
}

void intf_init0(struct intf *i, struct part *p0, void *p0_idata, const char *name) {
	i->p0 = p0;
	i->p0_idata = p0_idata;
	i->name = xstrdup(name);
}

void intf_free(struct intf *i) {
	intf_detach(i);
	if (i->name) {
		free(i->name);
		i->name = NULL;
	}
	if (i->free) {
		i->free(i);
	} else {
		free(i);
	}
}

_Bool intf_attach(struct part *p0, void *p0_idata,
		  struct part *p1, void *p1_idata, const char *intf_name) {

	assert(p0 != NULL);
	assert(p0->get_intf != NULL);
	assert(p0->attach_intf != NULL);
	assert(p1 != NULL);
	assert(p1->attach_intf != NULL);

	struct intf *i = p0->get_intf(p0, intf_name, p0_idata);
	if (!i)
		return 0;

	// it is the responsibility of get_intf() to populate p0 fields.  p0
	// may delegate handling of this interface to one of its subcomponents,
	// so they may change.
	assert(i->p0 != NULL);
	p0 = i->p0;

	i->p1 = p1;
	i->p1_idata = p1_idata;

	if (!p0->attach_intf(p0, i))
		return 0;

	// similarly, p1 fields may be updated by delegation.
	p1 = i->p1;

	p0->interfaces = slist_prepend(p0->interfaces, i);
	p1->interfaces = slist_prepend(p1->interfaces, i);

	return 1;
}

void intf_detach(struct intf *i) {
	assert(i != NULL);
	struct part *p0 = i->p0;
	assert(p0 != NULL);
	assert(p0->detach_intf != NULL);
	struct part *p1 = i->p1;
	assert(p1 != NULL);

	// p0 will call p1->detach_intf at an appropriate point
	p0->detach_intf(p0, i);

	// interface may now have been freed, but it's still safe to use the
	// pointer to remove it from lists:
	p0->interfaces = slist_remove(p0->interfaces, i);
	p1->interfaces = slist_remove(p1->interfaces, i);
}
#endif
