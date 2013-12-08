AC_PREREQ([2.64])
AC_INIT([wlbackends], [0.0.1])

# AC_CONFIG_SRCDIR([libwlb/libwlb.h])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11 foreign])
AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_CC

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT([disable-static])

if test "x$GCC" = "xyes"; then
	GCC_CFLAGS="-Wall -Wextra -Wno-unused-parameter \
		-Wno-missing-field-initializers -g -fvisibility=hidden \
		-Wstrict-prototypes -Wmissing-prototypes"
fi
AC_SUBST(GCC_CFLAGS)

# Checks for libraries.
PKG_PROG_PKG_CONFIG()

AC_CHECK_FUNCS([mkostemp posix_fallocate])

AC_ARG_ENABLE(gles2, [  --disable-gles2],,
              enable_gles2=yes)
AM_CONDITIONAL(ENABLE_GLES2, test x$enable_gles2 = xyes)
if test x$enable_gles2 = xyes; then
	AC_DEFINE([ENABLE_GLES2], [1], [Build libwlb with GLES2 support])
	PKG_CHECK_MODULES(GLES2, [glesv2])
fi

AC_ARG_ENABLE(egl, [  --disable-egl],,
              enable_egl=yes)
AM_CONDITIONAL(ENABLE_EGL, test x$enable_egl = xyes)
if test x$enable_egl = xyes; then
	AC_DEFINE([ENABLE_EGL], [1], [Build libwlb with EGL support])
	PKG_CHECK_MODULES(EGL, [egl >= 7.10])
fi

PKG_CHECK_MODULES(WAYLAND, [wayland-server])
PKG_CHECK_MODULES(PIXMAN, [pixman-1])

AC_PATH_PROG([wayland_scanner], [wayland-scanner])
if test x$wayland_scanner = x; then
	AC_MSG_ERROR([wayland-scanner is needed to compile weston])
fi

AC_ARG_ENABLE(x11-backend, [  --enable-x11-backend],,
	      enable_x11_backend=yes)
AM_CONDITIONAL(ENABLE_X11_BACKEND, test x$enable_x11_backend = xyes)
if test x$enable_x11_backend = xyes; then
  X11_BACKEND_MODULES="xcb x11 x11-xcb xcb-shm"
  X11_BACKEND_MODULES="$X11_BACKEND_MODULES xkbcommon >= 0.3.0"

  PKG_CHECK_MODULES(X11_BACKEND_XKB, [xcb-xkb],
		    [have_xcb_xkb="yes"], [have_xcb_xkb="no"])
  if test "x$have_xcb_xkb" = xyes; then
	# Most versions of XCB have totally broken XKB bindings, where the
	# events don't work.  Make sure we can actually use them.
	xcb_xkb_save_CFLAGS=$CFLAGS
	CFLAGS=$X11_BACKEND_XKB_CFLAGS
	AC_CHECK_MEMBER([struct xcb_xkb_state_notify_event_t.xkbType],
			[], [have_xcb_xkb=no], [[#include <xcb/xkb.h>]])
	CFLAGS=$xcb_xkb_save_CFLAGS
  fi
  if test "x$have_xcb_xkb" = xyes; then
	X11_BACKEND_MODULES="$X11_BACKEND_MODULES xcb-xkb"
	AC_DEFINE([HAVE_XCB_XKB], [1], [libxcb supports XKB protocol])
  fi

  PKG_CHECK_MODULES(X11_BACKEND, [$X11_BACKEND_MODULES])
fi

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT32_T
AC_TYPE_UINT32_T

# Checks for library functions.

AC_CONFIG_FILES([
	Makefile
	libwlb/Makefile
	Xwlb/Makefile])
AC_OUTPUT
