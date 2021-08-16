/** \file
 *
 *  \brief GTK+2 user-interface module.
 *
 *  \copyright Copyright 2010-2021 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "pl-string.h"
#include "slist.h"

#include "cart.h"
#include "events.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "module.h"
#include "ui.h"
#include "vdrive.h"
#include "vo.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/drivecontrol.h"
#include "gtk2/tapecontrol.h"

static void *ui_gtk2_new(void *cfg);
static void ui_gtk2_free(void *sptr);
static void ui_gtk2_run(void *sptr);
static void ui_gtk2_set_state(void *sptr, int tag, int value, const void *data);

extern struct module vo_gtkgl_module;
extern struct module vo_null_module;
static struct module * const gtk2_vo_module_list[] = {
#ifdef HAVE_GTKGL
	&vo_gtkgl_module,
#endif
	&vo_null_module,
	NULL
};

// The GTK+ file-requester module can act standalone, but here we abstract it
// to pass the UI's top window to its initialiser.

// TODO: query UI module for appropriate interface and then try filereq
// modules.

static void *filereq_ui_gtk2_new(void *cfg);

struct module filereq_ui_gtk2_module = {
	.name = "gtk2", .description = "GTK+-2 file requester",
	.new = filereq_ui_gtk2_new
};
extern struct module filereq_gtk2_module;
extern struct module filereq_cli_module;
extern struct module filereq_null_module;

static struct module * const gtk2_filereq_module_list[] = {
	&filereq_ui_gtk2_module,
#ifdef HAVE_CLI
	&filereq_cli_module,
#endif
	&filereq_null_module,
	NULL
};

static void *filereq_ui_gtk2_new(void *cfg) {
	(void)cfg;
	return filereq_gtk2_module.new(global_uigtk2->top_window);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct ui_module ui_gtk2_module = {
	.common = { .name = "gtk2", .description = "GTK+-2 UI",
                    .new = ui_gtk2_new,
	},
	.filereq_module_list = gtk2_filereq_module_list,
	.vo_module_list = gtk2_vo_module_list,
	.joystick_module_list = gtk2_js_modlist,
};

/* Dynamic menus */
static void update_machine_menu(struct ui_gtk2_interface *uigtk2);
static void update_cartridge_menu(struct ui_gtk2_interface *uigtk2);

/* for hiding cursor: */
static gboolean hide_cursor(GtkWidget *widget, GdkEventMotion *event, gpointer data);
static gboolean show_cursor(GtkWidget *widget, GdkEventMotion *event, gpointer data);

static gboolean run_cpu(gpointer data);

/* Helpers */
static char *escape_underscores(const char *str);

/* This is just stupid... */
static void insert_disk(GtkEntry *entry, gpointer user_data) { (void)entry; gtk2_insert_disk(user_data, -1); }
static void insert_disk1(GtkEntry *entry, gpointer user_data) { (void)entry; gtk2_insert_disk(user_data, 0); }
static void insert_disk2(GtkEntry *entry, gpointer user_data) { (void)entry; gtk2_insert_disk(user_data, 1); }
static void insert_disk3(GtkEntry *entry, gpointer user_data) { (void)entry; gtk2_insert_disk(user_data, 2); }
static void insert_disk4(GtkEntry *entry, gpointer user_data) { (void)entry; gtk2_insert_disk(user_data, 3); }

static void save_snapshot(GtkEntry *entry, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)entry;
	g_idle_remove_by_data(uigtk2->top_window);
	xroar_save_snapshot();
	g_idle_add(run_cpu, uigtk2->top_window);
}

static void do_quit(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	xroar_quit();
}

static void do_soft_reset(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	xroar_soft_reset();
}

static void do_hard_reset(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	xroar_hard_reset();
}

static void zoom_1_1(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	if (!xroar_vo_interface)
		return;
	DELEGATE_SAFE_CALL(xroar_vo_interface->resize, 320, 240);
}

static void zoom_2_1(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	if (!xroar_vo_interface)
		return;
	DELEGATE_SAFE_CALL(xroar_vo_interface->resize, 640, 480);
}

static void zoom_in(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	if (!xroar_vo_interface)
		return;
	int xscale = uigtk2->display_rect.w / 160;
	int yscale = uigtk2->display_rect.h / 120;
	int scale = 1;
	if (xscale < yscale)
		scale = yscale;
	else if (xscale > yscale)
		scale = xscale;
	else
		scale = xscale + 1;
	if (scale < 1)
		scale = 1;
	DELEGATE_SAFE_CALL(xroar_vo_interface->resize, 160 * scale, 120 * scale);
}

static void zoom_out(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	if (!xroar_vo_interface)
		return;
	int xscale = uigtk2->display_rect.w / 160;
	int yscale = uigtk2->display_rect.h / 120;
	int scale = 1;
	if (xscale < yscale)
		scale = xscale;
	else if (xscale > yscale)
		scale = yscale;
	else
		scale = xscale - 1;
	if (scale < 1)
		scale = 1;
	DELEGATE_SAFE_CALL(xroar_vo_interface->resize, 160 * scale, 120 * scale);
}

static void toggle_inverse_text(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gboolean val = gtk_toggle_action_get_active(current);
	xroar_set_vdg_inverted_text(0, val);
}

static void set_fullscreen(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gboolean val = gtk_toggle_action_get_active(current);
	xroar_set_fullscreen(0, val);
}

static void set_ccr(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_cross_colour_renderer(0, val);
}

static void set_cc(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_cross_colour(0, val);
}

static void set_machine(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_machine(0, val);
}

static void set_cart(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	struct cart_config *cc = cart_config_by_id(val);
	xroar_set_cart(0, cc ? cc->name : NULL);
}

static void set_keymap(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_keymap(0, val);
}

static char const * const joystick_name[] = {
	NULL, "joy0", "joy1", "kjoy0", "mjoy0"
};

static void set_joy_right(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	(void)user_data;
	if (val >= 0 && val <= 4)
		xroar_set_joystick(0, 0, joystick_name[val]);
}

static void set_joy_left(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	(void)user_data;
	if (val >= 0 && val <= 4)
		xroar_set_joystick(0, 1, joystick_name[val]);
}

static void swap_joysticks(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_swap_joysticks(1);
}

static void toggle_keyboard_translation(GtkToggleAction *current, gpointer user_data) {
	gboolean val = gtk_toggle_action_get_active(current);
	(void)user_data;
	xroar_set_kbd_translate(0, val);
}

static void toggle_ratelimit(GtkToggleAction *current, gpointer user_data) {
	gboolean val = gtk_toggle_action_get_active(current);
	(void)user_data;
	xroar_set_ratelimit_latch(0, val);
}

static void close_about(GtkDialog *dialog, gint response_id, gpointer user_data) {
	(void)response_id;
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gtk_widget_hide(GTK_WIDGET(dialog));
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void about(GtkMenuItem *item, gpointer user_data) {
	(void)item;
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	GtkAboutDialog *dialog = (GtkAboutDialog *)gtk_about_dialog_new();
	gtk_about_dialog_set_version(dialog, VERSION);
	gtk_about_dialog_set_copyright(dialog, "Copyright © " PACKAGE_YEAR " Ciaran Anscomb <xroar@6809.org.uk>");
	gtk_about_dialog_set_license(dialog,
"XRoar is free software; you can redistribute it and/or modify it under\n"
"the terms of the GNU General Public License as published by the Free Free\n"
"Software Foundation, either version 3 of the License, or (at your option)\n"
"any later version.\n"
"\n"
"XRoar is distributed in the hope that it will be useful, but WITHOUT\n"
"ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or\n"
"FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License\n"
"for more details.\n"
"\n"
"You should have received a copy of the GNU General Public License along\n"
"with XRoar.  If not, see <https://www.gnu.org/licenses/>."
	);
	gtk_about_dialog_set_website(dialog, "http://www.6809.org.uk/xroar/");
	g_signal_connect(dialog, "response", G_CALLBACK(close_about), uigtk2);
	gtk_widget_show(GTK_WIDGET(dialog));
}

static void do_load_file(GtkEntry *entry, gpointer user_data) { (void)entry; (void)user_data; xroar_load_file(NULL); }
static void do_run_file(GtkEntry *entry, gpointer user_data) { (void)entry; (void)user_data; xroar_run_file(NULL); }

static GtkActionEntry const ui_entries[] = {
	/* Top level */
	{ .name = "FileMenuAction", .label = "_File" },
	{ .name = "ViewMenuAction", .label = "_View" },
	{ .name = "HardwareMenuAction", .label = "H_ardware" },
	{ .name = "ToolMenuAction", .label = "_Tool" },
	{ .name = "HelpMenuAction", .label = "_Help" },
	/* File */
	{ .name = "RunAction", .stock_id = GTK_STOCK_EXECUTE, .label = "_Run",
	  .accelerator = "<shift><control>L",
	  .tooltip = "Load and attempt to autorun a file",
	  .callback = G_CALLBACK(do_run_file) },
	{ .name = "LoadAction", .stock_id = GTK_STOCK_OPEN, .label = "_Load",
	  .accelerator = "<control>L",
	  .tooltip = "Load a file",
	  .callback = G_CALLBACK(do_load_file) },
	{ .name = "InsertDiskAction",
	  .label = "Insert _Disk",
	  .tooltip = "Load a virtual disk image",
	  .callback = G_CALLBACK(insert_disk) },
	{ .name = "InsertDisk1Action", .accelerator = "<control>1", .callback = G_CALLBACK(insert_disk1) },
	{ .name = "InsertDisk2Action", .accelerator = "<control>2", .callback = G_CALLBACK(insert_disk2) },
	{ .name = "InsertDisk3Action", .accelerator = "<control>3", .callback = G_CALLBACK(insert_disk3) },
	{ .name = "InsertDisk4Action", .accelerator = "<control>4", .callback = G_CALLBACK(insert_disk4) },
	{ .name = "SaveSnapshotAction", .stock_id = GTK_STOCK_SAVE_AS, .label = "_Save Snapshot",
	  .accelerator = "<control>S",
	  .callback = G_CALLBACK(save_snapshot) },
	{ .name = "QuitAction", .stock_id = GTK_STOCK_QUIT, .label = "_Quit",
	  .accelerator = "<control>Q",
	  .tooltip = "Quit",
	  .callback = G_CALLBACK(do_quit) },
	/* View */
	{ .name = "CCRMenuAction", .label = "Composite _Rendering" },
	{ .name = "CrossColourMenuAction", .label = "_Composite Phase" },
	{ .name = "ZoomMenuAction", .label = "_Zoom" },
	{ .name = "zoom_in", .label = "Zoom In",
	  .accelerator = "<control>plus",
	  .callback = G_CALLBACK(zoom_in) },
	{ .name = "zoom_out", .label = "Zoom Out",
	  .accelerator = "<control>minus",
	  .callback = G_CALLBACK(zoom_out) },
	{ .name = "zoom_320x240", .label = "320x240 (1:1)",
	  .callback = G_CALLBACK(zoom_1_1) },
	{ .name = "zoom_640x480", .label = "640x480 (2:1)",
	  .callback = G_CALLBACK(zoom_2_1) },
	{ .name = "zoom_reset", .label = "Reset",
	  .accelerator = "<control>0",
	  .callback = G_CALLBACK(zoom_2_1) },
	/* Hardware */
	{ .name = "MachineMenuAction", .label = "_Machine" },
	{ .name = "CartridgeMenuAction", .label = "_Cartridge" },
	{ .name = "KeymapMenuAction", .label = "_Keyboard Map" },
	{ .name = "JoyRightMenuAction", .label = "_Right Joystick" },
	{ .name = "JoyLeftMenuAction", .label = "_Left Joystick" },
	{ .name = "JoySwapAction", .label = "Swap _Joysticks",
	  .accelerator = "<control><shift>J",
	  .callback = G_CALLBACK(swap_joysticks) },
	{ .name = "SoftResetAction", .label = "_Soft Reset",
	  .accelerator = "<control>R",
	  .tooltip = "Soft Reset",
	  .callback = G_CALLBACK(do_soft_reset) },
	{ .name = "HardResetAction",
	  .label = "_Hard Reset",
	  .accelerator = "<shift><control>R",
	  .tooltip = "Hard Reset",
	  .callback = G_CALLBACK(do_hard_reset) },
	/* Help */
	{ .name = "AboutAction", .stock_id = GTK_STOCK_ABOUT,
	  .label = "_About",
	  .callback = G_CALLBACK(about) },
};

static GtkToggleActionEntry const ui_toggles[] = {
	/* View */
	{ .name = "InverseTextAction", .label = "_Inverse Text",
	  .accelerator = "<shift><control>I",
	  .callback = G_CALLBACK(toggle_inverse_text) },
	{ .name = "FullScreenAction", .label = "_Full Screen",
	  .stock_id = GTK_STOCK_FULLSCREEN,
	  .accelerator = "F11", .callback = G_CALLBACK(set_fullscreen) },
	/* Tool */
	{ .name = "TranslateKeyboardAction", .label = "_Keyboard Translation",
	  .accelerator = "<control>Z",
	  .callback = G_CALLBACK(toggle_keyboard_translation) },
	{ .name = "DriveControlAction", .label = "_Drive Control",
	  .accelerator = "<control>D",
	  .callback = G_CALLBACK(gtk2_toggle_dc_window) },
	{ .name = "TapeControlAction", .label = "_Tape Control",
	  .accelerator = "<control>T",
	  .callback = G_CALLBACK(gtk2_toggle_tc_window) },
	{ .name = "RateLimitAction", .label = "_Rate Limit",
	  .callback = G_CALLBACK(toggle_ratelimit) },
};

static GtkRadioActionEntry const ccr_radio_entries[] = {
	{ .name = "ccr-simple", .label = "Simple (2-bit LUT)", .value = UI_CCR_SIMPLE },
	{ .name = "ccr-5bit", .label = "5-bit LUT", .value = UI_CCR_5BIT },
#ifdef WANT_SIMULATED_NTSC
	{ .name = "ccr-simulated", .label = "Simulated", .value = UI_CCR_SIMULATED },
#endif
};

static GtkRadioActionEntry const cross_colour_radio_entries[] = {
	{ .name = "cc-none", .label = "None", .value = VO_PHASE_OFF },
	{ .name = "cc-blue-red", .label = "Blue-red", .value = VO_PHASE_KBRW },
	{ .name = "cc-red-blue", .label = "Red-blue", .value = VO_PHASE_KRBW },
};

static GtkRadioActionEntry const keymap_radio_entries[] = {
	{ .name = "keymap_dragon", .label = "Dragon Layout", .value = KEYMAP_DRAGON },
	{ .name = "keymap_dragon200e", .label = "Dragon 200-E Layout", .value = KEYMAP_DRAGON200E },
	{ .name = "keymap_coco", .label = "CoCo Layout", .value = KEYMAP_COCO },
};

static GtkRadioActionEntry const joy_right_radio_entries[] = {
	{ .name = "joy_right_none", .label = "None", .value = 0 },
	{ .name = "joy_right_joy0", .label = "Joystick 0", .value = 1 },
	{ .name = "joy_right_joy1", .label = "Joystick 1", .value = 2 },
	{ .name = "joy_right_kjoy0", .label = "Keyboard", .value = 3 },
	{ .name = "joy_right_mjoy0", .label = "Mouse", .value = 4 },
};

static GtkRadioActionEntry const joy_left_radio_entries[] = {
	{ .name = "joy_left_none", .label = "None", .value = 0 },
	{ .name = "joy_left_joy0", .label = "Joystick 0", .value = 1 },
	{ .name = "joy_left_joy1", .label = "Joystick 1", .value = 2 },
	{ .name = "joy_left_kjoy0", .label = "Keyboard", .value = 3 },
	{ .name = "joy_left_mjoy0", .label = "Mouse", .value = 4 },
};

static void *ui_gtk2_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;

	// Be sure we've not made more than one of these
	assert(global_uigtk2 == NULL);

	gtk_init(NULL, NULL);

	g_set_application_name("XRoar");

	GtkBuilder *builder;
	GError *error = NULL;
	builder = gtk_builder_new();

	GBytes *res_top_window = g_resources_lookup_data("/uk/org/6809/xroar/ui_gtk2/top_window.glade", 0, NULL);
	if (!gtk_builder_add_from_string(builder, g_bytes_get_data(res_top_window, NULL), -1, &error)) {
		g_warning("Couldn't create UI: %s", error->message);
		g_error_free(error);
		return NULL;
	}
	g_bytes_unref(res_top_window);

	struct ui_gtk2_interface *uigtk2 = g_malloc(sizeof(*uigtk2));
	*uigtk2 = (struct ui_gtk2_interface){0};
	struct ui_interface *ui = &uigtk2->public;
	// Make available globally for other GTK+2 code
	global_uigtk2 = uigtk2;
	uigtk2->cfg = cfg;

	ui->free = DELEGATE_AS0(void, ui_gtk2_free, ui);
	ui->run = DELEGATE_AS0(void, ui_gtk2_run, ui);
	ui->set_state = DELEGATE_AS3(void, int, int, cvoidp, ui_gtk2_set_state, ui);

	/* Fetch top level window */
	uigtk2->top_window = GTK_WIDGET(gtk_builder_get_object(builder, "top_window"));

	/* Fetch vbox */
	GtkWidget *vbox = GTK_WIDGET(gtk_builder_get_object(builder, "vbox1"));

	/* Create a UI from XML */
	uigtk2->menu_manager = gtk_ui_manager_new();

	GBytes *res_ui = g_resources_lookup_data("/uk/org/6809/xroar/ui_gtk2/ui.xml", 0, NULL);
	const gchar *ui_xml_string = g_bytes_get_data(res_ui, NULL);

	// Sigh, glib-compile-resources can strip blanks, but it then forcibly
	// adds an XML version tag, which gtk_ui_manager_add_ui_from_string()
	// objects to.  Skip to the second tag...
	if (ui_xml_string) {
		do { ui_xml_string++; } while (*ui_xml_string != '<');
	}
	// The proper way to do this (for the next five minutes) is probably to
	// transition to using GtkBuilder.
	gtk_ui_manager_add_ui_from_string(uigtk2->menu_manager, ui_xml_string, -1, &error);
	if (error) {
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
	}
	g_bytes_unref(res_ui);

	/* Action groups */
	GtkActionGroup *main_action_group = gtk_action_group_new("Main");
	uigtk2->machine_action_group = gtk_action_group_new("Machine");
	uigtk2->cart_action_group = gtk_action_group_new("Cartridge");
	gtk_ui_manager_insert_action_group(uigtk2->menu_manager, main_action_group, 0);
	gtk_ui_manager_insert_action_group(uigtk2->menu_manager, uigtk2->machine_action_group, 0);
	gtk_ui_manager_insert_action_group(uigtk2->menu_manager, uigtk2->cart_action_group, 0);

	/* Set up main action group */
	gtk_action_group_add_actions(main_action_group, ui_entries, G_N_ELEMENTS(ui_entries), uigtk2);
	gtk_action_group_add_toggle_actions(main_action_group, ui_toggles, G_N_ELEMENTS(ui_toggles), uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, keymap_radio_entries, G_N_ELEMENTS(keymap_radio_entries), 0, (GCallback)set_keymap, uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, joy_right_radio_entries, G_N_ELEMENTS(joy_right_radio_entries), 0, (GCallback)set_joy_right, uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, joy_left_radio_entries, G_N_ELEMENTS(joy_left_radio_entries), 0, (GCallback)set_joy_left, uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, ccr_radio_entries, G_N_ELEMENTS(ccr_radio_entries), 0, (GCallback)set_ccr, uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, cross_colour_radio_entries, G_N_ELEMENTS(cross_colour_radio_entries), 0, (GCallback)set_cc, uigtk2);

	/* Menu merge points */
	uigtk2->merge_machines = gtk_ui_manager_new_merge_id(uigtk2->menu_manager);
	uigtk2->merge_carts = gtk_ui_manager_new_merge_id(uigtk2->menu_manager);

	/* Update all dynamic menus */
	update_machine_menu(uigtk2);
	update_cartridge_menu(uigtk2);

	/* Extract menubar widget and add to vbox */
	uigtk2->menubar = gtk_ui_manager_get_widget(uigtk2->menu_manager, "/MainMenu");
	gtk_box_pack_start(GTK_BOX(vbox), uigtk2->menubar, FALSE, FALSE, 0);
	gtk_window_add_accel_group(GTK_WINDOW(uigtk2->top_window), gtk_ui_manager_get_accel_group(uigtk2->menu_manager));
	gtk_box_reorder_child(GTK_BOX(vbox), uigtk2->menubar, 0);

	/* Create drawing_area widget, add to vbox */
	uigtk2->drawing_area = GTK_WIDGET(gtk_builder_get_object(builder, "drawing_area"));
	GdkGeometry hints = {
		.min_width = 160, .min_height = 120,
		.base_width = 0, .base_height = 0,
	};
	gtk_window_set_geometry_hints(GTK_WINDOW(uigtk2->top_window), GTK_WIDGET(uigtk2->drawing_area), &hints, GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);
	gtk_widget_show(uigtk2->drawing_area);

	/* Parse initial geometry */
	if (ui_cfg->vo_cfg.geometry) {
		gtk_window_parse_geometry(GTK_WINDOW(uigtk2->top_window), ui_cfg->vo_cfg.geometry);
	}

	/* Cursor hiding */
	uigtk2->blank_cursor = gdk_cursor_new(GDK_BLANK_CURSOR);
	gtk_widget_add_events(uigtk2->drawing_area, GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK);
	g_signal_connect(G_OBJECT(uigtk2->top_window), "key-press-event", G_CALLBACK(hide_cursor), uigtk2);
	g_signal_connect(G_OBJECT(uigtk2->drawing_area), "motion-notify-event", G_CALLBACK(show_cursor), uigtk2);

	gtk_builder_connect_signals(builder, uigtk2);
	g_object_unref(builder);

	/* Create (hidden) drive control window */
	gtk2_create_dc_window(uigtk2);

	/* Create (hidden) tape control window */
	gtk2_create_tc_window(uigtk2);

	// Window geometry sensible defaults
	uigtk2->display_rect.w = 640;
	uigtk2->display_rect.h = 480;

	struct module *vo_mod = (struct module *)module_select_by_arg((struct module * const *)gtk2_vo_module_list, uigtk2->cfg->vo);
	if (!(uigtk2->public.vo_interface = module_init(vo_mod, uigtk2))) {
		return NULL;
	}

	gtk2_keyboard_init(ui_cfg);
	gtk2_joystick_init(uigtk2);

	return ui;
}

static void ui_gtk2_free(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	gtk_widget_destroy(uigtk2->drawing_area);
	gtk_widget_destroy(uigtk2->top_window);
	g_free(uigtk2);
}

static gboolean run_cpu(gpointer data) {
	(void)data;
	xroar_run(EVENT_MS(10));
	return 1;
}

static void ui_gtk2_run(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	g_idle_add(run_cpu, uigtk2->top_window);
	gtk_main();
}

static void ui_gtk2_set_state(void *sptr, int tag, int value, const void *data) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	GtkToggleAction *toggle;
	GtkRadioAction *radio;

	switch (tag) {

	/* Hardware */

	case ui_tag_machine:
		radio = (GtkRadioAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/HardwareMenu/MachineMenu/machine1");
		uigtk2_notify_radio_action_set(radio, value, set_machine, uigtk2);
		break;

	case ui_tag_cartridge:
		radio = (GtkRadioAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/HardwareMenu/CartridgeMenu/cart0");
		uigtk2_notify_radio_action_set(radio, value, set_cart, uigtk2);
		break;

	/* Tape */

	case ui_tag_tape_flags:
		gtk2_update_tape_state(uigtk2, value);
		break;

	case ui_tag_tape_input_filename:
		gtk2_input_tape_filename_cb(uigtk2, (const char *)data);
		break;

	case ui_tag_tape_output_filename:
		gtk2_output_tape_filename_cb(uigtk2, (const char *)data);
		break;

	/* Disk */

	case ui_tag_disk_write_enable:
		gtk2_update_drive_write_enable(value, (intptr_t)data);
		break;

	case ui_tag_disk_write_back:
		gtk2_update_drive_write_back(value, (intptr_t)data);
		break;

	case ui_tag_disk_data:
		gtk2_update_drive_disk(value, (const struct vdisk *)data);
		break;

	/* Video */

	case ui_tag_fullscreen:
		toggle = (GtkToggleAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ViewMenu/FullScreen");
		uigtk2_notify_toggle_action_set(toggle, value ? TRUE : FALSE, set_fullscreen, uigtk2);
		break;

	case ui_tag_vdg_inverse:
		toggle = (GtkToggleAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ViewMenu/InverseText");
		uigtk2_notify_toggle_action_set(toggle, value ? TRUE : FALSE, toggle_inverse_text, uigtk2);
		break;

	case ui_tag_ccr:
		radio = (GtkRadioAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ViewMenu/CCRMenu/ccr-simple");
		uigtk2_notify_radio_action_set(radio, value, set_ccr, uigtk2);
		break;

	case ui_tag_cross_colour:
		radio = (GtkRadioAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ViewMenu/CrossColourMenu/cc-none");
		uigtk2_notify_radio_action_set(radio, value, set_cc, uigtk2);
		break;

	/* Audio */

	case ui_tag_ratelimit:
		toggle = (GtkToggleAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ToolMenu/RateLimit");
		uigtk2_notify_toggle_action_set(toggle, value ? TRUE : FALSE, toggle_ratelimit, uigtk2);
		break;

	/* Keyboard */

	case ui_tag_keymap:
		radio = (GtkRadioAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/HardwareMenu/KeymapMenu/keymap_dragon");
		uigtk2_notify_radio_action_set(radio, value, set_keymap, uigtk2);
		break;

	case ui_tag_kbd_translate:
		toggle = (GtkToggleAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/ToolMenu/TranslateKeyboard");
		uigtk2_notify_toggle_action_set(toggle, value ? TRUE : FALSE, toggle_keyboard_translation, uigtk2);
		break;

	/* Joysticks */

	case ui_tag_joy_right:
	case ui_tag_joy_left:
		{
			gpointer func;
			if (tag == ui_tag_joy_right) {
				radio = (GtkRadioAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/HardwareMenu/JoyRightMenu/joy_right_none");
				func = set_joy_right;
			} else {
				radio = (GtkRadioAction *)gtk_ui_manager_get_action(uigtk2->menu_manager, "/MainMenu/HardwareMenu/JoyLeftMenu/joy_left_none");
				func = set_joy_left;
			}
			int joy = 0;
			if (data) {
				for (int i = 1; i < 5; i++) {
					if (0 == strcmp((const char *)data, joystick_name[i])) {
						joy = i;
						break;
					}
				}
			}
			uigtk2_notify_radio_action_set(radio, joy, func, uigtk2);
		}
		break;

	default:
		break;
	}
}

static void remove_action_from_group(gpointer data, gpointer user_data) {
	GtkAction *action = data;
	GtkActionGroup *action_group = user_data;
	gtk_action_group_remove_action(action_group, action);
}

static void free_action_group(GtkActionGroup *action_group) {
	GList *list = gtk_action_group_list_actions(action_group);
	g_list_foreach(list, remove_action_from_group, action_group);
	g_list_free(list);
}

/* Dynamic machine menu */
static void update_machine_menu(struct ui_gtk2_interface *uigtk2) {
	struct slist *mcl = slist_reverse(slist_copy(machine_config_list()));
	int num_machines = slist_length(mcl);
	int selected = -1;
	free_action_group(uigtk2->machine_action_group);
	gtk_ui_manager_remove_ui(uigtk2->menu_manager, uigtk2->merge_machines);
	GtkRadioActionEntry *radio_entries = g_malloc0(num_machines * sizeof(*radio_entries));
	// jump through alloc hoops just to avoid const-ness warnings
	gchar **names = g_malloc0(num_machines * sizeof(gchar *));
	gchar **labels = g_malloc0(num_machines * sizeof(gchar *));
	/* add these to the ui in reverse order, as each will be
	 * inserted before the previous */
	int i = 0;
	for (struct slist *iter = mcl; iter; iter = iter->next, i++) {
		struct machine_config *mc = iter->data;
		if (mc == xroar_machine_config)
			selected = mc->id;
		names[i] = g_strdup_printf("machine%d", i+1);
		radio_entries[i].name = names[i];
		labels[i] = escape_underscores(mc->description);
		radio_entries[i].label = labels[i];
		radio_entries[i].value = mc->id;
		gtk_ui_manager_add_ui(uigtk2->menu_manager, uigtk2->merge_machines, "/MainMenu/HardwareMenu/MachineMenu", radio_entries[i].name, radio_entries[i].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	}
	gtk_action_group_add_radio_actions(uigtk2->machine_action_group, radio_entries, num_machines, selected, (GCallback)set_machine, uigtk2);
	// back through the hoops
	for (i = 0; i < num_machines; i++) {
		g_free(names[i]);
		g_free(labels[i]);
	}
	g_free(names);
	g_free(labels);
	g_free(radio_entries);
	slist_free(mcl);
}

/* Dynamic cartridge menu */
static void update_cartridge_menu(struct ui_gtk2_interface *uigtk2) {
	struct slist *ccl = slist_reverse(slist_copy(cart_config_list()));
	int num_carts = slist_length(ccl);
	int selected = 0;
	free_action_group(uigtk2->cart_action_group);
	gtk_ui_manager_remove_ui(uigtk2->menu_manager, uigtk2->merge_carts);
	GtkRadioActionEntry *radio_entries = g_malloc0((num_carts+1) * sizeof(*radio_entries));
	// jump through alloc hoops just to avoid const-ness warnings
	// note: final entry's name & label is const, no need to allow space
	// for it in names[] & labels[]
	gchar **names = g_malloc0(num_carts * sizeof(gchar *));
	gchar **labels = g_malloc0(num_carts * sizeof(gchar *));
	/* add these to the ui in reverse order, as each will be
	   inserted before the previous */
	struct cart *cart = xroar_machine ? xroar_machine->get_interface(xroar_machine, "cart") : NULL;
	int i = 0;
	for (struct slist *iter = ccl; iter; iter = iter->next, i++) {
		struct cart_config *cc = iter->data;
		if (cart && cc == cart->config)
			selected = cc->id;
		names[i] = g_strdup_printf("cart%d", i+1);
		radio_entries[i].name = names[i];
		labels[i] = escape_underscores(cc->description);
		radio_entries[i].label = labels[i];
		radio_entries[i].value = cc->id;
		gtk_ui_manager_add_ui(uigtk2->menu_manager, uigtk2->merge_carts, "/MainMenu/HardwareMenu/CartridgeMenu", radio_entries[i].name, radio_entries[i].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	}
	radio_entries[num_carts].name = "cart0";
	radio_entries[num_carts].label = "None";
	radio_entries[num_carts].value = -1;
	gtk_ui_manager_add_ui(uigtk2->menu_manager, uigtk2->merge_carts, "/MainMenu/HardwareMenu/CartridgeMenu", radio_entries[num_carts].name, radio_entries[num_carts].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	gtk_action_group_add_radio_actions(uigtk2->cart_action_group, radio_entries, num_carts+1, selected, (GCallback)set_cart, uigtk2);
	// back through the hoops
	for (i = 0; i < num_carts; i++) {
		g_free(names[i]);
		g_free(labels[i]);
	}
	g_free(names);
	g_free(labels);
	g_free(radio_entries);
	slist_free(ccl);
}

/* Cursor hiding */

static gboolean hide_cursor(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk2_interface *uigtk2 = user_data;
#ifndef WINDOWS32
	if (uigtk2->cursor_hidden)
		return FALSE;
	GdkWindow *window = gtk_widget_get_window(uigtk2->drawing_area);
	uigtk2->old_cursor = gdk_window_get_cursor(window);
	gdk_window_set_cursor(window, uigtk2->blank_cursor);
	uigtk2->cursor_hidden = 1;
#endif
	return FALSE;
}

static gboolean show_cursor(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk2_interface *uigtk2 = user_data;
#ifndef WINDOWS32
	if (!uigtk2->cursor_hidden)
		return FALSE;
	GdkWindow *window = gtk_widget_get_window(uigtk2->drawing_area);
	gdk_window_set_cursor(window, uigtk2->old_cursor);
	uigtk2->cursor_hidden = 0;
#endif
	return FALSE;
}

/* Tool callbacks */

static char *escape_underscores(const char *str) {
	if (!str) return NULL;
	int len = strlen(str);
	const char *in;
	char *out;
	for (in = str; *in; in++) {
		if (*in == '_')
			len++;
	}
	char *ret_str = g_malloc(len + 1);
	for (in = str, out = ret_str; *in; in++) {
		*(out++) = *in;
		if (*in == '_') {
			*(out++) = '_';
		}
	}
	*out = 0;
	return ret_str;
}
