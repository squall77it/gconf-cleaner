dnl Process this file with autoconf to produce a configure script.
AC_INIT([gconf-cleaner], 0.0.2, [at@gclab.org])

. ./requires

AM_INIT_AUTOMAKE($PACKAGE_NAME, $PACKAGE_VERSION)
AM_MAINTAINER_MODE
AM_CONFIG_HEADER(config.h)

AC_PROG_LIBTOOL
AC_PROG_INTLTOOL

AC_PROG_CC

dnl ======================================================================
dnl set CFLAGS
dnl ======================================================================
changequote(,)dnl
if test "x$GCC" = "xyes"; then
	case " $CFLAGS " in
	*[\ \	]-Wall[\ \	]*) ;;
	*) CFLAGS="$CFLAGS -Wall"   ;;
	esac

	if test "x$enable_maintainer_mode" = "xyes"; then
		case " $CFLAGS " in
		*[\ \	]-Werror[\ \	]*) ;;
		*) CFLAGS="$CFLAGS -Werror" ;;
		esac
		CFLAGS="`echo $CFLAGS | sed -e 's/-O./-O0/'`"
	fi

	if test "x$enable_ansi" = "xyes"; then
		case " $CFLAGS " in
		*[\ \	]-ansi[\ \	]*) ;;
		*) CFLAGS="$CFLAGS -ansi"   ;;
		esac

		case " $CFLAGS " in
		*[\ \	]-pedantic[\ \	]*)   ;;
		*) CFLAGS="$CFLAGS -pedantic" ;;
		esac
	fi
fi
changequote([,])dnl

dnl ======================================================================
dnl functions testing
dnl ======================================================================

dnl ======================================================================
dnl gettext stuff
dnl ======================================================================
GETTEXT_PACKAGE=$PACKAGE
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Gettext package])

ALL_LINGUAS="ja"
AM_GLIB_GNU_GETTEXT
AM_GLIB_DEFINE_LOCALEDIR(GCLEANER_LOCALEDIR)

dnl ======================================================================
dnl check pkg-config stuff
dnl ======================================================================
PKG_CHECK_MODULES(GCLEANER, glib-2.0 gtk+-2.0 >= $GTK_REQUIRED gconf-2.0)
AC_SUBST(GCLEANER_CFLAGS)
AC_SUBST(GCLEANER_LIBS)

dnl ======================================================================
dnl options
dnl ======================================================================

dnl ======================================================================
dnl output
dnl ======================================================================
AC_CONFIG_FILES([
Makefile
po/Makefile.in
src/Makefile
])
AC_OUTPUT

dnl ======================================================================
dnl result
dnl ======================================================================
echo ""
echo "========== Build Information =========="
echo " CFLAGS:                     $GCLEANER_CFLAGS"
echo " LIBS:                       $GCLEANER_LIBS"
echo ""
