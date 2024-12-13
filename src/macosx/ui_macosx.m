/** \file
 *
 *  \brief Mac OS X+ user-interface module.
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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
 *
 *  Based on SDLMain.m - main entry point for our Cocoa-ized SDL app
 *
 *  Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
 *
 *  Non-NIB-Code & other changes: Max Horn <max@quendi.de>
 *
 *  Feel free to customize this file to suit your needs.
 */

#include "top-config.h"

#include <SDL.h>
#include <sys/param.h> /* for MAXPATHLEN */
#include <unistd.h>

#import <Cocoa/Cocoa.h>
//#import <AppKit/AppKit.h>

#include "slist.h"
#include "xalloc.h"

#include "blockdev.h"
#include "cart.h"
#include "hkbd.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "messenger.h"
#include "module.h"
#include "printer.h"
#include "tape.h"
#include "ui.h"
#include "vdisk.h"
#include "vo.h"
#include "vo_render.h"
#include "xroar.h"
#include "sdl2/common.h"

#include "macosx/ui_macosx.h"

// Extend the UI tags for our own purposes

enum {
	uimac_tag_joystick_right = ui_num_tags,
	uimac_tag_joystick_left,
	uimac_tag_hd_new,
	uimac_tag_hd_detach,
	uimac_tag_config_save,
};

static const char *hd_type_name[4] = { "20MiB", "40MiB", "128MiB", "256MiB" };
static const int hd_type_map[4] = {
        BD_ACME_NEMESIS,
        BD_ACME_ULTRASONICUS,
        BD_ACME_ACCELLERATTI,
        BD_ACME_ZIPPIBUS
};

@interface SDLMain : NSObject <NSApplicationDelegate>
@end

/* For some reaon, Apple removed setAppleMenu from the headers in 10.4, but the
 * method still is there and works.  To avoid warnings, we declare it ourselves
 * here. */
@interface NSApplication(SDL_Missing_Methods)
- (void)setAppleMenu:(NSMenu *)menu;
@end

static int gArgc;
static char **gArgv;
static BOOL gFinderLaunch;
static BOOL gCalledAppMainline = FALSE;

static NSString *get_application_name(void) {
	const NSDictionary *dict;
	NSString *app_name = 0;

	/* Determine the application name */
	dict = (const NSDictionary *)CFBundleGetInfoDictionary(CFBundleGetMainBundle());
	if (dict)
		app_name = [dict objectForKey: @"CFBundleName"];

	if (![app_name length])
		app_name = [[NSProcessInfo processInfo] processName];

	return app_name;
}

static void cocoa_update_radio_menu_from_enum(NSMenu *menu, struct xconfig_enum *xc_enum,
					      unsigned tag);

/* Setting this to true is a massive hack so that cocoa file dialogues receive
 * keypresses.  Ideally, need to sort SDL out or turn this into a regular
 * OpenGL application.  SDL2 in 2019 note: not sure if this is needed now?  */
int cocoa_super_all_keys = 0;

@interface XRoarApplication : NSApplication
@end

@implementation XRoarApplication

- (void)sendEvent:(NSEvent *)anEvent {
	switch ([anEvent type]) {
		case NSEventTypeKeyDown:
		case NSEventTypeKeyUp:
			if (cocoa_super_all_keys || ([anEvent modifierFlags] & NSEventModifierFlagCommand)) {
				[super sendEvent:anEvent];
			}
			break;
		default:
			[super sendEvent:anEvent];
			break;
	}
}

+ (void)registerUserDefaults {
	NSDictionary *appDefaults = [[NSDictionary alloc] initWithObjectsAndKeys:
		[NSNumber numberWithBool:NO], @"AppleMomentumScrollSupported",
		[NSNumber numberWithBool:NO], @"ApplePressAndHoldEnabled",
		[NSNumber numberWithBool:YES], @"ApplePersistenceIgnoreState",
		nil];
	[[NSUserDefaults standardUserDefaults] registerDefaults:appDefaults];
	[appDefaults release];
}

- (void)do_set_state:(id)sender {
	int sender_tag = [sender tag];
	int tag = UIMAC_TAG_TYPE(sender_tag);
	int value = UIMAC_TAG_VALUE(sender_tag);

	// Try and ensure that the keydown event that (maybe) caused this
	// menuitem dispatch is not then handled by the main loop as well.
	SDL_PumpEvents();
	SDL_FlushEvent(SDL_KEYDOWN);

	switch (tag) {

	/* Simple actions: */
	case ui_tag_action:
		switch (value) {
		case ui_action_quit:
			{
				SDL_Event event;
				event.type = SDL_QUIT;
				SDL_PushEvent(&event);
			}
			break;
		case ui_action_reset_soft:
			hk_scan_release(hk_scan_Shift_L);
			hk_scan_release(hk_scan_Shift_R);
			xroar_soft_reset();
			break;
		case ui_action_reset_hard:
			hk_scan_release(hk_scan_Shift_L);
			hk_scan_release(hk_scan_Shift_R);
			xroar_hard_reset();
			break;
		case ui_action_file_run:
			hk_scan_release(hk_scan_Shift_L);
			hk_scan_release(hk_scan_Shift_R);
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
		case ui_action_tape_input:
			xroar_insert_input_tape();
			break;
		case ui_action_tape_output:
			xroar_insert_output_tape();
			break;
		case ui_action_tape_input_rewind:
			if (xroar.tape_interface->tape_input) {
				tape_seek(xroar.tape_interface->tape_input, 0, SEEK_SET);
			}
			break;
		case ui_action_print_flush:
			xroar_flush_printer();
			break;
		case ui_action_joystick_swap:
			ui_update_state(-1, ui_tag_joystick_cycle, 1, NULL);
			break;
		default:
			break;
		}
		break;

	/* Configuration: */
	case uimac_tag_config_save:
		xroar_save_config_file();
		break;
	case ui_tag_config_autosave:
		ui_update_state(-1, ui_tag_config_autosave, UI_NEXT, NULL);
		break;

	/* Machines: */
	case ui_tag_machine:
		ui_update_state(-1, ui_tag_machine, value, NULL);
		break;

	/* Cartridges: */
	case ui_tag_cartridge:
		ui_update_state(-1, ui_tag_cartridge, value, NULL);
		break;

	/* Cassettes: */
	case ui_tag_tape_flag_fast:
		ui_update_state(-1, ui_tag_tape_flag_fast, UI_NEXT, NULL);
		break;
	case ui_tag_tape_flag_pad_auto:
		ui_update_state(-1, ui_tag_tape_flag_pad_auto, UI_NEXT, NULL);
		break;
	case ui_tag_tape_flag_rewrite:
		ui_update_state(-1, ui_tag_tape_flag_rewrite, UI_NEXT, NULL);
		break;
	case ui_tag_tape_playing:
		ui_update_state(-1, ui_tag_tape_playing, UI_NEXT, NULL);
		break;

	/* Disks: */
	case ui_tag_disk_insert:
		xroar_insert_disk(value);
		break;
	case ui_tag_disk_new:
		xroar_new_disk(value);
		break;
	case ui_tag_disk_write_enable:
		ui_update_state(-1, ui_tag_disk_write_enable, UI_NEXT, (void *)(intptr_t)value);
		break;
	case ui_tag_disk_write_back:
		ui_update_state(-1, ui_tag_disk_write_back, UI_NEXT, (void *)(intptr_t)value);
		break;
	case ui_tag_disk_eject:
		xroar_eject_disk(value);
		break;
	case ui_tag_hd_filename:
		if (value >= 0 && value <= 1) {
			int hd = value;
			const char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->load_filename, "Attach hard disk image");
			if (filename) {
				xroar_insert_hd_file(hd, filename);
			}
		}
		break;
	case uimac_tag_hd_new:
		{
			int hd = value >> 4;
			int hd_type_index = value & 15;
			if (hd >= 0 && hd <= 1 && hd_type_index >= 0 && hd_type_index <= 3) {
				int hd_type = hd_type_map[hd_type_index];
				char *filename = DELEGATE_CALL(xroar.ui_interface->filereq_interface->save_filename, "Create hard disk image");
				if (filename) {
					if (bd_create(filename, hd_type)) {
						xroar_insert_hd_file(hd, filename);
					}
				}
			}
		}
		break;
	case uimac_tag_hd_detach:
		if (value >= 0 && value <= 1) {
			int hd = value;
			xroar_insert_hd_file(hd, NULL);
		}
		break;

	// Printers:
	case ui_tag_print_destination:
		if (value == PRINTER_DESTINATION_FILE) {
			char *filename = DELEGATE_CALL(global_uisdl2->ui_interface.filereq_interface->save_filename, "Print to file");
			if (filename) {
				ui_update_state(-1, ui_tag_print_file, 0, filename);
			}
		}
		ui_update_state(-1, ui_tag_print_destination, value, NULL);
		break;

	/* Video: */
	case ui_tag_cmp_fs:
		ui_update_state(-1, ui_tag_cmp_fs, value, NULL);
		break;
	case ui_tag_cmp_fsc:
		ui_update_state(-1, ui_tag_cmp_fsc, value, NULL);
		break;
	case ui_tag_cmp_system:
		ui_update_state(-1, ui_tag_cmp_system, value, NULL);
		break;
	case ui_tag_cmp_colour_killer:
		ui_update_state(-1, ui_tag_cmp_colour_killer, UI_NEXT, NULL);
		break;
	case ui_tag_ccr:
		ui_update_state(-1, ui_tag_ccr, value, NULL);
		break;
	case ui_tag_picture:
		ui_update_state(-1, ui_tag_picture, value, NULL);
		break;
	case ui_tag_ntsc_scaling:
		ui_update_state(-1, ui_tag_ntsc_scaling, UI_NEXT, NULL);
		break;
	case ui_tag_tv_input:
		ui_update_state(-1, ui_tag_tv_input, value, NULL);
		break;
	case ui_tag_fullscreen:
		ui_update_state(-1, ui_tag_fullscreen, UI_NEXT, NULL);
		break;
	case ui_tag_vdg_inverse:
		ui_update_state(-1, ui_tag_vdg_inverse, UI_NEXT, NULL);
		break;
	case ui_tag_zoom:
		ui_update_state(-1, tag, value, NULL);
		break;

	/* Keyboard: */
	case ui_tag_keymap:
		ui_update_state(-1, ui_tag_keymap, value, NULL);
		break;

	// Tool
	case ui_tag_hkbd_layout:
		ui_update_state(-1, ui_tag_hkbd_layout, value, NULL);
		break;
	case ui_tag_hkbd_lang:
		ui_update_state(-1, ui_tag_hkbd_lang, value, NULL);
		break;
	case ui_tag_kbd_translate:
		ui_update_state(-1, ui_tag_kbd_translate, UI_NEXT, NULL);
		break;
	case ui_tag_ratelimit:
		ui_update_state(-1, ui_tag_ratelimit_latch, UI_NEXT, NULL);
		break;

	/* Joysticks: */
	case uimac_tag_joystick_right:
		ui_update_state(-1, ui_tag_joystick_port, 0, (void *)(intptr_t)value);
		break;
	case uimac_tag_joystick_left:
		ui_update_state(-1, ui_tag_joystick_port, 1, (void *)(intptr_t)value);
		break;

	default:
		break;
	}
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
	struct ui_macosx_interface *uimac = (struct ui_macosx_interface *)global_uisdl2;

	int item_tag = [item tag];
	int tag = UIMAC_TAG_TYPE(item_tag);
	int value = UIMAC_TAG_VALUE(item_tag);

	switch (tag) {

	case ui_tag_config_autosave:
		[item setState:(uimac->config.autosave ? NSOnState : NSOffState)];
		break;

	case ui_tag_machine:
		[item setState:((value == uimac->machine.id) ? NSOnState : NSOffState)];
		break;

	case ui_tag_cartridge:
		[item setState:((value == uimac->cart.id) ? NSOnState : NSOffState)];
		break;

	case ui_tag_tape_flag_fast:
		[item setState:(uimac->tape.fast ? NSOnState : NSOffState)];
		break;
	case ui_tag_tape_flag_pad_auto:
		[item setState:(uimac->tape.pad_auto ? NSOnState : NSOffState)];
		break;
	case ui_tag_tape_flag_rewrite:
		[item setState:(uimac->tape.rewrite ? NSOnState : NSOffState)];
		break;
	case ui_tag_tape_playing:
		[item setState:(uimac->tape.playing ? NSOnState : NSOffState)];
		break;

	case ui_tag_disk_write_enable:
		[item setState:(uimac->disk.drive[value].write_enable ? NSOnState : NSOffState)];
		break;
	case ui_tag_disk_write_back:
		[item setState:(uimac->disk.drive[value].write_back ? NSOnState : NSOffState)];
		break;

	case ui_tag_print_destination:
		[item setState:((value == uimac->lp.destination) ? NSOnState : NSOffState)];
		break;

	case ui_tag_fullscreen:
		[item setState:(uimac->vo.fullscreen ? NSOnState : NSOffState)];
		break;
	case ui_tag_vdg_inverse:
		[item setState:(uimac->vo.invert_text ? NSOnState : NSOffState)];
		break;
	case ui_tag_ccr:
		[item setState:((value == uimac->vo.ccr) ? NSOnState : NSOffState)];
		break;
	case ui_tag_picture:
		[item setState:((value == uimac->vo.picture) ? NSOnState : NSOffState)];
		break;
	case ui_tag_ntsc_scaling:
		[item setState:((value == uimac->vo.ntsc_scaling) ? NSOnState : NSOffState)];
		break;
	case ui_tag_tv_input:
		[item setState:((value == uimac->vo.tv_input) ? NSOnState : NSOffState)];
		break;
	case ui_tag_cmp_fs:
		[item setState:((value == uimac->vo.cmp_fs) ? NSOnState : NSOffState)];
		break;
	case ui_tag_cmp_fsc:
		[item setState:((value == uimac->vo.cmp_fsc) ? NSOnState : NSOffState)];
		break;
	case ui_tag_cmp_system:
		[item setState:((value == uimac->vo.cmp_system) ? NSOnState : NSOffState)];
		break;
	case ui_tag_cmp_colour_killer:
		[item setState:((uimac->vo.cmp_colour_killer) ? NSOnState : NSOffState)];
		break;

	case ui_tag_keymap:
		[item setState:((value == uimac->machine.keymap) ? NSOnState : NSOffState)];
		break;
	case ui_tag_hkbd_layout:
		[item setState:((value == uimac->kbd.layout) ? NSOnState : NSOffState)];
		break;
	case ui_tag_hkbd_lang:
		[item setState:((value == uimac->kbd.lang) ? NSOnState : NSOffState)];
		break;
	case ui_tag_kbd_translate:
		[item setState:(uimac->kbd.translate ? NSOnState : NSOffState)];
		break;
	case ui_tag_ratelimit:
		[item setState:(uimac->misc.ratelimit_latch ? NSOnState : NSOffState)];
		break;

	case uimac_tag_joystick_right:
		[item setState:((value == uimac->joy.id[0]) ? NSOnState : NSOffState)];
		break;
	case uimac_tag_joystick_left:
		[item setState:((value == uimac->joy.id[1]) ? NSOnState : NSOffState)];
		break;

	}
	return YES;
}

@end

/* The main class of the application, the application's delegate */
@implementation SDLMain

/* Set the working directory to the .app's parent directory */
- (void)setupWorkingDirectory:(BOOL)shouldChdir {
	if (shouldChdir) {
		char parentdir[MAXPATHLEN];
		CFURLRef url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
		CFURLRef url2 = CFURLCreateCopyDeletingLastPathComponent(0, url);
		if (CFURLGetFileSystemRepresentation(url2, 1, (UInt8 *)parentdir, MAXPATHLEN)) {
			chdir(parentdir);   /* chdir to the binary app's parent */
		}
		CFRelease(url);
		CFRelease(url2);
	}
}

static void setApplicationMenu(void) {
	/* warning: this code is very odd */
	NSMenu *apple_menu;
	NSMenuItem *item;
	NSString *title;
	NSString *app_name;

	app_name = get_application_name();
	apple_menu = [[NSMenu alloc] initWithTitle:@""];

	/* Add menu items */
	title = [@"About " stringByAppendingString:app_name];
	[apple_menu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

	[apple_menu addItem:[NSMenuItem separatorItem]];

	title = [@"Hide " stringByAppendingString:app_name];
	[apple_menu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];

	item = (NSMenuItem *)[apple_menu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
	[item setKeyEquivalentModifierMask:(NSEventModifierFlagOption|NSEventModifierFlagCommand)];

	[apple_menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

	[apple_menu addItem:[NSMenuItem separatorItem]];

	title = [@"Quit " stringByAppendingString:app_name];
	item = [[NSMenuItem alloc] initWithTitle:title action:@selector(do_set_state:) keyEquivalent:@"q"];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_quit)];
	[apple_menu addItem:item];

	/* Put menu into the menubar */
	item = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
	[item setSubmenu:apple_menu];
	[[NSApp mainMenu] addItem:item];

	/* Tell the application object that this is now the application menu */
	[NSApp setAppleMenu:apple_menu];

	/* Finally give up our references to the objects */
	[apple_menu release];
	[item release];
}

/* Create File menu */
static void setup_file_menu(void) {
	NSMenu *file_menu;
	NSMenuItem *file_menu_item;
	NSMenuItem *item;
	NSMenu *submenu;
	NSString *tmp;

	file_menu = [[NSMenu alloc] initWithTitle:@"File"];

	tmp = [NSString stringWithFormat:@"Run%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@"L"];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_file_run)];
	[file_menu addItem:item];
	[item release];
	[tmp release];

	tmp = [NSString stringWithFormat:@"Load%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@"l"];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_file_load)];
	[file_menu addItem:item];
	[item release];
	[tmp release];

	[file_menu addItem:[NSMenuItem separatorItem]];

	submenu = [[NSMenu alloc] initWithTitle:@"Cassette"];

	tmp = [NSString stringWithFormat:@"Input tape%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_tape_input)];
	[submenu addItem:item];
	[item release];
	[tmp release];

	tmp = [NSString stringWithFormat:@"Output tape%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@"w"];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_tape_output)];
	[submenu addItem:item];
	[item release];
	[tmp release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Rewind input tape" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_tape_input_rewind)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Play" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAG(ui_tag_tape_playing)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Fast loading" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAG(ui_tag_tape_flag_fast)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"CAS padding" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAG(ui_tag_tape_flag_pad_auto)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Rewrite" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAG(ui_tag_tape_flag_rewrite)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Cassette" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[file_menu addItem:item];
	[item release];

	[file_menu addItem:[NSMenuItem separatorItem]];

	for (int drive = 0; drive < 4; ++drive) {
		NSString *title = [NSString stringWithFormat:@"Drive %d", drive+1];
		NSString *key1 = [NSString stringWithFormat:@"%d", drive+1];
		NSString *key2 = [NSString stringWithFormat:@"%d", drive+5];
		NSString *tmp;

		submenu = [[NSMenu alloc] initWithTitle:title];

		tmp = [NSString stringWithFormat:@"Insert disk%C", 0x2026];
		item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:key1];
		[item setTag:UIMAC_TAGV(ui_tag_disk_insert, drive)];
		[submenu addItem:item];
		[item release];
		[tmp release];

		tmp = [NSString stringWithFormat:@"New disk%C", 0x2026];
		item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:key1];
		[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
		[item setTag:UIMAC_TAGV(ui_tag_disk_new, drive)];
		[submenu addItem:item];
		[item release];
		[tmp release];

		[submenu addItem:[NSMenuItem separatorItem]];

		item = [[NSMenuItem alloc] initWithTitle:@"Write enable" action:@selector(do_set_state:) keyEquivalent:key2];
		[item setTag:UIMAC_TAGV(ui_tag_disk_write_enable, drive)];
		[submenu addItem:item];
		[item release];

		item = [[NSMenuItem alloc] initWithTitle:@"Write back" action:@selector(do_set_state:) keyEquivalent:key2];
		[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
		[item setTag:UIMAC_TAGV(ui_tag_disk_write_back, drive)];
		[submenu addItem:item];
		[item release];

		[submenu addItem:[NSMenuItem separatorItem]];

		tmp = [NSString stringWithFormat:@"Eject disk%C", 0x2026];
		item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:UIMAC_TAGV(ui_tag_disk_eject, drive)];
		[submenu addItem:item];
		[item release];
		[tmp release];

		item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
		[item setSubmenu:submenu];
		[file_menu addItem:item];
		[item release];

		[key2 release];
		[key1 release];
		[title release];
	}

	for (int hd = 0; hd < 2; ++hd) {
		NSString *title = [NSString stringWithFormat:@"HD %d", hd];
		NSString *tmp;

		submenu = [[NSMenu alloc] initWithTitle:title];

		tmp = [NSString stringWithFormat:@"Attach%C", 0x2026];
		item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:UIMAC_TAGV(ui_tag_hd_filename, hd)];
		[submenu addItem:item];
		[item release];
		[tmp release];

		NSString *subtitle = [NSString stringWithFormat:@"New"];
		NSMenu *subsubmenu = [[NSMenu alloc] initWithTitle:subtitle];
		for (int hs = 0; hs < 4; ++hs) {

			tmp = [NSString stringWithFormat:@"%s%C", hd_type_name[hs], 0x2026];
			item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
			[item setTag:UIMAC_TAGV(uimac_tag_hd_new, (hd << 4) | hs)];
			[subsubmenu addItem:item];
			[item release];
			[tmp release];
		}

		item = [[NSMenuItem alloc] initWithTitle:subtitle action:nil keyEquivalent:@""];
		[item setSubmenu:subsubmenu];
		[submenu addItem:item];
		[item release];

		[subtitle release];

		tmp = [NSString stringWithFormat:@"Detach"];
		item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:UIMAC_TAGV(uimac_tag_hd_detach, hd)];
		[submenu addItem:item];
		[item release];
		[tmp release];

		item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
		[item setSubmenu:submenu];
		[file_menu addItem:item];
		[item release];

		[title release];
	}

	[file_menu addItem:[NSMenuItem separatorItem]];

	submenu = [[NSMenu alloc] initWithTitle:@"Printer"];

	item = [[NSMenuItem alloc] initWithTitle:@"No printer" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(ui_tag_print_destination, PRINTER_DESTINATION_NONE)];
	[submenu addItem:item];
	[item release];

	tmp = [NSString stringWithFormat:@"Print to file%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(ui_tag_print_destination, PRINTER_DESTINATION_FILE)];
	[submenu addItem:item];
	[item release];
	[tmp release];

	item = [[NSMenuItem alloc] initWithTitle:@"Print to pipe" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(ui_tag_print_destination, PRINTER_DESTINATION_PIPE)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Flush" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_print_flush)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Printer" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[file_menu addItem:item];
	[item release];

	[file_menu addItem:[NSMenuItem separatorItem]];

	tmp = [NSString stringWithFormat:@"Save snapshot%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@"s"];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_file_save_snapshot)];
	[file_menu addItem:item];
	[item release];
	[tmp release];

#ifdef SCREENSHOT
	[file_menu addItem:[NSMenuItem separatorItem]];

	tmp = [NSString stringWithFormat:@"Screenshot to PNG%C", 0x2026];
	item = [[NSMenuItem alloc] initWithTitle:tmp action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_file_screenshot)];
	[file_menu addItem:item];
	[item release];
	[tmp release];
#endif

	[file_menu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Save configuration" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAG(uimac_tag_config_save)];
	[file_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Autosave configuration" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAG(ui_tag_config_autosave)];
	[file_menu addItem:item];
	[item release];

	file_menu_item = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
	[file_menu_item setSubmenu:file_menu];
	[[NSApp mainMenu] addItem:file_menu_item];
	[file_menu_item release];
}

static NSMenu *view_menu;

/* Create View menu */
static void setup_view_menu(void) {
	NSMenuItem *view_menu_item;
	NSMenuItem *item;
	NSMenu *submenu;

	view_menu = [[NSMenu alloc] initWithTitle:@"View"];

	submenu = [[NSMenu alloc] initWithTitle:@"TV input"];
	cocoa_update_radio_menu_from_enum(submenu, machine_tv_input_list, ui_tag_tv_input);
	item = [[NSMenuItem alloc] initWithTitle:@"TV input" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Picture area"];
	cocoa_update_radio_menu_from_enum(submenu, vo_viewport_list, ui_tag_picture);
	item = [[NSMenuItem alloc] initWithTitle:@"Picture area" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"60Hz scaling" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
	[item setTag:UIMAC_TAG(ui_tag_ntsc_scaling)];
	[view_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Composite rendering"];
	cocoa_update_radio_menu_from_enum(submenu, vo_cmp_ccr_list, ui_tag_ccr);
	item = [[NSMenuItem alloc] initWithTitle:@"Composite rendering" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Composite F(s)"];
	cocoa_update_radio_menu_from_enum(submenu, vo_render_fs_list, ui_tag_cmp_fs);
	item = [[NSMenuItem alloc] initWithTitle:@"Composite F(s)" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Composite F(sc)"];
	cocoa_update_radio_menu_from_enum(submenu, vo_render_fsc_list, ui_tag_cmp_fsc);
	item = [[NSMenuItem alloc] initWithTitle:@"Composite F(sc)" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Composite system"];
	cocoa_update_radio_menu_from_enum(submenu, vo_render_system_list, ui_tag_cmp_system);
	item = [[NSMenuItem alloc] initWithTitle:@"Composite system" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Colour killer" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
	[item setTag:UIMAC_TAG(ui_tag_cmp_colour_killer)];
	[view_menu addItem:item];
	[item release];

	[view_menu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Inverse text" action:@selector(do_set_state:) keyEquivalent:@"i"];
	[item setKeyEquivalentModifierMask:NSEventModifierFlagCommand|NSEventModifierFlagShift];
	[item setTag:UIMAC_TAG(ui_tag_vdg_inverse)];
	[view_menu addItem:item];
	[item release];

	[view_menu addItem:[NSMenuItem separatorItem]];

	submenu = [[NSMenu alloc] initWithTitle:@"Zoom"];

	item = [[NSMenuItem alloc] initWithTitle:@"Zoom in" action:@selector(do_set_state:) keyEquivalent:@"+"];
	[item setTag:UIMAC_TAGV(ui_tag_zoom, UI_NEXT)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Zoom out" action:@selector(do_set_state:) keyEquivalent:@"-"];
	[item setTag:UIMAC_TAGV(ui_tag_zoom, UI_PREV)];
	[submenu addItem:item];
	[item release];

	[submenu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Reset" action:@selector(do_set_state:) keyEquivalent:@"0"];
	[item setTag:UIMAC_TAGV(ui_tag_zoom, 0)];
	[submenu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Zoom" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[view_menu addItem:item];
	[item release];

	[view_menu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Full screen" action:@selector(do_set_state:) keyEquivalent:@"f"];
	[item setTag:UIMAC_TAG(ui_tag_fullscreen)];
	[view_menu addItem:item];
	[item release];

	view_menu_item = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
	[view_menu_item setSubmenu:view_menu];
	[[NSApp mainMenu] addItem:view_menu_item];
	[view_menu_item release];
}

static NSMenu *machine_menu;
static NSMenu *cartridge_menu;
static NSMenu *joy_right_menu;
static NSMenu *joy_left_menu;

/* Create Machine menu */
static void setup_hardware_menu(void) {
	NSMenu *hardware_menu;
	NSMenuItem *hardware_menu_item;
	NSMenuItem *item;
	NSMenu *submenu;

	hardware_menu = [[NSMenu alloc] initWithTitle:@"Hardware"];

	machine_menu = [[NSMenu alloc] initWithTitle:@"Machine"];
	item = [[NSMenuItem alloc] initWithTitle:@"Machine" action:nil keyEquivalent:@""];
	[item setSubmenu:machine_menu];
	[hardware_menu addItem:item];
	[item release];

	[hardware_menu addItem:[NSMenuItem separatorItem]];

	cartridge_menu = [[NSMenu alloc] initWithTitle:@"Cartridge"];
	item = [[NSMenuItem alloc] initWithTitle:@"Cartridge" action:nil keyEquivalent:@""];
	[item setSubmenu:cartridge_menu];
	[hardware_menu addItem:item];
	[item release];

	[hardware_menu addItem:[NSMenuItem separatorItem]];

	submenu = [[NSMenu alloc] initWithTitle:@"Keyboard type"];
	cocoa_update_radio_menu_from_enum(submenu, machine_keyboard_list, ui_tag_keymap);
	item = [[NSMenuItem alloc] initWithTitle:@"Keyboard type" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[hardware_menu addItem:item];
	[item release];

	[hardware_menu addItem:[NSMenuItem separatorItem]];

	joy_right_menu = [[NSMenu alloc] initWithTitle:@"Right joystick"];
	item = [[NSMenuItem alloc] initWithTitle:@"Right joystick" action:nil keyEquivalent:@""];
	[item setSubmenu:joy_right_menu];
	[hardware_menu addItem:item];
	[item release];

	joy_left_menu = [[NSMenu alloc] initWithTitle:@"Left joystick"];
	item = [[NSMenuItem alloc] initWithTitle:@"Left joystick" action:nil keyEquivalent:@""];
	[item setSubmenu:joy_left_menu];
	[hardware_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Swap joysticks" action:@selector(do_set_state:) keyEquivalent:@"J"];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_joystick_swap)];
	[hardware_menu addItem:item];
	[item release];

	[hardware_menu addItem:[NSMenuItem separatorItem]];

	item = [[NSMenuItem alloc] initWithTitle:@"Soft reset" action:@selector(do_set_state:) keyEquivalent:@"r"];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_reset_soft)];
	[hardware_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Hard reset" action:@selector(do_set_state:) keyEquivalent:@"R"];
	[item setTag:UIMAC_TAGV(ui_tag_action, ui_action_reset_hard)];
	[hardware_menu addItem:item];
	[item release];

	hardware_menu_item = [[NSMenuItem alloc] initWithTitle:@"Machine" action:nil keyEquivalent:@""];
	[hardware_menu_item setSubmenu:hardware_menu];
	[[NSApp mainMenu] addItem:hardware_menu_item];
	[hardware_menu_item release];
}

/* Create Tool menu */
static void setup_tool_menu(void) {
	NSMenu *tool_menu;
	NSMenuItem *tool_menu_item;
	NSMenuItem *item;
	NSMenu *submenu;

	tool_menu = [[NSMenu alloc] initWithTitle:@"Tool"];

	submenu = [[NSMenu alloc] initWithTitle:@"Keyboard layout"];
	cocoa_update_radio_menu_from_enum(submenu, hkbd_layout_list, ui_tag_hkbd_layout);
	item = [[NSMenuItem alloc] initWithTitle:@"Keyboard layout" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[tool_menu addItem:item];
	[item release];

	submenu = [[NSMenu alloc] initWithTitle:@"Keyboard language"];
	cocoa_update_radio_menu_from_enum(submenu, hkbd_lang_list, ui_tag_hkbd_lang);
	item = [[NSMenuItem alloc] initWithTitle:@"Keyboard language" action:nil keyEquivalent:@""];
	[item setSubmenu:submenu];
	[tool_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Keyboard translation" action:@selector(do_set_state:) keyEquivalent:@"z"];
	[item setTag:UIMAC_TAG(ui_tag_kbd_translate)];
	[tool_menu addItem:item];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"Rate limit" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAG(ui_tag_ratelimit)];
	[tool_menu addItem:item];
	[item release];

	tool_menu_item = [[NSMenuItem alloc] initWithTitle:@"Tool" action:nil keyEquivalent:@""];
	[tool_menu_item setSubmenu:tool_menu];
	[[NSApp mainMenu] addItem:tool_menu_item];
	[tool_menu_item release];
}

/* Create a window menu */
static void setup_window_menu(void) {
	NSMenu *window_menu;
	NSMenuItem *window_menu_item;
	NSMenuItem *item;

	window_menu = [[NSMenu alloc] initWithTitle:@"Window"];

	/* "Minimize" item */
	item = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
	[window_menu addItem:item];
	[item release];

	/* Put menu into the menubar */
	window_menu_item = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
	[window_menu_item setSubmenu:window_menu];
	[[NSApp mainMenu] addItem:window_menu_item];

	/* Tell the application object that this is now the window menu */
	[NSApp setWindowsMenu:window_menu];

	/* Finally give up our references to the objects */
	[window_menu release];
	[window_menu_item release];
}

#if 0
/* Replacement for NSApplicationMain */
static void CustomApplicationMain(int argc, char **argv) {
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	SDLMain *sdlMain;

	(void)argc;
	(void)argv;

	/* Create SDLMain and make it the app delegate */
	sdlMain = [[SDLMain alloc] init];
	[NSApp setDelegate:sdlMain];

	/* Start the main event loop */
	[NSApp run];

	[sdlMain release];
	[pool release];
}
#endif

/*
 * Catch document open requests...this lets us notice files when the app was
 * launched by double-clicking a document, or when a document was
 * dragged/dropped on the app's icon. You need to have a CFBundleDocumentsType
 * section in your Info.plist to get this message, apparently.
 *
 * Files are added to gArgv, so to the app, they'll look like command line
 * arguments. Previously, apps launched from the finder had nothing but an
 * argv[0].
 *
 * This message may be received multiple times to open several docs on launch.
 *
 * This message is ignored once the app's mainline has been called.
 */
- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename {
	const char *temparg;
	size_t arglen;
	char *arg;
	char **newargv;

	(void)theApplication;

	if (!gFinderLaunch)  /* MacOS is passing command line args. */
		return FALSE;

	if (gCalledAppMainline)  /* app has started, ignore this document. */
		return FALSE;

	temparg = [filename UTF8String];
	arglen = SDL_strlen(temparg) + 1;
	arg = (char *)SDL_malloc(arglen);
	if (arg == NULL)
		return FALSE;

	newargv = (char **)realloc(gArgv, sizeof(char *) * (gArgc + 2));
	if (newargv == NULL) {
		SDL_free(arg);
		return FALSE;
	}
	gArgv = newargv;

	SDL_strlcpy(arg, temparg, arglen);
	gArgv[gArgc++] = arg;
	gArgv[gArgc] = NULL;
	return TRUE;
}


/* Called when the internal event loop has just started running */
- (void)applicationDidFinishLaunching: (NSNotification *)note {
	(void)note;
	// Set the working directory to the .app's parent directory
	[self setupWorkingDirectory:gFinderLaunch];
	/* Doesn't seem present in SDL2:
	// Pass keypresses etc. to Cocoa
	setenv("SDL_ENABLEAPPEVENTS", "1", 1); */
	// Hand off to main application code
	gCalledAppMainline = TRUE;

	// Seems to be necessary to make the menu bar work (without it, you
	// have to focus something else then return).  I'm sure there's a good
	// reason for that.
	[NSApp activateIgnoringOtherApps:YES];
	[XRoarApplication registerUserDefaults];
}

@end

/* Called from ui_sdl_new() _before_ initialising SDL video. */
void cocoa_register_app(void) {

	// Ensure the application object is initialised
	[XRoarApplication sharedApplication];
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

	// Set up the menubar
	[NSApp setMainMenu:[[NSMenu alloc] init]];
	setApplicationMenu();
	setup_file_menu();
	setup_view_menu();
	setup_hardware_menu();
	setup_tool_menu();
	setup_window_menu();

	[NSApp finishLaunching];

	// More recent macosx adds some weird tab bar stuff to the view menu by
	// default.  I'm sure they had a good reason...
	[NSWindow setAllowsAutomaticWindowTabbing: NO];

	SDLMain *appDelegate = [[SDLMain alloc] init];
	[(NSApplication *)NSApp setDelegate:appDelegate];
}

#if 0
#ifdef main
#  undef main
#endif


/* Main entry point to executable - should *not* be SDL_main! */
int main(int argc, char **argv) {
	/* Copy the arguments into a global variable */
	/* This is passed if we are launched by double-clicking */
	if (argc >= 2 && strncmp(argv[1], "-psn", 4) == 0) {
		gArgv = (char **)SDL_malloc(sizeof(char *) * 2);
		gArgv[0] = argv[0];
		gArgv[1] = NULL;
		gArgc = 1;
		gFinderLaunch = YES;
	} else {
		int i;
		gArgc = argc;
		gArgv = (char **)SDL_malloc(sizeof(char *) * (argc+1));
		for (i = 0; i <= argc; i++)
			gArgv[i] = argv[i];
		gFinderLaunch = NO;
	}

	CustomApplicationMain(argc, argv);
	return 0;
}
#endif

// -------------------------------------------------------------------------

// XRoar UI definition

static void *ui_cocoa_new(void *cfg);
static void ui_cocoa_free(void *);

struct ui_module ui_cocoa_module = {
	.common = { .name = "macosx", .description = "Mac OS X+ SDL2 UI",
		.new = ui_cocoa_new,
	},
	.joystick_module_list = sdl_js_modlist,
};

static void cocoa_update_machine_menu(void *);
static void cocoa_update_cartridge_menu(void *);
static void cocoa_update_joystick_menus(void *);
static void cocoa_ui_state_notify(void *, int tag, void *smsg);

static void *ui_cocoa_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;

	cocoa_register_app();

	struct ui_macosx_interface *uimac = (struct ui_macosx_interface *)ui_sdl_allocate(sizeof(*uimac));
	if (!uimac) {
		return NULL;
	}
	*uimac = (struct ui_macosx_interface){0};
	struct ui_sdl2_interface *uisdl2 = &uimac->ui_sdl2_interface;
	ui_sdl_init(uisdl2, ui_cfg);
	struct ui_interface *ui = &uisdl2->ui_interface;
	ui->free = DELEGATE_AS0(void, ui_cocoa_free, uimac);
	ui->update_machine_menu = DELEGATE_AS0(void, cocoa_update_machine_menu, uimac);
	ui->update_cartridge_menu = DELEGATE_AS0(void, cocoa_update_cartridge_menu, uimac);
	ui->update_joystick_menus = DELEGATE_AS0(void, cocoa_update_joystick_menus, uimac);

	// Register with messenger
	uimac->msgr_client_id = messenger_client_register();

	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_machine, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_cartridge, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_tape_flag_fast, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_tape_flag_pad_auto, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_tape_flag_rewrite, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_tape_playing, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_disk_write_enable, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_disk_write_back, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_cmp_fs, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_cmp_fsc, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_cmp_system, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_cmp_colour_killer, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_ccr, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_picture, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_ntsc_scaling, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_fullscreen, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_vdg_inverse, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_keymap, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_hkbd_layout, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_hkbd_lang, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_kbd_translate, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_joystick_port, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_print_destination, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_ratelimit_latch, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));
	ui_messenger_join_group(uimac->msgr_client_id, ui_tag_config_autosave, MESSENGER_NOTIFY_DELEGATE(cocoa_ui_state_notify, uimac));

	cocoa_update_machine_menu(uisdl2);
	cocoa_update_cartridge_menu(uisdl2);
	cocoa_update_joystick_menus(uisdl2);

	if (!sdl_vo_init(uisdl2)) {
		free(uisdl2);
		return NULL;
	}

	return uisdl2;
}

static void ui_cocoa_free(void *sptr) {
	struct ui_macosx_interface *uimac = sptr;
	messenger_client_unregister(uimac->msgr_client_id);
}

static void cocoa_update_machine_menu(void *sptr) {
	struct ui_macosx_interface *uimac = sptr;
	(void)uimac;

	// Get list of machine configs
	struct slist *mcl = machine_config_list();

	// Remove old entries
	while ([machine_menu numberOfItems] > 0) {
		[machine_menu removeItem:[machine_menu itemAtIndex:0]];
	}

	// Add entries
	NSMenuItem *item;
	for (struct slist *iter = mcl; iter; iter = iter->next) {
		struct machine_config *mc = iter->data;
		NSString *description = [[NSString alloc] initWithUTF8String:mc->description];
		item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:UIMAC_TAGV(ui_tag_machine, mc->id)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[description release];
		[machine_menu addItem:item];
		[item release];
	}
}

static void cocoa_update_cartridge_menu(void *sptr) {
	struct ui_macosx_interface *uimac = sptr;
	(void)uimac;

	// Get list of cart configs
	struct slist *ccl = NULL;
	struct cart *cart = NULL;
	if (xroar.machine) {
		const struct machine_partdb_entry *mpe = (const struct machine_partdb_entry *)xroar.machine->part.partdb;
		const char *cart_arch = mpe->cart_arch;
		ccl = cart_config_list_is_a(cart_arch);
		cart = (struct cart *)part_component_by_id(&xroar.machine->part, "cart");
	}

	// Remove old entries
	while ([cartridge_menu numberOfItems] > 0) {
		[cartridge_menu removeItem:[cartridge_menu itemAtIndex:0]];
	}

	// Add entries
	NSMenuItem *item;
	item = [[NSMenuItem alloc] initWithTitle:@"None" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(ui_tag_cartridge, 0)];
	[cartridge_menu addItem:item];
	[item release];
	for (struct slist *iter = ccl; iter; iter = iter->next) {
		struct cart_config *cc = iter->data;
		NSString *description = [[NSString alloc] initWithUTF8String:cc->description];
		item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:UIMAC_TAGV(ui_tag_cartridge, cc->id)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[description release];
		[cartridge_menu addItem:item];
		[item release];
	}

	// Free everything
	slist_free(ccl);
}

static void cocoa_update_joystick_menus(void *sptr) {
	struct ui_macosx_interface *uimac = sptr;
	(void)uimac;

	// Get list of joystick configs
	struct slist *jcl = joystick_config_list();

	// Remove old entries
	while ([joy_right_menu numberOfItems] > 0) {
		[joy_right_menu removeItem:[joy_right_menu itemAtIndex:0]];
	}
	while ([joy_left_menu numberOfItems] > 0) {
		[joy_left_menu removeItem:[joy_left_menu itemAtIndex:0]];
	}

	// Add entries
	NSMenuItem *item;

	item = [[NSMenuItem alloc] initWithTitle:@"None" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(uimac_tag_joystick_right, 0)];
	[joy_right_menu insertItem:item atIndex:0];
	[item release];

	item = [[NSMenuItem alloc] initWithTitle:@"None" action:@selector(do_set_state:) keyEquivalent:@""];
	[item setTag:UIMAC_TAGV(uimac_tag_joystick_left, 0)];
	[joy_left_menu insertItem:item atIndex:0];
	[item release];

	for (struct slist *iter = jcl; iter; iter = iter->next) {
		struct joystick_config *jc = iter->data;
		NSString *description = [[NSString alloc] initWithUTF8String:jc->description];
		item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:UIMAC_TAGV(uimac_tag_joystick_right, jc->id)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[joy_right_menu addItem:item];
		[item release];

		item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:UIMAC_TAGV(uimac_tag_joystick_left, jc->id)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[description release];
		[joy_left_menu addItem:item];
		[item release];
	}
}

static void cocoa_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct ui_macosx_interface *uimac = sptr;
	struct ui_state_message *uimsg = smsg;
	int value = uimsg->value;
	const void *data = uimsg->data;

	switch (tag) {

	// Configuration

	case ui_tag_config_autosave:
		uimac->config.autosave = value;
		break;

	// Hardware

	case ui_tag_machine:
		uimac->machine.id = value;
		break;

	case ui_tag_cartridge:
		uimac->cart.id = value;
		break;

	// Cassettes

	case ui_tag_tape_flag_fast:
		uimac->tape.fast = value;
		break;

	case ui_tag_tape_flag_pad_auto:
		uimac->tape.pad_auto = value;
		break;

	case ui_tag_tape_flag_rewrite:
		uimac->tape.rewrite = value;
		break;

	case ui_tag_tape_playing:
		uimac->tape.playing = value;
		break;

	// Disk

	case ui_tag_disk_write_enable:
		{
			int drive = (intptr_t)data;
			if (drive >= 0 && drive <= 3) {
				uimac->disk.drive[drive].write_enable = value;
			}
		}
		break;

	case ui_tag_disk_write_back:
		{
			int drive = (intptr_t)data;
			if (drive >= 0 && drive <= 3) {
				uimac->disk.drive[drive].write_back = value;
			}
		}
		break;

	// Video

	case ui_tag_cmp_fs:
		uimac->vo.cmp_fs = value;
		break;

	case ui_tag_cmp_fsc:
		uimac->vo.cmp_fsc = value;
		break;

	case ui_tag_cmp_system:
		uimac->vo.cmp_system = value;
		break;

	case ui_tag_cmp_colour_killer:
		uimac->vo.cmp_colour_killer = value;
		break;

	case ui_tag_ccr:
		uimac->vo.ccr = value;
		break;

	case ui_tag_picture:
		uimac->vo.picture = value;
		break;

	case ui_tag_ntsc_scaling:
		uimac->vo.ntsc_scaling = value;
		break;

	case ui_tag_tv_input:
		uimac->vo.tv_input = value;
		break;

	case ui_tag_fullscreen:
		uimac->vo.fullscreen = value;
		break;

	case ui_tag_vdg_inverse:
		uimac->vo.invert_text = value;
		break;

	// Keyboard

	case ui_tag_keymap:
		uimac->machine.keymap = value;
		break;

	case ui_tag_hkbd_layout:
		uimac->kbd.layout = value;
		break;

	case ui_tag_hkbd_lang:
		uimac->kbd.lang = value;
		break;

	case ui_tag_kbd_translate:
		uimac->kbd.translate = value;
		break;

	// Joysticks

	case ui_tag_joystick_port:
		if (value >= 0 && value <= 1) {
			uimac->joy.id[value] = (intptr_t)data;
		}
		break;

	// Printer

	case ui_tag_print_destination:
		uimac->lp.destination = value;
		break;

	// Debugging, misc

	case ui_tag_ratelimit_latch:
		uimac->misc.ratelimit_latch = value;
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void cocoa_update_radio_menu_from_enum(NSMenu *menu, struct xconfig_enum *xc_enum,
					      unsigned tag) {
	// Remove old entries
	while ([menu numberOfItems] > 0) {
		[menu removeItem:[menu itemAtIndex:0]];
	}

	// Fine list terminating entry
	int enum_index;
	for (enum_index = 0; xc_enum[enum_index].name; enum_index++);

	// Add entries in reverse order
	while (enum_index > 0) {
		--enum_index;
		if (!xc_enum[enum_index].description) {
			continue;
		}
		NSString *description = [[NSString alloc] initWithUTF8String:xc_enum[enum_index].description];
		NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:description action:@selector(do_set_state:) keyEquivalent:@""];
		[item setTag:UIMAC_TAGV(tag, xc_enum[enum_index].value)];
		[item setOnStateImage:[NSImage imageNamed:@"NSMenuRadio"]];
		[menu insertItem:item atIndex:0];
		[item release];
		[description release];
	}
}
