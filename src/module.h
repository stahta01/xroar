/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2016  Ciaran Anscomb
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
	// new interface
	void *(*new)(void);
	// old interface
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
	void *(* const write_buffer)(void *buffer);
} SoundModule;

extern FileReqModule * const *filereq_module_list;
extern FileReqModule *filereq_module;
extern SoundModule * const *sound_module_list;
extern SoundModule *sound_module;

void module_print_list(struct module * const *list);
struct module *module_select(struct module * const *list, const char *name);
struct module *module_select_by_arg(struct module * const *list, const char *name);
struct module *module_init(struct module *module);
struct module *module_init_from_list(struct module * const *list, struct module *module);
void module_shutdown(struct module *module);

#endif  /* XROAR_MODULE_H_ */
