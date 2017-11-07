/*
 * Copyright (C) 2016 Search Solution Corporation. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#cmakedefine HAVE_ASPRINTF 1
#cmakedefine HAVE_VASPRINTF 1
#cmakedefine HAVE_BASENAME 1
#cmakedefine HAVE_DIRNAME 1
#cmakedefine HAVE_CTIME_R 1
#cmakedefine HAVE_LOCALTIME_R 1
#cmakedefine HAVE_DRAND48_R 1
#cmakedefine HAVE_GETHOSTBYNAME_R 1
#cmakedefine HAVE_GETHOSTBYNAME_R_GLIBC 1
#cmakedefine HAVE_GETHOSTBYNAME_R_SOLARIS 1
#cmakedefine HAVE_GETHOSTBYNAME_R_HOSTENT_DATA 1
#cmakedefine HAVE_GETOPT_LONG 1
#cmakedefine HAVE_OPEN_MEMSTREAM 1
#cmakedefine HAVE_STRDUP 1
#cmakedefine HAVE_STRLCPY 1

#cmakedefine HAVE_ERR_H 1
#cmakedefine HAVE_GETOPT_H 1
#cmakedefine HAVE_INTTYPES_H 1
#cmakedefine HAVE_LIBGEN_H 1
#cmakedefine HAVE_LIMITS_H 1
#cmakedefine PATH_MAX @PATH_MAX@
#cmakedefine NAME_MAX @NAME_MAX@
#cmakedefine LINE_MAX @LINE_MAX@
#cmakedefine HAVE_MEMORY_H 1
#cmakedefine HAVE_NL_TYPES_H 1
#cmakedefine HAVE_REGEX_H 1
#cmakedefine HAVE_RPC_DES_CRYPT_H 1
#cmakedefine HAVE_STDBOOL_H 1
#cmakedefine HAVE_STDINT_H 1
#cmakedefine HAVE_STDLIB_H 1
#cmakedefine HAVE_STRING_H 1
#cmakedefine HAVE_STRINGS_H 1
#cmakedefine HAVE_SYS_PARAM_H 1
#cmakedefine HAVE_SYS_STAT_H 1
#cmakedefine HAVE_SYS_TYPES_H 1
#cmakedefine HAVE_UNISTD_H 1

#cmakedefine STDC_HEADERS 1
#cmakedefine NOMINMAX 1


#cmakedefine HAVE_BYTE_T 1
#cmakedefine HAVE_INT8_T 1
#cmakedefine HAVE_INT16_T 1
#cmakedefine HAVE_INT32_T 1
#cmakedefine HAVE_INT64_T 1
#cmakedefine HAVE_INTPTR_T 1
#cmakedefine HAVE_UINT8_T 1
#cmakedefine HAVE_UINT16_T 1
#cmakedefine HAVE_UINT32_T 1
#cmakedefine HAVE_UINT64_T 1
#cmakedefine HAVE_UINTPTR_T 1
#cmakedefine HAVE__BOOL 1

#cmakedefine SIZEOF_CHAR @SIZEOF_CHAR@
#cmakedefine SIZEOF_SHORT @SIZEOF_SHORT@
#cmakedefine SIZEOF_INT @SIZEOF_INT@
#cmakedefine SIZEOF_LONG @SIZEOF_LONG@
#cmakedefine SIZEOF_LONG_LONG @SIZEOF_LONG_LONG@
#cmakedefine SIZEOF_VOID_P @SIZEOF_VOID_P@
#cmakedefine off_t @off_t@
#cmakedefine size_t @size_t@
#cmakedefine pid_t @pid_t@


#cmakedefine HAVE_GCC_ATOMIC_BUILTINS 1
#cmakedefine HAVE_LIBGCRYPT 1


#cmakedefine ENABLE_SYSTEMTAP 1

#include "system.h"
#include "version.h"

#endif /* _CONFIG_H_ */

