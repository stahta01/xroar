/*

Becker port support

Copyright 2012-2019 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

The "becker port" is an IP version of the usually-serial DriveWire protocol.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// for addrinfo
#define _POSIX_C_SOURCE 200112L

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef WINDOWS32

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#else

/* Windows has a habit of making include order important: */
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#endif

#include "xalloc.h"

#include "becker.h"
#include "logging.h"
#include "part.h"
#include "xroar.h"

/* In theory no reponse should be longer than this (though it doesn't actually
 * matter, this only constrains how much is read at a time). */
#define INPUT_BUFFER_SIZE 262
#define OUTPUT_BUFFER_SIZE 16

struct becker {
	struct part part;

	int sockfd;
	char input_buf[INPUT_BUFFER_SIZE];
	int input_buf_ptr;
	int input_buf_length;
	char output_buf[OUTPUT_BUFFER_SIZE];
	int output_buf_ptr;
	int output_buf_length;

	// Debugging
	struct log_handle *log_data_in_hex;
	struct log_handle *log_data_out_hex;
};

static void becker_free(struct part *p);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct becker *becker_new(void) {
	struct becker *becker = part_new(sizeof(*becker));
	*becker = (struct becker){0};
	part_init(&becker->part, "becker");
	becker->part.free = becker_free;

	struct addrinfo hints, *info = NULL;
	const char *hostname = xroar_cfg.becker_ip ? xroar_cfg.becker_ip : BECKER_IP_DEFAULT;
	const char *portname = xroar_cfg.becker_port ? xroar_cfg.becker_port : BECKER_PORT_DEFAULT;

	int sockfd = -1;

	// Find the server
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	if (getaddrinfo(hostname, portname, &hints, &info) < 0) {
		LOG_WARN("becker: getaddrinfo %s:%s failed\n", hostname, portname);
		goto failed;
	}
	if (!info) {
		LOG_WARN("becker: failed lookup %s:%s\n", hostname, portname);
		goto failed;
	}

	// Create a socket...
	sockfd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
	if (sockfd < 0) {
		LOG_WARN("becker: socket not created\n");
		goto failed;
	}

	// ... and connect it to the requested server
	if (connect(sockfd, info->ai_addr, info->ai_addrlen) < 0) {
		LOG_WARN("becker: connect %s:%s failed\n", hostname, portname);
		goto failed;
	}

	freeaddrinfo(info);

	// Set the socket to non-blocking
#ifndef WINDOWS32
	int flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#else
	u_long iMode = 1;
	if (ioctlsocket(sockfd, FIONBIO, &iMode) != NO_ERROR) {
		LOG_WARN("becker: couldn't set non-blocking mode on socket\n");
		goto failed;
	}
#endif

	becker->sockfd = sockfd;

	becker_reset(becker);
	return becker;

failed:
	if (sockfd != -1) {
		close(sockfd);
	}
	if (info)
		freeaddrinfo(info);
	part_free(&becker->part);
	return NULL;
}

static void becker_free(struct part *p) {
	struct becker *becker = (struct becker *)p;
	close(becker->sockfd);
	if (becker->log_data_in_hex)
		log_close(&becker->log_data_in_hex);
	if (becker->log_data_out_hex)
		log_close(&becker->log_data_out_hex);
}

void becker_reset(struct becker *becker) {
	if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_BECKER) {
		log_open_hexdump(&becker->log_data_in_hex, "BECKER IN ");
		log_open_hexdump(&becker->log_data_out_hex, "BECKER OUT");
	}
}

static void fetch_input(struct becker *becker) {
	if (becker->input_buf_ptr == 0) {
		ssize_t new = recv(becker->sockfd, becker->input_buf, INPUT_BUFFER_SIZE, 0);
		if (new > 0) {
			becker->input_buf_length = new;
			if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_BECKER) {
				// flush & reopen output hexdump
				log_open_hexdump(&becker->log_data_out_hex, "BECKER OUT");
				for (unsigned i = 0; i < (unsigned)new; i++)
					log_hexdump_byte(becker->log_data_in_hex, becker->input_buf[i]);
			}
		}
	}
}

static void write_output(struct becker *becker) {
	if (becker->output_buf_length > 0) {
		ssize_t sent = send(becker->sockfd, becker->output_buf + becker->output_buf_ptr, becker->output_buf_length - becker->output_buf_ptr, 0);
		if (sent > 0) {
			if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_BECKER) {
				// flush & reopen input hexdump
				log_open_hexdump(&becker->log_data_in_hex, "BECKER IN ");
				for (unsigned i = 0; i < (unsigned)sent; i++)
					log_hexdump_byte(becker->log_data_out_hex, becker->output_buf[becker->output_buf_ptr + i]);
			}
			becker->output_buf_ptr += sent;
			if (becker->output_buf_ptr >= becker->output_buf_length) {
				becker->output_buf_ptr = becker->output_buf_length = 0;
			}
		}
	}
}

uint8_t becker_read_status(struct becker *becker) {
	if (xroar_cfg.debug_fdc & XROAR_DEBUG_FDC_BECKER) {
		// flush both hexdump logs
		log_hexdump_line(becker->log_data_in_hex);
		log_hexdump_line(becker->log_data_out_hex);
	}
	fetch_input(becker);
	if (becker->input_buf_length > 0)
		return 0x02;
	return 0x00;
}

uint8_t becker_read_data(struct becker *becker) {
	fetch_input(becker);
	if (becker->input_buf_length == 0)
		return 0x00;
	uint8_t r = becker->input_buf[becker->input_buf_ptr++];
	if (becker->input_buf_ptr == becker->input_buf_length) {
		becker->input_buf_ptr = becker->input_buf_length = 0;
	}
	return r;
}

void becker_write_data(struct becker *becker, uint8_t D) {
	if (becker->output_buf_length < OUTPUT_BUFFER_SIZE) {
		becker->output_buf[becker->output_buf_length++] = D;
	}
	write_output(becker);
}
