# ================= initialization =================== #
AC_INIT([Audio Recorder], [1.9.4], [https://bugs.launchpad.net/~audio-recorder], [audio-recorder])
AM_INIT_AUTOMAKE

AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_SRCDIR([src/main.c])
#AC_CONFIG_HEADERS([src/config.h])
AM_MAINTAINER_MODE

# ============== basic compiler settings ============= #
AC_PROG_CC
AC_HEADER_STDC

# *************************
# Checks for programs.
# *************************
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# ---- compile with -Wall -----
# CFLAGS="$CFLAGS -Wall -Wextra"
CFLAGS="$CFLAGS -Wall"

# ---- lib math (-lm) -----
# LDFLAGS="$LDFLAGS -lm"
# Edit: See the MATH_LIB variable below.

# ========== export compiler / linker options ======== #
AC_SUBST(CFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(LIBS)

# Math library -lm is required by Fedora during linking. Ubuntu can do with or without it.
# But Ubuntu fails to link if -lm is in the beginning of linking list, therefore we add -lm at the end.
# See src/Makefile.am
MATH_LIB=-lm
AC_SUBST(MATH_LIB)

# ============== look for dependencies =============== #
# check for headers needed for standard interfaces
AC_CHECK_HEADERS(
	stdlib.h \
	string.h \ 
	stdio.h \
	unistd.h
)   

GLIB_REQUIRED=2.14.0
GTK_REQUIRED=3.0.0
GTHREAD_REQUIRED=2.0.0
# Notice: requires gstreamer 1.4
GSTREAMER_REQUIRED=1.4
APPINDICATOR_REQUIRED=0.3

PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_REQUIRED)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GIO, gio-2.0 >= $GLIB_REQUIRED)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(GTK, gtk+-3.0 >= $GTK_REQUIRED)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(GSTREAMER, gstreamer-1.0 >= $GSTREAMER_REQUIRED)
AC_SUBST(GSTREAMER_CFLAGS)
AC_SUBST(GSTREAMER_LIBS)

# Automatic installation of gstreamer plugins
PKG_CHECK_MODULES(GSTREAMER_PBUTILS, gstreamer-pbutils-1.0 >= $GSTREAMER_REQUIRED)
AC_SUBST(GSTREAMER_PBUTILS_CFLAGS)
AC_SUBST(GSTREAMER_PBUTILS_LIBS)

PKG_CHECK_MODULES(GTHREAD,  gthread-2.0 >= $GTHREAD_REQUIRED)
AC_SUBST(GTHREAD_CFLAGS)
AC_SUBST(GTHREAD_LIBS)

PKG_CHECK_MODULES(DBUS, dbus-1 >= 0.8)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

# Support AppIndicator?
# Ref: https://launchpad.net/indicator-application

PKG_CHECK_MODULES(APP_INDICATOR, appindicator3-0.1 >= $APPINDICATOR_REQUIRED)
AC_SUBST(APP_INDICATOR_CFLAGS)
AC_SUBST(APP_INDICATOR_LIBS)

# *************************
# Checks for library functions.
# *************************
AC_FUNC_STAT
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([exp pow])

# *************************
# Auto-set ALL_LINGUAS variable. See audio-recorder/po/*.po files.
# *************************
ALL_LINGUAS=`(cd "$srcdir/po" > /dev/null && ls *.po) | cut -d. -f1 | xargs`

GETTEXT_PACKAGE=audio-recorder

AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

# For po/Makefile
# Ref: https://bugs.launchpad.net/ubuntu/+source/gettext/+bug/122343
top_builddir=`pwd`
AC_SUBST(top_builddir)

IT_PROG_INTLTOOL

# Support for GSetting schemas
GLIB_GSETTINGS

# Change default from /usr/local to /usr 
AC_PREFIX_DEFAULT(/usr)

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    pixmaps/Makefile
    icons/Makefile
    icons/hicolor/Makefile
    icons/hicolor/scalable/Makefile
    icons/hicolor/scalable/apps/Makefile
    icons/hicolor/scalable/status/Makefile
    data/Makefile
    po/Makefile.in
    po/Makefile])

AC_OUTPUT



