dnl config.m4 for extension cubrid
dnl don't forget to call PHP_EXTENSION(cubrid)

dnl If your extension references something external, use with:

PHP_ARG_WITH(cubrid, for cubrid support,
dnl Make sure that the comment is aligned:
[  --with-cubrid             Include CUBRID support])

dnl Otherwise use enable:

dnl PHP_ARG_ENABLE(cubrid, whether to enable cubrid support,
dnl Make sure that the comment is aligned:
dnl [  --enable-cubrid           Enable cubrid support])

if test "$PHP_CUBRID" != "no"; then
  dnl Action..
  PHP_ADD_INCLUDE("../cci")
  PHP_ADD_LIBRARY_WITH_PATH(cascci, "../cci", CUBRID_SHARED_LIBADD)
  PHP_SUBST_OLD(CUBRID_SHARED_LIBADD)
  PHP_EXTENSION(cubrid, $ext_shared)
fi
