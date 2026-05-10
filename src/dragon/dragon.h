/** \file
 *
 *  \brief Dragon and Tandy Colour Computer machines.
 *
 *  \copyright Copyright 2003-2026 Ciaran Anscomb
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

#ifndef XROAR_DRAGON_DRAGON_H_
#define XROAR_DRAGON_DRAGON_H_

#include <stdint.h>

#include "breakpoint.h"
#include "events.h"
#include "machine.h"
#include "mc6809/mc6809.h"
#include "vdg_pal.h"
#include "xroar.h"

struct MC6821;
struct MC6847;
struct MC6883;
struct cart;
struct gdb_interface;
struct printer_interface;
struct ram;
struct rombank;
struct sound_interface;
struct tape_interface;
struct vo_interface;

enum dragon_sam_variant {
	DRAGON_SAM_74LS783,
	DRAGON_SAM_74LS785,
	DRAGON_SAM_SAMX8,
};

struct dragon {
	struct machine public;  // first element in turn is part

	struct MC6809 *CPU;
	struct MC6883 *SAM;
	struct MC6821 *PIA0, *PIA1;
	struct MC6847 *VDG;
	struct rombank *ROM0;
	struct rombank *ext_charset;
	struct ram *RAM;

	struct vo_interface *vo;
	int frame;  // track frameskip
	struct sound_interface *snd;

	// Optional iMMUnity memory expansion
	struct immunity *immunity;

	// Optional PAL logic interposing VDG
	struct vdg_pal vdg_pal;

	// Derived machines can use these to redirect address decoding.  If
	// they return true, the address was handled, no need to continue.
	_Bool (*read_byte)(struct dragon *, unsigned A);
	_Bool (*write_byte)(struct dragon *, unsigned A);

	_Bool inverted_text;
	struct cart *cart;
	unsigned configured_frameskip;
	unsigned frameskip;

	int cycles;

	// Clock inhibit - for when "speed up" code wants to access memory
	// without advancing the clock.
	_Bool clock_inhibit;

	// RAM read buffer.  Driven to data bus only when SAM S == 0.
	uint8_t Dread;

	// Debug
	struct bp_session *bp_session;
	_Bool single_step;
	int stop_signal;
#ifdef WANT_GDB_TARGET
	struct gdb_interface *gdb_interface;
#endif

	struct tape_interface *tape_interface;
	struct printer_interface *printer_interface;

	struct {
		struct keyboard_interface *interface;
	} keyboard;

	// Set to invert HS to PIA
	_Bool hs_invert;

	// NTSC colour bursts
	_Bool use_ntsc_burst_mod; // 0 for PAL-M (green-magenta artefacting)
	unsigned ntsc_burst_mod;

	// UI message receipt
	int msgr_client_id;

	// Useful configuration side-effect tracking
	_Bool has_bas, has_extbas, has_altbas, has_combined;
	_Bool has_ext_charset;
	uint32_t crc_bas, crc_extbas, crc_altbas, crc_combined;
	uint32_t crc_ext_charset;
	_Bool is_dragon;
	_Bool unexpanded_dragon32;
	_Bool relaxed_pia0_decode;
	_Bool relaxed_pia1_decode;

	struct {
		int sam_variant;
		_Bool immunity;
	} option;
};

extern const struct ser_struct_data dragon_ser_struct_data;
extern struct xconfig_option const dragon_options[];

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// CPU cycle code construction kit.
//
// Derived machines can override cpu_cycle() to simulate interposing the CPU or
// the SAM.  They may modify the address seen by the SAM (e.g. iMMUnity), or
// the addresses presented to RAM (e.g. Deluxe CoCo).  These are some common
// functions that such delegates may still need to call.
//
// The general order of operation for the cpy_cycle() delegate is as follows:
//
// Calls dragon_check_traps() to fire memory access traps using the original
// address accessed by the CPU.
//
// Possibly modify address seen by SAM.
//
// Calls SAM's mem_cycle() to perform address decode and fetch timing
// information.
//
// Collects together interrupt sources and presents them to the CPU.
//
// Possibly modify address presented to RAM.
//
// Calls dragon_cpu_cycle() to access RAM and devices common to the arch.

// Check memory access traps.
#ifdef WANT_GDB_TARGET
inline void dragon_check_traps(struct dragon *md, _Bool RnW, uint16_t A) {
	if (RnW) {
		if (md->bp_session->wp_read_list)
			bp_wp_read_hook(md->bp_session, A);
	} else {
		if (md->bp_session->wp_write_list)
			bp_wp_write_hook(md->bp_session, A);
	}
}
#else
#define dragon_check_traps(...)
#endif

// Advance clock and run scheduled events.
inline void dragon_advance_clock(struct dragon *md, int ncycles) {
	md->cycles -= ncycles;
	if (md->cycles <= 0) md->CPU->running = 0;
	event_run_queue(MACHINE_EVENT_LIST, ncycles);
}

// Common cycle-handling code.
void dragon_cpu_cycle(struct dragon *md, _Bool RnW, uint16_t A, unsigned Zrow, unsigned Zcol);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Set a ROM configuration to a default value if not "defined"
void dragon_set_default_rom(_Bool dfn, char **romp, const char *dfl);

void dragon_config_complete(struct machine_config *mc);
_Bool dragon_is_working_config(struct machine_config *mc);
void dragon_verify_ram_size(struct machine_config *mc);

void dragon_allocate_common(struct dragon *md);
void dragon_initialise_common(struct dragon *md, struct machine_config *mc);
_Bool dragon_finish_common(struct dragon *md);
void dragon_free_common(struct part *p);

_Bool dragon_has_interface(struct part *p, const char *ifname);
void dragon_attach_interface(struct part *p, const char *ifname, void *intf);

void dragon_reset(struct machine *m, _Bool hard);

void dragon_keyboard_update(void *sptr);
void dragon_update_vdg_mode(struct dragon *md);

void dragon_pia1b_data_postwrite(void *sptr);

// VDG interfacing
void dragon_vdg_hs(void *sptr, _Bool level);
void dragon_vdg_fs(void *sptr, _Bool level);

#endif
