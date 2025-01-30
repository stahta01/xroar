/** \file
 *
 *  \brief Parts & interfaces.
 *
 *  \copyright Copyright 2018-2024 Ciaran Anscomb
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

extern const struct machine_partdb_entry dragon32_part;
extern const struct machine_partdb_entry dragon64_part;
extern const struct machine_partdb_entry dragonpro_part;
extern const struct machine_partdb_entry coco_part;
extern const struct machine_partdb_entry deluxecoco_part;
extern const struct machine_partdb_entry coco3_part;
extern const struct machine_partdb_entry mc10_part;

extern const struct cart_partdb_entry cart_rom_part;
extern const struct cart_partdb_entry deltados_part;
extern const struct cart_partdb_entry dragondos_part;
extern const struct cart_partdb_entry gmc_part;
extern const struct cart_partdb_entry idecart_part;
extern const struct cart_partdb_entry ikon_part;
extern const struct cart_partdb_entry mooh_part;
extern const struct cart_partdb_entry mpi_part;
extern const struct cart_partdb_entry nx32_part;
extern const struct cart_partdb_entry orch90_part;
extern const struct cart_partdb_entry race_part;
extern const struct cart_partdb_entry rsdos_part;

extern const struct partdb_entry ram_part;

extern const struct partdb_entry ay891x_part;
extern const struct partdb_entry hd6309_part;
extern const struct partdb_entry mc6801_part;
extern const struct partdb_entry mc6803_part;
extern const struct partdb_entry mc6809_part;
extern const struct partdb_entry mc6821_part;
extern const struct partdb_entry mc6847_part;
extern const struct partdb_entry mc6847t1_part;
extern const struct partdb_entry mc6883_part;
extern const struct partdb_entry mos6551_part;
extern const struct partdb_entry sn76489_part;
extern const struct partdb_entry spi65_part;
extern const struct partdb_entry tcc1014_1986_part;
extern const struct partdb_entry tcc1014_1987_part;
extern const struct partdb_entry wd2791_part;
extern const struct partdb_entry wd2793_part;
extern const struct partdb_entry wd2795_part;
extern const struct partdb_entry wd2797_part;

extern const struct partdb_entry spi_sdcard_part;

extern const struct cart_partdb_entry mcx128_part;

const struct partdb_entry *partdb[] = {
#ifdef WANT_MACHINE_ARCH_DRAGON
	(const struct partdb_entry *)&dragon32_part,
	(const struct partdb_entry *)&dragon64_part,
	(const struct partdb_entry *)&dragonpro_part,
	(const struct partdb_entry *)&coco_part,
	(const struct partdb_entry *)&deluxecoco_part,
#endif

#ifdef WANT_MACHINE_ARCH_COCO3
	(const struct partdb_entry *)&coco3_part,
#endif

#ifdef WANT_MACHINE_ARCH_MC10
	(const struct partdb_entry *)&mc10_part,
#endif

#ifdef WANT_CART_ARCH_DRAGON
	(const struct partdb_entry *)&cart_rom_part,
	(const struct partdb_entry *)&deltados_part,
	(const struct partdb_entry *)&dragondos_part,
	(const struct partdb_entry *)&gmc_part,
	(const struct partdb_entry *)&orch90_part,
	(const struct partdb_entry *)&rsdos_part,
	(const struct partdb_entry *)&idecart_part,
#ifndef HAVE_WASM
#ifdef WANT_EXPERIMENTAL
	(const struct partdb_entry *)&ikon_part,
#endif
#endif
	(const struct partdb_entry *)&mooh_part,
#ifndef HAVE_WASM
	(const struct partdb_entry *)&mpi_part,
	(const struct partdb_entry *)&nx32_part,
	(const struct partdb_entry *)&race_part,
#endif
#endif

	&ram_part,

#ifdef WANT_PART_MC6809
	&hd6309_part,
	&mc6809_part,
	&mc6821_part,
#endif

#ifdef WANT_PART_MC6801
	&mc6801_part,
	&mc6803_part,
#endif

#ifdef WANT_PART_MC6883
	&mc6883_part,
#endif

#ifdef WANT_PART_MC6847
	&mc6847_part,
	&mc6847t1_part,
#endif

	&mos6551_part,

#ifdef WANT_PART_TCC1014
	&tcc1014_1986_part,
	&tcc1014_1987_part,
#endif

	&ay891x_part,

#ifdef WANT_CART_ARCH_DRAGON
	&sn76489_part,
	&wd2791_part,
	&wd2793_part,
	&wd2795_part,
	&wd2797_part,
	&spi65_part,
	&spi_sdcard_part,
#endif

#ifdef WANT_CART_ARCH_MC10
	(const struct partdb_entry *)&mcx128_part,
#endif

};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct part_component {
	char *id;
	struct part *p;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

const struct partdb_entry *partdb_find_entry(const char *name) {
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(partdb); i++) {
		if (strcmp(partdb[i]->name, name) == 0) {
			return partdb[i];
		}
	}
	return NULL;
}

_Bool partdb_ent_is_a(const struct partdb_entry *pe, const char *is_a) {
	if (!pe)
		return 0;
	// always match the actual part entry...
	if (strcmp(pe->name, is_a) == 0)
		return 1;
	// otherwise, call the entry's is_a (NULL fine as part name; it's not
	// used for checking)
	return pe->funcs->is_a && pe->funcs->is_a(NULL, is_a);
}

_Bool partdb_is_a(const char *name, const char *is_a) {
	// find partname
	const struct partdb_entry *pe = partdb_find_entry(name);
	if (!pe)
		return 0;
	return partdb_ent_is_a(pe, is_a);
}

void partdb_foreach(partdb_match_func match, void *mdata, partdb_iter_func iter, void *idata) {
	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(partdb); i++) {
		const struct partdb_entry *pe = partdb[i];
		if (match && !match(pe, mdata))
			continue;
		iter(pe, idata);
	}
}

void partdb_foreach_is_a(partdb_iter_func iter, void *idata, const char *is_a) {
	partdb_foreach((partdb_match_func)partdb_ent_is_a, (void *)is_a, iter, idata);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct part *part_create(const char *name, void *options) {
	// Find partdb entry
	const struct partdb_entry *pe = partdb_find_entry(name);
	if (!pe)
		return NULL;

	struct part *p = NULL;

	// Ensure we are able to allocate something sensible
	assert(pe->funcs->allocate != NULL);
	// ... and do so
	p = pe->funcs->allocate();
	if (!p)
		return NULL;
	// If there's a description, print it (log level >= 1)
	if (pe->description) {
		// Special-case the logging level
		if (partdb_ent_is_a(pe, "machine") || partdb_ent_is_a(pe, "cart")) {
			LOG_PAR_MOD_DEBUG(1, "part", pe->name, "%s\n", pe->description);
		} else {
			LOG_PAR_MOD_DEBUG(2, "part", pe->name, "%s\n", pe->description);
		}
	}

	// Initialise, populating useful stuff from partdb
	*p = (struct part){0};
	p->partdb = pe;
	if (pe->funcs->initialise) {
		pe->funcs->initialise(p, options);
	}

	// Finish
	if (pe->funcs->finish && !pe->funcs->finish(p)) {
		part_free(p);
		p = NULL;
	}

	return p;
}

void *part_new(size_t psize) {
	void *m = xmalloc(psize < sizeof(struct part) ? sizeof(struct part) : psize);
	struct part *p = m;
	*p = (struct part){0};
	PART_DEBUG("part_new() = %p\n", p);
	return m;
}

void part_free(struct part *p) {
	if (!p)
		return;

	const struct partdb_entry *pe = p->partdb;

	PART_DEBUG("part_free(%p) '%s'\n", p, pe->name);

	if (p->parent) {
		part_remove_component(p->parent, p);
		p->parent = NULL;
	}

	// part-specific free() called first as it may have to do stuff
	// before interfaces & components are destroyed.  mustn't actually free
	// the structure itself.
	if (pe->funcs->free) {
		pe->funcs->free(p);
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

	free(p);
}

// Add a subcomponent with a specified id.
void part_add_component(struct part *p, struct part *c, const char *id) {
	assert(p != NULL);
	if (c == NULL)
		return;
	PART_DEBUG("part_add_component('%s', '%s', '%s')\n", p->partdb->name, c->partdb->name, id);
	struct part_component *pc = xmalloc(sizeof(*pc));
	pc->id = xstrdup(id);
	pc->p = c;
	p->components = slist_prepend(p->components, pc);
	c->parent = p;
}

void part_remove_component(struct part *p, struct part *c) {
	assert(p != NULL);
	PART_DEBUG("part_remove_component('%s', '%s')\n", p->partdb->name, c->partdb->name);
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

_Bool part_is_a(struct part *p, const char *is_a) {
	if (!p)
		return 0;
	const struct partdb_entry *pe = p->partdb;
	if (strcmp(pe->name, is_a) == 0)
		return 1;
	return pe->funcs->is_a ? pe->funcs->is_a(p, is_a) : 0;
}

struct part *part_deserialise(struct ser_handle *sh) {
	struct part *p = NULL;
	const struct partdb_entry *pe = NULL;
	int tag;
	while (!ser_error(sh) && (tag = ser_read_tag(sh)) > 0) {
		switch (tag) {
		case PART_SER_DATA:
			// Data for the part itself
			{
				char *name = ser_read_string(sh);
				if (name) {
					pe = partdb_find_entry(name);
					if (!pe) {
						LOG_MOD_WARN("part", "can't deserialise '%s'\n", name);
						free(name);
						ser_set_error(sh, ser_error_format);
						return NULL;
					}
					free(name);
					assert(pe->funcs != NULL);
					assert(pe->funcs->ser_struct_data != NULL);
					p = pe->funcs->allocate();
					assert(p != NULL);
					// If there's a description, print it
					if (pe->description) {
						if (partdb_ent_is_a(pe, "machine") || partdb_ent_is_a(pe, "cart")) {
							LOG_PAR_MOD_DEBUG(1, "part", pe->name, "%s\n", pe->description);
						} else {
							LOG_PAR_MOD_DEBUG(2, "part", pe->name, "%s\n", pe->description);
						}
					}
					p->partdb = pe;
					ser_read_struct_data(sh, pe->funcs->ser_struct_data, p);
				}
			}
			break;

		case PART_SER_PART:
			// Once for each sub-part
			{
				if (!p) {
					LOG_MOD_DEBUG(3, "part", "part_deserialise(): DATA must come before sub-PARTs\n");
					ser_set_error(sh, ser_error_format);
					part_free(p);
					return NULL;
				}
				assert(pe != NULL);
				char *id = ser_read_string(sh);
				if (!id) {
					LOG_MOD_DEBUG(3, "part", "part_deserialise(): bad subpart for '%s'\n", pe->name);
					ser_set_error(sh, ser_error_format);
					part_free(p);
					return NULL;
				}
				struct part *c = part_deserialise(sh);
				if (!c) {
					LOG_MOD_DEBUG(3, "part", "part_deserialise(): failed to deserialise '%s' for '%s'\n", id, pe->name);
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

	if (!p) {
		LOG_MOD_DEBUG(3, "part", "part_deserialise(): failed to deserialise part\n");
		return NULL;
	}
	assert(pe != NULL);

	if (pe->funcs->finish && !pe->funcs->finish(p)) {
		LOG_MOD_DEBUG(3, "part", "part_deserialise(): failed to finalise '%s'\n", pe->name);
		part_free(p);
		return 0;
	}

	return p;
}

void part_serialise(struct part *p, struct ser_handle *sh) {
	if (!p)
		return;
	assert(p->partdb != NULL);

	const struct partdb_entry *pe = p->partdb;

	ser_write_open_string(sh, PART_SER_DATA, pe->name);
	assert(pe->funcs->ser_struct_data != NULL);
	ser_write_struct_data(sh, pe->funcs->ser_struct_data, p);
	for (struct slist *iter = p->components; iter; iter = iter->next) {
		struct part_component *pc = iter->data;
		ser_write_open_string(sh, PART_SER_PART, pc->id);
		part_serialise(pc->p, sh);
	}
	ser_write_close_tag(sh);
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
