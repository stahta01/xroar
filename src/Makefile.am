AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS =
EXTRA_DIST =
AM_CPPFLAGS = -I$(top_builddir)

bin_PROGRAMS += xroar

BUILT_SOURCES =

# Common sources

xroar_CFLAGS =
xroar_CPPFLAGS = -I$(top_srcdir)/portalib
xroar_OBJCFLAGS =
xroar_LDADD = $(top_builddir)/portalib/libporta.a -lm

xroar_SOURCES = \
	becker.c becker.h \
	breakpoint.c breakpoint.h \
	cart.c cart.h \
	crc16.c crc16.h \
	crc32.c crc32.h \
	crclist.c crclist.h \
	deltados.c deltados.h \
	dkbd.c dkbd.h \
	dragondos.c dragondos.h \
	events.c events.h \
	font-6847.c font-6847.h \
	font-6847t1.c font-6847t1.h \
	fs.c fs.h \
	hd6309.c hd6309.h \
	hexs19.c hexs19.h \
	joystick.c joystick.h \
	keyboard.c keyboard.h \
	logging.c logging.h \
	machine.c machine.h \
	mc6809.c mc6809.h \
	mc6821.c mc6821.h \
	mc6847.c mc6847.h \
	module.c module.h \
	mpi.c mpi.h \
	orch90.c orch90.h \
	path.c path.h \
	printer.c printer.h \
	romlist.c romlist.h \
	rsdos.c rsdos.h \
	sam.c sam.h \
	snapshot.c snapshot.h \
	sound.c sound.h \
	tape.c tape.h \
	tape_cas.c \
	ui.c ui.h \
	vdg_palette.c vdg_palette.h \
	vdisk.c vdisk.h \
	vdrive.c vdrive.h \
	wd279x.c wd279x.h \
	xconfig.c xconfig.h \
	xroar.c xroar.h \
	null/ui_null.c \
	null/vo_null.c

BUILT_SOURCES += \
	font-6847.c font-6847.h \
	font-6847t1.c font-6847t1.h

xroar_SOURCES += \
	main_unix.c

EXTRA_DIST += \
	font-6847.c font-6847.h \
	font-6847t1.c font-6847t1.h \
	mc6809_common.c mc6809_common.h \
	vo_generic_ops.c

# VDG bitmaps should be distributed, but can be generated from font image files
# if needed.

FONT2C = $(top_builddir)/tools/font2c

font-6847.h:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --header --array font_6847 --type "unsigned char" --vdg $(srcdir)/font-6847.png > $@

font-6847.c:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --array font_6847 --type "unsigned char" --vdg $(srcdir)/font-6847.png > $@

font-6847t1.h:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --header --array font_6847t1 --type "unsigned char" --vdgt1 $(srcdir)/font-6847t1.png > $@

font-6847t1.c:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --array font_6847t1 --type "unsigned char" --vdgt1 $(srcdir)/font-6847t1.png > $@

EXTRA_DIST += \
	font-6847.png font-6847t1.png

# ROM paths

if MINGW
xroar_CFLAGS += -DROMPATH=\":~/Local\ Settings/Application\ Data/XRoar/roms:~/Application\ Data/XRoar/roms\"
xroar_CFLAGS += -DCONFPATH=\":~/Local\ Settings/Application\ Data/XRoar:~/Application\ Data/XRoar\"
else
if UI_COCOA
xroar_CFLAGS += -DROMPATH=\"~/Library/XRoar/roms:~/.xroar/roms:$(datadir)/xroar/roms:\"
xroar_CFLAGS += -DCONFPATH=\"~/Library/XRoar:~/.xroar:$(sysconfdir):$(datadir)/xroar\"
else
xroar_CFLAGS += -DROMPATH=\"~/.xroar/roms:$(datadir)/xroar/roms:\"
xroar_CFLAGS += -DCONFPATH=\"~/.xroar:$(sysconfdir):$(datadir)/xroar\"
endif
endif

if ZLIB
xroar_CFLAGS += $(ZLIB_CFLAGS)
xroar_LDADD += $(ZLIB_LIBS)
endif

# Common OpenGL support

if OPENGL
xroar_CFLAGS += $(GL_CFLAGS)
xroar_LDADD += $(GL_LIBS)
xroar_SOURCES += \
	vo_opengl.c vo_opengl.h
endif

# GTK+-2.0 user interface

if GTK2
xroar_CFLAGS += $(GTK2_CFLAGS)
xroar_LDADD += $(GTK2_LIBS)
xroar_SOURCES += \
	gtk2/common.c gtk2/common.h \
	gtk2/drivecontrol.c gtk2/drivecontrol.h \
	gtk2/filereq_gtk2.c \
	gtk2/keyboard_gtk2.c \
	gtk2/tapecontrol.c gtk2/tapecontrol.h \
	gtk2/ui_gtk2.c gtk2/ui_gtk2.h

BUILT_SOURCES += \
	gtk2/drivecontrol_glade.h \
	gtk2/tapecontrol_glade.h \
	gtk2/top_window_glade.h

if GTKGL
xroar_CFLAGS += $(GTKGL_CFLAGS)
xroar_LDADD += $(GTKGL_LIBS)
xroar_SOURCES += \
	gtk2/vo_gtkgl.c
endif
endif

gtk2/drivecontrol.$(OBJEXT): gtk2/drivecontrol_glade.h
gtk2/tapecontrol.$(OBJEXT): gtk2/tapecontrol_glade.h
gtk2/ui_gtk2.$(OBJEXT): gtk2/top_window_glade.h

EXTRA_DIST += \
	gtk2/drivecontrol_glade.glade \
	gtk2/tapecontrol_glade.glade \
	gtk2/top_window_glade.glade \
	gtk2/drivecontrol_glade.h \
	gtk2/tapecontrol_glade.h \
	gtk2/top_window_glade.h \
	gtk2/keyboard_gtk2_mappings.c

.glade.h:
	echo "static const gchar *$(@:%.h=%) =" | sed 's/\*.*\//\*/'> $@
	$(SED) 's/"/'\''/g;s/^\( *\)/\1"/;s/$$/"/;' $< >> $@
	echo ";" >> $@

# SDL2 user interface

if UI_SDL2

xroar_CFLAGS += $(SDL_CFLAGS)
xroar_LDADD += $(SDL_LIBS)
xroar_SOURCES += \
	sdl2/common.c sdl2/common.h \
	sdl2/keyboard_sdl2.c \
	sdl2/ui_sdl2.c \
	sdl2/vo_sdl2.c

else

# SDL user interface - incompatible with SDL2

if UI_SDL
xroar_CFLAGS += $(SDL_CFLAGS)
xroar_LDADD += $(SDL_LIBS)
xroar_SOURCES += \
	sdl/ao_sdl.c \
	sdl/common.c sdl/common.h \
	sdl/joystick_sdl.c \
	sdl/keyboard_sdl.c \
	sdl/ui_sdl.c \
	sdl/vo_sdl.c \
	sdl/vo_sdlyuv.c
endif

if VO_SDLGL
xroar_SOURCES += \
	sdl/vo_sdlgl.c
endif

EXTRA_DIST += \
	sdl/keyboard_sdl_mappings.c

endif

# Mac OS X special case SDL user interface

if UI_SDL
if UI_COCOA
xroar_CFLAGS += $(COCOA_CFLAGS)
xroar_OBJCFLAGS += $(COCOA_CFLAGS) $(SDL_CFLAGS)
xroar_LDADD += $(COCOA_LIBS)
xroar_SOURCES += \
	macosx/filereq_cocoa.m \
	macosx/ui_macosx.m
endif
endif

EXTRA_DIST += \
	macosx/Info.plist.in \
	macosx/xroar.icns

# ALSA audio
if AO_ALSA
xroar_CFLAGS += $(ALSA_CFLAGS)
xroar_LDADD += $(ALSA_LIBS)
xroar_SOURCES += \
	alsa/ao_alsa.c
endif

# OSS audio
if AO_OSS
xroar_CFLAGS += $(OSS_CFLAGS)
xroar_LDADD += $(OSS_LIBS)
xroar_SOURCES += \
	oss/ao_oss.c
endif

# PulseAudio
if AO_PULSE
xroar_CFLAGS += $(PULSE_CFLAGS)
xroar_LDADD += $(PULSE_LIBS)
xroar_SOURCES += \
	pulseaudio/ao_pulse.c
endif

# Sun audio
if AO_SUN
xroar_CFLAGS += $(SUN_AUDIO_CFLAGS)
xroar_LDADD += $(SUN_AUDIO_LIBS)
xroar_SOURCES += \
	sunos/ao_sun.c
endif

# Mac OS X CoreAudio
if AO_COREAUDIO
xroar_CFLAGS += $(COREAUDIO_CFLAGS)
xroar_LDADD += $(COREAUDIO_LIBS)
xroar_SOURCES += \
	macosx/ao_macosx.c
endif

# JACK audio driver
if AO_JACK
xroar_CFLAGS += $(JACK_CFLAGS)
xroar_LDADD += $(JACK_LIBS)
xroar_SOURCES += \
	jack/ao_jack.c
endif

# Null audio driver
if AO_NULL
xroar_SOURCES += \
	null/ao_null.c
endif

# libsndfile support
if SNDFILE
xroar_CFLAGS += $(SNDFILE_CFLAGS)
xroar_LDADD += $(SNDFILE_LIBS)
xroar_SOURCES += \
	tape_sndfile.c
endif

# Linux joysticks
if LINUX_JOYSTICK
xroar_SOURCES += \
	linux/joystick_linux.c
endif

# MinGW
if MINGW
xroar_CFLAGS += $(MINGW_CFLAGS)
xroar_LDADD += $(MINGW_LIBS)
xroar_SOURCES += \
	windows32/ao_windows32.c \
	windows32/common_windows32.c windows32/common_windows32.h \
	windows32/filereq_windows32.c \
	windows32/ui_windows32.c \
	windows32/xroar.rc

.rc.o:
	$(RC) -o $@ -O coff -DRC_VERSION=$(RC_VERSION) -DRC_VER_MAJOR=$(RC_VER_MAJOR) -DRC_VER_MINOR=$(RC_VER_MINOR) -DRC_REV_MAJOR=$(RC_REV_MAJOR) -DRC_REV_MINOR=$(RC_REV_MINOR) $<
.rc.obj:
	$(RC) -o $@ -O coff -DRC_VERSION=$(RC_VERSION) -DRC_VER_MAJOR=$(RC_VER_MAJOR) -DRC_VER_MINOR=$(RC_VER_MINOR) -DRC_REV_MAJOR=$(RC_REV_MAJOR) -DRC_REV_MINOR=$(RC_REV_MINOR) $<

endif

EXTRA_DIST += \
	windows32/xroar-16x16.ico \
	windows32/xroar-32x32.ico \
	windows32/xroar-48x48.ico

# Trace mode support
if TRACE
xroar_SOURCES += \
	mc6809_trace.c mc6809_trace.h \
	hd6309_trace.c hd6309_trace.h
endif

if PTHREADS
xroar_CFLAGS += $(PTHREADS_CFLAGS)
xroar_LDADD += $(PTHREADS_LIBS)

if GDB
xroar_SOURCES += \
	gdb.c gdb.h
endif

if FILEREQ_CLI
xroar_SOURCES += \
	filereq_cli.c
endif

endif
