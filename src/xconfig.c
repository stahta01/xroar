/** \file
 *
 *  \brief Command-line and file-based configuration options.
 *
 *  \copyright Copyright 2009-2024 Ciaran Anscomb
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

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c-strcase.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "logging.h"
#include "part.h"
#include "xconfig.h"

static struct xconfig_option const *find_option(struct xconfig_option const *options,
		const char *opt) {
	for (size_t i = 0; options[i].type != XCONFIG_END; ++i) {
		if (options[i].type == XCONFIG_LINK) {
			// recurse
			struct xconfig_option const *r = find_option(options[i].dest.object, opt);
			if (r)
				return r;
			continue;
		}
		if (0 == strcmp(options[i].name, opt)) {
			return &options[i];
		}
	}
	return NULL;
}

static int lookup_enum(struct xconfig_enum *list, const char *name, int undef) {
	assert(name != NULL);
	assert(list != NULL);
	for (struct xconfig_enum *iter = list; iter->name; ++iter) {
		if (0 == c_strcasecmp(name, iter->name)) {
			return iter->value;
		}
	}
	// Only check this afterwards, as "help" could be a valid name
	if (0 == c_strcasecmp(name, "help")) {
		for (struct xconfig_enum *iter = list; iter->name; ++iter) {
			if (iter->description) {
				printf("\t%-10s %s\n", iter->name, iter->description);
			}
		}
		exit(EXIT_SUCCESS);
	}
	return undef;
}

static void print_part_name_description(const struct partdb_entry *pe, void *idata) {
	(void)idata;
	printf("\t%-10s %s\n", pe->name, pe->description ? pe->description : pe->name);
}

static const char *lookup_part(const char *name, const char *is_a) {
	assert(name != NULL);
	const struct partdb_entry *pe = partdb_find_entry(name);
	if (pe && partdb_ent_is_a(pe, name)) {
		return pe->name;
	}
	if (c_strcasecmp(name, "help") == 0) {
		partdb_foreach_is_a((partdb_iter_func)print_part_name_description, NULL, is_a);
		exit(EXIT_SUCCESS);
	}
	return NULL;
}

// Handle simple zero or one argument option setting (ie, not XCONFIG_ASSIGN).
// 'arg' should be parsed to handle any quoting or escape sequences by this
// point.

static void set_option(struct xconfig_option const *options, struct xconfig_option const *option, sds arg, void *sptr) {
	void *object;
	if (option->flags & XCONFIG_FLAG_OFFSET) {
		// A non-NULL struct pointer must have been passed if we are to
		// use offset-based options.
		assert(sptr != NULL);
		object = (char *)sptr + option->dest.object_offset;
	} else {
		object = option->dest.object;
	}

	if (option->defined)
		*(option->defined) = 1;
	switch (option->type) {
		case XCONFIG_BOOL:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_bool(1);
			else
				*(_Bool *)object = 1;
			break;
		case XCONFIG_BOOL0:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_bool(0);
			else
				*(_Bool *)object = 0;
			break;
		case XCONFIG_INT:
			{
				assert(arg != NULL);
				int val = strtol(arg, NULL, 0);
				if (option->flags & XCONFIG_FLAG_CALL)
					option->dest.func_int(val);
				else
					*(int *)object = val;
			}
			break;
		case XCONFIG_INT0:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_int(0);
			else
				*(int *)object = 0;
			break;
		case XCONFIG_INT1:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_int(1);
			else
				*(int *)object = 1;
			break;
		case XCONFIG_UINT8:
			{
				assert(arg != NULL);
				int val = strtol(arg, NULL, 0);
				if (!(option->flags & XCONFIG_FLAG_CALL))
					*(uint8_t *)object = val;
			}
			break;
		case XCONFIG_UINT16:
			{
				assert(arg != NULL);
				int val = strtol(arg, NULL, 0);
				if (!(option->flags & XCONFIG_FLAG_CALL))
					*(uint16_t *)object = val;
			}
			break;
		case XCONFIG_DOUBLE:
			{
				assert(arg != NULL);
				double val = strtod(arg, NULL);
				if (option->flags & XCONFIG_FLAG_CALL)
					option->dest.func_double(val);
				else
					*(double *)object = val;
			}
			break;
		case XCONFIG_STRING:
			if (option->flags & XCONFIG_FLAG_CALL) {
				option->dest.func_string(arg);
			} else {
				if (*(char **)object)
					free(*(char **)object);
				*(char **)object = xstrdup(arg);
			}
			break;
		case XCONFIG_STRING_LIST:
			assert(!(option->flags & XCONFIG_FLAG_CALL));
			*(struct slist **)object = slist_append(*(struct slist **)object, sdsdup(arg));
			break;
		case XCONFIG_NONE:
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_null();
			break;
		case XCONFIG_ENUM: {
			int val = lookup_enum((struct xconfig_enum *)option->ref, arg, -1);
			if (option->flags & XCONFIG_FLAG_CALL)
				option->dest.func_int(val);
			else
				*(int *)object = val;
			}
			break;
		case XCONFIG_PART: {
			const char *pname = lookup_part(arg, (char *)option->ref);
			if (*(char **)object)
				free(*(char **)object);
			*(char **)object = NULL;
			if (pname) {
				*(char **)object = xstrdup(pname);
			}
			}
			break;
		case XCONFIG_ALIAS:
			// Be aware this will process any argument for escapes
			xconfig_set_option_struct(options, (char *)object, (char *)option->ref, sptr);
			break;
		case XCONFIG_ALIAS1: {
			// User-supplied argument already parsed, so don't use
			// xconfig_set_option for this or it'll do it again.
			// Note at the moment this precludes the use of "no-".
			option = find_option(options, (char *)object);
			if (option) {
				set_option(options, option, arg, sptr);
			}
			}
			break;
		default:
			break;
	}
}

// Returns false on error.
static _Bool unset_option(struct xconfig_option const *option, void *sptr) {
	void *object;
	if (option->flags & XCONFIG_FLAG_OFFSET) {
		// A non-NULL struct pointer must have been passed if we are to
		// use offset-based options.
		assert(sptr != NULL);
		object = (char *)sptr + option->dest.object_offset;
	} else {
		object = option->dest.object;
	}

	if (option->defined)
		*(option->defined) = 1;

	switch (option->type) {
	case XCONFIG_BOOL:
		if (option->flags & XCONFIG_FLAG_CALL)
			option->dest.func_bool(0);
		else
			*(_Bool *)object = 0;
		return 1;
	case XCONFIG_BOOL0:
		if (option->flags & XCONFIG_FLAG_CALL)
			option->dest.func_bool(1);
		else
			*(_Bool *)object = 1;
		return 1;
	case XCONFIG_INT0:
		if (option->flags & XCONFIG_FLAG_CALL)
			option->dest.func_int(1);
		else
			*(int *)object = 1;
		return 1;
	case XCONFIG_INT1:
		if (option->flags & XCONFIG_FLAG_CALL)
			option->dest.func_int(0);
		else
			*(int *)object = 0;
		return 1;
	case XCONFIG_STRING:
		if (option->flags & XCONFIG_FLAG_CALL) {
			option->dest.func_string(NULL);
		} else if (*(char **)object) {
			free(*(char **)object);
			*(char **)object = NULL;
		}
		return 1;
	case XCONFIG_STRING_LIST:
		assert(!(option->flags & XCONFIG_FLAG_CALL));
		/* providing an argument to remove here might more sense, but
		 * for now just remove the entire list: */
		slist_free_full(*(struct slist **)object, (slist_free_func)sdsfree);
		*(struct slist **)object = NULL;
		return 1;
	default:
		break;
	}
	return 0;
}

static void xconfig_warn_deprecated(const struct xconfig_option *opt) {
	if (!opt->deprecated)
		return;
	LOG_WARN("Deprecated option `%s'", opt->name);
	if (opt->type == XCONFIG_ALIAS || opt->type == XCONFIG_ALIAS1) {
		LOG_PRINT(".  Try `%s' instead.", (char *)opt->dest.object);
	}
	LOG_PRINT("\n");
}

// Convenience function to manually set an option.  Only handles simple zero-
// or one-argument options.  'arg' will be parsed to process escape sequences,
// but should not contain quoted sections.

enum xconfig_result xconfig_set_option(struct xconfig_option const *options,
				       const char *opt, const char *arg) {
	return xconfig_set_option_struct(options, opt, arg, NULL);
}

enum xconfig_result xconfig_set_option_struct(struct xconfig_option const *options,
				       const char *opt, const char *arg, void *sptr) {
	struct xconfig_option const *option = find_option(options, opt);
	if (option == NULL) {
		if (0 == strncmp(opt, "no-", 3)) {
			option = find_option(options, opt + 3);
			if (option && unset_option(option, sptr)) {
				return XCONFIG_OK;
			}
		}
		LOG_ERROR("Unrecognised option `%s'\n", opt);
		return XCONFIG_BAD_OPTION;
	}
	xconfig_warn_deprecated(option);
	if (option->type == XCONFIG_BOOL ||
	    option->type == XCONFIG_BOOL0 ||
	    option->type == XCONFIG_INT0 ||
	    option->type == XCONFIG_INT1 ||
	    option->type == XCONFIG_NONE ||
	    option->type == XCONFIG_ALIAS) {
		set_option(options, option, NULL, sptr);
		return XCONFIG_OK;
	}
	if (!arg) {
		LOG_ERROR("Missing argument to `%s'\n", opt);
		return XCONFIG_MISSING_ARG;
	}
	sds arg_s = sdsx_parse_str(arg);
	set_option(options, option, arg_s, sptr);
	sdsfree(arg_s);
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
	return xconfig_parse_line_struct(options, line, NULL);
}

enum xconfig_result xconfig_parse_line_struct(struct xconfig_option const *options, const char *line, void *sptr) {
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
			if (option && unset_option(option, sptr)) {
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

	xconfig_warn_deprecated(option);
	if (option->type == XCONFIG_BOOL ||
	    option->type == XCONFIG_BOOL0 ||
	    option->type == XCONFIG_INT0 ||
	    option->type == XCONFIG_INT1 ||
	    option->type == XCONFIG_NONE ||
	    option->type == XCONFIG_ALIAS) {
		set_option(options, option, NULL, sptr);
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
	set_option(options, option, value, sptr);
	sdsfree(value);
	return XCONFIG_OK;
}

// Parse a list of lines

enum xconfig_result xconfig_parse_list_struct(struct xconfig_option const *options, struct slist *list, void *sptr) {
	int ret = XCONFIG_OK;
	for ( ; list; list = list->next) {
		sds line = list->data;
		enum xconfig_result r = xconfig_parse_line_struct(options, line, sptr);
		if (r != XCONFIG_OK)
			ret = r;
	}
	return ret;
}

// Parse CLI options

enum xconfig_result xconfig_parse_cli(struct xconfig_option const *options,
		int argc, char **argv, int *argn) {
	return xconfig_parse_cli_struct(options, argc, argv, argn, NULL);
}

enum xconfig_result xconfig_parse_cli_struct(struct xconfig_option const *options,
		int argc, char **argv, int *argn, void *sptr) {
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
				if (option && unset_option(option, sptr)) {
					_argn++;
					continue;
				}
			}
			if (argn) *argn = _argn;
			LOG_ERROR("Unrecognised option `%s'\n", opt);
			return XCONFIG_BAD_OPTION;
		}
		xconfig_warn_deprecated(option);
		if (option->type == XCONFIG_BOOL ||
		    option->type == XCONFIG_BOOL0 ||
		    option->type == XCONFIG_INT0 ||
		    option->type == XCONFIG_INT1 ||
		    option->type == XCONFIG_NONE ||
		    option->type == XCONFIG_ALIAS) {
			set_option(options, option, NULL, sptr);
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
				for (unsigned i = 0; i < values->len; ++i) {
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
			set_option(options, option, arg, sptr);
			sdsfree(arg);
		}
		_argn += 2;
	}
	if (argn) *argn = _argn;
	return XCONFIG_OK;
}

// This is basically the same as lookup_enum(), but doesn't test for "help".

int xconfig_enum_name_value(struct xconfig_enum *list, const char *name, int undef) {
	assert(name != NULL);
	assert(list != NULL);
	for (struct xconfig_enum *iter = list; iter->name; ++iter) {
		if (0 == c_strcasecmp(name, iter->name)) {
			return iter->value;
		}
	}
	return undef;
}

const char *xconfig_enum_value_name(struct xconfig_enum *list, int value) {
	assert(list != NULL);
	for (struct xconfig_enum *iter = list; iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		if (iter->value == value) {
			return iter->name;
		}
	}
	return NULL;
}

const char *xconfig_enum_name_description(struct xconfig_enum *list, const char *name) {
	for (struct xconfig_enum *iter = list; iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		if (0 == c_strcasecmp(name, iter->name)) {
			return iter->description;
		}
	}
	return NULL;
}

const char *xconfig_enum_value_description(struct xconfig_enum *list, int value) {
	assert(list != NULL);
	for (struct xconfig_enum *iter = list; iter->name; ++iter) {
		if (!iter->description) {
			continue;
		}
		if (iter->value == value) {
			return iter->description;
		}
	}
	return NULL;
}

int xconfig_check_enum(struct xconfig_enum *list, int val, int dfl) {
	for (size_t i = 0; list[i].name; ++i) {
		if (list[i].value == val) {
			return val;
		}
	}
	return dfl;
}

void xconfig_shutdown(struct xconfig_option const *options) {
	for (size_t i = 0; options[i].type != XCONFIG_END; ++i) {
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
