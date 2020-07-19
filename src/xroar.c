/*

XRoar - a Dragon/Tandy Coco emulator

Copyright 2003-2020 Ciaran Anscomb

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

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WANT_GDB_TARGET
#include <pthread.h>
#endif

#include "array.h"
#include "c-strcase.h"
#include "pl-string.h"
#include "sds.h"
#include "sdsx.h"
#include "slist.h"
#include "xalloc.h"

#include "ao.h"
#include "becker.h"
#include "cart.h"
#include "crclist.h"
#include "dkbd.h"
#include "events.h"
#include "fs.h"
#include "gdb.h"
#include "hd6309_trace.h"
#include "hexs19.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "mc6809_trace.h"
#include "module.h"
#include "mpi.h"
#include "part.h"
#include "path.h"
#include "printer.h"
#include "romlist.h"
#include "sam.h"
#include "snapshot.h"
#include "sound.h"
#include "tape.h"
#include "ui.h"
#include "vdg_palette.h"
#include "vdisk.h"
#include "vdrive.h"
#include "vo.h"
#include "wasm/wasm.h"
#include "xconfig.h"
#include "xroar.h"

#ifdef WINDOWS32
#include "windows32/common_windows32.h"
#endif

/* Configuration directives */

// Public

struct xroar_cfg xroar_cfg = {
	.disk_auto_os9 = 1,
	.disk_auto_sd = 1,
};

// Private

struct private_cfg {
	/* Emulated machine */
	char *default_machine;
	char *machine_desc;
	int machine_arch;
	int machine_keymap;
	int machine_cpu;
	char *machine_palette;
	char *bas;
	char *extbas;
	char *altbas;
	int nobas;
	int noextbas;
	int noaltbas;
	char *ext_charset;
	int tv;
	int vdg_type;
	char *machine_cart;
	int ram;
	int nodos;

	/* Emulated cartridge */
	char *cart_desc;
	char *cart_type;
	char *cart_rom;
	char *cart_rom2;
	int cart_becker;
	int cart_autorun;
	/* Deprecated */
	char *dos_option;

	/* Attach files */
	struct slist *load_list;
	char *run;
	char *tape_write;
	char *lp_file;
	char *lp_pipe;
	struct slist *type_list;

	/* Emulator interface */
	char *ui;
	char *filereq;
	char *ao;
	int volume;
	double gain;
	char *joy_right;
	char *joy_left;
	char *joy_virtual;
	char *joy_desc;
	int tape_fast;
	int tape_pad_auto;
	int tape_rewrite;
	int tape_ao_rate;

	char *joy_axis[JOYSTICK_NUM_AXES];
	char *joy_button[JOYSTICK_NUM_BUTTONS];

	_Bool config_print;
	_Bool config_print_all;
	char *timeout;
};

static struct private_cfg private_cfg = {
	.machine_arch = ANY_AUTO,
	.machine_keymap = ANY_AUTO,
	.machine_cpu = CPU_MC6809,
	.nobas = -1,
	.noextbas = -1,
	.noaltbas = -1,
	.tv = ANY_AUTO,
	.vdg_type = -1,
	.nodos = -1,
	.cart_becker = ANY_AUTO,
	.cart_autorun = ANY_AUTO,
	// if volume set >=0, use that, else use gain value in dB
	.volume = -1,
	.gain = -3.0,
	.tape_fast = 1,
	.tape_pad_auto = 1,
};

static struct ui_cfg xroar_ui_cfg = {
	.vo_cfg = {
		.gl_filter = UI_GL_FILTER_AUTO,
	},
};

static int ccr = UI_CCR_5BIT;

/* Helper functions used by configuration */
static void set_machine(const char *name);
static void set_pal(void);
static void set_ntsc(void);
static void set_cart(const char *name);
static void set_cart_type(const char *name);
static void set_gain(double gain);
static void set_joystick(const char *name);
static void set_joystick_axis(const char *spec);
static void set_joystick_button(const char *spec);

/* Help texts */
static void helptext(void);
static void versiontext(void);
static void config_print_all(FILE *f, _Bool all);

static int load_disk_to_drive = 0;

static struct joystick_config *cur_joy_config = NULL;

static struct xconfig_option const xroar_options[];

/**************************************************************************/
/* Global flags */

struct xroar_state {
	_Bool noratelimit_latch;
};

static struct xroar_state xroar_state = {
	.noratelimit_latch = 0,
};

static struct ui_interface *xroar_ui_interface;
static struct filereq_interface *xroar_filereq_interface;
struct vo_interface *xroar_vo_interface;
struct ao_interface *xroar_ao_interface;

struct machine_config *xroar_machine_config;
struct machine *xroar_machine;
struct tape_interface *xroar_tape_interface;
struct keyboard_interface *xroar_keyboard_interface;
struct printer_interface *xroar_printer_interface;
static struct cart_config *selected_cart_config;
struct vdg_palette *xroar_vdg_palette;

struct vdrive_interface *xroar_vdrive_interface;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Default configuration */

static char const * const default_config[] = {
	// Dragon 32
	"machine dragon32",
	"machine-desc 'Dragon 32'",
	"machine-arch dragon32",
	"tv-type pal",
	"ram 32",
	// Dragon 64
	"machine dragon64",
	"machine-desc 'Dragon 64'",
	"machine-arch dragon64",
	"tv-type pal",
	"ram 64",
	// Tano Dragon
	"machine tano",
	"machine-desc 'Tano Dragon (NTSC)'",
	"machine-arch dragon64",
	"tv-type ntsc",
	"ram 64",
	// Dragon 200-E
	"machine dragon200e",
	"machine-desc 'Dragon 200-E'",
	"machine-arch dragon64",
	"machine-keyboard dragon200e",
	"extbas @dragon200e",
	"altbas @dragon200e_alt",
	"ext-charset @dragon200e_charset",
	"tv-type pal",
	"ram 64",
	// CoCo
	"machine coco",
	"machine-desc 'Tandy CoCo (PAL)'",
	"machine-arch coco",
	"tv-type pal",
	"ram 64",
	// CoCo (US)
	"machine cocous",
	"machine-desc 'Tandy CoCo (NTSC)'",
	"machine-arch coco",
	"tv-type ntsc",
	"ram 64",
	// CoCo 2B
	"machine coco2b",
	"machine-desc 'Tandy CoCo 2B (PAL,T1)'",
	"machine-arch coco",
	"tv-type pal",
	"vdg-type 6847t1",
	"ram 64",
	// CoCo 2B (US)
	"machine coco2bus",
	"machine-desc 'Tandy CoCo 2B (NTSC,T1)'",
	"machine-arch coco",
	"tv-type ntsc",
	"vdg-type 6847t1",
	"ram 64",
	// Dynacom MX-1600
	"machine mx1600",
	"machine-desc 'Dynacom MX-1600'",
	"machine-arch coco",
	"bas @mx1600",
	"extbas @mx1600ext",
	"tv-type pal-m",
	"ram 64",

	// DragonDOS
	"cart dragondos",
	"cart-desc DragonDOS",
	"cart-type dragondos",
	"cart-rom @dragondos_compat",
	// RSDOS
	"cart rsdos",
	"cart-desc RS-DOS",
	"cart-type rsdos",
	"cart-rom @rsdos",
	// Delta
	"cart delta",
	"cart-desc 'Delta System'",
	"cart-type delta",
	"cart-rom @delta",
#ifndef HAVE_WASM
	// RSDOS w/ Becker port
	"cart becker",
	"cart-desc 'RS-DOS with becker port'",
	"cart-type rsdos",
	"cart-rom @rsdos_becker",
	"cart-becker",
#endif
	// Games Master Cartridge
	"cart gmc",
	"cart-desc 'Games Master Cartridge'",
	"cart-type gmc",
	"cart-autorun",
	// Orchestra 90
	"cart orch90",
	"cart-desc 'Orchestra-90 CC'",
	"cart-type orch90",
	"cart-rom orch90",
	"cart-autorun",
#ifndef HAVE_WASM
	// Multi-Pak Interface
	"cart mpi",
	"cart-desc 'Multi-Pak Interface'",
	"cart-type mpi",
	// IDE Cartridge
	"cart ide",
	"cart-desc 'IDE Interface'",
	"cart-type ide",
	"cart-rom @hdblba",
	"cart-becker",
	// nx32 memory cartridge
	"cart nx32",
	"cart-desc 'NX32 memory cartridge'",
	"cart-type nx32",
	// mooh memory cartridge
	"cart mooh",
	"cart-desc 'MOOH memory cartridge'",
	"cart-type mooh",
#endif

	// ROM lists

	// Fallback Dragon BASIC
	"romlist dragon=dragon",
	"romlist d64_1=d64_1,d64rom1,'Dragon Data Ltd - Dragon 64 - IC17','Dragon Data Ltd - TANO IC18','Eurohard S.A. - Dragon 200 IC18',dragrom",
	"romlist d64_2=d64_2,d64rom2,'Dragon Data Ltd - Dragon 64 - IC18','Dragon Data Ltd - TANO IC17','Eurohard S.A. - Dragon 200 IC17'",
	"romlist d32=d32,dragon32,d32rom,'Dragon Data Ltd - Dragon 32 - IC17'",
	"romlist d200e_1=d200e_1,d200e_rom1,ic18_v1.4e.ic34",
	"romlist d200e_2=d200e_2,d200e_rom2,ic17_v1.4e.ic37",
	// Specific Dragon BASIC
	"romlist dragon64=@d64_1,@dragon",
	"romlist dragon64_alt=@d64_2",
	"romlist dragon32=@d32,@dragon",
	"romlist dragon200e=@d200e_1,@d64_1,@dragon",
	"romlist dragon200e_alt=@d200e_2,@d64_2",
	"romlist dragon200e_charset=d200e_26,rom26.ic1",
	// Fallback CoCo BASIC
	"romlist coco=bas13,bas12,'Color Basic v1.2 (1982)(Tandy)',bas11,bas10",
	"romlist coco_ext=extbas11,extbas10,coco,COCO",
	// Specific CoCo BASIC
	"romlist coco1=bas10,@coco",
	"romlist coco1e=bas11,@coco",
	"romlist coco1e_ext=extbas10,@coco_ext",
	"romlist coco2=bas12,@coco",
	"romlist coco2_ext=extbas11,@coco_ext",
	"romlist coco2b=bas13,@coco",
	// MX-1600 and zephyr-patched version
	"romlist mx1600=mx1600bas,mx1600bas_zephyr",
	"romlist mx1600ext=mx1600extbas",
	// DragonDOS
	"romlist dragondos=ddos12a,ddos12,ddos40,ddos15,ddos10,'Dragon Data Ltd - DragonDOS 1.0'",
	"romlist dosplus=dplus49b,dplus48,dosplus-4.8,DOSPLUS",
	"romlist superdos=sdose6,'PNP - SuperDOS E6',sdose5,sdose4",
	"romlist cumana=cdos20,CDOS20",
	"romlist dragondos_compat=@dosplus,@superdos,@dragondos,@cumana",
	// RSDOS
	"romlist rsdos=disk11,disk10",
	// Delta
	"romlist delta=delta,deltados,'Premier Micros - DeltaDOS'",
	// RSDOS with becker port
	"romlist rsdos_becker=hdbdw3bck",

	// CRC lists

	// Dragon BASIC
	"crclist d64_1=0x84f68bf9,0x60a4634c,@woolham_d64_1",
	"crclist d64_2=0x17893a42,@woolham_d64_2",
	"crclist d32=0xe3879310,@woolham_d32",
	"crclist d200e_1=0x95af0a0a",
	"crclist dragon=@d64_1,@d32,@d200e_1",
	"crclist woolham_d64_1=0xee33ae92",
	"crclist woolham_d64_2=0x1660ae35",
	"crclist woolham_d32=0xff7bf41e,0x9c7eed69",
	// CoCo BASIC
	"crclist bas10=0x00b50aaa",
	"crclist bas11=0x6270955a",
	"crclist bas12=0x54368805",
	"crclist bas13=0xd8f4d15e",
	"crclist mx1600=0xd918156e,0xd11b1c96",  // 2nd is zephyr-patched
	"crclist coco=@bas13,@bas12,@bas11,@bas10,@mx1600",
	"crclist extbas10=0xe031d076,0x6111a086",  // 2nd is corrupt dump
	"crclist extbas11=0xa82a6254",
	"crclist mx1600ext=0x322a3d58",
	"crclist cocoext=@extbas11,@extbas10,@mx1600ext",
	"crclist coco_combined=@mx1600",

	// Joysticks
	"joy joy0",
	"joy-desc 'Physical joystick 0'",
	"joy-axis 0='physical:0,0'",
	"joy-axis 1='physical:0,1'",
	"joy-button 0='physical:0,0'",
	"joy-button 1='physical:0,1'",
	"joy joy1",
	"joy-desc 'Physical joystick 1'",
	"joy-axis 0='physical:1,0'",
	"joy-axis 1='physical:1,1'",
	"joy-button 0='physical:1,0'",
	"joy-button 1='physical:1,1'",
	"joy kjoy0",
	"joy-desc 'Virtual joystick 0'",
	"joy-axis 0='keyboard:'",
	"joy-axis 1='keyboard:'",
	"joy-button 0='keyboard:'",
	"joy-button 1='keyboard:'",
	"joy mjoy0",
	"joy-desc 'Mouse-joystick 0'",
	"joy-axis 0='mouse:'",
	"joy-axis 1='mouse:'",
	"joy-button 0='mouse:'",
	"joy-button 1='mouse:'",
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

const char *xroar_conf_path = NULL;
const char *xroar_rom_path = NULL;

struct event *xroar_ui_events = NULL;
struct event *xroar_machine_events = NULL;

static struct event load_file_event;
static void do_load_file(void *);
//static char *load_file = NULL;
static int autorun_loaded_file = 0;

static char const * const xroar_disk_exts[] = { "DMK", "JVC", "OS9", "VDK", "DSK", NULL };
static char const * const xroar_tape_exts[] = { "CAS", NULL };
static char const * const xroar_snap_exts[] = { "SNA", NULL };
/* static char const * const xroar_cart_exts[] = { "ROM", NULL }; */

static struct {
	const char *ext;
	int filetype;
} const filetypes[] = {
	{ "VDK", FILETYPE_VDK },
	{ "JVC", FILETYPE_JVC },
	{ "DSK", FILETYPE_JVC },
	{ "OS9", FILETYPE_OS9 },
	{ "DMK", FILETYPE_DMK },
	{ "BIN", FILETYPE_BIN },
	{ "DGN", FILETYPE_BIN },
	{ "CCO", FILETYPE_BIN },
	{ "HEX", FILETYPE_HEX },
	{ "CAS", FILETYPE_CAS },
	{ "WAV", FILETYPE_WAV },
	{ "SN",  FILETYPE_SNA },
	{ "ROM", FILETYPE_ROM },
	{ "CCC", FILETYPE_ROM },
	{ "BAS", FILETYPE_ASC },
	{ "ASC", FILETYPE_ASC },
	{ NULL, FILETYPE_UNKNOWN }
};

static struct vdg_palette *get_machine_palette(void);

/**************************************************************************/

#ifndef ROMPATH
# define ROMPATH "."
#endif
#ifndef CONFPATH
# define CONFPATH "."
#endif

struct ui_interface *xroar_init(int argc, char **argv) {
	int argn = 1, ret;
	char *conffile = NULL;
	_Bool no_conffile = 0;
	_Bool no_builtin = 0;
#ifdef WINDOWS32
	_Bool alloc_console = 0;
#endif

	/* Options that must come first on the command line, as they affect
	 * initial config & config file. */
	while (1) {
		if ((argn+1) < argc && 0 == strcmp(argv[argn], "-c")) {
			// -c, override conffile
			if (conffile)
				free(conffile);
			conffile = xstrdup(argv[argn+1]);
			argn += 2;
		} else if (argn < argc && 0 == strcmp(argv[argn], "-no-c")) {
			// -no-c, disable conffile
			no_conffile = 1;
			argn++;
		} else if (argn < argc && 0 == strcmp(argv[argn], "-no-builtin")) {
			// -no-builtin, disable builtin config
			no_builtin = 1;
			argn++;
#ifdef WINDOWS32
		} else if (argn < argc && 0 == strcmp(argv[argn], "-C")) {
			// Windows allocate console option
			alloc_console = 1;
			argn++;
		} else if (argn < argc && 0 == strcmp(argv[argn], "-no-C")) {
			// Windows allocate console option
			alloc_console = 0;
			argn++;
#endif
		} else {
			break;
		}
	}

#ifdef WINDOWS32
	windows32_init(alloc_console);
#endif

	machine_init();
	cart_init();

	xroar_conf_path = getenv("XROAR_CONF_PATH");
	if (!xroar_conf_path)
		xroar_conf_path = CONFPATH;

	for (unsigned i = 0; i < JOYSTICK_NUM_AXES; i++)
		private_cfg.joy_axis[i] = NULL;
	for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; i++)
		private_cfg.joy_button[i] = NULL;

	// Default configuration.
	if (!no_builtin) {
		for (unsigned i = 0; i < ARRAY_N_ELEMENTS(default_config); i++) {
			xconfig_parse_line(xroar_options, default_config[i]);
		}
		// Finish any machine or cart config in defaults.
		set_machine(NULL);
		set_cart(NULL);
		set_joystick(NULL);
		xroar_machine_config = NULL;
		selected_cart_config = NULL;
		cur_joy_config = NULL;
	}

	// If a configuration file is found, parse it.
	if (!no_conffile) {
		if (!conffile)
			conffile = find_in_path(xroar_conf_path, "xroar.conf");
		if (conffile) {
			(void)xconfig_parse_file(xroar_options, conffile);
			free(conffile);
		}
	}
	// Finish any machine or cart config in config file.
	set_machine(NULL);
	set_cart(NULL);
	set_joystick(NULL);
	// Don't auto-select last machine or cart in config file.
	xroar_machine_config = NULL;
	selected_cart_config = NULL;
	cur_joy_config = NULL;

	// Parse command line options.
	ret = xconfig_parse_cli(xroar_options, argc, argv, &argn);
	if (ret != XCONFIG_OK) {
		exit(EXIT_FAILURE);
	}
	// Set a default ROM search path if required.
	if (!xroar_rom_path) {
		char const *env = getenv("XROAR_ROM_PATH");
		if (!env)
			env = ROMPATH;
		if (env)
			xroar_rom_path = xstrdup(env);
	}
	// If no machine specified on command line, get default.
	if (!xroar_machine_config && private_cfg.default_machine) {
		xroar_machine_config = machine_config_by_name(private_cfg.default_machine);
	}
	// If that didn't work, just find the first one that will work.
	if (!xroar_machine_config) {
		xroar_machine_config = machine_config_first_working();
	}
	// Finish any machine or cart config on command line.
	set_machine(NULL);
	set_cart(NULL);
	set_joystick(NULL);

	// Help text

	// Useful for -vo help to list the video modules within all available UIs
	if (xroar_ui_cfg.vo && 0 == strcmp(xroar_ui_cfg.vo, "help")) {
		ui_print_vo_help();
		exit(EXIT_SUCCESS);
	}
	if (private_cfg.config_print) {
		config_print_all(stdout, 0);
		exit(EXIT_SUCCESS);
	}
	if (private_cfg.config_print_all) {
		config_print_all(stdout, 1);
		exit(EXIT_SUCCESS);
	}

	assert(xroar_machine_config != NULL);

	/* New vdrive interface */
	xroar_vdrive_interface = vdrive_interface_new();

	// Select a UI module.
	struct ui_module *ui_module = (struct ui_module *)module_select_by_arg((struct module * const *)ui_module_list, private_cfg.ui);
	if (ui_module == NULL) {
		if (ui_module_list) {
			ui_module = ui_module_list[0];
		}
		if (ui_module) {
			LOG_WARN("UI module `%s' not found: trying '%s'\n", private_cfg.ui, ui_module->common.name);
		} else {
			LOG_ERROR("UI module `%s' not found\n", private_cfg.ui);
			exit(EXIT_FAILURE);
		}
	}
	// Override other module lists if UI has an entry.
	if (ui_module->filereq_module_list != NULL)
		filereq_module_list = ui_module->filereq_module_list;
	if (ui_module->vo_module_list != NULL)
		vo_module_list = ui_module->vo_module_list;
	if (ui_module->ao_module_list != NULL)
		ao_module_list = ui_module->ao_module_list;
	// Select file requester, video & audio modules
	struct module *filereq_module = (struct module *)module_select_by_arg((struct module * const *)filereq_module_list, private_cfg.filereq);
	struct module *ao_module = module_select_by_arg((struct module * const *)ao_module_list, private_cfg.ao);
	ui_joystick_module_list = ui_module->joystick_module_list;

	/* Check other command-line options */
	if (xroar_cfg.frameskip < 0)
		xroar_cfg.frameskip = 0;

	// Remaining command line arguments are files.
	while (argn < argc) {
		if ((argn+1) < argc) {
			// important to set this as a command-line option
			char *opt[] = { "-load", argv[argn] };
			xconfig_parse_cli(xroar_options, 2, opt, NULL);
		} else {
			// Autorun last file given.
			private_cfg.run = argv[argn];
		}
		argn++;
	}
	_Bool autorun_last = 0;
	if (private_cfg.run) {
		// important to set this as a command-line option
		char *opt[] = { "-load", private_cfg.run };
		xconfig_parse_cli(xroar_options, 2, opt, NULL);
		autorun_last = 1;
		// XXX???  free(private_cfg.run);
		private_cfg.run = NULL;
	}

	private_cfg.tape_pad_auto = private_cfg.tape_pad_auto ? TAPE_PAD_AUTO : 0;
	private_cfg.tape_fast = private_cfg.tape_fast ? TAPE_FAST : 0;
	private_cfg.tape_rewrite = private_cfg.tape_rewrite ? TAPE_REWRITE : 0;

	_Bool no_auto_dos = xroar_machine_config->nodos;
	_Bool definitely_dos = 0;
	for (struct slist *tmp_list = private_cfg.load_list; tmp_list; tmp_list = tmp_list->next) {
		char *load_file = tmp_list->data;
		int load_file_type = xroar_filetype_by_ext(load_file);
		_Bool autorun = autorun_last && !tmp_list->next;
		switch (load_file_type) {
		// tapes - flag that DOS shouldn't be automatically found
		case FILETYPE_CAS:
		case FILETYPE_WAV:
		case FILETYPE_ASC:
		case FILETYPE_UNKNOWN:
			no_auto_dos = 1;
			break;
		// disks - flag that DOS should *definitely* be attempted
		case FILETYPE_VDK:
		case FILETYPE_JVC:
		case FILETYPE_OS9:
		case FILETYPE_DMK:
			// unless explicitly disabled
			if (!xroar_machine_config->nodos)
				definitely_dos = 1;
			break;
		// for cartridge ROMs, create a cart as machine default
		case FILETYPE_ROM:
			selected_cart_config = cart_config_by_name(load_file);
			selected_cart_config->autorun = autorun;
			break;
		// for the rest, wait until later
		default:
			break;
		}
	}
	if (definitely_dos) no_auto_dos = 0;

	/* Deprecated option overrides -cart-rom, forces DOS based on machine
	 * arch if not already chosen. */
	if (private_cfg.dos_option) {
		if (!selected_cart_config) {
			if (xroar_machine_config->architecture == ARCH_COCO) {
				selected_cart_config = cart_config_by_name("rsdos");
			} else {
				selected_cart_config = cart_config_by_name("dragondos");
			}
		}
		if (selected_cart_config) {
			if (selected_cart_config->rom)
				free(selected_cart_config->rom);
			selected_cart_config->rom = private_cfg.dos_option;
			private_cfg.dos_option = NULL;
		}
	}

	// Disable cart if necessary.
	if (!selected_cart_config && no_auto_dos) {
		xroar_machine_config->cart_enabled = 0;
	}
	// If any cart still configured, make it default for machine.
	if (selected_cart_config) {
		if (xroar_machine_config->default_cart)
			free(xroar_machine_config->default_cart);
		xroar_machine_config->default_cart = xstrdup(selected_cart_config->name);
	}

	/* Initial palette */
	xroar_vdg_palette = get_machine_palette();

	/* Initialise everything */
	event_current_tick = 0;
	/* ... modules */
	xroar_ui_interface = module_init((struct module *)ui_module, &xroar_ui_cfg);
	if (!xroar_ui_interface) {
		LOG_ERROR("No UI module initialised.\n");
		return NULL;
	}
	xroar_vo_interface = xroar_ui_interface->vo_interface;
	xroar_filereq_interface = module_init(filereq_module, NULL);
	if (filereq_module == NULL && filereq_module_list != NULL) {
		LOG_WARN("No file requester module initialised.\n");
	}
	if (!(xroar_ao_interface = module_init_from_list(ao_module_list, ao_module, NULL))) {
		LOG_ERROR("No audio module initialised.\n");
		return NULL;
	}
	if (private_cfg.volume >= 0) {
		sound_set_volume(xroar_ao_interface->sound_interface, private_cfg.volume);
	} else {
		sound_set_gain(xroar_ao_interface->sound_interface, private_cfg.gain);
	}
	/* ... subsystems */
	joystick_init();

	// Default joystick mapping
	if (private_cfg.joy_right) {
		xroar_set_joystick(1, 0, private_cfg.joy_right);
	} else {
		xroar_set_joystick(1, 0, "joy0");
	}
	if (private_cfg.joy_left) {
		xroar_set_joystick(1, 1, private_cfg.joy_left);
	} else {
		xroar_set_joystick(1, 1, "joy1");
	}
	if (private_cfg.joy_virtual) {
		joystick_set_virtual(joystick_config_by_name(private_cfg.joy_virtual));
	} else {
		joystick_set_virtual(joystick_config_by_name("kjoy0"));
	}

	/* Notify UI of starting options: */
	DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_fullscreen, xroar_ui_cfg.vo_cfg.fullscreen, NULL);
	xroar_set_kbd_translate(1, xroar_cfg.kbd_translate);

	xroar_tape_interface = tape_interface_new(xroar_ui_interface);
	if (private_cfg.tape_ao_rate > 0)
		tape_set_ao_rate(xroar_tape_interface, private_cfg.tape_ao_rate);

	/* Configure machine */
	xroar_configure_machine(xroar_machine_config);
	if (xroar_machine_config->cart_enabled) {
		xroar_set_cart(1, xroar_machine_config->default_cart);
	} else {
		xroar_set_cart(1, NULL);
	}
	/* Reset everything */
	xroar_hard_reset();
	tape_select_state(xroar_tape_interface, private_cfg.tape_fast | private_cfg.tape_pad_auto | private_cfg.tape_rewrite);

	load_disk_to_drive = 0;
	while (private_cfg.load_list) {
		sds load_file = private_cfg.load_list->data;
		int load_file_type = xroar_filetype_by_ext(load_file);
		// inhibit autorun if a -type option was given
		_Bool autorun = !private_cfg.type_list && autorun_last && !private_cfg.load_list->next;
		switch (load_file_type) {
		// cart will already be loaded (will autorun even with -type)
		case FILETYPE_ROM:
			sdsfree(load_file);
			break;
		// delay loading binary files by 2s
		case FILETYPE_BIN:
		case FILETYPE_HEX:
			event_init(&load_file_event, DELEGATE_AS0(void, do_load_file, load_file));
			load_file_event.at_tick = event_current_tick + EVENT_MS(2000);
			event_queue(&UI_EVENT_LIST, &load_file_event);
			autorun_loaded_file = autorun;
			break;
		// load disks then advice drive number
		case FILETYPE_VDK:
		case FILETYPE_JVC:
		case FILETYPE_OS9:
		case FILETYPE_DMK:
			xroar_load_file_by_type(load_file, autorun);
			load_disk_to_drive++;
			if (load_disk_to_drive > 3)
				load_disk_to_drive = 3;
			sdsfree(load_file);
			break;
		// the rest can be loaded straight off
		default:
			xroar_load_file_by_type(load_file, autorun);
			sdsfree(load_file);
			break;
		}
		private_cfg.load_list = slist_remove(private_cfg.load_list, private_cfg.load_list->data);
	}
	load_disk_to_drive = 0;

	if (private_cfg.tape_write) {
		int write_file_type = xroar_filetype_by_ext(private_cfg.tape_write);
		switch (write_file_type) {
			case FILETYPE_CAS:
			case FILETYPE_WAV:
				tape_open_writing(xroar_tape_interface, private_cfg.tape_write);
				DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_tape_output_filename, 0, private_cfg.tape_write);
				break;
			default:
				break;
		}
	}

	xroar_set_trace(xroar_cfg.trace_enabled);
	xroar_set_vdg_inverted_text(1, xroar_cfg.vdg_inverted_text);
	xroar_set_ratelimit_latch(1, XROAR_ON);

	if (private_cfg.timeout) {
		(void)xroar_set_timeout(private_cfg.timeout);
	}

	while (private_cfg.type_list) {
		sds data = private_cfg.type_list->data;
		keyboard_queue_basic_sds(xroar_keyboard_interface, data);
		private_cfg.type_list = slist_remove(private_cfg.type_list, data);
		sdsfree(data);
	}
	if (private_cfg.lp_file) {
		printer_open_file(xroar_printer_interface, private_cfg.lp_file);
	} else if (private_cfg.lp_pipe) {
		printer_open_pipe(xroar_printer_interface, private_cfg.lp_pipe);
	}
#ifdef HAVE_WASM
	if (xroar_machine_config) {
		xroar_set_machine(1, xroar_machine_config->id);
	}
#endif
	return xroar_ui_interface;
}

void xroar_shutdown(void) {
	static _Bool shutting_down = 0;
	if (shutting_down)
		return;
	shutting_down = 1;
	if (xroar_machine) {
		part_free((struct part *)xroar_machine);
		xroar_machine = NULL;
	}
	joystick_shutdown();
	cart_shutdown();
	mpi_shutdown();
	machine_shutdown();
	xroar_machine_config = NULL;
	if (xroar_ao_interface) {
		DELEGATE_SAFE_CALL0(xroar_ao_interface->free);
	}
	if (xroar_vo_interface) {
		DELEGATE_SAFE_CALL0(xroar_vo_interface->free);
	}
	if (xroar_filereq_interface) {
		DELEGATE_SAFE_CALL0(xroar_filereq_interface->free);
	}
	if (xroar_ui_interface) {
		DELEGATE_SAFE_CALL0(xroar_ui_interface->free);
	}
#ifdef WINDOWS32
	windows32_shutdown();
#endif
	romlist_shutdown();
	crclist_shutdown();
	for (unsigned i = 0; i < JOYSTICK_NUM_AXES; i++) {
		if (private_cfg.joy_axis[i])
			free(private_cfg.joy_axis[i]);
	}
	for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; i++) {
		if (private_cfg.joy_button[i])
			free(private_cfg.joy_button[i]);
	}
	vdrive_interface_free(xroar_vdrive_interface);
	tape_interface_free(xroar_tape_interface);
	xconfig_shutdown(xroar_options);
}

static struct vdg_palette *get_machine_palette(void) {
	struct vdg_palette *vp;
	vp = vdg_palette_by_name(xroar_machine_config->vdg_palette);
	if (!vp) {
		vp = vdg_palette_by_name("ideal");
		if (!vp) {
			vp = vdg_palette_index(0);
		}
	}
	return vp;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Called either by main() in a loop, or by a UI module's run().  Processes the
// UI event queue, then runs the machine for specified number of cycles.

void xroar_run(int ncycles) {
	event_run_queue(&UI_EVENT_LIST);
	if (!xroar_machine)
		return;
	switch (xroar_machine->run(xroar_machine, ncycles)) {
	case machine_run_state_stopped:
		DELEGATE_SAFE_CALL0(xroar_vo_interface->refresh);
		break;
	case machine_run_state_ok:
	default:
		break;
	}
}

int xroar_filetype_by_ext(const char *filename) {
	char *ext;
	int i;
	ext = strrchr(filename, '.');
	if (ext == NULL)
		return FILETYPE_UNKNOWN;
	ext++;
	for (i = 0; filetypes[i].ext; i++) {
		if (!c_strncasecmp(ext, filetypes[i].ext, strlen(filetypes[i].ext)))
			return filetypes[i].filetype;
	}
	return FILETYPE_UNKNOWN;
}

int xroar_load_file_by_type(const char *filename, int autorun) {
	int filetype;
	if (filename == NULL)
		return 1;
	int ret;
	filetype = xroar_filetype_by_ext(filename);
	switch (filetype) {
		case FILETYPE_VDK:
		case FILETYPE_JVC:
		case FILETYPE_OS9:
		case FILETYPE_DMK:
			xroar_insert_disk_file(load_disk_to_drive, filename);
			if (autorun && vdrive_disk_in_drive(xroar_vdrive_interface, 0)) {
				/* TODO: more intelligent recognition of the type of DOS
				 * we're talking to */
				switch (xroar_machine->config->architecture) {
				case ARCH_COCO:
					keyboard_queue_basic(xroar_keyboard_interface, "\\eDOS\\r");
					break;
				default:
					keyboard_queue_basic(xroar_keyboard_interface, "\\eBOOT\\r");
					break;
				}
				return 0;
			}
			return 1;
		case FILETYPE_BIN:
			return bin_load(filename, autorun);
		case FILETYPE_HEX:
			return intel_hex_read(filename, autorun);
		case FILETYPE_SNA:
			return read_snapshot(filename);
		case FILETYPE_ROM: {
			struct cart_config *cc;
			xroar_machine->remove_cart(xroar_machine);
			cc = cart_config_by_name(filename);
			if (cc) {
				cc->autorun = autorun;
				xroar_set_cart(1, cc->name);
				if (autorun) {
					xroar_hard_reset();
				}
			}
			}
			return 0;
		case FILETYPE_CAS:
		case FILETYPE_ASC:
		case FILETYPE_WAV:
		default:
			if (autorun) {
				ret = tape_autorun(xroar_tape_interface, filename);
			} else {
				ret = tape_open_reading(xroar_tape_interface, filename);
			}
			if (ret != -1) {
				DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_tape_input_filename, 0, filename);
			}
			break;
	}
	return ret;
}

static void do_load_file(void *data) {
	sds load_file = data;
	xroar_load_file_by_type(load_file, autorun_loaded_file);
	sdsfree(load_file);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct xroar_timeout {
	int seconds;
	int cycles;
	struct event event;
};

static void handle_timeout_event(void *sptr) {
	struct xroar_timeout *timeout = sptr;
	if (timeout->seconds == 0) {
		free(timeout);
		xroar_quit();
		return;
	}
	timeout->seconds--;
	if (timeout->seconds) {
		timeout->event.at_tick = event_current_tick + EVENT_S(1);
	} else {
		if (timeout->cycles == 0) {
			free(timeout);
			xroar_quit();
			return;
		}
		timeout->event.at_tick = event_current_tick + timeout->cycles;
	}
	event_queue(&MACHINE_EVENT_LIST, &timeout->event);
}

/* Configure a timeout (period after which emulator will exit). */

struct xroar_timeout *xroar_set_timeout(char const *timestring) {
	struct xroar_timeout *timeout = NULL;
	double t = strtod(timestring, NULL);
	if (t >= 0.0) {
		timeout = xmalloc(sizeof(*timeout));
		timeout->seconds = (int)t;
		timeout->cycles = EVENT_S(t - timeout->seconds);
		event_init(&timeout->event, DELEGATE_AS0(void, handle_timeout_event, timeout));
		/* handler can set up the first call for us... */
		timeout->seconds++;
		handle_timeout_event(timeout);
	}
	return timeout;
}

void xroar_cancel_timeout(struct xroar_timeout *timeout) {
	event_dequeue(&timeout->event);
	free(timeout);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Helper functions */

void xroar_set_trace(int mode) {
#ifdef TRACE
	int set_to;
	switch (mode) {
	case XROAR_OFF: default:
		set_to = 0;
		break;
	case XROAR_ON:
		set_to = 1;
		break;
	case XROAR_NEXT:
		set_to = 2;
		break;
	}
	xroar_cfg.trace_enabled = xroar_machine->set_trace(xroar_machine, set_to);
#endif
}

void xroar_new_disk(int drive) {
	char *filename = DELEGATE_CALL1(xroar_filereq_interface->save_filename, xroar_disk_exts);

	if (filename == NULL)
		return;
	int filetype = xroar_filetype_by_ext(filename);
	xroar_eject_disk(drive);
	// Default to 34T 1H.  Will be auto-expanded as necessary.
	struct vdisk *new_disk = vdisk_new(250000, 300);
	if (new_disk == NULL)
		return;
	LOG_DEBUG(1, "Creating blank disk in drive %d\n", 1 + drive);
	switch (filetype) {
		case FILETYPE_VDK:
		case FILETYPE_JVC:
		case FILETYPE_OS9:
		case FILETYPE_DMK:
			break;
		default:
			filetype = FILETYPE_DMK;
			break;
	}
	new_disk->filetype = filetype;
	new_disk->filename = xstrdup(filename);
	new_disk->write_back = 1;
	vdrive_insert_disk(xroar_vdrive_interface, drive, new_disk);
	if (xroar_ui_interface) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_disk_data, drive, new_disk);
	}
}

void xroar_insert_disk_file(int drive, const char *filename) {
	if (!filename) return;
	struct vdisk *disk = vdisk_load(filename);
	vdrive_insert_disk(xroar_vdrive_interface, drive, disk);
	if (xroar_ui_interface) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_disk_data, drive, disk);
	}
}

void xroar_insert_disk(int drive) {
	char *filename = DELEGATE_CALL1(xroar_filereq_interface->load_filename, xroar_disk_exts);
	xroar_insert_disk_file(drive, filename);
}

void xroar_eject_disk(int drive) {
	vdrive_eject_disk(xroar_vdrive_interface, drive);
	if (xroar_ui_interface) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_disk_data, drive, NULL);
	}
}

_Bool xroar_set_write_enable(_Bool notify, int drive, int action) {
	assert(drive >= 0 && drive < 4);
	struct vdisk *vd = vdrive_disk_in_drive(xroar_vdrive_interface, drive);
	if (!vd)
		return 0;
	_Bool new_we = !vd->write_protect;
	switch (action) {
	case XROAR_NEXT:
		new_we = !new_we;
		break;
	default:
		new_we = action;
		break;
	}
	vd->write_protect = !new_we;
	if (notify && xroar_ui_interface) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_disk_write_enable, drive, (void *)(uintptr_t)new_we);
	}
	return new_we;
}

_Bool xroar_set_write_back(_Bool notify, int drive, int action) {
	assert(drive >= 0 && drive < 4);
	struct vdisk *vd = vdrive_disk_in_drive(xroar_vdrive_interface, drive);
	if (!vd)
		return 0;
	_Bool new_wb = vd->write_back;
	switch (action) {
	case XROAR_NEXT:
		new_wb = !new_wb;
		break;
	default:
		new_wb = action;
		break;
	}
	vd->write_back = new_wb;
	if (notify && xroar_ui_interface) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_disk_write_back, drive, (void *)(uintptr_t)new_wb);
	}
	return new_wb;
}

void xroar_set_cross_colour_renderer(_Bool notify, int action) {
	switch (action) {
	case UI_CCR_SIMPLE:
	case UI_CCR_5BIT:
	case UI_CCR_SIMULATED:
		ccr = action;
		break;
	default:
		ccr = UI_CCR_5BIT;
		break;
	}
	xroar_set_cross_colour(0, xroar_machine_config->cross_colour_phase);
	if (notify) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_ccr, ccr, NULL);
	}
}

void xroar_set_cross_colour(_Bool notify, int action) {
	switch (action) {
	case XROAR_NEXT:
		xroar_machine_config->cross_colour_phase++;
		xroar_machine_config->cross_colour_phase %= NUM_VO_PHASES;
		break;
	default:
		xroar_machine_config->cross_colour_phase = action;
		break;
	}
	if (xroar_machine->set_vo_cmp) {
		if (xroar_machine_config->cross_colour_phase == VO_PHASE_OFF) {
			xroar_machine->set_vo_cmp(xroar_machine, VO_CMP_PALETTE);
			DELEGATE_SAFE_CALL1(xroar_vo_interface->set_vo_cmp, VO_CMP_PALETTE);
		} else {
			switch (ccr) {
			default:
				xroar_machine->set_vo_cmp(xroar_machine, VO_CMP_PALETTE);
				DELEGATE_SAFE_CALL1(xroar_vo_interface->set_vo_cmp, VO_CMP_PALETTE);
				break;
			case UI_CCR_SIMPLE:
				xroar_machine->set_vo_cmp(xroar_machine, VO_CMP_PALETTE);
				DELEGATE_SAFE_CALL1(xroar_vo_interface->set_vo_cmp, VO_CMP_2BIT);
				break;
			case UI_CCR_5BIT:
				xroar_machine->set_vo_cmp(xroar_machine, VO_CMP_PALETTE);
				DELEGATE_SAFE_CALL1(xroar_vo_interface->set_vo_cmp, VO_CMP_5BIT);
				break;
			case UI_CCR_SIMULATED:
				xroar_machine->set_vo_cmp(xroar_machine, VO_CMP_SIMULATED);
				DELEGATE_SAFE_CALL1(xroar_vo_interface->set_vo_cmp, VO_CMP_SIMULATED);
				break;
			}
		}
	}
	if (notify) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_cross_colour, xroar_machine_config->cross_colour_phase, NULL);
	}
}

void xroar_set_vdg_inverted_text(_Bool notify, int action) {
	_Bool state = xroar_machine->set_inverted_text(xroar_machine, action);
	if (notify) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_vdg_inverse, state, NULL);
	}
}

void xroar_set_fast_sound(_Bool notify, int action) {
	_Bool state = xroar_machine->set_fast_sound(xroar_machine, action);
	if (notify) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_fast_sound, state, NULL);
	}
}

void xroar_set_ratelimit(int action) {
	if (xroar_state.noratelimit_latch)
		return;
	if (action) {
		xroar_machine->set_frameskip(xroar_machine, xroar_cfg.frameskip);
		xroar_machine->set_ratelimit(xroar_machine, 1);
	} else {
		xroar_machine->set_frameskip(xroar_machine, 10);
		xroar_machine->set_ratelimit(xroar_machine, 0);
	}
}

void xroar_set_ratelimit_latch(_Bool notify, int action) {
	_Bool state = !xroar_state.noratelimit_latch;
	switch (action) {
	case XROAR_ON:
	default:
		state = 1;
		break;
	case XROAR_OFF:
		state = 0;
		break;
	case XROAR_NEXT:
		state = !state;
		break;
	}
	xroar_state.noratelimit_latch = !state;
	if (state) {
		xroar_machine->set_frameskip(xroar_machine, xroar_cfg.frameskip);
		xroar_machine->set_ratelimit(xroar_machine, 1);
	} else {
		xroar_machine->set_frameskip(xroar_machine, 10);
		xroar_machine->set_ratelimit(xroar_machine, 0);
	}
	if (notify) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_ratelimit, state, NULL);
	}
}

void xroar_set_pause(_Bool notify, int action) {
	_Bool state = xroar_machine->set_pause(xroar_machine, action);
	// TODO: UI indication of paused state
	(void)notify;
	(void)state;
}

void xroar_quit(void) {
	xroar_shutdown();
	exit(EXIT_SUCCESS);
}

void xroar_set_fullscreen(_Bool notify, int action) {
	_Bool set_to;
	switch (action) {
		case XROAR_OFF:
			set_to = 0;
			break;
		case XROAR_ON:
			set_to = 1;
			break;
		case XROAR_NEXT:
		default:
			set_to = !xroar_vo_interface->is_fullscreen;
			break;
	}
	DELEGATE_SAFE_CALL1(xroar_vo_interface->set_fullscreen, set_to);
	if (notify) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_fullscreen, set_to, NULL);
	}
}

void xroar_load_file(char const * const *exts) {
	char *filename = DELEGATE_CALL1(xroar_filereq_interface->load_filename, exts);
	if (filename) {
		xroar_load_file_by_type(filename, 0);
	}
}

void xroar_run_file(char const * const *exts) {
	char *filename = DELEGATE_CALL1(xroar_filereq_interface->load_filename, exts);
	if (filename) {
		xroar_load_file_by_type(filename, 1);
	}
}

void xroar_set_keymap(_Bool notify, int map) {
	int new;
	switch (map) {
		case XROAR_NEXT:
			// fudge the cycle order...
			switch (xroar_machine_config->keymap) {
			case dkbd_layout_dragon:
				new = dkbd_layout_dragon200e;
				break;
			case dkbd_layout_dragon200e:
				new = dkbd_layout_coco;
				break;
			case dkbd_layout_coco:
			default:
				new = dkbd_layout_dragon;
				break;
			}
			break;
		default:
			new = map;
			break;
	}
	if (new >= 0 && new < NUM_KEYMAPS) {
		keyboard_set_keymap(xroar_keyboard_interface, new);
		if (notify) {
			DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_keymap, new, NULL);
		}
	}
}

void xroar_set_kbd_translate(_Bool notify, int kbd_translate) {
	switch (kbd_translate) {
		case XROAR_NEXT:
			xroar_cfg.kbd_translate = !xroar_cfg.kbd_translate;
			break;
		default:
			xroar_cfg.kbd_translate = kbd_translate;
			break;
	}
	if (notify) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_kbd_translate, xroar_cfg.kbd_translate, NULL);
	}
}

static void update_ui_joysticks(int port) {
	const char *name = NULL;
	if (joystick_port_config[port] && joystick_port_config[port]->name) {
		name = joystick_port_config[port]->name;
	}
	DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_joy_right + port, 0, name);
}

void xroar_set_joystick(_Bool notify, int port, const char *name) {
	if (port < 0 || port > 1)
		return;
	if (name && *name) {
		joystick_map(joystick_config_by_name(name), port);
	} else {
		joystick_unmap(port);
	}
	if (notify)
		update_ui_joysticks(port);
}

void xroar_swap_joysticks(_Bool notify) {
	joystick_swap();
	if (notify) {
		update_ui_joysticks(0);
		update_ui_joysticks(1);
	}
}

void xroar_cycle_joysticks(_Bool notify) {
	joystick_cycle();
	if (notify) {
		update_ui_joysticks(0);
		update_ui_joysticks(1);
	}
}

void xroar_configure_machine(struct machine_config *mc) {
	if (xroar_machine) {
		part_free((struct part *)xroar_machine);
	}
	xroar_machine_config = mc;
	xroar_machine = machine_new(mc, xroar_vo_interface, xroar_ao_interface->sound_interface, xroar_tape_interface);
	tape_interface_connect_machine(xroar_tape_interface, xroar_machine);
	xroar_keyboard_interface = xroar_machine->get_interface(xroar_machine, "keyboard");
	xroar_printer_interface = xroar_machine->get_interface(xroar_machine, "printer");
	if (xroar_ui_interface) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_cartridge, -1, NULL);
	}
	switch (mc->architecture) {
	case ARCH_COCO:
		vdisk_set_interleave(VDISK_SINGLE_DENSITY, 5);
		vdisk_set_interleave(VDISK_DOUBLE_DENSITY, 5);
		break;
	default:
		vdisk_set_interleave(VDISK_SINGLE_DENSITY, 2);
		vdisk_set_interleave(VDISK_DOUBLE_DENSITY, 2);
		break;
	}
	mc->cross_colour_phase = (mc->tv_standard == TV_PAL) ? VO_PHASE_OFF : VO_PHASE_KBRW;
	xroar_set_cross_colour_renderer(1, ccr);
}

void xroar_set_machine(_Bool notify, int id) {
	int new = xroar_machine_config->id;
	struct slist *mcl, *mcc;
	switch (id) {
		case XROAR_NEXT:
			mcl = machine_config_list();
			mcc = slist_find(mcl, xroar_machine_config);
			if (mcc && mcc->next) {
				new = ((struct machine_config *)mcc->next->data)->id;
			} else {
				new = ((struct machine_config *)mcl->data)->id;
			}
			break;
		default:
			new = (id >= 0 ? id : 0);
			break;
	}
	struct machine_config *mc = machine_config_by_id(new);
#ifdef HAVE_WASM
	if (!wasm_ui_prepare_machine(mc)) {
		if (mc->default_cart) {
			struct cart_config *cc = cart_config_by_name(mc->default_cart);
			wasm_ui_prepare_cartridge(cc);
		}
		return;
	}
#endif
	xroar_configure_machine(mc);
	if (mc->cart_enabled) {
		xroar_set_cart(1, mc->default_cart);
	} else {
		xroar_set_cart(1, NULL);
	}
	xroar_vdg_palette = get_machine_palette();
	DELEGATE_SAFE_CALL0(xroar_vo_interface->update_palette);
	xroar_hard_reset();
	if (notify) {
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_machine, new, NULL);
	}
}

void xroar_toggle_cart(void) {
	assert(xroar_machine_config != NULL);
	xroar_machine_config->cart_enabled = !xroar_machine_config->cart_enabled;
	if (xroar_machine_config->cart_enabled) {
		xroar_set_cart(1, xroar_machine_config->default_cart);
	} else {
		xroar_set_cart(1, NULL);
	}
}

void xroar_set_cart_by_id(_Bool notify, int id) {
	struct cart_config *cc = cart_config_by_id(id);
	const char *name = cc ? cc->name : NULL;
#ifdef HAVE_WASM
	if (!wasm_ui_prepare_cartridge(cc)) {
		return;
	}
#endif
	xroar_set_cart(notify, name);
}

void xroar_set_cart(_Bool notify, const char *cc_name) {
	assert(xroar_machine_config != NULL);

	struct cart *old_cart = xroar_machine->get_interface(xroar_machine, "cart");
	if (!old_cart && !cc_name)
		return;
	// This trips GCC-10's static analyser at the moment, as it doesn't
	// seem to account for the short-circuit "&&".
	if (old_cart && cc_name && 0 == strcmp(cc_name, old_cart->config->name))
		return;
	xroar_machine->remove_cart(xroar_machine);

	struct cart *new_cart = NULL;
	if (!cc_name) {
		xroar_machine_config->cart_enabled = 0;
	} else {
		if (xroar_machine_config->default_cart != cc_name) {
			free(xroar_machine_config->default_cart);
			xroar_machine_config->default_cart = xstrdup(cc_name);
		}
		xroar_machine_config->cart_enabled = 1;
		new_cart = cart_new_named(cc_name);
		if (new_cart) {
			xroar_machine->insert_cart(xroar_machine, new_cart);
			if (new_cart->has_interface) {
				if (new_cart->has_interface(new_cart, "floppy")) {
					new_cart->attach_interface(new_cart, "floppy", xroar_vdrive_interface);
				}
				if (new_cart->has_interface(new_cart, "sound")) {
					new_cart->attach_interface(new_cart, "sound", xroar_ao_interface->sound_interface);
				}
			}
			// Reset the cart once all interfaces are attached
			if (new_cart->reset)
				new_cart->reset(new_cart);
		}
	}

	if (notify) {
		int id = new_cart ? new_cart->config->id : -1;
		DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_cartridge, id, NULL);
	}
}

/* For old snapshots */
void xroar_set_dos(int dos_type) {
	switch (dos_type) {
	case DOS_DRAGONDOS:
		xroar_set_cart(1, "dragondos");
		break;
	case DOS_RSDOS:
		xroar_set_cart(1, "rsdos");
		break;
	case DOS_DELTADOS:
		xroar_set_cart(1, "delta");
		break;
	default:
		break;
	}
}

void xroar_save_snapshot(void) {
	char *filename = DELEGATE_CALL1(xroar_filereq_interface->save_filename, xroar_snap_exts);
	if (filename) {
		write_snapshot(filename);
	}
}

void xroar_insert_input_tape_file(const char *filename) {
	if (!filename) return;
	tape_open_reading(xroar_tape_interface, filename);
	DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_tape_input_filename, 0, filename);
}

void xroar_insert_input_tape(void) {
	char *filename = DELEGATE_CALL1(xroar_filereq_interface->load_filename, xroar_tape_exts);
	xroar_insert_input_tape_file(filename);
}

void xroar_eject_input_tape(void) {
	tape_close_reading(xroar_tape_interface);
	DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_tape_input_filename, 0, NULL);
}

void xroar_insert_output_tape_file(const char *filename) {
	if (!filename) return;
	tape_open_writing(xroar_tape_interface, filename);
	DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_tape_output_filename, 0, filename);
}

void xroar_insert_output_tape(void) {
	char *filename = DELEGATE_CALL1(xroar_filereq_interface->save_filename, xroar_tape_exts);
	xroar_insert_output_tape_file(filename);
}

void xroar_eject_output_tape(void) {
	tape_close_writing(xroar_tape_interface);
	DELEGATE_CALL3(xroar_ui_interface->set_state, ui_tag_tape_output_filename, 0, NULL);
}

void xroar_soft_reset(void) {
	xroar_machine->reset(xroar_machine, RESET_SOFT);
	tape_reset(xroar_tape_interface);
}

void xroar_hard_reset(void) {
	xroar_machine->reset(xroar_machine, RESET_HARD);
	tape_reset(xroar_tape_interface);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Helper functions used by configuration */

static void set_pal(void) {
	private_cfg.tv = TV_PAL;
}

static void set_ntsc(void) {
	private_cfg.tv = TV_NTSC;
}

/* Called when a "-machine" option is encountered.  If an existing machine
 * config was in progress, copies any machine-related options into it and
 * clears those options.  Starts a new config. */
static void set_machine(const char *name) {
#ifdef LOGGING
	if (name && 0 == strcmp(name, "help")) {
		struct slist *mcl = machine_config_list();
		while (mcl) {
			struct machine_config *mc = mcl->data;
			mcl = mcl->next;
			printf("\t%-10s %s\n", mc->name, mc->description);
		}
		exit(EXIT_SUCCESS);
	}
#endif

	if (xroar_machine_config) {
		if (private_cfg.machine_arch != ANY_AUTO) {
			xroar_machine_config->architecture = private_cfg.machine_arch;
			private_cfg.machine_arch = ANY_AUTO;
		}
		if (private_cfg.machine_keymap != ANY_AUTO) {
			xroar_machine_config->keymap = private_cfg.machine_keymap;
			private_cfg.machine_keymap = ANY_AUTO;
		}
		xroar_machine_config->cpu = private_cfg.machine_cpu;
		if (private_cfg.machine_cpu == CPU_HD6309) {
			LOG_WARN("Hitachi HD6309 support is UNVERIFIED!\n");
		}
		if (private_cfg.machine_desc) {
			xroar_machine_config->description = private_cfg.machine_desc;
			private_cfg.machine_desc = NULL;
		}
#ifdef LOGGING
		if (private_cfg.machine_palette && 0 == strcmp(private_cfg.machine_palette, "help")) {
			int count = vdg_palette_count();
			int i;
			for (i = 0; i < count; i++) {
				struct vdg_palette *vp = vdg_palette_index(i);
				printf("\t%-10s %s\n", vp->name, vp->description);
			}
			exit(EXIT_SUCCESS);
		}
#endif
		if (private_cfg.machine_palette) {
			xroar_machine_config->vdg_palette = private_cfg.machine_palette;
			private_cfg.machine_palette = NULL;
		}
		if (private_cfg.tv != ANY_AUTO) {
			xroar_machine_config->tv_standard = private_cfg.tv;
			private_cfg.tv = ANY_AUTO;
		}
		if (private_cfg.vdg_type != -1) {
			xroar_machine_config->vdg_type = private_cfg.vdg_type;
			private_cfg.vdg_type = -1;
		}
		if (private_cfg.ram > 0) {
			xroar_machine_config->ram = private_cfg.ram;
			private_cfg.ram = 0;
		}
		if (private_cfg.nobas != -1)
			xroar_machine_config->nobas = private_cfg.nobas;
		if (private_cfg.noextbas != -1)
			xroar_machine_config->noextbas = private_cfg.noextbas;
		if (private_cfg.noaltbas != -1)
			xroar_machine_config->noaltbas = private_cfg.noaltbas;
		private_cfg.nobas = private_cfg.noextbas = private_cfg.noaltbas = -1;
		if (private_cfg.bas) {
			if (xroar_machine_config->bas_rom) {
				free(xroar_machine_config->bas_rom);
			}
			xroar_machine_config->bas_rom = private_cfg.bas;
			xroar_machine_config->nobas = 0;
			private_cfg.bas = NULL;
		}
		if (private_cfg.extbas) {
			if (xroar_machine_config->extbas_rom) {
				free(xroar_machine_config->extbas_rom);
			}
			xroar_machine_config->extbas_rom = private_cfg.extbas;
			xroar_machine_config->noextbas = 0;
			private_cfg.extbas = NULL;
		}
		if (private_cfg.altbas) {
			if (xroar_machine_config->altbas_rom) {
				free(xroar_machine_config->altbas_rom);
			}
			xroar_machine_config->altbas_rom = private_cfg.altbas;
			xroar_machine_config->noaltbas = 0;
			private_cfg.altbas = NULL;
		}
		if (private_cfg.ext_charset) {
			if (xroar_machine_config->ext_charset_rom) {
				free(xroar_machine_config->ext_charset_rom);
			}
			xroar_machine_config->ext_charset_rom = private_cfg.ext_charset;
			private_cfg.ext_charset = NULL;
		}
		if (private_cfg.machine_cart) {
			if (xroar_machine_config->default_cart) {
				free(xroar_machine_config->default_cart);
			}
			xroar_machine_config->default_cart = private_cfg.machine_cart;
			private_cfg.machine_cart = NULL;
		}
		if (private_cfg.nodos != -1) {
			xroar_machine_config->nodos = private_cfg.nodos;
			private_cfg.nodos = -1;
		}
		machine_config_complete(xroar_machine_config);
	}
	if (name) {
		xroar_machine_config = machine_config_by_name(name);
		if (!xroar_machine_config) {
			xroar_machine_config = machine_config_new();
			xroar_machine_config->name = xstrdup(name);
		}
	}
}

/* Called when a "-cart" option is encountered.  If an existing cart config was
* in progress, copies any cart-related options into it and clears those
* options.  Starts a new config.  */
static void set_cart(const char *name) {
#ifdef LOGGING
	if (name && 0 == strcmp(name, "help")) {
		struct slist *ccl = cart_config_list();
		while (ccl) {
			struct cart_config *cc = ccl->data;
			ccl = ccl->next;
			printf("\t%-10s %s\n", cc->name, cc->description);
		}
		exit(EXIT_SUCCESS);
	}
#endif
	// Apply any unassigned config to either the current cart config or the
	// current machine's default cart config.
	struct cart_config *cc = NULL;
	if (selected_cart_config) {
		cc = selected_cart_config;
	} else if (xroar_machine_config) {
		cc = cart_config_by_name(xroar_machine_config->default_cart);
	}
	if (cc) {
		if (private_cfg.cart_desc) {
			cc->description = private_cfg.cart_desc;
			private_cfg.cart_desc = NULL;
		}
		if (private_cfg.cart_type) {
			cc->type = private_cfg.cart_type;
			private_cfg.cart_type = NULL;
		}
		if (private_cfg.cart_rom) {
			if (cc->rom) {
				free(cc->rom);
			}
			cc->rom = private_cfg.cart_rom;
			private_cfg.cart_rom = NULL;
		}
		if (private_cfg.cart_rom2) {
			if (cc->rom2) {
				free(cc->rom2);
			}
			cc->rom2 = private_cfg.cart_rom2;
			private_cfg.cart_rom2 = NULL;
		}
		if (private_cfg.cart_becker != ANY_AUTO) {
			cc->becker_port = private_cfg.cart_becker;
			private_cfg.cart_becker = ANY_AUTO;
		}
		if (private_cfg.cart_autorun != ANY_AUTO) {
			cc->autorun = private_cfg.cart_autorun;
			private_cfg.cart_autorun = ANY_AUTO;
		}
		cart_config_complete(cc);
	}
	if (name) {
		selected_cart_config = cart_config_by_name(name);
		if (!selected_cart_config) {
			selected_cart_config = cart_config_new();
			selected_cart_config->name = xstrdup(name);
		}
	}
}

static void set_cart_type(const char *name) {
	if (name && 0 == strcmp(name, "help")) {
		cart_type_help();
		exit(EXIT_SUCCESS);
	}
	if (private_cfg.cart_type) {
		free(private_cfg.cart_type);
	}
	private_cfg.cart_type = xstrdup(name);
}

static void set_gain(double gain) {
	private_cfg.gain = gain;
	private_cfg.volume = -1;
}

static void cfg_mpi_slot(int slot) {
	mpi_set_initial(slot);
}

static void cfg_mpi_load_cart(const char *arg) {
	char *arg_copy = xstrdup(arg);
	char *carg = arg_copy;
	char *tmp = strsep(&carg, "=");
	static int slot = 0;
	if (carg) {
		slot = strtol(tmp, NULL, 0);
		tmp = carg;
	}
	mpi_set_cart(slot, tmp);
	slot++;
	free(arg_copy);
}

/* Called when a "-joystick" option is encountered. */
static void set_joystick(const char *name) {
	// Apply any config to the current joystick config.
	if (cur_joy_config) {
		if (private_cfg.joy_desc) {
			cur_joy_config->description = private_cfg.joy_desc;
			private_cfg.joy_desc = NULL;
		}
		for (unsigned i = 0; i < JOYSTICK_NUM_AXES; i++) {
			if (private_cfg.joy_axis[i]) {
				if (cur_joy_config->axis_specs[i])
					free(cur_joy_config->axis_specs[i]);
				cur_joy_config->axis_specs[i] = private_cfg.joy_axis[i];
				private_cfg.joy_axis[i] = NULL;
			}
		}
		for (unsigned i = 0; i < JOYSTICK_NUM_BUTTONS; i++) {
			if (private_cfg.joy_button[i]) {
				if (cur_joy_config->button_specs[i])
					free(cur_joy_config->button_specs[i]);
				cur_joy_config->button_specs[i] = private_cfg.joy_button[i];
				private_cfg.joy_button[i] = NULL;
			}
		}
	}
#ifdef LOGGING
	if (name && 0 == strcmp(name, "help")) {
		struct slist *jcl = joystick_config_list();
		while (jcl) {
			struct joystick_config *jc = jcl->data;
			jcl = jcl->next;
			printf("\t%-10s %s\n", jc->name, jc->description);
		}
		exit(EXIT_SUCCESS);
	}
#endif
	if (name) {
		cur_joy_config = joystick_config_by_name(name);
		if (!cur_joy_config) {
			cur_joy_config = joystick_config_new();
			cur_joy_config->name = xstrdup(name);
		}
	}
}

static void set_joystick_axis(const char *spec) {
	char *spec_copy = xstrdup(spec);
	char *cspec = spec_copy;
	unsigned axis = 0;
	char *tmp = strsep(&cspec, "=");
	if (cspec) {
		if (toupper(*tmp) == 'X') {
			axis = 0;
		} else if (toupper(*tmp) == 'Y') {
			axis = 1;
		} else {
			axis = strtol(tmp, NULL, 0);
		}
		if (axis > JOYSTICK_NUM_AXES) {
			LOG_WARN("Invalid axis number '%u'\n", axis);
			axis = 0;
		}
		tmp = cspec;
	}
	private_cfg.joy_axis[axis] = xstrdup(tmp);
	free(spec_copy);
}

static void set_joystick_button(const char *spec) {
	char *spec_copy = xstrdup(spec);
	char *cspec = spec_copy;
	unsigned button = 0;
	char *tmp = strsep(&cspec, "=");
	if (cspec) {
		button = strtol(tmp, NULL, 0);
		if (button > JOYSTICK_NUM_AXES) {
			LOG_WARN("Invalid button number '%u'\n", button);
			button = 0;
		}
		tmp = cspec;
	}
	private_cfg.joy_button[button] = xstrdup(tmp);
	free(spec_copy);
}

/* Enumeration lists used by configuration directives */

static struct xconfig_enum ao_format_list[] = {
	{ XC_ENUM_INT("u8", SOUND_FMT_U8, "8-bit unsigned") },
	{ XC_ENUM_INT("s8", SOUND_FMT_S8, "8-bit signed") },
	{ XC_ENUM_INT("s16", SOUND_FMT_S16_HE, "16-bit signed host-endian") },
	{ XC_ENUM_INT("s16se", SOUND_FMT_S16_SE, "16-bit signed swapped-endian") },
	{ XC_ENUM_INT("s16be", SOUND_FMT_S16_BE, "16-bit signed big-endian") },
	{ XC_ENUM_INT("s16le", SOUND_FMT_S16_LE, "16-bit signed little-endian") },
	{ XC_ENUM_INT("float", SOUND_FMT_FLOAT, "Floating point") },
	{ XC_ENUM_END() }
};

/* Configuration directives */

static union {
	_Bool v_bool;
	int v_int;
} dummy_value;

static struct xconfig_option const xroar_options[] = {
	/* Machines: */
	{ XC_SET_STRING("default-machine", &private_cfg.default_machine) },
	{ XC_CALL_STRING("machine", &set_machine) },
	{ XC_SET_STRING("machine-desc", &private_cfg.machine_desc) },
	{ XC_SET_ENUM("machine-arch", &private_cfg.machine_arch, machine_arch_list) },
	{ XC_SET_ENUM("machine-keyboard", &private_cfg.machine_keymap, machine_keyboard_list) },
	{ XC_SET_ENUM("machine-cpu", &private_cfg.machine_cpu, machine_cpu_list) },
	{ XC_SET_STRING_F("bas", &private_cfg.bas) },
	{ XC_SET_STRING_F("extbas", &private_cfg.extbas) },
	{ XC_SET_STRING_F("altbas", &private_cfg.altbas) },
	{ XC_SET_INT1("nobas", &private_cfg.nobas) },
	{ XC_SET_INT1("noextbas", &private_cfg.noextbas) },
	{ XC_SET_INT1("noaltbas", &private_cfg.noaltbas) },
	{ XC_SET_STRING_F("ext-charset", &private_cfg.ext_charset) },
	{ XC_SET_ENUM("tv-type", &private_cfg.tv, machine_tv_type_list) },
	{ XC_SET_ENUM("vdg-type", &private_cfg.vdg_type, machine_vdg_type_list) },
	{ XC_SET_INT("ram", &private_cfg.ram) },
	{ XC_SET_STRING("machine-cart", &private_cfg.machine_cart) },
	{ XC_SET_INT1("nodos", &private_cfg.nodos) },
	/* Shorthand: */
	{ XC_CALL_NULL("pal", &set_pal) },
	{ XC_CALL_NULL("ntsc", &set_ntsc) },
	/* Deliberately undocumented: */
	{ XC_SET_STRING("machine-palette", &private_cfg.machine_palette) },

	/* Cartridges: */
	{ XC_CALL_STRING("cart", &set_cart) },
	{ XC_SET_STRING("cart-desc", &private_cfg.cart_desc) },
	{ XC_CALL_STRING("cart-type", &set_cart_type) },
	{ XC_SET_STRING_F("cart-rom", &private_cfg.cart_rom) },
	{ XC_SET_STRING_F("cart-rom2", &private_cfg.cart_rom2) },
	{ XC_SET_INT1("cart-autorun", &private_cfg.cart_autorun) },
	{ XC_SET_INT1("cart-becker", &private_cfg.cart_becker) },
	/* Backwards compatibility: */
	{ XC_SET_STRING("dostype", &private_cfg.cart_type), .deprecated = 1 },
	{ XC_SET_STRING_F("dos", &private_cfg.dos_option), .deprecated = 1 },

	/* Multi-Pak Interface: */
	{ XC_CALL_INT("mpi-slot", &cfg_mpi_slot) },
	{ XC_CALL_STRING("mpi-load-cart", &cfg_mpi_load_cart) },

	/* Becker port: */
	{ XC_SET_BOOL("becker", &xroar_cfg.becker) },
	{ XC_SET_STRING("becker-ip", &xroar_cfg.becker_ip) },
	{ XC_SET_STRING("becker-port", &xroar_cfg.becker_port) },
	/* Backwards-compatibility: */
	{ XC_SET_STRING("dw4-ip", &xroar_cfg.becker_ip), .deprecated = 1 },
	{ XC_SET_STRING("dw4-port", &xroar_cfg.becker_port), .deprecated = 1 },

	/* Files: */
	{ XC_SET_STRING_LIST_F("load", &private_cfg.load_list) },
	{ XC_SET_STRING_F("run", &private_cfg.run) },
	/* Backwards-compatibility: */
	{ XC_SET_STRING_LIST_F("cartna", &private_cfg.load_list), .deprecated = 1 },
	{ XC_SET_STRING_LIST_F("snap", &private_cfg.load_list), .deprecated = 1 },

	/* Cassettes: */
	{ XC_SET_STRING_F("tape-write", &private_cfg.tape_write) },
	{ XC_SET_DOUBLE("tape-pan", &xroar_cfg.tape_pan) },
	{ XC_SET_INT1("tape-fast", &private_cfg.tape_fast) },
	{ XC_SET_INT1("tape-pad-auto", &private_cfg.tape_pad_auto) },
	{ XC_SET_INT1("tape-rewrite", &private_cfg.tape_rewrite) },
	{ XC_SET_INT("tape-ao-rate", &private_cfg.tape_ao_rate) },
	/* Backwards-compatibility: */
	{ XC_SET_INT1("tapehack", &private_cfg.tape_rewrite), .deprecated = 1 },
	{ XC_SET_INT1("tape-pad", &dummy_value.v_int), .deprecated = 1 },

	/* Disks: */
	{ XC_SET_BOOL("disk-write-back", &xroar_cfg.disk_write_back) },
	{ XC_SET_BOOL("disk-auto-os9", &xroar_cfg.disk_auto_os9) },
	{ XC_SET_BOOL("disk-auto-sd", &xroar_cfg.disk_auto_sd) },
	/* Backwards-compatibility: */
	{ XC_SET_BOOL("disk-jvc-hack", &dummy_value.v_bool), .deprecated = 1 },

	/* Firmware ROM images: */
	{ XC_SET_STRING_F("rompath", &xroar_rom_path) },
	{ XC_CALL_ASSIGN_F("romlist", &romlist_assign) },
	{ XC_CALL_NULL("romlist-print", &romlist_print) },
	{ XC_CALL_ASSIGN("crclist", &crclist_assign) },
	{ XC_CALL_NULL("crclist-print", &crclist_print) },
	{ XC_SET_BOOL("force-crc-match", &xroar_cfg.force_crc_match) },

	/* User interface: */
	{ XC_SET_STRING("ui", &private_cfg.ui) },
	/* Deliberately undocumented: */
	{ XC_SET_STRING("filereq", &private_cfg.filereq) },

	/* Video: */
	{ XC_SET_STRING("vo", &xroar_ui_cfg.vo) },
	{ XC_SET_BOOL("fs", &xroar_ui_cfg.vo_cfg.fullscreen) },
	{ XC_SET_INT("fskip", &xroar_cfg.frameskip) },
	{ XC_SET_ENUM("ccr", &ccr, ui_ccr_list) },
	{ XC_SET_ENUM("gl-filter", &xroar_ui_cfg.vo_cfg.gl_filter, ui_gl_filter_list) },
	{ XC_SET_STRING("geometry", &xroar_ui_cfg.vo_cfg.geometry) },
	{ XC_SET_STRING("g", &xroar_ui_cfg.vo_cfg.geometry) },
	{ XC_SET_BOOL("invert-text", &xroar_cfg.vdg_inverted_text) },

	/* Audio: */
	{ XC_SET_STRING("ao", &private_cfg.ao) },
	{ XC_SET_STRING("ao-device", &xroar_cfg.ao_device) },
	{ XC_SET_ENUM("ao-format", &xroar_cfg.ao_format, ao_format_list) },
	{ XC_SET_INT("ao-rate", &xroar_cfg.ao_rate) },
	{ XC_SET_INT("ao-channels", &xroar_cfg.ao_channels) },
	{ XC_SET_INT("ao-fragments", &xroar_cfg.ao_fragments) },
	{ XC_SET_INT("ao-fragment-ms", &xroar_cfg.ao_fragment_ms) },
	{ XC_SET_INT("ao-fragment-frames", &xroar_cfg.ao_fragment_nframes) },
	{ XC_SET_INT("ao-buffer-ms", &xroar_cfg.ao_buffer_ms) },
	{ XC_SET_INT("ao-buffer-frames", &xroar_cfg.ao_buffer_nframes) },
	{ XC_CALL_DOUBLE("ao-gain", &set_gain) },
	{ XC_SET_INT("volume", &private_cfg.volume) },
	{ XC_SET_BOOL("fast-sound", &xroar_cfg.fast_sound) },
	/* Backwards-compatibility: */
	{ XC_SET_INT("ao-buffer-samples", &xroar_cfg.ao_buffer_nframes), .deprecated = 1 },

	/* Keyboard: */
	{ XC_SET_STRING("keymap", &xroar_ui_cfg.keymap) },
	{ XC_SET_BOOL("kbd-translate", &xroar_cfg.kbd_translate) },
	{ XC_SET_STRING_LIST("type", &private_cfg.type_list) },

	/* Joysticks: */
	{ XC_CALL_STRING("joy", &set_joystick) },
	{ XC_SET_STRING("joy-desc", &private_cfg.joy_desc) },
	{ XC_CALL_STRING("joy-axis", &set_joystick_axis) },
	{ XC_CALL_STRING("joy-button", &set_joystick_button) },
	{ XC_SET_STRING("joy-right", &private_cfg.joy_right) },
	{ XC_SET_STRING("joy-left", &private_cfg.joy_left) },
	{ XC_SET_STRING("joy-virtual", &private_cfg.joy_virtual) },

	/* Printing: */
	{ XC_SET_STRING_F("lp-file", &private_cfg.lp_file) },
	{ XC_SET_STRING("lp-pipe", &private_cfg.lp_pipe) },

	/* Debugging: */
#ifdef WANT_GDB_TARGET
	{ XC_SET_BOOL("gdb", &xroar_cfg.gdb) },
	{ XC_SET_STRING("gdb-ip", &xroar_cfg.gdb_ip) },
	{ XC_SET_STRING("gdb-port", &xroar_cfg.gdb_port) },
#endif
#ifdef TRACE
	{ XC_SET_INT1("trace", &xroar_cfg.trace_enabled) },
#endif
	{ XC_SET_INT("debug-ui", &xroar_cfg.debug_ui) },
	{ XC_SET_INT("debug-file", &xroar_cfg.debug_file) },
	{ XC_SET_INT("debug-fdc", &xroar_cfg.debug_fdc) },
#ifdef WANT_GDB_TARGET
	{ XC_SET_INT("debug-gdb", &xroar_cfg.debug_gdb) },
#endif
	{ XC_SET_STRING("timeout", &private_cfg.timeout) },
	{ XC_SET_STRING("timeout-motoroff", &xroar_cfg.timeout_motoroff) },
	{ XC_SET_STRING("snap-motoroff", &xroar_cfg.snap_motoroff) },

	/* Other options: */
	{ XC_SET_BOOL("config-print", &private_cfg.config_print) },
	{ XC_SET_BOOL("config-print-all", &private_cfg.config_print_all) },
	{ XC_SET_INT0("quiet", &log_level) },
	{ XC_SET_INT0("q", &log_level) },
	{ XC_SET_INT("verbose", &log_level) },
	{ XC_SET_INT("v", &log_level) },
	{ XC_CALL_NULL("help", &helptext) },
	{ XC_CALL_NULL("h", &helptext) },
	{ XC_CALL_NULL("version", &versiontext) },
	{ XC_CALL_NULL("V", &versiontext) },
	{ XC_OPT_END() }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Help texts */

static void helptext(void) {
#ifdef LOGGING
	puts(
"Usage: xroar [STARTUP-OPTION]... [OPTION]...\n"
"XRoar is a Dragon emulator.  Due to hardware similarities, XRoar also\n"
"emulates the Tandy Colour Computer (CoCo) models 1 & 2.\n"

"\n Startup options:\n"
#ifdef WINDOWS32
"  -C              allocate a console window\n"
#endif
"  -c CONFFILE     specify a configuration file\n"

"\n Machines:\n"
"  -default-machine NAME   default machine on startup\n"
"  -machine NAME           configure named machine (-machine help for list)\n"
"    -machine-desc TEXT      machine description\n"
"    -machine-arch ARCH      machine architecture (-machine-arch help for list)\n"
"    -machine-keyboard LAYOUT\n"
"                            keyboard layout (-machine-keyboard help for list)\n"
"    -machine-cpu CPU        machine CPU (-machine-cpu help for list)\n"
"    -bas NAME               BASIC ROM to use (CoCo only)\n"
"    -extbas NAME            Extended BASIC ROM to use\n"
"    -altbas NAME            64K mode Extended BASIC ROM (Dragon 64)\n"
"    -nobas                  disable BASIC\n"
"    -noextbas               disable Extended BASIC\n"
"    -noaltbas               disable 64K mode Extended BASIC\n"
"    -ext-charset NAME       external character generator ROM to use\n"
"    -tv-type TYPE           TV type (-tv-type help for list)\n"
"    -vdg-type TYPE          VDG type (6847 or 6847t1)\n"
"    -ram KBYTES             amount of RAM in K\n"
"    -machine-cart NAME      default cartridge for selected machine\n"
"    -nodos                  don't automatically pick a DOS cartridge\n"

"\n Cartridges:\n"
"  -cart NAME            configure named cartridge (-cart help for list)\n"
"    -cart-desc TEXT       cartridge description\n"
"    -cart-type TYPE       cartridge base type (-cart-type help for list)\n"
"    -cart-rom NAME        ROM image to load ($C000-)\n"
"    -cart-rom2 NAME       second ROM image to load ($E000-)\n"
"    -cart-autorun         autorun cartridge\n"
"    -cart-becker          enable becker port where supported\n"

"\n Multi-Pak Interface:\n"
"  -mpi-slot SLOT               initially select slot (0-3)\n"
"  -mpi-load-cart [SLOT=]NAME   insert cartridge into next or numbered slot\n"

"\n Becker port:\n"
"  -becker               prefer becker-enabled DOS (when picked automatically)\n"
"  -becker-ip ADDRESS    address or hostname of DriveWire server [" BECKER_IP_DEFAULT "]\n"
"  -becker-port PORT     port of DriveWire server [" BECKER_PORT_DEFAULT "]\n"

"\n Files:\n"
"  -load FILE            load or attach FILE\n"
"  -run FILE             load or attach FILE and attempt autorun\n"

"\n Cassettes:\n"
"  -tape-write FILE    open FILE for tape writing\n"
"  -tape-pan PANNING   pan stereo input (0.0 = full left, 1.0 = full right) [0.5]\n"
"  -no-tape-fast       disable fast tape loading\n"
"  -no-tape-pad-auto   disable CAS file short leader workaround\n"
"  -tape-rewrite       enable tape rewriting\n"
"  -tape-ao-rate HZ    set tape writing frame rate\n"

"\n Disks:\n"
"  -disk-write-back      default to enabling write-back for disk images\n"
"  -no-disk-auto-os9     don't try to detect headerless OS-9 JVC disk images\n"
"  -no-disk-auto-sd      don't assume single density for 10 sec/track disks\n"

"\n Firmware ROM images:\n"
"  -rompath PATH         ROM search path (colon-separated list)\n"
"  -romlist NAME=LIST    define a ROM list\n"
"  -romlist-print        print defined ROM lists\n"
"  -crclist NAME=LIST    define a ROM CRC list\n"
"  -crclist-print        print defined ROM CRC lists\n"
"  -force-crc-match      force per-architecture CRC matches\n"

"\n User interface:\n"
"  -ui MODULE            user-interface module (-ui help for list)\n"

"\n Video:\n"
"  -vo MODULE            video module (-vo help for list)\n"
"  -fs                   start emulator full-screen if possible\n"
"  -fskip FRAMES         frameskip (default: 0)\n"
"  -ccr RENDERER         cross-colour renderer (-ccr help for list)\n"
"  -gl-filter FILTER     OpenGL texture filter (-gl-filter help for list)\n"
"  -geometry WxH+X+Y     initial emulator geometry\n"
"  -invert-text          start with text mode inverted\n"

"\n Audio:\n"
"  -ao MODULE            audio module (-ao help for list)\n"
"  -ao-device STRING     device to use for audio module\n"
"  -ao-format FMT        set audio sample format (-ao-format help for list)\n"
"  -ao-rate HZ           set audio frame rate (if supported by module)\n"
"  -ao-channels N        set number of audio channels, 1 or 2\n"
"  -ao-fragments N       set number of audio fragments\n"
"  -ao-fragment-ms MS    set audio fragment size in ms (if supported)\n"
"  -ao-fragment-frames N set audio fragment size in samples (if supported)\n"
"  -ao-buffer-ms MS      set total audio buffer size in ms (if supported)\n"
"  -ao-buffer-frames N   set total audio buffer size in samples (if supported)\n"
"  -ao-gain DB           audio gain in dB relative to 0 dBFS [-3.0]\n"
"  -volume VOLUME        older way to specify audio volume, linear (0-100)\n"
"  -fast-sound           faster but less accurate sound\n"

"\n Keyboard:\n"
"  -keymap CODE          host keyboard type (-keymap help for list)\n"
"  -kbd-translate        enable keyboard translation\n"
"  -type STRING          intercept ROM calls to type STRING into BASIC\n"

"\n Joysticks:\n"
"  -joy NAME             configure named joystick (-joy help for list)\n"
"    -joy-desc TEXT        joystick description\n"
"    -joy-axis AXIS=SPEC   configure joystick axis\n"
"    -joy-button BTN=SPEC  configure joystick button\n"
"  -joy-right NAME       map right joystick\n"
"  -joy-left NAME        map left joystick\n"
"  -joy-virtual NAME     specify the 'virtual' joystick to cycle [kjoy0]\n"

"\n Printing:\n"
"  -lp-file FILE         append Dragon printer output to FILE\n"
#ifdef HAVE_POPEN
"  -lp-pipe COMMAND      pipe Dragon printer output to COMMAND\n"
#endif

"\n Debugging:\n"
#ifdef WANT_GDB_TARGET
"  -gdb                  enable GDB target\n"
"  -gdb-ip ADDRESS       address of interface for GDB target [" GDB_IP_DEFAULT "]\n"
"  -gdb-port PORT        port for GDB target to listen on [" GDB_PORT_DEFAULT "]\n"
#endif
#ifdef TRACE
"  -trace                start with trace mode on\n"
#endif
"  -debug-ui FLAGS       UI debugging (see manual, or -1 for all)\n"
"  -debug-file FLAGS     file debugging (see manual, or -1 for all)\n"
"  -debug-fdc FLAGS      FDC debugging (see manual, or -1 for all)\n"
"  -debug-gdb FLAGS      GDB target debugging (see manual, or -1 for all)\n"
"  -v, --verbose LEVEL   general debug verbosity (0-3) [1]\n"
"  -q, --quiet           equivalent to --verbose 0\n"
"  -timeout S            run for S seconds then quit\n"
"  -timeout-motoroff S   quit S seconds after tape motor switches off\n"
"  -snap-motoroff FILE   write a snapshot each time tape motor switches off\n"

"\n Other options:\n"
"  -config-print       print configuration to standard out\n"
"  -config-print-all   print configuration to standard out, including defaults\n"
"  -h, --help          display this help and exit\n"
"  -V, --version       output version information and exit\n"

"\nWhen configuring a Multi-Pak Interface (MPI), only the last configured DOS\n"
"cartridge will end up connected to the virtual drives.\n"

"\nJoystick SPECs are of the form [MODULE:][ARG[,ARG]...], from:\n"

"\nMODULE          Axis ARGs                       Button ARGs\n"
"physical        joystick-index,[-]axis-index    joystick-index,button-index\n"
"keyboard        key-name0,key-name1             key-name\n"
"mouse           screen-offset0,screen-offset1   button-number\n"

"\nFor physical joysticks a '-' before the axis index inverts the axis.  AXIS 0 is\n"
"the X-axis, and AXIS 1 the Y-axis.  BTN 0 is the only one used so far, but in\n"
"the future BTN 1 will be the second button on certain CoCo joysticks."

	);
#endif
	exit(EXIT_SUCCESS);
}

static void versiontext(void) {
#ifdef LOGGING
	printf("%s", PACKAGE_TEXT);
	puts(
"\nCopyright (C) " PACKAGE_YEAR " Ciaran Anscomb\n"
"License: GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl-3.0.html>.\n"
"This is free software: you are free to change and redistribute it.\n"
"There is NO WARRANTY, to the extent permitted by law."
	);
#endif
	exit(EXIT_SUCCESS);
}

/* Dump all known config to stdout */

/*
 * The plan is to have proper introspection of the configuration, allowing
 * dynamic updates from a console or remotely.  Dumping of the current config
 * would then become pretty easy.
 *
 * Until then, this is a pretty awful stopgap measure.  It's liable to break if
 * a default changes or new options are added.  Be careful!
 */

static void config_print_all(FILE *f, _Bool all) {
	fputs("# Machines\n\n", f);
	xroar_cfg_print_string(f, all, "default-machine", private_cfg.default_machine, NULL);
	fputs("\n", f);
	machine_config_print_all(f, all);

	fputs("# Cartridges\n\n", f);
	cart_config_print_all(f, all);
	fputs("# Becker port\n", f);
	xroar_cfg_print_bool(f, all, "becker", xroar_cfg.becker, 0);
	xroar_cfg_print_string(f, all, "becker-ip", xroar_cfg.becker_ip, BECKER_IP_DEFAULT);
	xroar_cfg_print_string(f, all, "becker-port", xroar_cfg.becker_port, BECKER_PORT_DEFAULT);
	fputs("\n", f);

	fputs("# Files\n", f);
	xroar_cfg_print_string_list(f, all, "load", private_cfg.load_list);
	xroar_cfg_print_string(f, all, "run", private_cfg.run, NULL);
	fputs("\n", f);

	fputs("# Cassettes\n", f);
	xroar_cfg_print_string(f, all, "tape-write", private_cfg.tape_write, NULL);
	xroar_cfg_print_double(f, all, "tape-pan", xroar_cfg.tape_pan, 0.5);

	xroar_cfg_print_bool(f, all, "tape-fast", private_cfg.tape_fast, 1);
	xroar_cfg_print_bool(f, all, "tape-pad-auto", private_cfg.tape_pad_auto, 1);
	xroar_cfg_print_bool(f, all, "tape-rewrite", private_cfg.tape_rewrite, 0);
	xroar_cfg_print_int_nz(f, all, "tape-ao-rate", private_cfg.tape_ao_rate);
	fputs("\n", f);

	fputs("# Disks\n", f);
	xroar_cfg_print_bool(f, all, "disk-write-back", xroar_cfg.disk_write_back, 0);
	xroar_cfg_print_bool(f, all, "disk-auto-os9", xroar_cfg.disk_auto_os9, 1);
	xroar_cfg_print_bool(f, all, "disk-auto-sd", xroar_cfg.disk_auto_sd, 1);
	fputs("\n", f);

	fputs("# Firmware ROM images\n", f);
	xroar_cfg_print_string(f, all, "rompath", xroar_rom_path, NULL);
	romlist_print_all(f);
	crclist_print_all(f);
	xroar_cfg_print_bool(f, all, "force-crc-match", xroar_cfg.force_crc_match, 0);
	fputs("\n", f);

	fputs("# User interface\n", f);
	xroar_cfg_print_string(f, all, "ui", private_cfg.ui, NULL);
	xroar_cfg_print_string(f, all, "filereq", private_cfg.filereq, NULL);
	fputs("\n", f);

	fputs("# Video\n", f);
	xroar_cfg_print_string(f, all, "vo", xroar_ui_cfg.vo, NULL);
	xroar_cfg_print_bool(f, all, "fs", xroar_ui_cfg.vo_cfg.fullscreen, 0);
	xroar_cfg_print_int_nz(f, all, "fskip", xroar_cfg.frameskip);
	xroar_cfg_print_enum(f, all, "ccr", ccr, UI_CCR_5BIT, ui_ccr_list);
	xroar_cfg_print_enum(f, all, "gl-filter", xroar_ui_cfg.vo_cfg.gl_filter, ANY_AUTO, ui_gl_filter_list);
	xroar_cfg_print_string(f, all, "geometry", xroar_ui_cfg.vo_cfg.geometry, NULL);
	xroar_cfg_print_bool(f, all, "invert-text", xroar_cfg.vdg_inverted_text, 0);
	fputs("\n", f);

	fputs("# Audio\n", f);
	xroar_cfg_print_string(f, all, "ao", private_cfg.ao, NULL);
	xroar_cfg_print_string(f, all, "ao-device", xroar_cfg.ao_device, NULL);
	xroar_cfg_print_enum(f, all, "ao-format", xroar_cfg.ao_format, SOUND_FMT_NULL, ao_format_list);
	xroar_cfg_print_int_nz(f, all, "ao-rate", xroar_cfg.ao_rate);
	xroar_cfg_print_int_nz(f, all, "ao-channels", xroar_cfg.ao_channels);
	xroar_cfg_print_int_nz(f, all, "ao-fragments", xroar_cfg.ao_fragments);
	xroar_cfg_print_int_nz(f, all, "ao-fragment-ms", xroar_cfg.ao_fragment_ms);
	xroar_cfg_print_int_nz(f, all, "ao-fragment-frames", xroar_cfg.ao_fragment_nframes);
	xroar_cfg_print_int_nz(f, all, "ao-buffer-ms", xroar_cfg.ao_buffer_ms);
	xroar_cfg_print_int_nz(f, all, "ao-buffer-frames", xroar_cfg.ao_buffer_nframes);
	xroar_cfg_print_double(f, all, "ao-gain", private_cfg.gain, -3.0);
	xroar_cfg_print_int(f, all, "volume", private_cfg.volume, -1);
	xroar_cfg_print_bool(f, all, "fast-sound", xroar_cfg.fast_sound, 0);
	fputs("\n", f);

	fputs("# Keyboard\n", f);
	xroar_cfg_print_string(f, all, "keymap", xroar_ui_cfg.keymap, "uk");
	xroar_cfg_print_bool(f, all, "kbd-translate", xroar_cfg.kbd_translate, 0);
	for (struct slist *l = private_cfg.type_list; l; l = l->next) {
		sds s = sdsx_quote(l->data);
		fprintf(f, "type %s\n", s);
		sdsfree(s);
	}
	fputs("\n", f);

	fputs("# Joysticks\n", f);
	joystick_config_print_all(f, all);
	xroar_cfg_print_string(f, all, "joy-right", private_cfg.joy_right, "joy0");
	xroar_cfg_print_string(f, all, "joy-left", private_cfg.joy_left, "joy1");
	xroar_cfg_print_string(f, all, "joy-virtual", private_cfg.joy_virtual, "kjoy0");
	fputs("\n", f);

	fputs("# Printing\n", f);
	xroar_cfg_print_string(f, all, "lp-file", private_cfg.lp_file, NULL);
	xroar_cfg_print_string(f, all, "lp-pipe", private_cfg.lp_pipe, NULL);
	fputs("\n", f);

	fputs("# Debugging\n", f);
#ifdef WANT_GDB_TARGET
	xroar_cfg_print_bool(f, all, "gdb", xroar_cfg.gdb, 0);
	xroar_cfg_print_string(f, all, "gdb-ip", xroar_cfg.gdb_ip, GDB_IP_DEFAULT);
	xroar_cfg_print_string(f, all, "gdb-port", xroar_cfg.gdb_port, GDB_PORT_DEFAULT);
#endif
#ifdef TRACE
	xroar_cfg_print_bool(f, all, "trace", xroar_cfg.trace_enabled, 0);
#endif
	xroar_cfg_print_flags(f, all, "debug-ui", xroar_cfg.debug_ui);
	xroar_cfg_print_flags(f, all, "debug-file", xroar_cfg.debug_file);
	xroar_cfg_print_flags(f, all, "debug-fdc", xroar_cfg.debug_fdc);
#ifdef WANT_GDB_TARGET
	xroar_cfg_print_flags(f, all, "debug-gdb", xroar_cfg.debug_gdb);
#endif
	xroar_cfg_print_string(f, all, "timeout", private_cfg.timeout, NULL);
	xroar_cfg_print_string(f, all, "timeout-motoroff", xroar_cfg.timeout_motoroff, NULL);
	xroar_cfg_print_string(f, all, "snap-motoroff", xroar_cfg.snap_motoroff, NULL);
	fputs("\n", f);
}

/* Helper functions for config printing */

static int cfg_print_indent_level = 0;

void xroar_cfg_print_inc_indent(void) {
	cfg_print_indent_level++;
}

void xroar_cfg_print_dec_indent(void) {
	assert(cfg_print_indent_level > 0);
	cfg_print_indent_level--;
}

void xroar_cfg_print_indent(FILE *f) {
	for (int i = 0; i < cfg_print_indent_level; i++)
		fprintf(f, "  ");
}

void xroar_cfg_print_bool(FILE *f, _Bool all, char const *opt, int value, int normal) {
	if (!all && value == normal)
		return;
	xroar_cfg_print_indent(f);
	if (value >= 0) {
		if (!value)
			fprintf(f, "no-");
		fprintf(f, "%s\n", opt);
		return;
	}
	fprintf(f, "# %s undefined\n", opt);
}

void xroar_cfg_print_int(FILE *f, _Bool all, char const *opt, int value, int normal) {
	if (!all && value == normal)
		return;
	xroar_cfg_print_indent(f);
	fprintf(f, "%s %d\n", opt, value);
}

void xroar_cfg_print_int_nz(FILE *f, _Bool all, char const *opt, int value) {
	if (!all && value == 0)
		return;
	xroar_cfg_print_indent(f);
	if (value != 0) {
		fprintf(f, "%s %d\n", opt, value);
		return;
	}
	fprintf(f, "# %s undefined\n", opt);
}

void xroar_cfg_print_double(FILE *f, _Bool all, char const *opt, double value, double normal) {
	if (!all && value == normal)
		return;
	xroar_cfg_print_indent(f);
	fprintf(f, "%s %.4f\n", opt, value);
}

void xroar_cfg_print_flags(FILE *f, _Bool all, char const *opt, unsigned value) {
	if (!all && value == 0)
		return;
	xroar_cfg_print_indent(f);
	fprintf(f, "%s 0x%x\n", opt, value);
}

void xroar_cfg_print_string(FILE *f, _Bool all, char const *opt, char const *value, char const *normal) {
	if (!all && !value)
		return;
	xroar_cfg_print_indent(f);
	if (value || normal) {
		char const *tmp = value ? value : normal;
		sds str = sdsx_quote_str(tmp);
		fprintf(f, "%s %s\n", opt, str);
		sdsfree(str);
		return;
	}
	fprintf(f, "# %s undefined\n", opt);
}

void xroar_cfg_print_enum(FILE *f, _Bool all, char const *opt, int value, int normal, struct xconfig_enum const *e) {
	if (!all && value == normal)
		return;
	xroar_cfg_print_indent(f);
	for (int i = 0; e[i].name; i++) {
		if (value == e[i].value) {
			fprintf(f, "%s %s\n", opt, e[i].name);
			return;
		}
	}
	fprintf(f, "# %s undefined\n", opt);
}

void xroar_cfg_print_string_list(FILE *f, _Bool all, char const *opt, struct slist *l) {
	if (!all  && !l)
		return;
	xroar_cfg_print_indent(f);
	if (l) {
		for (; l; l = l->next) {
			char const *s = l->data;
			fprintf(f, "%s %s\n", opt, s);
		}
		return;
	}
	fprintf(f, "# %s undefined\n", opt);
}
