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
 * object_representation_typesize.h - Definitions related to the representation of
 *        objects on disk and in memory.
 *        his file is shared by both the client and server.
 */

#ifndef _OBJECT_REPRESENTATION_CONSTANTS_H_
#define _OBJECT_REPRESENTATION_CONSTANTS_H_

#include "dbtype_def.h"
#include "memory_alloc.h"

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
#define OR_INT64_SIZE           8
#define OR_BIGINT_SIZE          8
#define OR_FLOAT_SIZE           4
#define OR_DOUBLE_SIZE          8

#if __WORDSIZE == 32
#define OR_PTR_SIZE             4
#else /* __WORDSIZE == 32 */
#define OR_PTR_SIZE             8
#endif /* __WORDSIZE == 64 */

#define OR_BIGINT_ALIGNED_SIZE  (OR_BIGINT_SIZE + MAX_ALIGNMENT)
#define OR_DOUBLE_ALIGNED_SIZE  (OR_DOUBLE_SIZE + MAX_ALIGNMENT)
#define OR_PTR_ALIGNED_SIZE     (OR_PTR_SIZE + MAX_ALIGNMENT)

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

/* OBJECT HEADER LAYOUT */
/* representation id, CHN, MVCC insert id, MVCC delete id, prev_version_lsa = 32 */
#define OR_MVCC_MAX_HEADER_SIZE  32

/* representation id and CHN */
#define OR_MVCC_MIN_HEADER_SIZE  8

/* representation id, MVCC insert id and CHN */
#define OR_MVCC_INSERT_HEADER_SIZE  16

#define OR_NON_MVCC_HEADER_SIZE	      (8)	/* two integers */

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

#endif /* !_OBJECT_REPRESENTATION_CONSTANTS_H_ */
