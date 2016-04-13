/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2016  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_SDL2_COMMON_H_
#define XROAR_SDL2_COMMON_H_

#include <SDL_syswm.h>

#include "vo.h"
struct joystick_module;

extern SDL_Window *sdl_window;
extern Uint32 sdl_windowID;

extern unsigned sdl_window_x, sdl_window_y;
extern unsigned sdl_window_w, sdl_window_h;

extern struct vo_module vo_sdl_module;

extern struct joystick_interface sdl_js_if_physical;
extern struct joystick_interface sdl_js_if_keyboard;
extern struct joystick_module sdl_js_internal;

extern struct vo_module * const sdl_vo_module_list[];
extern struct joystick_module * const sdl_js_modlist[];

void sdl_run(void);
void sdl_keyboard_init(void);
void sdl_keyboard_set_translate(_Bool);
void sdl_keypress(SDL_Keysym *keysym);
void sdl_keyrelease(SDL_Keysym *keysym);
void sdl_js_physical_shutdown(void);

void sdl_zoom_in(void);
void sdl_zoom_out(void);

/* Platform-specific support */

#ifdef HAVE_X11

/* X11 event interception. */

void sdl_x11_handle_syswmevent(SDL_SysWMmsg *);

/* X11 keyboard handling. */

void sdl_x11_keyboard_init(SDL_Window *sw);
void sdl_x11_keyboard_free(SDL_Window *sw);

void sdl_x11_mapping_notify(XMappingEvent *);
void sdl_x11_keymap_notify(XKeymapEvent *);

void sdl_x11_fix_keyboard_event(SDL_Event *);
int sdl_x11_keysym_to_unicode(SDL_Keysym *);

#endif

#ifdef WINDOWS32

void sdl_windows32_keyboard_init(SDL_Window *);
int sdl_windows32_keysym_to_unicode(SDL_Keysym *);

/* These functions will be in the windows32-specific code. */

void sdl_windows32_handle_syswmevent(SDL_SysWMmsg *);
void sdl_windows32_set_events_window(SDL_Window *);
void sdl_windows32_add_menu(SDL_Window *);
void sdl_windows32_remove_menu(SDL_Window *);

#endif

/* Now wrap all of the above in inline functions so that common code doesn't
 * need to be littered with these conditionals. */

inline void sdl_os_keyboard_init(SDL_Window *sw) {
	(void)sw;
#if defined(HAVE_X11)
	sdl_x11_keyboard_init(sw);
#elif defined(WINDOWS32)
	sdl_windows32_keyboard_init(sw);
#endif
}

inline void sdl_os_keyboard_free(SDL_Window *sw) {
	(void)sw;
#if defined(HAVE_X11)
	sdl_x11_keyboard_free(sw);
#endif
}

inline void sdl_os_handle_syswmevent(SDL_SysWMmsg *wmmsg) {
	(void)wmmsg;
#if defined(HAVE_X11)
	sdl_x11_handle_syswmevent(wmmsg);
#elif defined(WINDOWS32)
	sdl_windows32_handle_syswmevent(wmmsg);
#endif
}

inline void sdl_os_fix_keyboard_event(SDL_Event *ev) {
	(void)ev;
#if defined(HAVE_X11)
	sdl_x11_fix_keyboard_event(ev);
#endif
}

inline int sdl_os_keysym_to_unicode(SDL_Keysym *keysym) {
#if defined(HAVE_X11)
	return sdl_x11_keysym_to_unicode(keysym);
#elif defined(WINDOWS32)
	return sdl_windows32_keysym_to_unicode(keysym);
#endif
	return keysym->sym;
}

#endif  /* XROAR_SDL2_COMMON_H_ */
