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
 * Accessible registers are now defined in the debug_cpu interface.

 * 'g' packet responses will contain hex pairs comprising all registers the
 * debug_cpu interface exposes.
 *
 * 'm' and 'M' packets will read or write translated memory addresses (as seen
 * by the CPU).
 *
 * Breakpoints and watchpoints are supported ('Z' and 'z').
 *
 * Some standard, and some vendor-specific general queries are supported:

 *      qxroar.sam      | XXXX  | get SAM register, reply is 4 hex digits
 *      qSupported      | XX... | report PacketSize
 *      qAttached       | 1     | always report attached

 * Only these vendor-specific general sets are supported:

 *      Qxroar.sam:XXXX       | set SAM register (4 hex digits)

 * TODO: machine-specific handling like SAM registers needs to be devolved to a
 * machine interface, then we can add GIME register querying too.

 */

#include "top-config.h"

// for addrinfo, struct timeval
#define _POSIX_C_SOURCE 200112L
// For strsep
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

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

	struct MC6809 *cpu;
	struct debug_cpu *dcpu;
	struct MC6883 *sam;
	_Bool is_6309;

	// Thread info
	int listenfd;
	struct addrinfo *info;
	pthread_t sock_thread;
	int sockfd;

	// Session state
	_Bool no_ack_mode;

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

const char target_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
"<target>"
  "<architecture>m6809</architecture>"
  "<xi:include href=\"m6809-core.xml\"/>"
"</target>";

const char target_6309_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
"<target>"
  "<architecture>h6309</architecture>"
  "<xi:include href=\"m6809-core.xml\"/>"
  "<xi:include href=\"m6809-h6309.xml\"/>"
"</target>";

const char m6809_core_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
"<feature name=\"org.gnu.gdb.m6809.core\">"
  "<flags id=\"cc_flags\" size=\"1\">"
    "<field name=\"C\" start=\"0\" end=\"0\"/>"
    "<field name=\"V\" start=\"1\" end=\"1\"/>"
    "<field name=\"Z\" start=\"2\" end=\"2\"/>"
    "<field name=\"N\" start=\"3\" end=\"3\"/>"
    "<field name=\"I\" start=\"4\" end=\"4\"/>"
    "<field name=\"H\" start=\"5\" end=\"5\"/>"
    "<field name=\"F\" start=\"6\" end=\"6\"/>"
    "<field name=\"E\" start=\"7\" end=\"7\"/>"
  "</flags>"
  "<reg name=\"cc\" bitsize=\"8\" type=\"cc_flags\" regnum=\"0\"/>"
  "<reg name=\"a\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"b\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"dp\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"x\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"y\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"u\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"s\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"pc\" bitsize=\"16\" type=\"code_ptr\"/>"
"</feature>";

const char m6809_h6309_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
"<feature name=\"org.gnu.gdb.m6809.h6309\">"
  "<flags id=\"md_flags\" size=\"1\">"
    "<field name=\"NM\" start=\"0\" end=\"0\"/>"
    "<field name=\"FM\" start=\"1\" end=\"1\"/>"
    "<field name=\"IL\" start=\"6\" end=\"6\"/>"
    "<field name=\"D0\" start=\"7\" end=\"7\"/>"
  "</flags>"
  "<reg name=\"md\" bitsize=\"8\" type=\"md_flags\" regnum=\"9\"/>"
  "<reg name=\"e\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"f\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"v\" bitsize=\"16\" type=\"uint16\"/>"
"</feature>";

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int snprint_value_endian(char *dst, size_t dsize, int endian,
				uint32_t value, unsigned vsize);
static int scan_value_endian(char *src, size_t ssize, int endian,
			     uint32_t *value, unsigned vsize);
static int hexdigit(char c);
static int hex8(char *s);
static int hex16(char *s);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct gdb_interface *gdb_interface_new(const char *hostname, const char *portname, struct machine *m) {
	if (!m)
		return NULL;

	struct MC6809 *cpu = (struct MC6809 *)part_component_by_id_is_a(&m->part, "CPU", "MC6809");
	if (!cpu) {
		LOG_MOD_WARN("gdb", "MC6809 CPU not found - not enabling GDB support\n");
		return NULL;
	}
	struct debug_cpu *dcpu = (struct debug_cpu *)part_component_by_id_is_a(&m->part, "CPU", "DEBUG-CPU");

	struct MC6883 *sam = (struct MC6883 *)part_component_by_id_is_a(&m->part, "SAM", "SN74LS783");

	struct gdb_interface_private *gip = xmalloc(sizeof(*gip));
	*gip = (struct gdb_interface_private){0};

	gip->machine = m;
	gip->cpu = cpu;
	gip->dcpu = dcpu;
	gip->sam = sam;
	gip->run_state = gdb_run_state_running;

	gip->is_6309 = (strcmp(((struct part *)cpu)->partdb->name, "HD6309") == 0);

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

static void gdb_handle_signal(struct gdb_interface_private *gip, int sig, _Bool ack) {
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

static void gdb_machine_signal(struct gdb_interface_private *gip, int sig, _Bool ack) {
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

		gdb_machine_signal(gip, MACHINE_SIGINT, 0);
		_Bool attached = 1;
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
	char *dst = packet;
	size_t dsize = sizeof(packet);
	for (unsigned i = 0; i < gip->dcpu->num_registers; ++i) {
		unsigned rsize = DELEGATE_CALL(gip->dcpu->register_size, i);
		uint32_t rval = DELEGATE_CALL(gip->dcpu->get_register, i);
		int n = snprint_value_endian(dst, dsize, gip->dcpu->endian, rval, rsize);
		dst += n;
		dsize -= n;
	}
	send_packet_string(gip, packet);
}

static void set_general_registers(struct gdb_interface_private *gip, char *args) {
	size_t asize = strlen(args);
	for (unsigned i = 0; i < gip->dcpu->num_registers; ++i) {
		unsigned rsize = DELEGATE_CALL(gip->dcpu->register_size, i);
		uint32_t rval = 0;
		int n = scan_value_endian(args, asize, gip->dcpu->endian, &rval, rsize);
		if (n == 0)
			break;
		args += n;
		asize -= n;
		DELEGATE_CALL(gip->dcpu->set_register, i, rval);
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
	unsigned regnum = strtoul(args, NULL, 16);
	if (regnum >= gip->dcpu->num_registers) {
		send_packet_string(gip, "E00");
	} else {
		unsigned rsize = DELEGATE_CALL(gip->dcpu->register_size, regnum);
		uint32_t rval = DELEGATE_CALL(gip->dcpu->get_register, regnum);
		snprint_value_endian(packet, sizeof(packet), gip->dcpu->endian, rval, rsize);
		send_packet_string(gip, packet);
	}
}

static void set_register(struct gdb_interface_private *gip, char *args) {
	char *regnum_str = strsep(&args, "=");
	if (!regnum_str || !args)
		goto error;
	unsigned regnum = strtoul(regnum_str, NULL, 16);
	if (regnum >= gip->dcpu->num_registers)
		goto error;
	unsigned rsize = DELEGATE_CALL(gip->dcpu->register_size, regnum);
	uint32_t rval = 0;
	int n = scan_value_endian(args, strlen(args), gip->dcpu->endian, &rval, rsize);
	if (n == 0)
		goto error;
	DELEGATE_CALL(gip->dcpu->set_register, regnum, rval);
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
	if (0 == strncmp(query, "xroar.", 6)) {
		query += 6;
#ifdef WANT_MACHINE_ARCH_DRAGON
		if (0 == strcmp(query, "sam") && gip->sam) {
			LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: xroar.sam\n");
			sprintf(packet, "%04x", gip->sam->get_register(gip->sam));
			send_packet(gip, packet, 4);
			return;
		}
#endif
		LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: unknown xroar vendor query\n");
		send_packet(gip, NULL, 0);
	} else if (0 == strncmp(query, "Supported", 9) && (query[9] == 0 || query[9] == ':')) {
		LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: Supported\n");
		send_supported(gip, args);
	} else if (0 == strcmp(query, "Attached")) {
		LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: Attached\n");
		send_packet_string(gip, "1");
	} else if (0 == strcmp(query, "Xfer")) {
		if (0 == strncmp(args, "features:read:", 14)) {
			args += 14;
			const char *src = NULL;
			size_t src_length = 0;
			if (0 == strncmp(args, "target.xml:", 11)) {
				args += 11;
				if (gip->is_6309) {
					src = target_6309_xml;
					src_length = sizeof(target_6309_xml) - 1;  // omit NUL
				} else {
					src = target_xml;
					src_length = sizeof(target_xml) - 1;  // omit NUL
				}
			} else if (0 == strncmp(args, "m6809-core.xml:", 15)) {
				args += 15;
				src = m6809_core_xml;
				src_length = sizeof(m6809_core_xml) - 1;  // omit NUL
			} else if (0 == strncmp(args, "m6809-h6309.xml:", 16)) {
				args += 16;
				src = m6809_h6309_xml;
				src_length = sizeof(m6809_h6309_xml) - 1;  // omit NUL
			} else {
				LOG_MOD_DEBUG_GDB(LOG_GDB_QUERY, "gdb", "query: unknown qXfer features read: %s\n", args);
				send_packet(gip, NULL, 0);
				return;
			}
			size_t offset = strtol(args, &args, 16);
			if (*args == ',')
				++args;
			size_t length = strtol(args, &args, 16);
			qXfer(gip, src, src_length, offset, length);
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
	if (0 == strncmp(set, "xroar.", 6)) {
		set += 6;
#ifdef WANT_MACHINE_ARCH_DRAGON
		if (0 == strcmp(set, "sam") && gip->sam) {
			gip->sam->set_register(gip->sam, hex16(args));
			send_packet_string(gip, "OK");
			return;
		}
#endif
	} else if (0 == strcmp(set, "StartNoAckMode")) {
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
	snprintf(packet, sizeof(packet), "PacketSize=%zx;qXfer:features:read+", sizeof(packet)-1);
	send_packet_string(gip, packet);
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

static int snprint_value_endian(char *dst, size_t dsize, int endian,
				uint32_t value, unsigned vsize) {
	if (vsize > 4)
		return 0;
	int nbytes = 0;
	for (unsigned i = 0; i < vsize; ++i) {
		if (dsize < 3)
			return nbytes;
		int shift;
		switch (endian) {
		case DCPU_ENDIAN_BIG: default: shift = (vsize - i - 1) * 8; break;
		case DCPU_ENDIAN_LITTLE: shift = i * 8;
		}
		int v = (value >> shift) & 0xff;
		int n = snprintf(dst, dsize, "%02x", v);
		dst += n;
		dsize -= n;
		nbytes += n;
	}
	return nbytes;
}

static int scan_value_endian(char *src, size_t ssize, int endian,
			     uint32_t *value, unsigned vsize) {
	if (vsize > 4)
		return 0;
	int nbytes = vsize * 2;
	if (ssize < (size_t)nbytes)
		return 0;
	_Bool valid = 1;
	uint32_t rval = 0;
	for (unsigned i = 0; i < vsize; ++i) {
		int tmp = hex8(src);
		if (tmp < 0) {
			valid = 0;
		} else {
			int shift;
			switch (endian) {
			case DCPU_ENDIAN_BIG: default: shift = (vsize - i - 1) * 8; break;
			case DCPU_ENDIAN_LITTLE: shift = i * 8;
			}
			rval |= (uint32_t)(tmp & 0xff) << shift;
		}
		src += 2;
		ssize -= 2;
	}
	if (valid && value) {
		*value = rval;
	}
	return nbytes;
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

static int hex16(char *s) {
	int b1 = hex8(s);
	int b0 = hex8(s+2);
	if (b0 < 0 || b1 < 0)
		return -1;
	return (b1 << 8) | b0;
}
