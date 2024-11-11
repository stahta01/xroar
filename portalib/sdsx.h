/** \file
 *
 *  \brief SDS extras.
 *
 *  \copyright Copyright 2018-2024 Ciaran Anscomb
 *
 *  \licenseblock
 *
 *  This is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU Lesser General Public License as published by the Free
 *  Software Foundation; either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  See COPYING.LGPL and COPYING.GPL for redistribution conditions.
 *
 *  Be aware that this depends on SDSLib functionality, which is distributed
 *  under the 3-clause "Modified BSD License".
 *
 *  \endlicenseblock
 *
 *  Layers on top of SDSLib 2.0, available here at time of writing:
 *
 *  - https://github.com/antirez/sds
 */

#ifndef SDSX_H_
#define SDSX_H_

#include "top-config.h"

#include <stdio.h>
#include <string.h>

#include "sds.h"
#include "xalloc.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Lists as arrays.  No fanciness here, just a simple struct with a VLA at the
// end, expanded as necessary.  Allocated size is always at least 1 more than
// length, so that a NULL entry there can act as a convenient list terminator.

// Possibly these belong somewhere else, they're pretty generic.

typedef void (*sdsx_list_free_func)(void *);

struct sdsx_list {
	unsigned len;  // used
	unsigned alloc;  // allocated
	sdsx_list_free_func free_func;
	void *elem[];
};

struct sdsx_list *sdsx_list_new(sdsx_list_free_func free_func);
void sdsx_list_free(struct sdsx_list *sl);

// add to/remove from end of list
struct sdsx_list *sdsx_list_push(struct sdsx_list *sl, void *elem);
void *sdsx_list_pop(struct sdsx_list *sl);

// add to/remove from beginning of list
struct sdsx_list *sdsx_list_unshift(struct sdsx_list *sl, void *elem);
void *sdsx_list_shift(struct sdsx_list *sl);

// remove from middle of list
void *sdsx_list_remove(struct sdsx_list *sl, unsigned i);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Some simple SDS utility functions

// As sdstrim(), prunes characters within 'cset' from 's', but only from the
// left (beginning) or right (end) of the string.

sds sdsx_ltrim(sds s, const char *cset);
sds sdsx_rtrim(sds s, const char *cset);

// As sdstrim() (so trims both ends), but avoids characters within quoted
// sections or escape sequences.

sds sdsx_trim_qe(sds s, const char *cset);

// Substring functions use well-known semantics: negative index means count
// from the right, negative length means to end of string (-1 being the very
// end, -2 being one character before, etc.).

// Return a new substring of SDS.
sds sdsx_substr(sds s, ssize_t index, ssize_t sublen);

// Replace an SDS with a substring of itself (address will not change).
sds sdsx_replace_substr(sds s, ssize_t index, ssize_t sublen);

// Quote a string to be suitable for the tokenising process.  An alternative to
// sdscatrepr().  Appends results to 's'.

sds sdsx_cat_quote_str_len(sds s, const char *str, size_t len) FUNC_ATTR_RETURNS_NONNULL FUNC_ATTR_NONNULL_V(1,2);

// Helpers wrapped around sdsx_cat_quote_str_len():

static inline sds sdsx_cat_quote(sds s, const sds t) FUNC_ATTR_RETURNS_NONNULL FUNC_ATTR_NONNULL_V(1,2);
static inline sds sdsx_cat_quote_str(sds s, const char *str) FUNC_ATTR_RETURNS_NONNULL FUNC_ATTR_NONNULL_V(1,2);
static inline sds sdsx_quote(sds t) FUNC_ATTR_RETURNS_NONNULL FUNC_ATTR_NONNULL_V(1);
static inline sds sdsx_quote_str(const char *str) FUNC_ATTR_RETURNS_NONNULL FUNC_ATTR_NONNULL_V(1);

static inline sds sdsx_cat_quote(sds s, const sds t) {
	return sdsx_cat_quote_str_len(s, t, sdslen(t));
}
static inline sds sdsx_cat_quote_str(sds s, const char *str) {
	return sdsx_cat_quote_str_len(s, str, strlen(str));
}
static inline sds sdsx_quote(sds t) {
	return sdsx_cat_quote_str_len(sdsempty(), t, sdslen(t));
}
static inline sds sdsx_quote_str(const char *str) {
	return sdsx_cat_quote_str_len(sdsempty(), str, strlen(str));
}

// Read a line from a file.  Calls fgets() as often as needed until newline or
// end of file.

sds sdsx_fgets(FILE *f);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Parse a single string, possibly containing escape sequences, appending to
// sds string 's'.  No special treatment of quote characters here.

sds sdsx_cat_parse_str_len(sds s, const char *str, size_t len);

// Helpers wrapped around sdsx_cat_parse_len():
static inline sds sdsx_cat_parse(sds s, const sds t) {
	return sdsx_cat_parse_str_len(s, t, sdslen(t));
}
static inline sds sdsx_cat_parse_str(sds s, const char *str) {
	return sdsx_cat_parse_str_len(s, str, strlen(str));
}
static inline sds sdsx_parse(const sds t) {
	return sdsx_cat_parse_str_len(sdsempty(), t, sdslen(t));
}
static inline sds sdsx_parse_str(const char *str) {
	return sdsx_cat_parse_str_len(sdsempty(), str, strlen(str));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tokenise a string, accounting for quoted sections and escape sequences.
//
// Token is all characters up to the next match of 'ere' (a POSIX Extended
// Regular Expression), and the source is updated such that the next characters
// trail whatever that matched.
//
// If 'parse' is true, quoted sections are extracted and escape sequences are
// translated, otherwise you get the results with those sequences still in
// place.
//
// Returns a new SDS containing the token, or NULL on error (e.g. unterminated
// quotes).  Supplied string will become NULL when no more tokens are found.

sds sdsx_tok_str_len(const char **s, size_t *len, const char *ere, _Bool parse);


// Wraps sdsx_tok_str_len() such that data is consumed from the left of a
// source SDS string.  The source is modified, but its address will not change.

sds sdsx_tok(sds s, const char *ere, _Bool parse);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Split a source string separated by a supplied POSIX Extended Regular
// Expression into a list of SDS strings.  Returns NULL on parsing error, else
// the list will contain at least one (potentially empty) element.

struct sdsx_list *sdsx_split_str_len(const char *str, size_t len, const char *cset,
				     _Bool parse);

static inline struct sdsx_list *sdsx_split(const sds s, const char *cset, _Bool parse) {
	return sdsx_split_str_len(s, sdslen(s), cset, parse);
}

static inline struct sdsx_list *sdsx_split_str(const char *str, const char *cset, _Bool parse) {
	return sdsx_split_str_len(str, strlen(str), cset, parse);
}

// Join a list of SDS strings using a common separator.

sds sdsx_join(struct sdsx_list *sl, const char *sep);

// Join a list of C strings using a common separator.

sds sdsx_join_str(const struct sdsx_list *sl, const char *sep);

#endif
