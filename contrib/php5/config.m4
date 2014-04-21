dnl $Id: config.m4,v 2.3 2006/10/17 08:36:36 cgkang Exp $
dnl config.m4 for extension cubrid
dnl don't forget to call PHP_EXTENSION(cubrid)

dnl If your extension references something external, use with:

PHP_ARG_WITH(cubrid, for CUBRID support,
[  --with-cubrid[=DIR]     Include CUBRID support. DIR is the CUBRID base install directory.])

if test "$PHP_CUBRID" != "no"; then

  CUBRID_INCDIR="$CUBRID/include"
  CUBRID_LIBDIR="$CUBRID/lib"

  if test "$PHP_CUBRID" != "" && test "$PHP_CUBRID" != "yes"; then
    CUBRID_INCDIR="$PHP_CUBRID/include"
    CUBRID_LIBDIR="$PHP_CUBRID/lib"
  fi

  if ! test -r "$CUBRID_INCDIR/cas_cci.h"; then
    AC_MSG_ERROR([$CUBRID_INCDIR/cas_cci.h Please set CUBRID base install dir with --with-cubrid[=DIR].])
  fi

  PHP_CHECK_LIBRARY("cascci", cci_init, [], [
  AC_MSG_ERROR([$CUBRID_LIBDIR/libcascci.so Please set CUBRID base install dir with --with-cubrid[=DIR].])
  ], [
  -L$CUBRID_LIBDIR
  ])

  dnl Action..
  PHP_ADD_INCLUDE("$CUBRID_INCDIR")
  PHP_ADD_LIBRARY_WITH_PATH(cascci, "$CUBRID_LIBDIR", CUBRID_SHARED_LIBADD)
  PHP_SUBST(CUBRID_SHARED_LIBADD)
  PHP_NEW_EXTENSION(cubrid, php_cubrid.c, $ext_shared)
fi
