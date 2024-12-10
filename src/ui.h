/** \file
 *
 *  \brief User-interface modules & interfaces.
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

#ifndef XROAR_UI_H_
#define XROAR_UI_H_

#include <stdint.h>

#include "messenger.h"
#include "module.h"
#include "vo.h"
#include "xconfig.h"

struct joystick_module;

struct ui_cfg {
	// File requester
	char *filereq;
	// Video
	char *vo;  // video output module
	struct vo_cfg vo_cfg;
};

// File requesters

typedef DELEGATE_S1(char *, char const *) DELEGATE_T1(charp, charcp);

struct filereq_interface {
	DELEGATE_T0(void) free;
	DELEGATE_T1(charp, charcp) load_filename;
	DELEGATE_T1(charp, charcp) save_filename;
};

extern struct module * const default_filereq_module_list[];

// UI module definitions

struct ui_module {
	struct module common;
	struct module * const *filereq_module_list;
	struct module * const *vo_module_list;
	struct module * const *ao_module_list;
	struct joystick_module * const *joystick_module_list;
};

extern struct ui_module * const *ui_module_list;

/** \brief Interface to UI module.
 */

struct ui_interface {
	DELEGATE_T0(void) free;

	/** \brief UI-specific function providing emulator main loop.
	 *
	 * If not provided, main() should call xroar_run() in a loop.
	 */
	DELEGATE_T0(void) run;

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

	/** \brief Create or update joystick menus.
	 *
	 * Called at startup, and whenever the list of joysticks changes.
	 */
	DELEGATE_T0(void) update_joystick_menus;

	/** \brief Interface to the file requester initialised by the UI.
	 */
	struct filereq_interface *filereq_interface;

	/** \brief Interface to the video module initialised by the UI.
	 */
	struct vo_interface *vo_interface;
};

// UI state messages are sent using the message groups facility.  A message
// group is created per UI tag, and the UI tag is also used as the message
// type.  The message is a simple structure containing an integer value and a
// void pointer to supplementary data.
//
// Typically one listener will preempt the group and handle actually acting on
// the message, potentially altering its contents after sanitising.  GUI
// listeners will then receive the message and update their state based on it.
// Other listeners might want to recieve the messages for other purposes.
//
// Details of how the message is interpreted are listed along with the tag
// definitions below.
//
// Anything described a simple toggle will interpret values of UI_PREV or
// UI_NEXT as a request to toggle, 0 or 1 setting a state explicitly.  Dialog
// toggles open or close a dialog window (where supported), and will be handled
// per UI toolkit.
//
// Anything described as a simple radio selection picks a value from a list
// using positive integers.  UI_PREV/UI_NEXT move back/forward in the list.
// UI_AUTO may pick a sensible default.

#define UI_PREV (-3)
#define UI_NEXT (-2)
#define UI_AUTO (-1)

struct ui_state_message {
	int value;
	const void *data;
};

enum ui_tag {

	ui_tag_action = 1,
	// Simple action.  Not really a UI message, used in some UI modules to
	// assign tags to menu options.

	// Hardware

	ui_tag_machine,
	// value = machine config id
	//
	// Configures new emulated machine.  Handled in xroar.c.

	ui_tag_cartridge,
	// value = cart config id
	// value = UI_PREV/UI_NEXT toggles cartridge enabled
	// value = UI_AUTO tries to find working DOS cart for selected machine
	//
	// Attaches cartridge to current emulated machine.  Handled in xroar.c.

	// Tape

	ui_tag_tape_dialog,
	// Simple toggle for "Cassette tapes" dialog.

	ui_tag_tape_flag_fast,      // fast tape loading
	ui_tag_tape_flag_pad_auto,  // automatic leader padding
	ui_tag_tape_flag_rewrite,   // tape rewrite mode
	// Simple toggles affecting tape options.  Handled in tape.c.

	ui_tag_tape_input_filename,
	ui_tag_tape_output_filename,
	// data  = (const char *) giving the new filename
	//
	// These messages are originated by helper functions in xroar.c, they
	// don't work as a request like most of the others.  Might need
	// rethinking.

	ui_tag_tape_motor,    // tape deck remote motor control
	ui_tag_tape_playing,  // tape deck manual play control
	// Simple toggles affect tape playback

	// Disk

	ui_tag_disk_dialog,
	// Simple toggle for "Floppy/hard disks" dialog.

	ui_tag_disk_new,
	ui_tag_disk_insert,
	ui_tag_disk_eject,
	// Not messages, actions that require data (don't fit as a ui_action).

	ui_tag_disk_data,
	// value = drive number (zero based)
	// data  = (const struct vdisk *) disk structure information
	//
	// This message is originated by helper functions in xroar.c, it
	// doesn't work as a request like most of the others.

	ui_tag_disk_write_enable,
	ui_tag_disk_write_back,
	// data  = (void *)(intptr_t) drive number (zero based)
	//
	// Toggle to change write behaviour for specified drive.

	ui_tag_disk_drive_info,
	// data  = (const struct vdrive_info *) containing information about the
	// current drive, head and cylinder.
	//
	// Originated in vdrive.c, not a request.

	ui_tag_hd_filename,
	// value = drive number
	// data = (const char *) new filename

	// Video

	ui_tag_tv_dialog,
	// Simple toggle for "TV controls" dialog.

	ui_tag_cmp_fs,
	ui_tag_cmp_fsc,
	// Simple radio selections that adjust the ratio between sample rate
	// and colour subcarrier frequency in the "Simulated" composite
	// renderer.  VO_RENDER_FS_* and VO_RENDER_FSC_*.  Handled in
	// vo_render.c.

	ui_tag_cmp_system,
	// Simple radio selection of the composite video system in use.  One of
	// VO_RENDER_SYSTEM_*.  Handled in vo_render.c.

	ui_tag_cmp_colour_killer,
	// Simple toggle controls whether black & white modes are interpreted
	// for cross-colour.

	ui_tag_ccr,
	// Simple radio selection of a cross-colour renderer.  One of
	// VO_CMP_CCR_*.  Handled in vo.c.

	ui_tag_gl_filter,
	// Simple radio selection determining when scaled windows are smoothed
	// using linear filtering.  Handled in vo.c, but the result is used by
	// individual UI toolkits where supported.

	ui_tag_picture,
	// Simple radio selection of a viewport area.  One of VO_PICTURE_*.
	// Handled per-machine, so each can override which makes sense for
	// UI_AUTO (e.g. CoCo 3 prefers a larger visible picture area).

	ui_tag_ntsc_scaling,
	// Simple toggle of whether 60Hz video modes are scaled vertically.
	// Actual 60Hz displays fit fewer lines of the same duration into the
	// same aspect ratio, and so each line appears about 1.2 × the height.
	// Handled in vo_render.c.

	ui_tag_tv_input,
	// Simple radio selection between a set of TV inputs that present the
	// picture in different ways.  One of TV_INPUT_*.  Handled per-machine.
	// Most machines just allow selection between palette-based S-Video or
	// a composite phase.  The CoCo 3 includes an RGB input with a
	// different palette.

	ui_tag_fullscreen,
	// Simple fullscreen toggle.  Handled per UI toolkit.

	ui_tag_menubar,
	// Simple menubar toggle.  Handled per UI toolkit.

	ui_tag_vdg_inverse,
	// Simple inverse text toggle.  Handled per machine.

	ui_tag_brightness,
	ui_tag_contrast,
	ui_tag_saturation,
	ui_tag_hue,
	// Video adjustment controls.  Each accepts a value 0-100 except hue
	// which accepts -179 to +180.  Handled in vr_render.c.

	ui_tag_frameskip,
	// Set frameskip.  Value 0-1000, default is no frameskip (0).  Handled
	// in xroar.c, but the result is used per-machine.  TODO: consider
	// moving.

	ui_tag_zoom,
	// value = UI_PREV zooms out
	// value = UI_NEXT zooms in
	// value = 0 reset zoom level
	// vale  > 0 set zoom level
	//
	// Set zoom level in multiples of half the viewport size.

	// Audio

	ui_tag_gain,
	// value = volume, 0-200 where 100 is normal "full" volume
	// data  = (float *) gain in dBFS (NULL implies to use volume value)
	//
	// Sets the audio gain.  The gain in dBFS is preferred, anything less
	// than -50dBFS implying mute.  Handled in sound.c.

	// Keyboard

	ui_tag_keymap,
	// value = UI_NEXT/UI_PREV toggles between similar layout
	//         (e.g. Dragon & CoCo)
	// value = UI_AUTO for sensible machine default
	// value = one of dkbd_layout_* for specific layout
	//
	// Handled per-machine, configures the emulated keyboard layout.

	ui_tag_hkbd_layout,
	// Simple radio selection between host keyboard layouts.  One of
	// hk_layout_*.  Not very useful yet.  Handled in hkbd.c.

	ui_tag_hkbd_lang,
	// Simple radio selection between host keyboard languages.  One of
	// hk_lang_*.  Useful in case XRoar can't query host key symbol
	// mapping.  Handled in hkbd.c.

	ui_tag_kbd_translate,
	// Simple toggle of keyboard translation.  Handled in hkbd.c.

	// Joysticks

	ui_tag_joystick_port,
	// value = joystick port (0 = right, 1 = left).
	// data  = (void *)(intptr_t) joystick config id.
	//
	// Creates joystick from config with given ID in the specified port.
	// Handled in joystick.c.

	ui_tag_joystick_cycle,
	// value = UI_NEXT cycles virtual joystick right, left, off
	// value = UI_PREV cycles virtual joystick left, right, off
	// value = 1 swaps left and right joysticks

	// Printer

	ui_tag_print_dialog,
	// Simple toggle for "Printer control" dialog.

	ui_tag_print_destination,
	// Simple radio selection between virtual printer destinations.  One of
	// PRINTER_DESTINATION_*.  Handled in printer.c

	ui_tag_print_file,
	// data  = (const char *) filename to use for PRINTER_DESTINATION_FILE.
	// Handled in printer.c.

	ui_tag_print_pipe,   // update print to pipe command
	// data  = (const char *) shell command to use for
	// PRINTER_DESTINATION_PIPE.  Handled in printer.c.

	ui_tag_print_count,
	// Value is a simple count of number of bytes printed since the last
	// flush.  Originated in printer.c, not a request.

	// Debugging, misc

	ui_tag_ratelimit_latch,
	// Simple toggle of rate limit latch.  When the latch is turned off
	// (emulator runs at high speed), the momentary action of
	// ui_tag_ratelimit is ignored.  Handled in xroar.c.

	ui_tag_ratelimit,
	// Value sets rate limiting either on (1) or off (0).  Intended to be a
	// momentary control initiated by input handling, ignored if the rate
	// limit latch is turned off.

	ui_tag_about,
	// Simple toggle for "About" dialog.  Actually, this dialog tends to be
	// treated specially, which is something that might need revisiting.

	ui_tag_config_autosave,
	// Whether configuration is automatically saved on exit.

	ui_num_tags
};

// To fit into the limits of the various UI toolkits in use, tag ids are 7
// bits, and values are 16 bits wide.

// Actions (simple responses to user input) are probably handled internally,
// but enumerate them here:

enum ui_action {
	ui_action_quit,
	ui_action_reset_soft,
	ui_action_reset_hard,
	ui_action_file_load,
	ui_action_file_run,
	ui_action_file_save_snapshot,
	ui_action_file_screenshot,
	ui_action_tape_input,
	ui_action_tape_output,
	ui_action_tape_play_pause,
	ui_action_tape_input_rewind,
	ui_action_tape_output_rewind,
	ui_action_joystick_swap,
	ui_action_print_flush,
};

extern int ui_tag_to_group_id[ui_num_tags];

void ui_init(void);

// Wrappers around messenger_join_group() et al. to join by UI tag
int ui_messenger_join_group(int client_id, int tag, messenger_notify_delegate notify);
int ui_messenger_preempt_group(int client_id, int tag, messenger_notify_delegate notify);

inline void ui_update_state(int client_id, int tag, int value, const void *data) {
	struct ui_state_message msg = { .value = value, .data = data };
	messenger_send_message(ui_tag_to_group_id[tag], client_id, tag, &msg);
}

// UI message receivers that apply configuration take simple integer values.
// This function applies adjustment and range checking to the value in a UI
// message.
//
// UI_PREV and UI_NEXT subtracts or adds 1 to 'cur'.  If UI_ADJUST_FLAG_CYCLE
// is set, the result is cycled through 'min' to 'max' and vice-versa.
// Otherwise the result is clamped.
//
// UI_AUTO sets value to 'dfl'.
//
// Note that if the range includes the special command values, i.e. if 'min' is
// negative, then commands will not take effect.
//
// The new value is returned, and the 'value' field in the UI message is
// updated.  If UI_ADJUST_FLAG_KEEP_AUTO is set, the value of UI_AUTO is kept
// in the message, even though the returned value differs, allowing an
// "automatic" selection to be defined.

#define UI_ADJUST_FLAG_CYCLE     (1 << 0)
#define UI_ADJUST_FLAG_KEEP_AUTO (1 << 1)

int ui_msg_adjust_value_range(struct ui_state_message *, int cur, int dfl,
			      int min, int max, unsigned flags);

#endif
