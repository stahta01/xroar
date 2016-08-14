/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2016  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_UI_H_
#define XROAR_UI_H_

#include <stdint.h>

#include "module.h"
#include "xconfig.h"

struct joystick_module;
struct vdisk;

/* Filtering option for OpenGL video */
#define UI_GL_FILTER_AUTO (-1)
#define UI_GL_FILTER_NEAREST (0)
#define UI_GL_FILTER_LINEAR  (1)

/* How to render NTSC cross-colour: simple (4-colour palette), 5bit (5-bit LUT)
 * or simulated (NTSC encoded, filtered, decoded). */
#define UI_CCR_SIMPLE (0)
#define UI_CCR_5BIT (1)
#define UI_CCR_SIMULATED (2)

struct ui_cfg {
	char *vo;  // video output module
	char *geometry;
	int gl_filter;
	_Bool fullscreen;
	int ccr;  // cross-colour renderer
};

extern struct xconfig_enum ui_ccr_list[];
extern struct xconfig_enum ui_gl_filter_list[];

/* To fit into the limits of the various UI toolkits in use, tag ids are 7
 * bits, and values are 16 bits wide. */

enum ui_tag {
	/* Simple action */
	ui_tag_action = 1,
	/* Hardware */
	ui_tag_machine,
	ui_tag_cartridge,
	/* Tape */
	ui_tag_tape_flags,
	ui_tag_tape_input_filename,  // .data = filename
	ui_tag_tape_output_filename,  // .data = filename
	/* Disk */
	ui_tag_disk_new,
	ui_tag_disk_insert,
	ui_tag_disk_eject,
	ui_tag_disk_write_enable,
	ui_tag_disk_write_back,
	ui_tag_disk_data,  // .data = struct vdisk
	/* Video */
	ui_tag_fullscreen,
	ui_tag_vdg_inverse,
	ui_tag_ccr,
	ui_tag_cross_colour,
	/* Audio */
	ui_tag_fast_sound,
	/* Keyboard */
	ui_tag_keymap,
	ui_tag_kbd_translate,
	/* Joysticks */
	ui_tag_joy_right,
	ui_tag_joy_left,
};

/* Actions (simple responses to user input) are probably handled internally,
 * but enumerate them here: */

enum ui_action {
	ui_action_quit,
	ui_action_reset_soft,
	ui_action_reset_hard,
	ui_action_file_load,
	ui_action_file_run,
	ui_action_file_save_snapshot,
	ui_action_tape_input,
	ui_action_tape_output,
	ui_action_tape_input_rewind,
	ui_action_zoom_in,
	ui_action_zoom_out,
	ui_action_joystick_swap,
};

struct ui_module {
	struct module common;
	FileReqModule * const *filereq_module_list;
	struct module * const *vo_module_list;
	SoundModule * const *sound_module_list;
	struct joystick_module * const *joystick_module_list;
	void (* const run)(void);
	void (* const set_state)(enum ui_tag, int value, const void *data);
};

extern struct ui_module * const *ui_module_list;
extern struct ui_module *ui_module;

void ui_print_vo_help(void);

#endif
