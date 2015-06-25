/*

XRoar, Dragon and Tandy CoCo 1/2 emulator
Copyright 2003-2015 Ciaran Anscomb

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/

#include "config.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "array.h"
#include "slist.h"

#include "cart.h"
#include "events.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6847.h"
#include "module.h"
#include "sam.h"
#include "tape.h"
#include "ui.h"
#include "vdisk.h"
#include "xroar.h"

#include "sdl/common.h"
#include "windows32/common_windows32.h"

#ifdef STRICT
#define WNDPROCTYPE WNDPROC
#else
#define WNDPROCTYPE FARPROC
#endif

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

static _Bool init(void);
static void ui_shutdown(void);
static void set_state(enum ui_tag tag, int value, const void *data);

/* Note: prefer the default order for sound and joystick modules, which
 * will include the SDL options. */

struct ui_module ui_windows32_module = {
	.common = { .name = "windows32", .description = "Windows SDL UI",
	            .init = init, .shutdown = ui_shutdown },
	.video_module_list = sdl_video_module_list,
	.joystick_module_list = sdl_js_modlist,
	.run = sdl_run,
	.set_state = set_state,
};

static HMENU top_menu;

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static WNDPROCTYPE sdl_window_proc = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void setup_file_menu(void);
static void setup_view_menu(void);
static void setup_hardware_menu(void);
static void setup_tool_menu(void);

static _Bool init(void) {
	if (!getenv("SDL_VIDEODRIVER"))
		putenv("SDL_VIDEODRIVER=windib");

	if (!SDL_WasInit(SDL_INIT_NOPARACHUTE)) {
		if (SDL_Init(SDL_INIT_NOPARACHUTE) < 0) {
			LOG_ERROR("Failed to initialise SDL: %s\n", SDL_GetError());
			return 0;
		}
	}
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
		LOG_ERROR("Failed to initialise SDL video: %s\n", SDL_GetError());
		return 0;
	}

	SDL_version sdlver;
	SDL_SysWMinfo sdlinfo;
	SDL_VERSION(&sdlver);
	sdlinfo.version = sdlver;
	SDL_GetWMInfo(&sdlinfo);
	windows32_main_hwnd = sdlinfo.window;

	// Preserve SDL's "windowproc"
	sdl_window_proc = (WNDPROCTYPE)GetWindowLongPtr(windows32_main_hwnd, GWLP_WNDPROC);

	// Set my own to process wm events.  Without this, the windows menu
	// blocks and the internal SDL event queue overflows, causing missed
	// selections.
	SetWindowLongPtr(windows32_main_hwnd, GWLP_WNDPROC, (LONG_PTR)window_proc);

	// Explicitly disable SDL processing of these events
	SDL_EventState(SDL_SYSWMEVENT, SDL_DISABLE);

	top_menu = CreateMenu();
	setup_file_menu();
	setup_view_menu();
	setup_hardware_menu();
	setup_tool_menu();

	sdl_keyboard_init();

	return 1;
}

static void ui_shutdown(void) {
	DestroyMenu(top_menu);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void sdl_windows32_update_menu(_Bool fullscreen) {
	if (fullscreen) {
		SetMenu(windows32_main_hwnd, NULL);
	} else {
		SetMenu(windows32_main_hwnd, top_menu);
	}
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
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_tape_output), "Output Tape...");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_tape_input_rewind), "Rewind Input Tape");
	AppendMenu(submenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_tape_flags, TAPE_FAST), "Fast Loading");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_tape_flags, TAPE_PAD), "Leader Padding");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_tape_flags, TAPE_PAD_AUTO), "Automatic Padding");
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
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (uintptr_t)submenu, "Cross-colour");
	for (int i = 0; xroar_cross_colour_list[i].name; i++) {
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_cross_colour, xroar_cross_colour_list[i].value), xroar_cross_colour_list[i].description);
	}

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (uintptr_t)view_menu, "&View");
}

static void setup_hardware_menu(void) {
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

	set_state(ui_tag_machine, xroar_machine_config ? xroar_machine_config->id : 0, NULL);
	set_state(ui_tag_cartridge, machine_cart ? machine_cart->config->id : 0, NULL);
}

static void setup_tool_menu(void) {
	HMENU tool_menu;

	tool_menu = CreatePopupMenu();
	AppendMenu(tool_menu, MF_STRING, TAG(ui_tag_kbd_translate), "Keyboard Translation");
	AppendMenu(tool_menu, MF_STRING, TAG(ui_tag_fast_sound), "Fast Sound");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (uintptr_t)tool_menu, "&Tool");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	SDL_Event event;
	SDL_SysWMmsg wmmsg;

	switch (msg) {

	case WM_COMMAND:
		/* Selectively push WM events onto the SDL queue */
		wmmsg.hwnd = hwnd;
		wmmsg.msg = msg;
		wmmsg.wParam = wParam;
		wmmsg.lParam = lParam;
		event.type = SDL_SYSWMEVENT;
		event.syswm.msg = &wmmsg;
		SDL_PushEvent(&event);
		break;

	default:
		/* Fall back to original SDL handler */
		return sdl_window_proc(hwnd, msg, wParam, lParam);

	}
	return 0;
}

void sdl_windows32_handle_syswmevent(void *data) {
	SDL_SysWMmsg *wmmsg = data;
	int msg = wmmsg->msg;
	int tag = LOWORD(wmmsg->wParam);
	int tag_type = TAG_TYPE(tag);
	int tag_value = TAG_VALUE(tag);

	switch (msg) {

	case WM_COMMAND:
		switch (tag_type) {

		/* Simple actions: */
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
				xroar_select_tape_input();
				break;
			case ui_action_tape_output:
				xroar_select_tape_output();
				break;
			case ui_action_tape_input_rewind:
				if (tape_input)
					tape_rewind(tape_input);
				break;
			case ui_action_zoom_in:
				sdl_zoom_in();
				break;
			case ui_action_zoom_out:
				sdl_zoom_out();
				break;
			case ui_action_joystick_swap:
				xroar_swap_joysticks(1);
				break;
			default:
				break;
			}
			break;

		/* Machines: */
		case ui_tag_machine:
			xroar_set_machine(1, tag_value);
			break;

		/* Cartridges: */
		case ui_tag_cartridge:
			{
				struct cart_config *cc = cart_config_by_id(tag_value - 1);
				xroar_set_cart(1, cc ? cc->name : NULL);
			}
			break;

		/* Cassettes: */
		case ui_tag_tape_flags:
			tape_select_state(tape_get_state() ^ tag_value);
			break;

		/* Disks: */
		case ui_tag_disk_insert:
			xroar_insert_disk(tag_value);
			break;
		case ui_tag_disk_new:
			xroar_new_disk(tag_value);
			break;
		case ui_tag_disk_write_enable:
			xroar_set_write_enable(1, tag_value, XROAR_TOGGLE);
			break;
		case ui_tag_disk_write_back:
			xroar_set_write_back(1, tag_value, XROAR_TOGGLE);
			break;
		case ui_tag_disk_eject:
			xroar_eject_disk(tag_value);
			break;

		/* Video: */
		case ui_tag_fullscreen:
			xroar_set_fullscreen(1, XROAR_TOGGLE);
			break;
		case ui_tag_cross_colour:
			xroar_set_cross_colour(1, tag_value);
			break;
		case ui_tag_vdg_inverse:
			xroar_set_vdg_inverted_text(1, XROAR_TOGGLE);
			break;
		/* Audio: */
		case ui_tag_fast_sound:
			machine_select_fast_sound(!xroar_cfg.fast_sound);
			break;

		/* Keyboard: */
		case ui_tag_keymap:
			xroar_set_keymap(1, tag_value);
			break;
		case ui_tag_kbd_translate:
			xroar_set_kbd_translate(1, XROAR_TOGGLE);
			break;

		/* Joysticks: */
		case ui_tag_joy_right:
			xroar_set_joystick(1, 0, joystick_names[tag_value].name);
			break;
		case ui_tag_joy_left:
			xroar_set_joystick(1, 1, joystick_names[tag_value].name);
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void set_state(enum ui_tag tag, int value, const void *data) {
	switch (tag) {

	/* Simple toggles */

	case ui_tag_fullscreen:
	case ui_tag_vdg_inverse:
	case ui_tag_fast_sound:
		CheckMenuItem(top_menu, TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	/* Hardware */

	case ui_tag_machine:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, max_machine_id), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_cartridge:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, max_cartridge_id), TAGV(tag, value + 1), MF_BYCOMMAND);
		break;

	/* Tape */

	case ui_tag_tape_flags:
		for (int i = 0; i < 4; i++) {
			int f = value & (1 << i);
			int t = TAGV(tag, 1 << i);
			CheckMenuItem(top_menu, t, MF_BYCOMMAND | (f ? MF_CHECKED : MF_UNCHECKED));
		}
		break;

	/* Disk */

	case ui_tag_disk_data:
		{
			const struct vdisk *disk = data;
			_Bool we = 1, wb = 0;
			if (disk) {
				we = !disk->write_protect;
				wb = disk->write_back;
			}
			set_state(ui_tag_disk_write_enable, value, (void *)(intptr_t)we);
			set_state(ui_tag_disk_write_back, value, (void *)(intptr_t)wb);
		}
		break;

	case ui_tag_disk_write_enable:
		CheckMenuItem(top_menu, TAGV(tag, value), MF_BYCOMMAND | (data ? MF_CHECKED : MF_UNCHECKED));
		break;

	case ui_tag_disk_write_back:
		CheckMenuItem(top_menu, TAGV(tag, value), MF_BYCOMMAND | (data ? MF_CHECKED : MF_UNCHECKED));
		break;

	/* Video */

	case ui_tag_cross_colour:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, 2), TAGV(tag, value), MF_BYCOMMAND);
		break;

	/* Keyboard */

	case ui_tag_keymap:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, (NUM_KEYMAPS - 1)), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_kbd_translate:
		CheckMenuItem(top_menu, TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		sdl_keyboard_set_translate(value);
		break;

	/* Joysticks */

	case ui_tag_joy_right:
	case ui_tag_joy_left:
		{
			int joy = 0;
			if (data) {
				for (int i = 1; i < NUM_JOYSTICK_NAMES; i++) {
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
