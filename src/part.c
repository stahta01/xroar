/*

Parts & interfaces.

Copyright 2018-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "slist.h"
#include "xalloc.h"

#include "logging.h"
#include "part.h"

struct part_component {
	char *id;
	struct part *p;
};

void *part_new(size_t psize) {
	void *m = xmalloc(psize < sizeof(struct part) ? sizeof(struct part) : psize);
	struct part *p = m;
	*p = (struct part){0};
	return m;
}

void part_init(struct part *p, const char *name) {
	p->name = xstrdup(name);
}

void part_free(struct part *p) {
	if (!p)
		return;

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

	slist_free_full(p->interfaces, (slist_free_func)intf_free);

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
	struct part_component *pc = xmalloc(sizeof(*pc));
	pc->id = xstrdup(id);
	pc->p = c;
	p->components = slist_prepend(p->components, pc);
	c->parent = p;
}

void part_remove_component(struct part *p, struct part *c) {
	assert(p != NULL);
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
