AC_PREREQ([2.64])
AC_INIT([wlbackends], [0.0.1])

# AC_CONFIG_SRCDIR([libwlb/libwlb.h])
# AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11 foreign])

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
