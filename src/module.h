/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2015  Ciaran Anscomb
 *
 *  See COPYING.GPL for redistribution conditions. */

#ifndef XROAR_MODULE_H_
#define XROAR_MODULE_H_

#include <stdint.h>

struct joystick_module;
struct vdisk;

struct module {
	const char *name;
	const char *description;
	_Bool (* const init)(void);
	_Bool initialised;
	void (* const shutdown)(void);
};

typedef struct {
	struct module common;
	char *(* const load_filename)(char const * const *extensions);
	char *(* const save_filename)(char const * const *extensions);
} FileReqModule;

typedef struct {
	struct module common;
	int scanline;
	int window_x, window_y;
	int window_w, window_h;
	void (* const update_palette)(void);
	void (* const resize)(unsigned int w, unsigned int h);
	int (* const set_fullscreen)(_Bool fullscreen);
	_Bool is_fullscreen;
	void (*render_scanline)(uint8_t const *scanline_data);
	void (* const vsync)(void);
	void (* const refresh)(void);
	void (* const update_cross_colour_phase)(void);
} VideoModule;

typedef struct {
	struct module common;
	void *(* const write_buffer)(void *buffer);
} SoundModule;

extern FileReqModule * const *filereq_module_list;
extern FileReqModule *filereq_module;
extern VideoModule * const *video_module_list;
extern VideoModule *video_module;
extern SoundModule * const *sound_module_list;
extern SoundModule *sound_module;

void module_print_list(struct module * const *list);
struct module *module_select(struct module * const *list, const char *name);
struct module *module_select_by_arg(struct module * const *list, const char *name);
struct module *module_init(struct module *module);
struct module *module_init_from_list(struct module * const *list, struct module *module);
void module_shutdown(struct module *module);

#endif  /* XROAR_MODULE_H_ */
