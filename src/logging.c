/** \file
 *
 *  \brief General logging framework.
 *
 *  \copyright Copyright 2013-2024 Ciaran Anscomb
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "xalloc.h"

#include "logging.h"
#include "xroar.h"

struct logging logging = {
	.level = 1,
};

enum log_type {
	LOG_HEXDUMP,
};

struct log_handle {
	enum log_type type;
	const char *prefix;
	union {
		struct {
			unsigned address;
			unsigned nbytes;
			uint8_t buf[16];
			unsigned flag;
		} hexdump;
	} ctx;
};

static void log_open(struct log_handle **lp, const char *prefix, enum log_type type) {
	assert(lp != NULL);
	log_close(lp);
	struct log_handle *l = xmalloc(sizeof(*l));
	*l = (struct log_handle){0};
	l->type = type;
	l->prefix = prefix;
	*lp = l;
}

void log_open_hexdump(struct log_handle **lp, const char *prefix) {
	log_open(lp, prefix, LOG_HEXDUMP);
	struct log_handle *l = *lp;
	l->ctx.hexdump.address = 0;
	l->ctx.hexdump.nbytes = 0;
	l->ctx.hexdump.flag = -1;
}

void log_close(struct log_handle **lp) {
	assert(lp != NULL);
	struct log_handle *l = *lp;
	if (!l)
		return;
	switch (l->type) {
	case LOG_HEXDUMP:
		log_hexdump_line(l);
		break;
	default:
		break;
	}
	free(l);
	*lp = NULL;
}

void log_hexdump_set_addr(struct log_handle *l, unsigned addr) {
	if (!l)
		return;
	if (l->ctx.hexdump.address != addr) {
		log_hexdump_line(l);
		l->ctx.hexdump.address = addr;
	}
}

void log_hexdump_line(struct log_handle *l) {
	if (!l)
		return;
	assert(l->prefix != NULL);
	assert(l->type == LOG_HEXDUMP);
	if (l->ctx.hexdump.nbytes == 0)
		return;
	LOG_PRINT("%s %04x  ", l->prefix, l->ctx.hexdump.address);
	unsigned i;
	for (i = 0; i < l->ctx.hexdump.nbytes; i++) {
		int f = ((i + 1) == l->ctx.hexdump.flag) ? '*' : ' ';
		LOG_PRINT("%02x%c", l->ctx.hexdump.buf[i], f);
		if (i == 7)
			LOG_PRINT(" ");
	}
	for (; i < 16; i++) {
		LOG_PRINT("   ");
		if (i == 7)
			LOG_PRINT(" ");
	}
	LOG_PRINT(" |");
	for (i = 0; i < l->ctx.hexdump.nbytes; i++) {
		int c = l->ctx.hexdump.buf[i];
		LOG_PRINT("%c", isprint(c) ? c : (int)'.');
	}
	LOG_PRINT("|\n");
	l->ctx.hexdump.address += l->ctx.hexdump.nbytes;
	l->ctx.hexdump.nbytes = 0;
	l->ctx.hexdump.flag = -1;
}

void log_hexdump_byte(struct log_handle *l, uint8_t b) {
	if (!l)
		return;
	assert(l->type == LOG_HEXDUMP);
	if (l->ctx.hexdump.nbytes >= 16)
		log_hexdump_line(l);
	l->ctx.hexdump.buf[l->ctx.hexdump.nbytes++] = b;
}

void log_hexdump_block(struct log_handle *l, uint8_t *buf, unsigned len) {
	if (!l)
		return;
	while (len > 0) {
		log_hexdump_byte(l, *(buf++));
		len--;
	}
}

void log_hexdump_flag(struct log_handle *l) {
	if (!l)
		return;
	assert(l->type == LOG_HEXDUMP);
	l->ctx.hexdump.flag = l->ctx.hexdump.nbytes;
}
