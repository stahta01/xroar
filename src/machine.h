/** \file
 *
 *  \brief Machine configuration.
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

#ifndef XROAR_MACHINE_H_
#define XROAR_MACHINE_H_

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "delegate.h"

#include "breakpoint.h"
#include "part.h"
#include "xconfig.h"

struct cart;
struct ser_handle;
struct ser_struct_data;
struct slist;
struct sound_interface;
struct tape_interface;
struct vo_interface;

#define RESET_SOFT 0
#define RESET_HARD 1

#define ANY_AUTO (-1)
#define MACHINE_DRAGON32 (0)
#define MACHINE_DRAGON64 (1)
#define MACHINE_TANO     (2)
#define MACHINE_COCO     (3)
#define MACHINE_COCOUS   (4)
#define CPU_MC6809 (0)
#define CPU_HD6309 (1)
#define ROMSET_DRAGON32 (0)
#define ROMSET_DRAGON64 (1)
#define ROMSET_COCO     (2)
#define TV_PAL  (0)
#define TV_NTSC (1)
#define TV_PAL_M (2)

// TV input profiles. These are converted into combinations of input,
// cross-colour renderer and cross-colour phase to configure the video module.

#define TV_INPUT_SVIDEO (0)
#define TV_INPUT_CMP_KBRW (1)
#define TV_INPUT_CMP_KRBW (2)
#define TV_INPUT_RGB (3)
#define NUM_TV_INPUTS_DRAGON (3)
#define NUM_TV_INPUTS_COCO3 (4)

#define VDG_6847 (0)
#define VDG_6847T1 (1)
#define VDG_GIME_1986 (2)
#define VDG_GIME_1987 (3)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_bp_entry {
	const char *label;
	void (*handler_func)(void *, _Bool, uint32_t);
};

/* Local flags determining whether breakpoints are added with
 * machine_add_bp_list(). */

#define BP_CRC_BAS (1 << 0)
#define BP_CRC_EXT (1 << 1)
#define BP_CRC_ALT (1 << 2)
#define BP_CRC_COMBINED (1 << 3)

struct machine_bp {
	struct breakpoint bp;

	// Each bit of add_cond represents a local condition that must match
	// before machine_add_bp_list() will add a breakpoint.
	unsigned add_cond;

	// Local conditions to be matched.
	int cond_machine_arch;
	// CRC conditions listed by crclist name.
	const char *cond_crc_combined;
	const char *cond_crc_bas;
	const char *cond_crc_extbas;
	const char *cond_crc_altbas;
};

/* Convenience macros for standard types of breakpoint. */

#define BP_DRAGON64_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_COMBINED, .cond_crc_combined = "@d64_1" }
#define BP_DRAGON32_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_COMBINED, .cond_crc_combined = "@d32" }
#define BP_DRAGON_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_COMBINED, .cond_crc_combined = "@dragon" }

#define BP_COCO_BAS10_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@bas10" }
#define BP_COCO_BAS11_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@bas11" }
#define BP_COCO_BAS12_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@bas12" }
#define BP_COCO_BAS13_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@bas13" }
#define BP_COCO3_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_EXT, .cond_crc_extbas = "@coco3" }
#define BP_MX1600_BAS_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@mx1600" }
#define BP_COCO_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@coco" }

#define BP_MC10_ROM(...) \
	{ .bp = { __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@mc10_compat" }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_config {
	char *name;
	char *description;
	int id;
	char *architecture;
	int cpu;
	char *vdg_palette;
	int keymap;
	int tv_standard;
	int tv_input;
	int vdg_type;
	int ram_org;
	int ram;
	int ram_init;
	_Bool bas_dfn;
	char *bas_rom;
	_Bool extbas_dfn;
	char *extbas_rom;
	_Bool altbas_dfn;
	char *altbas_rom;
	char *ext_charset_rom;
	_Bool default_cart_dfn;
	char *default_cart;
	_Bool nodos;
	_Bool cart_enabled;
	struct slist *opts;
};

extern struct xconfig_enum machine_keyboard_list[];
extern struct xconfig_enum machine_cpu_list[];
extern struct xconfig_enum machine_tv_type_list[];
extern struct xconfig_enum machine_tv_input_list[];
extern struct xconfig_enum machine_vdg_type_list[];
extern struct xconfig_enum machine_ram_org_list[];
extern struct xconfig_enum machine_ram_init_list[];

/** \brief Create a new machine config.
 */
struct machine_config *machine_config_new(void);

/** \brief Serialise machine config.
 */
void machine_config_serialise(struct ser_handle *sh, unsigned otag, struct machine_config *mc);

/** \brief Deserialise machine config.
 */
struct machine_config *machine_config_deserialise(struct ser_handle *sh);

/* For finding known configs: */
struct machine_config *machine_config_by_id(int id);
struct machine_config *machine_config_by_name(const char *name);
struct machine_config *machine_config_by_arch(int arch);
void machine_config_complete(struct machine_config *mc);
_Bool machine_config_remove(const char *name);
void machine_config_remove_all(void);
struct slist *machine_config_list(void);
/* Find a working machine by searching available ROMs: */
struct machine_config *machine_config_first_working(void);
void machine_config_print_all(FILE *f, _Bool all);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Extend struct partdb_entry to contain machine-specific helpers

struct machine_partdb_entry {
	struct partdb_entry partdb_entry;

	// resolve any undefined config
	void (*config_complete)(struct machine_config *mc);

	// check everything ok for this machine to run (e.g. ROM files exist)
	_Bool (*is_working_config)(struct machine_config *mc);

	// cartridge architecture valid for this machine
	const char *cart_arch;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define MACHINE_SIGINT (2)
#define MACHINE_SIGILL (4)
#define MACHINE_SIGTRAP (5)
#define MACHINE_SIGFPE (8)

enum machine_run_state {
	machine_run_state_ok = 0,
	machine_run_state_stopped,
};

struct machine {
	struct part part;

	struct machine_config *config;

	void (*insert_cart)(struct machine *m, struct cart *c);
	void (*remove_cart)(struct machine *m);

	void (*reset)(struct machine *m, _Bool hard);
	enum machine_run_state (*run)(struct machine *m, int ncycles);
	void (*single_step)(struct machine *m);
	void (*signal)(struct machine *m, int sig);

	void (*bp_add_n)(struct machine *m, struct machine_bp *list, int n, void *sptr);
	void (*bp_remove_n)(struct machine *m, struct machine_bp *list, int n);

	_Bool (*set_pause)(struct machine *m, int action);
	void *(*get_interface)(struct machine *m, const char *ifname);

	// Query if machine (or possibly sub-part) supports a named interface.
	_Bool (*has_interface)(struct part *p, const char *ifname);
	// Connect a named interface.
	void (*attach_interface)(struct part *p, const char *ifname, void *intf);

	/* simplified read & write byte for convenience functions */
	uint8_t (*read_byte)(struct machine *m, unsigned A, uint8_t D);
	void (*write_byte)(struct machine *m, unsigned A, uint8_t D);
	/* simulate an RTS without otherwise affecting machine state */
	void (*op_rts)(struct machine *m);
	// Simple RAM dump to file
	void (*dump_ram)(struct machine *m, FILE *fd);

	struct {
		int type;
	} keyboard;

	struct {
		// Resolve symbol to address (returns -1 if not found)
		int32_t (*get_symbol)(struct machine *, const char *label);

		// Add breakpoint
		void (*add_breakpoint)(struct machine *, uint32_t A,
				       DELEGATE_T2(void, bool, uint32) handler);

		// Remove breakpoint(s) by address and handler.  A < 0,
		// handler.func == NULL or handler.sptr == NULL specify
		// a wildcard for that field.
		void (*remove_breakpoint)(struct machine *, int32_t A,
					  DELEGATE_T2(void, bool, uint32) handler);
	} debug;
};

extern const struct ser_struct_data machine_ser_struct_data;

struct machine *machine_new(struct machine_config *mc);
_Bool machine_is_a(struct part *p, const char *name);

/* OLD Helper function to populate breakpoints from a list. */
#define machine_bp_add_list(m, list, sptr) (m)->bp_add_n(m, list, sizeof(list) / sizeof(struct machine_bp), sptr)
#define machine_bp_remove_list(m, list) (m)->bp_remove_n(m, list, sizeof(list) / sizeof(struct machine_bp))

#define machine_add_breakpoint(m,a,h) (m)->debug.add_breakpoint((m), (a), (h))
#define machine_remove_breakpoint(m,a,h) (m)->debug.remove_breakpoint((m), (a), (h))

// Resolve symbol and add breakpoint using debug.add_breakpoint()
void machine_add_breakpoint_sym(struct machine *m, const char *label,
				DELEGATE_T2(void, bool, uint32) handler);

// Resolve symbol and remove breakpoint using debug.remove_breakpoint().
// Specifying label == NULL acts as a wildcard in the same way as specifying
// A < 0 for the by-address function.
void machine_remove_breakpoint_sym(struct machine *m, const char *label,
				   DELEGATE_T2(void, bool, uint32) handler);

#define machine_remove_breakpoint_all(m,s) machine_remove_breakpoint_sym((m), NULL, DELEGATE_AS2(void, bool, uint32, NULL, (s)))

void machine_add_breakpoint_list_n(struct machine *m, struct machine_bp_entry *list,
				   size_t nentries, void *sptr);

#define machine_add_breakpoint_list(m,l,s) machine_add_breakpoint_list_n((m), (l), sizeof(l) / sizeof(struct machine_bp_entry), (s))

void machine_remove_breakpoint_list_n(struct machine *m, struct machine_bp_entry *list,
				      size_t nentries, void *sptr);

#define machine_remove_breakpoint_list(m,l,s) machine_remove_breakpoint_list_n((m), (l), sizeof(l) / sizeof(struct machine_bp_entry), (s))

struct machine_module {
	const char *name;
	const char *description;
	void (*config_complete)(struct machine_config *mc);
	struct machine *(* const new)(struct machine_config *mc);
};

#endif
