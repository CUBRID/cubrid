/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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


/*
 * object_representation.h - Definitions related to the representation of
 *        objects on disk and in memory.
 *        his file is shared by both the client and server.
 */

#ifndef _OBJECT_REPRESENTATION_H_
#define _OBJECT_REPRESENTATION_H_

#ident "$Id$"

#include <setjmp.h>
#include <time.h>
#include <float.h>
#include <limits.h>
#if !defined(WINDOWS)
#include <netinet/in.h>
#endif /* !WINDOWS */

#include "error_manager.h"
#include "storage_common.h"
#include "oid.h"
#include "dbtype.h"
#include "byte_order.h"
#include "memory_alloc.h"
#include "sha1.h"

/*
 * NUMERIC TYPE SIZES
 *
 * These constants define the byte sizes for the fundamental
 * primitives types as represented in memory and on disk.
 * WARNING: The disk size for the "short" type is actually the same
 * as integer since there is no intelligent attribute packing at this
 * time.
 */
#define OR_BYTE_SIZE            1
#define OR_SHORT_SIZE           2
#define OR_INT_SIZE             4
#define OR_BIGINT_SIZE          8
#define OR_FLOAT_SIZE           4
#define OR_DOUBLE_SIZE          8

#define OR_BIGINT_ALIGNED_SIZE  (OR_BIGINT_SIZE + MAX_ALIGNMENT)
#define OR_DOUBLE_ALIGNED_SIZE  (OR_DOUBLE_SIZE + MAX_ALIGNMENT)
#define OR_PTR_ALIGNED_SIZE     (OR_PTR_SIZE + MAX_ALIGNMENT)
#define OR_VALUE_ALIGNED_SIZE(value)   \
  (or_db_value_size (value) + MAX_ALIGNMENT)

/*
 * DISK IDENTIFIER SIZES
 *
 * These constants describe the size and contents of various disk
 * identifiers as they are represented in a communication buffer.
 * The OID can also be used in an attribute value.
 */

#define OR_OID_SIZE             8
#define OR_OID_PAGEID           0
#define OR_OID_SLOTID           4
#define OR_OID_VOLID            6

#define OR_VPID_SIZE		6
#define OR_VPID_PAGEID		0
#define OR_VPID_VOLID		4

#define OR_HFID_SIZE            12
#define OR_HFID_PAGEID          0
#define OR_HFID_VFID_FILEID     4
#define OR_HFID_VFID_VOLID      8

#define OR_BTID_SIZE            10
#define OR_BTID_ALIGNED_SIZE    (OR_BTID_SIZE + OR_SHORT_SIZE)
#define OR_BTID_PAGEID          0
#define OR_BTID_VFID_FILEID     4
#define OR_BTID_VFID_VOLID      8

#define OR_EHID_SIZE            12
#define OR_EHID_VOLID           0
#define OR_EHID_FILEID          4
#define OR_EHID_PAGEID          8

#define OR_LOG_LSA_SIZE         10
#define OR_LOG_LSA_ALIGNED_SIZE (OR_LOG_LSA_SIZE + OR_SHORT_SIZE)
#define OR_LOG_LSA_PAGEID       0
#define OR_LOG_LSA_OFFSET       8

/*
 * EXTENDED TYPE SIZES
 *
 * These define the sizes and contents of the primitive types that
 * are not simple numeric types.
 */
#define OR_TIME_SIZE            4
#define OR_UTIME_SIZE           4
#define OR_DATE_SIZE            4

#define OR_TIMETZ_SIZE		(OR_TIME_SIZE + sizeof (TZ_ID))
#define OR_TIMETZ_TZID		4

#define OR_DATETIME_SIZE        8
#define OR_DATETIME_DATE        0
#define OR_DATETIME_TIME        4

#define OR_TIMESTAMPTZ_SIZE	(OR_UTIME_SIZE + sizeof (TZ_ID))
#define OR_TIMESTAMPTZ_TZID	4

#define OR_DATETIMETZ_SIZE	(OR_DATETIME_SIZE + sizeof (TZ_ID))
#define OR_DATETIMETZ_TZID	8

#define OR_MONETARY_SIZE        12
#define OR_MONETARY_TYPE        0
#define OR_MONETARY_AMOUNT      4
#define OR_ELO_LENGTH_SIZE	4
#define OR_ELO_HEADER_SIZE	(OR_ELO_LENGTH_SIZE)

#define OR_SHA1_SIZE		(5 * OR_INT_SIZE)

/* NUMERIC RANGES */
#define OR_MAX_BYTE 127
#define OR_MIN_BYTE -128

#define OR_MAX_SHORT_UNSIGNED 65535	/* 0xFFFF */
#define OR_MAX_SHORT 32767	/* 0x7FFF */
#define OR_MIN_SHORT -32768	/* 0x8000 */

#define OR_MAX_INT 2147483647	/* 0x7FFFFFFF */
#define OR_MIN_INT -2147483648	/* 0x80000000 */

/* OVERFLOW CHECK MACROS */

#define OR_CHECK_ASSIGN_OVERFLOW(dest, src) \
  (((src) > 0 && (dest) < 0) || ((src) < 0 && (dest) > 0))
#define OR_CHECK_ADD_OVERFLOW(a, b, c) \
  (((a) > 0 && (b) > 0 && (c) < 0) \
   || ((a) < 0 && (b) < 0 && (c) >= 0))
#define OR_CHECK_UNS_ADD_OVERFLOW(a, b, c) \
  (c) < (a) || (c) < (b)
#define OR_CHECK_SUB_UNDERFLOW(a, b, c) \
  (((a) < (b) && (c) > 0) \
   || ((a) > (b) && (c) < 0))
#define OR_CHECK_UNS_SUB_UNDERFLOW(a, b, c) \
  (b) > (a)
#define OR_CHECK_MULT_OVERFLOW(a, b, c) \
  (((b) == 0) ? ((c) != 0) : ((c) / (b) != (a)))
#define OR_CHECK_SHORT_DIV_OVERFLOW(a, b) \
  ((a) == DB_INT16_MIN && (b) == -1)
#define OR_CHECK_INT_DIV_OVERFLOW(a, b) \
  ((a) == DB_INT32_MIN && (b) == -1)
#define OR_CHECK_BIGINT_DIV_OVERFLOW(a, b) \
  ((a) == DB_BIGINT_MIN && (b) == -1)

#define OR_CHECK_SHORT_OVERFLOW(i)  ((i) > DB_INT16_MAX || (i) < DB_INT16_MIN)
#define OR_CHECK_INT_OVERFLOW(i)    ((i) > DB_INT32_MAX || (i) < DB_INT32_MIN)
#define OR_CHECK_BIGINT_OVERFLOW(i) ((i) > DB_BIGINT_MAX || (i) < DB_BIGINT_MIN)
#define OR_CHECK_USHRT_OVERFLOW(i)  ((i) > DB_UINT16_MAX || (i) < 0)
#define OR_CHECK_UINT_OVERFLOW(i)   ((i) > DB_UINT32_MAX || (i) < 0)

#define OR_CHECK_FLOAT_OVERFLOW(i)         ((i) > FLT_MAX || (-(i)) > FLT_MAX)
#define OR_CHECK_DOUBLE_OVERFLOW(i)        ((i) > DBL_MAX || (-(i)) > DBL_MAX)

/* PACK/UNPACK MACROS */

#define OR_GET_BYTE(ptr) \
  (*(unsigned char *) ((char *) (ptr)))
#define OR_GET_SHORT(ptr) \
  ((short) ntohs (*(short *) ((char *) (ptr))))
#define OR_GET_INT(ptr) \
  ((int) ntohl (*(int *) ((char *) (ptr))))
#define OR_GET_FLOAT(ptr, value) \
  (*(value) = ntohf (*(UINT32 *) (ptr)))
#define OR_GET_DOUBLE(ptr, value) \
  (*(value) = ntohd (*(UINT64 *) (ptr)))
#define OR_GET_STRING(ptr) \
  ((char *) ((char *) (ptr)))

#define OR_PUT_BYTE(ptr, val) \
  (*((unsigned char *) (ptr)) = (unsigned char) (val))
#define OR_PUT_SHORT(ptr, val) \
  (*(short *) ((char *) (ptr)) = htons ((short) (val)))
#define OR_PUT_INT(ptr, val) \
  (*(int *) ((char *) (ptr)) = htonl ((int) (val)))
#define OR_PUT_FLOAT(ptr, val) \
  (*(UINT32 *) (ptr) = htonf (*(float*) (val)))
#define OR_PUT_DOUBLE(ptr, val) \
  (*(UINT64 *) (ptr) = htond (*(double *) (val)))

#define OR_GET_BIG_VAR_OFFSET(ptr) 	OR_GET_INT (ptr)	/* 4byte */
#define OR_PUT_BIG_VAR_OFFSET(ptr, val)	OR_PUT_INT (ptr, val)	/* 4byte */

#define OR_PUT_OFFSET(ptr, val) \
  OR_PUT_OFFSET_INTERNAL(ptr, val, BIG_VAR_OFFSET_SIZE)

#define OR_PUT_OFFSET_INTERNAL(ptr, val, offset_size) \
  do { \
    if (offset_size == OR_BYTE_SIZE) \
      { \
        OR_PUT_BYTE(ptr, val); \
      } \
    else if (offset_size == OR_SHORT_SIZE) \
      { \
        OR_PUT_SHORT(ptr, val); \
      } \
    else if (offset_size == OR_INT_SIZE) \
      { \
        OR_PUT_INT(ptr, val); \
      } \
  } while (0)

#define OR_GET_OFFSET(ptr) \
  OR_GET_OFFSET_INTERNAL (ptr, BIG_VAR_OFFSET_SIZE)

#define OR_GET_OFFSET_INTERNAL(ptr, offset_size) \
  (offset_size == OR_BYTE_SIZE) \
   ? OR_GET_BYTE (ptr) \
   : ((offset_size == OR_SHORT_SIZE) \
      ? OR_GET_SHORT (ptr) : OR_GET_INT (ptr))

#define OR_MOVE_MONETARY(src, dst) \
  do { \
    OR_MOVE_DOUBLE (src, dst); \
    ((DB_MONETARY *) dst)->type = ((DB_MONETARY *) src)->type; \
  } while (0)

#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN

#define swap64(x)  \
  ((((unsigned long long) (x) & (0x00000000000000FFULL)) << 56) \
   | (((unsigned long long) (x) & (0xFF00000000000000ULL)) >> 56) \
   | (((unsigned long long) (x) & (0x000000000000FF00ULL)) << 40) \
   | (((unsigned long long) (x) & (0x00FF000000000000ULL)) >> 40) \
   | (((unsigned long long) (x) & (0x0000000000FF0000ULL)) << 24) \
   | (((unsigned long long) (x) & (0x0000FF0000000000ULL)) >> 24) \
   | (((unsigned long long) (x) & (0x00000000FF000000ULL)) << 8) \
   | (((unsigned long long) (x) & (0x000000FF00000000ULL)) >> 8))

#else /* OR_BYTE_ORDER == OR_LITTLE_ENDIAN */
#define swap64(x)        (x)
#endif /* OR_BYTE_ORDER == OR_LITTLE_ENDIAN */

#if __WORDSIZE == 32
#define OR_PTR_SIZE             4
#define OR_PUT_PTR(ptr, val)    OR_PUT_INT ((ptr), (val))
#define OR_GET_PTR(ptr)         OR_GET_INT ((ptr))
#else /* __WORDSIZE == 32 */
#define OR_PTR_SIZE             8
#define OR_PUT_PTR(ptr, val)    (*(UINTPTR *) ((char *) (ptr)) = swap64 ((UINTPTR) val))
#define OR_GET_PTR(ptr)         ((UINTPTR) swap64 (*(UINTPTR *) ((char *) (ptr))))
#endif /* __WORDSIZE == 32 */

#define OR_INT64_SIZE           8

/* EXTENDED TYPES */

#define OR_PUT_BIGINT(ptr, val)  OR_PUT_INT64 (ptr, val)
#define OR_GET_BIGINT(ptr, val)  OR_GET_INT64 (ptr, val)

#define OR_GET_INT64(ptr, val) \
  do { \
    INT64 packed_value; \
    memcpy (&packed_value, ptr, OR_INT64_SIZE); \
    *((INT64*) (val)) = ((INT64) swap64 (packed_value)); \
  } while (0)

#define OR_PUT_INT64(ptr, val) \
  do { \
    INT64 packed_value; \
    packed_value = ((INT64) swap64 (*(INT64*) val)); \
    memcpy (ptr, &packed_value, OR_INT64_SIZE);\
  } while (0)

#define OR_GET_TIME(ptr, value) \
  *((DB_TIME *) (value)) = OR_GET_INT (ptr)

#define OR_PUT_TIME(ptr, value) \
  OR_PUT_INT (ptr, *((DB_TIME *) (value)))

#define OR_GET_TIMETZ(ptr, time_tz) \
  do { \
    (time_tz)->time = OR_GET_INT ((char *) (ptr)); \
    (time_tz)->tz_id = OR_GET_INT (((char *) (ptr)) + OR_TIMETZ_TZID); \
  } while (0)

#define OR_PUT_TIMETZ(ptr, time_tz) \
  do { \
    OR_PUT_INT (((char *) ptr), (time_tz)->time); \
    OR_PUT_INT (((char *) ptr) + OR_TIMETZ_TZID, (time_tz)->tz_id); \
  } while (0)

#define OR_GET_UTIME(ptr, value) \
  *((DB_UTIME *) (value)) = OR_GET_INT (ptr)

#define OR_PUT_UTIME(ptr, value) \
  OR_PUT_INT (ptr, *((DB_UTIME *) (value)))

#define OR_GET_TIMESTAMPTZ(ptr, ts_tz) \
  do { \
    (ts_tz)->timestamp = OR_GET_INT ((char *) (ptr)); \
    (ts_tz)->tz_id = OR_GET_INT (((char *) (ptr)) + OR_TIMESTAMPTZ_TZID); \
  } while (0)

#define OR_PUT_TIMESTAMPTZ(ptr, ts_tz) \
  do { \
    OR_PUT_INT (((char *) ptr), (ts_tz)->timestamp); \
    OR_PUT_INT (((char *) ptr) + OR_TIMESTAMPTZ_TZID, (ts_tz)->tz_id); \
  } while (0)

#define OR_GET_DATE(ptr, value) \
  *((DB_DATE *) (value)) = OR_GET_INT (ptr)

#define OR_PUT_DATE(ptr, value) \
  OR_PUT_INT (ptr, *((DB_DATE *) (value)))

#define OR_GET_DATETIME(ptr, datetime) \
  do { \
    (datetime)->date = OR_GET_INT (((char *) (ptr)) + OR_DATETIME_DATE); \
    (datetime)->time = OR_GET_INT (((char *) (ptr)) + OR_DATETIME_TIME); \
  } while (0)

#define OR_PUT_DATETIME(ptr, datetime) \
  do { \
    OR_PUT_INT (((char *)ptr) + OR_DATETIME_DATE, (datetime)->date); \
    OR_PUT_INT (((char *)ptr) + OR_DATETIME_TIME, (datetime)->time); \
  } while (0)

#define OR_GET_DATETIMETZ(ptr, datetimetz) \
  do { \
    OR_GET_DATETIME ((char *) ptr, \
		     &((DB_DATETIMETZ *) datetimetz)->datetime); \
    (datetimetz)->tz_id = OR_GET_INT (((char *) (ptr)) + OR_DATETIMETZ_TZID); \
  } while (0)

#define OR_PUT_DATETIMETZ(ptr, datetimetz) \
  do { \
    OR_PUT_DATETIME (((char *) ptr), \
		     &((DB_DATETIMETZ *) datetimetz)->datetime); \
    OR_PUT_INT (((char *) ptr) + OR_DATETIMETZ_TZID, (datetimetz)->tz_id); \
  } while (0)

#define OR_GET_MONETARY(ptr, value) \
  do { \
    double pack_value; \
    (value)->type = (DB_CURRENCY) OR_GET_INT (((char *) (ptr)) + OR_MONETARY_TYPE); \
    memcpy ((char *) (&pack_value), ((char *) (ptr)) + OR_MONETARY_AMOUNT, OR_DOUBLE_SIZE); \
    OR_GET_DOUBLE (&pack_value, &(value)->amount); \
  } while (0)

#define OR_GET_CURRENCY_TYPE(ptr) \
  (DB_CURRENCY) OR_GET_INT (((char *) (ptr)) + OR_MONETARY_TYPE)

#define OR_PUT_MONETARY(ptr, value) \
  do { \
    double pack_value; \
    OR_PUT_INT (((char *) (ptr)) + OR_MONETARY_TYPE, (int) (value)->type); \
    OR_PUT_DOUBLE (&pack_value, &((value)->amount)); \
    memcpy (((char *) (ptr)) + OR_MONETARY_AMOUNT, &pack_value, OR_DOUBLE_SIZE); \
  } while (0)

/* Sha1 */
#define OR_GET_SHA1(ptr, value) \
  do { \
    int i = 0; \
    for (; i < 5; i++) \
      { \
	((SHA1Hash *) (value))->h[i] = (INT32) OR_GET_INT (ptr + i * OR_INT_SIZE); \
      } \
  } while (0)
#define OR_PUT_SHA1(ptr, value) \
  do { \
    int i = 0; \
    for (; i < 5; i++) \
      { \
	OR_PUT_INT (ptr + i * OR_INT_SIZE, ((SHA1Hash *) (value))->h[i]); \
      } \
  } while (0)

/* DISK IDENTIFIERS */

#define OR_GET_OID(ptr, oid) \
  do { \
    (oid)->pageid = OR_GET_INT (((char *) (ptr)) + OR_OID_PAGEID); \
    (oid)->slotid = OR_GET_SHORT (((char *) (ptr)) + OR_OID_SLOTID); \
    (oid)->volid  = OR_GET_SHORT (((char *) (ptr)) + OR_OID_VOLID); \
  } while (0)

#define OR_PUT_OID(ptr, oid) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_OID_PAGEID, (oid)->pageid); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_OID_SLOTID, (oid)->slotid); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_OID_VOLID, (oid)->volid); \
  } while (0)

#define OR_GET_VPID(ptr, vpid) \
  do { \
    (vpid)->pageid = OR_GET_INT (((char *) (ptr)) + OR_VPID_PAGEID); \
    (vpid)->volid = OR_GET_SHORT (((char *) (ptr)) + OR_VPID_VOLID); \
  } while (0)

#define OR_PUT_VPID(ptr, vpid) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_VPID_PAGEID, (vpid)->pageid); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_VPID_VOLID, (vpid)->volid); \
  } while (0)
#define OR_PUT_VPID_ALIGNED(ptr, vpid) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_VPID_PAGEID, (vpid)->pageid); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_VPID_VOLID, (vpid)->volid); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_VPID_SIZE, 0); \
  } while (0)

#define OR_PUT_NULL_OID(ptr) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_OID_PAGEID, NULL_PAGEID); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_OID_SLOTID, 0); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_OID_VOLID,  0); \
  } while (0)

#define OR_GET_HFID(ptr, hfid) \
  do { \
    (hfid)->hpgid = OR_GET_INT (((char *) (ptr)) + OR_HFID_PAGEID); \
    (hfid)->vfid.fileid = OR_GET_INT (((char *) (ptr)) + OR_HFID_VFID_FILEID); \
    (hfid)->vfid.volid = OR_GET_INT (((char *) (ptr)) + OR_HFID_VFID_VOLID); \
  } while (0)

#define OR_PUT_HFID(ptr, hfid) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_HFID_PAGEID, (hfid)->hpgid); \
    OR_PUT_INT (((char *) (ptr)) + OR_HFID_VFID_FILEID, (hfid)->vfid.fileid); \
    OR_PUT_INT (((char *) (ptr)) + OR_HFID_VFID_VOLID, (hfid)->vfid.volid); \
  } while (0)

#define OR_PUT_NULL_HFID(ptr) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_HFID_PAGEID, -1); \
    OR_PUT_INT (((char *) (ptr)) + OR_HFID_VFID_FILEID, -1); \
    OR_PUT_INT (((char *) (ptr)) + OR_HFID_VFID_VOLID, -1); \
  } while (0)

#define OR_GET_BTID(ptr, btid) \
  do { \
    (btid)->root_pageid = OR_GET_INT (((char *) (ptr)) + OR_BTID_PAGEID); \
    (btid)->vfid.fileid = OR_GET_INT (((char *) (ptr)) + OR_BTID_VFID_FILEID); \
    (btid)->vfid.volid  = OR_GET_SHORT (((char *) (ptr)) + OR_BTID_VFID_VOLID); \
  } while (0)

#define OR_PUT_BTID(ptr, btid) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_BTID_PAGEID, (btid)->root_pageid); \
    OR_PUT_INT (((char *) (ptr)) + OR_BTID_VFID_FILEID, (btid)->vfid.fileid); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_BTID_VFID_VOLID, (btid)->vfid.volid); \
  } while (0)

#define OR_PUT_NULL_BTID(ptr) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_BTID_PAGEID, NULL_PAGEID); \
    OR_PUT_INT (((char *) (ptr)) + OR_BTID_VFID_FILEID, NULL_FILEID); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_BTID_VFID_VOLID, NULL_VOLID); \
  } while (0)

#define OR_GET_EHID(ptr, ehid) \
  do { \
    (ehid)->vfid.volid = OR_GET_INT (((char *) (ptr)) + OR_EHID_VOLID); \
    (ehid)->vfid.fileid = OR_GET_INT (((char *) (ptr)) + OR_EHID_FILEID); \
    (ehid)->pageid = OR_GET_INT (((char *) (ptr)) + OR_EHID_PAGEID); \
  } while (0)

#define OR_PUT_EHID(ptr, ehid) \
  do { \
    OR_PUT_INT (((char *) (ptr)) + OR_EHID_VOLID,  (ehid)->vfid.volid); \
    OR_PUT_INT (((char *) (ptr)) + OR_EHID_FILEID, (ehid)->vfid.fileid); \
    OR_PUT_INT (((char *) (ptr)) + OR_EHID_PAGEID, (ehid)->pageid); \
  } while (0)

#define OR_GET_LOG_LSA(ptr, lsa) \
  do { \
    INT64 value; \
    OR_GET_INT64 (((char *) (ptr)) + OR_LOG_LSA_PAGEID, &value); \
    (lsa)->pageid = value; \
    (lsa)->offset = OR_GET_SHORT (((char *) (ptr)) + OR_LOG_LSA_OFFSET); \
  } while (0)

#define OR_PUT_LOG_LSA(ptr, lsa) \
  do { \
    INT64 pageid = (lsa)->pageid; \
    OR_PUT_INT64 (((char *) (ptr)) + OR_LOG_LSA_PAGEID, &pageid); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_LOG_LSA_OFFSET, (lsa)->offset); \
  } while (0)

#define OR_PUT_NULL_LOG_LSA(ptr) \
  do { \
    INT64 pageid = -1; \
    OR_PUT_INT64 (((char *) (ptr)) + OR_LOG_LSA_PAGEID, &pageid); \
    OR_PUT_SHORT (((char *) (ptr)) + OR_LOG_LSA_OFFSET, -1); \
  } while (0)

/*
 * VARIABLE OFFSET TABLE ACCESSORS
 * The variable offset table is present in the headers of objects and sets.
 */

#define OR_VAR_TABLE_SIZE(vars) \
  (OR_VAR_TABLE_SIZE_INTERNAL (vars, BIG_VAR_OFFSET_SIZE))

#define OR_VAR_TABLE_SIZE_INTERNAL(vars, offset_size) \
  (((vars) == 0) ? 0 : DB_ALIGN ((offset_size * ((vars) + 1)), INT_ALIGNMENT))

#define OR_VAR_TABLE_ELEMENT_PTR(table, index, offset_size) \
  ((offset_size == OR_BYTE_SIZE) \
   ? (&((char *) (table))[(index)]) \
   : ((offset_size == OR_SHORT_SIZE) \
      ? ((char *) (&((short *) (table))[(index)])) \
      : ((char *) (&((int *) (table))[(index)]))))

#define OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL(table, index, offset_size) \
  ((offset_size == OR_BYTE_SIZE) \
   ? (OR_GET_BYTE (OR_VAR_TABLE_ELEMENT_PTR (table, index, offset_size))) \
   : ((offset_size == OR_SHORT_SIZE) \
      ? (OR_GET_SHORT (OR_VAR_TABLE_ELEMENT_PTR (table, index, offset_size))) \
      : (OR_GET_INT (OR_VAR_TABLE_ELEMENT_PTR (table, index, offset_size)))))

#define OR_VAR_TABLE_ELEMENT_LENGTH_INTERNAL(table, index, offset_size) \
  (OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (table, (index) + 1, offset_size) \
   - OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (table, (index), offset_size))

/* ATTRIBUTE LOCATION */

#define OR_FIXED_ATTRIBUTES_OFFSET(ptr, nvars) \
  (OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL (ptr, nvars, BIG_VAR_OFFSET_SIZE))

#define OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL(ptr, nvars, offset_size) \
  (OR_HEADER_SIZE (ptr) + OR_VAR_TABLE_SIZE_INTERNAL (nvars, offset_size))

/* OBJECT HEADER LAYOUT */
/* header fixed-size in non-MVCC only, in MVCC the header has variable size */

/* representation id, CHN, MVCC insert id, MVCC delete id, prev_version_lsa = 32 */
#define OR_MVCC_MAX_HEADER_SIZE  32

/* representation id and CHN */
#define OR_MVCC_MIN_HEADER_SIZE  8

/* representation id, MVCC insert id and CHN */
#define OR_MVCC_INSERT_HEADER_SIZE  16

#define OR_NON_MVCC_HEADER_SIZE	      (8)	/* two integers */
#define OR_HEADER_SIZE(ptr) (or_header_size ((char *) (ptr)))

/* 01 stand for 1byte, 10-> 2byte, 11-> 4byte  */
#define OR_OFFSET_SIZE_FLAG 0x60000000
#define OR_OFFSET_SIZE_1BYTE 0x20000000
#define OR_OFFSET_SIZE_2BYTE 0x40000000
#define OR_OFFSET_SIZE_4BYTE 0x60000000

/* Use for MVCC flags the remainder of 5 bits in the first byte. */
/* Flag will be shifter by 24 bits to the right */
#define OR_MVCC_FLAG_MASK	    0x1f
#define OR_MVCC_FLAG_SHIFT_BITS	    24

/* The following flags are used for dynamic MVCC information */
/* The record contains MVCC insert id */
#define OR_MVCC_FLAG_VALID_INSID	  0x01

/* The record contains MVCC delete id. If not set, the record contains chn */
#define OR_MVCC_FLAG_VALID_DELID	  0x02

/* The record have an LSA with the location of the previous version */
#define OR_MVCC_FLAG_VALID_PREV_VERSION   0x04

#define OR_MVCC_REPID_MASK	  0x00FFFFFF

/* representation offset in MVCC and non-MVCC. In MVCC the representation
 * contains flags that allow to compute header size and CHN offset.
 */

#define OR_REP_OFFSET    0
#define OR_MVCC_REP_SIZE 4

#define OR_MVCC_FLAG_OFFSET OR_REP_OFFSET
#define OR_MVCC_FLAG_SIZE OR_MVCC_REP_SIZE

#define OR_CHN_OFFSET (OR_REP_OFFSET + OR_MVCC_REP_SIZE)
#define OR_CHN_SIZE 4

#define OR_MVCC_INSERT_ID_OFFSET (OR_CHN_OFFSET + OR_CHN_SIZE)
#define OR_MVCC_INSERT_ID_SIZE 8

#define OR_MVCC_DELETE_ID_OFFSET(mvcc_flags) \
  (OR_MVCC_INSERT_ID_OFFSET + (((mvcc_flags) & OR_MVCC_FLAG_VALID_INSID) ? OR_MVCC_INSERT_ID_SIZE : 0))
#define OR_MVCC_DELETE_ID_SIZE 8

#define OR_MVCC_PREV_VERSION_LSA_OFFSET(mvcc_flags) \
  (OR_MVCC_DELETE_ID_OFFSET(mvcc_flags) + (((mvcc_flags) & OR_MVCC_FLAG_VALID_DELID) ? OR_MVCC_DELETE_ID_SIZE : 0))
#define OR_MVCC_PREV_VERSION_LSA_SIZE 8

/* MVCC */
#define OR_MVCCID_SIZE			OR_BIGINT_SIZE
#define OR_PUT_MVCCID			OR_PUT_BIGINT
#define OR_GET_MVCCID			OR_GET_BIGINT

/* In case MVCC is enabled and chn is needed it will be saved instead of
 * delete MVCC id.
 */

/* high bit of the repid word is reserved for the bound bit flag,
   need to keep representations from going negative ! */
#define OR_BOUND_BIT_FLAG   0x80000000

#define BIG_VAR_OFFSET_SIZE OR_INT_SIZE	/* 4byte */

/* OBJECT HEADER ACCESS MACROS */

#define OR_GET_REPID(ptr) \
  ((OR_GET_INT ((ptr) + OR_REP_OFFSET)) & ~OR_BOUND_BIT_FLAG & ~OR_OFFSET_SIZE_FLAG)

#define OR_GET_BOUND_BIT_FLAG(ptr) \
  ((OR_GET_INT ((ptr) + OR_REP_OFFSET)) & OR_BOUND_BIT_FLAG)

#define OR_GET_OFFSET_SIZE(ptr) \
  ((((OR_GET_INT (((char *) (ptr)) + OR_REP_OFFSET)) & OR_OFFSET_SIZE_FLAG) == OR_OFFSET_SIZE_1BYTE) \
     ? OR_BYTE_SIZE \
     : ((((OR_GET_INT (((char *) (ptr)) + OR_REP_OFFSET)) & OR_OFFSET_SIZE_FLAG) == OR_OFFSET_SIZE_2BYTE) \
          ? OR_SHORT_SIZE : OR_INT_SIZE))

#define OR_SET_VAR_OFFSET_SIZE(val, offset_size) \
  (((offset_size) == OR_BYTE_SIZE) \
   ? ((val) |= OR_OFFSET_SIZE_1BYTE) \
   : (((offset_size) == OR_SHORT_SIZE) \
      ? ((val) |= OR_OFFSET_SIZE_2BYTE) \
      : ((val) |= OR_OFFSET_SIZE_4BYTE)))

/* MVCC OBJECT HEADER ACCESS MACROS */
#define OR_GET_MVCC_INSERT_ID(ptr, mvcc_flags, valp) \
  ((((mvcc_flags) & OR_MVCC_FLAG_VALID_INSID) == 0) \
    ? MVCCID_ALL_VISIBLE \
    : (OR_GET_BIGINT (((char *) (ptr)) + OR_MVCC_INSERT_ID_OFFSET, (valp))))

#define OR_GET_MVCC_DELETE_ID(ptr, mvcc_flags, valp)  \
  ((((mvcc_flags) & OR_MVCC_FLAG_VALID_DELID) == 0) \
    ? MVCCID_NULL \
    : (OR_GET_BIGINT (((char *) (ptr)) + OR_MVCC_DELETE_ID_OFFSET(mvcc_flags), (valp))))

#define OR_GET_MVCC_REPID(ptr)	\
  ((OR_GET_INT(((char *) (ptr)) + OR_REP_OFFSET)) \
   & OR_MVCC_REPID_MASK)

#define OR_GET_MVCC_CHN(ptr) (OR_GET_INT ((char *) (ptr) + OR_CHN_OFFSET))

#define OR_GET_MVCC_FLAG(ptr) \
  (((OR_GET_INT (((char *) (ptr)) + OR_REP_OFFSET)) \
    >> OR_MVCC_FLAG_SHIFT_BITS) & OR_MVCC_FLAG_MASK)

#define OR_GET_MVCC_REPID_AND_FLAG(ptr) \
  (OR_GET_INT (((char *) (ptr)) + OR_REP_OFFSET))

/* VARIABLE OFFSET TABLE ACCESSORS */

#define OR_GET_OBJECT_VAR_TABLE(obj) \
  ((short *) (((char *) (obj)) + OR_HEADER_SIZE ((char *) (obj))))

#define OR_VAR_ELEMENT_PTR(obj, index) \
  (OR_VAR_TABLE_ELEMENT_PTR (OR_GET_OBJECT_VAR_TABLE (obj), index, OR_GET_OFFSET_SIZE (obj)))

#define OR_VAR_OFFSET(obj, index) \
  (OR_HEADER_SIZE (obj)	\
   + OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (OR_GET_OBJECT_VAR_TABLE (obj), \
                                           index, OR_GET_OFFSET_SIZE (obj)))

#define OR_VAR_IS_NULL(obj, index) \
  ((OR_VAR_TABLE_ELEMENT_LENGTH_INTERNAL (OR_GET_OBJECT_VAR_TABLE (obj), \
                                          index, OR_GET_OFFSET_SIZE (obj))) ? 0 : 1)

#define OR_VAR_LENGTH(length, obj, index, n_variables) \
  do { \
    int _this_offset, _next_offset, _temp_offset, _nth_var; \
    _this_offset = OR_VAR_OFFSET(obj, index); \
    _next_offset = OR_VAR_OFFSET(obj, index + 1); \
    if ((length = _next_offset - _this_offset) != 0) \
      { \
        _next_offset = 0; \
        for (_nth_var = 0; _nth_var <= n_variables; _nth_var++) \
          { \
            _temp_offset = OR_VAR_OFFSET(obj, _nth_var); \
            if (_temp_offset > _this_offset ) \
              { \
                if (_next_offset == 0) \
                  { \
                    _next_offset = _temp_offset; \
                  } \
                else if (_temp_offset < _next_offset) \
                  { \
                    _next_offset = _temp_offset; \
		  } \
              } \
          } \
        length = _next_offset - _this_offset; \
      } \
  } while (0)

/*
 * BOUND BIT ACCESSORS.
 * Note that these are assuming 4 byte integers to avoid a divide operation.
 */

#define OR_BOUND_BIT_WORDS(count) (((count) + 31) >> 5)
#define OR_BOUND_BIT_BYTES(count) ((((count) + 31) >> 5) * 4)

#define OR_BOUND_BIT_MASK(element) (1 << ((int) (element) & 7))

#define OR_GET_BOUND_BIT_BYTE(bitptr, element) \
  ((char *) (bitptr) + ((int) (element) >> 3))

#define OR_GET_BOUND_BIT(bitptr, element) \
  ((*OR_GET_BOUND_BIT_BYTE ((bitptr), (element))) & OR_BOUND_BIT_MASK ((element)))

#define OR_GET_BOUND_BITS(obj, nvars, fsize) \
  (char *) (((char *) (obj)) \
            + OR_HEADER_SIZE ((char *) (obj)) \
            + OR_VAR_TABLE_SIZE_INTERNAL ((nvars), OR_GET_OFFSET_SIZE (obj)) + (fsize))

/* These are the most useful ones if we're only testing a single attribute */

#define OR_FIXED_ATT_IS_BOUND(obj, nvars, fsize, position) \
  (!OR_GET_BOUND_BIT_FLAG (obj) || OR_GET_BOUND_BIT (OR_GET_BOUND_BITS (obj, nvars, fsize), position))

#define OR_FIXED_ATT_IS_UNBOUND(obj, nvars, fsize, position) \
  (OR_GET_BOUND_BIT_FLAG (obj) && !OR_GET_BOUND_BIT (OR_GET_BOUND_BITS (obj, nvars, fsize), position))

#define OR_ENABLE_BOUND_BIT(bitptr, element) \
  *OR_GET_BOUND_BIT_BYTE (bitptr, element) = *OR_GET_BOUND_BIT_BYTE (bitptr, element) | OR_BOUND_BIT_MASK (element)

#define OR_CLEAR_BOUND_BIT(bitptr, element) \
  *OR_GET_BOUND_BIT_BYTE (bitptr, element) = *OR_GET_BOUND_BIT_BYTE (bitptr, element) & ~OR_BOUND_BIT_MASK (element)

/* SET HEADER */

#define OR_SET_HEADER_SIZE 8
#define OR_SET_SIZE_OFFSET 4
/* optional header extension if the full domain is present */
#define OR_SET_DOMAIN_SIZE_OFFSET 8

/* Set header fields.
   These constants are used to construct and decompose the set header word. */
#define OR_SET_TYPE_MASK 	0xFF
#define OR_SET_ETYPE_MASK 	0xFF00
#define OR_SET_ETYPE_SHIFT 	8
#define OR_SET_BOUND_BIT 	0x10000
#define OR_SET_VARIABLE_BIT 	0x20000
#define OR_SET_DOMAIN_BIT	0x40000
#define OR_SET_TAG_BIT		0x80000
#define OR_SET_COMMON_SUB_BIT	0x100000

#define OR_SET_TYPE(setptr) \
  (DB_TYPE) ((OR_GET_INT ((char *) (setptr))) & OR_SET_TYPE_MASK)

#define OR_SET_ELEMENT_TYPE(setptr)  \
  (DB_TYPE) ((OR_GET_INT ((char *) (setptr)) & OR_SET_ETYPE_MASK) >> OR_SET_ETYPE_SHIFT)

#define OR_SET_HAS_BOUND_BITS(setptr) \
  (OR_GET_INT ((char *) (setptr)) & OR_SET_BOUND_BIT)

#define OR_SET_HAS_OFFSET_TABLE(setptr) \
  (OR_GET_INT ((char *) (setptr)) & OR_SET_VARIABLE_BIT)

#define OR_SET_HAS_DOMAIN(setptr) \
  (OR_GET_INT ((char *) (setptr)) & OR_SET_DOMAIN_BIT)

#define OR_SET_HAS_ELEMENT_TAGS(setptr) \
  (OR_GET_INT ((char *) (setptr)) & OR_SET_TAG_BIT)

#define OR_SET_ELEMENT_COUNT(setptr) \
  ((OR_GET_INT ((char *) (setptr) + OR_SET_SIZE_OFFSET)))

#define OR_SET_DOMAIN_SIZE(setptr) \
  ((OR_GET_INT ((char *) (setptr) + OR_SET_DOMAIN_SIZE_OFFSET)))

/*
 * SET VARIABLE OFFSET TABLE ACCESSORS.
 * Should make sure that the set actually has one before using.
 */
#define OR_GET_SET_VAR_TABLE(setptr) \
  ((int *) ((char *) (setptr) + OR_SET_HEADER_SIZE))

#define OR_SET_ELEMENT_OFFSET(setptr, element) \
  (OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (OR_GET_SET_VAR_TABLE (setptr), element, BIG_VAR_OFFSET_SIZE))

/*
 * SET BOUND BIT ACCESSORS
 *
 * Should make sure that the set actually has these before using.
 * Its essentially the same as OR_GET_SET_VAR_TABLE since these will
 * be in the same position and can't both appear at the same time.
 */

#define OR_GET_SET_BOUND_BITS(setptr) \
  (int *) ((char *) (setptr) + OR_SET_HEADER_SIZE)

/* MIDXKEY HEADER */

#define OR_MULTI_BOUND_BIT_BYTES(count)  (((count) + 7) >> 3)

#define OR_MULTI_BOUND_BIT_MASK(element) (1 << ((int) (element) & 7))

#define OR_MULTI_GET_BOUND_BIT_BYTE(bitptr, element) \
  ((char *)(bitptr) + ((int)(element) >> 3))

#define OR_MULTI_GET_BOUND_BIT(bitptr, element) \
  ((*OR_MULTI_GET_BOUND_BIT_BYTE(bitptr, element)) & OR_MULTI_BOUND_BIT_MASK(element))

#define OR_MULTI_GET_BOUND_BITS(bitptr, fsize) \
  (char *) (((char *) (bitptr)) + fsize)

#define OR_MULTI_ATT_IS_BOUND(bitptr, element) \
  OR_MULTI_GET_BOUND_BIT(bitptr, element)
#define OR_MULTI_ATT_IS_UNBOUND(bitptr, element) \
  (!OR_MULTI_GET_BOUND_BIT (bitptr, element))

#define OR_MULTI_ENABLE_BOUND_BIT(bitptr, element) \
  *OR_MULTI_GET_BOUND_BIT_BYTE (bitptr, element) = (*OR_MULTI_GET_BOUND_BIT_BYTE (bitptr, element) \
						    | OR_MULTI_BOUND_BIT_MASK (element))

#define OR_MULTI_CLEAR_BOUND_BIT(bitptr, element) \
  *OR_MULTI_GET_BOUND_BIT_BYTE (bitptr, element) = (*OR_MULTI_GET_BOUND_BIT_BYTE (bitptr, element) \
						    & ~OR_MULTI_BOUND_BIT_MASK (element))


/*
 * OR_SUB_HEADER_SIZE
 *
 * Used to tag each substructure.  Same as the object header currently.
 *   class oid, repid, flags
 */
#define OR_SUB_HEADER_SIZE 	OR_OID_SIZE + OR_INT_SIZE + OR_INT_SIZE

/*
 * OR_SUB_DOMAIN_AND_HEADER_SIZE
 *
 * Hack for the class transformer, since we always currently know what the
 * substructure lists contain, this allows us to skip over the packed domain
 * quickly.  Must match the stuff packed by or_put_sub_domain_and_header().
 */
#define OR_SUB_DOMAIN_SIZE	OR_INT_SIZE

/* VARIABLE HEADER */
#define OR_VARIABLE_HEADER_SIZE 4

#define OR_GET_VARIABLE_TYPE(ptr) (OR_GET_INT ((int *) (ptr)))


/* class */
enum
{
  ORC_REP_DIR_OFFSET = 8,
  ORC_HFID_FILEID_OFFSET = 16,
  ORC_HFID_VOLID_OFFSET = 20,
  ORC_HFID_PAGEID_OFFSET = 24,
  ORC_FIXED_COUNT_OFFSET = 28,
  ORC_VARIABLE_COUNT_OFFSET = 32,
  ORC_FIXED_LENGTH_OFFSET = 36,
  ORC_ATT_COUNT_OFFSET = 40,
  ORC_SHARED_COUNT_OFFSET = 48,
  ORC_CLASS_ATTR_COUNT_OFFSET = 60,
  ORC_CLASS_FLAGS = 64,
  ORC_CLASS_TYPE = 68
};

enum
{
  ORC_NAME_INDEX = 0,
  ORC_LOADER_COMMANDS_INDEX = 1,
  ORC_REPRESENTATIONS_INDEX = 2,
  ORC_SUBCLASSES_INDEX = 3,
  ORC_SUPERCLASSES_INDEX = 4,
  ORC_ATTRIBUTES_INDEX = 5,
  ORC_SHARED_ATTRS_INDEX = 6,
  ORC_CLASS_ATTRS_INDEX = 7,
  ORC_METHODS_INDEX = 8,
  ORC_CLASS_METHODS_INDEX = 9,
  ORC_METHOD_FILES_INDEX = 10,
  ORC_RESOLUTIONS_INDEX = 11,
  ORC_QUERY_SPEC_INDEX = 12,
  ORC_TRIGGERS_INDEX = 13,
  ORC_PROPERTIES_INDEX = 14,
  ORC_COMMENT_INDEX = 15,
  ORC_PARTITION_INDEX = 16,

  /* add a new one above */

  ORC_LAST_INDEX,

  ORC_CLASS_VAR_ATT_COUNT = ORC_LAST_INDEX
};

/* attribute */
enum
{
  ORC_ATT_ID_OFFSET = 0,
  ORC_ATT_TYPE_OFFSET = 4,
  ORC_ATT_DEF_ORDER_OFFSET = 12,
  ORC_ATT_CLASS_OFFSET = 16,
  ORC_ATT_FLAG_OFFSET = 24,
  ORC_ATT_INDEX_OFFSET = 28
};

enum
{
  ORC_ATT_NAME_INDEX = 0,
  ORC_ATT_CURRENT_VALUE_INDEX = 1,
  ORC_ATT_ORIGINAL_VALUE_INDEX = 2,
  ORC_ATT_DOMAIN_INDEX = 3,
  ORC_ATT_TRIGGER_INDEX = 4,
  ORC_ATT_PROPERTIES_INDEX = 5,
  ORC_ATT_COMMENT_INDEX = 6,

  /* add a new one above */

  ORC_ATT_LAST_INDEX,

  ORC_ATT_VAR_ATT_COUNT = ORC_ATT_LAST_INDEX
};

/* representation */
enum
{
  ORC_REP_ID_OFFSET = 0,
  ORC_REP_FIXED_COUNT_OFFSET = 4,
  ORC_REP_VARIABLE_COUNT_OFFSET = 8
};

enum
{
  ORC_REP_ATTRIBUTES_INDEX = 0,
  ORC_REP_PROPERTIES_INDEX = 1,

  /* add a new one above */

  ORC_REP_LAST_INDEX,

  ORC_REP_VAR_ATT_COUNT = ORC_REP_LAST_INDEX
};

/* rep_attribute */
enum
{
  ORC_REPATT_ID_OFFSET = 0,
  ORC_REPATT_TYPE_OFFSET = 4
};

enum
{
  ORC_REPATT_DOMAIN_INDEX = 0,

  /* add a new one above */

  ORC_REPATT_LAST_INDEX,

  ORC_REPATT_VAR_ATT_COUNT = ORC_REPATT_LAST_INDEX
};

/* domain */
enum
{
  ORC_DOMAIN_TYPE_OFFSET = 0,
  ORC_DOMAIN_PRECISION_OFFSET = 4,
  ORC_DOMAIN_SCALE_OFFSET = 8,
  ORC_DOMAIN_CODESET_OFFSET = 12,
  ORC_DOMAIN_COLLATION_ID_OFFSET = 16,
  ORC_DOMAIN_CLASS_OFFSET = 20
};

enum
{
  ORC_DOMAIN_SETDOMAIN_INDEX = 0,
  ORC_DOMAIN_ENUMERATION_INDEX = 1,

  /* add a new one above */

  ORC_DOMAIN_LAST_INDEX,

  ORC_DOMAIN_VAR_ATT_COUNT = ORC_DOMAIN_LAST_INDEX
};

/* method */
enum
{
  ORC_METHOD_NAME_INDEX = 0,
  ORC_METHOD_SIGNATURE_INDEX = 1,
  ORC_METHOD_PROPERTIES_INDEX = 2,

  /* add a new one above */

  ORC_METHOD_LAST_INDEX,

  ORC_METHOD_VAR_ATT_COUNT = ORC_METHOD_LAST_INDEX
};

/* method argument */
enum
{
  ORC_METHARG_DOMAIN_INDEX = 0,

  /* add a new one above */

  ORC_METHARG_LAST_INDEX,

  ORC_METHARG_VAR_ATT_COUNT = ORC_METHARG_LAST_INDEX
};

/* method signature */
enum
{
  ORC_METHSIG_FUNCTION_NAME_INDEX = 0,
  ORC_METHSIG_SQL_DEF_INDEX = 1,
  ORC_METHSIG_RETURN_VALUE_INDEX = 2,
  ORC_METHSIG_ARGUMENTS_INDEX = 3,

  /* add a new one above */

  ORC_METHSIG_LAST_INDEX,

  ORC_METHSIG_VAR_ATT_COUNT = ORC_METHSIG_LAST_INDEX
};

/* method file */
enum
{
  ORC_METHFILE_NAME_INDEX = 0,
  ORC_METHFILE_PROPERTIES_INDEX = 1,

  /* add a new one above */

  ORC_METHFILE_LAST_INDEX,

  ORC_METHFILE_VAR_ATT_COUNT = ORC_METHFILE_LAST_INDEX
};

/* query spec */
enum
{
  ORC_QUERY_SPEC_SPEC_INDEX = 0,

  /* add a new one above */

  ORC_QUERY_LAST_INDEX,

  ORC_QUERY_SPEC_VAR_ATT_COUNT = ORC_QUERY_LAST_INDEX
};

/* resolution */
enum
{
  ORC_RES_NAME_INDEX = 0,
  ORC_RES_ALIAS_INDEX = 1,

  /* add a new one above */

  ORC_RES_LAST_INDEX,

  ORC_RES_VAR_ATT_COUNT = ORC_RES_LAST_INDEX
};

/* partition */
enum
{
  ORC_PARTITION_NAME_INDEX = 0,
  ORC_PARTITION_EXPR_INDEX = 1,
  ORC_PARTITION_VALUES_INDEX = 2,
  ORC_PARTITION_COMMENT_INDEX = 3,

  /* add a new one above */
  ORC_PARTITION_LAST_INDEX,

  ORC_PARTITION_VAR_ATT_COUNT = ORC_PARTITION_LAST_INDEX
};

/* MEMORY REPRESENTATION STRUCTURES */

#define OR_BINARY_MAX_LENGTH 65535
#define OR_BINARY_LENGTH_MASK 0xFFFF
#define OR_BINARY_PAD_SHIFT  16

typedef struct db_binary DB_BINARY;
struct db_binary
{
  unsigned char *data;
  unsigned int length;
};

/*
 * DB_REFERENCE
 *    This is a common structure header used by DB_SET and DB_ELO.
 *    It encapsulates ownership information that must be maintained
 *    by these two types.  General routines can be written to maintain
 *    ownership information for both types.
 *
 */
typedef struct db_reference DB_REFERENCE;
struct db_reference
{
  struct db_object *handle;
  struct db_object *owner;
  int attribute;
};

typedef struct db_set SETREF;
struct db_set
{
  /* 
   * a garbage collector ticket is not required for the "owner" field as
   * the entire set references area is registered for scanning in area_grow.
   */
  struct db_object *owner;
  struct db_set *ref_link;
  struct setobj *set;
  char *disk_set;
  DB_DOMAIN *disk_domain;
  int attribute;
  int ref_count;
  int disk_size;
  bool need_clear;
};

/*
 * SETOBJ
 *    This is the primitive set object header.
 */
typedef struct setobj SETOBJ;

/*
 * OR_TYPE_SIZE
 *    Returns the byte size of the disk representation of a particular
 *    type.  Returns -1 if the type is variable and the size cannot
 *    be known from just the type id.
 */
extern int or_Type_sizes[];	/* map of type id to fixed value size */

#define OR_TYPE_SIZE(type) or_Type_sizes[(int)(type)]

/*
 * OR_VARINFO
 *    Memory representation for a variable offset table.  This is build
 *    from a disk offset table, either in an object header or in a
 *    set header.
 */
typedef struct or_varinfo OR_VARINFO;
struct or_varinfo
{
  int offset;
  int length;
};

#if __WORDSIZE == 32

#define OR_ALIGNED_BUF(size) \
union \
  { \
    double dummy; \
    char buf[(size) + MAX_ALIGNMENT]; \
  }

#define OR_ALIGNED_BUF_START(abuf) (PTR_ALIGN (abuf.buf, MAX_ALIGNMENT))
#define OR_ALIGNED_BUF_SIZE(abuf) (sizeof (abuf.buf) - MAX_ALIGNMENT)

#else /* __WORDSIZE == 32 */

#define OR_ALIGNED_BUF(size) \
union \
  { \
    double dummy; \
    char buf[(size)]; \
  }

#define OR_ALIGNED_BUF_START(abuf) (abuf.buf)
#define OR_ALIGNED_BUF_SIZE(abuf) (sizeof (abuf.buf))

#endif

#define OR_INFINITE_POINTER ((void *) (~((UINTPTR) 0UL)))

typedef struct or_buf OR_BUF;
struct or_buf
{
  char *buffer;
  char *ptr;
  char *endptr;
  struct or_fixup *fixups;
  jmp_buf env;
  int error_abort;
};

/* TODO: LP64 check DB_INT32_MAX */

#define OR_BUF_INIT(buf, data, size) \
  do { \
    (buf).buffer = (buf).ptr = (data); \
    (buf).endptr = ((size) <= 0 || (size) == DB_INT32_MAX) \
                    ? (char *) OR_INFINITE_POINTER : (data) + (size); \
    (buf).error_abort = 0; \
    (buf).fixups = NULL; \
  } while (0)

#define OR_BUF_INIT2(buf, data, size) \
  do { \
    (buf).buffer = (buf).ptr = (data); \
    (buf).endptr = ((size) <= 0 || (size) == DB_INT32_MAX) \
                    ? (char *) OR_INFINITE_POINTER : (data) + (size); \
    (buf).error_abort = 1; \
    (buf).fixups = NULL; \
  } while (0)

/* Need to translate types of DB_TYPE_OBJECT into DB_TYPE_OID in server-side */
#define OR_PACK_DOMAIN_OBJECT_TO_OID(p, d, o, n) \
  or_pack_domain ((p), \
                  TP_DOMAIN_TYPE (d) == DB_TYPE_OBJECT ? &tp_Oid_domain : (d), \
                  (o), (n))

#define ASSERT_ALIGN(ptr, alignment) (assert (PTR_ALIGN (ptr, alignment) == ptr))

extern int or_rep_id (RECDES * record);
extern int or_set_rep_id (RECDES * record, int repid);
extern int or_replace_rep_id (RECDES * record, int repid);
extern int or_chn (RECDES * record);
extern int or_replace_chn (RECDES * record, int chn);
extern int or_mvcc_get_repid_and_flags (OR_BUF * buf, int *error);
extern int or_mvcc_set_repid_and_flags (OR_BUF * buf, int mvcc_flag, int repid, int bound_bit,
					int variable_offset_size);
extern char *or_class_name (RECDES * record);
extern int or_mvcc_get_header (RECDES * record, MVCC_REC_HEADER * mvcc_rec_header);
extern int or_mvcc_set_header (RECDES * record, MVCC_REC_HEADER * mvcc_rec_header);
extern int or_mvcc_add_header (RECDES * record, MVCC_REC_HEADER * mvcc_rec_header, int bound_bit,
			       int variable_offset_size);

/* Pointer based decoding functions */
extern int or_set_element_offset (char *setptr, int element);

#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_get_bound_bit (char *bound_bits, int element);
extern void or_put_bound_bit (char *bound_bits, int element, int bound);
#endif

/* Data packing functions */
extern char *or_pack_int (char *ptr, int number);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_pack_bigint (char *ptr, DB_BIGINT number);
#endif
extern char *or_pack_int64 (char *ptr, INT64 number);
extern char *or_pack_float (char *ptr, float number);
extern char *or_pack_double (char *ptr, double number);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_pack_time (char *ptr, DB_TIME time);
extern char *or_pack_date (char *ptr, DB_DATE date);
extern char *or_pack_monetary (char *ptr, DB_MONETARY * money);
extern char *or_pack_utime (char *ptr, DB_UTIME utime);
#endif
extern char *or_pack_short (char *ptr, short number);
extern char *or_pack_string_with_null_padding (char *ptr, const char *stream, size_t len);
extern char *or_pack_stream (char *ptr, const char *stream, size_t len);
extern char *or_pack_string (char *ptr, const char *string);
extern char *or_pack_string_with_length (char *ptr, const char *string, int length);
extern char *or_pack_errcode (char *ptr, int error);
extern char *or_pack_oid (char *ptr, const OID * oid);
extern char *or_pack_oid_array (char *ptr, int n, const OID * oids);
extern char *or_pack_hfid (const char *ptr, const HFID * hfid);
extern char *or_pack_btid (char *buf, BTID * btid);
extern char *or_pack_ehid (char *buf, EHID * btid);
extern char *or_pack_recdes (char *buf, RECDES * recdes);
extern char *or_pack_log_lsa (const char *ptr, const LOG_LSA * lsa);
extern char *or_unpack_log_lsa (char *ptr, LOG_LSA * lsa);
extern char *or_unpack_set (char *ptr, SETOBJ ** set, struct tp_domain *domain);
extern char *or_unpack_setref (char *ptr, DB_SET ** ref);
extern char *or_pack_listid (char *ptr, void *listid);
extern char *or_pack_lock (char *ptr, LOCK lock);
extern char *or_pack_set_header (char *buf, DB_TYPE stype, DB_TYPE etype, int bound_bits, int size);
extern char *or_pack_method_sig_list (char *ptr, void *method_sig_list);
extern char *or_pack_set_node (char *ptr, void *set_node);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_pack_elo (char *ptr, void *elo);
extern char *or_pack_string_array (char *buffer, int count, const char **string_array);
extern char *or_pack_db_value_array (char *buffer, int count, DB_VALUE * val);
extern char *or_pack_int_array (char *buffer, int count, int *int_array);
#endif

/* should be using the or_pack_value family instead ! */
extern char *or_pack_db_value (char *buffer, DB_VALUE * var);
extern char *or_unpack_db_value (char *buffer, DB_VALUE * val);
extern int or_db_value_size (DB_VALUE * var);

/* Data unpacking functions */
extern char *or_unpack_int (char *ptr, int *number);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_unpack_bigint (char *ptr, DB_BIGINT * number);
#endif
extern char *or_unpack_int64 (char *ptr, INT64 * number);
extern char *or_unpack_int_array (char *ptr, int n, int **number_array);
extern char *or_unpack_longint (char *ptr, int *number);
extern char *or_unpack_short (char *ptr, short *number);
extern char *or_unpack_float (char *ptr, float *number);
extern char *or_unpack_double (char *ptr, double *number);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_unpack_time (char *ptr, DB_TIME * time);
extern char *or_unpack_date (char *ptr, DB_DATE * date);
extern char *or_unpack_monetary (char *ptr, DB_MONETARY * money);
extern char *or_unpack_utime (char *ptr, DB_UTIME * utime);
#endif
extern char *or_unpack_stream (char *ptr, char *stream, size_t len);
extern char *or_unpack_string (char *ptr, char **string);
extern char *or_unpack_string_alloc (char *ptr, char **string);
extern char *or_unpack_string_nocopy (char *ptr, char **string);
extern char *or_unpack_errcode (char *ptr, int *error);
extern char *or_unpack_oid (char *ptr, OID * oid);
extern char *or_unpack_oid_array (char *ptr, int n, OID ** oids);
extern char *or_unpack_hfid (char *ptr, HFID * hfid);
extern char *or_unpack_hfid_array (char *ptr, int n, HFID ** hfids);
extern char *or_unpack_btid (char *buf, BTID * btid);
extern char *or_unpack_ehid (char *buf, EHID * btid);
extern char *or_unpack_recdes (char *buf, RECDES ** recdes);
extern char *or_unpack_listid (char *ptr, void *listid_ptr);
extern char *or_unpack_unbound_listid (char *ptr, void **listid_ptr);
extern char *or_unpack_lock (char *ptr, LOCK * lock);
extern char *or_unpack_set_header (char *buf, DB_TYPE * stype, DB_TYPE * etype, int *bound_bits, int *size);
extern char *or_unpack_method_sig_list (char *ptr, void **method_sig_list_ptr);
extern char *or_unpack_set_node (char *ptr, void *set_node_ptr);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_unpack_string_array (char *buffer, char ***string_array, int *cnt);
extern char *or_unpack_db_value_array (char *buffer, DB_VALUE ** val, int *count);
extern char *or_unpack_elo (char *ptr, void **elo_ptr);
#endif
extern char *or_pack_ptr (char *ptr, UINTPTR ptrval);
extern char *or_unpack_ptr (char *ptr, UINTPTR * ptrval);

extern char *or_pack_bool_array (char *ptr, const bool * bools, int size);
extern char *or_unpack_bool_array (char *ptr, bool ** bools);
extern int or_packed_bool_array_length (const bool * bools, int size);

/* pack/unpack support functions */
extern int or_packed_stream_length (size_t len);
extern int or_packed_string_length (const char *string, int *strlen);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_align_length (int length);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int or_packed_varbit_length (int bitlen);
extern int or_varbit_length (int bitlen);
extern int or_packed_varchar_length (int charlen);
extern int or_varchar_length (int charlen);
extern int or_packed_recdesc_length (int length);

/*
 * to avoid circular dependencies, don't require the definition of QFILE_LIST_ID in
 * this file (it references DB_TYPE)
 */
extern int or_listid_length (void *listid);
extern int or_method_sig_list_length (void *method_sig_list_ptr);
extern int or_set_node_length (void *set_node_ptr);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_elo_length (void *elo_ptr);
extern int or_packed_string_array_length (int count, const char **string_array);
extern int or_packed_db_value_array_length (int count, DB_VALUE * val);
#endif

extern void or_encode (char *buffer, const char *source, int size);
extern void or_decode (const char *buffer, char *dest, int size);

extern void or_init (OR_BUF * buf, char *data, int length);

/* These are called when overflow/underflow are detected */
extern int or_overflow (OR_BUF * buf);
extern int or_underflow (OR_BUF * buf);
extern void or_abort (OR_BUF * buf);

/* Data packing functions */
extern int or_put_byte (OR_BUF * buf, int num);
extern int or_put_short (OR_BUF * buf, int num);
extern int or_put_int (OR_BUF * buf, int num);
extern int or_put_bigint (OR_BUF * buf, DB_BIGINT num);
extern int or_put_float (OR_BUF * buf, float num);
extern int or_put_double (OR_BUF * buf, double num);
extern int or_put_time (OR_BUF * buf, DB_TIME * timeval);
extern int or_put_timetz (OR_BUF * buf, DB_TIMETZ * time_tz);
extern int or_put_utime (OR_BUF * buf, DB_UTIME * timeval);
extern int or_put_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz);
extern int or_put_date (OR_BUF * buf, DB_DATE * date);
extern int or_put_datetime (OR_BUF * buf, DB_DATETIME * datetimeval);
extern int or_put_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz);
extern int or_put_monetary (OR_BUF * buf, DB_MONETARY * monetary);
extern int or_put_string (OR_BUF * buf, char *string);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_put_binary (OR_BUF * buf, DB_BINARY * binary);
#endif
extern int or_put_data (OR_BUF * buf, char *data, int length);
extern int or_put_oid (OR_BUF * buf, const OID * oid);
extern int or_put_varbit (OR_BUF * buf, char *string, int bitlen);
extern int or_packed_put_varbit (OR_BUF * buf, char *string, int bitlen);
extern int or_put_varchar (OR_BUF * buf, char *string, int charlen);
extern int or_packed_put_varchar (OR_BUF * buf, char *string, int charlen);
extern int or_put_align32 (OR_BUF * buf);
extern int or_put_offset (OR_BUF * buf, int num);
extern int or_put_offset_internal (OR_BUF * buf, int num, int offset_size);
extern int or_put_mvccid (OR_BUF * buf, MVCCID mvccid);

/* Data unpacking functions */
extern int or_get_byte (OR_BUF * buf, int *error);
extern int or_get_short (OR_BUF * buf, int *error);
extern int or_get_int (OR_BUF * buf, int *error);
extern DB_BIGINT or_get_bigint (OR_BUF * buf, int *error);
extern float or_get_float (OR_BUF * buf, int *error);
extern double or_get_double (OR_BUF * buf, int *error);
extern int or_get_time (OR_BUF * buf, DB_TIME * timeval);
extern int or_get_timetz (OR_BUF * buf, DB_TIMETZ * time_tz);
extern int or_get_utime (OR_BUF * buf, DB_UTIME * timeval);
extern int or_get_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz);
extern int or_get_date (OR_BUF * buf, DB_DATE * date);
extern int or_get_datetime (OR_BUF * buf, DB_DATETIME * datetime);
extern int or_get_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz);
extern int or_get_monetary (OR_BUF * buf, DB_MONETARY * monetary);
extern int or_get_data (OR_BUF * buf, char *data, int length);
extern int or_get_oid (OR_BUF * buf, OID * oid);
extern int or_get_offset (OR_BUF * buf, int *error);
extern int or_get_offset_internal (OR_BUF * buf, int *error, int offset_size);
extern int or_get_mvccid (OR_BUF * buf, MVCCID * mvccid);

extern int or_skip_varchar_remainder (OR_BUF * buf, int charlen, int align);
extern int or_skip_varchar (OR_BUF * buf, int align);
extern int or_skip_varbit (OR_BUF * buf, int align);
extern int or_skip_varbit_remainder (OR_BUF * buf, int bitlen, int align);

/* Pack/unpack support functions */
extern int or_advance (OR_BUF * buf, int offset);
extern int or_seek (OR_BUF * buf, int psn);
extern int or_align (OR_BUF * buf, int alignment);
extern int or_pad (OR_BUF * buf, int length);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_length_string (char *string);
extern int or_length_binary (DB_BINARY * binary);
#endif

extern int or_get_varchar_length (OR_BUF * buf, int *intval);
extern int or_get_align (OR_BUF * buf, int align);
extern int or_get_align32 (OR_BUF * buf);
extern int or_get_align64 (OR_BUF * buf);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_get_varchar (OR_BUF * buf, int *length_ptr);
extern char *or_get_varbit (OR_BUF * buf, int *length_ptr);
#endif
extern int or_get_varbit_length (OR_BUF * buf, int *intval);

extern char *or_unpack_var_table (char *ptr, int nvars, OR_VARINFO * vars);
extern OR_VARINFO *or_get_var_table (OR_BUF * buf, int nvars, char *(*allocator) (int));

extern OR_VARINFO *or_get_var_table_internal (OR_BUF * buf, int nvars, char *(*allocator) (int), int offset_size);

/* DOMAIN functions */
extern int or_packed_domain_size (struct tp_domain *domain, int include_classoids);
extern char *or_pack_domain (char *ptr, struct tp_domain *domain, int include_classoids, int is_null);
extern char *or_unpack_domain (char *ptr, struct tp_domain **domain_ptr, int *is_null);
extern int or_put_domain (OR_BUF * buf, struct tp_domain *domain, int include_classoids, int is_null);
extern struct tp_domain *or_get_domain (OR_BUF * buf, struct tp_domain *dom, int *is_null);
extern int or_put_sub_domain (OR_BUF * buf);

/* SET functions */
extern void or_packed_set_info (DB_TYPE set_type, struct tp_domain *domain, int include_domain, int *bound_bits,
				int *offset_table, int *element_tags, int *element_size);

extern int or_put_set_header (OR_BUF * buf, DB_TYPE set_type, int size, int domain, int bound_bits, int offset_table,
			      int element_tags, int common_sub_header);

extern int or_get_set_header (OR_BUF * buf, DB_TYPE * set_type, int *size, int *domain, int *bound_bits,
			      int *offset_table, int *element_tags, int *common_sub_header);

extern int or_skip_set_header (OR_BUF * buf);

extern int or_packed_set_length (SETOBJ * set, int include_domain);

extern void or_put_set (OR_BUF * buf, SETOBJ * set, int include_domain);

extern SETOBJ *or_get_set (OR_BUF * buf, struct tp_domain *domain);
extern int or_disk_set_size (OR_BUF * buf, struct tp_domain *domain, DB_TYPE * set_type);

/* DB_VALUE functions */
extern int or_packed_value_size (DB_VALUE * value, int collapse_null, int include_domain, int include_domain_classoids);

extern int or_put_value (OR_BUF * buf, DB_VALUE * value, int collapse_null, int include_domain,
			 int include_domain_classoids);

extern int or_get_value (OR_BUF * buf, DB_VALUE * value, struct tp_domain *domain, int expected, bool copy);

extern char *or_pack_value (char *buf, DB_VALUE * value);
extern char *or_pack_mem_value (char *buf, DB_VALUE * value);
extern char *or_unpack_value (char *buf, DB_VALUE * value);
extern char *or_unpack_mem_value (char *buf, DB_VALUE * value);

extern int or_packed_enumeration_size (const DB_ENUMERATION * e);
extern int or_put_enumeration (OR_BUF * buf, const DB_ENUMERATION * e);
extern int or_get_enumeration (OR_BUF * buf, DB_ENUMERATION * e);
extern int or_header_size (char *ptr);

extern char *or_pack_mvccid (char *ptr, const MVCCID mvccid);
extern char *or_unpack_mvccid (char *ptr, MVCCID * mvccid);
extern int or_mvcc_set_log_lsa_to_record (RECDES * record, LOG_LSA * lsa);

extern char *or_pack_sha1 (char *ptr, SHA1Hash * sha1);
extern char *or_unpack_sha1 (char *ptr, SHA1Hash * sha1);

/* Get the compressed and the decompressed lengths of a string stored in buffer */
extern int or_get_varchar_compression_lengths (OR_BUF * buf, int *compressed_size, int *decompressed_size);

#endif /* _OBJECT_REPRESENTATION_H_ */
