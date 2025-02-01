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
	auto_kbd.c auto_kbd.h \
	ay891x.c ay891x.h \
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
	hkbd.c hkbd.h \
	hkbd_joystick.c \
	joystick.c joystick.h \
	keyboard.c keyboard.h \
	logging.c logging.h \
	machine.c machine.h \
	mc10_cart.c mc10_cart.h \
	messenger.c messenger.h \
	module.c module.h \
	mos6551.c mos6551.h \
	ntsc.c ntsc.h \
	null/ui_null.c \
	part.c part.h \
	path.c path.h \
	printer.c printer.h \
	ram.c ram.h \
	rombank.c rombank.h \
	romlist.c romlist.h \
	screenshot.c screenshot.h \
	serialise.c serialise.h \
	snapshot.c snapshot.h \
	sndfile_compat.c sndfile_compat.h \
	sound.c sound.h \
	tape.c tape.h \
	tape_cas.c \
	tape_sndfile.c \
	ui.c ui.h \
	vdg_palette.c vdg_palette.h \
	vdisk.c vdisk.h \
	vdrive.c vdrive.h \
	vo.c vo.h \
	vo_render.c vo_render.h \
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
	ikon.c \
	mooh.c \
	mpi.c \
	nx32.c \
	orch90.c \
	rsdos.c \
	sn76489.c sn76489.h \
	spi65.c spi65.h \
	spi_sdcard.c \
	wd279x.c wd279x.h
endif

if CART_ARCH_MC10
xroar_SOURCES += \
	mcx128.c
endif

BUILT_SOURCES += \
	mc6847/font-6847.c mc6847/font-6847.h \
	mc6847/font-6847t1.c mc6847/font-6847t1.h \
	tcc1014/font-gime.c tcc1014/font-gime.h

xroar_SOURCES += \
	main_unix.c

EXTRA_DIST += \
	deluxecoco.c \
	dragon64.c \
	dragonpro.c \
	hkbd_lang_tables.c \
	mc6847/font-6847.c mc6847/font-6847.h \
	mc6847/font-6847t1.c mc6847/font-6847t1.h \
	mc6809/mc6809_common.c \
	mc680x/mc680x_ops.c \
	tcc1014/font-gime.c tcc1014/font-gime.h \
	vo_render_tmpl.c \
	x11/hkbd_x11_keycode_tables.h

# X11 keycode mapping tables should be distributed, but can be generated from
# files in /usr/share/X11/xkb/keycodes (xkb-data package in Debian).

XKB_TO_HK_SCANCODE = $(top_builddir)/tools/xkb_to_hk_scancode.pl

x11/hkbd_x11_keycode_tables.h:
	$(XKB_TO_HK_SCANCODE) > $@

# VDG bitmaps should be distributed, but can be generated from font image files
# if needed.

FONT2C = $(top_builddir)/tools/font2c

mc6847/font-6847.h:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --header --array font_6847 --stdint --type "uint8_t" --vdg $(srcdir)/mc6847/font-6847.png > $@

mc6847/font-6847.c:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --array font_6847 --stdint --type "uint8_t" --vdg $(srcdir)/mc6847/font-6847.png > $@

mc6847/font-6847t1.h:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --header --array font_6847t1 --stdint --type "uint8_t" --vdgt1 $(srcdir)/mc6847/font-6847t1.png > $@

mc6847/font-6847t1.c:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --array font_6847t1 --stdint --type "uint8_t" --vdgt1 $(srcdir)/mc6847/font-6847t1.png > $@

tcc1014/font-gime.h:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --header --array font_gime --stdint --type "uint8_t" --gime $(srcdir)/tcc1014/font-gime.png > $@

tcc1014/font-gime.c:
	test -f "$(FONT2C)" || make -C $(top_builddir)/tools font2c
	$(FONT2C) --array font_gime --stdint --type "uint8_t" --gime $(srcdir)/tcc1014/font-gime.png > $@

EXTRA_DIST += \
	mc6847/font-6847.png mc6847/font-6847t1.png tcc1014/font-gime.png

# ROM paths

if WASM
xroar_CFLAGS += -DROMPATH=\".\"
xroar_CFLAGS += -DCONFPATH=\".\"
else
if MINGW
xroar_CFLAGS += -DAUTOSAVE_PREFIX=\"%LOCALAPPDATA%\\\\XRoar\"
xroar_CFLAGS += -DROMPATH=\":%LOCALAPPDATA%\\\\XRoar\\\\roms:%USERPROFILE%\\\\Documents\\\\XRoar\\\\roms:%USERPROFILE%\\\\AppData\\\\Roaming\\\\XRoar\\\\roms\"
xroar_CFLAGS += -DCONFPATH=\":%LOCALAPPDATA%\\\\XRoar:%USERPROFILE%\\\\Documents\\\\XRoar:%USERPROFILE%\\\\AppData\\\\Roaming\\\\XRoar\"
else
if UI_COCOA
xroar_CFLAGS += -DAUTOSAVE_PREFIX=\"~/Library/XRoar\"
xroar_OBJCFLAGS += -DAUTOSAVE_PREFIX=\"~/Library/XRoar\"
xroar_CFLAGS += -DROMPATH=\"~/Library/XRoar/roms:~/.xroar/roms:$(datadir)/xroar/roms:\"
xroar_CFLAGS += -DCONFPATH=\"~/Library/XRoar:~/.xroar:$(sysconfdir):$(datadir)/xroar\"
else
xroar_CFLAGS += -DAUTOSAVE_PREFIX=\"~/.xroar\"
xroar_CFLAGS += -DROMPATH=\"~/.xroar/roms:$(datadir)/xroar/roms:\"
xroar_CFLAGS += -DCONFPATH=\"~/.xroar:$(sysconfdir):$(datadir)/xroar\"
endif
endif
endif

# Common X11 support

if X11
xroar_SOURCES += \
	x11/hkbd_x11.c x11/hkbd_x11.h \
	x11/hkbd_x11_keycode_tables.h
xroar_CFLAGS += $(X11_CFLAGS)
xroar_LDADD += $(X11_LIBS)
endif

# WebAssembly support

if WASM
xroar_CFLAGS += $(WASM_CFLAGS)
xroar_LDADD += $(WASM_LIBS)
xroar_SOURCES += \
	wasm/wasm.c wasm/wasm.h \
	wasm/js_wasm.c
EXTRA_xroar_DEPENDENCIES = \
	wasm/exported_functions
endif
EXTRA_DIST += wasm/exported_functions

# Common OpenGL support

if OPENGL
xroar_CFLAGS += $(GL_CFLAGS)
xroar_LDADD += $(GL_LIBS)
xroar_SOURCES += \
	vo_opengl.c vo_opengl.h
endif

# GTK+ 3 user interface

if GTK3
xroar_CFLAGS += $(GTK3_CFLAGS)
xroar_LDADD += $(GTK3_LIBS)
xroar_SOURCES += \
	gtk3/about.c \
	gtk3/common.c gtk3/common.h \
	gtk3/dialog.c gtk3/dialog.h \
	gtk3/drivecontrol.c gtk3/drivecontrol.h \
	gtk3/event_handlers.c gtk3/event_handlers.h \
	gtk3/filereq_gtk3.c \
	gtk3/gtk3.gresource.c \
	gtk3/joystick_gtk3.c \
	gtk3/printercontrol.c gtk3/printercontrol.h \
	gtk3/tapecontrol.c gtk3/tapecontrol.h \
	gtk3/ui_gtk3.c \
	gtk3/video_options.c gtk3/video_options.h \
	gtk3/vo_gtk3.c

BUILT_SOURCES += \
	gtk3/gtk3.gresource.c
endif

GTK3_RESOURCES = \
	gtk3/application.ui \
	gtk3/menu.ui \
	gtk3/drivecontrol.ui \
	gtk3/printercontrol.ui \
	gtk3/tapecontrol.ui \
	gtk3/video_options.ui

EXTRA_DIST += \
	$(GTK3_RESOURCES) \
	gtk3/gtk3.gresource.c \
	gtk3/gtk3.gresource.xml

if GLIB_COMPILE_RESOURCES
.INTERMEDIATE: gtk3/gtk3.gresource.tmp

gtk3/gtk3.gresource.tmp: $(GTK3_RESOURCES)

gtk3/gtk3.gresource.tmp: gtk3/gtk3.gresource.xml
	$(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=gtk3 --generate-source

gtk3/gtk3.gresource.c: gtk3/gtk3.gresource.tmp
	echo "#include \"top-config.h\"" > $@
	cat $< >> $@
endif

# GTK+ 2.0 user interface

if GTK2
xroar_CFLAGS += $(GTK2_CFLAGS)
xroar_LDADD += $(GTK2_LIBS)
xroar_SOURCES += \
	gtk2/about.c \
	gtk2/common.c gtk2/common.h \
	gtk2/dialog.c gtk2/dialog.h \
	gtk2/drivecontrol.c gtk2/drivecontrol.h \
	gtk2/event_handlers.c gtk2/event_handlers.h \
	gtk2/filereq_gtk2.c \
	gtk2/gtk2.gresource.c \
	gtk2/joystick_gtk2.c \
	gtk2/printercontrol.c gtk2/printercontrol.h \
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
	gtk2/printercontrol.ui \
	gtk2/tapecontrol.ui \
	gtk2/video_options.ui

EXTRA_DIST += \
	$(GTK2_RESOURCES) \
	gtk2/gtk2.gresource.c \
	gtk2/gtk2.gresource.xml

if GLIB_COMPILE_RESOURCES
.INTERMEDIATE: gtk2/gtk2.gresource.tmp

gtk2/gtk2.gresource.tmp: $(GTK2_RESOURCES)

gtk2/gtk2.gresource.tmp: gtk2/gtk2.gresource.xml
	$(GLIB_COMPILE_RESOURCES) $< --target=$@ --sourcedir=gtk2 --generate-source

gtk2/gtk2.gresource.c: gtk2/gtk2.gresource.tmp
	echo "#include \"top-config.h\"" > $@
	cat $< >> $@
endif

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
	sdl2/sdl_x11.c
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
	macosx/hkbd_darwin.c macosx/hkbd_darwin.h \
	macosx/ui_macosx.m macosx/ui_macosx.h
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
endif

# libpng support
if PNG
xroar_CFLAGS += $(PNG_CFLAGS)
xroar_LDADD += $(PNG_LIBS)
endif

if ZLIB
xroar_CFLAGS += $(ZLIB_CFLAGS)
xroar_LDADD += $(ZLIB_LIBS)
endif

# Linux evdev joysticks
if EVDEV
xroar_CFLAGS += $(EVDEV_CFLAGS)
xroar_LDADD += $(EVDEV_LIBS)
xroar_SOURCES += \
	evdev/js_evdev.c
endif

# Linux joydev joysticks
if JOYDEV
xroar_SOURCES += \
	joydev/js_joydev.c
endif

# MinGW
if MINGW
xroar_CFLAGS += $(MINGW_CFLAGS)
xroar_LDADD += $(MINGW_LIBS)
xroar_SOURCES += \
	windows32/about.c \
	windows32/common_windows32.c windows32/common_windows32.h \
	windows32/dialog.c windows32/dialog.h \
	windows32/drivecontrol.c windows32/drivecontrol.h \
	windows32/filereq_windows32.c \
	windows32/guicon.c windows32/guicon.h \
	windows32/hkbd_windows.c windows32/hkbd_windows.h \
	windows32/printercontrol.c windows32/printercontrol.h \
	windows32/resources.h \
	windows32/tapecontrol.c windows32/tapecontrol.h \
	windows32/ui_windows32.c \
	windows32/video_options.c windows32/video_options.h \
	windows32/xroar.rc

windows32/xroar.o: windows32/resources.h windows32/resources.rc

.rc.o:
	$(RC) -o $@ -O coff -DPACKAGE_TEXT="$(PACKAGE_TEXT)" -DPACKAGE_YEAR="$(PACKAGE_YEAR)" -DRC_VERSION=$(RC_VERSION) -DRC_VER_MAJOR=$(RC_VER_MAJOR) -DRC_VER_MINOR=$(RC_VER_MINOR) -DRC_REV_MAJOR=$(RC_REV_MAJOR) -DRC_REV_MINOR=$(RC_REV_MINOR) $<
.rc.obj:
	$(RC) -o $@ -O coff -DPACKAGE_TEXT="$(PACKAGE_TEXT)" -DPACKAGE_YEAR="$(PACKAGE_YEAR)" -DRC_VERSION=$(RC_VERSION) -DRC_VER_MAJOR=$(RC_VER_MAJOR) -DRC_VER_MINOR=$(RC_VER_MINOR) -DRC_REV_MAJOR=$(RC_REV_MAJOR) -DRC_REV_MINOR=$(RC_REV_MINOR) $<

endif

WIN2RC = $(top_builddir)/tools/win2rc

windows32/resources.h: windows32/resources.win
	$(WIN2RC) -o $@ -h -m XROAR_WINDOWS_RESOURCES_H_ $<

windows32/resources.rc: windows32/resources.win
	$(WIN2RC) -o $@ -r $<

BUILT_SOURCES += \
	windows32/resources.h windows32/resources.rc

EXTRA_DIST += \
	windows32/resources.win \
	windows32/resources.h windows32/resources.rc \
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
