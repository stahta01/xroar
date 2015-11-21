/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2015  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_MACHINE_H_
#define XROAR_MACHINE_H_

#include <stdint.h>
#include <sys/types.h>

#include "breakpoint.h"
#include "xconfig.h"

struct slist;
struct cart;
struct MC6883;
struct MC6809;
struct MC6821;

#define RESET_SOFT 0
#define RESET_HARD 1

#define IS_DRAGON64 (xroar_machine_config->architecture == ARCH_DRAGON64)
#define IS_DRAGON32 (xroar_machine_config->architecture == ARCH_DRAGON32)
#define IS_DRAGON (!IS_COCO)
#define IS_COCO (xroar_machine_config->architecture == ARCH_COCO)

#define IS_PAL (xroar_machine_config->tv_standard == TV_PAL)
#define IS_NTSC (xroar_machine_config->tv_standard == TV_NTSC)

#define ANY_AUTO (-1)
#define MACHINE_DRAGON32 (0)
#define MACHINE_DRAGON64 (1)
#define MACHINE_TANO     (2)
#define MACHINE_COCO     (3)
#define MACHINE_COCOUS   (4)
#define ARCH_DRAGON32 (0)
#define ARCH_DRAGON64 (1)
#define ARCH_COCO     (2)
#define CPU_MC6809 (0)
#define CPU_HD6309 (1)
#define ROMSET_DRAGON32 (0)
#define ROMSET_DRAGON64 (1)
#define ROMSET_COCO     (2)
#define TV_PAL  (0)
#define TV_NTSC (1)
#define VDG_6847 (0)
#define VDG_6847T1 (1)

/* These are now purely for backwards-compatibility with old snapshots.
 * Cartridge types are now more generic: see cart.h.  */
#define DOS_NONE      (0)
#define DOS_DRAGONDOS (1)
#define DOS_RSDOS     (2)
#define DOS_DELTADOS  (3)

/* NTSC cross-colour can either be switched off, or sychronised to one
 * of two phases (a real CoCo does not emit a colour burst in high resolution
 * mode, so NTSC televisions sync to one at random on machine reset) */
#define NUM_CROSS_COLOUR_PHASES (3)
#define CROSS_COLOUR_OFF  (0)
#define CROSS_COLOUR_KBRW (1)
#define CROSS_COLOUR_KRBW (2)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Breakpoint flags for Dragon & compatibles. */

#define BP_SAM_TY (1 << 15)
#define BP_SAM_P1 (1 << 10)

/* Useful breakpoint mask and condition combinations. */

#define BP_MASK_ROM (BP_SAM_TY)
#define BP_COND_ROM (0)

/* Local flags determining whether breakpoints are added with
 * machine_add_bp_list(). */

#define BP_MACHINE_ARCH (1 << 0)
#define BP_CRC_BAS (1 << 1)
#define BP_CRC_EXT (1 << 2)
#define BP_CRC_ALT (1 << 3)
#define BP_CRC_COMBINED (1 << 4)

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
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_COMBINED, .cond_crc_combined = "@d64_1" }
#define BP_DRAGON32_ROM(...) \
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_COMBINED, .cond_crc_combined = "@d32" }
#define BP_DRAGON_ROM(...) \
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_COMBINED, .cond_crc_combined = "@dragon" }

#define BP_COCO_BAS10_ROM(...) \
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@bas10" }
#define BP_COCO_BAS11_ROM(...) \
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@bas11" }
#define BP_COCO_BAS12_ROM(...) \
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@bas12" }
#define BP_COCO_BAS13_ROM(...) \
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@bas13" }
#define BP_MX1600_BAS_ROM(...) \
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@mx1600" }
#define BP_COCO_ROM(...) \
	{ .bp = { .cond_mask = BP_MASK_ROM, .cond = BP_COND_ROM, __VA_ARGS__ }, .add_cond = BP_CRC_BAS, .cond_crc_bas = "@coco" }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_config {
	char *name;
	char *description;
	int id;
	int architecture;
	int cpu;
	char *vdg_palette;
	int keymap;
	int tv_standard;
	int vdg_type;
	int cross_colour_phase;
	int ram;
	_Bool nobas;
	_Bool noextbas;
	_Bool noaltbas;
	char *bas_rom;
	char *extbas_rom;
	char *altbas_rom;
	char *ext_charset_rom;
	char *default_cart;
	_Bool nodos;
	_Bool cart_enabled;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define MACHINE_SIGINT (2)
#define MACHINE_SIGILL (4)
#define MACHINE_SIGTRAP (5)
#define MACHINE_SIGFPE (8)

extern struct MC6883 *SAM0;
extern unsigned int machine_ram_size;  /* RAM in bytes, up to 64K */
extern uint8_t machine_ram[0x10000];
extern struct cart *machine_cart;
extern _Bool has_bas, has_extbas, has_altbas, has_combined;
extern uint32_t crc_bas, crc_extbas, crc_altbas, crc_combined;

extern struct xconfig_enum machine_arch_list[];
extern struct xconfig_enum machine_keyboard_list[];
extern struct xconfig_enum machine_cpu_list[];
extern struct xconfig_enum machine_tv_type_list[];
extern struct xconfig_enum machine_vdg_type_list[];

/* Add a new machine config: */
struct machine_config *machine_config_new(void);
/* For finding known configs: */
struct machine_config *machine_config_by_id(int id);
struct machine_config *machine_config_by_name(const char *name);
struct machine_config *machine_config_by_arch(int arch);
_Bool machine_config_remove(const char *name);
struct slist *machine_config_list(void);
/* Find a working machine by searching available ROMs: */
struct machine_config *machine_config_first_working(void);
/* Complete a config replacing ANY_AUTO entries: */
void machine_config_complete(struct machine_config *mc);
void machine_config_print_all(_Bool all);

void machine_init(void);
void machine_shutdown(void);
void machine_configure(struct machine_config *mc);  /* apply config */
void machine_reset(_Bool hard);
int machine_run(int ncycles);
void machine_single_step(void);
void machine_toggle_pause(void);

void machine_signal(int sig);
void machine_trap(void *data);

void machine_set_trace(_Bool trace_on);

int machine_num_cpus(void);
int machine_num_pias(void);
struct MC6809 *machine_get_cpu(int n);
struct MC6821 *machine_get_pia(int n);

/* simplified read & write byte for convenience functions */
uint8_t machine_read_byte(unsigned A);
void machine_write_byte(unsigned A, unsigned D);
/* simulate an RTS without otherwise affecting machine state */
void machine_op_rts(struct MC6809 *cpu);

void machine_set_fast_sound(_Bool fast);
void machine_select_fast_sound(_Bool fast);
void machine_update_sound(void);

void machine_set_inverted_text(_Bool);

/* Helper function to populate breakpoints from a list. */
void machine_bp_add_n(struct machine_bp *list, int n);
void machine_bp_remove_n(struct machine_bp *list, int n);
#define machine_bp_add_list(list) machine_bp_add_n(list, sizeof(list) / sizeof(struct machine_bp))
#define machine_bp_remove_list(list) machine_bp_remove_n(list, sizeof(list) / sizeof(struct machine_bp))

void machine_insert_cart(struct cart *c);
void machine_remove_cart(void);

int machine_load_rom(const char *path, uint8_t *dest, off_t max_size);

#endif  /* XROAR_MACHINE_H_ */
