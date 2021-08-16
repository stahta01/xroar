/** \file
 *
 *  \brief GDB protocol support.
 *
 *  \copyright Copyright 2013-2021 Ciaran Anscomb
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

 * Support a subset of the gdb protocol over a socket.  See
 * http://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html
 *
 * The following registers are accessible:

 *    | Index | Name  | Bits
 *    | :---- | :---- | :----
 *    | 0     | CC    | 8
 *    | 1     | A     | 8
 *    | 2     | B     | 8
 *    | 3     | DP    | 8
 *    | 4     | X     | 16
 *    | 5     | Y     | 16
 *    | 6     | U     | 16
 *    | 7     | S     | 16
 *    | 8     | PC    | 16
 *    | 9     | MD    | 8       (HD6309)
 *    | 10    | E     | 8       (HD6309)
 *    | 11    | F     | 8       (HD6309)
 *    | 12    | V     | 16      (HD6309)

 * 'g' packet responses will contain 14 hex pairs comprising the 6809 register,
 * and either a further 5 hex pairs for the 6309 registers, or 'xx'.  'G'
 * packets must supply 19 values, either hex pairs or 'xx'.
 *
 * 'm' and 'M' packets will read or write translated memory addresses (as seen
 * by the CPU).
 *
 * Breakpoints and watchpoints are supported ('Z' and 'z').
 *
 * Some standard, and some vendor-specific general queries are supported:

 *      qxroar.sam    | XXXX  | get SAM register, reply is 4 hex digits
 *      qSupported    | XX... | report PacketSize
 *      qAttached     | 1     | always report attached

 * Only these vendor-specific general sets are supported:

 *      Qxroar.sam:XXXX       | set SAM register (4 hex digits)

 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// for addrinfo, struct timeval
#define _POSIX_C_SOURCE 200112L
// For strsep
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "pl-string.h"
#include "xalloc.h"

#ifndef WINDOWS32

#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#else

/* Windows has a habit of making include order important: */
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#endif

#include "breakpoint.h"
#include "gdb.h"
#include "hd6309.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "sam.h"
#include "xroar.h"

struct gdb_interface_private {
	struct machine *machine;

	struct MC6809 *cpu;
	struct MC6883 *sam;

	// Breakpoint session
	struct bp_session *bp_session;

	// Thread info
	int listenfd;
	struct addrinfo *info;
	pthread_t sock_thread;
	int sockfd;

	// Run state
	enum gdb_run_state run_state;
	pthread_cond_t run_state_cv;
	pthread_mutex_t run_state_mt;
	int last_signal;
};

static void *handle_tcp_sock(void *sptr);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

enum gdb_error {
	GDBE_OK = 0,
	GDBE_BAD_CHECKSUM,
	GDBE_BREAK,
	GDBE_READ_ERROR,
	GDBE_WRITE_ERROR,
};

static char in_packet[1025];
static char packet[1025];

static int read_packet(struct gdb_interface_private *gip, char *buffer, unsigned count);
static int send_packet(struct gdb_interface_private *gip, const char *buffer, unsigned count);
static int send_packet_string(struct gdb_interface_private *gip, const char *string);
static int send_char(struct gdb_interface_private *gip, char c);

static void send_last_signal(struct gdb_interface_private *gip);  // ?
static void send_general_registers(struct gdb_interface_private *gip);  // g
static void set_general_registers(struct gdb_interface_private *gip, char *args);  // G
static void send_memory(struct gdb_interface_private *gip, char *args);  // m
static void set_memory(struct gdb_interface_private *gip, char *args);  // M
static void send_register(struct gdb_interface_private *gip, char *args);  // p
static void set_register(struct gdb_interface_private *gip, char *args);  // P
static void general_query(struct gdb_interface_private *gip, char *args);  // q
static void general_set(struct gdb_interface_private *gip, char *args);  // Q
static void add_breakpoint(struct gdb_interface_private *gip, char *args);  // Z
static void remove_breakpoint(struct gdb_interface_private *gip, char *args);  // z

static void send_supported(struct gdb_interface_private *gip, char *args);  // qSupported

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int hexdigit(char c);
static int hex8(char *s);
static int hex16(char *s);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct gdb_interface *gdb_interface_new(const char *hostname, const char *portname, struct machine *m, struct bp_session *bp_session) {
	struct gdb_interface_private *gip = xmalloc(sizeof(*gip));
	*gip = (struct gdb_interface_private){0};

	gip->machine = m;
	gip->cpu = m->get_component(m, "CPU0");
	gip->sam = m->get_component(m, "SAM0");
	gip->bp_session = bp_session;
	gip->run_state = gdb_run_state_running;

	struct addrinfo hints;
	if (!hostname)
		hostname = GDB_IP_DEFAULT;
	if (!portname)
		portname = GDB_PORT_DEFAULT;

	// Find the interface
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	if (getaddrinfo(hostname, portname, &hints, &gip->info) < 0) {
		LOG_WARN("gdb: getaddrinfo %s:%s failed\n", hostname, portname);
		goto failed;
	}
	if (!gip->info) {
		LOG_WARN("gdb: failed lookup %s:%s\n", hostname, portname);
		goto failed;
	}

	// Create a socket...
	gip->listenfd = socket(gip->info->ai_family, gip->info->ai_socktype, gip->info->ai_protocol);
	if (gip->listenfd < 0) {
		LOG_WARN("gdb: socket not created\n");
		goto failed;
	}

	// bind
	if (bind(gip->listenfd, gip->info->ai_addr, gip->info->ai_addrlen) < 0) {
		LOG_WARN("gdb: bind %s:%s failed\n", hostname, portname);
		goto failed;
	}

	// ... and listen
	if (listen(gip->listenfd, 1) < 0) {
		LOG_WARN("gdb: failed to listen to socket\n");
		goto failed;
	}

	pthread_mutex_init(&gip->run_state_mt, NULL);
	pthread_cond_init(&gip->run_state_cv, NULL);
	pthread_create(&gip->sock_thread, NULL, handle_tcp_sock, gip);

	LOG_DEBUG(1, "gdb: target listening on %s:%s\n", hostname, portname);

	return (struct gdb_interface *)gip;

failed:
	if (gip->listenfd != -1) {
		close(gip->listenfd);
	}
	free(gip);
	return NULL;
}

void gdb_interface_free(struct gdb_interface *gi) {
	struct gdb_interface_private *gip = (struct gdb_interface_private *)gi;
	pthread_cancel(gip->sock_thread);
	pthread_join(gip->sock_thread, NULL);
	if (gip->info)
		freeaddrinfo(gip->info);
	pthread_mutex_destroy(&gip->run_state_mt);
	pthread_cond_destroy(&gip->run_state_cv);
	if (gip->listenfd != -1) {
		close(gip->listenfd);
	}
	free(gip);
}

// Lock the run_state mutex and return true if ready to run
int gdb_run_lock(struct gdb_interface *gi) {
	struct gdb_interface_private *gip = (struct gdb_interface_private *)gi;

	pthread_mutex_lock(&gip->run_state_mt);
	if (gip->run_state == gdb_run_state_stopped) {
		// If machine stopped, wait up to 20ms for state to change
		struct timeval tv;
		gettimeofday(&tv, NULL);
		tv.tv_usec += 20000;
		tv.tv_sec += (tv.tv_usec / 1000000);
		tv.tv_usec %= 1000000;
		struct timespec ts;
		ts.tv_sec = tv.tv_sec;
		ts.tv_nsec = tv.tv_usec * 1000;
		if (pthread_cond_timedwait(&gip->run_state_cv, &gip->run_state_mt, &ts) == ETIMEDOUT) {
			pthread_mutex_unlock(&gip->run_state_mt);
			return gdb_run_state_stopped;
		}
	}
	return gip->run_state;
}

void gdb_run_unlock(struct gdb_interface *gi) {
	struct gdb_interface_private *gip = (struct gdb_interface_private *)gi;
	pthread_mutex_unlock(&gip->run_state_mt);
}

static void gdb_handle_signal(struct gdb_interface_private *gip, int sig) {
	gip->last_signal = sig;
	send_last_signal(gip);
}

void gdb_stop(struct gdb_interface *gi, int sig) {
	struct gdb_interface_private *gip = (struct gdb_interface_private *)gi;
	gip->run_state = gdb_run_state_stopped;
	gdb_handle_signal(gip, sig);
}

void gdb_single_step(struct gdb_interface *gi) {
	struct gdb_interface_private *gip = (struct gdb_interface_private *)gi;
	gip->run_state = gdb_run_state_stopped;
	gdb_handle_signal(gip, MACHINE_SIGTRAP);
	pthread_cond_signal(&gip->run_state_cv);
}

static void gdb_machine_single_step(struct gdb_interface_private *gip) {
	pthread_mutex_lock(&gip->run_state_mt);
	if (gip->run_state == gdb_run_state_stopped) {
		gip->run_state = gdb_run_state_single_step;
		pthread_cond_wait(&gip->run_state_cv, &gip->run_state_mt);
	}
	pthread_mutex_unlock(&gip->run_state_mt);
}

static void gdb_machine_signal(struct gdb_interface_private *gip, int sig) {
	pthread_mutex_lock(&gip->run_state_mt);
	if (gip->run_state == gdb_run_state_running) {
		gip->machine->signal(gip->machine, sig);
		gip->run_state = gdb_run_state_stopped;
		gdb_handle_signal(gip, sig);
	}
	pthread_mutex_unlock(&gip->run_state_mt);
}

static void gdb_continue(struct gdb_interface_private *gip) {
	//struct gdb_interface_private *gip = (struct gdb_interface_private *)gi;
	pthread_mutex_lock(&gip->run_state_mt);
	if (gip->run_state == gdb_run_state_stopped) {
		gip->run_state = gdb_run_state_running;
		pthread_cond_signal(&gip->run_state_cv);
	}
	pthread_mutex_unlock(&gip->run_state_mt);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void *handle_tcp_sock(void *sptr) {
	struct gdb_interface_private *gip = sptr;

	for (;;) {

		/* Work around an oddness in Windows or MinGW where (struct
		 * addrinfo).ai_addrlen is size_t instead of sockaddr_t, but
		 * accept() takes an (int *).  Raises a warning when compiling
		 * 64-bit. */
		socklen_t ai_addrlen = gip->info->ai_addrlen;

		/* Work around Windows not killing off accept() with a 200ms
		 * select() timeout. */
		while (1) {
			fd_set fds;
			struct timeval tv;
			FD_ZERO(&fds);
			FD_SET(gip->listenfd, &fds);
			tv.tv_sec = 0;
			tv.tv_usec = 200000;
			pthread_testcancel();
			int r = select(gip->listenfd+1, &fds, NULL, NULL, &tv);
			if (r > 0) {
				gip->sockfd = accept(gip->listenfd, gip->info->ai_addr, &ai_addrlen);
				break;
			}
		}

		if (gip->sockfd < 0) {
			LOG_WARN("gdb: accept() failed\n");
			continue;
		}
		{
			int flag = 1;
			setsockopt(gip->sockfd, IPPROTO_TCP, TCP_NODELAY, (void const *)&flag, sizeof(flag));
		}
		LOG_DEBUG_GDB(LOG_GDB_CONNECT, "gdb: connection accepted\n");

		gdb_machine_signal(gip, MACHINE_SIGINT);
		_Bool attached = 1;
		while (attached) {
			int l = read_packet(gip, in_packet, sizeof(in_packet));
			if (l == -GDBE_BREAK) {
				LOG_DEBUG_GDB(LOG_GDB_PACKET, "gdb: BREAK\n");
				gdb_machine_signal(gip, MACHINE_SIGINT);
				continue;
			} else if (l == -GDBE_BAD_CHECKSUM) {
				if (send_char(gip, '-') < 0)
					break;
				continue;
			} else if (l < 0) {
				break;
			}
			if (logging.debug_gdb & LOG_GDB_PACKET) {
				if (gip->run_state == gdb_run_state_stopped) {
					LOG_PRINT("gdb: packet received: ");
				} else {
					LOG_PRINT("gdb: packet ignored (send ^C first): ");
				}
				for (unsigned i = 0; i < (unsigned)l; i++) {
					if (isprint(in_packet[i])) {
						LOG_PRINT("%c", in_packet[i]);
					} else {
						LOG_PRINT("\\%o", in_packet[i] & 0xff);
					}
				}
				LOG_PRINT("\n");
			}
			if (gip->run_state != gdb_run_state_stopped) {
				if (send_char(gip, '-') < 0)
					break;
				continue;
			}
			if (send_char(gip, '+') < 0)
				break;

			char *args = &in_packet[1];

			switch (in_packet[0]) {

			case '?':
				send_last_signal(gip);
				break;

			case 'c':
				gdb_continue(gip);
				break;

			case 'D':
				send_packet_string(gip, "OK");
				attached = 0;
				break;

			case 'g':
				send_general_registers(gip);
				break;

			case 'G':
				set_general_registers(gip, args);
				break;

			case 'm':
				send_memory(gip, args);
				break;

			case 'M':
				set_memory(gip, args);
				break;

			case 'p':
				send_register(gip, args);
				break;

			case 'P':
				set_register(gip, args);
				break;

			case 'q':
				general_query(gip, args);
				break;

			case 'Q':
				general_set(gip, args);
				break;

			case 's':
				gdb_machine_single_step(gip);
				break;

			case 'z':
				remove_breakpoint(gip, args);
				break;

			case 'Z':
				add_breakpoint(gip, args);
				break;

			default:
				send_packet(gip, NULL, 0);
				break;
			}
		}
		close(gip->sockfd);
		gdb_continue(gip);
		LOG_DEBUG_GDB(LOG_GDB_CONNECT, "gdb: connection closed\n");
	}
	return NULL;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

enum packet_state {
	packet_wait,
	packet_read,
	packet_csum0,
	packet_csum1,
};

static int read_packet(struct gdb_interface_private *gip, char *buffer, unsigned count) {
	enum packet_state state = packet_wait;
	unsigned length = 0;
	uint8_t packet_sum = 0;
	uint8_t csum = 0;
	char in_byte;
	int tmp;

	while (1) {

		// Another Windows workaround - recv() not a cancellation point?
		while (1) {
			fd_set fds;
			struct timeval tv;
			FD_ZERO(&fds);
			FD_SET(gip->sockfd, &fds);
			tv.tv_sec = 0;
			tv.tv_usec = 200000;
			pthread_testcancel();
			int r = select(gip->sockfd+1, &fds, NULL, NULL, &tv);
			if (r > 0) {
				break;
			}
		}

		int r = recv(gip->sockfd, &in_byte, 1, 0);
		if (r < 0)
			return -GDBE_READ_ERROR;
		if (r == 0)
			continue;

		switch (state) {
		case packet_wait:
			if (in_byte == '$') {
				packet_sum = 0;
				state = packet_read;
			} else if (in_byte == 3) {
				return -GDBE_BREAK;
			}
			break;
		case packet_read:
			if (in_byte == '#') {
				state = packet_csum0;
			} else {
				if (length < (count - 1)) {
					buffer[length++] = in_byte;
					packet_sum += (uint8_t)in_byte;
				}
			}
			break;
		case packet_csum0:
			tmp = hexdigit(in_byte);
			if (tmp < 0) {
				state = packet_wait;
			} else {
				csum = tmp << 4;
				state = packet_csum1;
			}
			break;
		case packet_csum1:
			tmp = hexdigit(in_byte);
			if (tmp < 0) {
				state = packet_wait;
				break;
			}
			csum |= tmp;
			if (csum != packet_sum) {
				if (logging.debug_gdb & LOG_GDB_CHECKSUM) {
					LOG_PRINT("gdb: bad checksum in '");
					if (isprint(buffer[0]))
						LOG_PRINT("%c", buffer[0]);
					else
						LOG_PRINT("0x%02x", buffer[0]);
					LOG_PRINT("' packet.  Expected 0x%02x, got 0x%02x.\n",
						  packet_sum, csum);
				}
				return -GDBE_BAD_CHECKSUM;
			}
			buffer[length] = 0;
			return length;
		}

	}

	return -GDBE_READ_ERROR;
}

static int send_packet(struct gdb_interface_private *gip, const char *buffer, unsigned count) {
	char tmpbuf[4];
	uint8_t csum = 0;
	tmpbuf[0] = '$';
	if (send(gip->sockfd, tmpbuf, 1, 0) < 0)
		return -GDBE_WRITE_ERROR;
	for (unsigned i = 0; i < count; i++) {
		csum += buffer[i];
		switch (buffer[i]) {
		case '#':
		case '$':
		case 0x7d:
		case '*':
			tmpbuf[0] = 0x7d;
			tmpbuf[1] = buffer[i] ^ 0x20;
			if (send(gip->sockfd, tmpbuf, 2, 0) < 0)
				return -GDBE_WRITE_ERROR;
			break;
		default:
			if (send(gip->sockfd, &buffer[i], 1, 0) < 0)
				return -GDBE_WRITE_ERROR;
			break;
		}
	}
	snprintf(tmpbuf, sizeof(tmpbuf), "#%02x", (unsigned)csum);
	if (send(gip->sockfd, tmpbuf, 3, 0) < 0)
		return -GDBE_WRITE_ERROR;
	// the reply ("+" or "-") will be discarded by the next read_packet

	if (logging.debug_gdb & LOG_GDB_PACKET) {
		LOG_PRINT("gdb: packet sent: ");
		for (unsigned i = 0; i < (unsigned)count; i++) {
			if (isprint(buffer[i])) {
				LOG_PRINT("%c", buffer[i]);
			} else {
				LOG_PRINT("\\%o", buffer[i] & 0xff);
			}
		}
		LOG_PRINT("\n");
	}

	return count;
}

static int send_packet_string(struct gdb_interface_private *gip, const char *string) {
	unsigned count = strlen(string);
	return send_packet(gip, string, count);
}

static int send_char(struct gdb_interface_private *gip, char c) {
	if (send(gip->sockfd, &c, 1, 0) < 0)
		return -GDBE_WRITE_ERROR;
	return GDBE_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void send_last_signal(struct gdb_interface_private *gip) {
	char tmpbuf[4];
	snprintf(tmpbuf, sizeof(tmpbuf), "S%02x", gip->last_signal);
	send_packet(gip, tmpbuf, 3);
}

static void send_general_registers(struct gdb_interface_private *gip) {
	sprintf(packet, "%02x%02x%02x%02x%04x%04x%04x%04x%04x",
		 gip->cpu->reg_cc,
		 MC6809_REG_A(gip->cpu),
		 MC6809_REG_B(gip->cpu),
		 gip->cpu->reg_dp,
		 gip->cpu->reg_x,
		 gip->cpu->reg_y,
		 gip->cpu->reg_u,
		 gip->cpu->reg_s,
		 gip->cpu->reg_pc);
	if (gip->cpu->variant == MC6809_VARIANT_HD6309) {
		sprintf(packet + 28, "%02x%02x%02x%04x",
			 ((struct HD6309 *)gip->cpu)->reg_md,
			 HD6309_REG_E(((struct HD6309 *)gip->cpu)),
			 HD6309_REG_F(((struct HD6309 *)gip->cpu)),
			 ((struct HD6309 *)gip->cpu)->reg_v);
	} else {
		strcat(packet, "xxxxxxxxxx");
	}
	send_packet_string(gip, packet);
}

static void set_general_registers(struct gdb_interface_private *gip, char *args) {
	if (strlen(args) != 38) {
		send_packet_string(gip, "E00");
		return;
	}
	int tmp;
	if ((tmp = hex8(args)) >= 0)
		gip->cpu->reg_cc = tmp;
	if ((tmp = hex8(args+2)) >= 0)
		MC6809_REG_A(gip->cpu) = tmp;
	if ((tmp = hex8(args+4)) >= 0)
		MC6809_REG_B(gip->cpu) = tmp;
	if ((tmp = hex8(args+6)) >= 0)
		gip->cpu->reg_dp = tmp;
	if ((tmp = hex16(args+8)) >= 0)
		gip->cpu->reg_x = tmp;
	if ((tmp = hex16(args+12)) >= 0)
		gip->cpu->reg_y = tmp;
	if ((tmp = hex16(args+16)) >= 0)
		gip->cpu->reg_u = tmp;
	if ((tmp = hex16(args+20)) >= 0)
		gip->cpu->reg_s = tmp;
	if ((tmp = hex16(args+24)) >= 0)
		gip->cpu->reg_pc = tmp;
	if (gip->cpu->variant == MC6809_VARIANT_HD6309) {
		if ((tmp = hex8(args+28)) >= 0)
			((struct HD6309 *)gip->cpu)->reg_md = tmp;
		if ((tmp = hex8(args+30)) >= 0)
			HD6309_REG_E(((struct HD6309 *)gip->cpu)) = tmp;
		if ((tmp = hex8(args+32)) >= 0)
			HD6309_REG_F(((struct HD6309 *)gip->cpu)) = tmp;
		if ((tmp = hex16(args+34)) >= 0)
			((struct HD6309 *)gip->cpu)->reg_v = tmp;
	}
	send_packet_string(gip, "OK");
}

static void send_memory(struct gdb_interface_private *gip, char *args) {
	char *addr = strsep(&args, ",");
	if (!args || !addr)
		goto error;
	uint16_t A = strtoul(addr, NULL, 16);
	unsigned length = strtoul(args, NULL, 16);
	uint8_t csum = 0;
	packet[0] = '$';
	if (send(gip->sockfd, packet, 1, 0) < 0)
		return;
	for (unsigned i = 0; i < length; i++) {
		uint8_t b = gip->machine->read_byte(gip->machine, A++);
		snprintf(packet, sizeof(packet), "%02x", b);
		csum += packet[0];
		csum += packet[1];
		if (send(gip->sockfd, packet, 2, 0) < 0)
			return;
	}
	snprintf(packet, sizeof(packet), "#%02x", csum);
	if (send(gip->sockfd, packet, 3, 0) < 0)
		return;
	// the ACK ("+") or NAK ("-") will be discarded by the next read_packet
	LOG_DEBUG_GDB(LOG_GDB_PACKET, "gdb: packet sent (binary): %u bytes\n", length);
	return;
error:
	send_packet(gip, NULL, 0);
}

static void set_memory(struct gdb_interface_private *gip, char *args) {
	char *arglist = strsep(&args, ":");
	char *data = args;
	if (!arglist || !data)
		goto error;
	char *addr = strsep(&arglist, ",");
	if (!addr || !arglist)
		goto error;
	uint16_t A = strtoul(addr, NULL, 16);
	uint16_t length = strtoul(arglist, NULL, 16);
	for (unsigned i = 0; i < length; i++) {
		if (!*data || !*(data+1))
			goto error;
		int v = hex8(data);
		if (v < 0)
			goto error;
		gip->machine->write_byte(gip->machine, A, v);
		A++;
		data += 2;
	}
	send_packet_string(gip, "OK");
	return;
error:
	send_packet_string(gip, "E00");
}

static void send_register(struct gdb_interface_private *gip, char *args) {
	unsigned regnum = strtoul(args, NULL, 16);
	unsigned value = 0;
	int size = 0;
	switch (regnum) {
	case 0: value = gip->cpu->reg_cc; size = 1; break;
	case 1: value = MC6809_REG_A(gip->cpu); size = 1; break;
	case 2: value = MC6809_REG_B(gip->cpu); size = 1; break;
	case 3: value = gip->cpu->reg_dp; size = 1; break;
	case 4: value = gip->cpu->reg_x; size = 2; break;
	case 5: value = gip->cpu->reg_y; size = 2; break;
	case 6: value = gip->cpu->reg_u; size = 2; break;
	case 7: value = gip->cpu->reg_s; size = 2; break;
	case 8: value = gip->cpu->reg_pc; size = 2; break;
	case 9: size = -1; break;
	case 10: size = -1; break;
	case 11: size = -1; break;
	case 12: size = -2; break;
	default: break;
	}
	if (gip->cpu->variant == MC6809_VARIANT_HD6309) {
		struct HD6309 *hcpu = (struct HD6309 *)gip->cpu;
		switch (regnum) {
		case 9: value = hcpu->reg_md; size = 1; break;
		case 10: value = HD6309_REG_E(hcpu); size = 1; break;
		case 11: value = HD6309_REG_F(hcpu); size = 1; break;
		case 12: value = hcpu->reg_v; size = 2; break;
		default: break;
		}
	}
	switch (size) {
	case -2: sprintf(packet, "xxxx"); break;
	case -1: sprintf(packet, "xx"); break;
	case 2: sprintf(packet, "%04x", value); break;
	case 1: sprintf(packet, "%02x", value); break;
	default: sprintf(packet, "E00"); break;
	}
	send_packet_string(gip, packet);
}

static void set_register(struct gdb_interface_private *gip, char *args) {
	char *regnum_str = strsep(&args, "=");
	if (!regnum_str || !args)
		goto error;
	unsigned regnum = strtoul(regnum_str, NULL, 16);
	unsigned value = strtoul(args, NULL, 16);
	struct HD6309 *hcpu = (struct HD6309 *)gip->cpu;
	if (regnum > 12)
		goto error;
	if (regnum > 8 && gip->cpu->variant != MC6809_VARIANT_HD6309)
		goto error;
	switch (regnum) {
	case 0: gip->cpu->reg_cc = value; break;
	case 1: MC6809_REG_A(gip->cpu) = value; break;
	case 2: MC6809_REG_B(gip->cpu) = value; break;
	case 3: gip->cpu->reg_dp = value; break;
	case 4: gip->cpu->reg_x = value; break;
	case 5: gip->cpu->reg_y = value; break;
	case 6: gip->cpu->reg_u = value; break;
	case 7: gip->cpu->reg_s = value; break;
	case 8: gip->cpu->reg_pc = value; break;
	case 9: hcpu->reg_md = value; break;
	case 10: HD6309_REG_E(hcpu) = value; break;
	case 11: HD6309_REG_F(hcpu) = value; break;
	case 12: hcpu->reg_v = value; break;
	default: break;
	}
	send_packet_string(gip, "OK");
	return;
error:
	send_packet_string(gip, "E00");
}

static void general_query(struct gdb_interface_private *gip, char *args) {
	char *query = strsep(&args, ":");
	if (0 == strncmp(query, "xroar.", 6)) {
		query += 6;
		if (0 == strcmp(query, "sam")) {
			LOG_DEBUG_GDB(LOG_GDB_QUERY, "gdb: query: xroar.sam\n");
			sprintf(packet, "%04x", sam_get_register(gip->sam));
			send_packet(gip, packet, 4);
		} else {
			LOG_DEBUG_GDB(LOG_GDB_QUERY, "gdb: query: unknown xroar vendor query\n");
		}
	} else if (0 == strcmp(query, "Supported")) {
		LOG_DEBUG_GDB(LOG_GDB_QUERY, "gdb: query: Supported\n");
		send_supported(gip, args);
	} else if (0 == strcmp(query, "Attached")) {
		LOG_DEBUG_GDB(LOG_GDB_QUERY, "gdb: query: Attached\n");
		send_packet_string(gip, "1");
	} else {
		LOG_DEBUG_GDB(LOG_GDB_QUERY, "gdb: query: unknown query\n");
		send_packet(gip, NULL, 0);
	}
}

static void general_set(struct gdb_interface_private *gip, char *args) {
	char *set = strsep(&args, ":");
	if (0 == strncmp(set, "xroar.", 6)) {
		set += 6;
		if (0 == strcmp(set, "sam")) {
			sam_set_register(gip->sam, hex16(args));
			send_packet_string(gip, "OK");
			return;
		}
	}
	send_packet(gip, NULL, 0);
	return;
}

static void add_breakpoint(struct gdb_interface_private *gip, char *args) {
	char *type_str = strsep(&args, ",");
	if (!type_str || !args)
		goto error;
	int type = *type_str - '0';
	if (type < 0 || type > 4)
		goto error;
	char *addr_str = strsep(&args, ",");
	if (!addr_str || !args)
		goto error;
	char *kind_str = strsep(&args, ";");
	if (!kind_str)
		goto error;
	unsigned addr = strtoul(addr_str, NULL, 16);
	if (type <= 1) {
		bp_hbreak_add(gip->bp_session, addr, 0, 0);
	} else {
		unsigned nbytes = strtoul(kind_str, NULL, 16);
		bp_wp_add(gip->bp_session, type, addr, nbytes, 0, 0);
	}
	send_packet_string(gip, "OK");
	return;
error:
	send_packet_string(gip, "E00");
}

static void remove_breakpoint(struct gdb_interface_private *gip, char *args) {
	char *type_str = strsep(&args, ",");
	if (!type_str || !args)
		goto error;
	int type = *type_str - '0';
	if (type < 0 || type > 4)
		goto error;
	char *addr_str = strsep(&args, ",");
	if (!addr_str || !args)
		goto error;
	char *kind_str = strsep(&args, ";");
	if (!kind_str)
		goto error;
	unsigned addr = strtoul(addr_str, NULL, 16);
	if (type <= 1) {
		bp_hbreak_remove(gip->bp_session, addr, 0, 0);
	} else {
		unsigned nbytes = strtoul(kind_str, NULL, 16);
		bp_wp_remove(gip->bp_session, type, addr, nbytes, 0, 0);
	}
	send_packet_string(gip, "OK");
	return;
error:
	send_packet_string(gip, "E00");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// qSupported

static void send_supported(struct gdb_interface_private *gip, char *args) {
	(void)args;  // args ignored at the moment
	snprintf(packet, sizeof(packet), "PacketSize=%zx", sizeof(packet)-1);
	send_packet_string(gip, packet);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int hexdigit(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return 10 + c - 'a';
	if (c >= 'A' && c <= 'F')
		return 10 + c - 'A';
	return -1;
}

static int hex8(char *s) {
	int n1 = hexdigit(s[0]);
	int n0 = hexdigit(s[1]);
	if (n0 < 0 || n1 < 0)
		return -1;
	return (n1 << 4) | n0;
}

static int hex16(char *s) {
	int b1 = hex8(s);
	int b0 = hex8(s+2);
	if (b0 < 0 || b1 < 0)
		return -1;
	return (b1 << 8) | b0;
}
