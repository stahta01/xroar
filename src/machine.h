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
#include "sds.h"

#include "breakpoint.h"
#include "debug.h"
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
	void (*handler_func)(void *, bool, uint32_t);
};

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
	bool bas_dfn;
	char *bas_rom;
	bool extbas_dfn;
	char *extbas_rom;
	bool altbas_dfn;
	char *altbas_rom;
	char *ext_charset_rom;
	bool default_cart_dfn;
	char *default_cart;
	bool nodos;
	bool cart_enabled;
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
bool machine_config_remove(const char *name);
void machine_config_remove_all(void);
struct slist *machine_config_list(void);
/* Find a working machine by searching available ROMs: */
struct machine_config *machine_config_first_working(void);
void machine_config_print_all(FILE *f, bool all);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Extend struct partdb_entry to contain machine-specific helpers

struct machine_partdb_entry {
	struct partdb_entry partdb_entry;

	// resolve any undefined config
	void (*config_complete)(struct machine_config *mc);

	// check everything ok for this machine to run (e.g. ROM files exist)
	bool (*is_working_config)(struct machine_config *mc);

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

enum machine_endian {
	machine_endian_big = 0,
	machine_endian_little,
};

struct machine {
	struct part part;

	struct machine_config *config;

	void (*insert_cart)(struct machine *m, struct cart *c);
	void (*remove_cart)(struct machine *m);

	void (*reset)(struct machine *m, bool hard);
	enum machine_run_state (*run)(struct machine *m, int ncycles);
	void (*single_step)(struct machine *m);
	void (*signal)(struct machine *m, int sig);

	bool (*set_pause)(struct machine *m, int action);
	void *(*get_interface)(struct machine *m, const char *ifname);

	// Query if machine (or possibly sub-part) supports a named interface.
	bool (*has_interface)(struct part *p, const char *ifname);
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

		// Add a watchpoint
		void (*add_watchpoint)(struct machine *, bool RnW,
				       uint32_t Astart, uint32_t Aend,
				       DELEGATE_T2(void, bool, uint32) handler);

		// Remove a watchpoint, wildcard matching similar to breakpoints,
		// though only Astart < 0 is checked.
		void (*remove_watchpoint)(struct machine *, int RnW,
					  int32_t Astart, uint32_t Aend,
					  DELEGATE_T2(void, bool, uint32) handler);

		// CPU debug information copied from main CPU (the one that
		// would be debugged)
		struct debug_cpu cpu;

		// Debug target definition
		struct debug_target *target;

		// Target description as XML (to send to GDB)
		sds target_xml;
	} debug;
};

extern const struct ser_struct_data machine_ser_struct_data;

struct machine *machine_new(struct machine_config *mc);
bool machine_is_a(struct part *p, const char *name);

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

#ifdef WANT_GDB_TARGET
void machine_add_hbreak(struct machine *m, uint32_t A);
void machine_remove_hbreak(struct machine *m, int32_t A);
#define machine_remove_hbreak_all(m) machine_remove_hbreak((m), -1)
#endif

#define machine_add_watchpoint(m,rnw,as,ae,h) (m)->debug.add_watchpoint((m), (rnw), (as), (ae), (h))
#define machine_remove_watchpoint(m,rnw,as,ae,h) (m)->debug.remove_watchpoint((m), (rnw), (as), (ae), (h))

#define machine_remove_watchpoint_all(m,s) machine_remove_watchpoint((m), -1, 0, DELEGATE_AS2(void, bool, uint32, NULL, (s)))

#ifdef WANT_GDB_TARGET
void machine_add_hwatch(struct machine *m, bool RnW, uint32_t Astart, uint32_t Aend);
void machine_remove_hwatch(struct machine *m, int RnW, int32_t Astart, uint32_t Aend);
#define machine_remove_hwatch_all(m) machine_remove_hwatch((m), -1, -1, 0)
#endif

struct machine_module {
	const char *name;
	const char *description;
	void (*config_complete)(struct machine_config *mc);
	struct machine *(* const new)(struct machine_config *mc);
};

#endif
