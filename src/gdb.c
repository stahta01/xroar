/** \file
 *
 *  \brief GDB protocol support.
 *
 *  \copyright Copyright 2013-2026 Ciaran Anscomb
 *
 *  \copyright Copyright 2021 Tormod Volden
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
 * Accessible registers are defined per-machine.

 * 'g' packet responses will contain hex pairs comprising all registers the
 * machine interface exposes.
 *
 * 'm' and 'M' packets will read or write translated memory addresses (as seen
 * by the CPU).
 *
 * Breakpoints and watchpoints are supported ('Z' and 'z').
 *
 * Some standard, and some vendor-specific general queries are supported:

 *      qSupported      | XX... | report PacketSize
 *      qAttached       | 1     | always report attached

 */

#include "top-config.h"

// for addrinfo, struct timeval
#define _POSIX_C_SOURCE 200112L
// For strsep
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
#include "events.h"
#include "gdb.h"
#include "logging.h"
#include "machine.h"
#include "mc6809/hd6309.h"
#include "mc6809/mc6809.h"
#include "mc6883.h"
#include "xroar.h"

struct gdb_interface_private {
	struct machine *machine;

	// Thread info
	int listenfd;
	struct addrinfo *info;
	pthread_t sock_thread;
	int sockfd;

	// Session state
	bool no_ack_mode;

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

static uint8_t in_packet[1025];
static uint8_t packet[1025];

static ssize_t read_packet(struct gdb_interface_private *gip, void *buf, size_t count);
static ssize_t send_packet(struct gdb_interface_private *gip, const void *buf, size_t count);
static ssize_t send_packet_string(struct gdb_interface_private *gip, const char *string);
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

static int qRcmd(struct gdb_interface_private *gip, char *args);
static void qXfer(struct gdb_interface_private *gip,
		  const char *src, size_t src_length,
		  size_t offset, size_t length);
static void send_supported(struct gdb_interface_private *gip, char *args);  // qSupported

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int set_register_hex(struct gdb_interface_private *gip, int regno,
			    size_t ssize, char *src);
static int get_register_hex(struct gdb_interface_private *gip, int regno,
			    size_t dsize, char *dest);
static int hexdigit(char c);
static int hex8(char *s);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct gdb_interface *gdb_interface_new(const char *hostname, const char *portname, struct machine *m) {
	if (!m)
		return NULL;

	struct gdb_interface_private *gip = xmalloc(sizeof(*gip));
	*gip = (struct gdb_interface_private){0};

	gip->machine = m;
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
		LOG_MOD_WARN("gdb", "getaddrinfo %s:%s failed\n", hostname, portname);
		goto failed;
	}
	if (!gip->info) {
		LOG_MOD_WARN("gdb", "failed lookup %s:%s\n", hostname, portname);
		goto failed;
	}

	// Create a socket...
	gip->listenfd = socket(gip->info->ai_family, gip->info->ai_socktype, gip->info->ai_protocol);
	if (gip->listenfd < 0) {
		LOG_MOD_WARN("gdb", "socket not created\n");
		goto failed;
	}

	// bind
	if (bind(gip->listenfd, gip->info->ai_addr, gip->info->ai_addrlen) < 0) {
		LOG_MOD_WARN("gdb", "bind %s:%s failed\n", hostname, portname);
		goto failed;
	}

	// ... and listen
	if (listen(gip->listenfd, 1) < 0) {
		LOG_MOD_WARN("gdb", "failed to listen to socket\n");
		goto failed;
	}

	pthread_mutex_init(&gip->run_state_mt, NULL);
	pthread_cond_init(&gip->run_state_cv, NULL);
	pthread_create(&gip->sock_thread, NULL, handle_tcp_sock, gip);

	LOG_MOD_DEBUG(1, "gdb", "target listening on %s:%s\n", hostname, portname);

	return (struct gdb_interface *)gip;

failed:
	if (gip->listenfd != -1) {
		close(gip->listenfd);
	}
	if (gip->info) {
		freeaddrinfo(gip->info);
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

static void gdb_handle_signal(struct gdb_interface_private *gip, int sig, bool ack) {
	gip->last_signal = sig;
	if (ack)
		send_last_signal(gip);
}

void gdb_stop(struct gdb_interface *gi, int sig) {
	struct gdb_interface_private *gip = (struct gdb_interface_private *)gi;
	gip->run_state = gdb_run_state_stopped;
	gdb_handle_signal(gip, sig, 1);
}

void gdb_single_step(struct gdb_interface *gi) {
	struct gdb_interface_private *gip = (struct gdb_interface_private *)gi;
	gip->run_state = gdb_run_state_stopped;
	gdb_handle_signal(gip, MACHINE_SIGTRAP, 1);
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

static void gdb_machine_signal(struct gdb_interface_private *gip, int sig, bool ack) {
	pthread_mutex_lock(&gip->run_state_mt);
	if (gip->run_state == gdb_run_state_running) {
		gip->machine->signal(gip->machine, sig);
		gip->run_state = gdb_run_state_stopped;
		gdb_handle_signal(gip, sig, ack);
	}
	pthread_mutex_unlock(&gip->run_state_mt);
}

static void gdb_continue(struct gdb_interface_private *gip) {
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
			LOG_MOD_WARN("gdb", "accept() failed\n");
			continue;
		}
		{
			int flag = 1;
			setsockopt(gip->sockfd, IPPROTO_TCP, TCP_NODELAY, (void const *)&flag, sizeof(flag));
		}
		LOG_MOD_DEBUG_GDB(LOG_GDB_CONNECT, "gdb", "connection accepted\n");

		gip->no_ack_mode = 0;

		gdb_machine_signal(gip, MACHINE_SIGINT, 0);
		bool attached = 1;
		while (attached) {
			ssize_t l = read_packet(gip, in_packet, sizeof(in_packet));
			if (l == -GDBE_BREAK) {
				LOG_MOD_DEBUG_GDB(LOG_GDB_PACKET, "gdb", "BREAK\n");
				gdb_machine_signal(gip, MACHINE_SIGINT, 1);
				continue;
			} else if (l == -GDBE_BAD_CHECKSUM) {
				if (!gip->no_ack_mode) {
					if (send_char(gip, '-') < 0)
						break;
					continue;
				}
			} else if (l < 0) {
				break;
			}
			if (logging.debug_gdb & LOG_GDB_PACKET) {
				if (gip->run_state == gdb_run_state_stopped) {
					LOG_MOD_PRINT("gdb", "packet received: ");
				} else {
					LOG_MOD_PRINT("gdb", "packet ignored (send ^C first): ");
				}
				for (ssize_t i = 0; i < l; ++i) {
					if (isprint(in_packet[i])) {
						LOG_PRINT("%c", in_packet[i]);
					} else {
						LOG_PRINT("\\%o", in_packet[i] & 0xff);
					}
				}
				LOG_PRINT("\n");
			}
			if (gip->run_state != gdb_run_state_stopped) {
				if (!gip->no_ack_mode && send_char(gip, '-') < 0)
					break;
				continue;
			}
			if (!gip->no_ack_mode && send_char(gip, '+') < 0)
				break;

			uint8_t *args = &in_packet[1];

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
				set_general_registers(gip, (char *)args);
				break;

			case 'm':
				send_memory(gip, (char *)args);
				break;

			case 'M':
				set_memory(gip, (char *)args);
				break;

			case 'p':
				send_register(gip, (char *)args);
				break;

			case 'P':
				set_register(gip, (char *)args);
				break;

			case 'q':
				general_query(gip, (char *)args);
				break;

			case 'Q':
				general_set(gip, (char *)args);
				break;

			case 's':
				gdb_machine_single_step(gip);
				break;

			case 'z':
				remove_breakpoint(gip, (char *)args);
				break;

			case 'Z':
				add_breakpoint(gip, (char *)args);
				break;

			default:
				send_packet(gip, NULL, 0);
				break;
			}
		}
		close(gip->sockfd);
		gdb_continue(gip);
		LOG_MOD_DEBUG_GDB(LOG_GDB_CONNECT, "gdb", "connection closed\n");
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

static ssize_t read_packet(struct gdb_interface_private *gip, void *buf, size_t count) {
	char *cbuf = buf;
	enum packet_state state = packet_wait;
	size_t length = 0;
	uint8_t packet_sum = 0;
	uint8_t csum = 0;

	// Apply Linux read() limit
	if (count > 0x7ffff000) {
		count = 0x7ffff000;
	}

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

		char in_byte;
		ssize_t r = recv(gip->sockfd, &in_byte, 1, 0);
		if (r < 0) {
			return -GDBE_READ_ERROR;
		}
		if (r == 0) {
			continue;
		}

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
					cbuf[length++] = in_byte;
					packet_sum += (uint8_t)in_byte;
				}
			}
			break;
		case packet_csum0:
			{
				int tmp = hexdigit(in_byte);
				if (tmp < 0) {
					state = packet_wait;
				} else {
					csum = tmp << 4;
					state = packet_csum1;
				}
			}
			break;
		case packet_csum1:
			{
				int tmp = hexdigit(in_byte);
				if (tmp < 0) {
					state = packet_wait;
					break;
				}
				csum |= tmp;
			}
			if (csum != packet_sum) {
				if (logging.debug_gdb & LOG_GDB_CHECKSUM) {
					LOG_MOD_PRINT("gdb", "bad checksum in '");
					if (isprint(cbuf[0]))
						LOG_PRINT("%c", cbuf[0]);
					else
						LOG_PRINT("0x%02x", cbuf[0]);
					LOG_PRINT("' packet.  Expected 0x%02x, got 0x%02x.\n",
						  packet_sum, csum);
				}
				return -GDBE_BAD_CHECKSUM;
			}
			cbuf[length] = 0;
			return length;
		}

	}

	return -GDBE_READ_ERROR;
}

// Send a standard response packet.  Format is '$' followed by escaped data,
// followed by '#' and a 2-byte hex checksum.

static ssize_t send_packet(struct gdb_interface_private *gip, const void *buf, size_t count) {
	const char *cbuf = buf;
	char tmpbuf[16];
	uint8_t csum = 0;

	// Apply Linux write() limit
	if (count > 0x7ffff000) {
		count = 0x7ffff000;
	}

	tmpbuf[0] = '$';
	if (send(gip->sockfd, tmpbuf, 1, 0) < 0) {
		return -GDBE_WRITE_ERROR;
	}

	size_t j = 0;
	for (size_t i = 0; i < count; ++i) {
		csum += cbuf[i];
		switch (cbuf[i]) {
		case '#':
		case '$':
		case 0x7d:
		case '*':
			tmpbuf[j++] = 0x7d;
			tmpbuf[j++] = cbuf[i] ^ 0x20;
			break;
		default:
			tmpbuf[j++] = cbuf[i];
			break;
		}
		if (i == (count - 1) || j >= (sizeof(tmpbuf) - 1)) {
			if (send(gip->sockfd, tmpbuf, j, 0) < 0) {
				return -GDBE_WRITE_ERROR;
			}
			j = 0;
		}
	}
	snprintf(tmpbuf, sizeof(tmpbuf), "#%02x", (unsigned)csum);
	if (send(gip->sockfd, tmpbuf, 3, 0) < 0) {
		return -GDBE_WRITE_ERROR;
	}
	// the reply ("+" or "-") will be discarded by the next read_packet

	if (logging.debug_gdb & LOG_GDB_PACKET) {
		LOG_MOD_PRINT("gdb", "packet sent: ");
		for (size_t i = 0; i < count; ++i) {
			if (isprint(cbuf[i])) {
				LOG_PRINT("%c", cbuf[i]);
			} else {
				LOG_PRINT("\\%o", cbuf[i] & 0xff);
			}
		}
		LOG_PRINT("\n");
	}

	return count;
}

// Wrapper sends a NUL-terminated string as a response packet.

static ssize_t send_packet_string(struct gdb_interface_private *gip, const char *string) {
	return send_packet(gip, string, strlen(string));
}

// Generates a hex-encoded response packet and sends it.  This is only
// currently used for the weird format of qRcmd responses.

static ssize_t send_packet_hexstring(struct gdb_interface_private *gip, const char *string) {
	size_t count = strlen(string);

	// Apply Linux write() limit
	if (count > 0X3ffff800) {
		count = 0X3ffff800;
	}

	char *hs = xmalloc((count * 2) + 1);
	char *hsp = hs;
	for (size_t i = 0; i < count; ++i) {
		hsp += snprintf(hsp, 3, "%02x", (uint8_t)string[i]);
	}
	ssize_t ret = send_packet(gip, hs, count * 2);
	free(hs);
	return ret;
}

static int send_char(struct gdb_interface_private *gip, char c) {
	if (send(gip->sockfd, &c, 1, 0) < 0) {
		return -GDBE_WRITE_ERROR;
	}
	return GDBE_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void send_last_signal(struct gdb_interface_private *gip) {
	char tmpbuf[4];
	snprintf(tmpbuf, sizeof(tmpbuf), "S%02x", gip->last_signal);
	send_packet(gip, tmpbuf, 3);
}

static void send_general_registers(struct gdb_interface_private *gip) {
	// Fetch all registers into buffer
	char *dest = (char *)packet;
	unsigned dsize = (sizeof(packet) - 1) / 2;
	for (unsigned i = 0; i < gip->machine->debug.target->nregs; ++i) {
		int n = get_register_hex(gip, i, dsize, dest);
		if (n < 0)
			break;
		dest += n;
		dsize -= n;
	}
	send_packet_string(gip, (char *)packet);
}

static void set_general_registers(struct gdb_interface_private *gip, char *args) {
	size_t asize = strlen(args);
	for (unsigned i = 0; i < gip->machine->debug.target->nregs; ++i) {
		int n = set_register_hex(gip, i, asize, args);
		if (n < 0)
			break;
		args += n;
	}
	send_packet_string(gip, "OK");
}

// Response handler for 'm' (read memory)

static void send_memory(struct gdb_interface_private *gip, char *args) {
	char *addr = strsep(&args, ",");
	if (!args || !addr) {
		send_packet(gip, NULL, 0);
		return;
	}
	uint16_t A = strtoul(addr, NULL, 16);
	unsigned length = strtoul(args, NULL, 16);
	uint8_t csum = 0;
	packet[0] = '$';
	if (send(gip->sockfd, packet, 1, 0) < 0)
		return;
	for (unsigned i = 0; i < length; i++) {
		uint8_t b = gip->machine->read_byte(gip->machine, A++, 0);
		snprintf((char *)packet, sizeof(packet), "%02x", b);
		csum += packet[0];
		csum += packet[1];
		if (send(gip->sockfd, packet, 2, 0) < 0)
			return;
	}
	snprintf((char *)packet, sizeof(packet), "#%02x", csum);
	if (send(gip->sockfd, packet, 3, 0) < 0)
		return;
	// the ACK ("+") or NAK ("-") will be discarded by the next read_packet
	LOG_MOD_DEBUG_GDB(LOG_GDB_PACKET, "gdb", "packet sent (binary): %u bytes\n", length);
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
	unsigned regno = strtoul(args, NULL, 16);
	int n = get_register_hex(gip, regno, sizeof(packet), (char *)packet);
	if (n <= 0) {
		send_packet_string(gip, "E00");
	} else {
		send_packet_string(gip, (char *)packet);
	}
}

static void set_register(struct gdb_interface_private *gip, char *args) {
	char *regno_str = strsep(&args, "=");
	if (!regno_str || !args)
		goto error;
	unsigned regno = strtoul(regno_str, NULL, 16);
	if (regno >= gip->machine->debug.target->nregs)
		goto error;
	size_t asize = strlen(args);
	if (set_register_hex(gip, regno, asize, args) < 0)
		goto error;
	send_packet_string(gip, "OK");
	return;
error:
	send_packet_string(gip, "E00");
}

// General query handler ('q' packets)

static void general_query(struct gdb_interface_private *gip, char *args) {
	if (0 == strncmp(args, "Rcmd", 4)) {
		// This query uses comma instead of colon as separator.
		// Copying args to argptr works around the GCC -fanalyzer false
		// positive -Wanalyzer-deref-before-check.
		char *argptr = args;
		strsep(&argptr, ",");
		if (!argptr || qRcmd(gip, argptr)) {
			send_packet_string(gip, "E00");
		}
		return;
	}
	char *query = strsep(&args, ":");
	if (0 == strncmp(query, "Supported", 9) && (query[9] == 0 || query[9] == ':')) {
		LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: Supported\n");
		send_supported(gip, args);
	} else if (0 == strcmp(query, "Attached")) {
		LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: Attached\n");
		send_packet_string(gip, "1");
	} else if (0 == strcmp(query, "Xfer")) {
		if (0 == strncmp(args, "features:read:target.xml:", 25)) {
			args += 25;
			const char *data = gip->machine->debug.target_xml;
			size_t data_size = sdslen(gip->machine->debug.target_xml);
			size_t offset = strtol(args, &args, 16);
			if (*args == ',')
				++args;
			size_t length = strtol(args, &args, 16);
			qXfer(gip, data, data_size, offset, length);
			return;
		} else {
			LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: unknown qXfer: %s\n", args);
			send_packet(gip, NULL, 0);
		}
	} else {
		LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: unknown query\n");
		send_packet(gip, NULL, 0);
	}
}

// General set handler ('Q' packets)

static void general_set(struct gdb_interface_private *gip, char *args) {
	char *set = strsep(&args, ":");
	if (0 == strcmp(set, "StartNoAckMode")) {
		gip->no_ack_mode = 1;
		send_packet_string(gip, "OK");
		return;
	}
	send_packet(gip, NULL, 0);
	return;
}

// Add breakpoint ('Z' packet)

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
		machine_add_hbreak(gip->machine, addr);
	} else {
		unsigned nbytes = strtoul(kind_str, NULL, 16);
		if (type == 2 || type == 4) {
			machine_add_hwatch(gip->machine, 0, addr, addr+nbytes-1);
		}
		if (type == 3 || type == 4) {
			machine_add_hwatch(gip->machine, 1, addr, addr+nbytes-1);
		}
	}
	send_packet_string(gip, "OK");
	return;
error:
	send_packet_string(gip, "E00");
}

// Remove breakpoint ('z' packet)

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
		machine_remove_hbreak(gip->machine, addr);
	} else {
		unsigned nbytes = strtoul(kind_str, NULL, 16);
		if (type == 2 || type == 4) {
			machine_remove_hwatch(gip->machine, 0, addr, addr+nbytes-1);
		}
		if (type == 3 || type == 4) {
			machine_remove_hwatch(gip->machine, 1, addr, addr+nbytes-1);
		}
	}
	send_packet_string(gip, "OK");
	return;
error:
	send_packet_string(gip, "E00");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// General query response handlers

// qSupported

static void send_supported(struct gdb_interface_private *gip, char *args) {
	(void)args;  // args ignored at the moment
	snprintf((char *)packet, sizeof(packet), "PacketSize=%zx;QStartNoAckMode+;qXfer:features:read+", sizeof(packet)-1);
	send_packet_string(gip, (char *)packet);
}

// Response handler for 'qRcmd' (execute command).
//
// We support the following commands:
//
// monitor cycles       - report number of elapsed cycles
// monitor trace n      - turn trace mode on (non-zero) or off (zero)

static int qRcmd(struct gdb_interface_private *gip, char *args) {
	if (!*args) {
		/* no words received, print usage */
		send_packet_hexstring(gip, "monitor cycles [STRING]\n"
					   "monitor trace VALUE\n");
		return 0;
	}

	/* decode hex string in place */
	char *p;
	char *np;
	for (p = np = args; *p; p += 2) {
		if (!p[1])
			return 1; /* odd number of hex digits */
		int v = hex8(p);
		if (v < 0)
			return 1;
		*np++ = v;
	}
	*np = '\0';

	// Parse our own gdb "monitor" command.  If no args found, use an empty
	// string for printing later.  Note: The temporary assignment to argptr
	// here works around the GCC -fanalyzer false positive
	// -Wanalyzer-deref-before-check.
	char *argptr = args;
	char *cmd = strsep(&argptr, " ");
	args = argptr ? argptr : "";

	char reply[255];
	*reply = '\0';
	if (0 == strcmp(cmd, "cycles")) {
		sprintf(reply, "%zu cycles %s\n", (size_t) event_current_tick, args);
	} else if (0 == strcmp(cmd, "trace")) {
		logging.trace_cpu = atoi(args);
	} else {
		sprintf(reply, "unknown monitor command\n");
	}

	if (*reply)
		send_packet_hexstring(gip, reply);
	else
		send_packet_string(gip, "OK");
	return 0;
}

// Response handler for 'qXfer' (general query to read target special data bytes)

static void qXfer(struct gdb_interface_private *gip,
		  const char *src, size_t src_length,
		  size_t offset, size_t length) {
	if (offset >= src_length) {
		offset = src_length;
		length = 0;
	} else if ((offset + length) > src_length) {
		length = src_length - offset;
	}
	if (length > (sizeof(packet) - 6)) {
		length = sizeof(packet) - 6;
	}
	if ((offset + length) >= src_length) {
		packet[0] = 'l';
	} else {
		packet[0] = 'm';
	}
	memcpy(packet + 1, src + offset, length);
	send_packet(gip, packet, length + 1);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Convert hex representation of a register to binary in-place and, if valid,
// set the appropriate register.  Returns 0 if invalid hex, -1 if not enough
// hex digits or invalid register.
static int set_register_hex(struct gdb_interface_private *gip, int regno,
			    size_t ssize, char *src) {
	if (regno < 0 || (unsigned)regno >= gip->machine->debug.target->nregs)
		return -1;
	unsigned rsize = gip->machine->debug.target->reg_size[regno];
	assert(rsize > 0);
	unsigned hsize = rsize * 2;
	if (hsize > ssize)
		return -1;
	uint8_t *rsrc = (uint8_t *)src;
	for (unsigned i = 0; i < rsize; ++i) {
		int v = hex8(src);
		if (v < 0)
			return 0;
		src += 2;
		rsrc[i] = v;
	}
	int n = debug_set_register_composite(gip->machine->debug.target, regno, rsize, rsrc);
	assert((unsigned)n == rsize);
	return hsize;
}

// Get requested register and convert it to a hex representation.  Returns -1
// if not enough space for hex digits or invalid register.  Populates with "xx"
// if there is a problem fetching the register.
static int get_register_hex(struct gdb_interface_private *gip, int regno,
			    size_t dsize, char *dest) {
	if (regno < 0 || (unsigned)regno >= gip->machine->debug.target->nregs)
		return -1;
	unsigned rsize = gip->machine->debug.target->reg_size[regno];
	assert(rsize > 0);
	unsigned hsize = rsize * 2;
	if ((hsize + 1) > dsize)
		return -1;
	// Fetch binary representation to end of area we will write into.
	uint8_t *rdest = (uint8_t *)dest + hsize + 1 - rsize;
	int n = debug_get_register_composite(gip->machine->debug.target, regno, rsize, rdest);
	if (n < 0 || (unsigned)n != rsize) {
		for (unsigned i = 0; i < hsize; ++i) {
			rdest[i] = 'x';
		}
		return hsize;
	}
	for (unsigned i = 0; i < rsize; ++i) {
		int n = snprintf(dest, dsize, "%02x", rdest[i]);
		if (n != 2)
			return -1;
		dest += n;
		dsize -= n;
	}
	return hsize;
}

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
