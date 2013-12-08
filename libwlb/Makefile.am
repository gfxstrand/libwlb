lib_LTLIBRARIES = libwlb.la

include_HEADERS = \
	libwlb.h

libwlb_la_LIBADD = $(WAYLAND_LIBS) $(PIXMAN_LIBS)
libwlb_la_SOURCES =			\
	fullscreen-shell-protocol.c	\
	util.c				\
	surface.c			\
	output.c			\
	seat.c				\
	pointer.c			\
	keyboard.c			\
	pixman-renderer.c		\
	compositor.c

if ENABLE_GLES2
libwlb_la_SOURCES += glesv2-renderer.c
libwlb_la_LIBADD += $(EGL_LIBS) $(GLES2_LIBS)
endif

AM_CPPFLAGS = $(WAYLAND_CFLAGS) $(PIXMAN_CFLAGS)
AM_CFLAGS = $(GCC_CFLAGS)

BUILT_SOURCES =					\
	fullscreen-shell-server-protocol.h	\
	fullscreen-shell-protocol.c

CLEANFILES = $(BUILT_SOURCES)

wayland_protocoldir = $(top_srcdir)/protocol
include $(top_srcdir)/wayland-scanner.mk
