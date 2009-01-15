
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#

AC_PREREQ(2.59)
AC_INIT([CUBRID 2008], RELEASE, , cubrid)
AC_CONFIG_SRCDIR([src/executables/server.c])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([CUBRID 2008], RELEASE)

# Checks for programs.
remember_CFLAGS="$CFLAGS"
remember_CXXFLAGS="$CXXFLAGS"
remember_LDFLAGS="$LDFLAGS"
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_PROG_INSTALL
AC_PROG_YACC
AC_PROG_LEX
CFLAGS="$remeber_CFLAGS"
CXXFLAGS="$remember_CXXFLAGS"
LDFLAGS="$remember_LDFLAGS"

# Checks bit model feature.
AC_ARG_ENABLE([64bit],
	[AS_HELP_STRING([--enable-64bit],
		[build 64 bit code @<:@default=no (32 bit code)@:>@])],
	[bit_model=$enableval],
	[bit_model=no])

if test "$bit_model" = "yes"
then
	BIT_MODEL="-m64"
else
	BIT_MODEL="-m32"
	CC="$CC -m32"
	CXX="$CXX -m32"
fi
AM_CONDITIONAL([BUILD_64], [test "$bit_model" = "yes"])

# System type
SYSTEM_TYPE="$host_vendor-$host_os"
MACHINE_TYPE="$host_cpu"
export BIT_MODEL SYSTEM_TYPE MACHINE_TYPE CC CXX

case $SYSTEM_TYPE in
	*linux*) SYS_DEFS="-DGCC -DLINUX -DI386"
		 SYS_LIBS="" ;;
	*)       SYS_DEFS=""
		 SYS_LIBS="" ;;
esac

# Checks for libraries.
AC_CHECK_LIB([m], [main])
#AC_CHECK_LIB([rt], [main])
AC_CHECK_LIB([dl], [main])
#AC_CHECK_LIB([elf], [main])
#AC_CHECK_LIB([iberty], [main])
#AC_CHECK_LIB([bfd], [main])
AC_CHECK_LIB([pthread], [main])
AC_CHECK_LIB([curses], [main])
AC_CHECK_LIB([stdc++], [main])

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_HEADER_STAT
AC_HEADER_TIME
AC_CHECK_HEADERS([limits.h],
	[],
	[AC_DEFINE([PATH_MAX], [512], [Max path length])])
AC_CHECK_HEADERS([limits.h],
	[],
	[AC_DEFINE([NAME_MAX], [255], [Max file name length])])
AC_CHECK_HEADERS([sys/param.h sys/socket.h nl_types.h regex.h getopt.h libgen.h])

# Checks for typedefs, structures, and compiler characteristics.
#AC_TYPE_INT8_T
#AC_TYPE_INT16_T
#AC_TYPE_INT32_T
#AC_TYPE_INT64_T
#AC_TYPE_INTMAX_T
#AC_TYPE_INTPTR_T
#AC_TYPE_UINT8_T
#AC_TYPE_UINT16_T
#AC_TYPE_UINT32_T
#AC_TYPE_UINT64_T
#AC_TYPE_UINTMAX_T
#AC_TYPE_UINTPTR_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_PID_T
AC_CHECK_SIZEOF([char])
AC_CHECK_SIZEOF([short])
AC_CHECK_SIZEOF([int])
AC_CHECK_SIZEOF([long])
AC_CHECK_SIZEOF([long long])
AC_CHECK_SIZEOF([void *])
AC_CHECK_TYPES([long long])
AC_CHECK_TYPES([byte_t, int8_t, int16_t, int32_t, int64_t, intmax_t, intptr_t,
		uint8_t, uint16_t, uint32_t, uint64_t, uintmax_t, uintptr_t])
remember_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -D_LARGEFILE64_SOURCE"
AC_CHECK_TYPES([off64_t], [SYS_DEFS="$SYS_DEFS -D_LARGEFILE64_SOURCE"])
CFLAGS="$remember_CFLAGS"

AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_VPRINTF

# Checks for system functions for which we have replacements.
AC_REPLACE_FUNCS([vasprintf asprintf strdup strlcpy strlcat \
		  getopt getopt_long dirname basename])

# Checks system services
AC_SYS_LARGEFILE

# Checks optional features.
AC_ARG_ENABLE([coverage],
	[AS_HELP_STRING([--enable-coverage],
		[build as code coverage mode @<:@default=no@:>@])],
	[enable_coverage=$enableval],
	[enable_coverage=no])

if test "$enable_coverage" = "yes"
then
	CFLAGS="$CFLAGS -fprofile-arcs -ftest-coverage"
	CXXFLAGS="$CXXFLAGS -fprofile-arcs -ftest-coverage"
	LDFLAGS="$LDFLAGS -lgcov"
fi

AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug],
		[build as debug mode @<:@default=no (release mode)@:>@])],
	[enable_debug=$enableval],
	[enable_debug=no])

if test "$enable_debug" = "yes"
then
	CFLAGS="$CFLAGS -ggdb -fno-inline"
	CXXFLAGS="$CXXFLAGS -ggdb -fno-inline"
else
	CFLAGS="$CFLAGS -ggdb -O2 -DNDEBUG -finline-functions"
	CXXFLAGS="$CXXFLAGS -ggdb -O2 -DNDEBUG -finline-functions"
fi
AM_CONDITIONAL([BUILD_DEBUG], [test "$enable_debug" = "yes"])

AC_ARG_ENABLE([profile],
	[AS_HELP_STRING([--enable-profile],
		[build as profile mode @<:@default=no@:>@])],
	[enable_profile=$enableval],
	[enable_profile=no])

if test "$enable_profile" = "yes"
then
	CFLAGS="$CFLAGS -g -pg"
	CXXFLAGS="$CXXFLAGS -g -pg"
fi

AC_ARG_ENABLE([hoard],
	[AS_HELP_STRING([--enable-hoard],
		[enable to use Hoard as memory allocator @<:@default=no (C library)@:>@])],
	[enable_hoard=$enableval],
	[enable_hoard=no])

if test "$enable_hoard" = "yes"
then
	AC_CONFIG_SUBDIRS([external/hoard-371])
	SYS_DEFS="$SYS_DEFS -DHOARD"
	SYS_LIBS="$SYS_LIBS -L\$(top_builddir)/external/lib -lhoard -lstdc++ -ldl -lpthread"
fi

AC_ARG_WITH([libedit],
	AS_HELP_STRING([--with-libedit=PATH],
		[prefix for installed editline @<:@default=builtin@:>@]),
	[with_libedit=$withval],
	[with_libedit=builtin])

if test "$with_libedit" = "builtin"
then
	AC_CONFIG_SUBDIRS([external/libedit-20070831-2.10])
	LIBEDIT_LIBS="\$(top_builddir)/external/lib/libedit.la"
	LIBEDIT_INC="-I\$(top_builddir)/external/include/editline"
else
	LIBEDIT_LIBS="-ledit"
fi

AC_ARG_WITH([lzo2],
	AS_HELP_STRING([--with-lzo2=PATH],
		[prefix for installed LZO2 @<:@default=builtin@:>@]),
	[with_lzo2=$withval],
	[with_lzo2=builtin])

if test "$with_lzo2" = "builtin"
then
	AC_CONFIG_SUBDIRS([external/lzo-2.03])
	LZO_LIBS="\$(top_builddir)/external/lib/liblzo2.la"
	LZO_INC="-I\$(top_builddir)/external/include/lzo"
        LDFLAGS="$LDFLAGS $LZO_LIBS"
else
	LZO_LIBS="-llzo2"
        LZO_INC="-I$with_lzo2/include/lzo"
fi

AC_ARG_WITH([pcre],
	AS_HELP_STRING([--with-pcre=PATH],
		[prefix for installed PCRE @<:@default=builtin@:>@]),
	[with_pcre=$withval],
	[with_pcre=builtin])

if test "$with_pcre" = "builtin"
then
	AC_CONFIG_SUBDIRS([external/pcre-7.6])
	PCRE_LIBS="\$(top_builddir)/external/lib/libpcre.la"
else
	PCRE_LIBS="-lpcre"
fi

AC_ARG_WITH([gc],
	AS_HELP_STRING([--with-gc=PATH],
		[prefix for installed GC @<:@default=builtin@:>@]),
	[with_gc=$withval],
	[with_gc=builtin])

if test "$with_gc" = "builtin"
then
	AC_CONFIG_SUBDIRS([external/gc6.7])
	GC_LIBS="\$(top_builddir)/external/lib/libgc.la"
else
	GC_LIBS="-lgc"
fi

AC_ARG_WITH([openssl],
	AS_HELP_STRING([--with-openssl=PATH],
		[prefix for installed OpenSSL @<:@default=builtin@:>@]),
	[with_openssl=$withval],
	[with_openssl=builtin])

if test "$with_openssl" = "builtin"
then
	AC_CONFIG_SUBDIRS([external/openssl-0.9.8g])
	OPENSSL_LIBS="\$(top_builddir)/external/lib/libssl.a \$(top_builddir)/external/lib/libcrypto.a"
else
	OPENSSL_LIBS="-lssl -lcrypto"
fi

#AC_ARG_WITH([expat],
#	AS_HELP_STRING([--with-expat=PATH],
#		[prefix for installed Expat @<:@default=builtin@:>@]),
#	[with_expat=$withval],
#	[with_expat=builtin])
#
#if test "$with_expat" = "builtin"
#then
#	AC_CONFIG_SUBDIRS([external/expat-2.0.1])
#	EXPAT_LIBS="\$(top_builddir)/external/lib/libexpat.la"
#else
#	EXPAT_LIBS="-lexpat"
#fi

#AC_ARG_WITH([xmlrpc-epi],
#	AS_HELP_STRING([--with-xmlrpc-epi=PATH],
#		[prefix for installed XMLRPC-EPI @<:@default=builtin@:>@]),
#	[with_xmlrpc_epi=$withval],
#	[with_xmlrpc_epi=builtin])
#
#if test "$with_xmlrpc_epi" = "builtin"
#then
#	AC_CONFIG_SUBDIRS([external/xmlrpc-epi-0.54])
#	XMLRPCEPI_LIBS="\$(top_builddir)/external/lib/libxmlrpc-epi.la"
#else
#	XMLRPCEPI_LIBS="-lxmlrpc-epi -lexpat"
#fi


#
AC_PREFIX_DEFAULT("$HOME/cubrid")

BUILD_NUMBER=`cat $ac_confdir/BUILD_NUMBER`
RELEASE_STRING=`echo $BUILD_NUMBER | sed -e "s|\.[[0-9]]*$||"`
MAJOR_RELEASE_STRING=`echo $RELEASE_STRING | sed -e "s|\.[[0-9]]*$||"`
MAJOR_VERSION=`echo $RELEASE_STRING | cut -d \. -f 1`
MINOR_VERSION=`echo $RELEASE_STRING | cut -d \. -f 2`
PATCH_VERSION=`echo $RELEASE_STRING | cut -d \. -f 3`
#AC_SUBST(RELEASE_STRING)
#AC_SUBST(MAJOR_RELEASE_STRING)
#AC_SUBST(BUILD_NUMBER)
AC_SUBST(LT_VERSION,$MAJOR_VERSION:$MINOR_VERSION:$PATCH_VERSION)

SERVER_DEFS="-DSERVER_MODE"
CS_DEFS="-DCS_MODE"
SA_DEFS="-DSA_MODE"
COMMON_DEFS="-DANSI=1 -DSYSV -DMAXPATHLEN=1024 -D_REENTRANT"
OTHER_DEFS="-DYY_PREFIX=qp_yy"
VERSION_DEFS="-DMAJOR_VERSION=$MAJOR_VERSION -DMINOR_VERSION=$MINOR_VERSION \
-DPATCH_VERSION=$PATCH_VERSION -DRELEASE_STRING=$RELEASE_STRING \
-DMAJOR_RELEASE_STRING=$MAJOR_RELEASE_STRING -DBUILD_NUMBER=$BUILD_NUMBER"

SRC_INC="-I\$(top_srcdir)/include"
DIR_LIST=`ls -l $ac_confdir/src | grep ^d | awk '{ print $9}'`
for D in $DIR_LIST
do
	SRC_INC="$SRC_INC -I\$(top_srcdir)/src/$D"
done

CUBRID_LIB="\$(top_builddir)/cubrid/libcubrid.la"
CS_LIB="\$(top_builddir)/cs/libcubridcs.la"
SA_LIB="\$(top_builddir)/sa/libcubridsa.la"
CAS_LIB="-L\$(top_builddir)/cas -lcas"
CCI_LIB="\$(top_builddir)/cas/libcascci.la"
ESQL_LIB="\$(top_builddir)/util/libcubridesql.la"

EXT_LIBS="-L\$(top_builddir)/external/lib $LIBEDIT_LIBS $LZO_LIBS $OPENSSL_LIBS $GC_LIBS $XMLRPCEPI_LIBS $EXPAT_LIBS"
EXT_INC="-I\$(top_builddir)/external/include $LIBEDIT_INC $LZO_INC"

AC_SUBST([BUILD_NUMBER])
AC_SUBST([SYS_DEFS])
AC_SUBST([SYS_LIBS])
AC_SUBST([LIBEDIT_LIBS])
AC_SUBST([LIBEDIT_INC])
AC_SUBST([LZO_LIBS])
AC_SUBST([LZO_INC])
AC_SUBST([PCRE_LIBS])
AC_SUBST([GC_LIBS])
AC_SUBST([OPENSSL_LIBS])
AC_SUBST([XMLRPCEPI_LIBS])
AC_SUBST([EXT_LIBS])
AC_SUBST([EXT_INC])
AC_SUBST([SERVER_DEFS])
AC_SUBST([CS_DEFS])
AC_SUBST([SA_DEFS])
AC_SUBST([COMMON_DEFS])
AC_SUBST([OTHER_DEFS])
AC_SUBST([VERSION_DEFS])
AC_SUBST([SRC_INC])
AC_SUBST([CUBRID_LIB])
AC_SUBST([CS_LIB])
AC_SUBST([SA_LIB])
AC_SUBST([CAS_LIB])
AC_SUBST([CCI_LIB])
AC_SUBST([ESQL_LIB])

AH_TOP([
#ifndef _CONFIG_H_
#define _CONFIG_H_
])
AH_BOTTOM([
#include "system.h"
#endif /* _CONFIG_H_ */
])

AC_CONFIG_FILES([Makefile
	cas/Makefile
	broker/Makefile
	cmserver/Makefile
	cmserver/conf/Makefile
	pccts/Makefile
	pccts/antlrd/Makefile
	pccts/dlg/Makefile
	include/Makefile
	sa/Makefile
	cubrid/Makefile
	cs/Makefile
	util/Makefile
	conf/Makefile
	msg/Makefile
	msg/en_US/Makefile
	msg/ko_KR.euckr/Makefile
	msg/ko_KR.utf8/Makefile
	demo/Makefile
	jdbc/Makefile
	java/Makefile
	cubridmanager/Makefile
	])

AC_OUTPUT
