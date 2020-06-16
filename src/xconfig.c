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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "logging.h"
#include "xconfig.h"

static struct xconfig_option const *find_option(struct xconfig_option const *options,
		const char *opt) {
	for (int i = 0; options[i].type != XCONFIG_END; i++) {
		if (0 == strcmp(options[i].name, opt)) {
			return &options[i];
		}
	}
	return NULL;
}

static int lookup_enum(const char *name, struct xconfig_enum *list, int undef_value) {
	for (int i = 0; list[i].name; i++) {
		if (0 == strcmp(name, list[i].name)) {
			return list[i].value;
		}
	}
	/* Only check this afterwards, as "help" could be a valid name */
	if (0 == strcmp(name, "help")) {
		for (int i = 0; list[i].name; i++) {
			printf("\t%-10s %s\n", list[i].name, list[i].description);
		}
		exit(EXIT_SUCCESS);
	}
	return undef_value;
}

// Handle simple zero or one argument option setting (ie, not XCONFIG_ASSIGN).
// 'arg' should be parsed to handle any quoting or escape sequences by this
// point.

static void set_option(struct xconfig_option const *option, sds arg) {
	switch (option->type) {
		case XCONFIG_BOOL:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_bool(1);
			else
				*(_Bool *)option->dest.object = 1;
			break;
		case XCONFIG_BOOL0:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_bool(0);
			else
				*(_Bool *)option->dest.object = 0;
			break;
		case XCONFIG_INT:
			{
				int val = strtol(arg, NULL, 0);
				if (option->flags & XCONFIG_FLAG_CALL)
					option->dest.func_int(val);
				else
					*(int *)option->dest.object = val;
			}
			break;
		case XCONFIG_INT0:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_int(0);
			else
				*(int *)option->dest.object = 0;
			break;
		case XCONFIG_INT1:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_int(1);
			else
				*(int *)option->dest.object = 1;
			break;
		case XCONFIG_DOUBLE:
			{
				double val = strtod(arg, NULL);
				if (option->flags & XCONFIG_FLAG_CALL)
					option->dest.func_double(val);
				else
					*(double *)option->dest.object = val;
			}
			break;
		case XCONFIG_STRING:
			if (option->flags & XCONFIG_FLAG_CALL) {
				option->dest.func_string(arg);
			} else {
				if (*(char **)option->dest.object)
					free(*(char **)option->dest.object);
				*(char **)option->dest.object = xstrdup(arg);
			}
			break;
		case XCONFIG_STRING_LIST:
			assert(!(option->flags & XCONFIG_FLAG_CALL));
			*(struct slist **)option->dest.object = slist_append(*(struct slist **)option->dest.object, sdsdup(arg));
			break;
		case XCONFIG_NULL:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_null();
			break;
		case XCONFIG_ENUM: {
			int val = lookup_enum(arg, (struct xconfig_enum *)option->ref, -1);
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_int(val);
			else
				*(int *)option->dest.object = val;
			}
			break;
		default:
			break;
	}
}

/* returns 0 if it's a value option to unset */
static int unset_option(struct xconfig_option const *option) {
	switch (option->type) {
	case XCONFIG_BOOL:
		if (option->flags & XCONFIG_FLAG_CALL)
			option->dest.func_bool(0);
		else
			*(_Bool *)option->dest.object = 0;
		return 0;
	case XCONFIG_BOOL0:
		if (option->flags & XCONFIG_FLAG_CALL)
			option->dest.func_bool(1);
		else
			*(_Bool *)option->dest.object = 1;
		return 0;
	case XCONFIG_INT0:
		if (option->flags & XCONFIG_FLAG_CALL)
			option->dest.func_int(1);
		else
			*(int *)option->dest.object = 1;
		return 0;
	case XCONFIG_INT1:
		if (option->flags & XCONFIG_FLAG_CALL)
			option->dest.func_int(0);
		else
			*(int *)option->dest.object = 0;
		return 0;
	case XCONFIG_STRING:
		if (option->flags & XCONFIG_FLAG_CALL) {
			option->dest.func_string(NULL);
		} else if (*(char **)option->dest.object) {
			free(*(char **)option->dest.object);
			*(char **)option->dest.object = NULL;
		}
		return 0;
	case XCONFIG_STRING_LIST:
		assert(!(option->flags & XCONFIG_FLAG_CALL));
		/* providing an argument to remove here might more sense, but
		 * for now just remove the entire list: */
		slist_free_full(*(struct slist **)option->dest.object, (slist_free_func)sdsfree);
		*(struct slist **)option->dest.object = NULL;
		return 0;
	default:
		break;
	}
	return -1;
}

// Convenience function to manually set an option.  Only handles simple zero-
// or one-argument options.  'arg' will be parsed to process escape sequences,
// but should not contain quoted sections.

enum xconfig_result xconfig_set_option(struct xconfig_option const *options,
				       const char *opt, const char *arg) {
	struct xconfig_option const *option = find_option(options, opt);
	if (option == NULL) {
		if (0 == strncmp(opt, "no-", 3)) {
			option = find_option(options, opt + 3);
			if (option && unset_option(option) == 0) {
				return XCONFIG_OK;
			}
		}
		LOG_ERROR("Unrecognised option `%s'\n", opt);
		return XCONFIG_BAD_OPTION;
	}
	if (option->deprecated) {
		LOG_WARN("Deprecated option `%s'\n", opt);
	}
	if (option->type == XCONFIG_BOOL ||
	    option->type == XCONFIG_BOOL0 ||
	    option->type == XCONFIG_INT0 ||
	    option->type == XCONFIG_INT1 ||
	    option->type == XCONFIG_NULL) {
		set_option(option, NULL);
		return XCONFIG_OK;
	}
	if (!arg) {
		LOG_ERROR("Missing argument to `%s'\n", opt);
		return XCONFIG_MISSING_ARG;
	}
	set_option(option, sdsx_parse_str(arg));
	return XCONFIG_OK;
}

/* Simple parser: one directive per line, "option argument" */
enum xconfig_result xconfig_parse_file(struct xconfig_option const *options,
		const char *filename) {
	FILE *cfg;
	int ret = XCONFIG_OK;

	cfg = fopen(filename, "r");
	if (cfg == NULL) return XCONFIG_FILE_ERROR;
	sds line;
	while ((line = sdsx_fgets(cfg))) {
		enum xconfig_result r = xconfig_parse_line(options, line);
		sdsfree(line);
		if (r != XCONFIG_OK)
			ret = r;
	}
	fclose(cfg);
	return ret;
}

// Parse whole config lines, usually from a file.
// Lines are of the form: KEY [=] [VALUE [,VALUE]...]

enum xconfig_result xconfig_parse_line(struct xconfig_option const *options, const char *line) {
	// Trim leading and trailing whitespace, accounting for quotes & escapes
	sds input = sdsx_trim_qe(sdsnew(line), NULL);

	// Ignore empty lines and comments
	if (!*input || *input == '#') {
		sdsfree(input);
		return XCONFIG_OK;
	}

	sds opt = sdsx_ltrim(sdsx_tok(input, "([ \t]*=[ \t]*|[ \t]+)", 1), "-");
	if (!opt) {
		sdsfree(input);
		return XCONFIG_BAD_VALUE;
	}
	if (!*opt) {
		sdsfree(input);
		return XCONFIG_OK;
	}

	struct xconfig_option const *option = find_option(options, opt);
	if (!option) {
		if (0 == strncmp(opt, "no-", 3)) {
			option = find_option(options, opt + 3);
			if (option && unset_option(option) == 0) {
				sdsfree(opt);
				sdsfree(input);
				return XCONFIG_OK;
			}
		}
		LOG_ERROR("Unrecognised option `%s'\n", opt);
		sdsfree(opt);
		sdsfree(input);
		return XCONFIG_BAD_OPTION;
	}
	sdsfree(opt);

	if (option->deprecated) {
		LOG_WARN("Deprecated option `%s'\n", option->name);
	}
	if (option->type == XCONFIG_BOOL ||
	    option->type == XCONFIG_BOOL0 ||
	    option->type == XCONFIG_INT0 ||
	    option->type == XCONFIG_INT1 ||
	    option->type == XCONFIG_NULL) {
		set_option(option, NULL);
		sdsfree(input);
		return XCONFIG_OK;
	}

	if (option->type == XCONFIG_ASSIGN) {
		// first part is key separated by '=' (or whitespace for now)
		sds key = sdsx_tok(input, "([ \t]*=[ \t]*|[ \t]+)", 1);
		if (!key) {
			LOG_ERROR("Bad argument to '%s'\n", option->name);
			sdsfree(input);
			return XCONFIG_BAD_VALUE;
		}
		if (!*key) {
			LOG_ERROR("Missing argument to `%s'\n", option->name);
			sdsfree(key);
			sdsfree(input);
			return XCONFIG_MISSING_ARG;
		}
		// parse rest as comma-separated list
		struct sdsx_list *values = sdsx_split(input, "[ \t]*,[ \t]*", 1);
		if (!values) {
			LOG_ERROR("Bad argument to '%s'\n", option->name);
			sdsfree(key);
			sdsfree(input);
			return XCONFIG_BAD_VALUE;
		}
		option->dest.func_assign(key, values);
		sdsx_list_free(values);
		sdsfree(key);
		sdsfree(input);
		return XCONFIG_OK;
	}

	// the rest of the string constitutes the value - parse it
	sds value = sdsx_tok(input, "[ \t]*$", 1);
	sdsfree(input);
	if (!value) {
		LOG_ERROR("Bad argument to '%s'\n", option->name);
		return XCONFIG_BAD_VALUE;
	}
	if (!*value) {
		LOG_ERROR("Missing argument to `%s'\n", option->name);
		sdsfree(value);
		return XCONFIG_MISSING_ARG;
	}
	set_option(option, value);
	sdsfree(value);
	return XCONFIG_OK;
}

enum xconfig_result xconfig_parse_cli(struct xconfig_option const *options,
		int argc, char **argv, int *argn) {
	int _argn = argn ? *argn : 0;
	while (_argn < argc) {
		if (argv[_argn][0] != '-') {
			break;
		}
		if (0 == strcmp("--", argv[_argn])) {
			_argn++;
			break;
		}
		char *opt = argv[_argn]+1;
		if (*opt == '-') opt++;
		struct xconfig_option const *option = find_option(options, opt);
		if (option == NULL) {
			if (0 == strncmp(opt, "no-", 3)) {
				option = find_option(options, opt + 3);
				if (option && unset_option(option) == 0) {
					_argn++;
					continue;
				}
			}
			if (argn) *argn = _argn;
			LOG_ERROR("Unrecognised option `%s'\n", opt);
			return XCONFIG_BAD_OPTION;
		}
		if (option->deprecated) {
			LOG_WARN("Deprecated option `%s'\n", opt);
		}
		if (option->type == XCONFIG_BOOL ||
		    option->type == XCONFIG_BOOL0 ||
		    option->type == XCONFIG_INT0 ||
		    option->type == XCONFIG_INT1 ||
		    option->type == XCONFIG_NULL) {
			set_option(option, NULL);
			_argn++;
			continue;
		}

		if ((_argn + 1) >= argc) {
			if (argn) *argn = _argn;
			LOG_ERROR("Missing argument to `%s'\n", opt);
			return XCONFIG_MISSING_ARG;
		}

		if (option->type == XCONFIG_ASSIGN) {
			const char *str = argv[_argn+1];
			size_t len = strlen(str);
			// first part is key separated by '=' (NO whitespace)
			sds key = sdsx_tok_str_len(&str, &len, "=", 0);
			if (!key) {
				if (argn) *argn = _argn;
				LOG_ERROR("Missing argument to `%s'\n", option->name);
				return XCONFIG_MISSING_ARG;
			}
			// tokenise rest as comma-separated list, unparsed
			struct sdsx_list *values = sdsx_split_str_len(str, len, ",", 0);
			// parse individual elements separately, as parsing in
			// sdsx_split() would also have processed quoting.
			if (!(option->flags & XCONFIG_FLAG_CLI_NOESC)) {
				for (unsigned i = 0; i < values->len; i++) {
					sds new = sdsx_parse(values->elem[i]);
					sdsfree(values->elem[i]);
					values->elem[i] = new;
				}
			}
			option->dest.func_assign(key, values);
			sdsx_list_free(values);
			sdsfree(key);
		} else {
			sds arg;
			if (option->flags & XCONFIG_FLAG_CLI_NOESC) {
				arg = sdsnew(argv[_argn+1]);
			} else {
				arg = sdsx_parse_str(argv[_argn+1]);
			}
			set_option(option, arg);
			sdsfree(arg);
		}
		_argn += 2;
	}
	if (argn) *argn = _argn;
	return XCONFIG_OK;
}

void xconfig_shutdown(struct xconfig_option const *options) {
	for (int i = 0; options[i].type != XCONFIG_END; i++) {
		if (options[i].type == XCONFIG_STRING) {
			if (!(options[i].flags & XCONFIG_FLAG_CALL) && *(char **)options[i].dest.object) {
				free(*(char **)options[i].dest.object);
				*(char **)options[i].dest.object = NULL;
			}
		} else if (options[i].type == XCONFIG_STRING_LIST) {
			slist_free_full(*(struct slist **)options[i].dest.object, (slist_free_func)sdsfree);
			*(struct slist **)options[i].dest.object = NULL;
		}
	}
}
