/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2006  Ciaran Anscomb
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "types.h"
#include "cart.h"
#include "filereq.h"
#include "hexs19.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "pia.h"
#include "snapshot.h"
#include "tape.h"
#include "vdisk.h"
#include "video.h"
#include "wd2797.h"
#include "xroar.h"

static void getargs(int argc, char **argv);
static int init(void);
static void shutdown(void);
static void poll(void);

KeyboardModule keyboard_sdl_module = {
	NULL,
	"sdl",
	"SDL keyboard driver",
	getargs, init, shutdown,
	poll
};

struct keymap {
	const char *name;
	unsigned int *raw;
};

#include "keyboard_sdl_mappings.c"

static unsigned int control = 0, shift = 0;
static unsigned int emulate_joystick = 0;

static uint_least16_t sdl_to_keymap[768];

static unsigned int unicode_to_dragon[128] = {
	0,       0,       0,       0,       0,       0,       0,       0,
	8,       9,       10,      0,       12,      13,      0,       0,
	0,       0,       0,       0,       0,       0,       0,       0,
	0,       0,       0,       27,      0,       0,       0,       0,
	' ',     128+'1', 128+'2', 128+'3', 128+'4', 128+'5', 128+'6', 128+'7',
	128+'8', 128+'9', 128+':', 128+';', ',',     '-',     '.',     '/',
	'0',     '1',     '2',     '3',     '4',     '5',     '6',     '7',
	'8',     '9',     ':',     ';',     128+',', 128+'-', 128+'.', 128+'/',
	'@',     128+'a', 128+'b', 128+'c', 128+'d', 128+'e', 128+'f', 128+'g',
	128+'h', 128+'i', 128+'j', 128+'k', 128+'l', 128+'m', 128+'n', 128+'o',
	128+'p', 128+'q', 128+'r', 128+'s', 128+'t', 128+'u', 128+'v', 128+'w',
	128+'x', 128+'y', 128+'z', 128+10,  128+12,  128+9,   '^',     128+'^',
	12,      'a',     'b',     'c',     'd',     'e',     'f',     'g',
	'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
	'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
	'x',     'y',     'z',     0,       0,       0,       12,      8
};

/* Track which unicode value was last generated by key presses (SDL
 * only guarantees to fill in the unicode field on key *releases*). */
static unsigned int unicode_last_keysym[SDLK_LAST];

static char *keymap_option;
static unsigned int *selected_keymap;
static int translated_keymap;

static FileReqModule *filereq;

static void map_keyboard(unsigned int *map) {
	int i;
	for (i = 0; i < 256; i++)
		sdl_to_keymap[i] = i & 0x7f;
	for (i = 0; i < SDLK_LAST; i++)
		unicode_last_keysym[i] = 0;
	if (map == NULL)
		return;
	while (*map) {
		unsigned int sdlkey = *(map++);
		unsigned int dgnkey = *(map++);
		sdl_to_keymap[sdlkey & 0xff] = dgnkey & 0x7f;
	}
}

static void getargs(int argc, char **argv) {
	int i;
	keymap_option = NULL;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-keymap")) {
			i++;
			if (i >= argc) break;
			keymap_option = argv[i];
		}
	}
}

static int init(void) {
	int i;
	filereq = filereq_init();
	if (filereq == NULL)
		return 0;
	selected_keymap = NULL;
	for (i = 0; mappings[i].name; i++) {
		if (selected_keymap == NULL
				&& !strcmp("uk", mappings[i].name)) {
			selected_keymap = mappings[i].raw;
		}
		if (keymap_option && !strcmp(keymap_option, mappings[i].name)) {
			selected_keymap = mappings[i].raw;
			LOG_DEBUG(2,"\tSelecting '%s' keymap\n",keymap_option);
		}
	}
	map_keyboard(selected_keymap);
	translated_keymap = 0;
	SDL_EnableUNICODE(translated_keymap);
	return 1;
}

static void shutdown(void) {
	if (filereq)
		filereq->shutdown();
}

static void keypress(SDL_keysym *keysym) {
	SDLKey sym = keysym->sym;
	if (emulate_joystick == 1) {
		if (sym == SDLK_UP) { joystick_lefty = 0; return; }
		if (sym == SDLK_DOWN) { joystick_lefty = 255; return; }
		if (sym == SDLK_LEFT) { joystick_leftx = 0; return; }
		if (sym == SDLK_RIGHT) { joystick_leftx = 255; return; }
		if (sym == SDLK_LALT) { PIA_0A.tied_low &= 0xfd; return; }
	}
	if (emulate_joystick == 2) {
		if (sym == SDLK_UP) { joystick_righty = 0; return; }
		if (sym == SDLK_DOWN) { joystick_righty = 255; return; }
		if (sym == SDLK_LEFT) { joystick_rightx = 0; return; }
		if (sym == SDLK_RIGHT) { joystick_rightx = 255; return; }
		if (sym == SDLK_LALT) { PIA_0A.tied_low &= 0xfe; return; }
	}
	if (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT) {
		shift = 1;
		KEYBOARD_PRESS(0);
		return;
	}
	if (sym == SDLK_LCTRL || sym == SDLK_RCTRL) { control = 1; return; }
	if (control) {
		switch (sym) {
		case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
			{
			const char *disk_exts[] = { "DMK", "JVC", "VDK", "DSK", NULL };
			char *filename = filereq->load_filename(disk_exts);
			if (filename)
				vdisk_load(filename, sym - SDLK_1);
			}
			break;
		case SDLK_a:
			video_artifact_mode++;
			if (video_artifact_mode > 2)
				video_artifact_mode = 0;
			vdg_set_mode();
			break;
		case SDLK_c:
			exit(0);
			break;
		case SDLK_e:
			requested_config.dos_type = DOS_ENABLED ? DOS_NONE : ANY_AUTO;
			break;
		case SDLK_f:
			if (video_module->set_fullscreen)
				video_module->set_fullscreen(!video_module->is_fullscreen);
			break;
		case SDLK_i:
			{
			const char *cart_exts[] = { "ROM", NULL };
			char *filename = filereq->load_filename(cart_exts);
			if (filename)
				cart_insert(filename, shift ? 0 : 1);
			else
				cart_remove();
			}
			break;
		case SDLK_j:
			emulate_joystick++;
			if (emulate_joystick > 2)
				emulate_joystick = 0;
			break;
		case SDLK_k:
			machine_set_keymap(running_config.keymap + 1);
			break;
		case SDLK_b:
		case SDLK_h:
		case SDLK_l:
		case SDLK_t:
			{
			char *filename;
			int type;
			filename = filereq->load_filename(NULL);
			if (filename == NULL)
				break;
			type = xroar_filetype_by_ext(filename);
			switch (type) {
			case FILETYPE_VDK: case FILETYPE_JVC:
			case FILETYPE_DMK:
				vdisk_load(filename, 0); break;
			case FILETYPE_BIN:
				coco_bin_read(filename); break;
			case FILETYPE_HEX:
				intel_hex_read(filename); break;
			case FILETYPE_SNA:
				read_snapshot(filename); break;
			case FILETYPE_CAS: default:
				if (shift)
					tape_autorun(filename);
				else
					tape_open_reading(filename);
				break;
			}
			}
			break;
		case SDLK_m:
			requested_machine = running_machine + 1;
			machine_reset(RESET_HARD);
			break;
		case SDLK_n:
			if (shift) video_next();
			else sound_next();
			break;
		case SDLK_r:
			machine_reset(shift ? RESET_HARD : RESET_SOFT);
			break;
		case SDLK_s:
			{
			const char *snap_exts[] = { "SNA", NULL };
			char *filename = filereq->save_filename(snap_exts);
			if (filename)
				write_snapshot(filename);
			}
			break;
		case SDLK_w:
			{
			const char *tape_exts[] = { "CAS", NULL };
			char *filename = filereq->save_filename(tape_exts);
			if (filename) {
				tape_open_writing(filename);
			}
			break;
			}
#ifdef TRACE
		case SDLK_v:
			trace = !trace;
			break;
#endif
		case SDLK_z: // running out of letters...
			translated_keymap = !translated_keymap;
			/* UNICODE translation only used in
			 * translation mode */
			SDL_EnableUNICODE(translated_keymap);
			break;
		default:
			break;
		}
		return;
	}
	if (sym == SDLK_UP) { KEYBOARD_PRESS(94); return; }
	if (sym == SDLK_DOWN) { KEYBOARD_PRESS(10); return; }
	if (sym == SDLK_LEFT) { KEYBOARD_PRESS(8); return; }
	if (sym == SDLK_RIGHT) { KEYBOARD_PRESS(9); return; }
	if (sym == SDLK_HOME) { KEYBOARD_PRESS(12); return; }
	if (translated_keymap) {
		unsigned int unicode;
		if (sym >= SDLK_LAST)
			return;
		unicode = keysym->unicode;
		unicode_last_keysym[sym] = unicode;
		if (unicode == '\\') {
			/* CoCo and Dragon 64 in 64K mode have a different way
			 * of scanning for '\' */
			if (IS_COCO_KEYMAP || (IS_DRAGON64 && !(PIA_1B.port_output & 0x04))) {
				KEYBOARD_PRESS(0);
				KEYBOARD_PRESS(12);
			} else {
				KEYBOARD_PRESS(0);
				KEYBOARD_PRESS(12);
				KEYBOARD_PRESS('/');
			}
			return;
		}
		if (shift && (unicode == 8 || unicode == 127)) {
			KEYBOARD_PRESS(0);
			KEYBOARD_PRESS(8);
			return;
		}
		if (unicode == 163) {
			KEYBOARD_PRESS(0);
			KEYBOARD_PRESS('3');
			return;
		}
		if (unicode < 128) {
			unsigned int code = unicode_to_dragon[unicode];
			if (code & 128)
				KEYBOARD_PRESS(0);
			else
				KEYBOARD_RELEASE(0);
			KEYBOARD_PRESS(code & 0x7f);
		}
		return;
	}
	if (sym < 256) {
		unsigned int mapped = sdl_to_keymap[sym];
		KEYBOARD_PRESS(mapped);
	}
}

#define JOY_UNLOW(j) if (j < 127) j = 127;
#define JOY_UNHIGH(j) if (j > 128) j = 128;

static void keyrelease(SDL_keysym *keysym) {
	SDLKey sym = keysym->sym;
	if (emulate_joystick == 1) {
		if (sym == SDLK_UP) { JOY_UNLOW(joystick_lefty); return; }
		if (sym == SDLK_DOWN) { JOY_UNHIGH(joystick_lefty); return; }
		if (sym == SDLK_LEFT) { JOY_UNLOW(joystick_leftx); return; }
		if (sym == SDLK_RIGHT) { JOY_UNHIGH(joystick_leftx); return; }
		if (sym == SDLK_LALT) { PIA_0A.tied_low |= 0x02; return; }
	}
	if (emulate_joystick == 2) {
		if (sym == SDLK_UP) { JOY_UNLOW(joystick_righty); return; }
		if (sym == SDLK_DOWN) { JOY_UNHIGH(joystick_righty); return; }
		if (sym == SDLK_LEFT) { JOY_UNLOW(joystick_rightx); return; }
		if (sym == SDLK_RIGHT) { JOY_UNHIGH(joystick_rightx); return; }
		if (sym == SDLK_LALT) { PIA_0A.tied_low |= 0x01; return; }
	}
	if (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT) {
		shift = 0;
		KEYBOARD_RELEASE(0);
		return;
	}
	if (sym == SDLK_LCTRL || sym == SDLK_RCTRL) { control = 0; return; }
	if (sym == SDLK_UP) { KEYBOARD_RELEASE(94); return; }
	if (sym == SDLK_DOWN) { KEYBOARD_RELEASE(10); return; }
	if (sym == SDLK_LEFT) { KEYBOARD_RELEASE(8); return; }
	if (sym == SDLK_RIGHT) { KEYBOARD_RELEASE(9); return; }
	if (sym == SDLK_HOME) { KEYBOARD_RELEASE(12); return; }
	if (translated_keymap) {
		unsigned int unicode;
		if (sym >= SDLK_LAST)
			return;
		unicode = unicode_last_keysym[sym];
		if (unicode == '\\') {
			/* CoCo and Dragon 64 in 64K mode have a different way
			 * of scanning for '\' */
			if (IS_COCO_KEYMAP || (IS_DRAGON64 && !(PIA_1B.port_output & 0x04))) {
				KEYBOARD_RELEASE(0);
				KEYBOARD_RELEASE(12);
			} else {
				KEYBOARD_RELEASE(0);
				KEYBOARD_RELEASE(12);
				KEYBOARD_RELEASE('/');
			}
			return;
		}
		if (shift && (unicode == 8 || unicode == 127)) {
			KEYBOARD_RELEASE(0);
			KEYBOARD_RELEASE(8);
			return;
		}
		if (unicode == 163) {
			KEYBOARD_RELEASE(0);
			KEYBOARD_RELEASE('3');
			return;
		}
		if (unicode < 128) {
			unsigned int code = unicode_to_dragon[unicode];
			if (code & 128)
				KEYBOARD_RELEASE(0);
			if (shift)
				KEYBOARD_PRESS(0);
			KEYBOARD_RELEASE(code & 0x7f);
		}
		return;
	}
	if (sym < 256) {
		unsigned int mapped = sdl_to_keymap[sym];
		KEYBOARD_RELEASE(mapped);
	}
}

static void poll(void) {
	SDL_Event event;
	while (SDL_PollEvent(&event) == 1) {
		switch(event.type) {
			case SDL_VIDEORESIZE:
				if (video_module->resize) {
					video_module->resize(event.resize.w, event.resize.h);
				}
				break;
			case SDL_QUIT:
				exit(0); break;
			case SDL_KEYDOWN:
				keypress(&event.key.keysym);
				keyboard_column_update();
				keyboard_row_update();
				break;
			case SDL_KEYUP:
				keyrelease(&event.key.keysym);
				keyboard_column_update();
				keyboard_row_update();
				break;
			default:
				break;
		}
	}
}
