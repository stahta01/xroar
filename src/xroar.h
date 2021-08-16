/** \file
 *
 *  \brief XRoar initialisation and top-level emulator functions.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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

#ifndef XROAR_XROAR_H_
#define XROAR_XROAR_H_

#include <stdint.h>
#include <stdio.h>

#include "ui.h"
#include "xconfig.h"

#if __STDC_VERSION__ >= 201112L
#define _NORETURN _Noreturn
#elif __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 5)
#define _NORETURN __attribute__ ((noreturn))
#else
#define __NORETURN
#endif

struct ao_interface;
struct cart;
struct event;
struct machine_config;
struct slist;
struct vdg_palette;
struct vo_interface;
struct xroar_timeout;

/* Convenient values for arguments to helper functions */
#define XROAR_NEXT (-2)
#define XROAR_AUTO (-1)
#define XROAR_OFF  (0)
#define XROAR_ON   (1)

enum xroar_filetype {
	FILETYPE_UNKNOWN,
	FILETYPE_VDK,
	FILETYPE_JVC,
	FILETYPE_OS9,  // special type of JVC
	FILETYPE_DMK,
	FILETYPE_BIN,
	FILETYPE_HEX,
	FILETYPE_CAS,
	FILETYPE_WAV,
	FILETYPE_SNA,
	FILETYPE_ROM,
	FILETYPE_ASC,
};

/**************************************************************************/
/* Command line arguments */

struct xroar_cfg {
	/* Emulator interface */
	// Video
	int frameskip;
	_Bool vdg_inverted_text;
	// Audio
	char *ao_device;
	int ao_format;
	int ao_rate;
	int ao_channels;
	int ao_fragments;
	int ao_fragment_ms;
	int ao_fragment_nframes;
	int ao_buffer_ms;
	int ao_buffer_nframes;
	_Bool fast_sound;  // deprecated - ignored

	// Keyboard
	_Bool kbd_translate;
	// Cartridges
	_Bool becker;
	char *becker_ip;
	char *becker_port;
	// Files
	char *rompath;
	// Cassettes
	double tape_pan;
	double tape_hysteresis;
	int tape_rewrite_gap_ms;
	int tape_rewrite_leader;
	// Disks
	_Bool disk_write_back;
	_Bool disk_auto_os9;
	_Bool disk_auto_sd;
	// CRC lists
	_Bool force_crc_match;
	// Debugging
	_Bool gdb;
	char *gdb_ip;
	char *gdb_port;
	char *timeout_motoroff;
	char *snap_motoroff;
};

extern struct xroar_cfg xroar_cfg;

/**************************************************************************/
/* Global flags */

#define UI_EVENT_LIST xroar_ui_events
#define MACHINE_EVENT_LIST xroar_machine_events
extern struct event *xroar_ui_events;
extern struct event *xroar_machine_events;

extern struct vo_interface *xroar_vo_interface;
extern struct ao_interface *xroar_ao_interface;

extern struct machine_config *xroar_machine_config;
extern struct machine *xroar_machine;
extern struct tape_interface *xroar_tape_interface;
extern struct keyboard_interface *xroar_keyboard_interface;
extern struct printer_interface *xroar_printer_interface;
extern struct vdg_palette *xroar_vdg_palette;

extern struct vdrive_interface *xroar_vdrive_interface;

/**************************************************************************/

/// Configure XRoar, initialise modules and start machine.
struct ui_interface *xroar_init(int argc, char **argv);

/// Cleanly shut down before program exit.
void xroar_shutdown(void);

/// Process UI event queue and run emulated machine.
void xroar_run(int ncycles);

int xroar_filetype_by_ext(const char *filename);
int xroar_load_file_by_type(const char *filename, int autorun);

/* Scheduled shutdown */
struct xroar_timeout *xroar_set_timeout(char const *timestring);
void xroar_cancel_timeout(struct xroar_timeout *);

/* Helper functions */
void xroar_set_trace(int mode);
void xroar_new_disk(int drive);
void xroar_insert_disk_file(int drive, const char *filename);
void xroar_insert_disk(int drive);
void xroar_eject_disk(int drive);
_Bool xroar_set_write_enable(_Bool notify, int drive, int action);
_Bool xroar_set_write_back(_Bool notify, int drive, int action);
void xroar_set_cross_colour_renderer(_Bool notify, int action);
void xroar_set_cross_colour(_Bool notify, int action);
void xroar_set_vdg_inverted_text(_Bool notify, int action);
void xroar_set_ratelimit(int action);
void xroar_set_ratelimit_latch(_Bool notify, int action);
void xroar_set_pause(_Bool notify, int action);
_NORETURN void xroar_quit(void);
void xroar_set_fullscreen(_Bool notify, int action);
void xroar_load_file(const char * const *exts);
void xroar_run_file(const char * const *exts);
void xroar_set_keymap(_Bool notify, int map);
void xroar_set_kbd_translate(_Bool notify, int kbd_translate);
void xroar_set_joystick(_Bool notify, int port, const char *name);
void xroar_swap_joysticks(_Bool notify);
void xroar_cycle_joysticks(_Bool notify);
void xroar_configure_machine(struct machine_config *mc);
void xroar_set_machine(_Bool notify, int id);
void xroar_toggle_cart(void);
void xroar_set_cart(_Bool notify, const char *cc_name);
void xroar_set_cart_by_id(_Bool notify, int id);
void xroar_set_dos(int dos_type);  /* for old snapshots only */
void xroar_save_snapshot(void);
void xroar_insert_input_tape_file(const char *filename);
void xroar_insert_input_tape(void);
void xroar_eject_input_tape(void);
void xroar_insert_output_tape_file(const char *filename);
void xroar_insert_output_tape(void);
void xroar_eject_output_tape(void);
void xroar_hard_reset(void);
void xroar_soft_reset(void);

/* Helper functions for config printing */
void xroar_cfg_print_inc_indent(void);
void xroar_cfg_print_dec_indent(void);
void xroar_cfg_print_indent(FILE *f);
void xroar_cfg_print_bool(FILE *f, _Bool all, char const *opt, int value, int normal);
void xroar_cfg_print_int(FILE *f, _Bool all, char const *opt, int value, int normal);
void xroar_cfg_print_int_nz(FILE *f, _Bool all, char const *opt, int value);
void xroar_cfg_print_double(FILE *f, _Bool all, char const *opt, double value, double normal);
void xroar_cfg_print_flags(FILE *f, _Bool all, char const *opt, unsigned value);
void xroar_cfg_print_string(FILE *f, _Bool all, char const *opt, char const *value,
			    char const *normal);
void xroar_cfg_print_enum(FILE *f, _Bool all, char const *opt, int value, int normal,
			  struct xconfig_enum const *e);
void xroar_cfg_print_string_list(FILE *f, _Bool all, char const *opt, struct slist *l);

#endif
