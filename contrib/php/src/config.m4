dnl $Id: config.m4,v 2.3 2006/10/17 08:36:36 cgkang Exp $
dnl config.m4 for extension cubrid
dnl don't forget to call PHP_EXTENSION(cubrid)

dnl If your extension references something external, use with:

PHP_ARG_WITH(cubrid, for cubrid support,
AC_HELP_STRING([--with-cubrid],[Include CUBRID support]))

PHP_ARG_WITH(cubrid-includedir, for cubrid include path,
AC_HELP_STRING([--with-cubrid-includedir=PATH], [Include CUBRID library path]),no,no)

PHP_ARG_WITH(cubrid-libdir, for cubrid library path,
AC_HELP_STRING([--with-cubrid-libdir=PATH],[Include CUBRID include path]),no,no)

PHP_ARG_ENABLE(64bit, for cubrid 64 bit code,
AC_HELP_STRING([--enable-64bit],[build 64 bit module @<:@default=no@:>@]),no,no)

if test "$PHP_CUBRID" != "no"; then

  CUBRID_INCDIR="$CUBRID/include"
  CUBRID_LIBDIR="$CUBRID/lib"

  if test "$PHP_CUBRID_INCLUDEDIR" != "no"; then
    CUBRID_INCDIR="$PHP_CUBRID_INCLUDEDIR"
  fi

  if ! test -r "$CUBRID_INCDIR/cas_cci.h"; then
    AC_MSG_ERROR([$CUBRID_INC/cas_cci.h Try adding --with-cubrid-includedir=PATH. Please check config.log for more information.])
  fi

  if test "$PHP_CUBRID_LIBDIR" != "no"; then
    CUBRID_LIBDIR="$PHP_CUBRID_LIBDIR"
  fi

  CFLAGS="$CFLAGS -Wall -Wextra -Wno-unused"

  if test "$PHP_64BIT" != "no"; then
	CFLAGS="$CFLAGS -m64"
  else
	CFLAGS="$CFLAGS -m32"
  fi

  PHP_CHECK_LIBRARY("cascci", cci_init, [], [
  AC_MSG_ERROR([Try adding --with-cubrid-libdir=PATH. Please check config.log for more information.])
  ], [
  -L$CUBRID_LIBDIR
  ])

  dnl Action..
  PHP_ADD_INCLUDE("$CUBRID_INCDIR")
  PHP_ADD_LIBRARY_WITH_PATH(cascci, "$CUBRID_LIBDIR", CUBRID_SHARED_LIBADD)
  PHP_SUBST_OLD(CUBRID_SHARED_LIBADD)
  PHP_EXTENSION(cubrid, $ext_shared)
fi
