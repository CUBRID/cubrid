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
#include "lock_table.h"		// LOCK, lock_conv

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

/* simple macro to calculate minimum bytes to contain given bits */
#define BITS_TO_BYTES(bit_cnt)		(((bit_cnt) + 7) / 8)

/* PACK/UNPACK MACROS */

/* NUMERIC */

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

#define OR_PUT_BIGINT(ptr, val) do {  \
     assert (OR_BIGINT_SIZE == OR_INT64_SIZE); \
     INT64* pval =  (INT64*) (val);   \
     OR_PUT_INT64 (ptr, pval);        \
  } while (0)

#define OR_GET_BIGINT(ptr, val) do {  \
     assert (OR_BIGINT_SIZE == OR_INT64_SIZE); \
     INT64* pval =  (INT64*) (val);   \
     OR_GET_INT64 (ptr, pval);        \
  } while (0)

STATIC_INLINE void
OR_PUT_FLOAT (char *ptr, float val)
{
  UINT32 ui = htonf (val);
  memcpy (ptr, &ui, sizeof (ui));
}

#define OR_GET_FLOAT(ptr, value) \
  (*(value) = ntohf (*(UINT32 *) (ptr)))

STATIC_INLINE void
OR_PUT_DOUBLE (char *ptr, double val)
{
  UINT64 ui = htond (val);
  memcpy (ptr, &ui, sizeof (ui));
}

#define OR_GET_DOUBLE(ptr, value) \
  (*(value) = ntohd (*(UINT64 *) (ptr)))

#if __WORDSIZE == 32
#define OR_PUT_PTR(ptr, val)    OR_PUT_INT ((ptr), (val))
#define OR_GET_PTR(ptr)         OR_GET_INT ((ptr))
#else /* __WORDSIZE == 32 */
#define OR_PUT_PTR(ptr, val)    (*(UINTPTR *) ((char *) (ptr)) = swap64 ((UINTPTR) val))
#define OR_GET_PTR(ptr)         ((UINTPTR) swap64 (*(UINTPTR *) ((char *) (ptr))))
#endif /* __WORDSIZE == 64 */

/* EXTENDED TYPE */

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

#define OR_GET_STRING(ptr) \
  ((char *) ((char *) (ptr)))

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

/* VARIABLE OFFSET ACCESSORS */

#define OR_PUT_BIG_VAR_OFFSET(ptr, val) \
  OR_PUT_INT ((ptr), (val))

#define OR_GET_BIG_VAR_OFFSET(ptr) \
  OR_GET_INT ((ptr))

#define OR_PUT_OFFSET(ptr, val) \
  OR_PUT_BIG_VAR_OFFSET ((ptr), (val))

#define OR_GET_OFFSET(ptr) \
  OR_GET_BIG_VAR_OFFSET ((ptr))

#define OR_PUT_OFFSET_INTERNAL(ptr, val, offset_size) \
  do { \
    if ((offset_size) == OR_BYTE_SIZE) \
      { \
	OR_PUT_BYTE ((ptr), (val)); \
      } \
    else if ((offset_size) == OR_SHORT_SIZE) \
      { \
	OR_PUT_SHORT ((ptr), (val)); \
      } \
    else \
      { \
	assert ((offset_size) == OR_INT_SIZE); \
	OR_PUT_INT ((ptr), (val)); \
      } \
  } while (0)

#define OR_GET_OFFSET_INTERNAL(ptr, offset_size) \
  ((offset_size) == OR_BYTE_SIZE) \
   ? OR_GET_BYTE ((ptr)) \
   : (((offset_size) == OR_SHORT_SIZE) \
      ? OR_GET_SHORT ((ptr)) : OR_GET_INT ((ptr)))

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
  extern int valcnv_convert_collection_value_to_string_all_elements (DB_VALUE * value);

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
extern char *or_pack_set_node (char *ptr, void *set_node);
extern char *or_pack_int_array (char *buffer, int count, const int *int_array);
#if defined(ENABLE_UNUSED_FUNCTION)
extern char *or_pack_elo (char *ptr, void *elo);
extern char *or_pack_string_array (char *buffer, int count, const char **string_array);
extern char *or_pack_db_value_array (char *buffer, int count, DB_VALUE * val);
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
extern int or_set_node_length (void *set_node_ptr);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_elo_length (void *elo_ptr);
extern int or_packed_string_array_length (int count, const char **string_array);
extern int or_packed_db_value_array_length (int count, DB_VALUE * val);
#endif

extern void or_encode (char *buffer, const char *source, int size);
extern void or_decode (const char *buffer, char *dest, int size);

STATIC_INLINE void or_init (OR_BUF * buf, char *data, int length) __attribute__ ((ALWAYS_INLINE));

/* Pack/unpack support functions */
STATIC_INLINE int or_advance (OR_BUF * buf, int offset) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_seek (OR_BUF * buf, int psn) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_pad (OR_BUF * buf, int length) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int or_put_align32 (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_align (OR_BUF * buf, int alignment) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_align (OR_BUF * buf, int align) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_align32 (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_align64 (OR_BUF * buf) __attribute__ ((ALWAYS_INLINE));

/*
 * NUMERIC DATA TRANSFORMS
 *    This set of functions handles the transformation of the
 *    numeric types byte, short, integer, float, and double.
 *
 */

STATIC_INLINE int or_put_byte (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_short (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_int (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_bigint (OR_BUF * buf, DB_BIGINT num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_float (OR_BUF * buf, float num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_double (OR_BUF * buf, double num) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int or_get_byte (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_short (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_int (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_BIGINT or_get_bigint (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE float or_get_float (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE double or_get_double (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));

/*
 * EXTENDED TYPE TRANSLATORS
 *    This set of functions reads and writes the extended types time,
 *    utime, date, and monetary.
 */

STATIC_INLINE int or_put_time (OR_BUF * buf, DB_TIME * timeval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_utime (OR_BUF * buf, DB_UTIME * timeval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_date (OR_BUF * buf, DB_DATE * date) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_datetime (OR_BUF * buf, DB_DATETIME * datetimeval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz) __attribute__ ((ALWAYS_INLINE));
extern int or_put_monetary (OR_BUF * buf, DB_MONETARY * monetary);

STATIC_INLINE int or_get_time (OR_BUF * buf, DB_TIME * timeval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_utime (OR_BUF * buf, DB_UTIME * timeval) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_date (OR_BUF * buf, DB_DATE * date) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_datetime (OR_BUF * buf, DB_DATETIME * datetime) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz) __attribute__ ((ALWAYS_INLINE));
extern int or_get_monetary (OR_BUF * buf, DB_MONETARY * monetary);

#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_put_binary (OR_BUF * buf, DB_BINARY * binary);
#endif
STATIC_INLINE int or_put_data (OR_BUF * buf, const char *data, int length) __attribute__ ((ALWAYS_INLINE));
extern int or_put_varbit (OR_BUF * buf, const char *string, int bitlen);
extern int or_put_varchar (OR_BUF * buf, char *string, int size, int length);
STATIC_INLINE int or_put_string_aligned (OR_BUF * buf, char *string) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_string_aligned_with_length (OR_BUF * buf, const char *str) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int or_get_data (OR_BUF * buf, char *data, int length) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_varbit_length (OR_BUF * buf, int *intval) __attribute__ ((ALWAYS_INLINE));
/* Get the compressed and the decompressed lengths of a string stored in buffer */
/* Legacy alias for compression-length-only reads. */
#define or_get_varchar_compression_lengths(buf, compressed_size, decompressed_size) \
  or_get_string_header (buf, NULL, decompressed_size, compressed_size)

STATIC_INLINE int or_varbit_length (int bitlen) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_varchar_length (int length, int size, int compressed_size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_varbit_length_internal (int bitlen, int align) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_varchar_length_internal (int length, int size, int compressed_size, int align)
  __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int or_skip_varbit (OR_BUF * buf, int align) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_skip_varchar (OR_BUF * buf, int align) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_skip_varbit_remainder (OR_BUF * buf, int bitlen, int align) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_skip_varchar_remainder (OR_BUF * buf, int charlen, int align) __attribute__ ((ALWAYS_INLINE));

#if defined(ENABLE_UNUSED_FUNCTION)
extern int or_length_binary (DB_BINARY * binary);
extern int or_length_string (char *string);
#endif

/*
 * DISK IDENTIFIER TRANSLATORS
 *    Translators for the disk identifiers OID, HFID, BTID, EHID.
 */

STATIC_INLINE int or_put_oid (OR_BUF * buf, const OID * oid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_mvccid (OR_BUF * buf, MVCCID mvccid) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int or_get_oid (OR_BUF * buf, OID * oid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_mvccid (OR_BUF * buf, MVCCID * mvccid) __attribute__ ((ALWAYS_INLINE));

/* VARIABLE OFFSET TABLE ACCESSORS */

STATIC_INLINE int or_put_big_var_offset (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_offset (OR_BUF * buf, int num) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_offset_internal (OR_BUF * buf, int num, int offset_size) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int or_get_big_var_offset (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_offset (OR_BUF * buf, int *error) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_offset_internal (OR_BUF * buf, int *error, int offset_size) __attribute__ ((ALWAYS_INLINE));

/* Data unpacking functions */

extern int or_packed_put_varbit (OR_BUF * buf, const char *string, int bitlen);
extern int or_packed_put_varchar (OR_BUF * buf, char *string, int size, int length);
extern int or_packed_varchar_length (int length, int size, int compressed_size);
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

/* Because of the STRING encoding, this one could not be changed for over 255, just lower. */
#define OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION 255

#define OR_IS_STRING_LENGTH_COMPRESSABLE(str_length) \
  ((str_length) >= OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION && (str_length) <= LZ4_MAX_INPUT_SIZE)

/*
 * VARIABLE-LENGTH STRING HEADER on disk (used by both VARCHAR and CHAR)
 *
 * The header begins with a 2-bit header type field stored in the top
 * 2 bits of the first byte. The header type determines which of the
 * following four header formats is used.
 *
 * Each format is optimized to minimize header overhead for different
 * string size ranges:
 *
 *   TINY   ( 1 byte ) : [ header type(2bit) | length(2bit) | size(4bit) ]
 *   SMALL  ( 4 bytes) : [ header type(2bit) | length(14bit) ][ size(16bit) ]
 *   MEDIUM ( 6 bytes) : [ header type(2bit) | length(14bit) ][ size(16bit) ][ compressed_size(16bit) ]
 *   LARGE  (12 bytes) : [ header type(2bit) | length(30bit) ][ size(32bit) ][ compressed_size(32bit) ]
 *
 * After the header, the string payload follows:
 *   - size bytes when stored in uncompressed form
 *   - compressed_size bytes when stored in compressed form
 *
 * Field meanings:
 * length          : character count
 * size            : decompressed byte count (includes CHAR trailing-space padding)
 * compressed_size : LZ4-compressed byte count;
 *                   0 when stored uncompressed
 */

/*
 * Each header type is selected based on the string payload size.
 *
 * The TINY and SMALL formats are used for payloads smaller than the compression threshold
 * (OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION = 255),
 * while MEDIUM and LARGE support compressed storage.
 *
 * type   | bytes | length range                  | size range
 * -------+-------+-------------------------------+-------------------------------
 * TINY   | 1     | 0 ~ 3                         | 0 ~ 12 (13~15 reserved)
 * SMALL  | 4     | 1 ~ 254 (255~16383 reserved)  | 13 ~ 254 (255~65535 reserved)
 * MEDIUM | 6     | 255 ~ 16383                   | 255 ~ 65535
 * LARGE  | 12    | 16384 ~ 1G                    | 65536 ~ 4G
 */
#define OR_STRING_HEADER_TYPE_TINY                (0x0u)
#define OR_STRING_HEADER_TYPE_SMALL               (0x1u)
#define OR_STRING_HEADER_TYPE_MEDIUM              (0x2u)
#define OR_STRING_HEADER_TYPE_LARGE               (0x3u)

/*
 * Shift amounts used to place or extract the header type field.
 *
 * The header type always occupies the top 2 bits of the first byte,
 * so the shift value is:
 *   shift = (word bit width) - 2
 *
 *   SHIFT_IN_BYTE  (6)  : 8-bit byte
 *                         - used when writing TINY headers
 *                         - used when reading all header types
 *                           (the header type can be read from the first byte alone)
 *
 *   SHIFT_IN_SHORT (14) : 16-bit short
 *                         - used when writing SMALL and MEDIUM headers
 *                           whose first header word is 2 bytes
 *
 *   SHIFT_IN_INT   (30) : 32-bit int
 *                         - used when writing LARGE headers
 *                           whose first header word is 4 bytes
 */
#define OR_STRING_HEADER_TYPE_SHIFT_IN_BYTE        (6)
#define OR_STRING_HEADER_TYPE_SHIFT_IN_SHORT       (14)
#define OR_STRING_HEADER_TYPE_SHIFT_IN_INT         (30)

/*
 * Bit masks used to read or write the length field of each header type
 * by accessors (e.g. OR_DISK_STRING_GET_* / OR_DISK_STRING_PUT_*).
 *
 * TINY additionally requires LENGTH_SHIFT_TINY and SIZE_MASK_TINY
 * because its length and size fields are packed into a single byte.
 */
#define OR_STRING_LENGTH_MASK_LARGE                (0x3FFFFFFFu)	/* 30-bit length (LARGE) */
#define OR_STRING_LENGTH_MASK_MEDIUM               (0x3FFFu)	/* 14-bit length (SMALL/MEDIUM) */
#define OR_STRING_LENGTH_MASK_TINY                 (0x3u)	/* 2-bit length (TINY) */

#define OR_STRING_LENGTH_SHIFT_TINY                (4)	/* TINY: shift length above size bits */
#define OR_STRING_SIZE_MASK_TINY                   (0xFu)	/* TINY: 4-bit size field */

/*
 * Maximum size and length thresholds used by
 * or_string_pick_header_type().
 *
 * Header types are primarily divided by string byte count,
 * with the main split at the compression threshold
 * (OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION = 255).
 *
 * However, TINY and MEDIUM additionally require length checks
 * because their length fields are narrower than their size ranges.
 * If the length exceeds the selected type's length field limit,
 * the next larger header type is used.
 *
 * Example:
 *   ASCII "abcd" has size = 4 (fits TINY size range 0~12),
 *   but length = 4 exceeds TINY's 2-bit length limit (max 3),
 *   so SMALL is selected.
 *
 * SMALL has no MAX_LENGTH because its maximum size (254)
 * always fits within the 14-bit length field.
 *
 * LARGE has no MAX_SIZE or MAX_LENGTH because it is the
 * final header type.
 */
#define OR_DISK_STRING_TINY_MAX_SIZE                (12)	/* 3 chars * up to 4 bytes per UTF-8 char */
#define OR_DISK_STRING_TINY_MAX_LENGTH              (3)	/* 2-bit length field limit */
#define OR_DISK_STRING_SMALL_MAX_SIZE               (254)	/* below compression threshold */
#define OR_DISK_STRING_MEDIUM_MAX_SIZE              (65535)	/* 16-bit size/csize field limit */
#define OR_DISK_STRING_MEDIUM_MAX_LENGTH            (16383)	/* 14-bit length field limit */

/* On-disk byte size of each header type's header. */
#define OR_DISK_STRING_TINY_HEADER_SIZE             (1)
#define OR_DISK_STRING_SMALL_HEADER_SIZE            (4)
#define OR_DISK_STRING_MEDIUM_HEADER_SIZE           (6)
#define OR_DISK_STRING_LARGE_HEADER_SIZE            (12)

/*
 * Disk header field accessors used by or_put_string_header()
 * and or_get_string_header() to read or write individual
 * header fields for each header type.
 *
 * GET_* read length / size / compressed_size from a header buffer.
 * PUT_TINY_HEADER packs the entire 1-byte TINY header (type | length | size).
 * PUT_*_LEAD writes the first header word (header type bits combined with length).
 * PUT_*_SIZE / PUT_*_CSIZE write the corresponding field at a fixed offset.
 */

/* GET — read from buffer */
#define OR_DISK_STRING_GET_TINY_LENGTH(b) \
  ((int) (((unsigned int) (unsigned char) (b) >> OR_STRING_LENGTH_SHIFT_TINY) & OR_STRING_LENGTH_MASK_TINY))
#define OR_DISK_STRING_GET_TINY_SIZE(b) \
  ((int) ((unsigned int) (unsigned char) (b) & OR_STRING_SIZE_MASK_TINY))

#define OR_DISK_STRING_GET_SMALL_LENGTH(buf) \
  ((int) ((unsigned int) (unsigned short) OR_GET_SHORT (buf) & OR_STRING_LENGTH_MASK_MEDIUM))
#define OR_DISK_STRING_GET_SMALL_SIZE(buf) \
  ((int) (unsigned short) OR_GET_SHORT ((char *) (buf) + OR_SHORT_SIZE))

#define OR_DISK_STRING_GET_MEDIUM_LENGTH(buf) \
  ((int) ((unsigned int) (unsigned short) OR_GET_SHORT (buf) & OR_STRING_LENGTH_MASK_MEDIUM))
#define OR_DISK_STRING_GET_MEDIUM_SIZE(buf) \
  ((int) (unsigned short) OR_GET_SHORT ((char *) (buf) + OR_SHORT_SIZE))
#define OR_DISK_STRING_GET_MEDIUM_CSIZE(buf) \
  ((int) (unsigned short) OR_GET_SHORT ((char *) (buf) + OR_SHORT_SIZE * 2))

#define OR_DISK_STRING_GET_LARGE_LENGTH(buf) \
  ((int) ((unsigned int) OR_GET_INT (buf) & OR_STRING_LENGTH_MASK_LARGE))
#define OR_DISK_STRING_GET_LARGE_SIZE(buf) \
  (OR_GET_INT ((char *) (buf) + OR_INT_SIZE))
#define OR_DISK_STRING_GET_LARGE_CSIZE(buf) \
  (OR_GET_INT ((char *) (buf) + OR_INT_SIZE * 2))

/* PUT — write to buffer */
#define OR_DISK_STRING_PUT_TINY_HEADER(buf, length, size) \
  (*(unsigned char *) (buf) = \
   (unsigned char) ((OR_STRING_HEADER_TYPE_TINY << OR_STRING_HEADER_TYPE_SHIFT_IN_BYTE) \
                    | (((unsigned int) (length) & OR_STRING_LENGTH_MASK_TINY) << OR_STRING_LENGTH_SHIFT_TINY) \
                    | ((unsigned int) (size) & OR_STRING_SIZE_MASK_TINY)))

#define OR_DISK_STRING_PUT_SMALL_LEAD(buf, length) \
  (OR_PUT_SHORT (buf, \
                (short) ((OR_STRING_HEADER_TYPE_SMALL << OR_STRING_HEADER_TYPE_SHIFT_IN_SHORT) \
                         | ((unsigned int) (length) & OR_STRING_LENGTH_MASK_MEDIUM))))
#define OR_DISK_STRING_PUT_SMALL_SIZE(buf, size) \
  (OR_PUT_SHORT ((char *) (buf) + OR_SHORT_SIZE, (short) (size)))

#define OR_DISK_STRING_PUT_MEDIUM_LEAD(buf, length) \
  (OR_PUT_SHORT (buf, \
                (short) ((OR_STRING_HEADER_TYPE_MEDIUM << OR_STRING_HEADER_TYPE_SHIFT_IN_SHORT) \
                         | ((unsigned int) (length) & OR_STRING_LENGTH_MASK_MEDIUM))))
#define OR_DISK_STRING_PUT_MEDIUM_SIZE(buf, size) \
  (OR_PUT_SHORT ((char *) (buf) + OR_SHORT_SIZE, (short) (size)))
#define OR_DISK_STRING_PUT_MEDIUM_CSIZE(buf, csize) \
  (OR_PUT_SHORT ((char *) (buf) + OR_SHORT_SIZE * 2, (short) (csize)))

#define OR_DISK_STRING_PUT_LARGE_LEAD(buf, length) \
  (OR_PUT_INT (buf, \
              (int) ((OR_STRING_HEADER_TYPE_LARGE << OR_STRING_HEADER_TYPE_SHIFT_IN_INT) \
                     | ((unsigned int) (length) & OR_STRING_LENGTH_MASK_LARGE))))
#define OR_DISK_STRING_PUT_LARGE_SIZE(buf, size) \
  (OR_PUT_INT ((char *) (buf) + OR_INT_SIZE, (int) (size)))
#define OR_DISK_STRING_PUT_LARGE_CSIZE(buf, csize) \
  (OR_PUT_INT ((char *) (buf) + OR_INT_SIZE * 2, (int) (csize)))

/*
 * CHAR/VARCHAR IN-MEMORY STRING HEADER (NOT CLOB/BLOB)
 *
 * The in-memory layout differs from the on-disk layout in two ways:
 *
 *  1. In-memory strings never store compressed bytes (see mr_setmem_char_type_common()),
 *     so the compressed_size field is removed.
 *
 *  2. Without a compression threshold separating SMALL
 *     and MEDIUM, the in-memory layout uses only three
 *     header types: TINY, SMALL, and LARGE.
 *
 *     SMALL therefore absorbs the uncompressed disk
 *     MEDIUM range (size up to 65535, the 16-bit size field limit).
 *
 * Per-header-type byte layout:
 *   TINY  (1 byte)  : [ type(2) | length(2) | size(4) ]
 *   SMALL (4 bytes) : [ type(2) | length(14) ][ size(16) ]
 *   LARGE (8 bytes) : [ type(2) | length(30) ][ size(32) ]
 *
 * Field meanings:
 *   length : character count
 *   size   : uncompressed byte count
 */

/*
 * Maximum size and length thresholds used by or_mem_string_pick_header_type().
 *
 * Header type selection follows the same rules as the
 * on-disk layout, but without a compression threshold.
 *
 * SMALL therefore covers all sizes up to the 16-bit size field limit (65535).
 *
 * LARGE has no MAX_SIZE or MAX_LENGTH because it is the final header type.
 *
 *   type   | bytes | length range | size range
 *   -------+-------+--------------+--------------------------
 *   TINY   | 1     | 0 ~ 3        | 0 ~ 12 (13~15 reserved)
 *   SMALL  | 4     | up to 16383  | 13 ~ 65535
 *   LARGE  | 8     | up to 30-bit | 65536 ~ UINT32_MAX
 */
#define OR_MEM_STRING_TINY_HEADER_SIZE      (1)
#define OR_MEM_STRING_SMALL_HEADER_SIZE     (4)
#define OR_MEM_STRING_LARGE_HEADER_SIZE     (8)

/*
 * Selection works the same as the on-disk version (see line 1480):
 * size first, length checked as fallback.
 */
#define OR_MEM_STRING_TINY_MAX_SIZE         (12)	/* same as disk TINY */
#define OR_MEM_STRING_TINY_MAX_LENGTH       (3)	/* 2-bit length field limit */
#define OR_MEM_STRING_SMALL_MAX_SIZE        (65535)	/* 16-bit size field limit */
#define OR_MEM_STRING_SMALL_MAX_LENGTH      (16383)	/* 14-bit length field limit */

/*
 * In-memory header field accessors.
 *
 * These follow the same GET/PUT pattern as OR_DISK_STRING_*,
 * but the in-memory layout has no compressed_size field and
 * no MEDIUM header type.
 *
 * TINY and SMALL use the same layout as the on-disk version,
 * so their accessors reuse the disk macros.
 *
 * LARGE uses a different layout size (8 bytes instead of 12, 
 * with no compressed_size field), so it defines separate 
 * GET/PUT accessors.
 */

/* GET — read from buffer */
#define OR_MEM_STRING_GET_TINY_LENGTH(b)          (OR_DISK_STRING_GET_TINY_LENGTH (b))
#define OR_MEM_STRING_GET_TINY_SIZE(b)            (OR_DISK_STRING_GET_TINY_SIZE (b))
#define OR_MEM_STRING_GET_SMALL_LENGTH(buf)       (OR_DISK_STRING_GET_SMALL_LENGTH (buf))
#define OR_MEM_STRING_GET_SMALL_SIZE(buf)         (OR_DISK_STRING_GET_SMALL_SIZE (buf))
#define OR_MEM_STRING_GET_LARGE_LENGTH(buf) \
  ((int) ((unsigned int) OR_GET_INT (buf) & OR_STRING_LENGTH_MASK_LARGE))
#define OR_MEM_STRING_GET_LARGE_SIZE(buf) \
  (OR_GET_INT ((char *) (buf) + OR_INT_SIZE))

/* PUT — write to buffer */
#define OR_MEM_STRING_PUT_TINY_HEADER(buf, length, size)   (OR_DISK_STRING_PUT_TINY_HEADER (buf, length, size))
#define OR_MEM_STRING_PUT_SMALL_LEAD(buf, length)          (OR_DISK_STRING_PUT_SMALL_LEAD (buf, length))
#define OR_MEM_STRING_PUT_SMALL_SIZE(buf, size)            (OR_DISK_STRING_PUT_SMALL_SIZE (buf, size))
#define OR_MEM_STRING_PUT_LARGE_LEAD(buf, length) \
  (OR_PUT_INT (buf, \
              (int) ((OR_STRING_HEADER_TYPE_LARGE << OR_STRING_HEADER_TYPE_SHIFT_IN_INT) \
                     | ((unsigned int) (length) & OR_STRING_LENGTH_MASK_LARGE))))
#define OR_MEM_STRING_PUT_LARGE_SIZE(buf, size) \
  (OR_PUT_INT ((char *) (buf) + OR_INT_SIZE, (int) (size)))

STATIC_INLINE unsigned int or_string_pick_header_type (int length, int size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_string_header (OR_BUF * buf, int length, int size, int compressed_size)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_string_header (OR_BUF * buf, int *length, int *size, int *compressed_size)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_string_header_size (int length, int size) __attribute__ ((ALWAYS_INLINE));

STATIC_INLINE unsigned int or_mem_string_pick_header_type (int length, int size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_put_mem_string_header (char *mem, int length, int size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_get_mem_string_header (char *mem, int *length, int *size) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_mem_string_header_size (int length, int size) __attribute__ ((ALWAYS_INLINE));

/*
 * MIDXKEY HEADER ACCESSORS
 */

STATIC_INLINE int or_multi_nullmap_size (const int n_elements) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_multi_offset_table_size (const int n_elements) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_multi_header_size (const int n_elements) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void or_multi_clear_header (char *nullmap_ptr, const int n_elements) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void or_multi_set_not_null (char *nullmap_ptr, const int index) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void or_multi_set_null (char *nullmap_ptr, const int index) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool or_multi_is_not_null (char *nullmap_ptr, const int index) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool or_multi_is_null (char *nullmap_ptr, const int index) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE char *or_multi_get_offset_table (char *nullmap_ptr, const int n_elements) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE char *or_multi_get_element_offset_ptr (char *nullmap_ptr, const int n_elements, const int index)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_multi_get_element_offset_internal (char *nullmap_ptr, const int n_elements, const int index)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_multi_get_element_offset (char *nullmap_ptr, const int n_elements, const int index)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_multi_get_next_element_offset (char *nullmap_ptr, const int n_elements, const int index)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int or_multi_get_size_offset (char *nullmap_ptr, const int n_elements) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void or_multi_put_element_offset_internal (char *nullmap_ptr, const int n_elements, const int offset,
							 const int index) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void or_multi_put_element_offset (char *nullmap_ptr, const int n_elements, const int offset,
						const int index) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void or_multi_put_next_element_offset (char *nullmap_ptr, const int n_elements, const int offset,
						     const int index) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void or_multi_put_size_offset (char *nullmap_ptr, const int n_elements, const int offset)
  __attribute__ ((ALWAYS_INLINE));

/*
 * or_init - initialize the field of an OR_BUF
 *    return: void
 *    buf(in/out): or buffer to initialize
 *    data(in): buffer data
 *    length(in):  buffer data length
 */
STATIC_INLINE void
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

  buf->fixups = NULL;
}

/*
 * OR_BUF PACK/UNPACK FUNCTIONS
 */

/*
 * or_seek - This sets the translation pointer directly to a certain byte in
 * the buffer.
 *    return: ERROR_SUCCESS or error code
 *    buf(in/out): or buffer
 *    psn(in): position within buffer
 */
STATIC_INLINE int
or_seek (OR_BUF * buf, int psn)
{
  assert (buf->buffer + psn <= buf->endptr);
  buf->ptr = buf->buffer + psn;
  return NO_ERROR;
}

/*
 * or_advance - This advances the translation pointer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    offset(in): number of bytes to skip
 */
STATIC_INLINE int
or_advance (OR_BUF * buf, int offset)
{
  assert (buf->ptr + offset <= buf->endptr);
  buf->ptr += offset;
  return NO_ERROR;
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
STATIC_INLINE int
or_pad (OR_BUF * buf, int length)
{
  assert (buf->ptr + length <= buf->endptr);
  (void) memset (buf->ptr, 0, length);
  buf->ptr += length;
  return NO_ERROR;
}

/*
 * or_put_align32 - pad zero bytes round up to 4 byte bound
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 */
STATIC_INLINE int
or_put_align32 (OR_BUF * buf)
{
  unsigned int bits;
  int rc = NO_ERROR;

  bits = (UINTPTR) buf->ptr & 3;
  if (bits)
    {
      assert (buf->ptr + 4 - bits <= buf->endptr);
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
STATIC_INLINE int
or_align (OR_BUF * buf, int alignment)
{
  char *new_ptr = PTR_ALIGN (buf->ptr, alignment);
  assert (new_ptr <= buf->endptr);
  buf->ptr = new_ptr;
  return NO_ERROR;
}

/*
 * or_get_align - adnvance or buf pointer to next alignment position
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    align(in):
 */
STATIC_INLINE int
or_get_align (OR_BUF * buf, int align)
{
  char *ptr;

  ptr = PTR_ALIGN (buf->ptr, align);
  assert (ptr <= buf->endptr);
  buf->ptr = ptr;
  return NO_ERROR;
}

/*
 * or_get_align32 - adnvance or buf pointer to next 4 byte alignment position
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 */
STATIC_INLINE int
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
STATIC_INLINE int
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
STATIC_INLINE int
or_put_byte (OR_BUF * buf, int num)
{
  assert (buf->ptr + OR_BYTE_SIZE <= buf->endptr);
  OR_PUT_BYTE (buf->ptr, num);
  buf->ptr += OR_BYTE_SIZE;
  return NO_ERROR;
}

/*
 * or_get_byte - read a byte value from or buffer
 *    return: byte value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
STATIC_INLINE int
or_get_byte (OR_BUF * buf, int *error)
{
  int value = 0;

  assert (buf->ptr + OR_BYTE_SIZE <= buf->endptr);
  value = OR_GET_BYTE (buf->ptr);
  buf->ptr += OR_BYTE_SIZE;
  *error = NO_ERROR;

  return value;
}

/*
 * or_put_short - put a short value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    num(in): short value to put
 */
STATIC_INLINE int
or_put_short (OR_BUF * buf, int num)
{
  ASSERT_ALIGN (buf->ptr, SHORT_ALIGNMENT);

  assert (buf->ptr + OR_SHORT_SIZE <= buf->endptr);
  OR_PUT_SHORT (buf->ptr, num);
  buf->ptr += OR_SHORT_SIZE;
  return NO_ERROR;
}

/*
 * or_get_short - read a short value from or buffer
 *    return: short value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
STATIC_INLINE int
or_get_short (OR_BUF * buf, int *error)
{
  int value = 0;

  ASSERT_ALIGN (buf->ptr, SHORT_ALIGNMENT);

  assert (buf->ptr + OR_SHORT_SIZE <= buf->endptr);
  value = OR_GET_SHORT (buf->ptr);
  buf->ptr += OR_SHORT_SIZE;
  *error = NO_ERROR;
  return value;
}

/*
 * or_put_int - put int value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    num(in): int value to put
 */
STATIC_INLINE int
or_put_int (OR_BUF * buf, int num)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);
  assert (buf->ptr + OR_INT_SIZE <= buf->endptr);
  OR_PUT_INT (buf->ptr, num);
  buf->ptr += OR_INT_SIZE;
  return NO_ERROR;
}

/*
 * or_get_int - get int value from or buffer
 *    return: int value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
STATIC_INLINE int
or_get_int (OR_BUF * buf, int *error)
{
  int value = 0;

  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_INT_SIZE <= buf->endptr);
  value = OR_GET_INT (buf->ptr);
  buf->ptr += OR_INT_SIZE;
  *error = NO_ERROR;
  return value;
}


/*
 * or_put_bigint - put bigint value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    num(in): bigint value to put
 */
STATIC_INLINE int
or_put_bigint (OR_BUF * buf, DB_BIGINT num)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_BIGINT_SIZE <= buf->endptr);
  OR_PUT_BIGINT (buf->ptr, &num);
  buf->ptr += OR_BIGINT_SIZE;
  return NO_ERROR;
}

/*
 * or_get_bigint - get bigint value from or buffer
 *    return: bigint value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
STATIC_INLINE DB_BIGINT
or_get_bigint (OR_BUF * buf, int *error)
{
  DB_BIGINT value = 0;

  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_BIGINT_SIZE <= buf->endptr);

  OR_GET_BIGINT (buf->ptr, &value);
  buf->ptr += OR_BIGINT_SIZE;
  *error = NO_ERROR;
  return value;
}

/*
 * or_put_float - put a float value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    fnum(in): float value to put
 */
STATIC_INLINE int
or_put_float (OR_BUF * buf, float fnum)
{
  ASSERT_ALIGN (buf->ptr, FLOAT_ALIGNMENT);

  assert (buf->ptr + OR_FLOAT_SIZE <= buf->endptr);
  OR_PUT_FLOAT (buf->ptr, fnum);
  buf->ptr += OR_FLOAT_SIZE;
  return NO_ERROR;
}

/*
 * or_get_float - read a float value from or buffer
 *    return: float value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
STATIC_INLINE float
or_get_float (OR_BUF * buf, int *error)
{
  float value = 0.0;

  ASSERT_ALIGN (buf->ptr, FLOAT_ALIGNMENT);

  assert (buf->ptr + OR_FLOAT_SIZE <= buf->endptr);

  OR_GET_FLOAT (buf->ptr, &value);
  buf->ptr += OR_FLOAT_SIZE;
  *error = NO_ERROR;
  return value;
}

/*
 * or_put_double - put a double value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    dnum(in): double value to put
 */
STATIC_INLINE int
or_put_double (OR_BUF * buf, double dnum)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_DOUBLE_SIZE <= buf->endptr);
  OR_PUT_DOUBLE (buf->ptr, dnum);
  buf->ptr += OR_DOUBLE_SIZE;
  return NO_ERROR;
}

/*
 * or_get_double - read a double value from or buffer
 *    return: double value read
 *    buf(in/out): or buffer
 *    error(out): NO_ERROR or error code
 */
STATIC_INLINE double
or_get_double (OR_BUF * buf, int *error)
{
  double value = 0.0;

  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);
  assert (buf->ptr + OR_DOUBLE_SIZE <= buf->endptr);

  OR_GET_DOUBLE (buf->ptr, &value);
  buf->ptr += OR_DOUBLE_SIZE;
  *error = NO_ERROR;
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
STATIC_INLINE int
or_put_time (OR_BUF * buf, DB_TIME * timeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_TIME_SIZE <= buf->endptr);
  OR_PUT_TIME (buf->ptr, timeval);
  buf->ptr += OR_TIME_SIZE;
  return NO_ERROR;
}

/*
 * or_get_time - read a  DB_TIME from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    timeval(out): pointer to DB_TIME value
 */
STATIC_INLINE int
or_get_time (OR_BUF * buf, DB_TIME * timeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);
  assert (buf->ptr + OR_TIME_SIZE <= buf->endptr);
  OR_GET_TIME (buf->ptr, timeval);
  buf->ptr += OR_TIME_SIZE;
  return NO_ERROR;
}

/*
 * or_put_utime - write a timestamp value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    timeval(in): pointer to timestamp value
 */
STATIC_INLINE int
or_put_utime (OR_BUF * buf, DB_UTIME * timeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_UTIME_SIZE <= buf->endptr);
  OR_PUT_UTIME (buf->ptr, timeval);
  buf->ptr += OR_UTIME_SIZE;
  return NO_ERROR;
}

/*
 * or_get_utime - read a timestamp value from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    timeval(out): pointer to timestamp value
 */
STATIC_INLINE int
or_get_utime (OR_BUF * buf, DB_UTIME * timeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);
  assert (buf->ptr + OR_UTIME_SIZE <= buf->endptr);

  OR_GET_UTIME (buf->ptr, timeval);
  buf->ptr += OR_UTIME_SIZE;
  return NO_ERROR;
}

/*
 * or_put_timestamptz - write a timestamp with tz value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    ts_tz(in): pointer to DB_TIMESTAMPTZ value
 */
STATIC_INLINE int
or_put_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_TIMESTAMPTZ_SIZE <= buf->endptr);
  OR_PUT_TIMESTAMPTZ (buf->ptr, ts_tz);
  buf->ptr += OR_TIMESTAMPTZ_SIZE;
  return NO_ERROR;
}

/*
 * or_get_timestamptz - read a timestamp with tz value from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    ts_tz(out): pointer to DB_TIMESTAMPTZ value
 */
STATIC_INLINE int
or_get_timestamptz (OR_BUF * buf, DB_TIMESTAMPTZ * ts_tz)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_TIMESTAMPTZ_SIZE <= buf->endptr);
  OR_GET_TIMESTAMPTZ (buf->ptr, ts_tz);
  buf->ptr += OR_TIMESTAMPTZ_SIZE;
  return NO_ERROR;
}

/*
 * or_put_date - write a DB_DATE value to or_buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    date(in): pointer to DB_DATE value
 */
STATIC_INLINE int
or_put_date (OR_BUF * buf, DB_DATE * date)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_DATE_SIZE <= buf->endptr);
  OR_PUT_DATE (buf->ptr, date);
  buf->ptr += OR_DATE_SIZE;
  return NO_ERROR;
}

/*
 * or_get_date - read a DB_DATE value from or_buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    date(out): pointer to DB_DATE value
 */
STATIC_INLINE int
or_get_date (OR_BUF * buf, DB_DATE * date)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_DATE_SIZE <= buf->endptr);
  OR_GET_DATE (buf->ptr, date);
  buf->ptr += OR_DATE_SIZE;
  return NO_ERROR;
}

/*
 * or_put_datetime - write a datetime value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    datetimeval(in): pointer to datetime value
 */
STATIC_INLINE int
or_put_datetime (OR_BUF * buf, DB_DATETIME * datetimeval)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_DATETIME_SIZE <= buf->endptr);
  OR_PUT_DATETIME (buf->ptr, datetimeval);
  buf->ptr += OR_DATETIME_SIZE;
  return NO_ERROR;
}

/*
 * or_get_datetime - read a DB_DATETIME value from or_buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    date(out): pointer to DB_DATETIME value
 */
STATIC_INLINE int
or_get_datetime (OR_BUF * buf, DB_DATETIME * datetime)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_DATETIME_SIZE <= buf->endptr);
  OR_GET_DATETIME (buf->ptr, datetime);
  buf->ptr += OR_DATETIME_SIZE;
  return NO_ERROR;
}

/*
 * or_put_datetimetz - write a datetime with tz value to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    datetimetz(in): pointer to DB_DATETIMETZ value
 */
STATIC_INLINE int
or_put_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_DATETIMETZ_SIZE <= buf->endptr);
  OR_PUT_DATETIMETZ (buf->ptr, datetimetz);
  buf->ptr += OR_DATETIMETZ_SIZE;
  return NO_ERROR;
}

/*
 * or_get_datetimetz - read a datetime with tz value from or_buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    datetimetz(out): pointer to DB_DATETIMETZ value
 */
STATIC_INLINE int
or_get_datetimetz (OR_BUF * buf, DB_DATETIMETZ * datetimetz)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_DATETIMETZ_SIZE <= buf->endptr);
  OR_GET_DATETIMETZ (buf->ptr, datetimetz);
  buf->ptr += OR_DATETIMETZ_SIZE;
  return NO_ERROR;
}

/*
 * or_put_data - write an array of bytes to or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    data(in): pointer to data
 *    length(in): length in bytes
 */
STATIC_INLINE int
or_put_data (OR_BUF * buf, const char *data, int length)
{
  assert (buf->ptr + length <= buf->endptr);
  (void) memcpy (buf->ptr, data, length);
  buf->ptr += length;
  return NO_ERROR;
}

/*
 * or_get_data - read an array of bytes from or buffer for given length
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    data(in): pointer to buffer to read data into
 *    length(in): length of read data
 */
STATIC_INLINE int
or_get_data (OR_BUF * buf, char *data, int length)
{
  assert (buf->ptr + length <= buf->endptr);
  (void) memcpy (data, buf->ptr, length);
  buf->ptr += length;
  return NO_ERROR;
}

/*
 * or_get_varbit_length - get varbit length from or buffer
 *    return: length of varbit or 0 if error
 *    buf(in/out): or buffer
 *    rc(out): NO_ERROR or error code
 */
STATIC_INLINE int
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
 * or_put_string_header() - Write the variable-length string header.
 *
 *   return              : NO_ERROR or error code
 *   buf(in/out)         : or buffer
 *   length(in)          : character count
 *   size(in)            : decompressed byte count
 *   compressed_size(in) : LZ4-compressed byte count; 0 when stored uncompressed
 *                        (only present in MEDIUM and LARGE headers)
 *
 * Note:
 *   See the VARIABLE-LENGTH STRING HEADER comment above for the
 *   on-disk layout and header type selection rule.
 *
 *   Uses byte-level writes (OR_PUT_* + or_put_data) instead of
 *   or_put_int() / or_put_short() because this function may be
 *   called with unaligned buffers from CHAR_ALIGNMENT contexts.
 */
STATIC_INLINE int
or_put_string_header (OR_BUF * buf, int length, int size, int compressed_size)
{
  switch (or_string_pick_header_type (length, size))
    {
    case OR_STRING_HEADER_TYPE_TINY:
      {
	char header_buf[OR_DISK_STRING_TINY_HEADER_SIZE];
	OR_DISK_STRING_PUT_TINY_HEADER (header_buf, length, size);
	return or_put_data (buf, header_buf, OR_DISK_STRING_TINY_HEADER_SIZE);
      }

    case OR_STRING_HEADER_TYPE_SMALL:
      {
	char header_buf[OR_DISK_STRING_SMALL_HEADER_SIZE];
	OR_DISK_STRING_PUT_SMALL_LEAD (header_buf, length);
	OR_DISK_STRING_PUT_SMALL_SIZE (header_buf, size);
	return or_put_data (buf, header_buf, OR_DISK_STRING_SMALL_HEADER_SIZE);
      }

    case OR_STRING_HEADER_TYPE_MEDIUM:
      {
	char header_buf[OR_DISK_STRING_MEDIUM_HEADER_SIZE];
	OR_DISK_STRING_PUT_MEDIUM_LEAD (header_buf, length);
	OR_DISK_STRING_PUT_MEDIUM_SIZE (header_buf, size);
	OR_DISK_STRING_PUT_MEDIUM_CSIZE (header_buf, compressed_size);
	return or_put_data (buf, header_buf, OR_DISK_STRING_MEDIUM_HEADER_SIZE);
      }

    case OR_STRING_HEADER_TYPE_LARGE:
      {
	char header_buf[OR_DISK_STRING_LARGE_HEADER_SIZE];
	OR_DISK_STRING_PUT_LARGE_LEAD (header_buf, length);
	OR_DISK_STRING_PUT_LARGE_SIZE (header_buf, size);
	OR_DISK_STRING_PUT_LARGE_CSIZE (header_buf, compressed_size);
	return or_put_data (buf, header_buf, OR_DISK_STRING_LARGE_HEADER_SIZE);
      }

    default:
      assert (false);
      return ER_FAILED;
    }
}

/*
 * or_get_string_header() - Read the variable-length string header.
 *
 *   return               : NO_ERROR or error code
 *   buf(in/out)          : or buffer
 *   length(out)          : character count   (NULL to skip)
 *   size(out)            : decompressed byte count   (NULL to skip)
 *   compressed_size(out) : LZ4-compressed byte count, 0 when stored uncompressed
 *                          (only present in MEDIUM and LARGE headers; NULL to skip)
 *
 * Note:
 *   See the VARIABLE-LENGTH STRING HEADER comment above for the
 *   on-disk layout.
 *
 *   Byte 0 is peeked to determine the header type from the top
 *   2 bits. For SMALL, MEDIUM, and LARGE the peek does not advance
 *   buf->ptr, so the subsequent read re-includes byte 0.
 *
 *   Uses byte-level reads (or_get_data() + OR_GET_*) instead of
 *   or_get_int() / or_get_short() because this function may be
 *   called with unaligned buffers from CHAR_ALIGNMENT contexts.
 */
STATIC_INLINE int
or_get_string_header (OR_BUF * buf, int *length, int *size, int *compressed_size)
{
  int rc;
  int tmp_length = 0, tmp_size = 0, tmp_csize = 0;
  unsigned int header_type;
  unsigned char peek_byte;

  assert (buf->ptr + OR_BYTE_SIZE <= buf->endptr);
  peek_byte = (unsigned char) OR_GET_BYTE (buf->ptr);
  header_type = (unsigned int) peek_byte >> OR_STRING_HEADER_TYPE_SHIFT_IN_BYTE;

  switch (header_type)
    {
    case OR_STRING_HEADER_TYPE_TINY:
      /* TINY: 1 byte — entire header already in peek_byte */
      tmp_length = OR_DISK_STRING_GET_TINY_LENGTH (peek_byte);
      tmp_size = OR_DISK_STRING_GET_TINY_SIZE (peek_byte);
      buf->ptr += OR_BYTE_SIZE;
      break;

    case OR_STRING_HEADER_TYPE_SMALL:
      {
	char header_buf[OR_DISK_STRING_SMALL_HEADER_SIZE];
	rc = or_get_data (buf, header_buf, OR_DISK_STRING_SMALL_HEADER_SIZE);
	if (rc != NO_ERROR)
	  {
	    return rc;
	  }
	tmp_length = OR_DISK_STRING_GET_SMALL_LENGTH (header_buf);
	tmp_size = OR_DISK_STRING_GET_SMALL_SIZE (header_buf);
	break;
      }

    case OR_STRING_HEADER_TYPE_MEDIUM:
      {
	char header_buf[OR_DISK_STRING_MEDIUM_HEADER_SIZE];
	rc = or_get_data (buf, header_buf, OR_DISK_STRING_MEDIUM_HEADER_SIZE);
	if (rc != NO_ERROR)
	  {
	    return rc;
	  }
	tmp_length = OR_DISK_STRING_GET_MEDIUM_LENGTH (header_buf);
	tmp_size = OR_DISK_STRING_GET_MEDIUM_SIZE (header_buf);
	tmp_csize = OR_DISK_STRING_GET_MEDIUM_CSIZE (header_buf);
	break;
      }

    case OR_STRING_HEADER_TYPE_LARGE:
      {
	char header_buf[OR_DISK_STRING_LARGE_HEADER_SIZE];
	rc = or_get_data (buf, header_buf, OR_DISK_STRING_LARGE_HEADER_SIZE);
	if (rc != NO_ERROR)
	  {
	    return rc;
	  }
	tmp_length = OR_DISK_STRING_GET_LARGE_LENGTH (header_buf);
	tmp_size = OR_DISK_STRING_GET_LARGE_SIZE (header_buf);
	tmp_csize = OR_DISK_STRING_GET_LARGE_CSIZE (header_buf);
	break;
      }

    default:
      assert (false);
      return ER_FAILED;
    }

  if (length != NULL)
    {
      *length = tmp_length;
    }
  if (size != NULL)
    {
      *size = tmp_size;
    }
  if (compressed_size != NULL)
    {
      *compressed_size = tmp_csize;
    }
  return NO_ERROR;
}

/*
 * or_string_pick_header_type() - Select the header type used for a
 *                                string of given length and size.
 *
 *   return     : OR_STRING_HEADER_TYPE_{TINY,SMALL,MEDIUM,LARGE}
 *   length(in) : character count
 *   size(in)   : decompressed byte count
 *
 * Note:
 *   Selects the smallest header type that can represent both
 *   the string size and length.
 *
 *   Shared by or_string_header_size() and
 *   or_put_string_header() so the computed header size
 *   always matches the actual written layout.
 */
STATIC_INLINE unsigned int
or_string_pick_header_type (int length, int size)
{
  if (size <= OR_DISK_STRING_TINY_MAX_SIZE && length <= OR_DISK_STRING_TINY_MAX_LENGTH)
    {
      return OR_STRING_HEADER_TYPE_TINY;
    }
  if (size <= OR_DISK_STRING_SMALL_MAX_SIZE)
    {
      return OR_STRING_HEADER_TYPE_SMALL;
    }
  if (size <= OR_DISK_STRING_MEDIUM_MAX_SIZE && length <= OR_DISK_STRING_MEDIUM_MAX_LENGTH)
    {
      return OR_STRING_HEADER_TYPE_MEDIUM;
    }
  return OR_STRING_HEADER_TYPE_LARGE;
}

/*
 * or_string_header_size() - Variable-length string header byte count.
 *
 *   return              : header byte count (1 / 4 / 6 / 12)
 *   length(in)          : character count
 *   size(in)            : decompressed byte count (caller's data size)
 *
 * Note:
 *   Mirrors or_put_string_header / or_get_string_header — uses the same
 *   or_string_pick_header_type() dispatch so the predicted byte count always
 *   matches what the emitter actually writes.
 */
STATIC_INLINE int
or_string_header_size (int length, int size)
{
  switch (or_string_pick_header_type (length, size))
    {
    case OR_STRING_HEADER_TYPE_TINY:
      return OR_DISK_STRING_TINY_HEADER_SIZE;
    case OR_STRING_HEADER_TYPE_SMALL:
      return OR_DISK_STRING_SMALL_HEADER_SIZE;
    case OR_STRING_HEADER_TYPE_MEDIUM:
      return OR_DISK_STRING_MEDIUM_HEADER_SIZE;
    case OR_STRING_HEADER_TYPE_LARGE:
      return OR_DISK_STRING_LARGE_HEADER_SIZE;
    default:
      assert (false);
      return OR_DISK_STRING_LARGE_HEADER_SIZE;
    }
}

/*
 * or_mem_string_pick_header_type() - Select the header type used for
 *                                    an in-memory string of given
 *                                    length and size.
 *
 *   return     : OR_STRING_HEADER_TYPE_{TINY, SMALL, LARGE} (never MEDIUM)
 *   length(in) : character count
 *   size(in)   : byte count (always uncompressed in memory)
 *
 * Note:
 *   In-memory strings are never compressed, so the
 *   MEDIUM header type is not used.
 *
 *   See the CHAR/VARCHAR IN-MEMORY STRING HEADER comment
 *   above for the layout and selection thresholds.
 *
 *   Shared by or_mem_string_header_size() and
 *   or_put_mem_string_header() so the computed header size
 *   always matches the actual written layout.
 */
STATIC_INLINE unsigned int
or_mem_string_pick_header_type (int length, int size)
{
  if (size <= OR_MEM_STRING_TINY_MAX_SIZE && length <= OR_MEM_STRING_TINY_MAX_LENGTH)
    {
      return OR_STRING_HEADER_TYPE_TINY;
    }
  if (size <= OR_MEM_STRING_SMALL_MAX_SIZE && length <= OR_MEM_STRING_SMALL_MAX_LENGTH)
    {
      return OR_STRING_HEADER_TYPE_SMALL;
    }
  return OR_STRING_HEADER_TYPE_LARGE;
}

/*
 * or_mem_string_header_size() - Mem header byte count for the given (length, size).
 */
STATIC_INLINE int
or_mem_string_header_size (int length, int size)
{
  switch (or_mem_string_pick_header_type (length, size))
    {
    case OR_STRING_HEADER_TYPE_TINY:
      return OR_MEM_STRING_TINY_HEADER_SIZE;
    case OR_STRING_HEADER_TYPE_SMALL:
      return OR_MEM_STRING_SMALL_HEADER_SIZE;
    case OR_STRING_HEADER_TYPE_LARGE:
      return OR_MEM_STRING_LARGE_HEADER_SIZE;
    default:
      assert (false);
      return OR_MEM_STRING_LARGE_HEADER_SIZE;
    }
}

/*
 * or_put_mem_string_header() - Write the in-memory string header
 *                              (no compressed_size field).
 *
 *   return     : NO_ERROR (cannot fail; int return type kept for
 *                API parity with or_get_mem_string_header())
 *   mem(out)   : caller-allocated memory buffer; must have at least
 *                or_mem_string_header_size(length, size) bytes available
 *   length(in) : character count
 *   size(in)   : uncompressed byte count
 *
 * Note:
 *   See the CHAR/VARCHAR IN-MEMORY STRING HEADER comment above
 *   for the layout and selection thresholds.
 *
 *   Unlike or_put_string_header(), this function does not use
 *   OR_BUF, perform bounds checks, or advance any pointer.
 *   The caller owns the destination buffer and its lifetime.
 */
STATIC_INLINE int
or_put_mem_string_header (char *mem, int length, int size)
{
  switch (or_mem_string_pick_header_type (length, size))
    {
    case OR_STRING_HEADER_TYPE_TINY:
      OR_MEM_STRING_PUT_TINY_HEADER (mem, length, size);
      return NO_ERROR;

    case OR_STRING_HEADER_TYPE_SMALL:
      OR_MEM_STRING_PUT_SMALL_LEAD (mem, length);
      OR_MEM_STRING_PUT_SMALL_SIZE (mem, size);
      return NO_ERROR;

    case OR_STRING_HEADER_TYPE_LARGE:
      OR_MEM_STRING_PUT_LARGE_LEAD (mem, length);
      OR_MEM_STRING_PUT_LARGE_SIZE (mem, size);
      return NO_ERROR;

    default:
      assert (false);
      return ER_FAILED;
    }
}

/*
 * or_get_mem_string_header() - Read the in-memory string header.
 *
 *   return    : NO_ERROR or ER_FAILED
 *   mem(in)   : memory pointer at the header start
 *   length(out): character count   (NULL to skip)
 *   size(out)  : byte count        (NULL to skip)
 *
 * Note:
 *   See the CHAR/VARCHAR IN-MEMORY STRING HEADER comment above
 *   for the layout.
 *
 *   Byte 0 is peeked to determine the header type from the top
 *   2 bits. MEDIUM is rejected because in-memory strings never
 *   use that header type.
 *
 *   Unlike or_get_string_header(), this function does not use
 *   OR_BUF or advance any pointer. The caller computes the
 *   payload start as:
 *     mem + or_mem_string_header_size(length, size)
 */
STATIC_INLINE int
or_get_mem_string_header (char *mem, int *length, int *size)
{
  int tmp_length = 0, tmp_size = 0;
  unsigned int header_type;
  unsigned char peek_byte;

  peek_byte = (unsigned char) OR_GET_BYTE (mem);
  header_type = (unsigned int) peek_byte >> OR_STRING_HEADER_TYPE_SHIFT_IN_BYTE;

  /* Parse into locals via OR_MEM_STRING_GET_* accessors; commit at bottom. */
  switch (header_type)
    {
    case OR_STRING_HEADER_TYPE_TINY:
      /* TINY: 1 byte — entire header already in peek_byte */
      tmp_length = OR_MEM_STRING_GET_TINY_LENGTH (peek_byte);
      tmp_size = OR_MEM_STRING_GET_TINY_SIZE (peek_byte);
      break;

    case OR_STRING_HEADER_TYPE_SMALL:
      tmp_length = OR_MEM_STRING_GET_SMALL_LENGTH (mem);
      tmp_size = OR_MEM_STRING_GET_SMALL_SIZE (mem);
      break;

    case OR_STRING_HEADER_TYPE_LARGE:
      tmp_length = OR_MEM_STRING_GET_LARGE_LENGTH (mem);
      tmp_size = OR_MEM_STRING_GET_LARGE_SIZE (mem);
      break;

    case OR_STRING_HEADER_TYPE_MEDIUM:
      assert (false);
      return ER_FAILED;

    default:
      assert (false);
      return ER_FAILED;
    }

  if (length != NULL)
    {
      *length = tmp_length;
    }
  if (size != NULL)
    {
      *size = tmp_size;
    }
  return NO_ERROR;
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
STATIC_INLINE int
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
STATIC_INLINE int
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
 * or_packed_varbit_length - returns packed varbit length of or buffer encoding
 *    return: varbit encoding length
 *    bitlen(in): varbit length
 */
STATIC_INLINE int
or_varbit_length (int bitlen)
{
  return or_varbit_length_internal (bitlen, CHAR_ALIGNMENT);
}

/*
 * or_varchar_length - byte count of a packed varchar (header + data).
 *
 *   return              : header bytes + data bytes (CHAR_ALIGNMENT, no NUL/pad)
 *   length(in)          : character count
 *   size(in)            : decompressed byte count
 *   compressed_size(in) : LZ4-compressed byte count; 0 when stored uncompressed
 */
STATIC_INLINE int
or_varchar_length (int length, int size, int compressed_size)
{
  return or_varchar_length_internal (length, size, compressed_size, CHAR_ALIGNMENT);
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

STATIC_INLINE int
or_varchar_length_internal (int length, int size, int compressed_size, int align)
{
  int header = or_string_header_size (length, size);
  int data = (compressed_size > 0) ? compressed_size : size;
  int len = header + data;

  if (align == INT_ALIGNMENT)
    {
      len += OR_BYTE_SIZE;	/* trailing NUL */
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
STATIC_INLINE int
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
 * or_skip_varchar - skip varchar field (length + data) from or buffer
 *    return: NO_ERROR or error code.
 *    buf(in/out): or buffer
 *    align(in):
 *
 * Note:
 *   Reads the string header via or_get_string_header(), then
 *   advances past the data bytes (compressed_size if present,
 *   otherwise size) and any trailing padding via
 *   or_skip_varchar_remainder().
 */
STATIC_INLINE int
or_skip_varchar (OR_BUF * buf, int align)
{
  int size = 0, compressed_size = 0, rc = NO_ERROR;

  /* length unused for skip — only data byte count matters. */
  rc = or_get_string_header (buf, NULL, &size, &compressed_size);
  if (rc != NO_ERROR)
    {
      return rc;
    }

  return or_skip_varchar_remainder (buf, (compressed_size > 0) ? compressed_size : size, align);
}

/*
 * or_skip_varbit_remainder - skip varbit field of given length in or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    bitlen(in): bitlen to skip
 *    align(in):
 */
STATIC_INLINE int
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
 * or_skip_varchar_remainder - skip varchar field of given length
 *    return: NO_ERROR if successful, error code otherwise
 *    buf(in/out): or buffer
 *    charlen(in): length of varchar field to skip
 *    align(in):
 */
STATIC_INLINE int
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

/*
 * DISK IDENTIFIER TRANSLATORS
 *    Translators for the disk identifiers OID, HFID, BTID, EHID.
 */

/*
 * or_put_oid - write content of an OID structure from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    oid(in): pointer to OID
 */
STATIC_INLINE int
or_put_oid (OR_BUF * buf, const OID * oid)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_OID_SIZE <= buf->endptr);
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
	  assert (false);
	}
      OR_PUT_OID (buf->ptr, oid);
    }
  buf->ptr += OR_OID_SIZE;
  return NO_ERROR;
}

/*
 * or_get_oid - read content of an OID structure from or buffer
 *    return: NO_ERROR or error code
 *    buf(in/out): or buffer
 *    oid(out): pointer to OID
 */
STATIC_INLINE int
or_get_oid (OR_BUF * buf, OID * oid)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);
  assert (buf->ptr + OR_OID_SIZE <= buf->endptr);
  OR_GET_OID (buf->ptr, oid);
  buf->ptr += OR_OID_SIZE;
  return NO_ERROR;
}

/*
 * or_put_mvccid () - Put an MVCCID to OR Buffer.
 *
 * return      : Error code.
 * buf (in)    : OR Buffer
 * mvccid (in) : MVCCID
 */
STATIC_INLINE int
or_put_mvccid (OR_BUF * buf, MVCCID mvccid)
{
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (buf->ptr + OR_MVCCID_SIZE <= buf->endptr);
  OR_PUT_MVCCID (buf->ptr, &mvccid);
  buf->ptr += OR_MVCCID_SIZE;
  return NO_ERROR;
}

/*
 * or_get_mvccid () - Get an MVCCID from OR Buffer.
 *
 * return	: MVCCID
 * buf (in/out) : OR Buffer.
 * error (out)  : Error code.
 */
STATIC_INLINE int
or_get_mvccid (OR_BUF * buf, MVCCID * mvccid)
{
  assert (mvccid != NULL);
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  *mvccid = MVCCID_NULL;
  assert (buf->ptr + OR_MVCCID_SIZE <= buf->endptr);
  OR_GET_MVCCID (buf->ptr, mvccid);
  buf->ptr += OR_MVCCID_SIZE;
  return NO_ERROR;
}

/* VARIABLE OFFSET TABLE ACCESSORS */

STATIC_INLINE int
or_put_big_var_offset (OR_BUF * buf, int num)
{
  return or_put_int (buf, num);
}

STATIC_INLINE int
or_get_big_var_offset (OR_BUF * buf, int *error)
{
  return or_get_int (buf, error);
}

STATIC_INLINE int
or_put_offset (OR_BUF * buf, int num)
{
  return or_put_big_var_offset (buf, num);
}

STATIC_INLINE int
or_get_offset (OR_BUF * buf, int *error)
{
  return or_get_big_var_offset (buf, error);
}

STATIC_INLINE int
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
      assert (offset_size == OR_INT_SIZE);
      return or_put_int (buf, num);
    }
}

STATIC_INLINE int
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
      assert (offset_size == OR_INT_SIZE);
      return or_get_int (buf, error);
    }
}

/*
 * MIDXKEY HEADER ACCESSORS
 */

STATIC_INLINE int
or_multi_nullmap_size (const int n_elements)
{
  int nullmap_size;

  assert (n_elements > 0);

  nullmap_size = ((n_elements + 7) >> 3);
  assert (nullmap_size > 0);

  return nullmap_size;
}

STATIC_INLINE int
or_multi_offset_table_size (const int n_elements)
{
  assert (n_elements > 0);

  return n_elements + 1;
}

STATIC_INLINE int
or_multi_header_size (const int n_elements)
{
  return or_multi_nullmap_size (n_elements) + or_multi_offset_table_size (n_elements);
}

STATIC_INLINE void
or_multi_clear_header (char *nullmap_ptr, const int n_elements)
{
  assert (nullmap_ptr != NULL);

  memset (nullmap_ptr, 0x00, or_multi_header_size (n_elements));
}

STATIC_INLINE void
or_multi_set_not_null (char *nullmap_ptr, const int index)
{
  assert (nullmap_ptr != NULL);
  assert (index >= 0);

  (*(nullmap_ptr + (index >> 3))) |= (1 << (index & 7));
}


STATIC_INLINE void
or_multi_set_null (char *nullmap_ptr, const int index)
{
  assert (nullmap_ptr != NULL);
  assert (index >= 0);

  (*(nullmap_ptr + (index >> 3))) &= (~(1 << (index & 7)));
}

STATIC_INLINE bool
or_multi_is_not_null (char *nullmap_ptr, const int index)
{
  assert (nullmap_ptr != NULL);
  assert (index >= 0);

  if ((*(nullmap_ptr + (index >> 3))) & (1 << (index & 7)))
    {
      return true;
    }

  return false;
}

STATIC_INLINE bool
or_multi_is_null (char *nullmap_ptr, const int index)
{
  return !or_multi_is_not_null (nullmap_ptr, index);
}

STATIC_INLINE char *
or_multi_get_offset_table (char *nullmap_ptr, const int n_elements)
{
  assert (nullmap_ptr != NULL);

  return nullmap_ptr + or_multi_nullmap_size (n_elements);
}

STATIC_INLINE char *
or_multi_get_element_offset_ptr (char *nullmap_ptr, const int n_elements, const int index)
{
  return or_multi_get_offset_table (nullmap_ptr, n_elements) + index;
}

STATIC_INLINE int
or_multi_get_element_offset_internal (char *nullmap_ptr, const int n_elements, const int index)
{
  int offset;

  offset = OR_GET_BYTE (or_multi_get_element_offset_ptr (nullmap_ptr, n_elements, index));
  assert (offset >= or_multi_header_size (n_elements));
  assert (offset <= OR_MULTI_MAX_OFFSET);

  return offset;
}

STATIC_INLINE int
or_multi_get_element_offset (char *nullmap_ptr, const int n_elements, const int index)
{
  int offset;

  offset = or_multi_get_element_offset_internal (nullmap_ptr, n_elements, index);
  if (offset < OR_MULTI_MAX_OFFSET)
    {
      return offset;
    }

  return -1;
}

STATIC_INLINE int
or_multi_get_next_element_offset (char *nullmap_ptr, const int n_elements, const int index)
{
  return or_multi_get_element_offset (nullmap_ptr, n_elements, index + 1);
}

STATIC_INLINE int
or_multi_get_size_offset (char *nullmap_ptr, const int n_elements)
{
  int offset;

  offset = or_multi_get_element_offset_internal (nullmap_ptr, n_elements, n_elements);
  if (offset < OR_MULTI_MAX_OFFSET)
    {
      return offset;
    }

  return -1;
}

STATIC_INLINE void
or_multi_put_element_offset_internal (char *nullmap_ptr, const int n_elements, const int offset, const int index)
{
  assert (nullmap_ptr != NULL);
  assert (n_elements > 0);
  assert (offset >= or_multi_header_size (n_elements));
  assert (offset <= OR_MULTI_MAX_OFFSET);
  assert (index >= 0);

  OR_PUT_BYTE (or_multi_get_element_offset_ptr (nullmap_ptr, n_elements, index), offset);
}

STATIC_INLINE void
or_multi_put_element_offset (char *nullmap_ptr, const int n_elements, const int offset, const int index)
{
  if (offset < OR_MULTI_MAX_OFFSET)
    {
      or_multi_put_element_offset_internal (nullmap_ptr, n_elements, offset, index);
    }
  else
    {
      or_multi_put_element_offset_internal (nullmap_ptr, n_elements, OR_MULTI_MAX_OFFSET, index);
    }
}

STATIC_INLINE void
or_multi_put_next_element_offset (char *nullmap_ptr, const int n_elements, const int offset, const int index)
{
  or_multi_put_element_offset (nullmap_ptr, n_elements, offset, index + 1);
}

STATIC_INLINE void
or_multi_put_size_offset (char *nullmap_ptr, const int n_elements, const int offset)
{
  if (offset < OR_MULTI_MAX_OFFSET)
    {
      or_multi_put_element_offset_internal (nullmap_ptr, n_elements, offset, n_elements);
    }
  else
    {
      or_multi_put_element_offset_internal (nullmap_ptr, n_elements, OR_MULTI_MAX_OFFSET, n_elements);
    }
}

#endif /* _OBJECT_REPRESENTATION_H_ */
