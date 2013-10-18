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
#  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
#

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#

AC_PREREQ(2.59)
AC_INIT([CUBRID],[BUILD_NUMBER_STRING],[],[cubrid])
AC_CONFIG_SRCDIR([src/base/error_code.h])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_HEADERS([config.h])

# check system type
AC_CANONICAL_SYSTEM
SYSTEM_TYPE="$host_vendor-$host_os"
MACHINE_TYPE="$host_cpu"
export SYSTEM_TYPE MACHINE_TYPE

AM_INIT_AUTOMAKE([1.9 tar-ustar])

# Checks for programs.
remember_CFLAGS="$CFLAGS"
remember_CXXFLAGS="$CXXFLAGS"
remember_LDFLAGS="$LDFLAGS"
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O

# Checks whether OS is AIX.
AM_CONDITIONAL([BUILD_CMSERVER_FOR_AIX], [test $(uname) = "AIX"])

# Checks bit model feature.
AC_ARG_ENABLE([64bit],
	[AS_HELP_STRING([--enable-64bit],
		[build 64 bit code @<:@default=no (32 bit code)@:>@])],
	[enable_64bit=$enableval],
	[enable_64bit=no])
AM_CONDITIONAL([BUILD_64], [test "$enable_64bit" = "yes"])

OBJECT_MODE=""
case $SYSTEM_TYPE in
	*linux*) 
		if test "$enable_64bit" = "yes"
		then
		  BIT_MODEL="-m64"
		else
		  BIT_MODEL="-m32"
		  CC="$CC $BIT_MODEL"
		  CXX="$CXX $BIT_MODEL"
		  case "$build_cpu" in
			i?86)
				CC="$CC -march=$build_cpu"
				CXX="$CXX -march=$build_cpu"
				;;
			x86_64|amd64)
				CC="$CC -march=i686"
				CXX="$CXX -march=i686"
				;;
			*)
				;;
		  esac
		fi
		;;
	*aix*)
		if test "$enable_64bit" = "yes"
		then
		  BIT_MODEL="-maix64"
		  CC="$CC -maix64"
		  CXX="$CXX -maix64"
		  OBJECT_MODE="64"
		else
		  BIT_MODEL="-maix32"
		  CC="$CC -maix32"
		  CXX="$CXX -maix32"
		  OBJECT_MODE="32"
		fi
		;;
	*)
		;;
esac
export BIT_MODEL CC CXX OBJECT_MODE

LT_INIT
AC_PROG_LIBTOOL
AM_PROG_LIBTOOL
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_YACC
AC_PROG_LEX
CFLAGS="$remember_CFLAGS"
CXXFLAGS="$remember_CXXFLAGS"
LDFLAGS="$remember_LDFLAGS"

# Patch libtool to not use rpath
sed < libtool > libtool-2 's/^hardcode_libdir_flag_spec.*$'/'hardcode_libdir_flag_spec=" -D__LIBTOOL_NO_RPATH__ "/'
mv libtool-2 libtool
chmod 755 libtool

WARN=""
WARN_CXX=""
case $SYSTEM_TYPE in
	*linux*) 
		SYS_DEFS="-DGCC -DLINUX -D_GNU_SOURCE -DI386"
		SYS_LIBS=""
		XOPEN_DEFS="-D_XOPEN_SOURCE"
		WARN="-Wextra"
		WARN_CXX="-Wextra"
		;;
	*aix*)
		SYS_DEFS="-DGCC -DAIX -D_GNU_SOURCE -DPPC -fsigned-char -D_THREAD_SAFE"
		XOPEN_DEFS=""
		CC="$CC -pthread"
		CXX="$CXX -pthread"
		LDFLAGS="-Wl,-bbigtoc -Wl,-brtl -Wl,-bnortllib -Wl,-bnosymbolic -Wl,-bnoautoexp -Wl,-bexpall"
		if test "$enable_64bit" = "yes"
		then
		  SYS_LIBS="-Wl,-bmaxdata:0x200000000 -Wl,-bnoipath"
		  SYS_DEFS="$SYS_DEFS -DAIX_PPC64 -D__WORDSIZE=64"
		else
		  SYS_LIBS="-Wl,-bmaxdata:0x80000000 -Wl,-bnoipath"
		  SYS_DEFS="$SYS_DEFS -D_LARGE_FILES -DAIX_PPC32 -D__WORDSIZE=32"
		fi
		;;
	*)
		SYS_DEFS=""
		SYS_LIBS=""
		XOPEN_DEFS=""
		;;
esac

WARN="-Wall -W -Wwrite-strings -Wno-cast-qual -Wmissing-prototypes -Wredundant-decls -Wlong-long $WARN"
WARN_CXX="-Wall -W -Wwrite-strings -Wno-cast-qual -Wredundant-decls -Wlong-long $WARN_CXX"
NOWARN="-Wno-unused"
CFLAGS="$CFLAGS $WARN $NOWARN"
CXXFLAGS="$CXXFLAGS $WARN_CXX $NOWARN"

case $MACHINE_TYPE in
	x86*|i*86)	SYS_DEFS="$SYS_DEFS -DX86" ;;
	*)		;;
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
#AC_CHECK_LIB([gcrypt], [main])

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
AC_CHECK_HEADERS([limits.h],
	[],
	[AC_DEFINE([LINE_MAX], [2048], [Max line length])])
AC_CHECK_HEADERS([sys/param.h sys/socket.h nl_types.h regex.h getopt.h libgen.h rpc/des_crypt.h err.h vsnprintf])

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
CFLAGS="$CFLAGS -D_LARGEFILE64_SOURCE -finfile-functions"
AC_CHECK_TYPES([off64_t], [SYS_DEFS="$SYS_DEFS -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"])
CFLAGS="$remember_CFLAGS"

AC_STRUCT_TM

# Generate WORDS_BIGENDIAN in AIX
AC_C_BIGENDIAN

AS_IF([test "$GCC" = "yes"],
	[AC_MSG_CHECKING([for --hash-style=both linker support])
	remember_LDFLAGS="$LDFLAGS"
	hashstyle=`gcc -dumpspecs | grep hash-style=gnu`
	if test "x$hashstyle" != "x"
	then
		LDFLAGS="$LDFLAGS -Wl,--hash-style=both"
		AC_LINK_IFELSE([AC_LANG_SOURCE([
			#include <stdio.h>
			int main(void) { return 0; }
		])], have_ld_hash_style=yes, have_ld_hash_style=no)

	else
		have_ld_hash_style=no
	fi

	if test "x$have_ld_hash_style" = "xyes"
	then
		AC_MSG_RESULT([yes])
	else
		AC_MSG_RESULT([no])
		LDFLAGS="$remember_LDFLAGS"
	fi])

# Checks for library functions.
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([getrlimit sysconf open_memstream])

# Checks for system functions for which we have replacements.
AC_REPLACE_FUNCS([vasprintf asprintf strdup strlcpy strlcat \
		  getopt getopt_long dirname basename])

# Check reentrant functions
AC_CHECK_FUNCS([ctime_r localtime_r drand48_r])

if test "$BIT_MODEL" = "-maix64"
then
	remember_LDFLAGS="$LDFLAGS"
	LDFLAGS="$LDFLAGS -L/opt/freeware/lib -lstdc++ -Wl,-bernotok"
	AC_MSG_CHECKING([for libgcc_s and libstdc++ ...])
	AC_LANG_PUSH([C++])
	AC_LINK_IFELSE([AC_LANG_SOURCE([#include <iostream>
			int main(void) { return 0; }])],
		       [AC_MSG_RESULT(yes)],
		       [AC_MSG_ERROR([Cannot compile 64bit program, please check libgcc_s or libcstdc++ in /opt/freeware/lib.])])
	AC_LANG_POP
	LDFLAGS="$remember_LDFLAGS"
fi

# Check gethostbyname_r function
AC_MSG_CHECKING([for glibc gethostbyname_r])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <netdb.h>]],
	[[struct hostent result_buf;
	 char buf[1024];
	 struct hostent *result;
	 int h_erropp;
	 gethostbyname_r("localhost", &result_buf, buf, sizeof(buf), &result, &h_erropp);
	]])],
	[AC_MSG_RESULT([yes])
	 AC_DEFINE(HAVE_GETHOSTBYNAME_R_GLIBC, 1, [Using glibc version of gethostbyname_r()])
	 AC_DEFINE(HAVE_GETHOSTBYNAME_R, 1, [Define to 1 if you have the gethostbyname_r function.])],
	[AC_MSG_RESULT([no])])

AC_MSG_CHECKING([for Solaris/Irix gethostbyname_r])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <netdb.h>]],
	[[struct hostent result;
	 char buf[1024];
	 int h_errnop;
	 gethostbyname_r("localhost", &result, buf, sizeof(buf), &h_errnop);
	]])],
	[AC_MSG_RESULT([yes])
	 AC_DEFINE(HAVE_GETHOSTBYNAME_R_SOLARIS, 1, [Using Solaris gethostbyname_r()])
	 AC_DEFINE(HAVE_GETHOSTBYNAME_R, 1, [Define to 1 if you have the gethostbyname_r function.])],
	[AC_MSG_RESULT([no])])

AC_MSG_CHECKING([for struct hostent_data gethostbyname_r])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <netdb.h>]],
	[[struct hostent result;
	 struct hostent_data ht_data;
	 gethostbyname_r("localhost", &result, &ht_data);
	]])],
	[AC_MSG_RESULT([yes])
	 AC_DEFINE(HAVE_GETHOSTBYNAME_R_HOSTENT_DATA, 1, [Using struct hostent_data gethostbyname_r()])
	 AC_DEFINE(HAVE_GETHOSTBYNAME_R, 1, [Define to 1 if you have the gethostbyname_r function.])],
	[AC_MSG_RESULT([no])])

# Check gcc atomic builtins
# Attention: We used UINT64 in source code. But AIX 32bit seems doesn't support uint64_t type
# in atomic builtins. so, in AIX32bit, HAVE_GCC_ATOMIC_BUILTINS is undefined.
AC_MSG_CHECKING([whether gcc provides atomic builtins])
AC_RUN_IFELSE([AC_LANG_SOURCE([
	#include <stdint.h>
	int main() {
  		uint64_t i, j, r;
		i = 100; j = 200;
		r = __sync_bool_compare_and_swap(&i, 100, j);
		if (r == 0 || i != 200) {
		  return(1);
		}
		i = 100; j = 200;
		r = __sync_bool_compare_and_swap(&i, 101, j);
		if (r != 0 || i != 100) {
		  return(1);
		}
		i = 100; j = 200;
		r = __sync_add_and_fetch(&i, j);
		if (r != 300 || i != 300) {
		  return(1);
		}
		i = 100;
		r = __sync_lock_test_and_set(&i, 200);
		if (r != 100 || i != 200) {
		  return(1);
		}
		return(0);
	}])],
	[AC_MSG_RESULT([yes])
	 AC_DEFINE(HAVE_GCC_ATOMIC_BUILTINS, 1, [Define to 1 if your gcc provides atomic builtins.])
	],
	[AC_MSG_RESULT([no])]
)

# Checks system services
AC_SYS_LARGEFILE

# Checks optional features.
AC_ARG_ENABLE([coverage],
	[AS_HELP_STRING([--enable-coverage],
		[build as code coverage mode @<:@default=no@:>@])],
	[enable_coverage=$enableval],
	[enable_coverage=no])

AS_IF([test "$enable_coverage" = "yes"],
	[CFLAGS="$CFLAGS -fprofile-arcs -ftest-coverage"
	CXXFLAGS="$CXXFLAGS -fprofile-arcs -ftest-coverage"
	LDFLAGS="$LDFLAGS -lgcov"])
AM_CONDITIONAL([BUILD_COVERAGE], [test "$enable_coverage" = "yes"])

AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug],
		[build as debug mode @<:@default=no (release mode)@:>@])],
	[enable_debug=$enableval],
	[enable_debug=no])
AS_IF([test "$enable_debug" = "yes"],
	[CFLAGS="$CFLAGS -ggdb -fno-inline"
	CXXFLAGS="$CXXFLAGS -ggdb -fno-inline"],
	[CFLAGS="$CFLAGS -ggdb -O2 -DNDEBUG -finline-functions"
	CXXFLAGS="$CXXFLAGS -ggdb -O2 -DNDEBUG -finline-functions"])
AM_CONDITIONAL([BUILD_DEBUG], [test "$enable_debug" = "yes"])

AC_ARG_ENABLE([profile],
	[AS_HELP_STRING([--enable-profile],
		[build as profile mode @<:@default=no@:>@])],
	[enable_profile=$enableval],
	[enable_profile=no])
AS_IF([test "$enable_profile" = "yes"],
	[CFLAGS="$CFLAGS -g -pg"
	CXXFLAGS="$CXXFLAGS -g -pg"])

AC_MSG_CHECKING([whether build cci library only])
AC_ARG_WITH(cci-only,
	AS_HELP_STRING([--with-cci-only],
		[build cci library only @<:@default=no@:>@]),
	[with_cci_only=$enableval],
	[with_cci_only=no])
AS_IF([test "$with_cci_only" = "no"],
	[AS_IF([test ! -d $srcdir/cubrid], [with_cci_only=yes])])
AC_MSG_RESULT([$with_cci_only])

AC_ARG_ENABLE([hoard],
	[AS_HELP_STRING([--enable-hoard],
		[enable to use Hoard as memory allocator @<:@default=no (C library)@:>@])],
	[enable_hoard=$enableval],
	[enable_hoard=no])
AS_IF([test "$with_cci_only" = "yes"], [enable_hoard=no],
	[EXTERNAL_DIST="hoard-371"])
AS_IF([test "$enable_hoard" = "yes"],
	[AC_CONFIG_SUBDIRS([external/hoard-371])
	SYS_DEFS="$SYS_DEFS -DHOARD"
	SYS_LIBS="$SYS_LIBS -L\$(top_builddir)/external/hoard-371 -lhoard -lstdc++ -ldl -lpthread"])

AC_ARG_ENABLE([owfs],
	[AS_HELP_STRING([--enable-owfs],
		[enable to use OwFS for external file system @<:@default=no (C library)@:>@])],
	[enable_owfs=$enableval],
	[enable_owfs=no])
AS_IF([test "$with_cci_only" = "yes"], [enable_owfs=no])
AS_IF([test "$enable_owfs" = "yes"],
	[AC_CONFIG_SUBDIRS([external/owfs])
	SYS_DEFS="$SYS_DEFS -DCUBRID_OWFS"
	SYS_LIBS="$SYS_LIBS -L\$(top_builddir)/external/lib -lowfsclient -ldl -lpthread"])

AC_ARG_ENABLE([mock],
	[AS_HELP_STRING([--enable-mock],
		[build as dbgw lib to use cci mock @<:@default=no@:>@])],
	[enable_mock=$enableval],
	[enable_mock=no])
AS_IF([test "$enable_mock" = "yes"],
	[DBGW_DEFS="$DBGW_DEFS -DBUILD_MOCK"])

AC_MSG_CHECKING(for java home)
AS_IF([test "$with_cci_only" = "no"],
      	[AS_IF([test "x$JAVA_HOME" != "x"],
	[JAVA_BIN=$JAVA_HOME/bin
	JAVAC=$JAVA_HOME/bin/javac
	AC_MSG_RESULT($JAVA_HOME)],
	[AC_MSG_ERROR(set your JAVA_HOME environment variable.)])])

AC_ARG_WITH(jdbc,
	AS_HELP_STRING([--without-jdbc],
		[JDBC driver does not build @<:@default=no@:>@]),
	[],
	[with_jdbc=yes])
AS_IF([test "$with_cci_only" = "yes"], [with_jdbc=no])
AS_IF([test "x$with_jdbc" = "xyes"],
	[AC_MSG_CHECKING([for javac version])
		JAVAC_VERSION=`$JAVAC -version 2>&1 | head -1 | awk '{ print $2 }' | awk -F\. '{ print $1 "." $2 }'`
		AC_MSG_RESULT($JAVAC_VERSION)
		if test -n "$JAVAC_VERSION"; then
			if expr 1.5 \> $JAVAC_VERSION > /dev/null; then
				AC_MSG_ERROR(JDK 1.5 or later is required)
			fi
		fi
		OPTIONAL_DIR="$OPTIONAL_DIR jdbc java"

	AC_CHECK_PROG(ANT, ant, ant)
	AS_IF([test "x$ANT" = "x"],
		[AC_MSG_ERROR(Ant was not found. check PATH or install ant)])

	AC_CHECK_FILE($JAVA_HOME/include/jni.h,
		[JAVA_INC="-I$JAVA_HOME/include"],
		AC_MSG_ERROR(jni.h is required.))
	case ${host_os} in
		linux*)
			mddir="linux";;
		solaris*)
			mddir="solaris";;
		aix*)
			mddir="aix";;
		*)
			mddir=""
	esac

	if test "x$mddir" != "xaix"
	then  
		AC_CHECK_FILE($JAVA_HOME/include/$mddir/jni_md.h,
			[JAVA_INC="$JAVA_INC -I$JAVA_HOME/include/$mddir"],
			AC_MSG_ERROR(jni_md.h is required.))
	fi

	AC_MSG_CHECKING([whether JNI programs can be compiled with $JAVA_INC])
	CFLAGS="$CFLAGS $JAVA_INC"
	AC_LINK_IFELSE([AC_LANG_SOURCE([
			#include <jni.h>
			int main(void) { return 0; }
		])], [AC_MSG_RESULT(yes)],
		[AC_MSG_ERROR([Cannot compile a JNI program. See config.log for details.])])
	AC_SUBST(JAVA_INC)
	AC_SUBST(JAVA_HOME)])

AC_MSG_CHECKING([for bison library])
AC_ARG_WITH([bison],
	AS_HELP_STRING([--with-bison=PATH],
		[prefix for installed bison @<:@default=builtin@:>@]),
	[with_bison=$withval],
	[with_bison=builtin])
AS_IF([test "$with_cci_only" = "yes"], [with_bison=no],
	[AS_IF([test "$with_bison" = "builtin"],
		[AC_CONFIG_SUBDIRS([external/bison-2.3])
		BISON="\$(top_builddir)/external/bin/bison"
		EXTERNAL_PKGS="$EXTERNAL_PKGS bison-2.3"],
		[BISON="$with_bison/bin/bison"
		if test ! -x $BISON
		then
			AC_MSG_ERROR([Invalid bison path($BISON) or permission denied.])
		fi
		BISON_EXPECTED_VERSION="2.3"
		BISON_VERSION=`$BISON --version 2> /dev/null | grep 'GNU Bison' | cut -d ' ' -f 4 | tr -d a-z`
		if [[ -z "$BISON_VERSION" ]] || [[ "$BISON_VERSION" != "$BISON_EXPECTED_VERSION" ]]
		then
			AC_MSG_ERROR([Unsupported bison version($BISON_VERSION). It should be $BISON_EXPECTED_VERSION.])
		fi
		EXTERNAL_DIST="$EXTERNAL_DIST bison-2.3"])])
AC_MSG_RESULT([$with_bison])

AC_MSG_CHECKING([for libedit library])
AC_ARG_WITH([libedit],
	AS_HELP_STRING([--with-libedit=PATH],
		[prefix for installed editline @<:@default=builtin@:>@]),
	[with_libedit=$withval],
	[with_libedit=builtin])
AS_IF([test "$with_cci_only" = "yes"], [with_libedit=no],
	[AS_IF([test "$with_libedit" = "builtin"],
		[AC_CONFIG_SUBDIRS([external/libedit-20120601-3.0])
		LIBEDIT_LIBS="\$(top_builddir)/external/lib/libedit.la"
		LIBEDIT_INC="-I\$(top_builddir)/external/include"
		EXTERNAL_PKGS="$EXTERNAL_PKGS libedit-20120601-3.0"],
		[LIBEDIT_LIBS="-L$with_libedit/lib -ledit"
		LIBEDIT_INC="-I$with_libedit/include"
		EXTERNAL_DIST="$EXTERNAL_DIST libedit-20120601-3.0"])])
AC_MSG_RESULT([$with_libedit])

AC_MSG_CHECKING([for lzo2 library])
AC_ARG_WITH([lzo2],
	AS_HELP_STRING([--with-lzo2=PATH],
		[prefix for installed LZO2 @<:@default=builtin@:>@]),
	[with_lzo2=$withval],
	[with_lzo2=builtin])
AS_IF([test "$with_cci_only" = "yes"], [with_lzo2=no],
	[AS_IF([test "$with_lzo2" = "builtin"],
		[AC_CONFIG_SUBDIRS([external/lzo-2.03])
		LZO_LIBS="\$(top_builddir)/external/lib/liblzo2.la"
		LZO_INC="-I\$(top_builddir)/external/include/lzo"
		EXTERNAL_PKGS="$EXTERNAL_PKGS lzo-2.03"],
		[LZO_LIBS="-L$with_lzo2/lib -llzo2"
		LZO_INC="-I$with_lzo2/include/lzo"
		EXTERNAL_DIST="$EXTERNAL_DIST lzo-2.03"])])
AC_MSG_RESULT([$with_lzo2])

AC_MSG_CHECKING([for gc library])
AC_ARG_WITH([gc],
	AS_HELP_STRING([--with-gc=PATH],
		[prefix for installed GC @<:@default=builtin@:>@]),
	[with_gc=$withval],
	[with_gc=builtin])
AS_IF([test "$with_cci_only" = "yes"], [with_gc=no],
	[AS_IF([test "$with_gc" = "builtin"],
		[AC_CONFIG_SUBDIRS([external/gc6.7])
		GC_LIBS="\$(top_builddir)/external/lib/libgc.la"
		GC_INC="-I\$(top_builddir)/external/include/gc"
		EXTERNAL_PKGS="$EXTERNAL_PKGS gc6.7"],
		[GC_LIBS="-L$with_gc/lib -lgc"
		GC_INC="-I$with_gc/include/gc"
		EXTERNAL_DIST="$EXTERNAL_DIST gc6.7"])])
AC_MSG_RESULT([$with_gc])

AC_MSG_CHECKING([for libgcrypt library])
AC_ARG_WITH([libgcrypt],
	AS_HELP_STRING([--with-libgcrypt=PATH],
		[prefix for installed libgcrypt @<:@default=builtin@:>@]),
	[with_libgcrypt=$withval],
	[with_libgcrypt=builtin])	
AS_IF([test "$with_cci_only" = "yes"], [with_libgcrypt=no],
	[AS_IF([test "$with_libgcrypt" = "builtin"],
		[AC_CONFIG_SUBDIRS([external/libgcrypt-1.5.2])		
		GCRYPT_LIBS="\$(top_builddir)/external/lib/libgcrypt.la \$(top_builddir)/external/lib/libgpg-error.la"
		GCRYPT_INC="-I\$(top_builddir)/external/include/"
		EXTERNAL_PKGS="$EXTERNAL_PKGS libgpg-error-1.11 libgcrypt-1.5.2"],
		[GCRYPT_LIBS="-L$with_libgcrypt/lib -lgcrypt"
		GCRYPT_INC="-I$with_libgcrypt/include/"
		EXTERNAL_DIST="$EXTERNAL_DIST libgcrypt-1.5.2"])])
AC_MSG_RESULT([$with_libgcrypt])

AC_MSG_CHECKING([for aio library])
AC_ARG_WITH([aio],
	AS_HELP_STRING([--with-aio],
		[use aio]),
	[with_aio=$withval],
	[with_aio=no])
AS_IF([test "$with_cci_only" = "yes"], [with_aio=no])
AS_IF([test "$with_aio" = "yes"],
	[SYS_DEFS="$SYS_DEFS -DUSE_AIO"
	SYS_LIBS="$SYS_LIBS -lrt"])
AC_MSG_RESULT([$with_aio])

AC_MSG_CHECKING([for mysql client library version 5.1])
AC_ARG_WITH([mysql51],
	AS_HELP_STRING([--with-mysql51=PATH],
		[for CAS for MySQL @<:@default=builtin@:>@]),
	[with_mysql51=$withval],
	[with_mysql51=builtin])

case "$with_mysql51" in
	yes|builtin)
		if test -d "$srcdir/external/mysqlclient/5.1" && test "$enable_64bit" == "yes"; then
			AC_CONFIG_SUBDIRS([external/mysqlclient/5.1])
			MYSQL51_LIB_PATH="-L\$(top_builddir)/external/mysqlclient/5.1/lib"
			MYSQL51_INC_PATH="-I\$(top_builddir)/external/mysqlclient/5.1/include"
		else
			with_mysql51=no
		fi
		;;
	no)
		;;
	*)
		MYSQL51_LIB_PATH="-L$with_mysql/lib"
		MYSQL51_INC_PATH="-I$with_mysql/include"
		;;
esac
AM_CONDITIONAL([BUILD_CAS_FOR_MYSQL51], [test "$with_mysql51" != "no"])
AC_MSG_RESULT([$with_mysql51])

AC_MSG_CHECKING([for mysql client library version 6.1])
AC_ARG_WITH([mysql],
	AS_HELP_STRING([--with-mysql=PATH],
		[for CAS for MySQL @<:@default=builtin@:>@]),
	[with_mysql=$withval],
	[with_mysql=builtin])

case "$with_mysql" in
	yes|builtin)
		if test -d "$srcdir/external/mysqlclient/6.1" && test "$enable_64bit" == "yes"; then
			AC_CONFIG_SUBDIRS([external/mysqlclient/6.1])
			MYSQL_LIB_PATH="-L\$(top_builddir)/external/mysqlclient/6.1/lib"
			MYSQL_INC_PATH="-I\$(top_builddir)/external/mysqlclient/6.1/include"
		else
			with_mysql=no
		fi
		;;
	no)
		;;
	*)
		MYSQL_LIB_PATH="-L$with_mysql/lib"
		MYSQL_INC_PATH="-I$with_mysql/include"
		;;
esac
AM_CONDITIONAL([BUILD_CAS_FOR_MYSQL], [test "$with_mysql" != "no"])
AC_MSG_RESULT([$with_mysql])

AC_MSG_CHECKING([for oracle client library])
AC_ARG_WITH([oracle],
	AS_HELP_STRING([--with-oracle=PATH],
		[for CAS for Oracle @<:@default=builtin@:>@]),
	[with_oracle=$withval],
	[with_oracle=builtin])
AS_IF([test "$with_cci_only" = "yes"], [with_oracle=no])
case "$with_oracle" in
	yes|builtin)
		if test -d "$srcdir/external/oracleclient" -a "$enable_64bit" = "yes"; then
			AC_CONFIG_SUBDIRS([external/oracleclient])
			ORACLE_LIB_PATH="-L\$(top_builddir)/external/lib"
			ORACLE_INC_PATH="-I\$(top_builddir)/external/include/oracleclient"
		else
			with_oracle=no
		fi
		;;
	no)
		;;
	*)
		ORACLE_LIB_PATH="-L$with_oracle/lib"
		ORACLE_INC_PATH="-I$with_oracle/include"
		;;
esac
AM_CONDITIONAL([BUILD_CAS_FOR_ORACLE], [test "$with_oracle" != "no"])
AC_MSG_RESULT([$with_oracle])

AC_ARG_ENABLE([dbgw],
       [AS_HELP_STRING([--enable-dbgw],
               [build as dbgw lib @<:@default=no@:>@])],
       [enable_dbgw=$enableval],
       [enable_dbgw=no])
       
AM_CONDITIONAL([BUILD_DBGW], [test "$enable_dbgw" != "no"])
AS_IF([test "$with_cci_only" = "yes"], [enable_dbgw=no])
AM_CONDITIONAL([BUILD_DBGW_ORACLE], [test "$enable_dbgw" != "no" && test "$with_oracle" != "no"])
AM_CONDITIONAL([BUILD_DBGW_MYSQL51], [test "$enable_dbgw" != "no" && test "$with_mysql51" != "no"])
AM_CONDITIONAL([BUILD_DBGW_MYSQL], [test "$enable_dbgw" != "no" && test "$with_mysql" != "no"])
AM_CONDITIONAL([BUILD_DBGW_ALL], [test "$enable_dbgw" != "no" && test "$with_oracle" != "no" && test "$with_mysql" != "no"])
if test "$enable_dbgw" = "yes"
then
	AC_LANG_PUSH([C++])
	AC_CHECK_HEADERS([boost/property_tree/ptree.hpp], [], [AC_MSG_ERROR(Boost library is required.(version 1.41.0 or higher))])
	AC_LANG_POP([C++])
fi

AC_MSG_CHECKING([for cubrid manager server])
AC_ARG_WITH([cmserver],
	AS_HELP_STRING([--with-cmserver=PATH],
		[for CUBRID Manager Server@<:@default=check@:>@]),
	[with_cmserver=$withval],
	[with_cmserver=check])
AS_IF([test "$with_cci_only" = "yes"], [with_cmserver=no])
case "$with_cmserver" in
	yes|builtin|check)
		if test -d "$srcdir/cubridmanager/server"; then
		  if test ! -x "$srcdir/cubridmanager/server/configure"; then
		    (cd "$srcdir/cubridmanager/server" && sh ./autogen.sh)
		  fi
		  if test "$enable_64bit" = "yes"
                  then
		    build_64="--enable-64bit"
		  else
                    build_64=""
		  fi
		    cm_common_libdir=`readlink -f $PWD/cm_common`
		    cm_common_includedir=`readlink -f $srcdir/src/cm_common`
		    cci_includedir=`readlink -f $srcdir/src/cci`
		    cci_libdir=`readlink -f $PWD/cci`
		    cs_libdir=`readlink -f $PWD/cs`
		    broker_includedir=`readlink -f $srcdir/src/broker`
		    ac_configure_args="$ac_configure_args $build_64 --prefix='$prefix' CXXFLAGS='$CXXFLAGS -I$cci_includedir -I$broker_includedir' LDFLAGS='-L$cci_libdir -L$cs_libdir' --with-cubrid-libdir=$cm_common_libdir --with-cubrid-includedir=$cm_common_includedir" AC_CONFIG_SUBDIRS([cubridmanager/server])
		    OPTIONAL_DIR="$OPTIONAL_DIR cubridmanager/server"
		    with_cmserver=builtin
		else
		  if test "$with_cmserver" != "check"; then
		    AC_MSG_ERROR([there is no server in $srcdir/cubridmanager])
		  else
		    with_cmserver=no
		  fi
		fi
		;;
	no)
		;;
	*)
		AC_MSG_ERROR([not supported yet])
		;;
esac
AM_CONDITIONAL([BUILD_CMSERVER], [test "$with_cmserver" != "no"])
AC_MSG_RESULT([$with_cmserver])

AC_CONFIG_SUBDIRS([external/libregex38a])
REGEX_LIBS="\$(top_builddir)/external/libregex38a/libregex38a.la"
REGEX_INC="-I\$(top_builddir)/external/include/libregex38a"
EXTERNAL_PKGS="$EXTERNAL_PKGS libregex38a"

AC_MSG_CHECKING([for expat library])
AC_ARG_WITH([expat],
	AS_HELP_STRING([--with-expat=PATH],
		[prefix for installed expat @<:@default=builtin@:>@]),
	[with_expat=$withval],
	[with_expat=builtin])
AS_IF([test "$with_cci_only" = "yes"], [with_expat=no],
	[AS_IF([test "$with_expat" = "builtin"],
		[AC_CONFIG_SUBDIRS([external/expat-2.0.1])
		EXPAT_LIBS="\$(top_builddir)/external/lib/libexpat.la"
		EXPAT_INC="-I\$(top_builddir)/external/include/expat"
		EXTERNAL_PKGS="$EXTERNAL_PKGS expat-2.0.1"],
		[EXPAT_LIBS="-L$with_expat/lib -lexpat"
		EXPAT_INC="-I$with_expat/include"
		EXTERNAL_DIST="$EXTERNAL_DIST expat-2.0.1"])])
AC_MSG_RESULT([$with_expat])

AC_MSG_CHECKING([for jansson library])
AC_ARG_WITH([jansson],
        AS_HELP_STRING([--with-jansson=PATH],
                [prefix for installed jansson @<:@default=builtin@:>@]),
        [with_jansson=$withval],
        [with_jansson=builtin])
AS_IF([test "$with_cci_only" = "yes"], [with-jansson=no],
	[AS_IF([test "$with_expat" = "builtin"],
		[AC_CONFIG_SUBDIRS([external/jansson-2.4])
        	JANSSON_LIBS="\$(top_builddir)/external/lib/libjansson.la"
		JANSSON_INC="-I\$(top_builddir)/external/include/jansson"
		EXTERNAL_PKGS="$EXTERNAL_PKGS jansson-2.4"],
		[JANSSON_LIBS="-L$with_jansson/lib -ljansson"
		JANSSON_INC="-I$with_jansson/include"
		EXTERNAL_DIST="$EXTERNAL_DIST jansson-2.4"])])
AC_MSG_RESULT([$with_jansson])

AC_PREFIX_DEFAULT("$HOME/cubrid")

BUILD_NUMBER=BUILD_NUMBER_STRING
BUILD_OS=`echo $build_os | sed -e "s|-|_|"`
AC_SUBST(LT_VERSION,MAJOR_VERSION:MINOR_VERSION:PATCH_VERSION)
AC_SUBST([CONFIG_STATUS_DEPENDENCIES], ['$(top_srcdir)/BUILD_NUMBER'])

SERVER_DEFS="-DSERVER_MODE"
CS_DEFS="-DCS_MODE"
SA_DEFS="-DSA_MODE"
COMMON_DEFS="-DANSI=1 -DSYSV -DMAXPATHLEN=1024 -D_REENTRANT"
VERSION_DEFS="-DMAJOR_VERSION=MAJOR_VERSION -DMINOR_VERSION=MINOR_VERSION \
-DPATCH_VERSION=PATCH_VERSION -DRELEASE_STRING=RELEASE_STRING \
-DMAJOR_RELEASE_STRING=MAJOR_RELEASE_STRING -DBUILD_NUMBER=BUILD_NUMBER_STRING \
-DBUILD_OS='$BUILD_OS'"

SRC_INC="-I\$(top_srcdir)/include"
DIR_LIST=`ls -l $srcdir/src | grep ^d | awk '{ print $9}'`
for D in $DIR_LIST
do
	SRC_INC="$SRC_INC -I\$(top_srcdir)/src/$D"
done

CUBRID_LIB="\$(top_builddir)/cubrid/libcubrid.la"
CS_LIB="\$(top_builddir)/cs/libcubridcs.la"
SA_LIB="\$(top_builddir)/sa/libcubridsa.la"
CAS_LIB="\$(top_builddir)/cas/libcas.la"
CCI_LIB="\$(top_builddir)/cci/libcascci.la"
ESQL_LIB="\$(top_builddir)/util/libcubridesql.la"
BROKER_ADMIN_LIB="\$(top_builddir)/broker/libbrokeradmin.la"

EXT_LIBS="$LIBEDIT_LIBS $LZO_LIBS $GC_LIBS $REGEX_LIBS $EXPAT_LIBS $JANSSON_LIBS $GCRYPT_LIBS"
EXT_INC="$LIBEDIT_INC $LZO_INC $GC_INC $REGEX_INC $EXPAT_INC $JANSSON_INC $GCRYPT_INC"

AC_MSG_CHECKING([whether to use CUBRID environment variable])
AC_ARG_ENABLE([cubridenv],
	AS_HELP_STRING([--enable-cubridenv],
		       [Using CUBRID environment variable @<:@default=yes@:>@]),
	[case "$enableval" in
	  yes)
	    ;;
	   no)
	    COMMON_DEFS="$COMMON_DEFS -DDO_NOT_USE_CUBRIDENV"
	    ;;
	  *)
	    AC_MSG_ERROR([bad value ${enableval} for --enable-cubridenv])
	    ;;
	  esac],
	  [enableval=yes])
AC_MSG_RESULT($enableval)
AM_CONDITIONAL([DISABLE_CUBRIDENV], [test "x$enableval" = "xno"])

# install dir
AC_MSG_CHECKING([for bindir])
AS_IF([test "$bindir" != '${exec_prefix}/bin'],
      [AC_MSG_ERROR(Do not use --bindir option ($bindir). It must be PREFIX/bin)])
AC_MSG_RESULT($bindir)

AC_MSG_CHECKING([for libdir])
AS_IF([test "$libdir" != '${exec_prefix}/lib'],
      [AC_MSG_ERROR(Do not use --libdir option ($libdir). It must be PREFIX/lib)])
AC_MSG_RESULT($libdir)

AC_MSG_CHECKING([for localstatedir])
#AS_IF([test "$localstatedir" != '${prefix}/var'],
#      [AC_MSG_ERROR(Do not use --localstatedir option ($localstatedir). It must be PREFIX/var)])
AC_MSG_RESULT($localstatedir)

DIR_DEFS="-DCUBRID_PREFIXDIR=\\\"\$(prefix)\\\" -DCUBRID_BINDIR=\\\"\$(bindir)\\\" -DCUBRID_LIBDIR=\\\"\$(libdir)\\\" -DCUBRID_VARDIR=\\\"\$(localstatedir)\\\""

AC_MSG_CHECKING([for confdir])
AC_ARG_WITH([confdir],
	AS_HELP_STRING([--with-confdir=PATH],
		       [Directory containing configuration files @<:@default=PREFIX/conf@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need CONFDIR.)
	    ;;
	  yes)
	    CUBRID_CONFDIR="\$(prefix)/conf"
	    ;;
	  *)
	    CUBRID_CONFDIR="$withval"
	    ;;
	  esac],
	[CUBRID_CONFDIR="\$(prefix)/conf"])
AC_MSG_RESULT($CUBRID_CONFDIR)

DIR_DEFS="$DIR_DEFS -DCUBRID_CONFDIR=\\\"$CUBRID_CONFDIR\\\""

AC_MSG_CHECKING([for jspdir])
AC_ARG_WITH([jspdir],
	AS_HELP_STRING([--with-jspdir=PATH],
		       [Directory containing JSP server files @<:@default=PREFIX/java@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need JSPDIR.)
	    ;;
	  yes)
	    CUBRID_JAVADIR="\$(prefix)/java"
	    ;;
	  *)
	    CUBRID_JAVADIR="$withval"
	    ;;
	  esac],
	[CUBRID_JAVADIR="\$(prefix)/java"])
AC_MSG_RESULT($CUBRID_JAVADIR)

DIR_DEFS="$DIR_DEFS -DCUBRID_JAVADIR=\\\"$CUBRID_JAVADIR\\\""

AC_MSG_CHECKING([for jdbcdir])
AC_ARG_WITH([jdbcdir],
	AS_HELP_STRING([--with-jdbcdir=PATH],
		       [Directory containing JDBC files @<:@default=PREFIX/jdbc@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need JDBCDIR.)
	    ;;
	  yes)
	    CUBRID_JDBCDIR="\$(prefix)/jdbc"
	    ;;
	  *)
	    CUBRID_JDBCDIR="$withval"
	    ;;
	  esac],
	[CUBRID_JDBCDIR="\$(prefix)/jdbc"])
AC_MSG_RESULT($CUBRID_JDBCDIR)

DIR_DEFS="$DIR_DEFS -DCUBRID_JDBCDIR=\\\"$CUBRID_JDBCDIR\\\""

AC_MSG_CHECKING([for demodir])
AC_ARG_WITH([demodir],
	AS_HELP_STRING([--with-demodir=PATH],
		       [Directory containing demo files @<:@default=PREFIX/demo@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need DEMODIR.)
	    ;;
	  yes)
	    CUBRID_DEMODIR="\$(prefix)/demo"
	    ;;
	  *)
	    CUBRID_DEMODIR="$withval"
	    ;;
	  esac],
	[CUBRID_DEMODIR="\$(prefix)/demo"])
AC_MSG_RESULT($CUBRID_DEMODIR)

AC_MSG_CHECKING([for msgdir])
AC_ARG_WITH([msgdir],
	AS_HELP_STRING([--with-msgdir=PATH],
		       [Directory containing message files @<:@default=PREFIX/msg@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need MSGDIR.)
	    ;;
	  yes)
	    CUBRID_LOCALEDIR="\$(prefix)/msg"
	    ;;
	  *)
	    CUBRID_LOCALEDIR="$withval"
	    ;;
	  esac],
	[CUBRID_LOCALEDIR="\$(prefix)/msg"])
AC_MSG_RESULT($CUBRID_LOCALEDIR)
DIR_DEFS="$DIR_DEFS -DCUBRID_LOCALEDIR=\\\"$CUBRID_LOCALEDIR\\\""

AC_MSG_CHECKING([for shlocdir])
AC_ARG_WITH([shlocdir],
	AS_HELP_STRING([--with-locdir=PATH],
		       [Directory containing message files @<:@default=PREFIX/locales@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need LOCALESDIR.)
	    ;;
	  yes)
	    CUBRID_SHLOCALESDIR="\$(prefix)/locales"
	    ;;
	  *)
	    CUBRID_SHLOCALESDIR="$withval"
	    ;;
	  esac],
	[CUBRID_SHLOCALESDIR="\$(prefix)/locales"])
AC_MSG_RESULT($CUBRID_SHLOCALESDIR)
DIR_DEFS="$DIR_DEFS -DCUBRID_SHLOCALESDIR=\\\"$CUBRID_SHLOCALESDIR\\\""

AC_MSG_CHECKING([for compatdir])
AC_ARG_WITH([compatdir],
	AS_HELP_STRING([--with-compatdir=PATH],
		       [Directory containing compatibility files @<:@default=PREFIX/compat@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need COMPATDIR.)
	    ;;
	  yes)
	    CUBRID_COMPATDIR="\$(prefix)/compat"
	    ;;
	  *)
	    CUBRID_COMPATDIR="$withval"
	    ;;
	  esac],
	[CUBRID_COMPATDIR="\$(prefix)/compat"])
AC_MSG_RESULT($CUBRID_COMPATDIR)

AC_MSG_CHECKING([for logdir])
AC_ARG_WITH([logdir],
	AS_HELP_STRING([--with-logdir=PATH],
		       [Directory containing log files @<:@default=PREFIX/log@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need LOGDIR.)
	    ;;
	  yes)
	    CUBRID_LOGDIR="\$(prefix)/log"
	    ;;
	  *)
	    CUBRID_LOGDIR="$withval"
	    ;;
	  esac],
	[CUBRID_LOGDIR="\$(prefix)/log"])
AC_MSG_RESULT($CUBRID_LOGDIR)
DIR_DEFS="$DIR_DEFS -DCUBRID_LOGDIR=\\\"$CUBRID_LOGDIR\\\""

AC_MSG_CHECKING([for tmpdir])
AC_ARG_WITH([tmpdir],
	AS_HELP_STRING([--with-tmpdir=PATH],
		       [Directory containing temporary files @<:@default=PREFIX/tmp@:>@]),
	[case "$withval" in
	   no)
	    AC_MSG_ERROR(Need TMPDIR.)
	    ;;
	  yes)
	    CUBRID_TMPDIR="\$(prefix)/tmp"
	    ;;
	  *)
	    CUBRID_TMPDIR="$withval"
	    ;;
	  esac],
	[CUBRID_TMPDIR="\$(prefix)/tmp"])
AC_MSG_RESULT($CUBRID_TMPDIR)
DIR_DEFS="$DIR_DEFS -DCUBRID_TMPDIR=\\\"$CUBRID_TMPDIR\\\""

# change PACKAGE_STRING to official version string
PACKAGE_STRING="CUBRID VERSION_STRING"
sed < confdefs.h > confdefs.h-2 "s/^#define PACKAGE_STRING .*$"/"#define PACKAGE_STRING \"$PACKAGE_STRING\"/"
mv confdefs.h-2 confdefs.h
AC_DEFINE([PRODUCT_STRING], ["VERSION_STRING"], [official version string])

AS_IF([test "$with_cci_only" = "yes"],
	[BUILD_SUBDIRS="external include cci"
	EXTRA_DIST_SUBDIRS=""
	CUBRID_LOGDIR=""
	CUBRID_TMPDIR=""],
	[BUILD_SUBDIRS="external include cas sa cs cubrid util cci broker cas4mysql cas4oracle cm_common conf msg demo contrib locales dbgw $OPTIONAL_DIR"
	EXTRA_DIST_SUBDIRS="debian win src"])

AC_SUBST([BUILD_NUMBER])
AC_SUBST([PRODUCT_STRING])
AC_SUBST([EXTERNAL_PKGS])
AC_SUBST([EXTERNAL_DIST])
AC_SUBST([SYS_DEFS])
AC_SUBST([SYS_LIBS])
AC_SUBST([XOPEN_DEFS])
AC_SUBST([EXT_LIBS])
AC_SUBST([EXT_INC])
AC_SUBST([SERVER_DEFS])
AC_SUBST([CS_DEFS])
AC_SUBST([SA_DEFS])
AC_SUBST([COMMON_DEFS])
AC_SUBST([VERSION_DEFS])
AC_SUBST([SRC_INC])
AC_SUBST([CUBRID_LIB])
AC_SUBST([CS_LIB])
AC_SUBST([SA_LIB])
AC_SUBST([CAS_LIB])
AC_SUBST([CCI_LIB])
AC_SUBST([ESQL_LIB])
AC_SUBST([BROKER_ADMIN_LIB])
AC_SUBST([BIT_MODEL])
AC_SUBST([DIR_DEFS])
AC_SUBST([CUBRID_CONFDIR])
AC_SUBST([CUBRID_JAVADIR])
AC_SUBST([CUBRID_JDBCDIR])
AC_SUBST([CUBRID_DEMODIR])
AC_SUBST([CUBRID_LOCALEDIR])
AC_SUBST([CUBRID_SHLOCALESDIR])
AC_SUBST([CUBRID_COMPATDIR])
AC_SUBST([CUBRID_LOGDIR])
AC_SUBST([CUBRID_TMPDIR])
AC_SUBST([MYSQL51_LIB_PATH])
AC_SUBST([MYSQL51_INC_PATH])
AC_SUBST([MYSQL_LIB_PATH])
AC_SUBST([MYSQL_INC_PATH])
AC_SUBST([ORACLE_LIB_PATH])
AC_SUBST([ORACLE_INC_PATH])
AC_SUBST([BISON])
AC_SUBST([BUILD_SUBDIRS])
AC_SUBST([EXTRA_DIST_SUBDIRS])
AC_SUBST([EXPAT_LIBS])
AC_SUBST([JANSSON_LIBS])
AC_SUBST([DBGW_DEFS])

AH_TOP([
#ifndef _CONFIG_H_
#define _CONFIG_H_
])
AH_BOTTOM([
#include "system.h"
#endif /* _CONFIG_H_ */
])

AC_CONFIG_FILES([Makefile
	external/Makefile
	include/Makefile
	cci/Makefile
	])

AS_IF([test "$with_cci_only" = "no"],
	[AC_CONFIG_FILES([cas/Makefile
	broker/Makefile
	cm_common/Makefile
	sa/Makefile
	cubrid/Makefile
	cs/Makefile
	util/Makefile
	conf/Makefile
	msg/Makefile
	msg/en_US/Makefile
	msg/en_US.utf8/Makefile
	msg/ko_KR.euckr/Makefile
	msg/ko_KR.utf8/Makefile
	msg/tr_TR.utf8/Makefile
	msg/de_DE.utf8/Makefile
	msg/es_ES.utf8/Makefile
	msg/fr_FR.utf8/Makefile
	msg/it_IT.utf8/Makefile
	msg/ja_JP.utf8/Makefile
	msg/km_KH.utf8/Makefile
	msg/vi_VN.utf8/Makefile
	msg/zh_CN.utf8/Makefile
	msg/ro_RO.utf8/Makefile
	demo/Makefile
	contrib/Makefile
	jdbc/Makefile
	java/Makefile
	cas4mysql/Makefile
	cas4oracle/Makefile
	locales/Makefile
	locales/loclib/Makefile
	locales/data/Makefile
	locales/data/codepages/Makefile
	locales/data/ldml/Makefile
	dbgw/Makefile
	])])

AC_OUTPUT
