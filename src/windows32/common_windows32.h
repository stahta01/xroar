/*

Windows user-interface common functions

Copyright 2006-2017 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifndef XROAR_COMMON_WINDOWS32_H_
#define XROAR_COMMON_WINDOWS32_H_

#include <windows.h>

extern HWND windows32_main_hwnd;

int windows32_init(_Bool alloc_console);
void windows32_shutdown(void);

void windows32_handle_wm_command(WPARAM wParam, LPARAM lParam);

#endif
