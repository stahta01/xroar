/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2004  Ciaran Anscomb
 *
 *  See COPYING for redistribution conditions. */

#ifndef __UI_H__
#define __UI_H__

typedef struct UIModule UIModule;
struct UIModule {
	UIModule *next;
	const char *name;
	const char *help;
	int (*init)(void);
	void (*shutdown)(void);
	void (*menu)(void);
	char *(*get_filename)(const char **extensions);
};

extern UIModule *ui_module;

void ui_getargs(int argc, char **argv);
int ui_init(void);
void ui_shutdown(void);

#endif  /* __UI_H__ */
