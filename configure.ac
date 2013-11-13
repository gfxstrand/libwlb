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

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT32_T
AC_TYPE_UINT32_T

# Checks for library functions.

AC_CONFIG_FILES([
	Makefile
	libwlb/Makefile])
AC_OUTPUT
