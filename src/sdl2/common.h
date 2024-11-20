/** \file
 *
 *  \brief SDL2 user-interface common functions.
 *
 *  \copyright Copyright 2015-2024 Ciaran Anscomb
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

#ifndef XROAR_SDL2_COMMON_H_
#define XROAR_SDL2_COMMON_H_

#include <stdint.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "ui.h"
#include "vo.h"

struct joystick_module;
struct keyboard_sdl2_state;

struct ui_sdl2_interface {
	struct ui_interface ui_interface;

	struct ui_cfg *cfg;

	// Shared SDL2 data
	SDL_Window *vo_window;
	Uint32 vo_window_id;

	// Viewport size not modified by 60Hz scaling
	struct vo_viewport viewport;
	// User-specified geometry inhibits auto-resize
	_Bool user_specified_geometry;

	// Pointer state
	_Bool mouse_hidden;
};

extern struct ui_sdl2_interface *global_uisdl2;

struct ui_sdl2_interface *ui_sdl_allocate(size_t usize);
void ui_sdl_init(struct ui_sdl2_interface *, struct ui_cfg *ui_cfg);
void ui_sdl_free(void *);
void ui_sdl_run(void *);

_Bool sdl_vo_init(struct ui_sdl2_interface *);
void sdl_keyboard_init(struct ui_sdl2_interface *);

extern struct joystick_submodule sdl_js_physical;
extern struct joystick_module sdl_js_internal;

extern struct module * const sdl2_vo_module_list[];
extern struct joystick_module * const sdl_js_modlist[];

int filter_sdl_events(void *userdata, SDL_Event *event);
void run_sdl_event_loop(struct ui_sdl2_interface *uisdl2);
void sdl_keypress(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym);
void sdl_keyrelease(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym);

void sdl_js_enable_events(void);
void sdl_js_device_added(int index);
void sdl_js_device_removed(int index);
void sdl_js_physical_shutdown(void);

void sdl_vo_notify_render_device_reset(struct ui_sdl2_interface *uisdl2);
void sdl_vo_notify_size_changed(struct ui_sdl2_interface *uisdl2, int w, int h);

/* Platform-specific support */

#ifdef HAVE_X11

/* X11 event interception. */

void sdl_x11_handle_syswmevent(SDL_SysWMmsg *);

#endif

#ifdef WINDOWS32

/* These functions will be in the windows32-specific code. */

void sdl_windows32_handle_syswmevent(struct ui_sdl2_interface *, SDL_SysWMmsg *);
void sdl_windows32_set_events_window(SDL_Window *);
void sdl_windows32_set_menu_visible(struct ui_sdl2_interface *, _Bool visible);

#endif

/* Now wrap all of the above in inline functions so that common code doesn't
 * need to be littered with these conditionals. */

inline void sdl_os_handle_syswmevent(struct ui_sdl2_interface *uisdl2, SDL_SysWMmsg *wmmsg) {
	(void)uisdl2;
	(void)wmmsg;
#if defined(HAVE_X11)
	sdl_x11_handle_syswmevent(wmmsg);
#elif defined(WINDOWS32)
	sdl_windows32_handle_syswmevent(uisdl2, wmmsg);
#endif
}

#endif
