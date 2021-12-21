/** \file
 *
 *  \brief main() function.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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

#include "top-config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_WASM
#include <emscripten.h>
#endif

#include "events.h"
#include "ui.h"
#include "xroar.h"
#include "logging.h"

/** \brief Entry point.
 *
 * Sets up the exit handler and calls xroar_init(), which will process all
 * configuration and return a UI interface.  If the returned interface provides
 * its own run() method, calls that, otherwise provides a default "main loop"
 * that repeatedly calls xroar_run().
 */

int main(int argc, char **argv) {
	atexit(xroar_shutdown);
	struct ui_interface *ui = xroar_init(argc, argv);
	if (!ui) {
		exit(EXIT_FAILURE);
	}

#ifdef HAVE_WASM
	EM_ASM( ui_done_initialising(); );
	emscripten_set_main_loop_arg(ui->run.func, ui->run.sptr, 0, 0);
	// In Wasm, main() will now return!
#else
	if (DELEGATE_DEFINED(ui->run)) {
		DELEGATE_CALL(ui->run);
	} else {
		for (;;) {
			xroar_run(EVENT_MS(10));
		}
	}
#endif
	return 0;
}
