/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#ifndef _SYSTEM_H_
#define _SYSTEM_H_

#ifndef _CONFIG_H_
#include "config.h"
#endif

#if defined(WINDOWS)
#define _CRT_RAND_S

#include <winsock2.h>
#include <windows.h>
#endif

#include <stdio.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#else
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#endif
#ifdef HAVE_STRING_H
#if !defined STDC_HEADERS && defined HAVE_MEMORY_H
#include <memory.h>
#endif
#include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_REGEX_H
#include <regex.h>
#endif
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif
#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#else
#ifndef HAVE__BOOL
#ifdef __cplusplus
typedef bool _Bool;
#else
#define bool char
#endif
#endif
#define false 0
#define true 1
#define __bool_true_false_are_defined 1
#endif

/* need more consideration for the system which is not LP64 model */
#if SIZEOF_VOID_P < 4
#error "Error: sizeof(void *) < 4"
#endif
#if SIZEOF_INT < 4
#error "Error: sizeo(int) < 4"
#endif

#ifdef HAVE_BYTE_T
typedef byte_t BYTE;
#else
#if SIZEOF_CHAR == 1
typedef unsigned char BYTE;
#else
#error "Error: BYTE"
#endif
#endif
#ifdef HAVE_INT8_T
typedef int8_t INT8;
#else
#if SIZEOF_CHAR == 1
#if !defined(WINDOWS)
typedef char INT8;		/* TODO: check to exist redefined macro INT8 */
#endif
#else
#error "Error: INT8"
#endif
#endif
#ifdef HAVE_INT16_T
typedef int16_t INT16;
#else
#if SIZEOF_SHORT == 2
typedef short INT16;
#else
#error "Error: INT16"
#endif
#endif
#ifdef HAVE_INT32_T
typedef int32_t INT32;
#else
#if SIZEOF_INT == 4
typedef int INT32;
#else
#error "Error: INT32"
#endif
#endif
#ifdef HAVE_INT64_T
typedef int64_t INT64;
#else
#if SIZEOF_LONG == 8
typedef long INT64;
#elif SIZEOF_LONG_LONG == 8
typedef long long INT64;
#else
#error "Error: INT64"
#endif
#endif
#ifdef HAVE_INTPTR_T
typedef intptr_t INTPTR;
#else
#if SIZEOF_VOID_P == SIZEOF_INT
typedef int INTPTR;
#elif SIZEOF_VOID_P == SIZEOF_LONG
typedef long INTPTR;
#elif SIZEOF_VOID_P == SIZEOF_LONG_LONG
typedef long long INTPTR;
#else
#error "Error: UINTPTR"
#endif
#endif
#ifdef HAVE_UINT8_T
typedef uint8_t UINT8;
#else
#if SIZEOF_CHAR == 1
typedef unsigned char UNINT8;
#else
#error "Error: UINT18"
#endif
#endif
#ifdef HAVE_UINT16_T
typedef uint16_t UINT16;
#else
#if SIZEOF_SHORT == 2
typedef unsigned short UINT16;
#else
#error "Error: UINT16"
#endif
#endif
#ifdef HAVE_UINT32_T
typedef uint32_t UINT32;
#else
#if SIZEOF_INT == 4
typedef unsigned int UINT32;
#else
#error "Error: UINT32"
#endif
#endif
#ifdef HAVE_UINT64_T
typedef uint64_t UINT64;
#else
#if SIZEOF_LONG == 8
typedef unsigned long UINT64;
#elif SIZEOF_LONG_LONG == 8
typedef unsigned long long UINT64;
#endif
#endif
#ifdef HAVE_UINTPTR_T
typedef uintptr_t UINTPTR;
#else
#if SIZEOF_VOID_P == SIZEOF_INT
typedef unsigned int UINTPTR;
#elif SIZEOF_VOID_P == SIZEOF_LONG
typedef unsigned long UINTPTR;
#elif SIZEOF_VOID_P == SIZEOF_LONG_LONG
typedef unsigned long long UINTPTR;
#else
#error "Error: UINTPTR"
#endif
#endif

#if defined(WINDOWS)
#define off_t __int64
#endif

/* standard constants for use with variables of type bool */
#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

/* int value of a digit */
#define DECODE(X)       ((int)((X) - '0'))

/* number of elements in array A */
#define DIM(A)          (sizeof(A) / sizeof(*(A)))

#ifndef MAX
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#endif

#define NULL_FUNCTION   (void (*)())0

/* a nice interface to strcmp */
#define STRCMP(a, op, b)        (strcmp((a), (b)) op 0)
#define STREQ(a, b)             (STRCMP((a), ==, (b)))

#define NAME2(X, Y)     X##Y
#define NAME3(X, Y, Z)  X##Y##Z

typedef UINTPTR HL_HEAPID;
#define HL_NULL_HEAPID 0

typedef UINTPTR QUERY_ID;
#define NULL_QUERY_ID ((QUERY_ID) (~0))

#if defined(WINDOWS)
#define SSIZEOF(val) ((SSIZE_T) sizeof(val))
#else
#define SSIZEOF(val) ((ssize_t) sizeof(val))
#endif

/* TODO: rethink to use __WORDSIZE */
#if defined(WINDOWS)
#if defined(_WIN64)
#define __WORDSIZE 64
#else
#define __WORDSIZE 32
#endif
#endif

#if defined(_POWER)
#define __powerpc__
#endif

#endif /* _SYSTEM_H_ */
