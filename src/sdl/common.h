/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2017  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_SDL_COMMON_H_
#define XROAR_SDL_COMMON_H_

#include <SDL_syswm.h>

#include "module.h"
#include "vo.h"
struct joystick_module;

extern struct vo_rect sdl_display;

extern struct module vo_sdlgl_module;
extern struct module vo_sdlyuv_module;
extern struct module vo_sdl_module;
extern struct module vo_null_module;

extern struct joystick_submodule sdl_js_submod_physical;
extern struct joystick_submodule sdl_js_submod_keyboard;
extern struct joystick_module sdl_js_internal;

extern struct module * const sdl_vo_module_list[];
extern struct joystick_module * const sdl_js_modlist[];

void ui_sdl_run(void *sptr);
void sdl_keyboard_init(void);
void sdl_keyboard_set_translate(_Bool);
void sdl_keypress(SDL_keysym *keysym);
void sdl_keyrelease(SDL_keysym *keysym);
void sdl_js_physical_shutdown(void);

void sdl_zoom_in(void);
void sdl_zoom_out(void);

// Fake "SDL_Window" type
typedef void SDL_Window;

#ifdef WINDOWS32

/* These functions will be in the windows32-specific code. */

void sdl_windows32_handle_syswmevent(SDL_SysWMmsg *);
void sdl_windows32_set_events_window(SDL_Window *sw);
void sdl_windows32_add_menu(SDL_Window *sw);
void sdl_windows32_remove_menu(SDL_Window *sw);

#endif

#endif  /* XROAR_SDL_COMMON_H_ */
