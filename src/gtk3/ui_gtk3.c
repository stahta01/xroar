/** \file
 *
 *  \brief GTK+ 3 user-interface module.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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

// for setenv()
#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <stdlib.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtk/gtk.h>
#ifdef HAVE_X11
#include <gdk/gdkx.h>
#endif

#include "slist.h"

#include "cart.h"
#include "events.h"
#include "hkbd.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "messenger.h"
#include "module.h"
#include "ui.h"
#include "vdrive.h"
#include "vo.h"
#include "xroar.h"

#include "gtk3/common.h"
#include "gtk3/dialog.h"
#include "gtk3/drivecontrol.h"
#include "gtk3/event_handlers.h"
#include "gtk3/printercontrol.h"
#include "gtk3/tapecontrol.h"
#include "gtk3/video_options.h"

#ifdef HAVE_X11
#include "x11/hkbd_x11.h"
#endif

// Module definition

static void *ui_gtk3_new(void *cfg);

extern struct module filereq_gtk3_module;
extern struct module filereq_cli_module;
extern struct module filereq_null_module;

static struct module * const gtk3_filereq_module_list[] = {
	&filereq_gtk3_module,
#ifdef HAVE_CLI
	&filereq_cli_module,
#endif
	&filereq_null_module,
	NULL
};

struct ui_module ui_gtk3_module = {
	.common = { .name = "gtk3", .description = "GTK+ 3 UI",
		.new = ui_gtk3_new,
	},
	.filereq_module_list = gtk3_filereq_module_list,
	.joystick_module_list = gtk3_js_modlist,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Dynamic menus

static void gtk3_update_machine_menu(void *);
static void gtk3_update_cartridge_menu(void *);
static void gtk3_update_joystick_menus(void *);

// File menu callbacks

static void do_load_file(GtkEntry *entry, gpointer user_data);
static void do_run_file(GtkEntry *entry, gpointer user_data);
static void insert_disk1(GtkEntry *entry, gpointer user_data);
static void insert_disk2(GtkEntry *entry, gpointer user_data);
static void insert_disk3(GtkEntry *entry, gpointer user_data);
static void insert_disk4(GtkEntry *entry, gpointer user_data);
static void toggle_dc_window(GtkToggleAction *current, gpointer user_data);
static void toggle_pc_window(GtkToggleAction *current, gpointer user_data);
static void toggle_tc_window(GtkToggleAction *current, gpointer user_data);
static void toggle_tv_window(GtkToggleAction *current, gpointer user_data);
static void save_snapshot(GtkEntry *entry, gpointer user_data);
static void save_screenshot(GtkEntry *entry, gpointer user_data);
static void config_save(GtkEntry *entry, gpointer user_data);
static void toggle_config_autosave(GtkToggleAction *current, gpointer user_data);
static void do_quit(GtkEntry *entry, gpointer user_data);

// View menu callbacks

static void set_tv_input(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void set_ccr(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void toggle_inverse_text(GtkToggleAction *current, gpointer user_data);
static void zoom_in(GtkEntry *entry, gpointer user_data);
static void zoom_out(GtkEntry *entry, gpointer user_data);
static void zoom_reset(GtkEntry *entry, gpointer user_data);
static void set_fullscreen(GtkToggleAction *current, gpointer user_data);

// Hardware menu callbacks

static void set_machine(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void set_cart(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void set_keymap(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void set_joy_right(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void set_joy_left(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void swap_joysticks(GtkEntry *entry, gpointer user_data);
static void do_soft_reset(GtkEntry *entry, gpointer user_data);
static void do_hard_reset(GtkEntry *entry, gpointer user_data);

// Tool menu callbacks

static void set_hkbd_layout(GtkRadioAction *action, GtkRadioAction *current,
			    gpointer user_data);
static void set_hkbd_lang(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void toggle_keyboard_translation(GtkToggleAction *current, gpointer user_data);
static void toggle_ratelimit(GtkToggleAction *current, gpointer user_data);

// Help menu callbacks

static void show_about_window(GtkMenuItem *item, gpointer user_data);

// General callbacks

static void ui_gtk3_destroy(GtkWidget *w, gpointer user_data);

// UI message reception

static void uigtk3_ui_state_notify(void *, int tag, void *smsg);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// GTK+ actions

static GtkActionEntry const ui_entries[] = {
	// Top level
	{ .name = "FileMenuAction", .label = "_File" },
	{ .name = "ViewMenuAction", .label = "_View" },
	{ .name = "HardwareMenuAction", .label = "H_ardware" },
	{ .name = "ToolMenuAction", .label = "_Tool" },
	{ .name = "HelpMenuAction", .label = "_Help" },

	// File
	{ .name = "RunAction", .label = "_Run…",
	  .accelerator = "<shift><control>L",
	  .tooltip = "Load & attempt to autorun file",
	  .callback = G_CALLBACK(do_run_file) },
	{ .name = "LoadAction", .label = "_Load…",
	  .accelerator = "<control>L",
	  .tooltip = "Load file",
	  .callback = G_CALLBACK(do_load_file) },
	{ .name = "InsertDisk1Action",
	  .accelerator = "<control>1",
	  .callback = G_CALLBACK(insert_disk1) },
	{ .name = "InsertDisk2Action",
	  .accelerator = "<control>2",
	  .callback = G_CALLBACK(insert_disk2) },
	{ .name = "InsertDisk3Action",
	  .accelerator = "<control>3",
	  .callback = G_CALLBACK(insert_disk3) },
	{ .name = "InsertDisk4Action",
	  .accelerator = "<control>4",
	  .callback = G_CALLBACK(insert_disk4) },
	{ .name = "SaveSnapshotAction", .label = "_Save Snapshot…",
	  .accelerator = "<control>S",
	  .callback = G_CALLBACK(save_snapshot) },
	{ .name = "ScreenshotAction", .label = "Screenshot to PNG…",
	  .accelerator = "<control><shift>S",
	  .callback = G_CALLBACK(save_screenshot) },
	{ .name = "ConfigSaveAction", .label = "Save _configuration",
	  .callback = G_CALLBACK(config_save) },
	{ .name = "QuitAction", .label = "_Quit",
	  .accelerator = "<control>Q",
	  .tooltip = "Quit",
	  .callback = G_CALLBACK(do_quit) },

	// View
	{ .name = "TVInputMenuAction", .label = "_TV input" },
	{ .name = "CCRMenuAction", .label = "Composite _rendering" },
	{ .name = "ZoomMenuAction", .label = "_Zoom" },
	{ .name = "zoom_in", .label = "Zoom In",
	  .accelerator = "<control>plus",
	  .callback = G_CALLBACK(zoom_in) },
	{ .name = "zoom_out", .label = "Zoom Out",
	  .accelerator = "<control>minus",
	  .callback = G_CALLBACK(zoom_out) },
	{ .name = "zoom_reset", .label = "Reset",
	  .accelerator = "<control>0",
	  .callback = G_CALLBACK(zoom_reset) },

	// Hardware
	{ .name = "MachineMenuAction", .label = "_Machine" },
	{ .name = "CartridgeMenuAction", .label = "_Cartridge" },
	{ .name = "KeymapMenuAction", .label = "_Keyboard type" },
	{ .name = "JoyRightMenuAction", .label = "_Right joystick" },
	{ .name = "JoyLeftMenuAction", .label = "_Left joystick" },
	{ .name = "JoySwapAction", .label = "Swap _joysticks",
	  .accelerator = "<control><shift>J",
	  .callback = G_CALLBACK(swap_joysticks) },
	{ .name = "SoftResetAction", .label = "_Soft reset",
	  .accelerator = "<control>R",
	  .tooltip = "Soft reset machine (press reset)",
	  .callback = G_CALLBACK(do_soft_reset) },
	{ .name = "HardResetAction",
	  .label = "_Hard reset",
	  .accelerator = "<shift><control>R",
	  .tooltip = "Hard reset machine (power cycle)",
	  .callback = G_CALLBACK(do_hard_reset) },

	// Tool
	{ .name = "HKBDLayoutMenuAction", .label = "Keyboard la_yout" },
	{ .name = "HKBDLangMenuAction", .label = "Keyboard lan_guage" },

	// Help
	{ .name = "AboutAction",
	  .label = "_About",
	  .callback = G_CALLBACK(show_about_window) },
};

static GtkToggleActionEntry const ui_toggles[] = {
	// File
	{ .name = "TapeControlAction", .label = "Cassette _tapes",
	  .accelerator = "<control>T",
	  .callback = G_CALLBACK(toggle_tc_window) },
	{ .name = "DriveControlAction", .label = "Floppy/hard _disks",
	  .accelerator = "<control>D",
	  .callback = G_CALLBACK(toggle_dc_window) },
	{ .name = "PrinterControlAction", .label = "_Printer control",
	  .accelerator = "<control>P",
	  .callback = G_CALLBACK(toggle_pc_window) },
	{ .name = "ConfigAutosaveAction", .label = "_Autosave configuration",
	  .callback = G_CALLBACK(toggle_config_autosave) },

	// View
	{ .name = "VideoOptionsAction", .label = "TV _controls",
	  .accelerator = "<control><shift>V",
	  .callback = G_CALLBACK(toggle_tv_window) },
	{ .name = "InverseTextAction", .label = "_Inverse text",
	  .accelerator = "<shift><control>I",
	  .callback = G_CALLBACK(toggle_inverse_text) },
	{ .name = "FullScreenAction", .label = "_Full screen",
	  .accelerator = "F11",
	  .callback = G_CALLBACK(set_fullscreen) },

	// Tool
	{ .name = "TranslateKeyboardAction", .label = "_Keyboard translation",
	  .accelerator = "<control>Z",
	  .callback = G_CALLBACK(toggle_keyboard_translation) },
	{ .name = "RateLimitAction", .label = "_Rate limit",
	  .accelerator = "<shift>F12",
	  .callback = G_CALLBACK(toggle_ratelimit) },
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Create GTK+ UI module instance

static void ui_gtk3_free(void *);
static void ui_gtk3_run(void *);

static void *ui_gtk3_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;

	// Be sure we've not made more than one of these
	assert(global_uigtk3 == NULL);

#ifdef HAVE_X11
	// Force use of X11 backend.  If we don't do this, the call to
	// gdk_x11_get_default_xdisplay() later will still return some non-NULL
	// Display and I know of no way to tell that it's valid, which leads
	// to a crash later on.
	setenv("GDK_BACKEND", "x11", 0);
#endif

	gtk_init(NULL, NULL);

	g_set_application_name("XRoar");

#ifdef HAVE_X11
	Display *display = gdk_x11_get_default_xdisplay();
	hk_x11_set_display(display);
#endif

	GError *error = NULL;

	struct ui_gtk3_interface *uigtk3 = g_malloc(sizeof(*uigtk3));
	*uigtk3 = (struct ui_gtk3_interface){0};
	struct ui_interface *ui = &uigtk3->public;

	uigtk3->builder = gtk_builder_new();
	uigtk3_add_from_resource(uigtk3, "/uk/org/6809/xroar/gtk3/application.ui");

	// Make available globally for other GTK+ 3 code
	global_uigtk3 = uigtk3;
	uigtk3->cfg = cfg;

	ui->free = DELEGATE_AS0(void, ui_gtk3_free, uigtk3);
	ui->run = DELEGATE_AS0(void, ui_gtk3_run, uigtk3);

	// Register with messenger
	uigtk3->msgr_client_id = messenger_client_register();

	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_machine, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_cartridge, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_tape_dialog, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_disk_dialog, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_tv_dialog, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_ccr, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_tv_input, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_fullscreen, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_vdg_inverse, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_keymap, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_hkbd_layout, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_hkbd_lang, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_kbd_translate, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_ratelimit_latch, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_joystick_port, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));
	ui_messenger_join_group(uigtk3->msgr_client_id, ui_tag_config_autosave, MESSENGER_NOTIFY_DELEGATE(uigtk3_ui_state_notify, uigtk3));

	// Fetch top level window
	uigtk3->top_window = GTK_WIDGET(gtk_builder_get_object(uigtk3->builder, "top_window"));
	g_signal_connect(uigtk3->top_window, "destroy", G_CALLBACK(ui_gtk3_destroy), (gpointer)(intptr_t)0);
	// Fetch display for top level window.  It appears to be safe to do
	// this before it is show()n.
	uigtk3->display = gtk_widget_get_display(uigtk3->top_window);

	// Fetch vbox
	GtkWidget *vbox = GTK_WIDGET(gtk_builder_get_object(uigtk3->builder, "box"));

	// Create a UI from XML
	uigtk3->menu_manager = gtk_ui_manager_new();

	GBytes *res_ui = g_resources_lookup_data("/uk/org/6809/xroar/gtk3/menu.ui", 0, NULL);
	const gchar *ui_xml_string = g_bytes_get_data(res_ui, NULL);

	// Sigh, glib-compile-resources can strip blanks, but it then forcibly
	// adds an XML version tag, which gtk_ui_manager_add_ui_from_string()
	// objects to.  Skip to the second tag...
	if (ui_xml_string) {
		do { ui_xml_string++; } while (*ui_xml_string != '<');
	}
	// The proper way to do this (for the next five minutes) is probably to
	// transition to using GtkBuilder.
	gtk_ui_manager_add_ui_from_string(uigtk3->menu_manager, ui_xml_string, -1, &error);
	if (error) {
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
	}
	g_bytes_unref(res_ui);

	// Action groups
	GtkActionGroup *main_action_group = gtk_action_group_new("Main");
	gtk_ui_manager_insert_action_group(uigtk3->menu_manager, main_action_group, 0);

	// Set up main action group
	gtk_action_group_add_actions(main_action_group, ui_entries, G_N_ELEMENTS(ui_entries), uigtk3);
	gtk_action_group_add_toggle_actions(main_action_group, ui_toggles, G_N_ELEMENTS(ui_toggles), uigtk3);

	// Dynamic radio menus
	uigtk3->tv_input_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/ViewMenu/TVInputMenu", (GCallback)set_tv_input);
	uigtk3->ccr_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/ViewMenu/CCRMenu", (GCallback)set_ccr);
	uigtk3->machine_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/HardwareMenu/MachineMenu", (GCallback)set_machine);
	uigtk3->cart_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/HardwareMenu/CartridgeMenu", (GCallback)set_cart);
	uigtk3->keymap_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/HardwareMenu/KeymapMenu", (GCallback)set_keymap);
	uigtk3->joy_right_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/HardwareMenu/JoyRightMenu", (GCallback)set_joy_right);
	uigtk3->joy_left_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/HardwareMenu/JoyLeftMenu", (GCallback)set_joy_left);
	uigtk3->hkbd_layout_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/ToolMenu/HKBDLayoutMenu", (GCallback)set_hkbd_layout);
	uigtk3->hkbd_lang_radio_menu = uigtk3_radio_menu_new(uigtk3, "/MainMenu/ToolMenu/HKBDLangMenu", (GCallback)set_hkbd_lang);

	// Update all dynamic menus
	uigtk3_update_radio_menu_from_enum(uigtk3->tv_input_radio_menu, machine_tv_input_list, "tv-input-%s", NULL, 0);
	uigtk3_update_radio_menu_from_enum(uigtk3->ccr_radio_menu, vo_cmp_ccr_list, "ccr-%s", NULL, 0);
	ui->update_machine_menu = DELEGATE_AS0(void, gtk3_update_machine_menu, uigtk3);
	ui->update_cartridge_menu = DELEGATE_AS0(void, gtk3_update_cartridge_menu, uigtk3);
	ui->update_joystick_menus = DELEGATE_AS0(void, gtk3_update_joystick_menus, uigtk3);
	gtk3_update_machine_menu(uigtk3);
	gtk3_update_cartridge_menu(uigtk3);
	uigtk3_update_radio_menu_from_enum(uigtk3->keymap_radio_menu, machine_keyboard_list, "machine-keyboard-%s", NULL, 0);
	gtk3_update_joystick_menus(uigtk3);
	uigtk3_update_radio_menu_from_enum(uigtk3->hkbd_layout_radio_menu, hkbd_layout_list, "hkbd-layout-%s", NULL, 0);
	uigtk3_update_radio_menu_from_enum(uigtk3->hkbd_lang_radio_menu, hkbd_lang_list, "hkbd-lang-%s", NULL, 0);

	// Extract menubar widget and add to vbox
	uigtk3->menubar = gtk_ui_manager_get_widget(uigtk3->menu_manager, "/MainMenu");
	gtk_box_pack_start(GTK_BOX(vbox), uigtk3->menubar, FALSE, FALSE, 0);
	gtk_window_add_accel_group(GTK_WINDOW(uigtk3->top_window), gtk_ui_manager_get_accel_group(uigtk3->menu_manager));
	gtk_box_reorder_child(GTK_BOX(vbox), uigtk3->menubar, 0);

	// Create drawing_area widget, add to vbox
	uigtk3->drawing_area = GTK_WIDGET(gtk_builder_get_object(uigtk3->builder, "drawing_area"));
	GdkGeometry hints = {
		.min_width = 160, .min_height = 120,
		.base_width = 0, .base_height = 0,
	};
	gtk_window_set_geometry_hints(GTK_WINDOW(uigtk3->top_window), GTK_WIDGET(uigtk3->drawing_area), &hints, GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);
	gtk_widget_show(uigtk3->drawing_area);

	// Parse initial geometry
	if (ui_cfg->vo_cfg.geometry) {
		gtk_window_parse_geometry(GTK_WINDOW(uigtk3->top_window), ui_cfg->vo_cfg.geometry);
		uigtk3->user_specified_geometry = 1;
	}

	// Cursor hiding
	uigtk3->blank_cursor = gdk_cursor_new_for_display(uigtk3->display, GDK_BLANK_CURSOR);

	// Create (hidden) drive control window
	(void)gtk3_dc_dialog_new(uigtk3);

	// Create (hidden) printer control window
	(void)gtk3_pc_dialog_new(uigtk3);

	// Create (hidden) tape control window
	(void)gtk3_tc_dialog_new(uigtk3);

	// Create (hidden) video options window
	(void)gtk3_tv_dialog_new(uigtk3);

	// Video output
	if (!gtk3_vo_init(uigtk3)) {
		free(uigtk3);
		return NULL;
	}

	// File requester
	struct module *fr_module = module_select_by_arg(gtk3_filereq_module_list, ui_cfg->filereq);
	if (fr_module == &filereq_gtk3_module) {
		ui->filereq_interface = module_init(fr_module, "filereq", uigtk3);
	} else {
		ui->filereq_interface = module_init(fr_module, "filereq", NULL);
	}

	hk_init();
	GdkKeymap *gdk_keymap = gdk_keymap_get_for_display(gdk_display_get_default());
	gtk3_handle_keys_changed(gdk_keymap, NULL);
	g_signal_connect(G_OBJECT(gdk_keymap), "keys-changed", G_CALLBACK(gtk3_handle_keys_changed), NULL);

	// Connect relevant event signals
	g_signal_connect(G_OBJECT(uigtk3->top_window), "key-press-event", G_CALLBACK(gtk3_handle_key_press), uigtk3);
	g_signal_connect(G_OBJECT(uigtk3->top_window), "key-release-event", G_CALLBACK(gtk3_handle_key_release), uigtk3);
	g_signal_connect(G_OBJECT(uigtk3->top_window), "focus-in-event", G_CALLBACK(gtk3_handle_focus_in), uigtk3);
	g_signal_connect(G_OBJECT(uigtk3->drawing_area), "motion-notify-event", G_CALLBACK(gtk3_handle_motion_notify), uigtk3);
	g_signal_connect(G_OBJECT(uigtk3->drawing_area), "button-press-event", G_CALLBACK(gtk3_handle_button_press), uigtk3);
	g_signal_connect(G_OBJECT(uigtk3->drawing_area), "button-release-event", G_CALLBACK(gtk3_handle_button_release), uigtk3);

	// Any remaining signals
	gtk_builder_connect_signals(uigtk3->builder, uigtk3);

	// Ensure we get those events
	gtk_widget_add_events(uigtk3->top_window, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
	gtk_widget_add_events(uigtk3->drawing_area, GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

	return ui;
}

// Shut down GTK+ UI module instance

static void ui_gtk3_free(void *sptr) {
	struct ui_gtk3_interface *uigtk3 = sptr;
	DELEGATE_SAFE_CALL(uigtk3->public.filereq_interface->free);
	uigtk3_dialog_shutdown();
	slist_free_full(uigtk3->rm_list, (slist_free_func)uigtk3_radio_menu_free_void);
	slist_free_full(uigtk3->cbtv_list, (slist_free_func)uigtk3_cbt_value_free_void);
	g_object_unref(uigtk3->builder);
	gtk_widget_destroy(uigtk3->drawing_area);
	gtk_widget_destroy(uigtk3->top_window);
	messenger_client_unregister(uigtk3->msgr_client_id);
	// we can't actually have more than one, but i also can't stop myself
	// coding it like this:
	if (global_uigtk3 == uigtk3)
		global_uigtk3 = NULL;
	g_free(uigtk3);
}

// GTK+ module run() sets up xroar_run() as what to do when the interface is
// "idle", and runs the GTK main loop.

static gboolean run_cpu(gpointer data) {
	(void)data;
	xroar_run(EVENT_MS(10));
	return 1;
}

static void ui_gtk3_run(void *sptr) {
	struct ui_gtk3_interface *uigtk3 = sptr;
	g_idle_add(run_cpu, uigtk3->top_window);
	gtk_main();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI message reception

static void uigtk3_ui_state_notify(void *sptr, int tag, void *smsg) {
	struct ui_gtk3_interface *uigtk3 = sptr;
	struct ui_state_message *msg = smsg;
	int value = msg->value;
	const void *data = msg->data;

	switch (tag) {

	case ui_tag_config_autosave:
		uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/FileMenu/ConfigAutosave", value ? TRUE : FALSE);
		break;

	// Hardware

	case ui_tag_machine:
		uigtk3_radio_menu_set_current_value(uigtk3->machine_radio_menu, value);
		break;

	case ui_tag_cartridge:
		uigtk3_radio_menu_set_current_value(uigtk3->cart_radio_menu, value);
		break;

	// Cassettes

	case ui_tag_tape_dialog:
		uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/FileMenu/TapeControl", value ? TRUE : FALSE);
		break;

	// Floppy disks

	case ui_tag_disk_dialog:
		uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/FileMenu/DriveControl", value ? TRUE : FALSE);
		break;

	// Video

	case ui_tag_tv_dialog:
		uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/ViewMenu/VideoOptions", value ? TRUE : FALSE);
		break;

	case ui_tag_ccr:
		uigtk3_radio_menu_set_current_value(uigtk3->ccr_radio_menu, value);
		break;

	case ui_tag_tv_input:
		uigtk3_radio_menu_set_current_value(uigtk3->tv_input_radio_menu, value);
		break;

	case ui_tag_fullscreen:
		uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/ViewMenu/FullScreen", value ? TRUE : FALSE);
		break;

	case ui_tag_vdg_inverse:
		uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/ViewMenu/InverseText", value ? TRUE : FALSE);
		break;

	// Keyboard

	case ui_tag_keymap:
		uigtk3_radio_menu_set_current_value(uigtk3->keymap_radio_menu, value);
		break;

	case ui_tag_hkbd_layout:
		uigtk3_radio_menu_set_current_value(uigtk3->hkbd_layout_radio_menu, value);
		break;

	case ui_tag_hkbd_lang:
		uigtk3_radio_menu_set_current_value(uigtk3->hkbd_lang_radio_menu, value);
		break;

	case ui_tag_kbd_translate:
		uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/ToolMenu/TranslateKeyboard", value ? TRUE : FALSE);
		break;

	// Joysticks

	case ui_tag_joystick_port:
		if (value == 0) {
			uigtk3_notify_radio_menu_set_current_value(uigtk3->joy_right_radio_menu, (intptr_t)data);
		} else if (value == 1) {
			uigtk3_notify_radio_menu_set_current_value(uigtk3->joy_left_radio_menu, (intptr_t)data);
		}
		break;

	// Debugging

	case ui_tag_ratelimit_latch:
		uigtk3_toggle_action_set_active(uigtk3, "/MainMenu/ToolMenu/RateLimit", value ? TRUE : FALSE);
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Dynamic menus

// Dynamic machine menu

static void gtk3_update_machine_menu(void *sptr) {
	struct ui_gtk3_interface *uigtk3 = sptr;
	struct uigtk3_radio_menu *rm = uigtk3->machine_radio_menu;

	// Get list of machine configs
	struct slist *mcl = machine_config_list();
	int num_machines = slist_length(mcl);

	// Remove old entries
	uigtk3_free_action_group(rm->action_group);
	gtk_ui_manager_remove_ui(uigtk3->menu_manager, rm->merge_id);

	// Add entries
	GtkRadioActionEntry *entries = g_malloc0(num_machines * sizeof(*entries));
	int selected = -1;
	int i = 0;
	for (struct slist *iter = mcl; iter; iter = iter->next) {
		struct machine_config *mc = iter->data;
		entries[i].name = g_strdup_printf("machine%d", i+1);
		entries[i].label = uigtk3_escape_underscores(mc->description);
		entries[i].value = mc->id;
		gtk_ui_manager_add_ui(uigtk3->menu_manager, rm->merge_id, rm->path, entries[i].name, entries[i].name, GTK_UI_MANAGER_MENUITEM, FALSE);
		++i;
	}
	gtk_action_group_add_radio_actions(rm->action_group, entries, num_machines, selected, (GCallback)set_machine, uigtk3);

	// Free everything
	for (i = 0; i < num_machines; i++) {
		g_free((gpointer)entries[i].name);
		g_free((gpointer)entries[i].label);
	}
	g_free(entries);
}

// Dynamic cartridge menu

static void gtk3_update_cartridge_menu(void *sptr) {
	struct ui_gtk3_interface *uigtk3 = sptr;
	struct uigtk3_radio_menu *rm = uigtk3->cart_radio_menu;

	// Get list of cart configs
	struct slist *ccl = NULL;
	int num_carts = 0;
	struct cart *cart = NULL;
	if (xroar.machine) {
		const struct machine_partdb_entry *mpe = (const struct machine_partdb_entry *)xroar.machine->part.partdb;
		const char *cart_arch = mpe->cart_arch;
		ccl = cart_config_list_is_a(cart_arch);
		num_carts = slist_length(ccl);
		cart = (struct cart *)part_component_by_id(&xroar.machine->part, "cart");
	}

	// Remove old entries
	uigtk3_free_action_group(rm->action_group);
	gtk_ui_manager_remove_ui(uigtk3->menu_manager, rm->merge_id);

	// Add entries
	GtkRadioActionEntry *entries = g_malloc0((num_carts+1) * sizeof(*entries));
	int selected = 0;
	entries[0].name = "cart0";
	entries[0].label = "None";
	entries[0].value = 0;
	gtk_ui_manager_add_ui(uigtk3->menu_manager, rm->merge_id, rm->path, entries[0].name, entries[0].name, GTK_UI_MANAGER_MENUITEM, FALSE);
	int i = 1;
	for (struct slist *iter = ccl; iter; iter = iter->next) {
		struct cart_config *cc = iter->data;
		if (cart && cc == cart->config)
			selected = cc->id;
		entries[i].name = g_strdup_printf("cart%d", i+1);
		entries[i].label = uigtk3_escape_underscores(cc->description);
		entries[i].value = cc->id;
		gtk_ui_manager_add_ui(uigtk3->menu_manager, rm->merge_id, rm->path, entries[i].name, entries[i].name, GTK_UI_MANAGER_MENUITEM, FALSE);
		++i;
	}
	gtk_action_group_add_radio_actions(rm->action_group, entries, num_carts+1, selected, (GCallback)set_cart, uigtk3);

	// Free everything
	for (i = 1; i <= num_carts; i++) {
		g_free((gpointer)entries[i].name);
		g_free((gpointer)entries[i].label);
	}
	g_free(entries);
	slist_free(ccl);
}

// Dynamic joystick menus

static void update_joystick_menu(struct ui_gtk3_interface *uigtk3,
				 struct uigtk3_radio_menu *rm,
				 const char *name_fmt, const char *name0) {
	// Get list of joystick configs
	struct slist *jcl = joystick_config_list();

	int num_joystick_configs = slist_length(jcl);

	// Remove old entries
	uigtk3_free_action_group(rm->action_group);
	gtk_ui_manager_remove_ui(uigtk3->menu_manager, rm->merge_id);

	// Add entries
	GtkRadioActionEntry *entries = g_malloc0((num_joystick_configs+1) * sizeof(*entries));
	entries[0].name = name0;
	entries[0].label = "None";
	entries[0].value = 0;
	gtk_ui_manager_add_ui(uigtk3->menu_manager, rm->merge_id, rm->path, entries[0].name, entries[0].name, GTK_UI_MANAGER_MENUITEM, FALSE);
	int i = 1;
	for (struct slist *iter = jcl; iter; iter = iter->next) {
		struct joystick_config *jc = iter->data;
		entries[i].name = g_strdup_printf(name_fmt, i+1);
		entries[i].label = uigtk3_escape_underscores(jc->description);
		entries[i].value = jc->id;
		gtk_ui_manager_add_ui(uigtk3->menu_manager, rm->merge_id, rm->path, entries[i].name, entries[i].name, GTK_UI_MANAGER_MENUITEM, FALSE);
		++i;
	}
	gtk_action_group_add_radio_actions(rm->action_group, entries, num_joystick_configs+1, 0, rm->callback, uigtk3);

	// Free everything
	for (i = 1; i <= num_joystick_configs; i++) {
		g_free((gpointer)entries[i].name);
		g_free((gpointer)entries[i].label);
	}
	g_free(entries);
}

static void gtk3_update_joystick_menus(void *sptr) {
	struct ui_gtk3_interface *uigtk3 = sptr;

	update_joystick_menu(uigtk3, uigtk3->joy_right_radio_menu, "rjoy%d", "rjoy0");
	update_joystick_menu(uigtk3, uigtk3->joy_left_radio_menu, "ljoy%d", "ljoy0");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Callbacks

// File menu callbacks

static void do_load_file(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_load_file();
}

static void do_run_file(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_run_file();
}

// insert_disk*() don't actually appear in the menu any more, but are
// associated with accelerators:

static void insert_disk1(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	gtk3_insert_disk(uigtk3, 0);
}

static void insert_disk2(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	gtk3_insert_disk(uigtk3, 1);
}

static void insert_disk3(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	gtk3_insert_disk(uigtk3, 2);
}

static void insert_disk4(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	gtk3_insert_disk(uigtk3, 3);
}

static void toggle_dc_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(uigtk3->msgr_client_id, ui_tag_disk_dialog, val, NULL);
}

static void toggle_pc_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(uigtk3->msgr_client_id, ui_tag_print_dialog, val, NULL);
}

static void toggle_tc_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(uigtk3->msgr_client_id, ui_tag_tape_dialog, val, NULL);
}

static void toggle_tv_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(uigtk3->msgr_client_id, ui_tag_tv_dialog, val, NULL);
}

static void save_snapshot(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	g_idle_remove_by_data(uigtk3->top_window);
	xroar_save_snapshot();
	g_idle_add(run_cpu, uigtk3->top_window);
}

static void save_screenshot(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	g_idle_remove_by_data(uigtk3->top_window);
#ifdef SCREENSHOT
	xroar_screenshot();
#endif
	g_idle_add(run_cpu, uigtk3->top_window);
}

static void config_save(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_save_config_file();
}

static void toggle_config_autosave(GtkToggleAction *current, gpointer user_data) {
	(void)user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(-1, ui_tag_config_autosave, val, NULL);
}

static void do_quit(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_quit();
}

// View menu callbacks

static void set_tv_input(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)action;
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	ui_update_state(-1, ui_tag_tv_input, val, NULL);
}

static void set_ccr(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)action;
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	ui_update_state(-1, ui_tag_ccr, val, NULL);
}

static void toggle_inverse_text(GtkToggleAction *current, gpointer user_data) {
	(void)user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(-1, ui_tag_vdg_inverse, val, NULL);
}

static void zoom_in(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	ui_update_state(-1, ui_tag_zoom, UI_NEXT, NULL);
}

static void zoom_out(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	ui_update_state(-1, ui_tag_zoom, UI_PREV, NULL);
}

static void zoom_reset(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	ui_update_state(-1, ui_tag_zoom, 0, NULL);
}

static void set_fullscreen(GtkToggleAction *current, gpointer user_data) {
	(void)user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(-1, ui_tag_fullscreen, val, NULL);
}

// Hardware menu callbacks

static void set_machine(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	ui_update_state(-1, ui_tag_machine, val, NULL);
}

static void set_cart(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	ui_update_state(-1, ui_tag_cartridge, val, NULL);
}

static void set_keymap(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)action;
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	ui_update_state(-1, ui_tag_keymap, val, NULL);
}

static void set_joy_right(GtkRadioAction *action, GtkRadioAction *current,
			  gpointer user_data) {
	(void)action;
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	ui_update_state(-1, ui_tag_joystick_port, 0, (void *)(intptr_t)val);
}

static void set_joy_left(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)action;
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	ui_update_state(-1, ui_tag_joystick_port, 1, (void *)(intptr_t)val);
}

static void swap_joysticks(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	ui_update_state(-1, ui_tag_joystick_cycle, 1, NULL);
}

static void do_soft_reset(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_soft_reset();
}

static void do_hard_reset(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_hard_reset();
}

// Tool menu callbacks

static void set_hkbd_layout(GtkRadioAction *action, GtkRadioAction *current,
			    gpointer user_data) {
	(void)action;
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	ui_update_state(-1, ui_tag_hkbd_layout, val, NULL);
}

static void set_hkbd_lang(GtkRadioAction *action, GtkRadioAction *current,
			  gpointer user_data) {
	(void)action;
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	ui_update_state(-1, ui_tag_hkbd_lang, val, NULL);
}

static void toggle_keyboard_translation(GtkToggleAction *current, gpointer user_data) {
	(void)user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(-1, ui_tag_kbd_translate, val, NULL);
}

static void toggle_ratelimit(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	(void)uigtk3;
	gboolean val = gtk_toggle_action_get_active(current);
	ui_update_state(-1, ui_tag_ratelimit_latch, val, NULL);
}

// Help menu callbacks

static void show_about_window(GtkMenuItem *item, gpointer user_data) {
	(void)item;
	struct ui_gtk3_interface *uigtk3 = user_data;
	gtk3_create_about_window(uigtk3);
}

// General callbacks

// Work around gtk_exit() being deprecated:
static void ui_gtk3_destroy(GtkWidget *w, gpointer user_data) {
	(void)w;
	exit((intptr_t)user_data);
}
