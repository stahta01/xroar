/** \file
 *
 *  \brief Command-line and file-based configuration options.
 *
 *  \copyright Copyright 2009-2022 Ciaran Anscomb
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

#ifndef XROAR_XCONFIG_H_
#define XROAR_XCONFIG_H_

#include <stddef.h>

#include "sds.h"

struct sdsx_list;
struct slist;

// Basic common option fields
#define XC_TN(t,n) .type = XCONFIG_##t, .name = (n)
#define XC_TNF(t,n,f) XC_TN(t,n), .flags = (f)

// Options with flags.  _REF includes extra reference data, _NE flags NOESC.
#define XCF_OPT(t,n,d,f) XC_TNF(t,n,f), .dest.object = (d)
#define XCF_OPT_REF(t,n,d,f,r) XCF_OPT(t,n,d,f), .ref = (r)
#define XCF_OPT_NE(t,n,d,f) XCF_OPT(t,n,d,XCONFIG_FLAG_CLI_NOESC|(f))

// Options
#define XC_OPT(t,n,d) XCF_OPT(t,n,d,0)
#define XC_OPT_REF(t,n,d,r) XCF_OPT_REF(t,n,d,0,r)
#define XC_OPT_NE(t,n,d) XCF_OPT(t,n,d,XCONFIG_FLAG_CLI_NOESC)

// Options with flags, destinations specified as struct offsets
#define XCFO_OPT(t,n,s,e,f) XC_TNF(t,n,XCONFIG_FLAG_OFFSET|(f)), .dest.object_offset = offsetof(s,e)
#define XCFO_OPT_REF(t,n,s,e,f,r) XCFO_OPT(t,n,s,e,f), .ref = (r)
#define XCFO_OPT_NE(t,n,s,e,f) XCFO_OPT(t,n,s,e,XCONFIG_FLAG_CLI_NOESC|(f))

// Options, destinations specified as struct offsets
#define XCO_OPT(t,n,s,e) XCFO_OPT(t,n,s,e,0)
#define XCO_OPT_REF(t,n,s,e,r) XCFO_OPT_REF(t,n,s,e,0,r)
#define XCO_OPT_NE(t,n,s,e,f) XCFO_OPT(t,n,s,e,XCONFIG_FLAG_CLI_NOESC)

// Map xc_type to dispatch function member
#define XC_FUNC_BOOL func_bool
#define XC_FUNC_BOOL0 func_bool
#define XC_FUNC_INT func_int
#define XC_FUNC_INT0 func_int
#define XC_FUNC_INT1 func_int
#define XC_FUNC_ENUM func_int
#define XC_FUNC_DOUBLE func_double
#define XC_FUNC_STRING func_string
#define XC_FUNC_STRING_NE func_string
#define XC_FUNC_ASSIGN func_assign
#define XC_FUNC_ASSIGN func_assign
#define XC_FUNC_NONE func_null

// Options with flags that call a handler
#define XCF_CALL(t,n,f,d) XC_TNF(t,n,XCONFIG_FLAG_CALL|(f)), .dest.XC_FUNC_##t = (d)
#define XCF_CALL_REF(t,n,d,f,r) XCF_CALL(t,n,f,d), .ref = (r)

// Options that call a handler
#define XC_CALL(t,n,d) XCF_CALL(t,n,0,d)
#define XC_CALL_REF(t,n,r,d) XC_CALL(t,n,d), .ref = (r)

// Compatibility
#define XC_SET_BOOL(n,d) XC_OPT(BOOL,n,d)
#define XC_SET_BOOL0(n,d) XC_OPT(BOOL0,n,d)
#define XC_SET_INT(n,d) XC_OPT(INT,n,d)
#define XC_SET_INT0(n,d) XC_OPT(INT0,n,d)
#define XC_SET_INT1(n,d) XC_OPT(INT1,n,d)
#define XC_SET_UINT8(n,d) XC_OPT(UINT8,n,d)
#define XC_SET_UINT16(n,d) XC_OPT(UINT16,n,d)
#define XC_SET_DOUBLE(n,d) XC_OPT(DOUBLE,n,d)
#define XC_SET_STRING(n,d) XC_OPT(STRING,n,d)
#define XC_SET_STRING_LIST(n,d) XC_OPT(STRING_LIST,n,d)
#define XC_SET_STRING_NE(n,d) XC_OPT_NE(STRING,n,d)
#define XC_SET_STRING_LIST_NE(n,d) XC_OPT_NE(STRING_LIST,n,d)
#define XC_SET_ENUM(n,d,r) XC_OPT_REF(ENUM,n,d,r)
#define XC_SET_PART(n,d,r) XC_OPT_REF(PART,n,d,r)
#define XC_ALIAS_NOARG(n,d) XC_OPT(ALIAS,n,d)
#define XC_ALIAS_ARG(n,d,r) XC_OPT_REF(ALIAS,n,d,r)
#define XC_ALIAS_UARG(n,d) XC_OPT(ALIAS1,n,d)

#define XC_LINK(d) XC_OPT(LINK,"",d)

#define XCO_SET_BOOL(n,s,m) XCO_OPT(BOOL,n,s,m)
#define XCO_SET_BOOL0(n,s,m) XCO_OPT(BOOL0,n,s,m)
#define XCO_SET_INT(n,s,m) XCO_OPT(INT,n,s,m)
#define XCO_SET_INT0(n,s,m) XCO_OPT(INT0,n,s,m)
#define XCO_SET_INT1(n,s,m) XCO_OPT(INT1,n,s,m)
#define XCO_SET_UINT8(n,s,m) XCO_OPT(UINT8,n,s,m)
#define XCO_SET_UINT16(n,s,m) XCO_OPT(UINT16,n,s,m)
#define XCO_SET_DOUBLE(n,s,m) XCO_OPT(DOUBLE,n,s,m)
#define XCO_SET_STRING(n,s,m) XCO_OPT(STRING,n,s,m)
#define XCO_SET_STRING_LIST(n,s,m) XCO_OPT(STRING_LIST,n,s,m)
#define XCO_SET_STRING_NE(n,s,m) XCFO_OPT(STRING,n,s,m,XCONFIG_FLAG_CLI_NOESC)
#define XCO_SET_STRING_LIST_NE(n,s,m) XCFO_OPT(STRING_LIST,n,s,m,XCONFIG_FLAG_CLI_NOESC)
#define XCO_SET_ENUM(n,s,m,r) XCO_OPT_REF(ENUM,n,s,m,r)
#define XCO_SET_PART(n,s,m,r) XCO_OPT_REF(PART,n,s,m,r)
#define XCO_ALIAS_NOARG(n,s,m) XCO_OPT(ALIAS,n,s,m)
#define XCO_ALIAS_ARG(n,s,m,r) XCO_OPT_REF(ALIAS,n,s,m,r)
#define XCO_ALIAS_UARG(n,s,m) XCO_OPT(ALIAS1,n,s,m)

#define XC_CALL_BOOL(n,d) XC_CALL(BOOL,n,d)
#define XC_CALL_BOOL0(n,d) XC_CALL(BOOL0,n,d)
#define XC_CALL_INT(n,d) XC_CALL(INT,n,d)
#define XC_CALL_INT0(n,d) XC_CALL(INT0,n,d)
#define XC_CALL_INT1(n,d) XC_CALL(INT1,n,d)
#define XC_CALL_ENUM(n,d,r) XC_CALL_REF(ENUM,n,r,d)
#define XC_CALL_DOUBLE(n,d) XC_CALL(DOUBLE,n,d)
#define XC_CALL_STRING(n,d) XC_CALL(STRING,n,d)
#define XC_CALL_ASSIGN(n,d) XC_CALL(ASSIGN,n,d)
#define XC_CALL_STRING_NE(n,d) XCF_CALL(STRING,n,XCONFIG_FLAG_CLI_NOESC,d)
#define XC_CALL_ASSIGN_NE(n,d) XCF_CALL(ASSIGN,n,XCONFIG_FLAG_CLI_NOESC,d)
#define XC_CALL_NONE(n,d) XC_CALL(NONE,n,d)

#define XC_OPT_END() .type = XCONFIG_END

#define XC_ENUM_INT(k,v,d) .name = (k), .value = (v), .description = (d)
#define XC_ENUM_END() .name = NULL

// Option passes data to supplied function instead of setting directly
#define XCONFIG_FLAG_CALL      (1 << 0)
// Option will _not_ be parsed for escape sequences if passed on the command
// line (a kludge for Windows, basically)
#define XCONFIG_FLAG_CLI_NOESC (1 << 1)
// Dest is an offset into a struct, not a direct pointer.  Struct pointer must
// be passed into parser.
#define XCONFIG_FLAG_OFFSET    (1 << 2)

enum xconfig_result {
	XCONFIG_OK = 0,
	XCONFIG_BAD_OPTION,
	XCONFIG_MISSING_ARG,
	XCONFIG_BAD_VALUE,
	XCONFIG_FILE_ERROR
};

enum xconfig_option_type {
	XCONFIG_BOOL,
	XCONFIG_BOOL0,  // unsets a BOOL
	XCONFIG_INT,
	XCONFIG_INT0,  // sets an INT to 0
	XCONFIG_INT1,  // sets an INT to 1
	XCONFIG_UINT8,
	XCONFIG_UINT16,
	XCONFIG_DOUBLE,
	XCONFIG_STRING,
	XCONFIG_STRING_LIST,
	XCONFIG_ASSIGN,
	XCONFIG_NONE,
	XCONFIG_ENUM,
	XCONFIG_PART,
	XCONFIG_ALIAS,  // alias with no user-supplied argument
	XCONFIG_ALIAS1,  // alias with user-supplied argument
	XCONFIG_LINK,  // continue option processing with new list
	XCONFIG_END
};

typedef void (*xconfig_func_bool)(_Bool);
typedef void (*xconfig_func_int)(int);
typedef void (*xconfig_func_double)(double);
typedef void (*xconfig_func_string)(const char *);
typedef void (*xconfig_func_assign)(const char *, struct sdsx_list *);
typedef void (*xconfig_func_null)(void);

struct xconfig_option {
	enum xconfig_option_type type;
	const char *name;
	union {
		void *object;
		off_t object_offset;
		xconfig_func_bool func_bool;
		xconfig_func_int func_int;
		xconfig_func_double func_double;
		xconfig_func_string func_string;
		xconfig_func_assign func_assign;
		xconfig_func_null func_null;
	} dest;
	_Bool *defined;
	void *ref;
	unsigned flags;
	_Bool deprecated;
};

struct xconfig_enum {
	int value;
	const char *name;
	const char *description;
};

// For error reporting:
extern char *xconfig_option;
extern int xconfig_line_number;

enum xconfig_result xconfig_set_option(struct xconfig_option const *options,
				       const char *opt, const char *arg);

enum xconfig_result xconfig_set_option_struct(struct xconfig_option const *options,
				       const char *opt, const char *arg, void *sptr);

enum xconfig_result xconfig_parse_file(struct xconfig_option const *options,
		const char *filename);

enum xconfig_result xconfig_parse_file_struct(struct xconfig_option const *options,
		const char *filename, void *sptr);

enum xconfig_result xconfig_parse_line(struct xconfig_option const *options,
		const char *line);

enum xconfig_result xconfig_parse_line_struct(struct xconfig_option const *options,
		const char *line, void *sptr);

enum xconfig_result xconfig_parse_list_struct(struct xconfig_option const *options,
					      struct slist *list, void *sptr);

enum xconfig_result xconfig_parse_cli(struct xconfig_option const *options,
		int argc, char **argv, int *argn);

enum xconfig_result xconfig_parse_cli_struct(struct xconfig_option const *options,
		int argc, char **argv, int *argn, void *sptr);

// Return members of an enum searching by other members

int xconfig_enum_name_value(struct xconfig_enum *, const char *name, int undef);
const char *xconfig_enum_value_name(struct xconfig_enum *, int value);
const char *xconfig_enum_name_description(struct xconfig_enum *, const char *name);
const char *xconfig_enum_value_description(struct xconfig_enum *, int value);

// Sanity check value assigned to an enumeration type.  Pass in assigned and
// default values.  Returns assigned value if valid, else the default.

int xconfig_check_enum(struct xconfig_enum *list, int val, int dfl);

void xconfig_shutdown(struct xconfig_option const *options);

#endif
