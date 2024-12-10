/** \file
 *
 *  \brief Windows user-interface module.
 *
 *  \copyright Copyright 2014-2024 Ciaran Anscomb
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

#include "top-config.h"

#include <windows.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <commctrl.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "array.h"
#include "slist.h"
#include "xalloc.h"

#include "ao.h"
#include "cart.h"
#include "events.h"
#include "hkbd.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "messenger.h"
#include "module.h"
#include "sound.h"
#include "tape.h"
#include "ui.h"
#include "vdisk.h"
#include "vo.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialog.h"
#include "windows32/drivecontrol.h"
#include "windows32/printercontrol.h"
#include "windows32/resources.h"
#include "windows32/tapecontrol.h"
#include "windows32/video_options.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static WNDPROC sdl_window_proc = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void *ui_windows32_new(void *cfg);
static void ui_windows32_free(void *);

struct ui_module ui_windows32_module = {
	.common = { .name = "windows32", .description = "Windows32 SDL2 UI",
		.new = ui_windows32_new,
	},
	.joystick_module_list = sdl_js_modlist,
};

static void windows32_create_menus(struct ui_windows32_interface *);
static void windows32_update_machine_menu(void *);
static void windows32_update_cartridge_menu(void *);
static void windows32_update_joystick_menus(void *);

static void uiw32_ui_state_notify(void *, int tag, void *smsg);

static void *ui_windows32_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;

	struct ui_windows32_interface *uiw32 = (struct ui_windows32_interface *)ui_sdl_allocate(sizeof(*uiw32));
	if (!uiw32) {
		return NULL;
	}
	*uiw32 = (struct ui_windows32_interface){0};
	struct ui_sdl2_interface *uisdl2 = &uiw32->ui_sdl2_interface;
	ui_sdl_init(uisdl2, ui_cfg);
	struct ui_interface *ui = &uisdl2->ui_interface;
	ui->free = DELEGATE_AS0(void, ui_windows32_free, uiw32);
	ui->update_machine_menu = DELEGATE_AS0(void, windows32_update_machine_menu, uiw32);
	ui->update_cartridge_menu = DELEGATE_AS0(void, windows32_update_cartridge_menu, uiw32);
	ui->update_joystick_menus = DELEGATE_AS0(void, windows32_update_joystick_menus, uiw32);

	// Register with messenger
	uiw32->msgr_client_id = messenger_client_register();

	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_machine, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_cartridge, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_tape_dialog, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_disk_dialog, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_tv_dialog, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_ccr, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_fullscreen, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_vdg_inverse, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_keymap, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_hkbd_layout, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_hkbd_lang, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_kbd_translate, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_print_dialog, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_ratelimit_latch, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_joystick_port, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));
	ui_messenger_join_group(uiw32->msgr_client_id, ui_tag_config_autosave, MESSENGER_NOTIFY_DELEGATE(uiw32_ui_state_notify, uiw32));

	windows32_create_menus(uiw32);

	if (!sdl_vo_init(uisdl2)) {
		ui_windows32_free(uiw32);
		return NULL;
	}

	windows32_update_machine_menu(uiw32);
	windows32_update_cartridge_menu(uiw32);
	windows32_update_joystick_menus(uiw32);

	return uiw32;
}

static void ui_windows32_free(void *sptr) {
	struct ui_windows32_interface *uiw32 = sptr;
	uiw32_dialog_shutdown();
	messenger_client_unregister(uiw32->msgr_client_id);
	DestroyMenu(uiw32->top_menu);
	ui_sdl_free(uiw32);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void setup_file_menu(struct ui_windows32_interface *);
static void setup_view_menu(struct ui_windows32_interface *);
static void setup_hardware_menu(struct ui_windows32_interface *);
static void setup_tool_menu(struct ui_windows32_interface *);
static void setup_help_menu(struct ui_windows32_interface *);

static void windows32_create_menus(struct ui_windows32_interface *uiw32) {
	uiw32->top_menu = CreateMenu();
	setup_file_menu(uiw32);
	setup_view_menu(uiw32);
	setup_hardware_menu(uiw32);
	setup_tool_menu(uiw32);
	setup_help_menu(uiw32);
	(void)uiw32_dc_dialog_new(uiw32);
	(void)uiw32_pc_dialog_new(uiw32);
	(void)uiw32_tc_dialog_new(uiw32);
	(void)uiw32_tv_dialog_new(uiw32);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void setup_file_menu(struct ui_windows32_interface *uiw32) {
	HMENU file_menu;

	file_menu = CreatePopupMenu();

	AppendMenu(file_menu, MF_STRING, UIW32_TAGV(ui_tag_action, ui_action_file_run), "&Run...");
	AppendMenu(file_menu, MF_STRING, UIW32_TAGV(ui_tag_action, ui_action_file_load), "&Load...");

	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, UIW32_TAG(ui_tag_tape_dialog), "Cassette &tapes");

	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, UIW32_TAG(ui_tag_disk_dialog), "Floppy &disks");

	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, UIW32_TAG(ui_tag_print_dialog), "&Printer control");

	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, UIW32_TAGV(ui_tag_action, ui_action_file_save_snapshot), "&Save snapshot...");
#ifdef SCREENSHOT
	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, UIW32_TAGV(ui_tag_action, ui_action_file_screenshot), "Screenshot to PNG...");
#endif
	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, UIW32_TAG(uiw32_tag_config_save), "Save &configuration");
	AppendMenu(file_menu, MF_STRING, UIW32_TAG(ui_tag_config_autosave), "&Autosave configuration");
	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, UIW32_TAGV(ui_tag_action, ui_action_quit), "&Quit");

	AppendMenu(uiw32->top_menu, MF_STRING | MF_POPUP, (UINT_PTR)file_menu, "&File");
}

static void setup_view_menu(struct ui_windows32_interface *uiw32) {
	HMENU view_menu;
	HMENU submenu;

	view_menu = CreatePopupMenu();

	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "&TV input");
	uiw32_update_radio_menu_from_enum(submenu, machine_tv_input_list, ui_tag_tv_input);

	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Composite &rendering");
	uiw32_update_radio_menu_from_enum(submenu, vo_cmp_ccr_list, ui_tag_ccr);

	AppendMenu(view_menu, MF_STRING, UIW32_TAG(ui_tag_tv_dialog), "TV &controls");

	AppendMenu(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(view_menu, MF_STRING, UIW32_TAG(ui_tag_vdg_inverse), "&Inverse text");

	AppendMenu(view_menu, MF_SEPARATOR, 0, NULL);
	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Zoom");
	AppendMenu(submenu, MF_STRING, UIW32_TAGV(ui_tag_zoom, UI_NEXT), "Zoom In");
	AppendMenu(submenu, MF_STRING, UIW32_TAGV(ui_tag_zoom, UI_PREV), "Zoom Out");
	AppendMenu(submenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(submenu, MF_STRING, UIW32_TAGV(ui_tag_zoom, 0), "Reset");

	AppendMenu(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(view_menu, MF_STRING, UIW32_TAG(ui_tag_fullscreen), "&Full screen");

	AppendMenu(uiw32->top_menu, MF_STRING | MF_POPUP, (UINT_PTR)view_menu, "&View");
}

static void setup_hardware_menu(struct ui_windows32_interface *uiw32) {
	HMENU hardware_menu;
	HMENU submenu;

	hardware_menu = CreatePopupMenu();

	uiw32->machine_menu = submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Machine");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	uiw32->cartridge_menu = submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Cartridge");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Keyboard type");
	uiw32_update_radio_menu_from_enum(submenu, machine_keyboard_list, ui_tag_keymap);

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	uiw32->right_joystick_menu = submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Right joystick");
	uiw32->left_joystick_menu = submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Left joystick");
	AppendMenu(hardware_menu, MF_STRING, UIW32_TAGV(ui_tag_action, ui_action_joystick_swap), "Swap joysticks");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hardware_menu, MF_STRING, UIW32_TAGV(ui_tag_action, ui_action_reset_soft), "Soft reset");
	AppendMenu(hardware_menu, MF_STRING, UIW32_TAGV(ui_tag_action, ui_action_reset_hard), "Hard reset");

	AppendMenu(uiw32->top_menu, MF_STRING | MF_POPUP, (UINT_PTR)hardware_menu, "&Hardware");
}

static void setup_tool_menu(struct ui_windows32_interface *uiw32) {
	HMENU tool_menu;
	HMENU submenu;

	tool_menu = CreatePopupMenu();

	submenu = CreatePopupMenu();
	AppendMenu(tool_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Keyboard la&yout");
	uiw32_update_radio_menu_from_enum(submenu, hkbd_layout_list, ui_tag_hkbd_layout);

	submenu = CreatePopupMenu();
	AppendMenu(tool_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Keyboard lan&guage");
	uiw32_update_radio_menu_from_enum(submenu, hkbd_lang_list, ui_tag_hkbd_lang);

	AppendMenu(tool_menu, MF_STRING, UIW32_TAG(ui_tag_kbd_translate), "&Keyboard translation");
	AppendMenu(tool_menu, MF_STRING, UIW32_TAG(ui_tag_ratelimit_latch), "&Rate limit");

	AppendMenu(uiw32->top_menu, MF_STRING | MF_POPUP, (UINT_PTR)tool_menu, "&Tool");
}

static void setup_help_menu(struct ui_windows32_interface *uiw32) {
	HMENU help_menu;

	help_menu = CreatePopupMenu();
	AppendMenu(help_menu, MF_STRING, UIW32_TAG(ui_tag_about), "About");

	AppendMenu(uiw32->top_menu, MF_STRING | MF_POPUP, (UINT_PTR)help_menu, "&Help");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void windows32_update_machine_menu(void *sptr) {
	struct ui_windows32_interface *uiw32 = sptr;
	(void)uiw32;

	// Get list of machine configs
	struct slist *mcl = machine_config_list();
	// Note: this list is not a copy, so does not need freeing

	// Remove old entries
	while (DeleteMenu(uiw32->machine_menu, 0, MF_BYPOSITION))
		;

	// Add new entries
	uiw32->max_machine_id = 0;
	while (mcl) {
		struct machine_config *mc = mcl->data;
		if (mc->id > uiw32->max_machine_id) {
			uiw32->max_machine_id = mc->id;
		}
		AppendMenu(uiw32->machine_menu, MF_STRING, UIW32_TAGV(ui_tag_machine, mc->id), mc->description);
		mcl = mcl->next;
	}
}

static void windows32_update_cartridge_menu(void *sptr) {
	struct ui_windows32_interface *uiw32 = sptr;
	(void)uiw32;

	// Get list of cart configs
	struct slist *ccl = NULL;
	if (xroar.machine) {
		const struct machine_partdb_entry *mpe = (const struct machine_partdb_entry *)xroar.machine->part.partdb;
		const char *cart_arch = mpe->cart_arch;
		ccl = cart_config_list_is_a(cart_arch);
	}

	// Remove old entries
	while (DeleteMenu(uiw32->cartridge_menu, 0, MF_BYPOSITION))
		;

	// Add new entries
	AppendMenu(uiw32->cartridge_menu, MF_STRING, UIW32_TAGV(ui_tag_cartridge, 0), "None");
	uiw32->max_cartridge_id = 0;
	for (struct slist *iter = ccl; iter; iter = iter->next) {
		struct cart_config *cc = iter->data;
		if (cc->id > uiw32->max_cartridge_id) {
			uiw32->max_cartridge_id = cc->id;
		}
		AppendMenu(uiw32->cartridge_menu, MF_STRING, UIW32_TAGV(ui_tag_cartridge, cc->id), cc->description);
	}
	slist_free(ccl);
}

static void windows32_update_joystick_menus(void *sptr) {
	struct ui_windows32_interface *uiw32 = sptr;

	// Get list of joystick configs
	struct slist *jl = joystick_config_list();

	// Remove old entries
	while (DeleteMenu(uiw32->right_joystick_menu, 0, MF_BYPOSITION))
		;
	while (DeleteMenu(uiw32->left_joystick_menu, 0, MF_BYPOSITION))
		;

	AppendMenu(uiw32->right_joystick_menu, MF_STRING, UIW32_TAGV(uiw32_tag_joystick_right, 0), "None");
	AppendMenu(uiw32->left_joystick_menu, MF_STRING, UIW32_TAGV(uiw32_tag_joystick_left, 0), "None");
	uiw32->max_joystick_id = 0;
	for (struct slist *iter = jl; iter; iter = iter->next) {
		struct joystick_config *jc = iter->data;
		if (jc->id > uiw32->max_joystick_id) {
			uiw32->max_joystick_id = jc->id;
		}
		AppendMenu(uiw32->right_joystick_menu, MF_STRING, UIW32_TAGV(uiw32_tag_joystick_right, jc->id), jc->description);
		AppendMenu(uiw32->left_joystick_menu, MF_STRING, UIW32_TAGV(uiw32_tag_joystick_left, jc->id), jc->description);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Field WM_COMMAND events generated by menu selections.
//
// Unlike GTK+, selecting a checkbox (say) doesn't actually update Windows32 UI
// state, so we _want_ the notification to come back to us.  Thus calls to
// ui_update_state() should use -1 as the sender, and calls to older mechanisms
// should set notify to true.

void sdl_windows32_handle_syswmevent(struct ui_sdl2_interface *uisdl2, SDL_SysWMmsg *wmmsg) {
	struct ui_windows32_interface *uiw32 = (struct ui_windows32_interface *)uisdl2;

	UINT msg = wmmsg->msg.win.msg;
	WPARAM wParam = wmmsg->msg.win.wParam;

	if (msg != WM_COMMAND)
		return;

	int tag = LOWORD(wParam);
	int tag_type = UIW32_TAG_TYPE(tag);
	int tag_value = UIW32_TAG_VALUE(tag);

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
			xroar_run_file();
			break;
		case ui_action_file_load:
			xroar_load_file();
			break;
		case ui_action_file_save_snapshot:
			xroar_save_snapshot();
			break;
#ifdef SCREENSHOT
		case ui_action_file_screenshot:
			xroar_screenshot();
			break;
#endif

		case ui_action_joystick_swap:
			ui_update_state(-1, ui_tag_joystick_cycle, 1, NULL);
			break;
		default:
			break;
		}
		break;

	// Configuration:
	case uiw32_tag_config_save:
		xroar_save_config_file();
		break;
	case ui_tag_config_autosave:
		ui_update_state(-1, ui_tag_config_autosave, UI_NEXT, NULL);
		break;

	// Machines:
	case ui_tag_machine:
		ui_update_state(-1, ui_tag_machine, tag_value, NULL);
		break;

	// Cartridges:
	case ui_tag_cartridge:
		ui_update_state(-1, ui_tag_cartridge, tag_value, NULL);
		break;

	// Cassettes:
	case ui_tag_tape_dialog:
		ui_update_state(-1, ui_tag_tape_dialog, UI_NEXT, NULL);
		break;

	// Floppy disks:
	case ui_tag_disk_dialog:
		ui_update_state(-1, ui_tag_disk_dialog, UI_NEXT, NULL);
		break;

	// Video:

	// TV controls:
	case ui_tag_tv_dialog:
		ui_update_state(-1, ui_tag_tv_dialog, UI_NEXT, NULL);
		break;

	case ui_tag_fullscreen:
		ui_update_state(-1, ui_tag_fullscreen, UI_NEXT, NULL);
		break;
	case ui_tag_ccr:
		ui_update_state(-1, ui_tag_ccr, tag_value, NULL);
		break;
	case ui_tag_tv_input:
		ui_update_state(-1, ui_tag_tv_input, tag_value, NULL);
		break;
	case ui_tag_vdg_inverse:
		ui_update_state(-1, ui_tag_vdg_inverse, UI_NEXT, NULL);
		break;

	case ui_tag_zoom:
		ui_update_state(-1, tag_type, tag_value, NULL);
		break;

	// Keyboard:
	case ui_tag_hkbd_layout:
		ui_update_state(-1, ui_tag_hkbd_layout, tag_value, NULL);
		break;
	case ui_tag_hkbd_lang:
		ui_update_state(-1, ui_tag_hkbd_lang, tag_value, NULL);
		break;
	case ui_tag_keymap:
		ui_update_state(-1, ui_tag_keymap, tag_value, NULL);
		break;
	case ui_tag_kbd_translate:
		ui_update_state(-1, ui_tag_kbd_translate, UI_NEXT, NULL);
		break;

	// Joysticks

	case uiw32_tag_joystick_right:
		ui_update_state(-1, ui_tag_joystick_port, 0, (void *)(intptr_t)tag_value);
		break;
	case uiw32_tag_joystick_left:
		ui_update_state(-1, ui_tag_joystick_port, 1, (void *)(intptr_t)tag_value);
		break;

	// Printers

	case ui_tag_print_dialog:
		ui_update_state(-1, ui_tag_print_dialog, UI_NEXT, NULL);
		break;

	// Debugging

	case ui_tag_ratelimit_latch:
		ui_update_state(-1, ui_tag_ratelimit_latch, UI_NEXT, NULL);
		break;

	// Help:
	case ui_tag_about:
		uiw32_create_about_window(uiw32);
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void uiw32_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct ui_windows32_interface *uiw32 = sptr;
	struct ui_state_message *msg = smsg;
	int value = msg->value;
	const void *data = msg->data;

	switch (tag) {

	// Simple toggles

	case ui_tag_fullscreen:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Configuration

	case ui_tag_config_autosave:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Hardware

	case ui_tag_machine:
		CheckMenuRadioItem(uiw32->top_menu, UIW32_TAGV(tag, 0), UIW32_TAGV(tag, uiw32->max_machine_id), UIW32_TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_cartridge:
		CheckMenuRadioItem(uiw32->top_menu, UIW32_TAGV(tag, 0), UIW32_TAGV(tag, uiw32->max_cartridge_id), UIW32_TAGV(tag, value), MF_BYCOMMAND);
		break;

	// Cassettes

	case ui_tag_tape_dialog:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Floppy disks

	case ui_tag_disk_dialog:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Video

	case ui_tag_tv_dialog:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	case ui_tag_ccr:
		CheckMenuRadioItem(uiw32->top_menu, UIW32_TAGV(tag, 0), UIW32_TAGV(tag, 4), UIW32_TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_tv_input:
		CheckMenuRadioItem(uiw32->top_menu, UIW32_TAGV(tag, 0), UIW32_TAGV(tag, 3), UIW32_TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_vdg_inverse:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Keyboard

	case ui_tag_keymap:
		CheckMenuRadioItem(uiw32->top_menu, UIW32_TAGV(tag, 0), UIW32_TAGV(tag, (dkbd_num_layouts - 1)), UIW32_TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_hkbd_layout:
		CheckMenuRadioItem(uiw32->top_menu, UIW32_TAGV(tag, 0), UIW32_TAGV(tag, 0xff), UIW32_TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_hkbd_lang:
		CheckMenuRadioItem(uiw32->top_menu, UIW32_TAGV(tag, 0), UIW32_TAGV(tag, 0xff), UIW32_TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_kbd_translate:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Joysticks

	case ui_tag_joystick_port:
		{
			int xtag = uiw32_tag_joystick_right + value;
			CheckMenuRadioItem(uiw32->top_menu, UIW32_TAGV(xtag, 0), UIW32_TAGV(xtag, 0xff), UIW32_TAGV(xtag, (intptr_t)data), MF_BYCOMMAND);
		}
		break;

	// Printers

	case ui_tag_print_dialog:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Debugging

	case ui_tag_ratelimit_latch:
		CheckMenuItem(uiw32->top_menu, UIW32_TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// SDL integration.  The SDL2 video modules call out to these when
// WINDOWS32 is defined to add and remove the menu bar.

/* Get underlying window handle from SDL. */

static HWND get_hwnd(SDL_Window *w) {
	SDL_version sdlver;
	SDL_SysWMinfo sdlinfo;
	SDL_VERSION(&sdlver);
	sdlinfo.version = sdlver;
	SDL_GetWindowWMInfo(w, &sdlinfo);
	return sdlinfo.info.win.window;
}

/* Custom window event handler to intercept menu selections. */

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	SDL_Event event;
	SDL_SysWMmsg wmmsg;

	switch (msg) {

	case WM_COMMAND:
		// Selectively push WM events onto the SDL queue.
		wmmsg.msg.win.hwnd = hwnd;
		wmmsg.msg.win.msg = msg;
		wmmsg.msg.win.wParam = wParam;
		wmmsg.msg.win.lParam = lParam;
		event.type = SDL_SYSWMEVENT;
		event.syswm.msg = &wmmsg;
		SDL_PushEvent(&event);
		break;

	case WM_UNINITMENUPOPUP:
		DELEGATE_SAFE_CALL(xroar.vo_interface->draw);
		return CallWindowProc(sdl_window_proc, hwnd, msg, wParam, lParam);

	case WM_TIMER:
		// In Wine, this event only seems to fire when menus are being
		// browsed, which is exactly the time we need to keep the audio
		// buffer full with silence:
		sound_send_silence(xroar.ao_interface->sound_interface);
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

// Change menubar visibility.  This will change the size of the client area
// while leaving the window size the same, so the video module should then
// resize itself to account for this.

void sdl_windows32_set_menu_visible(struct ui_sdl2_interface *uisdl2, _Bool visible) {
	if (!uisdl2) {
		return;
	}

	struct ui_windows32_interface *uiw32 = (struct ui_windows32_interface *)uisdl2;

	HWND hwnd = get_hwnd(uisdl2->vo_window);
	_Bool is_visible = (GetMenu(hwnd) != NULL);

	if (!is_visible && visible) {
		SetMenu(hwnd, uiw32->top_menu);
	} else if (is_visible && !visible) {
		SetMenu(hwnd, NULL);
	}
}
