bin_PROGRAMS = font2c scandump scandump_windows

font2c_CFLAGS = $(SDL_CFLAGS)
font2c_LDADD = $(SDL_LIBS) $(SDL2_IMAGE_LIBS)
font2c_SOURCES = font2c.c

scandump_CFLAGS = -I$(top_srcdir)/portalib -I$(top_srcdir)/src $(SDL_CFLAGS)
scandump_LDFLAGS = $(SDL_LIBS)
scandump_SOURCES = scandump.c scancodes_darwin.h scancodes_xfree86.h

scandump_windows_CFLAGS = -I$(top_srcdir)/portalib -I$(top_srcdir)/src $(SDL_CFLAGS)
scandump_windows_LDFLAGS = $(SDL_LIBS)
scandump_windows_SOURCES = scandump_windows.c scancodes_windows.h
