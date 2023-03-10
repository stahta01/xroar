/** \file
 *
 *  \brief User-interface modules & interfaces.
 *
 *  \copyright Copyright 2003-2023 Ciaran Anscomb
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

#ifndef XROAR_UI_H_
#define XROAR_UI_H_

#include <stdint.h>

#include "module.h"
#include "vo.h"
#include "xconfig.h"

struct joystick_module;
struct vdisk;

/* Filtering option for OpenGL video */
#define UI_GL_FILTER_AUTO (-1)
#define UI_GL_FILTER_NEAREST (0)
#define UI_GL_FILTER_LINEAR  (1)

struct ui_cfg {
	// Video
	char *vo;  // video output module
	struct vo_cfg vo_cfg;
	// Keyboard
	char *keymap;
};

extern struct xconfig_enum ui_gl_filter_list[];

/* To fit into the limits of the various UI toolkits in use, tag ids are 7
 * bits, and values are 16 bits wide. */

enum ui_tag {
	// Simple action
	ui_tag_action = 1,
	// Hardware
	ui_tag_machine,
	ui_tag_cartridge,
	// Tape
	ui_tag_tape_flags,
	ui_tag_tape_input_filename,  // .data = filename
	ui_tag_tape_output_filename,  // .data = filename
	ui_tag_tape_motor,  // automatic control
	ui_tag_tape_playing,  // manual control (0 = paused)
	// Disk
	ui_tag_disk_new,
	ui_tag_disk_insert,
	ui_tag_disk_eject,
	ui_tag_disk_write_enable,
	ui_tag_disk_write_back,
	ui_tag_disk_data,  // .data = struct vdisk
	// Video
	ui_tag_ccr,
	ui_tag_tv_input,
	ui_tag_fullscreen,
	ui_tag_vdg_inverse,
	ui_tag_brightness,
	ui_tag_contrast,
	// Audio
	ui_tag_ratelimit,
	// Keyboard
	ui_tag_keymap,
	ui_tag_kbd_translate,
	// Joysticks
	ui_tag_joy_right,
	ui_tag_joy_left,
	// Misc
	ui_tag_about,
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
	ui_action_tape_play_pause,
	ui_action_tape_input_rewind,
	ui_action_tape_output_rewind,
	ui_action_zoom_in,
	ui_action_zoom_out,
	ui_action_joystick_swap,
};

struct ui_module {
	struct module common;
	struct module * const *filereq_module_list;
	struct module * const *vo_module_list;
	struct module * const *ao_module_list;
	struct joystick_module * const *joystick_module_list;
};

/** \brief Interface to UI module.
 */

struct ui_interface {
	DELEGATE_T0(void) free;

	/** \brief UI-specific function providing emulator main loop.
	 *
	 * If not provided, main() should call xroar_run() in a loop.
	 */
	DELEGATE_T0(void) run;

	/** \brief Update UI to reflect a change in emulator state.
	 *  \param ui_tag  from enum ui_tag.
	 *  \param value   value to set.
	 *  \param data    other tag-specific data.
	 *
	 * Calling this shall not in itself change any emulator state.
	 */
	DELEGATE_T3(void, int, int, cvoidp) update_state;  // ui_tag, value, data

	/** \brief Create or update machine menu.
	 *
	 * Called at startup, and whenever the machine config list changes.
	 */
	DELEGATE_T0(void) update_machine_menu;

	/** \brief Create or update cartridge menu.
	 *
	 * Called at startup, and whenever the cartridge config list changes.
	 */
	DELEGATE_T0(void) update_cartridge_menu;

	/** \brief Interface to the video module initialised by the UI.
	 */
	struct vo_interface *vo_interface;
};

extern struct ui_module * const *ui_module_list;

void ui_print_vo_help(void);

#endif
