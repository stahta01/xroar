bin_PROGRAMS = font2c scandump

font2c_CFLAGS = `sdl-config --cflags`
font2c_LDFLAGS = `sdl-config --libs` -lSDL_image
font2c_SOURCES = font2c.c

scandump_CFLAGS = -I$(top_srcdir)/portalib -I$(top_srcdir)/src `sdl2-config --cflags`
scandump_LDFLAGS = `sdl2-config --libs`
scandump_SOURCES = scandump.c scancodes_darwin.h  scancodes_xfree86.h
