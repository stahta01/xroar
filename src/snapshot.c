/** \file
 *
 *  \brief Snapshotting of emulated system.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "c-strcase.h"
#include "xalloc.h"

#include "auto_kbd.h"
#include "cart.h"
#include "fs.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809/hd6309.h"
#include "mc6809/mc6809.h"
#include "mc6821.h"
#include "mc6847/mc6847.h"
#include "mc6883.h"
#include "part.h"
#include "ram.h"
#include "serialise.h"
#include "snapshot.h"
#include "tape.h"
#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

#ifdef HAVE_WASM
#include "wasm/wasm.h"
#endif

// Top-level snapshot serialisation tags:

// Special header tag - it's fine to reuse this as it only ever appears
// at the beginning of the file as a header.
#define SNAPSHOT_SER_HEADER         (0x23)

#define SNAPSHOT_SER_MACHINE        (1)
#define SNAPSHOT_SER_VDRIVE_INTF    (2)

// Only include V1 snapshot support for non-emscripten builds including Dragon
// arch support.
#undef WANT_V1_SNAPSHOTS
#ifdef WANT_MACHINE_ARCH_DRAGON
#ifndef HAVE_WASM
#define WANT_V1_SNAPSHOTS
#endif
#endif

const char *snapv2_header = "/usr/bin/env xroar\n# 6809.org.uk\n";
static int read_v2_snapshot(const char *filename);

#ifdef WANT_V1_SNAPSHOTS
const char *snapv1_header = "XRoar snapshot.\012\000";
static int read_v1_snapshot(const char *filename);
#else
#ifndef HAVE_WASM
static int read_v1_snapshot(const char *f) { (void)f; return -1; }
#endif
#endif

// For WebAssembly, read the snapshot in, but if that resulted in any pending
// files, queue up another go (but just the one).  This only really applies to
// ROM files.

#ifdef HAVE_WASM
static void do_retry_read_snapshot(void *sptr) {
	char *filename = sptr;
	if (filename) {
		read_v2_snapshot(filename);
		free(filename);
	}
}
#endif

int read_snapshot(const char *filename) {
#ifndef HAVE_WASM
	if (read_v2_snapshot(filename) < 0 &&
	    read_v1_snapshot(filename) < 0) {
		LOG_MOD_WARN("snapshot", "%s: read failed\n", filename);
		return -1;
	}
#else
	read_v2_snapshot(filename);
	if (xroar.machine) {
		wasm_ui_prepare_machine(xroar.machine->config);
		struct cart *c = xroar.machine->get_interface(xroar.machine, "cart");
		if (c) {
			wasm_ui_prepare_cartridge(c->config);
		}
	}
	char *fncopy = xstrdup(filename);
	event_queue_auto(&UI_EVENT_LIST, DELEGATE_AS0(void, do_retry_read_snapshot, fncopy), 1);
	return -1;
#endif
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

int write_snapshot(const char *filename) {
	if (xroar_filetype_by_ext(filename) == FILETYPE_RAM) {
		// simple ram dump
		if (!xroar.machine->dump_ram) {
			LOG_MOD_WARN("snapshot/ram", "machine does not support RAM dump\n");
			return -1;
		}
		FILE *fd = fopen(filename, "wb");
		if (!fd) {
			LOG_MOD_WARN("snapshot/ram", "%s: %s\n", filename, strerror(errno));
			return -1;
		}
		xroar.machine->dump_ram(xroar.machine, fd);
		fclose(fd);
		return 0;
	}

	struct ser_handle *sh = ser_open(filename, ser_mode_write);
	if (!sh) {
		LOG_MOD_WARN("snapshot/v2", "%s: %s\n", filename, strerror(errno));
		return -1;
	}

	ser_write_tag(sh, 0x23, strlen(snapv2_header));
	ser_write_untagged(sh, snapv2_header, strlen(snapv2_header));

	ser_write_open_string(sh, SNAPSHOT_SER_MACHINE, "machine");
	part_serialise((struct part *)xroar.machine, sh);

	vdrive_interface_serialise(xroar.vdrive_interface, sh, SNAPSHOT_SER_VDRIVE_INTF);

	ser_write_close_tag(sh);
	ser_close(sh);
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int read_v2_snapshot(const char *filename) {
	struct ser_handle *sh = ser_open(filename, ser_mode_read);
	if (!sh) {
		return -1;
	}

	int tag = ser_read_tag(sh);
	if (tag != 0x23) {
		ser_close(sh);
		return -1;
	}
	char *id = ser_read_string(sh);
	if (!id || strcmp(id, snapv2_header) != 0) {
		ser_close(sh);
		return -1;
	}
	free(id);

	struct machine *m = NULL;

	while ((tag = ser_read_tag(sh)) > 0) {
		switch (tag) {

		case SNAPSHOT_SER_MACHINE:
			// Deserialises new machine.
			m = (struct machine *)part_deserialise(sh);
			if (m && !part_is_a((struct part *)m, "machine")) {
				LOG_MOD_WARN("snapshot/v2", "read: not a machine\n");
				part_free((struct part *)m);
				m = NULL;
				ser_set_error(sh, ser_error_format);
			}
			break;

		case SNAPSHOT_SER_VDRIVE_INTF:
			// Deserialise into vdrive interface.  Important that
			// new machine has been successfully read first, as
			// this will eject anything associated with currently
			// running machine.
			if (!m) {
				ser_set_error(sh, ser_error_format);
				break;
			}
			vdrive_interface_deserialise(xroar.vdrive_interface, sh);
			break;

		default:
			LOG_MOD_WARN("snapshot/v2", "read: unknown tag '%d'\n", tag);
			break;
		}
		if (ser_error(sh))
			break;
	}

	if (ser_error(sh)) {
		LOG_MOD_WARN("snapshot/v2", "read: %s at byte %zd\n", ser_errstr(sh), ser_errpos(sh));
	}

	ser_close(sh);

	if (!m) {
		return -1;
	}

	// TODO: probably a good idea to check the machine is really a machine
	// and the cart is really a cart before we start accessing them.

	// TODO: verify that any deserialised carts had their configs included
	// earlier in the snapshot.

	if (xroar.auto_kbd) {
		auto_kbd_free(xroar.auto_kbd);
		xroar.auto_kbd = NULL;
	}
	if (xroar.machine) {
		part_free((struct part *)xroar.machine);
	}
	xroar.machine_config = m->config;
	xroar.machine = m;
	xroar_connect_machine();

	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#ifdef WANT_V1_SNAPSHOTS

// Old snapshot READING code only follows.

/* Note: Setting up the correct ROM select for Dragon 64 depends on SAM
 * register update following PIA configuration. */

#define ID_REGISTER_DUMP (0)  // deprecated - part of ID_MC6809_STATE
#define ID_RAM_PAGE0     (1)
#define ID_PIA_REGISTERS (2)
#define ID_SAM_REGISTERS (3)
#define ID_MC6809_STATE  (4)
#define ID_KEYBOARD_MAP  (5)  // deprecated - part of ID_MACHINECONFIG
#define ID_ARCHITECTURE  (6)  // deprecated - part of ID_MACHINECONFIG
#define ID_RAM_PAGE1     (7)
#define ID_MACHINECONFIG (8)
#define ID_SNAPVERSION   (9)
#define ID_VDISK_FILE    (10)
#define ID_HD6309_STATE  (11)
#define ID_CART          (12)  // as of v1.8

#define SNAPSHOT_VERSION_MAJOR 1
#define SNAPSHOT_VERSION_MINOR 8

static char *read_string(FILE *fd, unsigned *size) {
	char *str = NULL;
	if (*size == 0) {
		return NULL;
	}
	int len = fs_read_uint8(fd);
	(*size)--;
	// For whatever reason, I chose to store len+1 as the size field
	// for strings.  Oh well, this means zero is invalid.
	if (len < 1) {
		return NULL;
	}
	if ((unsigned)(len-1) >= *size) {
		return NULL;
	}
	str = xzalloc(len);
	if (len > 1) {
		*size -= fread(str, 1, len-1, fd);
	}
	return str;
}

static const char *old_arch_mapping[4] = {
	"dragon32",
	"dragon64",
	"dragon64",
	"coco"
};

static void old_set_registers(struct machine *m, uint8_t *regs) {
	struct MC6809 *cpu = (struct MC6809 *)part_component_by_id_is_a(&m->part, "CPU", "MC6809");
	if (!cpu)
		return;
	cpu->reg_cc = regs[0];
	MC6809_REG_A(cpu) = regs[1];
	MC6809_REG_B(cpu) = regs[2];
	cpu->reg_dp = regs[3];
	cpu->reg_x = regs[4] << 8 | regs[5];
	cpu->reg_y = regs[6] << 8 | regs[7];
	cpu->reg_u = regs[8] << 8 | regs[9];
	cpu->reg_s = regs[10] << 8 | regs[11];
	cpu->reg_pc = regs[12] << 8 | regs[13];
	cpu->halt = 0;
	cpu->nmi = 0;
	cpu->firq = 0;
	cpu->irq = 0;
	cpu->state = MC6809_COMPAT_STATE_NORMAL;
	cpu->nmi_armed = 0;
}

static uint16_t *tfm_reg_ptr(struct HD6309 *hcpu, unsigned reg) {
	struct MC6809 *cpu = &hcpu->mc6809;
	switch (reg >> 4) {
	case 0:
		return &cpu->reg_d;
	case 1:
		return &cpu->reg_x;
	case 2:
		return &cpu->reg_y;
	case 3:
		return &cpu->reg_u;
	case 4:
		return &cpu->reg_s;
	default:
		break;
	}
	return NULL;
}

#define sex4(v) (((uint16_t)(v) & 0x07) - ((uint16_t)(v) & 0x08))

static int read_v1_snapshot(const char *filename) {
	FILE *fd;
	uint8_t buffer[17];
	int section, tmp;
	int version_major = 1, version_minor = 0;

	if (!(fd = fopen(filename, "rb")))
		return -1;
	if (fread(buffer, 17, 1, fd) < 1) {
		fclose(fd);
		return -1;
	}
	if (strncmp((char *)buffer, snapv1_header, 17)) {
		// Very old-style snapshot.  Register dump always came first.
		// Also, it used to be written out as only taking 12 bytes.
		if (buffer[0] != ID_REGISTER_DUMP || buffer[1] != 0
		    || (buffer[2] != 12 && buffer[2] != 14)) {
			fclose(fd);
			return -1;
		}
	}

	// Default to Dragon 64 for old snapshots
	struct machine_config *mc = machine_config_by_arch(1);  // 1 == old ARCH_DRAGON64
	xroar_configure_machine(mc);
	xroar.machine->reset(xroar.machine, RESET_HARD);
	// If old snapshot, buffer contains register dump
	if (buffer[0] != 'X') {
		old_set_registers(xroar.machine, buffer + 3);
	}

	// Cache values to set once done
	int keymap = -1;

	struct cart_config *cart_config = NULL;
	while ((section = fs_read_uint8(fd)) >= 0) {
		unsigned size = fs_read_uint16(fd);
		if (size == 0) size = 0x10000;
		LOG_MOD_DEBUG(3, "snapshot/v1", "read: chunk type %d, size %u\n", section, size);
		switch (section) {
			case ID_ARCHITECTURE:
				// Deprecated: Machine architecture
				if (size < 1) break;
				tmp = fs_read_uint8(fd);
				tmp %= 4;
				if (mc->architecture)
					free(mc->architecture);
				mc->architecture = xstrdup(old_arch_mapping[tmp]);
				xroar_configure_machine(mc);
				xroar.machine->reset(xroar.machine, RESET_HARD);
				size--;
				break;
			case ID_KEYBOARD_MAP:
				// Deprecated: Keyboard map
				if (size < 1) break;
				keymap = fs_read_uint8(fd);
				size--;
				break;
			case ID_REGISTER_DUMP:
				// Deprecated
				if (size < 14) break;
				size -= fread(buffer, 1, 14, fd);
				old_set_registers(xroar.machine, buffer);
				break;

			case ID_MC6809_STATE:
				{
					// MC6809 state
					if (size < 20) break;
					if (mc->cpu != CPU_MC6809) {
						LOG_MOD_WARN("snapshot/v1", "read: CPU mismatch - skipping MC6809 chunk\n");
						break;
					}
					struct MC6809 *cpu = (struct MC6809 *)part_component_by_id_is_a(&xroar.machine->part, "CPU", "MC6809");
					if (!cpu) {
						LOG_MOD_WARN("snapshot/v1", "read: CPU not found - skipping MC6809 chunk\n");
						break;
					}
					cpu->reg_cc = fs_read_uint8(fd);
					MC6809_REG_A(cpu) = fs_read_uint8(fd);
					MC6809_REG_B(cpu) = fs_read_uint8(fd);
					cpu->reg_dp = fs_read_uint8(fd);
					cpu->reg_x = fs_read_uint16(fd);
					cpu->reg_y = fs_read_uint16(fd);
					cpu->reg_u = fs_read_uint16(fd);
					cpu->reg_s = fs_read_uint16(fd);
					cpu->reg_pc = fs_read_uint16(fd);
					cpu->halt = fs_read_uint8(fd);
					cpu->nmi = fs_read_uint8(fd);
					cpu->firq = fs_read_uint8(fd);
					cpu->irq = fs_read_uint8(fd);
					if (size == 21) {
						// Old style
						int wait_for_interrupt;
						int skip_register_push;
						wait_for_interrupt = fs_read_uint8(fd);
						skip_register_push = fs_read_uint8(fd);
						if (wait_for_interrupt && skip_register_push) {
							cpu->state = MC6809_COMPAT_STATE_CWAI;
						} else if (wait_for_interrupt) {
							cpu->state = MC6809_COMPAT_STATE_SYNC;
						} else {
							cpu->state = MC6809_COMPAT_STATE_NORMAL;
						}
						size--;
					} else {
						cpu->state = fs_read_uint8(fd);
						// Translate old otherwise-unused MC6809
						// states indicating instruction page.
						cpu->page = 0;
						if (cpu->state == mc6809_state_instruction_page_2) {
							cpu->page = 0x0200;
							cpu->state = mc6809_state_next_instruction;
						}
						if (cpu->state == mc6809_state_instruction_page_3) {
							cpu->page = 0x0300;
							cpu->state = mc6809_state_next_instruction;
						}
					}
					cpu->nmi_armed = fs_read_uint8(fd);
					size -= 20;
					if (size > 0) {
						// Skip 'halted'
						(void)fs_read_uint8(fd);
						size--;
					}
				}
				break;

			case ID_HD6309_STATE:
				{
					// HD6309 state
					if (size < 27) break;
					if (mc->cpu != CPU_HD6309) {
						LOG_MOD_WARN("snapshot/v1", "read: CPU mismatch - skipping HD6309 chunk\n");
						break;
					}
					struct HD6309 *hcpu = (struct HD6309 *)part_component_by_id_is_a(&xroar.machine->part, "CPU", "HD6309");
					if (!hcpu) {
						LOG_MOD_WARN("snapshot/v1", "read: CPU not found - skipping HD6309 chunk\n");
						break;
					}
					struct MC6809 *cpu = &hcpu->mc6809;
					cpu->reg_cc = fs_read_uint8(fd);
					MC6809_REG_A(cpu) = fs_read_uint8(fd);
					MC6809_REG_B(cpu) = fs_read_uint8(fd);
					cpu->reg_dp = fs_read_uint8(fd);
					cpu->reg_x = fs_read_uint16(fd);
					cpu->reg_y = fs_read_uint16(fd);
					cpu->reg_u = fs_read_uint16(fd);
					cpu->reg_s = fs_read_uint16(fd);
					cpu->reg_pc = fs_read_uint16(fd);
					cpu->halt = fs_read_uint8(fd);
					cpu->nmi = fs_read_uint8(fd);
					cpu->firq = fs_read_uint8(fd);
					cpu->irq = fs_read_uint8(fd);
					hcpu->state = fs_read_uint8(fd);
					// Translate old otherwise-unused HD6309
					// states indicating instruction page.
					cpu->page = 0;
					if (hcpu->state == hd6309_state_instruction_page_2) {
						cpu->page = 0x0200;
						hcpu->state = hd6309_state_next_instruction;
					}
					if (hcpu->state == hd6309_state_instruction_page_3) {
						cpu->page = 0x0300;
						hcpu->state = hd6309_state_next_instruction;
					}
					cpu->nmi_armed = fs_read_uint8(fd);
					HD6309_REG_E(hcpu) = fs_read_uint8(fd);
					HD6309_REG_F(hcpu) = fs_read_uint8(fd);
					hcpu->reg_v = fs_read_uint16(fd);
					tmp = fs_read_uint8(fd);
					hcpu->reg_md = tmp;
					tmp = fs_read_uint8(fd);
					hcpu->tfm_src = tfm_reg_ptr(hcpu, tmp >> 4);
					hcpu->tfm_dest = tfm_reg_ptr(hcpu, tmp & 15);
					tmp = fs_read_uint8(fd);
					hcpu->tfm_src_mod = sex4(tmp >> 4);
					hcpu->tfm_dest_mod = sex4(tmp & 15);
					size -= 27;
				}
				break;

			case ID_MACHINECONFIG:
				// Machine running config
				if (size < 7) break;
				(void)fs_read_uint8(fd);  // requested_machine
				tmp = fs_read_uint8(fd);
				mc = machine_config_by_arch(tmp);
				tmp = fs_read_uint8(fd);  // was romset
				if (version_minor >= 7) {
					// old field not used any more, repurposed
					// in v1.7 to hold cpu type:
					mc->cpu = tmp;
				}
				mc->keymap = fs_read_uint8(fd);  // keymap
				mc->tv_standard = fs_read_uint8(fd);
				mc->ram = fs_read_uint8(fd);
				tmp = fs_read_uint8(fd);  // dos_type
				if (version_minor < 8) {
					// v1.8 adds a separate cart chunk
					switch (tmp) {
					case 1: // DragonDOS
						cart_config = cart_config_by_name("dragondos");
						break;
					case 2: // RSDOS
						cart_config = cart_config_by_name("rsdos");
						break;
					case 3: // Delta
						cart_config = cart_config_by_name("delta");
						break;
					default: break;
					}
				}
				size -= 7;
				if (size > 0) {
					mc->tv_input = fs_read_uint8(fd);
					size--;
				}
				xroar_configure_machine(mc);
				xroar.machine->reset(xroar.machine, RESET_HARD);
				break;

			case ID_PIA_REGISTERS:
				for (unsigned i = 0; i < 2; ++i) {
					char id[5];
					snprintf(id, sizeof(id), "PIA%u", i);
					struct MC6821 *pia = (struct MC6821 *)part_component_by_id_is_a(&xroar.machine->part, id, "MC6821");
					if (!pia) {
						LOG_MOD_WARN("snapshot/v1", "read: %s not found - skipping PIA\n", id);
						break;
					}
					if (size < 3) break;
					pia->a.direction_register = fs_read_uint8(fd);
					pia->a.output_register = fs_read_uint8(fd);
					pia->a.control_register = fs_read_uint8(fd);
					size -= 3;
					mc6821_update_a_state(pia);
					mc6821_write(pia, 1, pia->a.control_register);
					if (size < 3) break;
					pia->b.direction_register = fs_read_uint8(fd);
					pia->b.output_register = fs_read_uint8(fd);
					pia->b.control_register = fs_read_uint8(fd);
					size -= 3;
					mc6821_update_b_state(pia);
					mc6821_write(pia, 3, pia->b.control_register);
				}
				break;

			case ID_RAM_PAGE0:
				{
					struct part *p = &xroar.machine->part;
					struct ram *ram = (struct ram *)part_component_by_id_is_a(p, "RAM", "ram");
					if (ram && ram->d && ram->d[0]) {
						size_t psize = (ram->bank_nelems < size) ? ram->bank_nelems : size;
						uint8_t *ram_dst = ram->d[0];
						size -= fread(ram_dst, 1, psize, fd);
					}
				}
				break;
			case ID_RAM_PAGE1:
				{
					struct part *p = &xroar.machine->part;
					struct ram *ram = (struct ram *)part_component_by_id_is_a(p, "RAM", "ram");
					if (ram && ram->d && ram->d[0]) {
						size_t psize = (ram->bank_nelems < (size + 0x8000)) ? ram->bank_nelems - 0x8000: size;
						uint8_t *ram_dst = (uint8_t *)ram->d[0] + 0x8000;
						size -= fread(ram_dst, 1, psize, fd);
					}
				}
				break;
			case ID_SAM_REGISTERS:
				// SAM
				if (size < 2) break;
				tmp = fs_read_uint16(fd);
				size -= 2;
				{
					struct MC6883 *sam = (struct MC6883 *)part_component_by_id_is_a(&xroar.machine->part, "SAM", "SN74LS783");
					if (!sam) {
						LOG_MOD_WARN("snapshot/v1", "read: SAM not found - skipping SAM registers chunk\n");
						break;
					}
					mc6883_set_register(sam, tmp);
				}
				break;

			case ID_SNAPVERSION:
				// Snapshot version - abort if snapshot
				// contains stuff we don't understand
				if (size < 3) break;
				version_major = fs_read_uint8(fd);
				version_minor = fs_read_uint16(fd);
				size -= 3;
				if (version_major != SNAPSHOT_VERSION_MAJOR
				    || version_minor > SNAPSHOT_VERSION_MINOR) {
					LOG_MOD_WARN("snapshot/v1", "read: version %d.%d not supported.\n", version_major, version_minor);
					fclose(fd);
					return -1;
				}
				break;

			case ID_VDISK_FILE:
				// Attached virtual disk filenames
				{
					int drive;
					size--;
					drive = fs_read_uint8(fd);
					vdrive_eject_disk(xroar.vdrive_interface, drive);
					if (size > 0) {
						char *name = xmalloc(size);
						size -= fread(name, 1, size, fd);
						// the NUL should be in the snapshot, but
						// terminate explicitly just in case:
						name[size-1] = 0;
						vdrive_insert_disk(xroar.vdrive_interface, drive, vdisk_load(name));
						free(name);
					}
				}
				break;

			case ID_CART:
				// Attached cartridge
				{
					char *name = read_string(fd, &size);
					// must have a name
					if (!name || size == 0) break;
					char *desc = read_string(fd, &size);
					if (size == 0) break;
					char *type = read_string(fd, &size);
					if (size == 0) break;
					char *rom = read_string(fd, &size);
					if (size == 0) break;
					char *rom2 = read_string(fd, &size);
					if (size < 2) break;
					cart_config = cart_config_by_name(name);
					if (!cart_config) {
						cart_config = cart_config_new();
					}
					if (cart_config->name)
						free(cart_config->name);
					cart_config->name = name;
					if (cart_config->description)
						free(cart_config->description);
					cart_config->description = desc;
					if (cart_config->type)
						free(cart_config->type);
					cart_config->type = type;
					if (cart_config->rom)
						free(cart_config->rom);
					cart_config->rom = rom;
					if (cart_config->rom2)
						free(cart_config->rom2);
					cart_config->rom2 = rom2;
					cart_config->becker_port = fs_read_uint8(fd);
					cart_config->autorun = fs_read_uint8(fd);
					size -= 2;
				}
				break;

			default:
				// Unknown chunk
				LOG_MOD_WARN("snapshot/v1", "read: unknown chunk type %d in snaphot.\n", section);
				break;
		}
		if (size > 0) {
			LOG_MOD_WARN("snapshot/v1", "read: skipping extra bytes in snapshot chunk type %d\n", (int)section);
			for (; size; size--)
				(void)fs_read_uint8(fd);
		}
	}
	fclose(fd);
	if (cart_config) {
		ui_update_state(-1, ui_tag_cartridge, cart_config->id, NULL);
	}
	if (keymap >= 0) {
		ui_update_state(-1, ui_tag_keymap, keymap, NULL);
	}
	return 0;
}

#endif
