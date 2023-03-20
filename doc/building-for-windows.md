# Building XRoar for Windows

What's needed to cross-compile for Windows under Debian Linux (generally
"testing").

## Debian packages

 * g++-mingw-w64
 * gcc-mingw-w64
 * mingw-w64-tools
 * autogen (required to build libsndfile, not related to autogen.sh)

AFAICT that's it - the rest should come as dependencies.  You might also
want to install wine to try out the end result.

## Other packages

In general, create a subdirectory like "build-w32" or "build-w64", change into
it and run the configure line documented.  Then make, make install.

Note all these are configured to build static libraries only (so they end up in
the final binary), and that I tend to disable features that aren't required.
For this reason, you may want to maintain this environment separately in a
chroot/container/VM.

### SDL2

 * Cross-platform development library
 * https://github.com/libsdl-org/SDL
 * http://libsdl.org/

Be sure to checkout one of the 2.x release branches - the SDL developers have
started developing SDL3, and XRoar probably won't be compatible with that.

~~~

../configure --prefix=/usr/i686-w64-mingw32 --host=i686-w64-mingw32 \
    --enable-static --disable-shared \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"

../configure --prefix=/usr/x86_64-w64-mingw32 --host=x86_64-w64-mingw32 \
    --enable-static --disable-shared \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
~~~


### libsndfile

 * Audio file support
 * https://github.com/erikd/libsndfile.git
 * http://libsndfile.github.io/libsndfile/

~~~
../configure --prefix=/usr/i686-w64-mingw32 --host=i686-w64-mingw32 \
    --enable-static --disable-shared --disable-external-libs \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1" \
    ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes

../configure --prefix=/usr/x86_64-w64-mingw32 --host=x86_64-w64-mingw32 \
    --enable-static --disable-shared --disable-external-libs \
    CFLAGS="-Ofast -g" CPPFLAGS="-D__USE_MINGW_ANSI_STDIO=1" \
    ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes
~~~

### tre

 * POSIX regex matching library
 * https://github.com/laurikari/tre/

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
    --without-oss --without-pulse --without-joydev \
    CFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
    LDFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1"

ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes ./configure \
    --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32 \
    --with-sdl-prefix="/usr/x86_64-w64-mingw32" \
    --enable-filereq-cli --without-gtk2 --without-gtkgl --without-alsa \
    --without-oss --without-pulse --without-joydev \
    CFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1" \
    LDFLAGS="-std=c11 -Ofast -flto=auto -D__USE_MINGW_ANSI_STDIO=1"
~~~
