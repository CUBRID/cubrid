#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([libregex38a], [3.8a])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([regfree.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([limits.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

CFLAGS_OLD="$CFLAGS"
CFLAGS="-D_LARGEFILE64_SOURCE"
AC_CHECK_TYPES([off64_t], [SYS_DEF="-D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"])
CFLAGS="$CFLAGS_OLD $SYS_DEF"

# Checks for library functions.
AC_FUNC_MEMCMP
AC_CHECK_FUNCS([memmove memset])

AC_CONFIG_FILES([Makefile include/Makefile])
AC_OUTPUT
