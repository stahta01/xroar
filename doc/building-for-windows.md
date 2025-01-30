# Building XRoar for Windows

What's needed to cross-compile for Windows under Debian Linux (generally
"testing").

## Debian packages

 * g++-mingw-w64
 * gcc-mingw-w64
 * mingw-w64-tools

AFAICT that's it - the rest should come as dependencies.  You might also
want to install wine to try out the end result.

## Other packages

In general, create a subdirectory like "build-w32" or "build-w64", change into
it and run the configure line documented.  Then make, make install.

Note all these are configured to build static libraries only (so they end up in
the final binary), and that I tend to disable features that aren't required.
For this reason, you may want to maintain this environment separately in a
chroot/container/VM.

The references to \_\_USE\_MINGW\_ANSI\_STDIO prevented some weird linking
errors, and may not be necessary any more.

### SDL2

 * Cross-platform development library
 * https://github.com/libsdl-org/SDL
 * http://libsdl.org/

Be sure to checkout one of the 2.x release branches.  XRoar will not build
against SDL3 yet.

~~~

../configure --prefix=/usr/i686-w64-mingw32 --host=i686-w64-mingw32 \
    --enable-static --disable-shared \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"

../configure --prefix=/usr/x86_64-w64-mingw32 --host=x86_64-w64-mingw32 \
    --enable-static --disable-shared \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
~~~

### tre

 * POSIX regex matching library
 * https://github.com/laurikari/tre/

I've been using version 0.8.0; it seems possible that later versions break
something.

~~~
../configure --prefix=/usr/i686-w64-mingw32 --host=i686-w64-mingw32 \
    --enable-static --disable-shared \
    --disable-agrep --disable-approx \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"

../configure --prefix /usr/x86_64-w64-mingw32 --host=x86_64-w64-mingw32 \
    --enable-static --disable-shared \
    --disable-agrep --disable-approx \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
~~~

## Building XRoar

I have scripts to do all this for me, but here's the configure lines I end up
using.  Note that I explicitly disable all the stuff not needed under Windows.
Also, this is a link-time-optimised build: if you're debugging, you might want
to change CFLAGS/LDFLAGS.

~~~
ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes ../configure \
    --host=i686-w64-mingw32 --prefix=/usr/i686-w64-mingw32 \
    --with-sdl-prefix="/usr/i686-w64-mingw32" \
    --enable-filereq-cli --without-gtk2 --without-gtkgl --without-alsa \
    --without-oss --without-pulse --without-sndfile --without-joydev \
    CFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
    LDFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1"

ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes ../configure \
    --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32 \
    --with-sdl-prefix="/usr/x86_64-w64-mingw32" \
    --enable-filereq-cli --without-gtk2 --without-gtkgl --without-alsa \
    --without-oss --without-pulse --without-sndfile --without-joydev \
    CFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
    LDFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1"
~~~
