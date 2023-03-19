AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS =
EXTRA_DIST =
AM_CPPFLAGS = -I$(top_builddir)

bin_PROGRAMS += xroar

BUILT_SOURCES =

# Common sources

xroar_CFLAGS =
xroar_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/portalib
xroar_OBJCFLAGS =
xroar_LDADD = $(top_builddir)/portalib/libporta.a -lm

xroar_SOURCES = \
	ao.c ao.h \
	becker.c becker.h \
	blockdev.c blockdev.h \
	breakpoint.c breakpoint.h \
	cart.c cart.h \
	colourspace.c colourspace.h \
	crc16.c crc16.h \
	crc32.c crc32.h \
	crclist.c crclist.h \
	debug_cpu.h \
	dkbd.c dkbd.h \
	events.c events.h \
	filter.c filter.h \
	fs.c fs.h \
	hexs19.c hexs19.h \
	joystick.c joystick.h \
	keyboard.c keyboard.h \
	logging.c logging.h \
	machine.c machine.h \
	module.c module.h \
	ntsc.c ntsc.h \
	null/ui_null.c \
	null/vo_null.c \
	part.c part.h \
	path.c path.h \
	printer.c printer.h \
	romlist.c romlist.h \
	serialise.c serialise.h \
	snapshot.c snapshot.h \
	sound.c sound.h \
	tape.c tape.h \
	tape_cas.c \
	ui.c ui.h \
	vdg_palette.c vdg_palette.h \
	vdisk.c vdisk.h \
	vdrive.c vdrive.h \
	vo.c vo.h \
	xconfig.c xconfig.h \
	xroar.c xroar.h

if MACHINE_ARCH_DRAGON
xroar_SOURCES += \
	dragon.c
endif

if MACHINE_ARCH_COCO3
xroar_SOURCES += \
	coco3.c
endif

if MACHINE_ARCH_MC10
xroar_SOURCES += \
	mc10.c
endif

if PART_MC6801
xroar_SOURCES += \
	mc6801/mc6801.c mc6801/mc6801.h
endif

if PART_MC6809
xroar_SOURCES += \
	mc6809/hd6309.c mc6809/hd6309.h \
	mc6809/mc6809.c mc6809/mc6809.h
endif

if PART_MC6821
xroar_SOURCES += \
	mc6821.c mc6821.h
endif

if PART_MC6847
xroar_SOURCES += \
	mc6847/font-6847.c mc6847/font-6847.h \
	mc6847/font-6847t1.c mc6847/font-6847t1.h \
	mc6847/mc6847.c mc6847/mc6847.h
endif

if PART_MC6883
xroar_SOURCES += \
	mc6883.c mc6883.h
endif

if PART_TCC1014
xroar_SOURCES += \
	tcc1014/font-gime.c tcc1014/font-gime.h \
	tcc1014/tcc1014.c tcc1014/tcc1014.h
endif

if CART_ARCH_DRAGON
xroar_SOURCES += \
	deltados.c \
	dragondos.c \
	gmc.c \
	ide.c ide.h \
	idecart.c idecart.h \
	mooh.c \
	mpi.c mpi.h \
	nx32.c \
	orch90.c \
	rsdos.c \
	sn76489.c sn76489.h \
	spi65.c spi65.h \
	spi_sdcard.c \
	wd279x.c wd279x.h
endif

BUILT_SOURCES += \
	mc6847/font-6847.c mc6847/font-6847.h \
	mc6847/font-6847t1.c mc6847/font-6847t1.h \
	tcc1014/font-gime.c tcc1014/font-gime.h

xroar_SOURCES += \
	main_unix.c

EXTRA_DIST += \
	mc6847/font-6847.c mc6847/font-6847.h \
	mc6847/font-6847t1.c mc6847/font-6847t1.h \
	mc6809/mc6809_common.c \
	mc680x/mc680x_ops.c \
	tcc1014/font-gime.c tcc1014/font-gime.h \
	vo_generic_ops.c

# VDG bitmaps should be distributed, but can be generated from font image files
# if needed.

FONT2C = $(top_builddir)/tools/font2c

mc6847/font-6847.h:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --header --array font_6847 --type "unsigned char" --vdg $(srcdir)/mc6847/font-6847.png > $@

mc6847/font-6847.c:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --array font_6847 --type "unsigned char" --vdg $(srcdir)/mc6847/font-6847.png > $@

mc6847/font-6847t1.h:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --header --array font_6847t1 --type "unsigned char" --vdgt1 $(srcdir)/mc6847/font-6847t1.png > $@

mc6847/font-6847t1.c:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --array font_6847t1 --type "unsigned char" --vdgt1 $(srcdir)/mc6847/font-6847t1.png > $@

tcc1014/font-gime.h:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --header --array font_gime --type "unsigned char" --gime $(srcdir)/tcc1014/font-gime.png > $@

tcc1014/font-gime.c:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --array font_gime --type "unsigned char" --gime $(srcdir)/tcc1014/font-gime.png > $@

EXTRA_DIST += \
	mc6847/font-6847.png mc6847/font-6847t1.png tcc1014/font-gime.png

# ROM paths

if WASM
xroar_CFLAGS += -DROMPATH=\".\"
xroar_CFLAGS += -DCONFPATH=\".\"
else
if MINGW
xroar_CFLAGS += -DROMPATH=\":~\\\\Documents\\\\XRoar\\\\roms:~\\\\AppData\\\\Local\\\\XRoar\\\\roms:~\\\\AppData\\\\Roaming\\\\XRoar\\\\roms\"
xroar_CFLAGS += -DCONFPATH=\":~\\\\Documents\\\\XRoar:~\\\\AppData\\\\Local\\\\XRoar:~\\\\AppData\\\\Roaming\\\\XRoar\"
else
if UI_COCOA
xroar_CFLAGS += -DROMPATH=\"~/Library/XRoar/roms:~/.xroar/roms:$(datadir)/xroar/roms:\"
xroar_CFLAGS += -DCONFPATH=\"~/Library/XRoar:~/.xroar:$(sysconfdir):$(datadir)/xroar\"
else
xroar_CFLAGS += -DROMPATH=\"~/.xroar/roms:$(datadir)/xroar/roms:\"
xroar_CFLAGS += -DCONFPATH=\"~/.xroar:$(sysconfdir):$(datadir)/xroar\"
endif
endif
endif

if WASM
xroar_CFLAGS += $(WASM_CFLAGS)
xroar_LDADD += $(WASM_LIBS)
xroar_SOURCES += \
	wasm/wasm.c wasm/wasm.h
EXTRA_xroar_DEPENDENCIES = \
	wasm/exported_functions
endif
EXTRA_DIST += wasm/exported_functions

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
	gtk2/gtk2.gresource.c \
	gtk2/joystick_gtk2.c \
	gtk2/keyboard_gtk2.c \
	gtk2/tapecontrol.c gtk2/tapecontrol.h \
	gtk2/ui_gtk2.c \
	gtk2/video_options.c gtk2/video_options.h

BUILT_SOURCES += \
	gtk2/gtk2.gresource.c

if GTKGL
xroar_CFLAGS += $(GTKGL_CFLAGS)
xroar_LDADD += $(GTKGL_LIBS)
xroar_SOURCES += \
	gtk2/vo_gtkgl.c
endif
endif

GTK2_RESOURCES = \
	gtk2/application.ui \
	gtk2/menu.ui \
	gtk2/drivecontrol.ui \
	gtk2/tapecontrol.ui \
	gtk2/video_options.ui

EXTRA_DIST += \
	$(GTK2_RESOURCES) \
	gtk2/gtk2.gresource.c \
	gtk2/gtk2.gresource.xml \
	gtk2/keyboard_gtk2_mappings.c

.INTERMEDIATE: gtk2/gtk2.gresource.tmp

gtk2/gtk2.gresource.tmp: $(GTK2_RESOURCES)

gtk2/gtk2.gresource.tmp: gtk2/gtk2.gresource.xml
	$(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=gtk2 --generate-source

gtk2/gtk2.gresource.c: gtk2/gtk2.gresource.tmp
	echo "#include \"top-config.h\"" > $@
	cat $< >> $@

# SDL2 user interface

if UI_SDL2

xroar_CFLAGS += $(SDL_CFLAGS)
xroar_LDADD += $(SDL_LIBS)
xroar_SOURCES += \
	sdl2/ao_sdl2.c \
	sdl2/common.c sdl2/common.h \
	sdl2/joystick_sdl2.c \
	sdl2/keyboard_sdl2.c \
	sdl2/ui_sdl2.c \
	sdl2/vo_sdl2.c

if X11
xroar_SOURCES += \
	sdl2/sdl_x11.c \
	sdl2/sdl_x11_keyboard.c \
	sdl2/sdl_x11_keycode_tables.h
xroar_CFLAGS += $(X11_CFLAGS)
xroar_LDADD += $(X11_LIBS)
endif

if MINGW
xroar_SOURCES += \
	sdl2/sdl_windows32_keyboard.c \
	sdl2/sdl_windows32_vsc_table.h
endif

endif

# Mac OS X special case SDL user interface

if UI_SDL2
if UI_COCOA
xroar_CFLAGS += $(COCOA_CFLAGS)
xroar_OBJCFLAGS += $(COCOA_CFLAGS) $(SDL_CFLAGS)
xroar_LDADD += $(COCOA_LIBS)
xroar_SOURCES += \
	macosx/filereq_cocoa.m \
	macosx/ui_macosx.m
xroar_SOURCES += \
	sdl2/sdl_cocoa_keyboard.c
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
	windows32/common_windows32.c windows32/common_windows32.h \
	windows32/dialogs.h \
	windows32/filereq_windows32.c \
	windows32/guicon.c windows32/guicon.h \
	windows32/ui_windows32.c \
	windows32/video_options.c windows32/video_options.h \
	windows32/xroar.rc

windows32/xroar.o: windows32/dialogs.h windows32/dialogs.rc

.rc.o:
	$(RC) -o $@ -O coff -DPACKAGE_TEXT="$(PACKAGE_TEXT)" -DPACKAGE_YEAR="$(PACKAGE_YEAR)" -DRC_VERSION=$(RC_VERSION) -DRC_VER_MAJOR=$(RC_VER_MAJOR) -DRC_VER_MINOR=$(RC_VER_MINOR) -DRC_REV_MAJOR=$(RC_REV_MAJOR) -DRC_REV_MINOR=$(RC_REV_MINOR) $<
.rc.obj:
	$(RC) -o $@ -O coff -DPACKAGE_TEXT="$(PACKAGE_TEXT)" -DPACKAGE_YEAR="$(PACKAGE_YEAR)" -DRC_VERSION=$(RC_VERSION) -DRC_VER_MAJOR=$(RC_VER_MAJOR) -DRC_VER_MINOR=$(RC_VER_MINOR) -DRC_REV_MAJOR=$(RC_REV_MAJOR) -DRC_REV_MINOR=$(RC_REV_MINOR) $<

endif

WIN2RC = $(top_builddir)/tools/win2rc

windows32/dialogs.h: windows32/dialogs.win
	$(WIN2RC) -h -m XROAR_WINDOWS_DIALOGS_H_ $< > $@

windows32/dialogs.rc: windows32/dialogs.win
	$(WIN2RC) -r $< > $@

BUILT_SOURCES += \
	windows32/dialogs.h windows32/dialogs.rc

EXTRA_DIST += \
	windows32/dialogs.h windows32/dialogs.rc \
	windows32/xroar-16x16.ico \
	windows32/xroar-32x32.ico \
	windows32/xroar-48x48.ico \
	windows32/xroar-256x256.ico

# Trace mode support
if TRACE
xroar_SOURCES += \
	mc6801/mc6801_trace.c mc6801/mc6801_trace.h \
	mc6809/mc6809_trace.c mc6809/mc6809_trace.h \
	mc6809/hd6309_trace.c mc6809/hd6309_trace.h
endif

if PTHREADS

xroar_CFLAGS += $(PTHREADS_CFLAGS)
xroar_LDADD += $(PTHREADS_LIBS)

if GDB
xroar_SOURCES += \
	gdb.c gdb.h
endif

endif

if TRE
xroar_LDADD += $(TRE_LIBS)
endif

if FILEREQ_CLI
xroar_SOURCES += \
	filereq_cli.c
endif

clean-local:
	rm -f xroar.wasm
