#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([xroar], [0.37])
AM_INIT_AUTOMAKE([foreign])

MANPAGE_DATE="June 2020"
AC_SUBST(MANPAGE_DATE)

AC_CONFIG_SRCDIR([src/xroar.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CC
AC_PROG_OBJC
#AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_RANLIB

AN_MAKEVAR([RC], [AC_PROG_RC])
AN_PROGRAM([windres], [AC_PROG_RC])
AC_CHECK_TOOL(RC, windres, :)

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([endian.h regex.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([getaddrinfo popen strnlen strsep])
AX_GCC_BUILTIN(__builtin_parity)
AX_GCC_VAR_ATTRIBUTE(packed)

AC_PATH_XTRA

### Options

AC_ARG_ENABLE([snapshot],[])

AC_ARG_ENABLE([wasm],
	AS_HELP_STRING([--enable-wasm], [build for WebAssembly using emscripten]) )

AC_ARG_WITH([gtk2],
	AS_HELP_STRING([--without-gtk2], [don't test for GTK+-2]) )

AC_ARG_WITH([gtkgl],
	AS_HELP_STRING([--without-gtkgl], [don't test for GtkGLExt]) )
test "x$with_gtk2" = "xno" && with_gtkgl=no

AC_ARG_ENABLE([kbd_translate],
	AS_HELP_STRING([--disable-kbd-translate], [don't support keyboard translation]) )

AC_ARG_ENABLE([simulated_ntsc],
	AS_HELP_STRING([--disable-simulated-ntsc], [remove accurate NTSC support for speed]) )

AC_ARG_WITH([sdl2],
	AS_HELP_STRING([--without-sdl2], [don't test for SDL2]) )

AC_ARG_WITH([cocoa],
	AS_HELP_STRING([--without-cocoa], [don't test for Mac OS X Cocoa]) )
test "x$with_sdl2" = "xno" && with_cocoa=no

AC_ARG_ENABLE([filereq_cli],
	AS_HELP_STRING([--enable-filereq-cli], [include command-line file requester]) )

AC_ARG_WITH([alsa],
	AS_HELP_STRING([--without-alsa], [don't test for ALSA audio]) )

AC_ARG_WITH([oss],
	AS_HELP_STRING([--without-oss], [don't test for OSS audio]) )

AC_ARG_WITH([pulse],
	AS_HELP_STRING([--without-pulse], [don't test for PulseAudio]) )

AC_ARG_WITH([coreaudio],
	AS_HELP_STRING([--without-coreaudio], [don't test for Mac OS X Core Audio]) )

AC_ARG_WITH([jack],
	AS_HELP_STRING([--with-jack], [test for JACK audio]) )

AC_ARG_ENABLE([ao_null],
	AS_HELP_STRING([--disable-ao-null], [don't build null audio driver]) )

AC_ARG_WITH([joydev],
	AS_HELP_STRING([--without-joydev], [don't test for Linux joystick interface]) )

AC_ARG_WITH([sndfile],
	AS_HELP_STRING([--without-sndfile], [don't test for libsndfile]) )

AC_ARG_WITH([tre],
	AS_HELP_STRING([--with-tre], [test for TRE]) )

AC_ARG_ENABLE([logging],
	AS_HELP_STRING([--disable-logging], [disable logging output]) )

AC_ARG_ENABLE([trace],
	AS_HELP_STRING([--disable-trace], [disable trace mode]) )

AC_ARG_WITH([pthreads],
	AS_HELP_STRING([--without-pthreads], [don't use POSIX threads]) )

AC_ARG_WITH([zlib],
	AS_HELP_STRING([--without-zlib], [don't use zlib]) )

AC_ARG_ENABLE([gdb_target],
	AS_HELP_STRING([--disable-gdb-target], [don't include GDB target (requires pthreads)]) )

#'

### WebAssembly

AM_CONDITIONAL([WASM], [test "x$enable_wasm" = "xyes"])
AM_COND_IF([WASM], [
		AC_DEFINE([HAVE_WASM], 1, [WebAssembly build])
		WASM_CFLAGS=""
		WASM_LIBS="[-s WASM=1 -s EXPORTED_FUNCTIONS='@wasm/exported_functions' -s EXPORTED_RUNTIME_METHODS=\"['FS','ccall','cwrap']\"]"
		test -z "$SDL_CFLAGS" && SDL_CFLAGS="-s USE_SDL=2"
		test -z "$SDL_LIBS" && SDL_LIBS="-s USE_SDL=2"
		test -z "$enable_trace" && enable_trace=no
		test -z "$enable_ao_null" && enable_ao_null=no
		test -z "$enable_gdb_target" && enable_gdb_target=no
		test -z "$with_x" && with_x=no
		test -z "$no_x" && no_x=yes
		test -z "$with_gtk2" && with_gtk2=no
		test -z "$with_cocoa" && with_cocoa=no
		test -z "$with_alsa" && with_alsa=no
		test -z "$with_oss" && with_oss=no
		test -z "$with_pulse" && with_pulse=no
		test -z "$with_coreaudio" && with_coreaudio=no
		test -z "$with_joydev" && with_joydev=no
		test -z "$with_sndfile" && with_sndfile=no
		test -z "$with_tre" && with_tre=no
		test -z "$with_pthreads" && with_pthreads=no
		test -z "$with_zlib" && with_zlib=no
		EXEEXT=".js"
		])
AC_SUBST(WASM_CFLAGS)
AC_SUBST(WASM_LIBS)

### MinGW

unset have_mingw
case "$host_os" in
mingw*)
	have_mingw=1
	;;
esac

AM_CONDITIONAL([MINGW], [test -n "$have_mingw"])
AM_COND_IF([MINGW], [
		AC_DEFINE([WINDOWS32], 1, [Windows])
		MINGW_CFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
		MINGW_LIBS="-lmingw32 -lwinmm -mwindows -lws2_32"
		])
AC_SUBST(MINGW_CFLAGS)
AC_SUBST(MINGW_LIBS)

### zlib

AS_IF([test "x$with_zlib" != "xno"], [
		AC_CHECK_LIB([z], [crc32], [ZLIB_LIBS="-lz"])
		AS_IF([test -z "$ZLIB_LIBS"], [
			PKG_CHECK_MODULES([ZLIB], [zlib], :, :)
			]) ])

unset have_zlib
AS_IF([test -n "$ZLIB_LIBS"], [
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $ZLIB_CFLAGS"
		LIBS="$LIBS $ZLIB_LIBS"
		AC_MSG_CHECKING([if zlib is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <stdlib.h>
#include <zlib.h>
int main(int argc, char **argv) { (void)argc; (void)argv; (void)crc32(0L, NULL, 0); (void)gzopen("", ""); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_zlib=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([ZLIB], [test -n "$have_zlib"])
AM_COND_IF([ZLIB], [AC_DEFINE([HAVE_ZLIB], 1, [Have zlib])])

### X11

unset have_xlib
AS_IF([test "x$no_x" != "xyes"], [
		X11_CFLAGS="$X_CFLAGS"
		X11_LIBS="$X_PRE_LIBS $X_LIBS -lX11 $X_EXTRA_LIBS"
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $X11_CFLAGS"
		LIBS="$LIBS $X11_LIBS"
		AC_MSG_CHECKING([if X is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
int main(int argc, char **argv) { (void)argc; (void)argv; (void)XKeysymToKeycode(NULL, 0); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_xlib=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([X11], [test -n "$have_xlib"])
AM_COND_IF([X11], [AC_DEFINE([HAVE_X11], 1, [Have xlib])])
AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)

### POSIX threads

AS_IF([test "x$with_pthreads" != "xno"], [
		AM_COND_IF([MINGW], [
			PTHREADS_LIBS="-Wl,-Bstatic -lpthread -Wl,-Bdynamic"
			], [
			PTHREADS_LIBS="-pthread"
			]) ])
AC_SUBST(PTHREADS_LIBS)

unset have_pthreads
AS_IF([test -n "$PTHREADS_LIBS"], [
		OLD_LIBS="$LIBS"
		LIBS="$LIBS $PTHREADS_LIBS"
		AC_MSG_CHECKING([POSIX threads])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <pthread.h>
int main(int argc, char **argv) { (void)argc; (void)argv; pthread_create((void *)1,0,(void *)2,0); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_pthreads=1
			], [AC_MSG_RESULT([no])] )
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([PTHREADS], [test -n "$have_pthreads"])
AM_COND_IF([PTHREADS], [AC_DEFINE([HAVE_PTHREADS], 1, [POSIX threads])])

### Endian

AC_C_BIGENDIAN([AC_DEFINE([HAVE_BIG_ENDIAN], 1, [Correct-endian architecture])])

### ALSA

AS_IF([test "x$with_alsa" != "xno"], [
		OLD_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS -D_POSIX_C_SOURCE=200112L"
		AC_CHECK_HEADER([alsa/asoundlib.h], [
			AC_CHECK_LIB([asound], [snd_pcm_open], [
				ALSA_LIBS="-lasound"
				], :) ])
		CFLAGS="$OLD_CFLAGS"
	])
AM_CONDITIONAL([AO_ALSA], [test -n "$ALSA_LIBS"])
AM_COND_IF([AO_ALSA], [AC_DEFINE([HAVE_ALSA_AUDIO], 1, [Use ALSA audio])])
AC_SUBST(ALSA_LIBS)

### OSS

unset have_oss_audio
AS_IF([test "x$with_oss" != "xno"], [
		AC_CHECK_HEADER([sys/soundcard.h], [
			have_oss_audio=1
			], :) ] )
AM_CONDITIONAL([AO_OSS], [test -n "$have_oss_audio"])
AM_COND_IF([AO_OSS], [AC_DEFINE([HAVE_OSS_AUDIO], 1, [Use OSS audio])])
AC_SUBST(OSS_LIBS)

### PulseAudio

AS_IF([test "x$with_pulse" != "xno"], [
	PKG_CHECK_MODULES([PULSE], [libpulse-simple], :, :) ], :)

unset have_pulse
AS_IF([test -n "$PULSE_LIBS"], [
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $PULSE_CFLAGS"
		LIBS="$LIBS $PULSE_LIBS"
		AC_MSG_CHECKING([if PulseAudio is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <pulse/simple.h>
int main(int argc, char **argv) { (void)argc; (void)argv; pa_simple *p = pa_simple_new(0,0,0,0,0,0,0,0,0); (void)p; return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_pulse=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([AO_PULSE], [test -n "$have_pulse"])
AM_COND_IF([AO_PULSE], [AC_DEFINE([HAVE_PULSE], 1, [Have PulseAudio])])

### Mac OS X Core Audio

AS_IF([test "x$with_coreaudio" != "xno"], [
		AC_CHECK_HEADER([CoreAudio/AudioHardware.h], [
			COREAUDIO_LIBS="-framework CoreAudio"
			], :) ] )

AM_CONDITIONAL([AO_COREAUDIO], [test -n "$COREAUDIO_LIBS"])
AM_COND_IF([AO_COREAUDIO], [
		AC_DEFINE([HAVE_MACOSX_AUDIO], 1, [Have Mac OS X Core Audio framework]) ])
AC_SUBST(COREAUDIO_LIBS)

### JACK audio

AS_IF([test "x$with_jack" = "xyes"], [
		PKG_CHECK_MODULES([JACK], [jack], :, :) ], :)

unset have_jack
AS_IF([test -n "$JACK_LIBS"], [
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $JACK_CFLAGS"
		LIBS="$LIBS $JACK_LIBS"
		AC_MSG_CHECKING([if JACK is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <jack/jack.h>
int main(int argc, char **argv) { (void)argc; (void)argv; jack_client_t *c; jack_activate(c); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_jack=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([AO_JACK], [test -n "$JACK_LIBS"])
AM_COND_IF([AO_JACK], [AC_DEFINE([HAVE_JACK_AUDIO], 1, [Use JACK audio])])

### GTK+-2

AS_IF([test "x$with_gtk2" != "xno"], [
		PKG_CHECK_MODULES([GTK2], [gtk+-2.0], :, :) ], :)

unset have_gtk2
AS_IF([test -n "$GTK2_LIBS"], [
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $GTK2_CFLAGS"
		LIBS="$LIBS $GTK2_LIBS"
		AC_MSG_CHECKING([if GTK+ 2 is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <gtk/gtk.h>
int main(int argc, char **argv) { gtk_init(&argc, &argv); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_gtk2=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([GTK2], [test -n "$have_gtk2"])
AM_COND_IF([GTK2], [AC_DEFINE([HAVE_GTK2], 1, [Use GTK+ 2])])

GLIB_COMPILE_RESOURCES=`$PKG_CONFIG --variable glib_compile_resources gio-2.0`
AC_SUBST(GLIB_COMPILE_RESOURCES)

### GtkGLExt

AS_IF([test "x$with_gtkgl" != "xno"], [
		PKG_CHECK_MODULES([GTKGL], [gtkglext-1.0], :, :)], :)

unset have_gtkgl
AS_IF([test -n "$GTKGL_LIBS"], [
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $GTKGL_CFLAGS"
		LIBS="$LIBS $GTKGL_LIBS"
		AC_MSG_CHECKING([if GtkGLExt is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <gtk/gtkgl.h>
int main(int argc, char **argv) { gtk_gl_init(&argc, &argv); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			AC_DEFINE(HAVE_GTKGL, 1, [Have GtkGLExt])
			have_gtkgl=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([GTKGL], [test -n "$have_gtkgl"])
AM_COND_IF([GTKGL], [AC_DEFINE([HAVE_GTKGL], 1, [Use GtkGLExt])])

### OpenGL

AS_IF([test -n "$have_gtkgl"], [
		AM_CONDITIONAL([OPENGL], [true])
	], [
		AX_CHECK_GL
		AS_IF([test -z "$GL_LIBS"], [
				OLD_LIBS="$LIBS"
				LIBS="$LIBS -framework OpenGL"
				AC_MSG_CHECKING([if -framework OpenGL works instead])
				AC_LINK_IFELSE([AC_LANG_SOURCE([
		#include <OpenGL/gl.h>
		int main(int argc, char **argv) { (void)argc; (void)argv; GLuint t; glGenTextures(1, &t); return 0; }
						])], [
					AC_MSG_RESULT([yes])
					GL_LIBS="-framework OpenGL"
					], [AC_MSG_RESULT([no])] )
				LIBS="$OLD_LIBS"
				])
		AM_CONDITIONAL([OPENGL], [test -n "$GL_LIBS"])
	])

### Linux joysticks

unset have_linux_joystick
AS_IF([test "x$with_joydev" != "xno"], [
		AC_CHECK_HEADER([linux/joystick.h], [have_linux_joystick=1], :) ])

AM_CONDITIONAL([LINUX_JOYSTICK], [test -n "$have_linux_joystick"])
AM_COND_IF([LINUX_JOYSTICK], [AC_DEFINE([HAVE_LINUX_JOYSTICK], 1, [Have linux/joystick.h])])

### SDL2

AS_IF([test "x$with_sdl2" != "xno"], [
		AM_PATH_SDL2([2.0.0], :) ], :)

unset have_sdl2
AS_IF([test -n "$SDL_LIBS"], [
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $SDL_CFLAGS"
		LIBS="$LIBS $SDL_LIBS"
		AC_MSG_CHECKING([if SDL2 is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <SDL.h>
#include <SDL_syswm.h>
int main(int argc, char **argv) { (void)argc; (void)argv; SDL_Init(0); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_sdl2=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([UI_SDL2], [test -n "$have_sdl2"])
AM_COND_IF([UI_SDL2], [AC_DEFINE([HAVE_SDL2], 1, [Have SDL2])])

### Mac OS X Cocoa

unset have_cocoa
if test "x$ac_cv_objc_compiler_gnu" = "xyes"; then
	AM_COND_IF([UI_SDL2], [AS_IF([test "x$with_cocoa" != "xno"], [
		    COCOA_LIBS="-framework Cocoa -framework Carbon"
		    AC_LANG_PUSH([Objective C])
		    AC_CHECK_HEADER([Cocoa/Cocoa.h], [
				     have_cocoa=1
				     ], :)
		    AC_LANG_POP([Objective C]) ]) ])
fi
AC_SUBST(COCOA_CFLAGS)
AC_SUBST(COCOA_LIBS)

# Cocoa is the only thing that requires Objective C.  If it's not found, copy
# standard C settings into OBJC & OBJCFLAGS, as user should not have to guess
# that an ObjC compiler is going to be used anyway (I don't know how to
# conditionally test for ObjC compiler).

AM_CONDITIONAL([UI_COCOA], [test -n "$have_cocoa"])
AM_COND_IF([UI_COCOA], [
		AC_DEFINE([HAVE_COCOA], 1, [Have Mac OS X Cocoa framework])
	], [
		OBJC="$CC"
		OBJCFLAGS="$CFLAGS"
	])

### libsndfile

AS_IF([test "x$with_sndfile" != "xno"], [
		PKG_CHECK_MODULES([SNDFILE], [sndfile], :, :)], :)

unset have_sndfile
AS_IF([test -n "$SNDFILE_LIBS"], [
	OLD_CFLAGS="$CFLAGS"
	OLD_LIBS="$LIBS"
	CFLAGS="$CFLAGS $SNDFILE_CFLAGS"
	LIBS="$LIBS $SNDFILE_LIBS"
	AC_MSG_CHECKING([if libsndfile is usable])
	AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <sndfile.h>
int main(int argc, char **argv) { (void)argc; (void)argv; SNDFILE *f; SF_INFO i; f = sf_open("", SFM_READ, &i); (void)f; return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_sndfile=1
			], [AC_MSG_RESULT([no])] )
	CFLAGS="$OLD_CFLAGS"
	LIBS="$OLD_LIBS"
	])

AM_CONDITIONAL([SNDFILE], [test -n "$have_sndfile"])
AM_COND_IF([SNDFILE], [AC_DEFINE([HAVE_SNDFILE], 1, [Use libsndfile])])

### TRE

if test "x$ac_cv_header_regex_h" != "xyes" -a "x$with_tre" != "xno"; then
	with_tre=yes
fi

AS_IF([test "x$with_tre" = "xyes"], [
       TRE_LIBS="-ltre"
       ])
AC_SUBST(TRE_LIBS)

unset have_tre
AS_IF([test -n "$TRE_LIBS"], [
		OLD_LIBS="$LIBS"
		LIBS="$LIBS $TRE_LIBS"
		AC_MSG_CHECKING([TRE])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <tre/regex.h>
int main(int argc, char **argv) { (void)argc; (void)argv; regcomp(0,0,REG_EXTENDED); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_tre=1
			], [AC_MSG_RESULT([no])] )
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([TRE], [test -n "$have_tre"])
AM_COND_IF([TRE], [AC_DEFINE([HAVE_TRE], 1, [TRE])])

### GDB target

unset have_gdb_target
AM_COND_IF([PTHREADS], [
		AS_IF([test "x$enable_gdb_target" != "xno"], [
			have_gdb_target=1
			]) ])

AM_CONDITIONAL([GDB], [test -n "$have_gdb_target"])
AM_COND_IF([GDB], [AC_DEFINE([WANT_GDB_TARGET], 1, [GDB target])])

### Misc minor features

AM_CONDITIONAL([KBD_TRANSLATE], [test "x$enable_kbd_translate" != "xno"])
AM_COND_IF([KBD_TRANSLATE], [AC_DEFINE([HAVE_KBD_TRANSLATE], 1, [Support keyboard translation])])

AM_CONDITIONAL([SIMULATED_NTSC], [test "x$enable_simulated_ntsc" != "xno"])
AM_COND_IF([SIMULATED_NTSC], [AC_DEFINE([WANT_SIMULATED_NTSC], 1, [Enable simulated NTSC support])])

AM_CONDITIONAL([AO_NULL], [test "x$enable_ao_null" != "xno"])
AM_COND_IF([AO_NULL], [AC_DEFINE([HAVE_NULL_AUDIO], 1, [Include null audio driver])])

AM_CONDITIONAL([FILEREQ_CLI], [test "x$enable_filereq_cli" = "xyes"])
AM_COND_IF([FILEREQ_CLI], [AC_DEFINE([HAVE_CLI], 1, [Include CLI file requester])])

AM_CONDITIONAL([LOGGING], [test "x$enable_logging" != "xno"])
AM_COND_IF([LOGGING], [AC_DEFINE([LOGGING], 1, [Logging])])

AM_CONDITIONAL([TRACE], [test "x$enable_trace" != "xno"])
AM_COND_IF([TRACE], [AC_DEFINE([TRACE], 1, [Support trace mode])])

AM_CONDITIONAL([ENABLE_SNAPSHOT], [test "x$enable_snapshot" = "xyes"])
AM_COND_IF([ENABLE_SNAPSHOT], [AC_DEFINE([ENABLE_SNAPSHOT], 1, [Snapshot build])])

# Version information for Windows resource file
test -z "$RC_VER_MAJOR" && RC_VER_MAJOR=`echo $VERSION|cut -d. -f1`
test -z "$RC_VER_MINOR" && RC_VER_MINOR=`echo $VERSION|cut -d. -f2`
test -z "$RC_REV_MAJOR" && RC_REV_MAJOR=`echo $VERSION|cut -d. -f3`
test -z "$RC_REV_MAJOR" && RC_REV_MAJOR="0"
test -z "$RC_REV_MINOR" && RC_REV_MINOR="0"
RC_VER_MAJOR=`expr $RC_VER_MAJOR + 0`
RC_VER_MINOR=`expr $RC_VER_MINOR + 0`
RC_REV_MAJOR=`expr $RC_REV_MAJOR + 0`
RC_REV_MINOR=`expr $RC_REV_MINOR + 0`
RC_VERSION="$RC_VER_MAJOR.$RC_VER_MINOR.$RC_REV_MAJOR.$RC_REV_MINOR"
RC_VERSION3="$RC_VER_MAJOR.$RC_VER_MINOR.$RC_REV_MAJOR"
if test -z "$PACKAGE_TEXT"; then
	if test "x$enable_snapshot" = "xyes"; then
		PACKAGE_TEXT="XRoar snapshot $RC_VERSION3"
	else
		PACKAGE_TEXT="XRoar $VERSION"
	fi
fi
test -z "$PACKAGE_YEAR" && PACKAGE_YEAR=`date +%Y`

AC_SUBST(RC_VER_MAJOR)
AC_SUBST(RC_VER_MINOR)
AC_SUBST(RC_REV_MAJOR)
AC_SUBST(RC_REV_MINOR)
AC_SUBST(RC_VERSION)
AC_SUBST(PACKAGE_TEXT)
AC_SUBST(PACKAGE_YEAR)
AC_DEFINE_UNQUOTED([PACKAGE_TEXT], ["$PACKAGE_TEXT"], [Visible package name])
AC_DEFINE_UNQUOTED([PACKAGE_YEAR], ["$PACKAGE_YEAR"], [Package year])
AC_DEFINE_UNQUOTED([RC_REV_MAJOR], [$RC_REV_MAJOR], [Snapshot major])
AC_DEFINE_UNQUOTED([RC_REV_MINOR], [$RC_REV_MINOR], [Snapshot minor])

### Write everything out

AC_CONFIG_FILES([Makefile
                 portalib/Makefile
                 src/Makefile
                 doc/xroar.1
                 doc/Makefile
                 tools/Makefile])
AC_OUTPUT
