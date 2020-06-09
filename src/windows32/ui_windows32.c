/*

Windows user-interface module

Copyright 2014-2020 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <windows.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "array.h"
#include "slist.h"
#include "xalloc.h"

#include "cart.h"
#include "events.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "module.h"
#include "sam.h"
#include "tape.h"
#include "ui.h"
#include "vdisk.h"
#include "vo.h"
#include "xroar.h"

#ifdef HAVE_SDL2
#include "sdl2/common.h"
#endif
#include "windows32/common_windows32.h"

#define TAG(t) (((t) & 0x7f) << 8)
#define TAGV(t,v) (TAG(t) | ((v) & 0xff))
#define TAG_TYPE(t) (((t) >> 8) & 0x7f)
#define TAG_VALUE(t) ((t) & 0xff)

static int max_machine_id = 0;
static int max_cartridge_id = 0;

static struct {
	const char *name;
	const char *description;
} const joystick_names[] = {
	{ NULL, "None" },
	{ "joy0", "Joystick 0" },
	{ "joy1", "Joystick 1" },
	{ "kjoy0", "Keyboard" },
	{ "mjoy0", "Mouse" },
};
#define NUM_JOYSTICK_NAMES ARRAY_N_ELEMENTS(joystick_names)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Note: prefer the default order for sound and joystick modules, which
 * will include the SDL options. */

static HMENU top_menu;

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static HWND about_dialog = NULL;
static WNDPROC sdl_window_proc = NULL;
static BOOL CALLBACK about_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void setup_file_menu(void);
static void setup_view_menu(void);
static void setup_hardware_menu(struct ui_sdl2_interface *uisdl2);
static void setup_tool_menu(void);
static void setup_help_menu(void);

void windows32_create_menus(struct ui_sdl2_interface *uisdl2) {
	top_menu = CreateMenu();
	setup_file_menu();
	setup_view_menu();
	setup_hardware_menu(uisdl2);
	setup_tool_menu();
	setup_help_menu();
}

void windows32_destroy_menus(struct ui_sdl2_interface *uisdl2) {
	DestroyMenu(top_menu);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void setup_file_menu(void) {
	HMENU file_menu;
	HMENU submenu;

	file_menu = CreatePopupMenu();

	AppendMenu(file_menu, MF_STRING, TAGV(ui_tag_action, ui_action_file_run), "&Run...");
	AppendMenu(file_menu, MF_STRING, TAGV(ui_tag_action, ui_action_file_load), "&Load...");

	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);

	submenu = CreatePopupMenu();
	AppendMenu(file_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Cassette");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_tape_input), "Input Tape...");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_tape_input_rewind), "Rewind Input Tape");
	AppendMenu(submenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_tape_output), "Output Tape...");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_tape_output_rewind), "Rewind Output Tape");
	AppendMenu(submenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_tape_flags, TAPE_FAST), "Fast Loading");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_tape_flags, TAPE_PAD_AUTO), "CAS padding");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_tape_flags, TAPE_REWRITE), "Rewrite");

	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);

	for (int drive = 0; drive < 4; drive++) {
		char title[9];
		snprintf(title, sizeof(title), "Drive &%c", '1' + drive);
		submenu = CreatePopupMenu();
		AppendMenu(file_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, title);
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_disk_insert, drive), "Insert Disk...");
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_disk_new, drive), "New Disk...");
		AppendMenu(submenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_disk_write_enable, drive), "Write Enable");
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_disk_write_back, drive), "Write Back");
		AppendMenu(submenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_disk_eject, drive), "Eject Disk");
	}

	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, TAGV(ui_tag_action, ui_action_file_save_snapshot), "&Save Snapshot...");
	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, TAGV(ui_tag_action, ui_action_quit), "&Quit");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (uintptr_t)file_menu, "&File");
}

static void setup_view_menu(void) {
	HMENU view_menu;
	HMENU submenu;

	view_menu = CreatePopupMenu();

	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Zoom");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_zoom_in), "Zoom In");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_zoom_out), "Zoom Out");

	AppendMenu(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(view_menu, MF_STRING, TAG(ui_tag_fullscreen), "Full Screen");
	AppendMenu(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(view_menu, MF_STRING, TAG(ui_tag_vdg_inverse), "Inverse Text");

	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Composite Rendering");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_ccr, UI_CCR_SIMPLE), "Simple (2-bit LUT)");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_ccr, UI_CCR_5BIT), "5-bit LUT");
#ifdef WANT_SIMULATED_NTSC
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_ccr, UI_CCR_SIMULATED), "Simulated");
#endif

	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Cross-colour");
	for (int i = 0; vo_ntsc_phase_list[i].name; i++) {
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_cross_colour, vo_ntsc_phase_list[i].value), vo_ntsc_phase_list[i].description);
	}

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (uintptr_t)view_menu, "&View");
}

static void setup_hardware_menu(struct ui_sdl2_interface *uisdl2) {
	HMENU hardware_menu;
	HMENU submenu;

	hardware_menu = CreatePopupMenu();

	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Machine");
	max_machine_id = 0;
	struct slist *mcl = machine_config_list();
	while (mcl) {
		struct machine_config *mc = mcl->data;
		if (mc->id > max_machine_id)
			max_machine_id = mc->id;
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_machine, mc->id), mc->description);
		mcl = mcl->next;
	}

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Cartridge");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_cartridge, 0), "None");
	max_cartridge_id = 0;
	struct slist *ccl = cart_config_list();
	while (ccl) {
		struct cart_config *cc = ccl->data;
		if ((cc->id + 1) > max_cartridge_id)
			max_cartridge_id = cc->id + 1;
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_cartridge, cc->id + 1), cc->description);
		ccl = ccl->next;
	}

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Keyboard Map");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, KEYMAP_DRAGON), "Dragon Layout");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, KEYMAP_DRAGON200E), "Dragon 200-E Layout");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, KEYMAP_COCO), "CoCo Layout");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Right Joystick");
	for (unsigned i = 0; i < NUM_JOYSTICK_NAMES; i++) {
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_joy_right, i), joystick_names[i].description);
	}
	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Left Joystick");
	for (unsigned i = 0; i < NUM_JOYSTICK_NAMES; i++) {
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_joy_left, i), joystick_names[i].description);
	}
	AppendMenu(hardware_menu, MF_STRING, TAGV(ui_tag_action, ui_action_joystick_swap), "Swap Joysticks");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hardware_menu, MF_STRING, TAGV(ui_tag_action, ui_action_reset_soft), "Soft Reset");
	AppendMenu(hardware_menu, MF_STRING, TAGV(ui_tag_action, ui_action_reset_hard), "Hard Reset");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (uintptr_t)hardware_menu, "&Hardware");

	windows32_ui_set_state(uisdl2, ui_tag_machine, xroar_machine_config ? xroar_machine_config->id : 0, NULL);
	struct cart *cart = xroar_machine ? xroar_machine->get_interface(xroar_machine, "cart") : NULL;
	windows32_ui_set_state(uisdl2, ui_tag_cartridge, cart ? cart->config->id : 0, NULL);
}

static void setup_tool_menu(void) {
	HMENU tool_menu;

	tool_menu = CreatePopupMenu();
	AppendMenu(tool_menu, MF_STRING, TAG(ui_tag_kbd_translate), "Keyboard Translation");
	AppendMenu(tool_menu, MF_STRING, TAG(ui_tag_fast_sound), "Fast Sound");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (uintptr_t)tool_menu, "&Tool");
}

static void setup_help_menu(void) {
	HMENU help_menu;

	help_menu = CreatePopupMenu();
	AppendMenu(help_menu, MF_STRING, TAG(ui_tag_about), "About");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (uintptr_t)help_menu, "&Help");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_windows32_handle_syswmevent(SDL_SysWMmsg *wmmsg) {
#ifdef HAVE_SDL2
	HWND hwnd = wmmsg->msg.win.hwnd;
	UINT msg = wmmsg->msg.win.msg;
	WPARAM wParam = wmmsg->msg.win.wParam;
#else
	UINT msg = wmmsg->msg;
	WPARAM wParam = wmmsg->wParam;
#endif

	if (msg != WM_COMMAND)
		return;

	int tag = LOWORD(wParam);
	int tag_type = TAG_TYPE(tag);
	int tag_value = TAG_VALUE(tag);

	switch (tag_type) {

	// Simple actions:
	case ui_tag_action:
		switch (tag_value) {
		case ui_action_quit:
			{
				SDL_Event event;
				event.type = SDL_QUIT;
				SDL_PushEvent(&event);
			}
			break;
		case ui_action_reset_soft:
			xroar_soft_reset();
			break;
		case ui_action_reset_hard:
			xroar_hard_reset();
			break;
		case ui_action_file_run:
			xroar_run_file(NULL);
			break;
		case ui_action_file_load:
			xroar_load_file(NULL);
			break;
		case ui_action_file_save_snapshot:
			xroar_save_snapshot();
			break;
		case ui_action_tape_input:
			xroar_insert_input_tape();
			break;
		case ui_action_tape_input_rewind:
			if (xroar_tape_interface && xroar_tape_interface->tape_input)
				tape_rewind(xroar_tape_interface->tape_input);
			break;
		case ui_action_tape_output:
			xroar_insert_output_tape();
			break;
		case ui_action_tape_output_rewind:
			if (xroar_tape_interface && xroar_tape_interface->tape_output)
				tape_rewind(xroar_tape_interface->tape_output);
			break;
		case ui_action_zoom_in:
			sdl_zoom_in(global_uisdl2);
			break;
		case ui_action_zoom_out:
			sdl_zoom_out(global_uisdl2);
			break;
		case ui_action_joystick_swap:
			xroar_swap_joysticks(1);
			break;
		default:
			break;
		}
		break;

	// Machines:
	case ui_tag_machine:
		xroar_set_machine(1, tag_value);
		break;

	// Cartridges:
	case ui_tag_cartridge:
		{
			struct cart_config *cc = cart_config_by_id(tag_value - 1);
			xroar_set_cart(1, cc ? cc->name : NULL);
		}
		break;

	// Cassettes:
	case ui_tag_tape_flags:
		tape_select_state(xroar_tape_interface, tape_get_state(xroar_tape_interface) ^ tag_value);
		break;

	// Disks:
	case ui_tag_disk_insert:
		xroar_insert_disk(tag_value);
		break;
	case ui_tag_disk_new:
		xroar_new_disk(tag_value);
		break;
	case ui_tag_disk_write_enable:
		xroar_set_write_enable(1, tag_value, XROAR_NEXT);
		break;
	case ui_tag_disk_write_back:
		xroar_set_write_back(1, tag_value, XROAR_NEXT);
		break;
	case ui_tag_disk_eject:
		xroar_eject_disk(tag_value);
		break;

	// Video:
	case ui_tag_fullscreen:
		xroar_set_fullscreen(1, XROAR_NEXT);
		break;
	case ui_tag_ccr:
		xroar_set_cross_colour_renderer(1, tag_value);
		break;
	case ui_tag_cross_colour:
		xroar_set_cross_colour(1, tag_value);
		break;
	case ui_tag_vdg_inverse:
		xroar_set_vdg_inverted_text(1, XROAR_NEXT);
		break;

	// Audio:
	case ui_tag_fast_sound:
		xroar_set_fast_sound(1, !xroar_cfg.fast_sound);
		break;

	// Keyboard:
	case ui_tag_keymap:
		xroar_set_keymap(1, tag_value);
		break;
	case ui_tag_kbd_translate:
		xroar_set_kbd_translate(1, XROAR_NEXT);
		break;

	// Joysticks:
	case ui_tag_joy_right:
		xroar_set_joystick(1, 0, joystick_names[tag_value].name);
		break;
	case ui_tag_joy_left:
		xroar_set_joystick(1, 1, joystick_names[tag_value].name);
		break;

	// Help:
	case ui_tag_about:
		if (!IsWindow(about_dialog)) {
			about_dialog = CreateDialog(NULL, MAKEINTRESOURCE(1), hwnd, (DLGPROC)about_proc);
			if (about_dialog) {
				ShowWindow(about_dialog, SW_SHOW);
			}
		}
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void windows32_ui_set_state(void *sptr, int tag, int value, const void *data) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	switch (tag) {

	// Simple toggles

	case ui_tag_fullscreen:
	case ui_tag_vdg_inverse:
	case ui_tag_fast_sound:
		CheckMenuItem(top_menu, TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Hardware

	case ui_tag_machine:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, max_machine_id), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_cartridge:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, max_cartridge_id), TAGV(tag, value + 1), MF_BYCOMMAND);
		break;

	// Tape

	case ui_tag_tape_flags:
		for (int i = 0; i < 4; i++) {
			int f = value & (1 << i);
			int t = TAGV(tag, 1 << i);
			CheckMenuItem(top_menu, t, MF_BYCOMMAND | (f ? MF_CHECKED : MF_UNCHECKED));
		}
		break;

	// Disk

	case ui_tag_disk_data:
		{
			const struct vdisk *disk = data;
			_Bool we = 1, wb = 0;
			if (disk) {
				we = !disk->write_protect;
				wb = disk->write_back;
			}
			windows32_ui_set_state(uisdl2, ui_tag_disk_write_enable, value, (void *)(intptr_t)we);
			windows32_ui_set_state(uisdl2, ui_tag_disk_write_back, value, (void *)(intptr_t)wb);
		}
		break;

	case ui_tag_disk_write_enable:
		CheckMenuItem(top_menu, TAGV(tag, value), MF_BYCOMMAND | (data ? MF_CHECKED : MF_UNCHECKED));
		break;

	case ui_tag_disk_write_back:
		CheckMenuItem(top_menu, TAGV(tag, value), MF_BYCOMMAND | (data ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Video

	case ui_tag_ccr:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, 2), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_cross_colour:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, 2), TAGV(tag, value), MF_BYCOMMAND);
		break;

	// Keyboard

	case ui_tag_keymap:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, (NUM_KEYMAPS - 1)), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_kbd_translate:
		CheckMenuItem(top_menu, TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		uisdl2->keyboard.translate = value;
		break;

	// Joysticks

	case ui_tag_joy_right:
	case ui_tag_joy_left:
		{
			int joy = 0;
			if (data) {
				for (unsigned i = 1; i < NUM_JOYSTICK_NAMES; i++) {
					if (0 == strcmp((const char *)data, joystick_names[i].name)) {
						joy = i;
						break;
					}
				}
			}
			CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, NUM_JOYSTICK_NAMES - 1), TAGV(tag, joy), MF_BYCOMMAND);
		}
		break;

	default:
		break;

	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* SDL integration.  The SDL and SDL2 video modules call out to these when
 * WINDOWS32 is defined to add and remove the menu bar.  Note that SDL 1.2 only
 * handles one window, so the SDL_Window type is faked, and arguments of that
 * type are not used. */

/* Get underlying window handle from SDL. */

static HWND get_hwnd(SDL_Window *w) {
	SDL_version sdlver;
	SDL_SysWMinfo sdlinfo;
	SDL_VERSION(&sdlver);
	sdlinfo.version = sdlver;
#ifdef HAVE_SDL2
	SDL_GetWindowWMInfo(w, &sdlinfo);
	return sdlinfo.info.win.window;
#else
	(void)w;
	SDL_GetWMInfo(&sdlinfo);
	return sdlinfo.window;
#endif
}

/* Custom window event handler to intercept menu selections. */

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	SDL_Event event;
	SDL_SysWMmsg wmmsg;

	switch (msg) {

	case WM_COMMAND:
		// Selectively push WM events onto the SDL queue.
#ifdef HAVE_SDL2
		wmmsg.msg.win.hwnd = hwnd;
		wmmsg.msg.win.msg = msg;
		wmmsg.msg.win.wParam = wParam;
		wmmsg.msg.win.lParam = lParam;
#else
		wmmsg.hwnd = hwnd;
		wmmsg.msg = msg;
		wmmsg.wParam = wParam;
		wmmsg.lParam = lParam;
#endif
		event.type = SDL_SYSWMEVENT;
		event.syswm.msg = &wmmsg;
		SDL_PushEvent(&event);
		break;

	case WM_UNINITMENUPOPUP:
		DELEGATE_SAFE_CALL0(xroar_vo_interface->refresh);
		return CallWindowProc(sdl_window_proc, hwnd, msg, wParam, lParam);

	default:
		// Fall back to original SDL handler for anything else -
		// SysWMEvent handling is not enabled, so this should not flood
		// the queue.
		return CallWindowProc(sdl_window_proc, hwnd, msg, wParam, lParam);

	}
	return 0;
}

/* While the menu is being navigated, the main application is blocked. If event
 * processing is enabled for SysWMEvent, SDL quickly runs out of space in its
 * event queue, leading to the ultimate menu option often being missed.  This
 * sets up a custom Windows event handler that pushes a SDL_SysWMEvent only for
 * WM_COMMAND messages. */

void sdl_windows32_set_events_window(SDL_Window *sw) {
	HWND hwnd = get_hwnd(sw);
	WNDPROC old_window_proc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
	if (old_window_proc != window_proc) {
		// Preserve SDL's "windowproc"
		sdl_window_proc = old_window_proc;
		// Set my own to process wm events.  Without this, the windows menu
		// blocks and the internal SDL event queue overflows, causing missed
		// selections.
		SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)window_proc);
		// Explicitly disable SDL processing of these events
		SDL_EventState(SDL_SYSWMEVENT, SDL_DISABLE);
	}
	windows32_main_hwnd = hwnd;
}

/* Add menubar to window. This will reduce the size of the client area while
 * leaving the window size the same, so the video module should then resize
 * itself to account for this. */

void sdl_windows32_add_menu(SDL_Window *sw) {
	HWND hwnd = get_hwnd(sw);
	SetMenu(hwnd, top_menu);
}

/* Remove menubar from window. */

void sdl_windows32_remove_menu(SDL_Window *sw) {
	HWND hwnd = get_hwnd(sw);
	SetMenu(hwnd, NULL);
}

static BOOL CALLBACK about_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			DestroyWindow(hwnd);
			about_dialog = NULL;
			return TRUE;

		default:
			break;
		}

	default:
		break;
	}
	return FALSE;
}
