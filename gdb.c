/*
 * Copyright 2003-2013 Ciaran Anscomb
 *
 * This file is part of XRoar.
 *
 * XRoar is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * XRoar is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Support a subset of the gdb protocol over a socket.  See
 * http://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html
 *
 * The following registers are accessible:

 *      Index   Name    Bits
 *
 *      0       CC      8
 *      1       A       8
 *      2       B       8
 *      3       DP      8
 *      4       X       16
 *      5       Y       16
 *      6       U       16
 *      7       S       16
 *      8       PC      16
 *      9       MD      8       (HD6309)
 *      10      E       8       (HD6309)
 *      11      F       8       (HD6309)
 *      12      V       16      (HD6309)

 * 'g' packet responses will contain 14 hex pairs comprising the 6809 register,
 * and either a further 5 hex pairs for the 6309 registers, or 'xx'.  'G'
 * packets must supply 19 values, either hex pairs or 'xx'.
 *
 * The machine is not currently stopped for debugging, and 'c' and 's' do not
 * yet function correctly.  'c' will however put the stub into a state where a
 * break character (0x03) is required.
 *
 * 'm' and 'M' packets will read or write translated memory addresses (as seen
 * by the CPU).
 *
 * Breakpoints and watchpoints are not yet supported (given that the machine is
 * not yet stopped), but the 'z' and 'Z' packets will elicit an "OK" response.
 *
 * Some standard, and some vendor-specific general queries are supported:

 *      qxroar.sam      XXXX    get SAM register, reply is 4 hex digits
 *      qSupported      XX...   report PacketSize
 *      qAttached       1       always report attached

 * Only these vendor-specific general sets are supported:

 *      Qxroar.sam:XXXX         set SAM register (4 hex digits)

 */

#define _POSIX_C_SOURCE 200112L
// For strsep()
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "portalib/glib.h"
#include "portalib/string.h"

#include "config.h"

#ifndef WINDOWS32

#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#else

/* Windows has a habit of making include order important: */
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#endif

#include "gdb.h"
#include "hd6309.h"
#include "logging.h"
#include "machine.h"
#include "mc6809.h"
#include "sam.h"
#include "xroar.h"

static int listenfd = -1;
static struct addrinfo *info;

static pthread_t sock_thread;
static void *handle_tcp_sock(void *data);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

enum gdb_error {
	GDBE_OK = 0,
	GDBE_BAD_CHECKSUM,
	GDBE_BREAK,
	GDBE_READ_ERROR,
	GDBE_WRITE_ERROR,
};

static char packet[1025];

static int read_packet(int fd, char *buffer, unsigned count);
static int send_packet(int fd, char *buffer, unsigned count);
static int send_packet_string(int fd, char *string);
static int send_char(int fd, char c);

static void send_general_registers(int fd);  // g
static void set_general_registers(int fd, char *args);  // G
static void send_memory(int fd, char *args);  // m
static void set_memory(int fd, char *args);  // M
static void send_register(int fd, char *args);  // p
static void set_register(int fd, char *args);  // P
static void general_query(int fd, char *args);  // q
static void general_set(int fd, char *args);  // Q

static void send_supported(int fd, char *args);  // qSupported

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int hexdigit(char c);
static int hex8(char *s);
static int hex16(char *s);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

int gdb_init(void) {

	struct addrinfo hints;
	const char *hostname = xroar_cfg.gdb_ip ? xroar_cfg.gdb_ip : "localhost";
	const char *portname = xroar_cfg.gdb_port ? xroar_cfg.gdb_port : "65520";

	// Find the interface
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	if (getaddrinfo(hostname, portname, &hints, &info) < 0) {
		LOG_WARN("gdb: getaddrinfo %s:%s failed\n", hostname, portname);
		goto failed;
	}

	// Create a socket...
	listenfd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
	if (listenfd < 0) {
		LOG_WARN("gdb: socket not created\n");
		goto failed;
	}

	// bind
	if (bind(listenfd, info->ai_addr, info->ai_addrlen) < 0) {
		LOG_WARN("gdb: bind %s:%s failed\n", hostname, portname);
		goto failed;
	}

	// ... and listen
	if (listen(listenfd, 1) < 0) {
		LOG_WARN("gdb: failed to listen to socket\n");
		goto failed;
	}

	pthread_create(&sock_thread, NULL, handle_tcp_sock, NULL);

	LOG_DEBUG(2, "gdb: stub listening on %s:%s\n", hostname, portname);

	return 0;

failed:
	if (listenfd != -1) {
		close(listenfd);
		listenfd = -1;
	}
	return -1;
}

void gdb_shutdown(void) {
	pthread_cancel(sock_thread);
	pthread_join(sock_thread, NULL);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void *handle_tcp_sock(void *data) {
	(void)data;

	for (;;) {
		int sockfd = accept(listenfd, info->ai_addr, &info->ai_addrlen);
		if (sockfd < 0) {
			LOG_WARN("gdb: accept() failed\n");
			continue;
		}
		if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_CONNECT) {
			LOG_PRINT("gdb: connection accepted\n");
		}
		machine_stop();
		_Bool attached = 1;
		while (attached) {
			int l = read_packet(sockfd, packet, sizeof(packet));
			if (l == -GDBE_BREAK) {
				if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_PACKET) {
					LOG_PRINT("gdb: BREAK\n");
				}
				if (machine_state == machine_state_running)
					machine_stop();
				send_packet_string(sockfd, "S02");
				continue;
			} else if (l == -GDBE_BAD_CHECKSUM) {
				if (send_char(sockfd, '-') < 0)
					break;
				continue;
			} else if (l < 0) {
				break;
			}
			if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_PACKET) {
				if (machine_state == machine_state_stopped) {
					LOG_PRINT("gdb: packet received: ");
				} else {
					LOG_PRINT("gdb: packet ignored (send ^C first): ");
				}
				for (unsigned i = 0; i < (unsigned)l; i++) {
					if (isprint(packet[i])) {
						LOG_PRINT("%c", packet[i]);
					} else {
						LOG_PRINT("\\%o", packet[i] & 0xff);
					}
				}
				LOG_PRINT("\n");
			}
			if (machine_state != machine_state_stopped) {
				if (send_char(sockfd, '-') < 0)
					break;
				continue;
			}
			if (send_char(sockfd, '+') < 0)
				break;

			char *args = &packet[1];

			switch (packet[0]) {

			case '?':
				// XXX
				send_packet_string(sockfd, "S00");
				break;

			case 'c':
				machine_start();
				break;

			case 'D':
				send_packet_string(sockfd, "OK");
				attached = 0;
				break;

			case 'g':
				send_general_registers(sockfd);
				break;

			case 'G':
				set_general_registers(sockfd, args);
				break;

			case 'm':
				send_memory(sockfd, args);
				break;

			case 'M':
				set_memory(sockfd, args);
				break;

			case 'p':
				send_register(sockfd, args);
				break;

			case 'P':
				set_register(sockfd, args);
				break;

			case 'q':
				general_query(sockfd, args);
				break;

			case 'Q':
				general_set(sockfd, args);
				break;

			case 's':
				machine_step();
				send_packet_string(sockfd, "S05");
				break;

			case 'z':
				// XXX
				send_packet_string(sockfd, "OK");
				break;

			case 'Z':
				// XXX
				send_packet_string(sockfd, "OK");
				break;

			default:
				send_packet(sockfd, NULL, 0);
				break;
			}
		}
		close(sockfd);
		machine_start();
		if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_CONNECT) {
			LOG_PRINT("gdb: connection closed\n");
		}
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

static int read_packet(int fd, char *buffer, unsigned count) {
	enum packet_state state = packet_wait;
	unsigned length = 0;
	uint8_t packet_sum = 0;
	uint8_t csum = 0;
	char in_byte;
	int tmp;
	while (recv(fd, &in_byte, 1, 0) > 0) {
		switch (state) {
		case packet_wait:
			if (in_byte == '$') {
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
				state = packet_read;
			} else {
				csum = tmp << 4;
				state = packet_csum1;
			}
			break;
		case packet_csum1:
			tmp = hexdigit(in_byte);
			if (tmp < 0) {
				state = packet_read;
				break;
			}
			csum |= tmp;
			if (csum != packet_sum) {
				if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_CHECKSUM) {
					LOG_PRINT("gdb: bad checksum in '");
					if (isprint(packet[0]))
						LOG_PRINT("%c", packet[0]);
					else
						LOG_PRINT("0x%02x", packet[0]);
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

static int send_packet(int fd, char *buffer, unsigned count) {
	char tmpbuf[4];
	uint8_t csum = 0;
	tmpbuf[0] = '$';
	if (send(fd, tmpbuf, 1, 0) < 0)
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
			if (send(fd, tmpbuf, 2, 0) < 0)
				return -GDBE_WRITE_ERROR;
			break;
		default:
			if (send(fd, &buffer[i], 1, 0) < 0)
				return -GDBE_WRITE_ERROR;
			break;
		}
	}
	snprintf(tmpbuf, sizeof(tmpbuf), "#%02x", csum);
	if (send(fd, tmpbuf, 3, 0) < 0)
		return -GDBE_WRITE_ERROR;
	if (recv(fd, tmpbuf, 1, 0) < 0)
		return -GDBE_READ_ERROR;
	if (tmpbuf[0] != '+')
		return -GDBE_WRITE_ERROR;

	if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_PACKET) {
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

static int send_packet_string(int fd, char *string) {
	unsigned count = strlen(string);
	return send_packet(fd, string, count);
}

static int send_char(int fd, char c) {
	if (send(fd, &c, 1, 0) < 0)
		return -GDBE_WRITE_ERROR;
	return GDBE_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void send_general_registers(int fd) {
	sprintf(packet, "%02x%02x%02x%02x%04x%04x%04x%04x%04x",
		 CPU0->reg_cc,
		 MC6809_REG_A(CPU0),
		 MC6809_REG_B(CPU0),
		 CPU0->reg_dp,
		 CPU0->reg_x,
		 CPU0->reg_y,
		 CPU0->reg_u,
		 CPU0->reg_s,
		 CPU0->reg_pc);
	if (xroar_machine_config->cpu == CPU_HD6309) {
		sprintf(packet + 28, "%02x%02x%02x%04x",
			 ((struct HD6309 *)CPU0)->reg_md,
			 HD6309_REG_E(((struct HD6309 *)CPU0)),
			 HD6309_REG_F(((struct HD6309 *)CPU0)),
			 ((struct HD6309 *)CPU0)->reg_v);
	} else {
		strcat(packet, "xxxxxxxxxx");
	}
	send_packet_string(fd, packet);
}

static void set_general_registers(int fd, char *args) {
	if (strlen(args) != 38) {
		send_packet_string(fd, "E00");
		return;
	}
	int tmp;
	if ((tmp = hex8(args)) >= 0)
		CPU0->reg_cc = tmp;
	if ((tmp = hex8(args+2)) >= 0)
		MC6809_REG_A(CPU0) = tmp;
	if ((tmp = hex8(args+4)) >= 0)
		MC6809_REG_B(CPU0) = tmp;
	if ((tmp = hex8(args+6)) >= 0)
		CPU0->reg_dp = tmp;
	if ((tmp = hex16(args+8)) >= 0)
		CPU0->reg_x = tmp;
	if ((tmp = hex16(args+12)) >= 0)
		CPU0->reg_y = tmp;
	if ((tmp = hex16(args+16)) >= 0)
		CPU0->reg_u = tmp;
	if ((tmp = hex16(args+20)) >= 0)
		CPU0->reg_s = tmp;
	if ((tmp = hex16(args+24)) >= 0)
		CPU0->reg_pc = tmp;
	if (xroar_machine_config->cpu == CPU_HD6309) {
		if ((tmp = hex8(args)) >= 0)
			((struct HD6309 *)CPU0)->reg_md = tmp;
		if ((tmp = hex8(args)) >= 0)
			HD6309_REG_E(((struct HD6309 *)CPU0)) = tmp;
		if ((tmp = hex8(args)) >= 0)
			HD6309_REG_F(((struct HD6309 *)CPU0)) = tmp;
		if ((tmp = hex16(args)) >= 0)
			((struct HD6309 *)CPU0)->reg_v = tmp;
	}
	send_packet_string(fd, "OK");
}

static void send_memory(int fd, char *args) {
	char *addr = strsep(&args, ",");
	if (!args || !addr)
		goto error;
	uint16_t A = strtoul(addr, NULL, 16);
	unsigned length = strtoul(args, NULL, 16);
	uint8_t csum = 0;
	packet[0] = '$';
	if (send(fd, packet, 1, 0) < 0)
		return;
	for (unsigned i = 0; i < length; i++) {
		uint8_t b = machine_read_byte(A++);
		snprintf(packet, sizeof(packet), "%02x", b);
		csum += packet[0];
		csum += packet[1];
		if (send(fd, packet, 2, 0) < 0)
			return;
	}
	snprintf(packet, sizeof(packet), "#%02x", csum);
	if (send(fd, packet, 3, 0) < 0)
		return;
	if (recv(fd, packet, 1, 0) < 0)
		return;
	if (packet[0] != '+')
		return;
	if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_PACKET) {
		LOG_PRINT("gdb: packet sent (binary): %u bytes\n", length);
	}
	return;
error:
	send_packet(fd, NULL, 0);
}

static void set_memory(int fd, char *args) {
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
		machine_write_byte(A, v);
		A++;
		data += 2;
	}
	send_packet_string(fd, "OK");
	return;
error:
	send_packet_string(fd, "E00");
}

static void send_register(int fd, char *args) {
	unsigned regnum = strtoul(args, NULL, 16);
	unsigned value = 0;
	int size = 0;
	switch (regnum) {
	case 0: value = CPU0->reg_cc; size = 1; break;
	case 1: value = MC6809_REG_A(CPU0); size = 1; break;
	case 2: value = MC6809_REG_B(CPU0); size = 1; break;
	case 3: value = CPU0->reg_dp; size = 1; break;
	case 4: value = CPU0->reg_x; size = 2; break;
	case 5: value = CPU0->reg_y; size = 2; break;
	case 6: value = CPU0->reg_u; size = 2; break;
	case 7: value = CPU0->reg_s; size = 2; break;
	case 8: value = CPU0->reg_pc; size = 2; break;
	case 9: size = -1; break;
	case 10: size = -1; break;
	case 11: size = -1; break;
	case 12: size = -2; break;
	default: break;
	}
	if (xroar_machine_config->cpu == CPU_HD6309) {
		struct HD6309 *hcpu = (struct HD6309 *)CPU0;
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
	send_packet_string(fd, packet);
}

static void set_register(int fd, char *args) {
	char *regnum_str = strsep(&args, "=");
	if (!regnum_str || !args)
		goto error;
	unsigned regnum = strtoul(regnum_str, NULL, 16);
	unsigned value = strtoul(args, NULL, 16);
	struct HD6309 *hcpu = (struct HD6309 *)CPU0;
	if (regnum > 12)
		goto error;
	if (regnum > 8 && xroar_machine_config->cpu != CPU_HD6309)
		goto error;
	switch (regnum) {
	case 0: CPU0->reg_cc = value; break;
	case 1: MC6809_REG_A(CPU0) = value; break;
	case 2: MC6809_REG_B(CPU0) = value; break;
	case 3: CPU0->reg_dp = value; break;
	case 4: CPU0->reg_x = value; break;
	case 5: CPU0->reg_y = value; break;
	case 6: CPU0->reg_u = value; break;
	case 7: CPU0->reg_s = value; break;
	case 8: CPU0->reg_pc = value; break;
	case 9: hcpu->reg_md = value; break;
	case 10: HD6309_REG_E(hcpu) = value; break;
	case 11: HD6309_REG_F(hcpu) = value; break;
	case 12: hcpu->reg_v = value; break;
	default: break;
	}
	send_packet_string(fd, "OK");
	return;
error:
	send_packet_string(fd, "E00");
}

static void general_query(int fd, char *args) {
	char *query = strsep(&args, ":");
	if (0 == strncmp(query, "xroar.", 6)) {
		query += 6;
		if (0 == strcmp(query, "sam")) {
			if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_QUERY) {
				LOG_PRINT("gdb: query: xroar.sam\n");
			}
			sprintf(packet, "%04x", sam_get_register());
			send_packet(fd, packet, 4);
		} else {
			if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_QUERY) {
				LOG_PRINT("gdb: query: unknown xroar vendor query\n");
			}
		}
	} else if (0 == strcmp(query, "Supported")) {
		if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_QUERY) {
			LOG_PRINT("gdb: query: Supported\n");
		}
		send_supported(fd, args);
	} else if (0 == strcmp(query, "Attached")) {
		if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_QUERY) {
			LOG_PRINT("gdb: query: Attached\n");
		}
		send_packet_string(fd, "1");
	} else {
		if (xroar_cfg.debug_gdb & XROAR_DEBUG_GDB_QUERY) {
			LOG_PRINT("gdb: query: unknown query\n");
		}
		send_packet(fd, NULL, 0);
	}
}

static void general_set(int fd, char *args) {
	char *set = strsep(&args, ":");
	if (0 == strncmp(set, "xroar.", 6)) {
		set += 6;
		if (0 == strcmp(set, "sam")) {
			sam_set_register(hex16(args));
			send_packet_string(fd, "OK");
			return;
		}
	}
	send_packet(fd, NULL, 0);
	return;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// qSupported

static void send_supported(int fd, char *args) {
	(void)args;  // args ignored at the moment
	snprintf(packet, sizeof(packet), "PacketSize=%zx", sizeof(packet)-1);
	send_packet_string(fd, packet);
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