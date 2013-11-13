lib_LTLIBRARIES = libwlb.la

include_HEADERS = \
	libwlb.h

libwlb_la_LIBADD = $(WAYLAND_LIBS)
libwlb_la_SOURCES = \
	fullscreen-shell-protocol.c \
	compositor.c

AM_CPPFLAGS = $(WAYLAND_CFLAGS) $(PIXMAN_CFLAGS)
AM_CFLAGS = $(GCC_CFLAGS)

BUILT_SOURCES =					\
	fullscreen-shell-server-protocol.h	\
	fullscreen-shell-protocol.c

CLEANFILES = $(BUILT_SOURCES)

wayland_protocoldir = $(top_srcdir)/protocol
include $(top_srcdir)/wayland-scanner.mk
