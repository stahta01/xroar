/*

Command-line and file-based configuration options

Copyright 2009-2020 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_XCONFIG_H_
#define XROAR_XCONFIG_H_

#include "sds.h"

struct sdsx_list;

#define XC_SET_BOOL(o,d) .type = XCONFIG_BOOL, .name = (o), .dest.object = (d)
#define XC_SET_BOOL0(o,d) .type = XCONFIG_BOOL0, .name = (o), .dest.object = (d)
#define XC_SET_INT(o,d) .type = XCONFIG_INT, .name = (o), .dest.object = (d)
#define XC_SET_INT0(o,d) .type = XCONFIG_INT0, .name = (o), .dest.object = (d)
#define XC_SET_INT1(o,d) .type = XCONFIG_INT1, .name = (o), .dest.object = (d)
#define XC_SET_DOUBLE(o,d) .type = XCONFIG_DOUBLE, .name = (o), .dest.object = (d)
#define XC_SET_STRING(o,d) .type = XCONFIG_STRING, .name = (o), .dest.object = (d)
#define XC_SET_STRING_LIST(o,d) .type = XCONFIG_STRING_LIST, .name = (o), .dest.object = (d)
#define XC_SET_STRING_F(o,d) .type = XCONFIG_STRING, .name = (o), .dest.object = (d), .flags = XCONFIG_FLAG_CLI_NOESC
#define XC_SET_STRING_LIST_F(o,d) .type = XCONFIG_STRING_LIST, .name = (o), .dest.object = (d), .flags = XCONFIG_FLAG_CLI_NOESC
#define XC_SET_ENUM(o,d,e) .type = XCONFIG_ENUM, .name = (o), .ref = (e), .dest.object = (d)

#define XC_CALL_BOOL(o,d) .type = XCONFIG_BOOL, .name = (o), .dest.func_bool = (d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_BOOL0(o,d) .type = XCONFIG_BOOL0, .name = (o), .dest.func_bool = (d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_INT(o,d) .type = XCONFIG_INT, .name = (o), .dest.func_int = (d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_INT0(o,d) .type = XCONFIG_INT0, .name = (o), .dest.func_int = (d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_INT1(o,d) .type = XCONFIG_INT1, .name = (o), .dest.func_int = (d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_DOUBLE(o,d) .type = XCONFIG_DOUBLE, .name = (o), .dest.func_double = (d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_STRING(o,d) .type = XCONFIG_STRING, .name = (o), .dest.func_string = (xconfig_func_string)(d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_ASSIGN(o,d) .type = XCONFIG_ASSIGN, .name = (o), .dest.func_assign = (xconfig_func_assign)(d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_STRING_F(o,d) .type = XCONFIG_STRING, .name = (o), .dest.func_string = (xconfig_func_string)(d), .flags = XCONFIG_FLAG_CALL | XCONFIG_FLAG_CLI_NOESC
#define XC_CALL_ASSIGN_F(o,d) .type = XCONFIG_ASSIGN, .name = (o), .dest.func_assign = (xconfig_func_assign)(d), .flags = XCONFIG_FLAG_CALL | XCONFIG_FLAG_CLI_NOESC
#define XC_CALL_NULL(o,d) .type = XCONFIG_NULL, .name = (o), .dest.func_null = (d), .flags = XCONFIG_FLAG_CALL
#define XC_CALL_ENUM(o,d,e) .type = XCONFIG_ENUM, .name = (o), .ref = (e), .dest.func_int = (d), .flags = XCONFIG_FLAG_CALL

#define XC_OPT_END() .type = XCONFIG_END

#define XC_ENUM_INT(k,v,d) .name = (k), .value = (v), .description = (d)
#define XC_ENUM_END() .name = NULL

// Option passes data to supplied function instead of setting directly
#define XCONFIG_FLAG_CALL      (1 << 0)
// Option will _not_ be parsed for escape sequences if passed on the command
// line (a kludge for Windows, basically)
#define XCONFIG_FLAG_CLI_NOESC (1 << 1)

enum xconfig_result {
	XCONFIG_OK = 0,
	XCONFIG_BAD_OPTION,
	XCONFIG_MISSING_ARG,
	XCONFIG_BAD_VALUE,
	XCONFIG_FILE_ERROR
};

enum xconfig_option_type {
	XCONFIG_BOOL,
	XCONFIG_BOOL0,  /* unsets a BOOL */
	XCONFIG_INT,
	XCONFIG_INT0,  /* sets an int to 0 */
	XCONFIG_INT1,  /* sets an int to 1 */
	XCONFIG_DOUBLE,
	XCONFIG_STRING,
	XCONFIG_STRING_LIST,
	XCONFIG_ASSIGN,
	XCONFIG_NULL,
	XCONFIG_ENUM,
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
		xconfig_func_bool func_bool;
		xconfig_func_int func_int;
		xconfig_func_double func_double;
		xconfig_func_string func_string;
		xconfig_func_assign func_assign;
		xconfig_func_null func_null;
	} dest;
	void *ref;
	unsigned flags;
	_Bool deprecated;
};

struct xconfig_enum {
	int value;
	const char *name;
	const char *description;
};

/* For error reporting: */
extern char *xconfig_option;
extern int xconfig_line_number;

enum xconfig_result xconfig_set_option(struct xconfig_option const *options,
				       const char *opt, const char *arg);

enum xconfig_result xconfig_parse_file(struct xconfig_option const *options,
		const char *filename);

enum xconfig_result xconfig_parse_line(struct xconfig_option const *options,
		const char *line);

enum xconfig_result xconfig_parse_cli(struct xconfig_option const *options,
		int argc, char **argv, int *argn);

void xconfig_shutdown(struct xconfig_option const *options);

#endif
