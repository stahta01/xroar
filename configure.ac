#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([xroar], [0.33])
AM_INIT_AUTOMAKE([foreign])

MANPAGE_DATE="January 2015"
AC_SUBST(MANPAGE_DATE)

AC_CONFIG_SRCDIR([src/xroar.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_OBJC
#AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_RANLIB

AN_MAKEVAR([RC], [AC_PROG_RC])
AN_PROGRAM([windres], [AC_PROG_RC])
AC_CHECK_TOOL(RC, windres, :)

# Checks for libraries.

# Checks for header files.
AC_FUNC_ALLOCA
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h endian.h fcntl.h limits.h malloc.h netdb.h stdint.h stdlib.h string.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_HEADER_TIME
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STAT
AC_FUNC_STRTOD
AC_CHECK_FUNCS([atexit gettimeofday memset putenv setenv socket strchr strcspn strerror strrchr strsep strtol strtoul])

### Options

AC_ARG_ENABLE([snapshot],[])

AC_ARG_WITH([gtk2],
	AS_HELP_STRING([--without-gtk2], [don't test for GTK+-2]) )

AC_ARG_WITH([gtkgl],
	AS_HELP_STRING([--without-gtkgl], [don't test for GtkGLExt]) )
test "x$with_gtk2" = "xno" && with_gtkgl=no

AC_ARG_WITH([sdl],
	AS_HELP_STRING([--without-sdl], [don't test for SDL]) )

AC_ARG_WITH([sdlgl],
	AS_HELP_STRING([--without-sdlgl], [don't test for SDL OpenGL support]) )
test "x$with_sdl" = "xno" && with_sdlgl=no

AC_ARG_WITH([cocoa],
	AS_HELP_STRING([--without-cocoa], [don't test for Mac OS X Cocoa]) )
test "x$with_sdl" = "xno" && with_cocoa=no

AC_ARG_ENABLE([filereq_cli],
	AS_HELP_STRING([--enable-filereq-cli], [include command-line file requester]) )

AC_ARG_WITH([alsa],
	AS_HELP_STRING([--without-alsa], [don't test for ALSA audio]) )

AC_ARG_WITH([oss],
	AS_HELP_STRING([--without-oss], [don't test for OSS audio]) )

AC_ARG_WITH([pulse],
	AS_HELP_STRING([--without-pulse], [don't test for PulseAudio]) )

AC_ARG_WITH([sunaudio],
	AS_HELP_STRING([--with-sunaudio], [test for Sun audio]) )

AC_ARG_WITH([coreaudio],
	AS_HELP_STRING([--without-coreaudio], [don't test for Mac OS X Core Audio]) )

AC_ARG_WITH([jack],
	AS_HELP_STRING([--with-jack], [test for JACK audio]) )

AC_ARG_ENABLE([ao_null],
	AS_HELP_STRING([--disable-ao-null], [don't build null audio driver]) )

AC_ARG_WITH([sndfile],
	AS_HELP_STRING([--without-sndfile], [don't test for libsndfile]) )

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
int main(int argc, char **argv) { (void)crc32(0L, NULL, 0); (void)gzopen("", ""); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_zlib=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([ZLIB], [test -n "$have_zlib"])
AM_COND_IF([ZLIB], [AC_DEFINE([HAVE_ZLIB], 1, [Have zlib])])

### POSIX threads

AS_IF([test "x$with_pthreads" != "xno"], [
		AM_COND_IF([MINGW], [
			PTHREADS_LIBS="-Wl,-Bstatic -lpthread -Wl,-Bdynamic"
			], [
			PTHREADS_LIBS="-pthread"
			]) ])
AC_SUBST(PTHREADS_LIBS)

unset have_pthreads
AS_IF([test -n "PTHREADS_LIBS"], [
		OLD_LIBS="$LIBS"
		LIBS="$LIBS $PTHREADS_LIBS"
		AC_MSG_CHECKING([POSIX threads])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <pthread.h>
int main(int argc, char **argv) { pthread_create(0,0,0,0); return 0; }
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
		AC_CHECK_HEADER([alsa/asoundlib.h], [
			AC_CHECK_LIB([asound], [snd_pcm_open], [
				ALSA_LIBS="-lasound"
				], :) ]) ])
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
int main(int argc, char **argv) { pa_simple *p = pa_simple_new(0,0,0,0,0,0,0,0,0); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_pulse=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([AO_PULSE], [test -n "$have_pulse"])
AM_COND_IF([AO_PULSE], [AC_DEFINE([HAVE_PULSE], 1, [Have PulseAudio])])

### Sun audio

AS_IF([test "x$with_sunaudio" = "xyes"], [
		SUN_AUDIO_CFLAGS=""
		SUN_AUDIO_LIBS="-lrt"
		])
AC_SUBST(SUN_AUDIO_CFLAGS)
AC_SUBST(SUN_AUDIO_LIBS)

unset have_sunaudio
AS_IF([test -n "$SUN_AUDIO_LIBS"], [
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $SUN_AUDIO_CFLAGS"
		LIBS="$LIBS $SUN_AUDIO_LIBS"
		AC_MSG_CHECKING([if Sun audio is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <stdlib.h>
#include <sys/audioio.h>
#include <sys/stropts.h>
#include <time.h>
int main(int argc, char **argv) { audio_info_t d; int t = AUDIO_ENCODING_LINEAR; t = I_FLUSH; nanosleep(NULL,NULL); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_sunaudio=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
	 ])

AM_CONDITIONAL([AO_SUN], [test -n "$have_sunaudio"])
AM_COND_IF([AO_SUN], [AC_DEFINE([HAVE_SUN_AUDIO], 1, [Have Sun Audio])])

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
int main(int argc, char **argv) { jack_client_t *c; jack_activate(c); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_jack=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([AO_JACK], [test -n "$JACK_LIBS"])
AM_COND_IF([AO_JACK], [AC_DEFINE([HAVE_JACK_AUDIO], 1, [Use JACK audio])])

### OpenGL

AX_CHECK_GL
AS_IF([test -z "$GL_LIBS"], [
		OLD_LIBS="$LIBS"
		LIBS="$LIBS -framework OpenGL"
		AC_MSG_CHECKING([if -framework OpenGL works instead])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <OpenGL/gl.h>
int main(int argc, char **argv) { GLuint t; glGenTextures(1, &t); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			GL_LIBS="-framework OpenGL"
			], [AC_MSG_RESULT([no])] )
		LIBS="$OLD_LIBS"
		])
AM_CONDITIONAL([OPENGL], [test -n "$GL_LIBS"])

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

### Linux joysticks

unset have_linux_joystick
AC_CHECK_HEADER([linux/joystick.h], [have_linux_joystick=1], :)

AM_CONDITIONAL([LINUX_JOYSTICK], [test -n "$have_linux_joystick"])
AM_COND_IF([LINUX_JOYSTICK], [AC_DEFINE([HAVE_LINUX_JOYSTICK], 1, [Have linux/joystick.h])])

### SDL

AS_IF([test "x$with_sdl" != "xno"], [
		AM_PATH_SDL([1.2.0], :) ], :)

unset have_sdl
AS_IF([test -n "$SDL_LIBS"], [
		OLD_CFLAGS="$CFLAGS"
		OLD_LIBS="$LIBS"
		CFLAGS="$CFLAGS $SDL_CFLAGS"
		LIBS="$LIBS $SDL_LIBS"
		AC_MSG_CHECKING([if SDL is usable])
		AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <SDL.h>
#include <SDL_syswm.h>
int main(int argc, char **argv) { SDL_Init(0); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_sdl=1
			], [AC_MSG_RESULT([no])] )
		CFLAGS="$OLD_CFLAGS"
		LIBS="$OLD_LIBS"
		])

AM_CONDITIONAL([UI_SDL], [test -n "$have_sdl"])
AM_COND_IF([UI_SDL], [AC_DEFINE([HAVE_SDL], 1, [Have SDL])])

unset have_sdlgl
AM_COND_IF([UI_SDL], [AM_COND_IF([OPENGL], [
		AS_IF([test "x$with_sdlgl" != "xno"], [
			OLD_CFLAGS="$CFLAGS"
			OLD_LIBS="$LIBS"
			CFLAGS="$CFLAGS $SDL_CFLAGS $GL_CFLAGS"
			LIBS="$LIBS $SDL_LIBS $GL_LIBS"
			AC_MSG_CHECKING([for OpenGL support in SDL])
			AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <SDL.h>
#include <SDL_opengl.h>
int main(int argc, char **argv) { SDL_GL_SetAttribute(0, 5); glGenTextures(0,0); }
					])], [
				AC_MSG_RESULT([yes])
				have_sdlgl=1
				], [AC_MSG_RESULT([no])] )
			CFLAGS="$OLD_CFLAGS"
			LIBS="$OLD_LIBS"
			]) ]) ])

AM_CONDITIONAL([VO_SDLGL], [test -n "$have_sdlgl"])
AM_COND_IF([VO_SDLGL], [AC_DEFINE([HAVE_SDLGL], 1, [Have SDL OpenGL support])])

### Mac OS X Cocoa

unset have_cocoa
if test "x$ac_cv_objc_compiler_gnu" = "xyes"; then
	AM_COND_IF([UI_SDL], [AS_IF([test "x$with_cocoa" != "xno"], [
		    COCOA_LIBS="-framework Cocoa"
		    AC_LANG_PUSH([Objective C])
		    AC_CHECK_HEADER([Cocoa/Cocoa.h], [
				     have_cocoa=1
				     ], :)
		    AC_LANG_POP([Objective C]) ]) ])
fi
AC_SUBST(COCOA_CFLAGS)
AC_SUBST(COCOA_LIBS)

AM_CONDITIONAL([UI_COCOA], [test -n "$have_cocoa"])
AM_COND_IF([UI_COCOA], [AC_DEFINE([HAVE_COCOA], 1, [Have Mac OS X Cocoa framework])])

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
int main(int argc, char **argv) { SNDFILE *f; SF_INFO i; f = sf_open("", SFM_READ, &i); return 0; }
				])], [
			AC_MSG_RESULT([yes])
			have_sndfile=1
			], [AC_MSG_RESULT([no])] )
	CFLAGS="$OLD_CFLAGS"
	LIBS="$OLD_LIBS"
	])

AM_CONDITIONAL([SNDFILE], [test -n "$have_sndfile"])
AM_COND_IF([SNDFILE], [AC_DEFINE([HAVE_SNDFILE], 1, [Use libsndfile])])

### GDB target

unset have_gdb_target
AM_COND_IF([PTHREADS], [
		AS_IF([test "x$enable_gdb_target" != "xno"], [
			have_gdb_target=1
			]) ])

AM_CONDITIONAL([GDB], [test -n "$have_gdb_target"])
AM_COND_IF([GDB], [AC_DEFINE([WANT_GDB_TARGET], 1, [GDB target])])

### Misc minor features

AM_CONDITIONAL([AO_NULL], [test "x$enable_ao_null" != "xno"])
AM_COND_IF([AO_NULL], [AC_DEFINE([HAVE_NULL_AUDIO], 1, [Include null audio driver])])

AM_CONDITIONAL([FILEREQ_CLI], [test "x$enable_filereq_cli" = "xyes"])
AM_COND_IF([FILEREQ_CLI], [AC_DEFINE([HAVE_CLI], 1, [Include CLI file requester])])

AM_CONDITIONAL([LOGGING], [test "x$enable_logging" != "xno"])
AM_COND_IF([LOGGING], [AC_DEFINE([LOGGING], 1, [Logging])])

AM_CONDITIONAL([TRACE], [test "x$enable_trace" != "xno"])
AM_COND_IF([TRACE], [AC_DEFINE([TRACE], 1, [Support trace mode])])

# Version information for Windows resource file
RC_VER_MAJOR=`echo $VERSION|cut -d. -f1`
RC_VER_MINOR=`echo $VERSION|cut -d. -f2`
test -z "$RC_REV_MAJOR" && RC_REV_MAJOR=`date +%Y`
if test -z "$RC_REV_MINOR"; then
	tmp_m=`date +%m`
	tmp_d=`date +%d`
	tmp_H=`date +%H`
	tmp_M=`date +%M`
	RC_REV_MINOR=`expr $tmp_m \* 4096 + $tmp_d \* 128 + $tmp_H \* 4 + $tmp_M / 15`
fi
RC_VER_MAJOR=`expr $RC_VER_MAJOR + 0`
RC_VER_MINOR=`expr $RC_VER_MINOR + 0`
RC_REV_MAJOR=`expr $RC_REV_MAJOR + 0`
RC_REV_MINOR=`expr $RC_REV_MINOR + 0`
RC_VERSION="$RC_VER_MAJOR.$RC_VER_MINOR.$RC_REV_MAJOR.$RC_REV_MINOR"
AC_SUBST(RC_VER_MAJOR)
AC_SUBST(RC_VER_MINOR)
AC_SUBST(RC_REV_MAJOR)
AC_SUBST(RC_REV_MINOR)
AC_SUBST(RC_VERSION)
AC_DEFINE_UNQUOTED([RC_REV_MAJOR], [$RC_REV_MAJOR], [Snapshot major])
AC_DEFINE_UNQUOTED([RC_REV_MINOR], [$RC_REV_MINOR], [Snapshot minor])

AM_CONDITIONAL([ENABLE_SNAPSHOT], [test "x$enable_snapshot" = "xyes"])
AM_COND_IF([ENABLE_SNAPSHOT], [AC_DEFINE([ENABLE_SNAPSHOT], 1, [Snapshot build])])

### Write everything out

AC_CONFIG_FILES([Makefile
                 portalib/Makefile
                 src/Makefile
                 doc/xroar.1
                 doc/Makefile
                 tools/Makefile])
AC_OUTPUT
