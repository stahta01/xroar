/** \file
 *
 *  \brief main() function.
 *
 *  \copyright Copyright 2003-2026 Ciaran Anscomb
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
#include <time.h>
#include <unistd.h>

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef UI_SDL3
#include <SDL3/SDL_main.h>
#endif

#include "events.h"
#include "ui.h"
#include "xroar.h"
#include "logging.h"

#include "wasm/wasm.h"

#ifdef HAVE_SIGNAL_H
#ifndef HAVE_WASM

/** \brief Signal handler.
 *
 * For UIs that do not provide their own run(), we trap SIGTERM and SIGINT to
 * point to this signal handler, which will set the shutdown flag, terminating
 * the controlling while loop.
 *
 * We don't do this for other UIs, as toolkits may have cleaner (to them) ways
 * of dealing with OS signals.
 */

static bool shutdown = 0;

static void handle_signal(int sig) {
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		shutdown = 1;
		break;

#ifdef SIGHUP
	case SIGHUP:
		signal(SIGHUP, handle_signal);
		break;
#endif

	default:
		break;
	}
}

#endif
#endif

/** \brief Entry point.
 *
 * Sets up the exit handler and calls xroar_init(), which will process all
 * configuration and return a UI interface.
 *
 * xroar_init_finish() is then called to finish initialisation and attach any
 * media.  If the interface provides its own run() method, it is called,
 * otherwise a default "main loop" repeatedly calls xroar_run().
 */

int main(int argc, char **argv) {
	atexit(xroar_shutdown);

	srand(getpid() ^ time(NULL));

#ifndef HAVE_WASM
	struct ui_interface *ui = xroar_init(argc, argv);
	if (!ui) {
		exit(EXIT_FAILURE);
	}

	// In normal builds, just finish up initialisation.
	xroar_init_finish();

	// If the UI interface provides its own run() delegate, just call that
	// (e.g. the GTK+ UI sets up emulated execution as an idle function and
	// passes control to GTK's own main loop).  Otherwise just repeatedly
	// call xroar_run().
	if (DELEGATE_DEFINED(ui->run)) {
		DELEGATE_CALL(ui->run);
	} else {
#ifdef HAVE_SIGNAL_H
		// Intercept signals to shutdown emulator
		signal(SIGTERM, handle_signal);
		signal(SIGINT, handle_signal);
#ifdef SIGHUP
		signal(SIGHUP, handle_signal);
#endif
#endif
		while (!shutdown) {
			xroar_run(EVENT_MS(10));
		}
		xroar_shutdown();
	}
#else
	// The WebAssembly build has its own approach to completing
	// initialisation in order to allow initial required file transfers to
	// complete.
	wasm_init(argc, argv);
#endif
	return 0;
}
