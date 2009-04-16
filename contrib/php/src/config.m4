dnl $Id: config.m4,v 2.3 2006/10/17 08:36:36 cgkang Exp $
dnl config.m4 for extension cubrid
dnl don't forget to call PHP_EXTENSION(cubrid)

dnl If your extension references something external, use with:

PHP_ARG_WITH(cubrid, for cubrid support,
dnl Make sure that the comment is aligned:
[  --with-cubrid             Include CUBRID support])

if test "$PHP_CUBRID" != "no"; then
  dnl Action..
  PHP_ADD_INCLUDE("$CUBRID/include")
  PHP_ADD_LIBRARY_WITH_PATH(cascci, "$CUBRID/lib64", CUBRID_SHARED_LIBADD)
  PHP_SUBST_OLD(CUBRID_SHARED_LIBADD)
  PHP_EXTENSION(cubrid, $ext_shared)
fi
