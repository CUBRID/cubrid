/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
#include <assert.h>
#if !defined(WINDOWS)
#include <netinet/in.h>
#endif /* !WINDOWS */

#include "byte_order.h"
#include "db_set.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "oid.h"
#include "object_representation_constants.h"
#include "porting_inline.hpp"
#include "storage_common.h"

// forward declarations
struct log_lsa;
struct setobj;

#define OR_VALUE_ALIGNED_SIZE(value)   \
  (or_db_value_size (value) + MAX_ALIGNMENT)

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
#define OR_CHECK_USHRT_OVERFLOW(i)  ((i) > (int) DB_UINT16_MAX || (i) < 0)
#define OR_CHECK_UINT_OVERFLOW(i)   ((i) > DB_UINT32_MAX || (i) < 0)

#define OR_CHECK_FLOAT_OVERFLOW(i)         ((i) > FLT_MAX || (-(i)) > FLT_MAX)
#define OR_CHECK_DOUBLE_OVERFLOW(i)        ((i) > DBL_MAX || (-(i)) > DBL_MAX)

#if __WORDSIZE == 32
#define OR_PTR_SIZE             4
#define OR_PUT_PTR(ptr, val)    OR_PUT_INT ((ptr), (val))
#define OR_GET_PTR(ptr)         OR_GET_INT ((ptr))
#else /* __WORDSIZE == 32 */
#define OR_PTR_SIZE             8
#define OR_PUT_PTR(ptr, val)    (*(UINTPTR *) ((char *) (ptr)) = swap64 ((UINTPTR) val))
#define OR_GET_PTR(ptr)         ((UINTPTR) swap64 (*(UINTPTR *) ((char *) (ptr))))
#endif /* __WORDSIZE == 64 */

#define OR_INT64_SIZE           8

/* simple macro to calculate minimum bytes to contain given bits */
#define BITS_TO_BYTES(bit_cnt)		(((bit_cnt) + 7) / 8)

/* PACK/UNPACK MACROS */

#define OR_PUT_BYTE(ptr, val) \
  (*((unsigned char *) (ptr)) = (unsigned char) (val))

#define OR_GET_BYTE(ptr) \
  (*(unsigned char *) ((char *) (ptr)))

#define OR_PUT_SHORT(ptr, val) \
  (*(short *) ((char *) (ptr)) = htons ((short) (val)))

#define OR_GET_SHORT(ptr) \
  ((short) ntohs (*(short *) ((char *) (ptr))))

#define OR_PUT_INT(ptr, val) \
  (*(int *) ((char *) (ptr)) = htonl ((int) (val)))

#define OR_GET_INT(ptr) \
  ((int) ntohl (*(int *) ((char *) (ptr))))

#define OR_PUT_INT64(ptr, val) \
  do { \
    INT64 packed_value; \
    packed_value = ((INT64) swap64 (*(INT64*) val)); \
    memcpy (ptr, &packed_value, OR_INT64_SIZE); \
  } while (0)

#define OR_GET_INT64(ptr, val) \
  do { \
    INT64 packed_value; \
    memcpy (&packed_value, ptr, OR_INT64_SIZE); \
    *((INT64*) (val)) = ((INT64) swap64 (packed_value)); \
  } while (0)

#define OR_PUT_BIGINT(ptr, val) \
  OR_PUT_INT64 (ptr, val)

#define OR_GET_BIGINT(ptr, val) \
  OR_GET_INT64 (ptr, val)

INLINE void
OR_PUT_FLOAT (char *ptr, float val)
{
  UINT32 ui = htonf (val);
  memcpy (ptr, &ui, sizeof (ui));
}

#define OR_GET_FLOAT(ptr, value) \
  (*(value) = ntohf (*(UINT32 *) (ptr)))

INLINE void
OR_PUT_DOUBLE (char *ptr, double val)
{
  UINT64 ui = htond (val);
  memcpy (ptr, &ui, sizeof (ui));
}

#define OR_GET_DOUBLE(ptr, value) \
  (*(value) = ntohd (*(UINT64 *) (ptr)))

#define OR_PUT_TIME(ptr, value) \
  OR_PUT_INT (ptr, *((DB_TIME *) (value)))

#define OR_GET_TIME(ptr, value) \
  *((DB_TIME *) (value)) = OR_GET_INT (ptr)

#define OR_PUT_UTIME(ptr, value) \
  OR_PUT_INT (ptr, *((DB_UTIME *) (value)))

#define OR_GET_UTIME(ptr, value) \
  *((DB_UTIME *) (value)) = OR_GET_INT (ptr)

#define OR_PUT_TIMESTAMPTZ(ptr, ts_tz) \
  do { \
    OR_PUT_INT (((char *) ptr), (ts_tz)->timestamp); \
    OR_PUT_INT (((char *) ptr) + OR_TIMESTAMPTZ_TZID, (ts_tz)->tz_id); \
  } while (0)

#define OR_GET_TIMESTAMPTZ(ptr, ts_tz) \
  do { \
    (ts_tz)->timestamp = OR_GET_INT ((char *) (ptr)); \
    (ts_tz)->tz_id = OR_GET_INT (((char *) (ptr)) + OR_TIMESTAMPTZ_TZID); \
  } while (0)

#define OR_PUT_DATE(ptr, value) \
  OR_PUT_INT (ptr, *((DB_DATE *) (value)))

#define OR_GET_DATE(ptr, value) \
  *((DB_DATE *) (value)) = OR_GET_INT (ptr)

#define OR_PUT_DATETIME(ptr, datetime) \
  do { \
    OR_PUT_INT (((char *)ptr) + OR_DATETIME_DATE, (datetime)->date); \
    OR_PUT_INT (((char *)ptr) + OR_DATETIME_TIME, (datetime)->time); \
  } while (0)

#define OR_GET_DATETIME(ptr, datetime) \
  do { \
    (datetime)->date = OR_GET_INT (((char *) (ptr)) + OR_DATETIME_DATE); \
    (datetime)->time = OR_GET_INT (((char *) (ptr)) + OR_DATETIME_TIME); \
  } while (0)

#define OR_PUT_DATETIMETZ(ptr, datetimetz) \
  do { \
    OR_PUT_DATETIME (((char *) ptr), \
		     &((DB_DATETIMETZ *) datetimetz)->datetime); \
    OR_PUT_INT (((char *) ptr) + OR_DATETIMETZ_TZID, (datetimetz)->tz_id); \
  } while (0)

#define OR_GET_DATETIMETZ(ptr, datetimetz) \
  do { \
    OR_GET_DATETIME ((char *) ptr, \
		     &((DB_DATETIMETZ *) datetimetz)->datetime); \
    (datetimetz)->tz_id = OR_GET_INT (((char *) (ptr)) + OR_DATETIMETZ_TZID); \
  } while (0)

#define OR_PUT_MONETARY(ptr, value) \
  do { \
    char pack_value[OR_DOUBLE_SIZE]; \
    OR_PUT_INT (((char *) (ptr)) + OR_MONETARY_TYPE, (int) (value)->type); \
    OR_PUT_DOUBLE (pack_value, (value)->amount); \
    memcpy (((char *) (ptr)) + OR_MONETARY_AMOUNT, pack_value, OR_DOUBLE_SIZE); \
  } while (0)

#define OR_GET_MONETARY(ptr, value) \
  do { \
    UINT64 pack_value; \
    (value)->type = (DB_CURRENCY) OR_GET_INT (((char *) (ptr)) + OR_MONETARY_TYPE); \
    memcpy ((char *) (&pack_value), ((char *) (ptr)) + OR_MONETARY_AMOUNT, OR_DOUBLE_SIZE); \
    OR_GET_DOUBLE (&pack_value, &(value)->amount); \
  } while (0)

#define OR_MOVE_MONETARY(src, dst) \
  do { \
    OR_MOVE_DOUBLE (src, dst); \
    ((DB_MONETARY *) dst)->type = ((DB_MONETARY *) src)->type; \
  } while (0)

#define OR_GET_CURRENCY_TYPE(ptr) \
  (DB_CURRENCY) OR_GET_INT (((char *) (ptr)) + OR_MONETARY_TYPE)

#define OR_GET_STRING(ptr) \
  ((char *) ((char *) (ptr)))

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

#define OR_GET_OFFSET_INTERNAL(ptr, offset_size) \
  (offset_size == OR_BYTE_SIZE) \
   ? OR_GET_BYTE (ptr) \
   : ((offset_size == OR_SHORT_SIZE) \
      ? OR_GET_SHORT (ptr) : OR_GET_INT (ptr))

#define OR_PUT_OFFSET(ptr, val) \
  OR_PUT_OFFSET_INTERNAL(ptr, val, BIG_VAR_OFFSET_SIZE)

#define OR_GET_OFFSET(ptr) \
  OR_GET_OFFSET_INTERNAL (ptr, BIG_VAR_OFFSET_SIZE)

#define OR_PUT_BIG_VAR_OFFSET(ptr, val)	OR_PUT_INT (ptr, val)	/* 4byte */

#define OR_GET_BIG_VAR_OFFSET(ptr) 	OR_GET_INT (ptr)	/* 4byte */

#define OR_PUT_SHA1(ptr, value) \
  do { \
    int i = 0; \
    for (; i < 5; i++) \
      { \
	OR_PUT_INT (ptr + i * OR_INT_SIZE, ((SHA1Hash *) (value))->h[i]); \
      } \
  } while (0)

#define OR_GET_SHA1(ptr, value) \
  do { \
    int i = 0; \
    for (; i < 5; i++) \
      { \
	((SHA1Hash *) (value))->h[i] = (INT32) OR_GET_INT (ptr + i * OR_INT_SIZE); \
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
#define OR_HEADER_SIZE(ptr) (or_header_size ((char *) (ptr)))

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
#define SHORT_VAR_OFFSET_SIZE OR_SHORT_SIZE	/* 2byte */

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
  ORC_CLASS_TYPE = 68,
  ORC_CLASS_TDE_ALGORITHM = 84
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
  ORC_DOMAIN_SCHEMA_JSON_OFFSET = 2,

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

/*
 * SETOBJ
 *    This is the primitive set object header.
 */
typedef struct setobj SETOBJ;

typedef struct db_set SETREF;

#if defined (__cplusplus)
class JSON_VALIDATOR;
#endif
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

/* Need to translate types of DB_TYPE_OBJECT into DB_TYPE_OID in server-side */
#define OR_PACK_DOMAIN_OBJECT_TO_OID(p, d, o, n) \
  or_pack_domain ((p), \
                  TP_DOMAIN_TYPE (d) == DB_TYPE_OBJECT ? &tp_Oid_domain : (d), \
                  (o), (n))

#define ASSERT_ALIGN(ptr, alignment) (assert (PTR_ALIGN (ptr, alignment) == ptr))

#if defined __cplusplus
extern "C"
{
#endif

  extern int db_string_put_cs_and_collation (DB_VALUE * value, const int codeset, const int collation_id);
  extern int db_enum_put_cs_and_collation (DB_VALUE * value, const int codeset, const int collation_id);

  extern int valcnv_convert_value_to_string (DB_VALUE * value);

#if defined __cplusplus
}
#endif

extern int or_rep_id (RECDES * record);
extern int or_set_rep_id (RECDES * record, int repid);
extern int or_chn (RECDES * record);
extern int or_replace_chn (RECDES * record, int chn);
extern int or_mvcc_get_repid_and_flags (OR_BUF * buf, int *error);
extern int or_mvcc_set_repid_and_flags (OR_BUF * buf, int mvcc_flag, int repid, int bound_bit,
					int variable_offset_size);
extern char *or_class_name (RECDES * record);

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
extern char *or_pack_btid (char *buf, const BTID * btid);
extern char *or_pack_ehid (char *buf, EHID * btid);
extern char *or_pack_recdes (char *buf, RECDES * recdes);
extern char *or_pack_log_lsa (const char *ptr, const struct log_lsa *lsa);
extern char *or_unpack_log_lsa (char *ptr, struct log_lsa *lsa);
extern char *or_unpack_set (char *ptr, setobj ** set, struct tp_domain *domain);
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
extern char *or_pack_key_val_range (char *ptr, const void *key_val_range_ptr);
extern char *or_unpack_key_val_range (char *ptr, void *key_val_range_ptr);

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

EXTERN_INLINE void or_init (OR_BUF * buf, char *data, int length) __attribute__ ((ALWAYS_INLINE));

/* These are called when overflow/underflow are detected */
EXTERN_INLINE int or_overflow (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_underflow (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE void or_abort (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));

/* Pack/unpack support functions */
EXTERN_INLINE int or_advance (OR_BUF * buf, int offset) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_seek (OR_BUF * buf, int psn) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_pad (OR_BUF * buf, int length) __attribute__ ((ALWAYS_INLINE));

EXTERN_INLINE int or_put_align32 (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_align (OR_BUF * buf, int alignment) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_align (OR_BUF * buf, int align) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_align32 (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_align64 (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));

/* Data packing functions */
EXTERN_INLINE int or_put_byte (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_short (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_int (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_bigint (OR_BUF * buf, DB_BIGINT num) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_float (OR_BUF * buf, float num) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_double (OR_BUF * buf, double num) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_time (OR_BUF * buf, DB_TIME * timeval) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_utime (OR_BUF * buf, DB_UTIME * timeval) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_date (OR_BUF * buf, DB_DATE * date) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_datetime (OR_BUF * buf, DB_DATETIME * datetimeval) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_monetary (OR_BUF * buf, DB_MONETARY * monetary) __attribute__ ((ALWAYS_INLINE));
#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_put_binary (OR_BUF * buf, DB_BINARY * binary);
#endif
EXTERN_INLINE int or_put_data (OR_BUF * buf, const char *data, int length) __attribute__ ((ALWAYS_INLINE));
extern int or_put_varbit (OR_BUF * buf, const char *string, int bitlen);
extern int or_put_varchar (OR_BUF * buf, char *string, int charlen);
EXTERN_INLINE int or_put_string_aligned (OR_BUF * buf, char *string) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_string_aligned_with_length (OR_BUF * buf, const char *str) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_offset (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_offset_internal (OR_BUF * buf, int num, int offset_size) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_put_oid (OR_BUF * buf, const OID * oid) __attribute__ ((ALWAYS_INLINE));
extern int or_put_mvccid (OR_BUF * buf, MVCCID mvccid);

/* Data unpacking functions */
EXTERN_INLINE int or_get_byte (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_short (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_int (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE DB_BIGINT or_get_bigint (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE float or_get_float (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE double or_get_double (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_time (OR_BUF * buf, DB_TIME * timeval) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_utime (OR_BUF * buf, DB_UTIME * timeval) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_date (OR_BUF * buf, DB_DATE * date) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_datetime (OR_BUF * buf, DB_DATETIME * datetime) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_monetary (OR_BUF * buf, DB_MONETARY * monetary) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_data (OR_BUF * buf, char *data, int length) __attribute__ ((ALWAYS_INLINE));
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_get_varbit (OR_BUF * buf, int *length_ptr);
#endif
EXTERN_INLINE int or_get_varbit_length (OR_BUF * buf, int *intval) __attribute__ ((ALWAYS_INLINE));
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_get_varchar (OR_BUF * buf, int *length_ptr);
#endif
EXTERN_INLINE int or_get_varchar_length (OR_BUF * buf, int *intval) __attribute__ ((ALWAYS_INLINE));
/* Get the compressed and the decompressed lengths of a string stored in buffer */
STATIC_INLINE int or_get_varchar_compression_lengths (OR_BUF * buf, int *compressed_size, int *decompressed_size)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_string_size_byte (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_offset (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_offset_internal (OR_BUF * buf, int *error, int offset_size) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_get_oid (OR_BUF * buf, OID * oid) __attribute__ ((ALWAYS_INLINE));
extern int or_get_mvccid (OR_BUF * buf, MVCCID * mvccid);

EXTERN_INLINE int or_varbit_length (int bitlen) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_varbit_length_internal (int bitlen, int align) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_varchar_length (int charlen) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_varchar_length_internal (int charlen, int align) __attribute__ ((ALWAYS_INLINE));

EXTERN_INLINE int or_skip_varbit (OR_BUF * buf, int align) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_skip_varbit_remainder (OR_BUF * buf, int bitlen, int align) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_skip_varchar (OR_BUF * buf, int align) __attribute__ ((ALWAYS_INLINE));
EXTERN_INLINE int or_skip_varchar_remainder (OR_BUF * buf, int charlen, int align) __attribute__ ((ALWAYS_INLINE));

#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_length_binary (DB_BINARY * binary);
extern int or_length_string (char *string);
#endif

extern int or_packed_put_varbit (OR_BUF * buf, const char *string, int bitlen);
extern int or_packed_put_varchar (OR_BUF * buf, char *string, int charlen);
extern int or_packed_varchar_length (int charlen);
extern int or_packed_recdesc_length (int length);

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

extern int or_packed_set_length (setobj * set, int include_domain);

extern void or_put_set (OR_BUF * buf, setobj * set, int include_domain);

extern setobj *or_get_set (OR_BUF * buf, struct tp_domain *domain);
extern int or_disk_set_size (OR_BUF * buf, struct tp_domain *domain, DB_TYPE * set_type);

/* DB_VALUE functions */
extern int or_packed_value_size (const DB_VALUE * value, int collapse_null, int include_domain,
				 int include_domain_classoids);

extern int or_put_value (OR_BUF * buf, DB_VALUE * value, int collapse_null, int include_domain,
			 int include_domain_classoids);

extern int or_get_value (OR_BUF * buf, DB_VALUE * value, struct tp_domain *domain, int expected, bool copy);

extern char *or_pack_value (char *buf, DB_VALUE * value);
extern char *or_pack_mem_value (char *ptr, DB_VALUE * value, int *packed_len_except_alignment);
extern char *or_unpack_value (const char *buf, DB_VALUE * value);
extern char *or_unpack_mem_value (char *buf, DB_VALUE * value);

extern int or_packed_enumeration_size (const DB_ENUMERATION * e);
extern int or_put_enumeration (OR_BUF * buf, const DB_ENUMERATION * e);
extern int or_get_enumeration (OR_BUF * buf, DB_ENUMERATION * e);
extern int or_header_size (char *ptr);
extern char *or_pack_mvccid (char *ptr, const MVCCID mvccid);
extern char *or_unpack_mvccid (char *ptr, MVCCID * mvccid);

extern char *or_pack_sha1 (char *ptr, const SHA1Hash * sha1);
extern char *or_unpack_sha1 (char *ptr, SHA1Hash * sha1);

extern int or_packed_spacedb_size (const SPACEDB_ALL * all, const SPACEDB_ONEVOL * vols, const SPACEDB_FILES * files);
extern char *or_pack_spacedb (char *ptr, const SPACEDB_ALL * all, const SPACEDB_ONEVOL * vols,
			      const SPACEDB_FILES * files);
extern char *or_unpack_spacedb (char *ptr, SPACEDB_ALL * all, SPACEDB_ONEVOL ** vols, SPACEDB_FILES * files);

/* class object */
extern int classobj_decompose_property_oid (const char *buffer, int *volid, int *fileid, int *pageid);
extern void classobj_initialize_default_expr (DB_DEFAULT_EXPR * default_expr);
extern int classobj_get_prop (DB_SEQ * properties, const char *name, DB_VALUE * pvalue);
#if defined (__cplusplus)
extern int or_get_json_validator (OR_BUF * buf, REFPTR (JSON_VALIDATOR, validator));
extern int or_put_json_validator (OR_BUF * buf, JSON_VALIDATOR * validator);
extern int or_get_json_schema (OR_BUF * buf, REFPTR (char, schema));
extern int or_put_json_schema (OR_BUF * buf, const char *schema);
#endif

/* Because of the VARNCHAR and STRING encoding, this one could not be changed for over 255, just lower. */
#define OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION 255

#define OR_IS_STRING_LENGTH_COMPRESSABLE(str_length) \
  ((str_length) >= OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION && (str_length) <= LZ4_MAX_INPUT_SIZE)

/*
 * or_init - initialize the field of an OR_BUF
 *    return: void
 *    buf(in/out): or buffer to initialize
 *    data(in): buffer data
 *    length(in):  buffer data length
 */
EXTERN_INLINE void
or_init (OR_BUF * buf, char *data, int length)
{
  buf->buffer = data;
  buf->ptr = data;

  /* TODO: LP64 check DB_INT32_MAX */
  if (length <= 0 || length == DB_INT32_MAX)
    {
      buf->endptr = (char *) OR_INFINITE_POINTER;
    }
  else
    {
      buf->endptr = data + length;
    }

  buf->error_abort = 0;
  buf->fixups = NULL;
}

/*
 * OR_BUF PACK/UNPACK FUNCTIONS
 */

/*
 * or_overflow - called by the or_put_ functions when there is not enough
 * room in the buffer to hold a particular value.
 *    return: ER_TF_BUFFER_OVERFLOW or long jump to buf->error_abort
 *    buf(in): translation state structure
 *
 * Note:
 *    Because of the recursive nature of the translation functions, we may
 *    be several levels deep so we can do a longjmp out to the top level
 *    if the user has supplied a jmpbuf.
 *    Because jmpbuf is not a pointer, we have to keep an additional flag
 *    called "error_abort" in the OR_BUF structure to indicate the validity
 *    of the jmpbuf.
 *    This is a fairly common ocurrence because the locator regularly calls
 *    the transformer with a buffer that is too small.  When overflow
 *    is detected, it allocates a larger one and retries the operation.
 *    Because of this, a system error is not signaled here.
 */
EXTERN_INLINE int
or_overflow (OR_BUF * buf)
{
  /*
   * since this is normal behavior, don't set an error condition, the
   * main transformer functions will need to test the status value
   * for ER_TF_BUFFER_OVERFLOW and know that this isn't an error condition.
   */

  if (buf->error_abort)
    {
      _longjmp (buf->env, ER_TF_BUFFER_OVERFLOW);
    }

  return ER_TF_BUFFER_OVERFLOW;
}

/*
 * or_underflow - This is called by the or_get_ functions when there is
 * not enough data in the buffer to extract a particular value.
 *    return: ER_TF_BUFFER_UNDERFLOW or long jump to buf->env
 *    buf(in): translation state structure
 *
 * Note:
 * Unlike or_overflow this is NOT a common ocurrence and indicates a serious
 * memory or disk corruption problem.
 */
EXTERN_INLINE int
or_underflow (OR_BUF * buf)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_BUFFER_UNDERFLOW, 0);

  if (buf->error_abort)
    {
      _longjmp (buf->env, ER_TF_BUFFER_UNDERFLOW);
    }
  return ER_TF_BUFFER_UNDERFLOW;
}

/*
 * or_abort - This is called if there was some fundemtal error
 *    return: void
 *    buf(in): translation state structure
 *
 * Note:
 *    An appropriate error message should have already been set.
 */
EXTERN_INLINE void
or_abort (OR_BUF * buf)
{
  /* assume an appropriate error has already been set */
  if (buf->error_abort)
    {
      _longjmp (buf->env, er_errid ());
    }
}

/*
 * or_seek - This sets the translation pointer directly to a certain byte in
 * the buffer.
 *    return: ERROR_SUCCESS or error code
 *    buf(in/out): or buffer
 *    psn(in): position within buffer
 */
EXTERN_INLINE int
or_seek (OR_BUF * buf, int psn)
{
  if ((buf->buffer + psn) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      buf->ptr = buf->buffer + psn;
    }
  return NO_ERROR;
}

/*
 * or_advance - This advances the translation pointer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    offset(in): number of bytes to skip
 */
EXTERN_INLINE int
or_advance (OR_BUF * buf, int offset)
{
  if ((buf->ptr + offset) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      buf->ptr += offset;
      return NO_ERROR;
    }
}

/*
 * or_pad - This advances the translation pointer and adds bytes of zero.
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    length(in): number of bytes to pad
 *
 * Note:
 *    This advances the translation pointer and adds bytes of zero.
 *    This is used add padding bytes to ensure proper alignment of
 *    some data types.
 */
EXTERN_INLINE int
or_pad (OR_BUF * buf, int length)
{
  if ((buf->ptr + length) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      (void) memset (buf->ptr, 0, length);
      buf->ptr += length;
    }
  return NO_ERROR;
}

/*
 * or_put_align32 - pad zero bytes round up to 4 byte bound
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 */
EXTERN_INLINE int
or_put_align32 (OR_BUF * buf)
{
  unsigned int bits;
  int rc = NO_ERROR;

  bits = (UINTPTR) buf->ptr & 3;
  if (bits)
    {
      rc = or_pad (buf, 4 - bits);
    }

  return rc;
}

/*
 * or_align () - Align current buffer pointer to given alignment.
 *
 * return	 : Error code.
 * buf (in/out)	 : Buffer.
 * alignment (in) : Desired alignment.
 */
EXTERN_INLINE int
or_align (OR_BUF * buf, int alignment)
{
  char *new_ptr = PTR_ALIGN (buf->ptr, alignment);
  if (new_ptr > buf->endptr)
    {
      return (or_overflow (buf));
    }
  buf->ptr = new_ptr;
  return NO_ERROR;
}

/*
 * or_get_align - adnvance or buf pointer to next alignment position
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    align(in):
 */
EXTERN_INLINE int
or_get_align (OR_BUF * buf, int align)
{
  char *ptr;

  ptr = PTR_ALIGN (buf->ptr, align);
  if (ptr > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      buf->ptr = ptr;
      return NO_ERROR;
    }
}

/*
 * or_get_align32 - adnvance or buf pointer to next 4 byte alignment position
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 */
EXTERN_INLINE int
or_get_align32 (OR_BUF * buf)
{
  unsigned int bits;
  int rc = NO_ERROR;

  bits = (UINTPTR) (buf->ptr) & 3;
  if (bits)
    {
      rc = or_advance (buf, 4 - bits);
    }

  return rc;
}

/*
 * or_get_align64 - adnvance or buf pointer to next 8 byte alignment position
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 */
EXTERN_INLINE int
or_get_align64 (OR_BUF * buf)
{
  unsigned int bits;
  int rc = NO_ERROR;

  bits = (UINTPTR) (buf->ptr) & 7;
  if (bits)
    {
      rc = or_advance (buf, 8 - bits);
    }

  return rc;
}

/*
 * NUMERIC DATA TRANSFORMS
 *    This set of functions handles the transformation of the
 *    numeric types byte, short, integer, float, and double.
 *
 */

/*
 * or_put_byte - put a byte to or buffer
 *    return: NO_ERROR or error code
 *    buf(out/out): or buffer
 *    num(in): byte value
 */
EXTERN_INLINE int
or_put_byte (OR_BUF * buf, int num)
{
  if ((buf->ptr + OR_BYTE_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_BYTE (buf->ptr, num);
      buf->ptr += OR_BYTE_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_byte - read a byte value from or buffer
 *    return: byte value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
EXTERN_INLINE int
or_get_byte (OR_BUF * buf, int *error)
{
  int value = 0;

  if ((buf->ptr + OR_BYTE_SIZE) > buf->endptr)
    {
      *error = or_underflow (buf);
      return 0;
    }
  else
    {
      value = OR_GET_BYTE (buf->ptr);
      buf->ptr += OR_BYTE_SIZE;
      *error = NO_ERROR;
    }
  return value;
}

/*
 * or_put_short - put a short value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    num(in): short value to put
 */
EXTERN_INLINE int
or_put_short (OR_BUF * buf, int num)
{
  ASSERT_ALIGN (buf->ptr, SHORT_ALIGNMENT);

  if ((buf->ptr + OR_SHORT_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_SHORT (buf->ptr, num);
      buf->ptr += OR_SHORT_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_short - read a short value from or buffer
 *    return: short value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
EXTERN_INLINE int
or_get_short (OR_BUF * buf, int *error)
{
  int value = 0;

  ASSERT_ALIGN (buf->ptr, SHORT_ALIGNMENT);

  if ((buf->ptr + OR_SHORT_SIZE) > buf->endptr)
    {
      *error = or_underflow (buf);
      return 0;
    }
  else
    {
      value = OR_GET_SHORT (buf->ptr);
      buf->ptr += OR_SHORT_SIZE;
    }
  *error = NO_ERROR;
  return value;
}

/*
 * or_put_int - put int value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    num(in): int value to put
 */
EXTERN_INLINE int
or_put_int (OR_BUF * buf, int num)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_INT_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_INT (buf->ptr, num);
      buf->ptr += OR_INT_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_int - get int value from or buffer
 *    return: int value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
EXTERN_INLINE int
or_get_int (OR_BUF * buf, int *error)
{
  int value = 0;

  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_INT_SIZE) > buf->endptr)
    {
      *error = or_underflow (buf);
    }
  else
    {
      value = OR_GET_INT (buf->ptr);
      buf->ptr += OR_INT_SIZE;
      *error = NO_ERROR;
    }
  return value;
}

/*
 * or_put_bigint - put bigint value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    num(in): bigint value to put
 */
EXTERN_INLINE int
or_put_bigint (OR_BUF * buf, DB_BIGINT num)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_BIGINT_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_BIGINT (buf->ptr, &num);
      buf->ptr += OR_BIGINT_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_bigint - get bigint value from or buffer
 *    return: bigint value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
EXTERN_INLINE DB_BIGINT
or_get_bigint (OR_BUF * buf, int *error)
{
  DB_BIGINT value = 0;

  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_BIGINT_SIZE) > buf->endptr)
    {
      *error = or_underflow (buf);
    }
  else
    {
      OR_GET_BIGINT (buf->ptr, &value);
      buf->ptr += OR_BIGINT_SIZE;
      *error = NO_ERROR;
    }
  return value;
}

/*
 * or_put_float - put a float value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    fnum(in): float value to put
 */
EXTERN_INLINE int
or_put_float (OR_BUF * buf, float fnum)
{
  ASSERT_ALIGN (buf->ptr, FLOAT_ALIGNMENT);

  if ((buf->ptr + OR_FLOAT_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_FLOAT (buf->ptr, fnum);
      buf->ptr += OR_FLOAT_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_float - read a float value from or buffer
 *    return: float value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
EXTERN_INLINE float
or_get_float (OR_BUF * buf, int *error)
{
  float value = 0.0;

  ASSERT_ALIGN (buf->ptr, FLOAT_ALIGNMENT);

  if ((buf->ptr + OR_FLOAT_SIZE) > buf->endptr)
    {
      *error = or_underflow (buf);
    }
  else
    {
      OR_GET_FLOAT (buf->ptr, &value);
      buf->ptr += OR_FLOAT_SIZE;
      *error = NO_ERROR;
    }
  return value;
}

/*
 * or_put_double - put a double value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    dnum(in): double value to put
 */
EXTERN_INLINE int
or_put_double (OR_BUF * buf, double dnum)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_DOUBLE_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_DOUBLE (buf->ptr, dnum);
      buf->ptr += OR_DOUBLE_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_double - read a double value from or buffer
 *    return: double value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
EXTERN_INLINE double
or_get_double (OR_BUF * buf, int *error)
{
  double value = 0.0;

  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_DOUBLE_SIZE) > buf->endptr)
    {
      *error = or_underflow (buf);
    }
  else
    {
      OR_GET_DOUBLE (buf->ptr, &value);
      buf->ptr += OR_DOUBLE_SIZE;
      *error = NO_ERROR;
    }
  return value;
}

/*
 * EXTENDED TYPE TRANSLATORS
 *    This set of functions reads and writes the extended types time,
 *    utime, date, and monetary.
 */

/*
 * or_put_time - write a DB_TIME to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    timeval(in): time value to write
 */
EXTERN_INLINE int
or_put_time (OR_BUF * buf, DB_TIME * timeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_TIME_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_TIME (buf->ptr, timeval);
      buf->ptr += OR_TIME_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_time - read a  DB_TIME from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    timeval(out): pointer to DB_TIME value
 */
EXTERN_INLINE int
or_get_time (OR_BUF * buf, DB_TIME * timeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_TIME_SIZE) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      OR_GET_TIME (buf->ptr, timeval);
      buf->ptr += OR_TIME_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_put_utime - write a timestamp value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    timeval(in): pointer to timestamp value
 */
EXTERN_INLINE int
or_put_utime (OR_BUF * buf, DB_UTIME * timeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_UTIME_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_UTIME (buf->ptr, timeval);
      buf->ptr += OR_UTIME_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_utime - read a timestamp value from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    timeval(out): pointer to timestamp value
 */
EXTERN_INLINE int
or_get_utime (OR_BUF * buf, DB_UTIME * timeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_UTIME_SIZE) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      OR_GET_UTIME (buf->ptr, timeval);
      buf->ptr += OR_UTIME_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_put_timestamptz - write a timestamp with tz value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    ts_tz(in): pointer to DB_TIMESTAMPTZ value
 */
EXTERN_INLINE int
or_put_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_TIMESTAMPTZ_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_TIMESTAMPTZ (buf->ptr, ts_tz);
      buf->ptr += OR_TIMESTAMPTZ_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_timestamptz - read a timestamp with tz value from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    ts_tz(out): pointer to DB_TIMESTAMPTZ value
 */
EXTERN_INLINE int
or_get_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_TIMESTAMPTZ_SIZE) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      OR_GET_TIMESTAMPTZ (buf->ptr, ts_tz);
      buf->ptr += OR_TIMESTAMPTZ_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_put_date - write a DB_DATE value to or_buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    date(in): pointer to DB_DATE value
 */
EXTERN_INLINE int
or_put_date (OR_BUF * buf, DB_DATE * date)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_DATE_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_DATE (buf->ptr, date);
      buf->ptr += OR_DATE_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_date - read a DB_DATE value from or_buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    date(out): pointer to DB_DATE value
 */
EXTERN_INLINE int
or_get_date (OR_BUF * buf, DB_DATE * date)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_DATE_SIZE) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      OR_GET_DATE (buf->ptr, date);
      buf->ptr += OR_DATE_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_put_datetime - write a datetime value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    datetimeval(in): pointer to datetime value
 */
EXTERN_INLINE int
or_put_datetime (OR_BUF * buf, DB_DATETIME * datetimeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_DATETIME_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_DATETIME (buf->ptr, datetimeval);
      buf->ptr += OR_DATETIME_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_datetime - read a DB_DATETIME value from or_buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    date(out): pointer to DB_DATETIME value
 */
EXTERN_INLINE int
or_get_datetime (OR_BUF * buf, DB_DATETIME * datetime)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_DATETIME_SIZE) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      OR_GET_DATETIME (buf->ptr, datetime);
      buf->ptr += OR_DATETIME_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_put_datetimetz - write a datetime with tz value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    datetimetz(in): pointer to DB_DATETIMETZ value
 */
EXTERN_INLINE int
or_put_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_DATETIMETZ_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_DATETIMETZ (buf->ptr, datetimetz);
      buf->ptr += OR_DATETIMETZ_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_datetimetz - read a datetime with tz value from or_buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    datetimetz(out): pointer to DB_DATETIMETZ value
 */
EXTERN_INLINE int
or_get_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_DATETIMETZ_SIZE) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      OR_GET_DATETIMETZ (buf->ptr, datetimetz);
      buf->ptr += OR_DATETIMETZ_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_put_monetary - write a DB_MONETARY value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    monetary(in): pointer to DB_MONETARY value
 */
EXTERN_INLINE int
or_put_monetary (OR_BUF * buf, DB_MONETARY * monetary)
{
  int error;

  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* check for valid currency type don't put default case in the switch!!! */
  error = ER_INVALID_CURRENCY_TYPE;
  switch (monetary->type)
    {
    case DB_CURRENCY_DOLLAR:
    case DB_CURRENCY_YEN:
    case DB_CURRENCY_WON:
    case DB_CURRENCY_TL:
    case DB_CURRENCY_BRITISH_POUND:
    case DB_CURRENCY_CAMBODIAN_RIEL:
    case DB_CURRENCY_CHINESE_RENMINBI:
    case DB_CURRENCY_INDIAN_RUPEE:
    case DB_CURRENCY_RUSSIAN_RUBLE:
    case DB_CURRENCY_AUSTRALIAN_DOLLAR:
    case DB_CURRENCY_CANADIAN_DOLLAR:
    case DB_CURRENCY_BRASILIAN_REAL:
    case DB_CURRENCY_ROMANIAN_LEU:
    case DB_CURRENCY_EURO:
    case DB_CURRENCY_SWISS_FRANC:
    case DB_CURRENCY_DANISH_KRONE:
    case DB_CURRENCY_NORWEGIAN_KRONE:
    case DB_CURRENCY_BULGARIAN_LEV:
    case DB_CURRENCY_VIETNAMESE_DONG:
    case DB_CURRENCY_CZECH_KORUNA:
    case DB_CURRENCY_POLISH_ZLOTY:
    case DB_CURRENCY_SWEDISH_KRONA:
    case DB_CURRENCY_CROATIAN_KUNA:
    case DB_CURRENCY_SERBIAN_DINAR:
      error = NO_ERROR;		/* it's a type we expect */
      break;
    default:
      break;
    }

  if (error != NO_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, monetary->type);
      return error;
    }

  if ((buf->ptr + OR_MONETARY_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      OR_PUT_MONETARY (buf->ptr, monetary);
      buf->ptr += OR_MONETARY_SIZE;
    }

  return error;
}

/*
 * or_get_monetary - read a DB_MONETARY from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    monetary(out): pointer to DB_MONETARY value
 */
EXTERN_INLINE int
or_get_monetary (OR_BUF * buf, DB_MONETARY * monetary)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_MONETARY_SIZE) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      OR_GET_MONETARY (buf->ptr, monetary);
      buf->ptr += OR_MONETARY_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_put_data - write an array of bytes to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    data(in): pointer to data
 *    length(in): length in bytes
 */
EXTERN_INLINE int
or_put_data (OR_BUF * buf, const char *data, int length)
{
  if ((buf->ptr + length) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      (void) memcpy (buf->ptr, data, length);
      buf->ptr += length;
    }
  return NO_ERROR;
}

/*
 * or_get_data - read an array of bytes from or buffer for given length
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    data(in): pointer to buffer to read data into
 *    length(in): length of read data
 */
EXTERN_INLINE int
or_get_data (OR_BUF * buf, char *data, int length)
{
  if ((buf->ptr + length) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      (void) memcpy (data, buf->ptr, length);
      buf->ptr += length;
    }
  return NO_ERROR;
}

/*
 * or_get_varbit_length - get varbit length from or buffer
 *    return: length of varbit or 0 if error
 *    buf(in/out): or buffer
 *    rc(out): NO_ERROR or error code
 */
EXTERN_INLINE int
or_get_varbit_length (OR_BUF * buf, int *rc)
{
  int net_bitlen = 0, bitlen = 0;

  /* unpack the size prefix */
  bitlen = or_get_byte (buf, rc);

  if (*rc != NO_ERROR)
    {
      return bitlen;
    }

  if (bitlen == 0xFF)
    {
      *rc = or_get_data (buf, (char *) &net_bitlen, OR_INT_SIZE);
      bitlen = OR_GET_INT (&net_bitlen);
    }
  return bitlen;
}

/*
 * or_get_varchar_length - get varchar length from or buffer
 *    return: length of varchar or 0 if error.
 *    buf(in/out): or buffer
 *    rc(out): status code
 */
EXTERN_INLINE int
or_get_varchar_length (OR_BUF * buf, int *rc)
{
  int charlen, compressed_length = 0, decompressed_length = 0;

  *rc = or_get_varchar_compression_lengths (buf, &compressed_length, &decompressed_length);

  if (compressed_length > 0)
    {
      charlen = compressed_length;
    }
  else
    {
      charlen = decompressed_length;
    }

  return charlen;
}

/* or_get_varchar_compression_lengths() - Function to get the compressed length and the uncompressed length of
 *					  a compressed string.
 *
 * return                 : NO_ERROR or error_code.
 * buf(in)                : The buffer where the string is stored.
 * compressed_size(out)   : The compressed size of the string. Set to 0 if the string was not compressed.
 * decompressed_size(out) : The uncompressed size of the string.
 */
STATIC_INLINE int
or_get_varchar_compression_lengths (OR_BUF * buf, int *compressed_size, int *decompressed_size)
{
  int compressed_length = 0, decompressed_length = 0, rc = NO_ERROR, net_charlen = 0;
  int size_prefix = 0;

  /* Check if the string is compressed */
  size_prefix = or_get_string_size_byte (buf, &rc);
  if (rc != NO_ERROR)
    {
      assert (size_prefix == 0);
      return rc;
    }

  if (size_prefix == OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION)
    {
      /* String was compressed */
      /* Get the compressed size */
      rc = or_get_data (buf, (char *) &net_charlen, OR_INT_SIZE);
      compressed_length = OR_GET_INT ((char *) &net_charlen);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
      *compressed_size = compressed_length;

      net_charlen = 0;

      /* Get the decompressed size */
      rc = or_get_data (buf, (char *) &net_charlen, OR_INT_SIZE);
      decompressed_length = OR_GET_INT ((char *) &net_charlen);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
      *decompressed_size = decompressed_length;
    }
  else
    {
      /* String was not compressed so we set compressed_size to 0 to know that no compression happened. */
      *compressed_size = 0;
      *decompressed_size = size_prefix;
    }

  return rc;
}

/*
 * or_put_string - write string to or buf
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    str(in): string to write
 *
 * Note:
 *    Does byte padding on strings to bring them up to 4 byte boundary.
 *
 *    There is no or_get_string since this is the same as or_get_data.
 *    Since the workspace allocator (and most other Unix allocators) will
 *    keep track of the size of allocated blocks (and they will be
 *    in word multiples anyway), we can just include the disk padding
 *    bytes with the string when it is brought in from disk even though
 *    the total length may be more than that returned by strlen.
 */
EXTERN_INLINE int
or_put_string_aligned (OR_BUF * buf, char *str)
{
  int len, bits, pad;
  int rc = NO_ERROR;

  if (str == NULL)
    {
      return rc;
    }
  len = strlen (str) + 1;
  rc = or_put_data (buf, str, len);
  if (rc == NO_ERROR)
    {
      /* PAD */
      bits = len & 3;
      if (bits)
	{
	  pad = 4 - bits;
	  rc = or_pad (buf, pad);
	}
    }
  return rc;
}

/*
 *  this function also adds
 *  the length of the string to the buffer
 */
EXTERN_INLINE int
or_put_string_aligned_with_length (OR_BUF * buf, const char *str)
{
  int len;
  int rc = NO_ERROR;

  if (str == NULL)
    {
      return rc;
    }
  len = (int) strlen (str) + 1;

  rc = or_put_int (buf, len);
  if (rc != NO_ERROR)
    {
      return rc;
    }

  rc = or_put_data (buf, str, len);
  if (rc == NO_ERROR)
    {
      or_align (buf, OR_INT_SIZE);
    }
  return rc;
}

/*
 * or_get_string_size_byte - read string size byte value from or buffer
 *    return: byte value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 *
 * NOTE that it is really same as or_get_byte function. It is duplicated to inline the function for performance.
 */
STATIC_INLINE int
or_get_string_size_byte (OR_BUF * buf, int *error)
{
  int size_prefix;

  if ((buf->ptr + OR_BYTE_SIZE) > buf->endptr)
    {
      *error = or_underflow (buf);
      size_prefix = 0;
    }
  else
    {
      size_prefix = OR_GET_BYTE (buf->ptr);
      buf->ptr += OR_BYTE_SIZE;
      *error = NO_ERROR;
    }
  return size_prefix;
}

EXTERN_INLINE int
or_put_offset (OR_BUF * buf, int num)
{
  return or_put_offset_internal (buf, num, BIG_VAR_OFFSET_SIZE);
}

EXTERN_INLINE int
or_put_offset_internal (OR_BUF * buf, int num, int offset_size)
{
  if (offset_size == OR_BYTE_SIZE)
    {
      return or_put_byte (buf, num);
    }
  else if (offset_size == OR_SHORT_SIZE)
    {
      return or_put_short (buf, num);
    }
  else
    {
      assert (offset_size == BIG_VAR_OFFSET_SIZE);

      return or_put_int (buf, num);
    }
}

EXTERN_INLINE int
or_get_offset (OR_BUF * buf, int *error)
{
  return or_get_offset_internal (buf, error, BIG_VAR_OFFSET_SIZE);
}

EXTERN_INLINE int
or_get_offset_internal (OR_BUF * buf, int *error, int offset_size)
{
  if (offset_size == OR_BYTE_SIZE)
    {
      return or_get_byte (buf, error);
    }
  else if (offset_size == OR_SHORT_SIZE)
    {
      return or_get_short (buf, error);
    }
  else
    {
      assert (offset_size == BIG_VAR_OFFSET_SIZE);
      return or_get_int (buf, error);
    }
}

/*
 * or_put_oid - write content of an OID structure from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    oid(in): pointer to OID
 */
EXTERN_INLINE int
or_put_oid (OR_BUF * buf, const OID * oid)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_OID_SIZE) > buf->endptr)
    {
      return (or_overflow (buf));
    }
  else
    {
      if (oid == NULL)
	{
	  OR_PUT_NULL_OID (buf->ptr);
	}
      else
	{
	  /* Cannot allow any temp oid's to be written */
	  if (OID_ISTEMP (oid))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      or_abort (buf);
	    }
	  OR_PUT_OID (buf->ptr, oid);
	}
      buf->ptr += OR_OID_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_get_oid - read content of an OID structure from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    oid(out): pointer to OID
 */
EXTERN_INLINE int
or_get_oid (OR_BUF * buf, OID * oid)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  if ((buf->ptr + OR_OID_SIZE) > buf->endptr)
    {
      return or_underflow (buf);
    }
  else
    {
      OR_GET_OID (buf->ptr, oid);
      buf->ptr += OR_OID_SIZE;
    }
  return NO_ERROR;
}

/*
 * or_packed_varbit_length - returns packed varbit length of or buffer encoding
 *    return: varbit encoding length
 *    bitlen(in): varbit length
 */
EXTERN_INLINE int
or_varbit_length (int bitlen)
{
  return or_varbit_length_internal (bitlen, CHAR_ALIGNMENT);
}

STATIC_INLINE int
or_varbit_length_internal (int bitlen, int align)
{
  int len;

  /* calculate size of length prefix */
  if (bitlen < 0xFF)
    {
      len = 1;
    }
  else
    {
      len = 1 + OR_INT_SIZE;
    }

  /* add in the string length in bytes */
  len += ((bitlen + 7) / 8);

  if (align == INT_ALIGNMENT)
    {
      /* round up to a word boundary */
      len = DB_ALIGN (len, INT_ALIGNMENT);
    }
  return len;
}

/*
 * or_varchar_length - returns length of place holder that can contain
 * package varchar length.
 *    return: length of place holder that can contain packed varchar length
 *    charlen(in): varchar length
 */
EXTERN_INLINE int
or_varchar_length (int charlen)
{
  return or_varchar_length_internal (charlen, CHAR_ALIGNMENT);
}

STATIC_INLINE int
or_varchar_length_internal (int charlen, int align)
{
  int len;

  if (charlen < OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION)
    {
      len = OR_BYTE_SIZE + charlen;
    }
  else
    {
      /*
       * Regarding the new encoding for VARCHAR and VARNCHAR, the strings stored in buffers have this representation:
       * OR_BYTE_SIZE    : First byte in encoding. If it's 0xFF, the string's length is greater than 255.
       *                 : Otherwise, the first byte states the length of the string.
       * 1st OR_INT_SIZE : string's compressed length
       * 2nd OR_INT_SIZE : string's decompressed length
       * charlen         : string's disk length
       */
      len = OR_BYTE_SIZE + OR_INT_SIZE + OR_INT_SIZE + charlen;
    }

  if (align == INT_ALIGNMENT)
    {
      /* size of NULL terminator */
      len += OR_BYTE_SIZE;

      len = DB_ALIGN (len, INT_ALIGNMENT);
    }

  return len;
}

/*
 * or_skip_varbit - skip varbit in or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    align(in):
 */
EXTERN_INLINE int
or_skip_varbit (OR_BUF * buf, int align)
{
  int bitlen;
  int rc = NO_ERROR;

  bitlen = or_get_varbit_length (buf, &rc);
  if (rc == NO_ERROR)
    {
      return (or_skip_varbit_remainder (buf, bitlen, align));
    }
  return rc;
}

/*
 * or_skip_varbit_remainder - skip varbit field of given length in or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    bitlen(in): bitlen to skip
 *    align(in):
 */
EXTERN_INLINE int
or_skip_varbit_remainder (OR_BUF * buf, int bitlen, int align)
{
  int rc = NO_ERROR;

  rc = or_advance (buf, BITS_TO_BYTES (bitlen));
  if (rc == NO_ERROR && align == INT_ALIGNMENT)
    {
      rc = or_get_align32 (buf);
    }
  return rc;
}

/*
 * or_skip_varchar - skip varchar field (length + data) from or buffer
 *    return: NO_ERROR or error code.
 *    buf(in/out): or buffer
 *    align(in):
 */
EXTERN_INLINE int
or_skip_varchar (OR_BUF * buf, int align)
{
  int charlen, rc = NO_ERROR;

  charlen = or_get_varchar_length (buf, &rc);

  if (rc == NO_ERROR)
    {
      return (or_skip_varchar_remainder (buf, charlen, align));
    }

  return rc;
}

/*
 * or_skip_varchar_remainder - skip varchar field of given length
 *    return: NO_ERROR if successful, error code otherwise
 *    buf(in/out): or buffer
 *    charlen(in): length of varchar field to skip
 *    align(in):
 */
EXTERN_INLINE int
or_skip_varchar_remainder (OR_BUF * buf, int charlen, int align)
{
  int rc = NO_ERROR;

  if (align == INT_ALIGNMENT)
    {
      rc = or_advance (buf, charlen + 1);
      if (rc == NO_ERROR)
	{
	  rc = or_get_align32 (buf);
	}
    }
  else
    {
      rc = or_advance (buf, charlen);
    }

  return rc;
}

#endif /* _OBJECT_REPRESENTATION_H_ */
