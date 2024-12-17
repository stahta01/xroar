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

#include "top-config.h"

// for regex, strspn
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "pl-regex.h"
#include "sdsx.h"
#include "xalloc.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Helper: convert hex digit to integer 0-15.

static int hex_to_int(int c) {
	switch (c) {
	default:
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'A': case 'a': return 10;
	case 'B': case 'b': return 11;
	case 'C': case 'c': return 12;
	case 'D': case 'd': return 13;
	case 'E': case 'e': return 14;
	case 'F': case 'f': return 15;
	}
}

// Helper: test for octal digit.

static int is_odigit(int c) {
	return (c >= '0' && c <= '7');
}


// str_next_chars() classifies the next one or more characters in a string.
// SDSX_ESC_* indicates an escape sequence, else SDSX_NOESC indicates a regular
// character.

enum {
	SDSX_NOESC,     // not an escape sequence
	SDSX_ESC_CHAR,  // "\." - single-byte escaped char (2 chars)
	SDSX_ESC_OCT,   // "\[0-7]{3}" - 8-bit octal byte (4 chars)
	SDSX_ESC_HEX,   // "\x[0-9A-Fa-f]{2}" - 8-bit hexadecimal byte (4 chars)
	SDSX_ESC_U16,   // "\u[0-9A-Fa-f]{4}" - 16-bit Unicode code point (6 chars)
};

static int str_next_chars(const char *p, size_t len) {
	if (len < 2 || *p != '\\') {
		return SDSX_NOESC;
	}

	if (len < 4) {
		return SDSX_ESC_CHAR;
	}

	if (is_odigit(*(p+1)) && is_odigit(*(p+2)) && is_odigit(*(p+3))) {
		return SDSX_ESC_OCT;
	}

	if (*(p+1) == 'x' && isxdigit(*(p+2)) && isxdigit(*(p+3))) {
		return SDSX_ESC_HEX;
	}

	if (len < 6) {
		return SDSX_ESC_CHAR;
	}

	if (*(p+1) == 'u' && isxdigit(*(p+2)) && isxdigit(*(p+3))
	    && isxdigit(*(p+4)) && isxdigit(*(p+5))) {
		return SDSX_ESC_U16;
	}

	return SDSX_ESC_CHAR;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Lists as arrays.  No fanciness here, just a simple struct with a VLA at the
// end, expanded as necessary.  Allocated size is always at least 1 more than
// length, so that a NULL entry there can act as a convenient list terminator.

// Possibly these belong somewhere else, they're pretty generic.

struct sdsx_list *sdsx_list_new(sdsx_list_free_func free_func) {
	unsigned alloc = 1;
	struct sdsx_list *sl = xmalloc(sizeof(struct sdsx_list) + alloc*sizeof(void *));
	sl->len = 0;
	sl->alloc = alloc;
	sl->free_func = free_func;
	sl->elem[0] = NULL;
	return sl;
}

void sdsx_list_free(struct sdsx_list *sl) {
	if (!sl)
		return;
	for (unsigned i = 0; i < sl->len; i++) {
		if (sl->elem[i]) {
			if (sl->free_func)
				sl->free_func(sl->elem[i]);
			sl->elem[i] = NULL;
		}
	}
	sl->len = 0;
	free(sl);
}

// append to list, returns new list
struct sdsx_list *sdsx_list_push(struct sdsx_list *sl, void *elem) {
	// there is always space for a new element
	sl->elem[sl->len] = elem;
	sl->len++;
	// but might need to make more space for a new NULL entry
	unsigned alloc = (sl->len + 1) | 1;
	if (alloc != sl->alloc) {
		sl = xrealloc(sl, sizeof(struct sdsx_list) + alloc*sizeof(void *));
		sl->alloc = alloc;
	}
	sl->elem[sl->len] = NULL;
	return sl;
}

// remove from end of list
void *sdsx_list_pop(struct sdsx_list *sl) {
	if (sl->len == 0)
		return NULL;
	void *elem = sl->elem[sl->len-1];
	sl->len--;
	sl->elem[sl->len] = NULL;
	return elem;
}

// prepend to list, returns new list
struct sdsx_list *sdsx_list_unshift(struct sdsx_list *sl, void *elem) {
	// there is always space to move old elements up one position
	for (unsigned i = sl->len; i > 0; i--) {
		sl->elem[i] = sl->elem[i-1];
	}
	sl->elem[0] = elem;
	sl->len++;
	// but might need to make more space for a new NULL entry
	unsigned alloc = (sl->len + 1) | 1;
	if (alloc != sl->alloc) {
		sl = xrealloc(sl, sizeof(struct sdsx_list) + alloc*sizeof(void *));
		sl->alloc = alloc;
	}
	sl->elem[sl->len] = NULL;
	return sl;
}

// remove from beginning of list
void *sdsx_list_shift(struct sdsx_list *sl) {
	if (sl->len == 0)
		return NULL;
	void *elem = sl->elem[0];
	sl->len--;
	for (unsigned i = 0; i < sl->len; i++) {
		sl->elem[i] = sl->elem[i+1];
	}
	sl->elem[sl->len] = NULL;
	return elem;
}

// remove from middle of list
void *sdsx_list_remove(struct sdsx_list *sl, unsigned i) {
	if (i >= sl->len)
		return NULL;
	void *elem = sl->elem[i];
	sl->len--;
	for (; i < sl->len; i++) {
		sl->elem[i] = sl->elem[i+1];
	}
	sl->elem[sl->len] = NULL;
	return elem;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Some simple SDS utility functions

// As sdstrim(), prunes characters within 'cset' from 's', but only from the
// left (beginning) of the string.

sds sdsx_ltrim(sds s, const char *cset) {
	if (!s)
		return NULL;

	// default delimiters are any whitespace
	if (!cset)
		cset = " \f\n\r\t\v";

	size_t len = sdslen(s);
	size_t skip = strspn(s, cset);
	if (skip == 0)
		return s;

	const char *p = s + skip;
	len -= skip;
	memmove(s, p, len);
	s[len] = '\0';
	sdssetlen(s, len);
	return s;
}

// Again, but for only the right (end) of the string.

sds sdsx_rtrim(sds s, const char *cset) {
	if (!s)
		return NULL;

	// default delimiters are any whitespace
	if (!cset)
		cset = " \f\n\r\t\v";

	size_t len = sdslen(s);
	char *ep = s + len - 1;

	while (len > 0 && strchr(cset, *ep)) {
		ep--;
		len--;
	}
	s[len] = '\0';
	sdssetlen(s, len);
	return s;
}

// As sdstrim() (so trims both ends), but avoids characters within quoted
// sections or escape sequences.

sds sdsx_trim_qe(sds s, const char *cset) {
	if (!s)
		return NULL;

	// default delimiters are any whitespace
	if (!cset)
		cset = " \f\n\r\t\v";

	size_t len = sdslen(s);
	size_t skip = strspn(s, cset);
	if (skip > len) {
		skip = len;
	}
	const char *sp = s + skip;
	const char *p = sp;
	len -= skip;
	int quote = 0;

	while (len > 0) {
		if (quote) {
			// matching quote mark exits quote mode:
			if (*p == quote) {
				p++;
				len--;
				quote = 0;
				continue;
			}
		} else {
			// delimiter?
			skip = strspn(p, cset);
			if (skip >= len) {
				// end of string - finish up
				break;
			}
			if (skip > 0) {
				p += skip;
				len -= skip;
				continue;
			}

			// quote?
			if (*p == '\'' || *p == '"') {
				quote = *(p++);
				len--;
				continue;
			}
		}

		switch (str_next_chars(p, len)) {
		case SDSX_ESC_U16:
			p += 6;
			len -= 6;
			break;
		case SDSX_ESC_HEX:
		case SDSX_ESC_OCT:
			p += 4;
			len -= 4;
			break;
		case SDSX_ESC_CHAR:
			p += 2;
			len -= 2;
			break;
		default:
		case SDSX_NOESC:
			p++;
			len--;
			break;
		}
	}

	size_t plen = p - sp;
	if (s != sp) {
		memmove(s, sp, plen);
	}
	s[plen] = '\0';
	sdssetlen(s, plen);
	return s;
}

// Helper for substr() functions.  Calculates substring bounds based on
// well-known semantics: negative index means count from the right, negative
// length means until end of string.  Updates index and sublen such that
// they always refer to a valid string slice.

static void calc_valid_substr(size_t len, ssize_t index, ssize_t sublen,
			      size_t *valid_index, size_t *valid_sublen) {
	size_t start, end;

	if (index < 0) {
		if ((size_t)-index < len) {
			start = len + index;
		} else {
			start = 0;
		}
	} else {
		start = index;
	}

	if (sublen < 0) {
		end = len + (sublen + 1);
	} else {
		end = start + sublen;
	}

	if (start > len) {
		start = end = len;
	} else if (start > end) {
		end = start;
	} else if (end > len) {
		end = len;
	}

	*valid_index = start;
	*valid_sublen = end - start;
}

// Return a new substring of SDS.

sds sdsx_substr(sds s, ssize_t index, ssize_t sublen) {
	size_t valid_index, valid_sublen;
	calc_valid_substr(sdslen(s), index, sublen, &valid_index, &valid_sublen);
	return sdsnewlen(s + valid_index, valid_sublen);
}

// Replace an SDS with a substring of itself (address will not change).

sds sdsx_replace_substr(sds s, ssize_t index, ssize_t sublen) {
	size_t valid_index, valid_sublen;
	calc_valid_substr(sdslen(s), index, sublen, &valid_index, &valid_sublen);
	if (valid_index != 0 && valid_sublen > 0) {
		memmove(s, s+valid_index, valid_sublen);
	}
	s[valid_sublen] = '\0';
	sdssetlen(s, valid_sublen);
	return s;
}

// Quote a string to be suitable for the tokenising process.  An alternative to
// sdscatrepr().  Appends results to 's'.

sds sdsx_cat_quote_str_len(sds s, const char *str, size_t len) {
	// Default to double-quoting, unless a double quote found in the
	// string, in which case default to single-quoting.
	char quote = '"';
	if (memchr(str, '"', len) != NULL) {
		quote = '\'';
	}
	s = sdscatlen(s, &quote, 1);
	// Track whether we've seen any unsafe characters.  If we don't see
	// any, we'll undo the quoting later.  Although escape sequences can
	// appear outside quoted section, flag those as unsafe, too.
	_Bool unsafe = 0;
	while (len > 0) {
		char c = *(str++);
		len--;
		switch (c) {
		case '\'': case '"':
			unsafe = 1;
			if (c == quote) {
				s = sdscatprintf(s, "\\%c", c);
				continue;
			}
			break;
		case '\0':
			unsafe = 1;
			if (len == 0 || is_odigit(*str)) {
				s = sdscatlen(s, "\\x00", 2);
			} else {
				s = sdscatlen(s, "\\0", 2);
			}
			break;
		case ' ':
		case ',':
		case '=':
			unsafe = 1;
			break;
		case '\a': unsafe = 1; s = sdscatlen(s, "\\a", 2); continue;
		case '\b': unsafe = 1; s = sdscatlen(s, "\\b", 2); continue;
		case 0x1b: unsafe = 1; s = sdscatlen(s, "\\e", 2); continue;
		case '\f': unsafe = 1; s = sdscatlen(s, "\\f", 2); continue;
		case '\n': unsafe = 1; s = sdscatlen(s, "\\n", 2); continue;
		case '\r': unsafe = 1; s = sdscatlen(s, "\\r", 2); continue;
		case '\t': unsafe = 1; s = sdscatlen(s, "\\t", 2); continue;
		case '\v': unsafe = 1; s = sdscatlen(s, "\\v", 2); continue;
		case '\\': unsafe = 1; s = sdscatlen(s, "\\\\", 2); continue;
		default:
			   if (!isprint(c)) {
				   unsafe = 1;
				   s = sdscatprintf(s, "\\x%02x", (unsigned char)c);
				   continue;
			   }
			   break;
		}
		s = sdscatlen(s, &c, 1);
	}
	if (!unsafe) {
		len = sdslen(s) - 1;
		memmove(s, s+1, len);
		s[len] = '\0';
	} else {
		s = sdscatlen(s, &quote, 1);
	}
	return s;
}

#define SDSX_FGETS_BUFSIZE (256)

// Read a line from a file.  Calls fgets() as often as needed until newline or
// end of file.

sds sdsx_fgets(FILE *f) {
	char buf[SDSX_FGETS_BUFSIZE];
	char *in = fgets(buf, SDSX_FGETS_BUFSIZE, f);
	if (!in)
		return NULL;
	if (strlen(in) == 0)
		return NULL;
	sds s = sdsnew(in);
	while (s[sdslen(s)-1] != '\n') {
		if (feof(f))
			return s;
		in = fgets(buf, SDSX_FGETS_BUFSIZE, f);
		if (!in)
			return s;
		s = sdscat(s, in);
	}
	return s;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Parse a single string, possibly containing escape sequences, appending to
// sds string 's'.  No special treatment of quote characters here.

sds sdsx_cat_parse_str_len(sds s, const char *str, size_t len) {
	const char *p = str;
	while (len > 0) {

		switch (str_next_chars(p, len)) {

		// 16-bit unicode code point, encode as utf-8:
		case SDSX_ESC_U16:
			{
				int l;
				unsigned char b[3];
				unsigned i = (hex_to_int(*(p+2)) << 12)
				             | (hex_to_int(*(p+3)) << 8)
				             | (hex_to_int(*(p+4)) << 4)
				             | hex_to_int(*(p+5));
				if (i < 0x80) {
					b[0] = i;
					l = 1;
				} else if (i < 0x800) {
					b[0] = 0xc0 | (i >> 6);
					b[1] = 0x80 | (i & 0x3f);
					l = 2;
				} else {
					b[0] = 0xe0 | (i >> 12);
					b[1] = 0x80 | ((i >> 6) & 0x3f);
					b[2] = 0x80 | (i & 0x3f);
					l = 3;
				}
				s = sdscatlen(s, (char *)b, l);
				p += 6;
				len -= 6;
			}
			break;

		// hexadecimal byte:
		case SDSX_ESC_HEX:
			{
				uint8_t b = (hex_to_int(*(p+2)) << 4) | hex_to_int(*(p+3));
				s = sdscatlen(s, (char *)&b, 1);
				p += 4;
				len -= 4;
			}
			break;

		// octal byte:
		case SDSX_ESC_OCT:
			{
				uint8_t b = (hex_to_int(*(p+1)) << 6)
				            | (hex_to_int(*(p+2)) << 3)
				            | hex_to_int(*(p+3));
				s = sdscatlen(s, (char *)&b, 1);
				p += 4;
				len -= 4;
			}
			break;

		// escaped character:
		case SDSX_ESC_CHAR:
			{
				char c = *(p+1);
				switch (c) {
				case '0': c = '\x00'; break;  // NUL
				case 'a': c = '\x07'; break;  // BEL
				case 'b': c = '\x08'; break;  // BS
				case 'e': c = '\x1b'; break;  // ESC
				case 'f': c = '\x0c'; break;  // FF
				case 'n': c = '\x0a'; break;  // NL
				case 'r': c = '\x0d'; break;  // CR
				case 't': c = '\x09'; break;  // HT
				case 'v': c = '\x0b'; break;  // VT
				default: break;
				}
				s = sdscatlen(s, &c, 1);
				p += 2;
				len -= 2;
			}
			break;

		// normal character:
		default:
		case SDSX_NOESC:
			s = sdscatlen(s, p, 1);
			p++;
			len--;
			break;

		}

	}
	return s;
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

sds sdsx_tok_str_len(const char **s, size_t *lenp, const char *ere, _Bool parse) {
	// default delimiter, space/tab
	if (!ere)
		ere = "[ \t]+";

	sds r = sdsempty();
	size_t len = *lenp;
	if (len == 0) {
		*s = NULL;
		return r;
	}
	const char *p = *s;

	int errcode;
	regex_t preg;
	regmatch_t pmatch;
	if ((errcode = regcomp(&preg, ere, REG_EXTENDED))) {
		fprintf(stderr, "Error in regex: %d\n", errcode);
		abort();
	}

	int quote = 0;

	// Track spans of characters to process with normal escape sequence
	// rules.  ie, any quoted span, or unquoted span up to the next quote
	// character, delimiter, or end of string.
	const char *sp = p;

	do {
		if (quote) {
			// matching quote mark exits quote mode:
			if (*p == quote) {
				if (parse)
					r = sdsx_cat_parse_str_len(r, sp, p - sp);
				p++;
				len--;
				quote = 0;
				if (parse)
					sp = p;
				if (len == 0)
					*s = NULL;
				continue;
			}
		} else {
			// delimiter?
			// TODO: there's definitely a better way of doing this,
			// but for now, run the match every time we get here.
			if (regexec(&preg, p, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
				if (parse)
					r = sdsx_cat_parse_str_len(r, sp, p - sp);
				else
					r = sdscatlen(r, sp, p - sp);
				// sanity check
				if ((size_t)pmatch.rm_eo > len)
					pmatch.rm_eo = len;
				p += pmatch.rm_eo;
				len -= pmatch.rm_eo;
				sp = p;
				break;
			}

			// quote?
			if (*p == '\'' || *p == '"') {
				if (parse)
					r = sdsx_cat_parse_str_len(r, sp, p - sp);
				quote = *(p++);
				len--;
				if (parse)
					sp = p;
				if (len == 0)
					*s = NULL;
				continue;
			}
		}

		switch (str_next_chars(p, len)) {
		case SDSX_ESC_U16:
			p += 6;
			len -= 6;
			break;
		case SDSX_ESC_HEX:
		case SDSX_ESC_OCT:
			p += 4;
			len -= 4;
			break;
		case SDSX_ESC_CHAR:
			p += 2;
			len -= 2;
			break;
		default:
		case SDSX_NOESC:
			p++;
			len--;
			break;
		}

		if (len == 0)
			*s = NULL;

	} while (len > 0);

	regfree(&preg);

	// if we're still in quote mode by the end of the string, that's an
	// error.
	if (quote) {
		sdsfree(r);
		return NULL;
	}

	// append any pending span
	if (p > sp) {
		if (parse)
			r = sdsx_cat_parse_str_len(r, sp, p - sp);
		else
			r = sdscatlen(r, sp, p - sp);
	}

	// only update string pointer if it wasn't previously set to NULL
	if (*s)
		*s = p;
	*lenp = len;

	return r;
}

// Wraps sdsx_tok_str_len() such that data is consumed from the left of a
// source SDS string.  The source is modified, but its address will not change.

sds sdsx_tok(sds s, const char *ere, _Bool parse) {
	const char *p = s;
	size_t len = sdslen(s);
	sds r = sdsx_tok_str_len(&p, &len, ere, parse);

	if (!r)
		return NULL;

	// move remaining data to beginning of string
	if (p && s != p && len > 0) {
		memmove(s, p, len);
	}
	s[len] = '\0';
	sdssetlen(s, len);
	return r;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void sdsfree_void(void *sptr) {
	sdsfree((sds)sptr);
}

// Split a source string separated by a supplied POSIX Extended Regular
// Expression into a list of SDS strings.  Returns NULL on parsing error, else
// the list will contain at least one (potentially empty) element.

struct sdsx_list *sdsx_split_str_len(const char *str, size_t len, const char *ere,
				     _Bool parse) {

	struct sdsx_list *sl = sdsx_list_new((sdsx_list_free_func)sdsfree_void);

	do {
		sds t = sdsx_tok_str_len(&str, &len, ere, parse);
		if (!t) {
			// tokenising error
			sdsx_list_free(sl);
			return NULL;
		}
		sl = sdsx_list_push(sl, t);
	} while (str);
	return sl;
}

// Join a list of SDS strings using a common separator.

sds sdsx_join(struct sdsx_list *sl, const char *sep) {
	sds s = sdsempty();
	for (unsigned i = 0; i < sl->len; i++) {
		if (sl->elem[i])
			s = sdscatsds(s, (const sds)(sl->elem[i]));
		if (sep && (i+1) < sl->len)
			s = sdscat(s, sep);
	}
	return s;
}

// Join a list of C strings using a common separator.

sds sdsx_join_str(const struct sdsx_list *sl, const char *sep) {
	sds s = sdsempty();
	for (unsigned i = 0; i < sl->len; i++) {
		if (sl->elem[i])
			s = sdscat(s, (char *)(sl->elem[i]));
		if (sep && (i+1) < sl->len)
			s = sdscat(s, sep);
	}
	return s;
}
