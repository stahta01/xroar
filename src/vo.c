/*

XRoar, a Dragon 32/64 emulator
Copyright 2003-2015 Ciaran Anscomb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.

*/

#include "config.h"

#include <stdlib.h>

#include "module.h"
#include "vo.h"

extern struct vo_module vo_null_module;
static struct vo_module * const default_vo_module_list[] = {
	&vo_null_module,
	NULL
};

struct vo_module * const *vo_module_list = default_vo_module_list;
struct vo_module *vo_module = NULL;
