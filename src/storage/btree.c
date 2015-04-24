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
 * btree.c - B+-Tree mananger
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "btree_load.h"
#include "storage_common.h"
#include "error_manager.h"
#include "page_buffer.h"
#include "file_io.h"
#include "file_manager.h"
#include "slotted_page.h"
#include "oid.h"
#include "log_manager.h"
#include "memory_alloc.h"
#include "overflow_file.h"
#include "xserver_interface.h"
#include "btree.h"
#include "scan_manager.h"
#if defined(SERVER_MODE)
#include "thread.h"
#endif /* SERVER_MODE */
#include "heap_file.h"
#include "object_primitive.h"
#include "list_file.h"
#include "fetch.h"
#include "connection_defs.h"
#include "locator_sr.h"
#include "network_interface_sr.h"
#include "utility.h"
#include "mvcc.h"
#include "transform.h"

#include "fault_injection.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define BTREE_HEALTH_CHECK

#define BTREE_DEBUG_DUMP_SIMPLE		0x0001	/* simple message in SMO */
#define BTREE_DEBUG_DUMP_FULL		0x0002	/* full dump in insert or delete */

#define BTREE_DEBUG_HEALTH_SIMPLE	0x0010	/* simple health check in SMO */
#define BTREE_DEBUG_HEALTH_FULL		0x0020	/* full health check (traverse all slot in page) */

#define BTREE_DEBUG_TEST_SPLIT		0x0100	/* full split test */

#define BTREE_SPLIT_LOWER_BOUND 0.20f
#define BTREE_SPLIT_UPPER_BOUND (1.0f - BTREE_SPLIT_LOWER_BOUND)

#define BTREE_SPLIT_MIN_PIVOT 0.05f
#define BTREE_SPLIT_MAX_PIVOT (1.0f - BTREE_SPLIT_MIN_PIVOT)

#define BTREE_SPLIT_DEFAULT_PIVOT 0.5f
#define DISK_PAGE_BITS  (DB_PAGESIZE * CHAR_BIT)	/* Num of bits per page   */
#define RESERVED_SIZE_IN_PAGE   sizeof(FILEIO_PAGE_RESERVED)

#define BTREE_NODE_MAX_SPLIT_SIZE(page_ptr) \
  (db_page_size() - spage_header_size() - spage_get_space_for_record((page_ptr), HEADER))

#define OID_MSG_BUF_SIZE 64

#define MIN_KEY_SIZE DB_ALIGN (1, BTREE_MAX_ALIGN)
#define MIN_LEAF_REC_SIZE (OR_OID_SIZE + MIN_KEY_SIZE)
#define MAX_LEAF_REC_NUM (IO_MAX_PAGE_SIZE / MIN_LEAF_REC_SIZE)

#define MAX_MERGE_ALIGN_WASTE \
  ((DB_PAGESIZE/MIN_LEAF_REC_SIZE) * (BTREE_MAX_ALIGN - 1))

/* Merge two nodes when a page containing both is this empty. */
#define CAN_MERGE_WHEN_EMPTY \
  (MAX (DB_PAGESIZE * 0.33, MAX_MERGE_ALIGN_WASTE * 1.3))
/* Force merging two node when a page containing bot is this empty. */
#define FORCE_MERGE_WHEN_EMPTY \
  (MAX (DB_PAGESIZE * 0.66, MAX_MERGE_ALIGN_WASTE * 1.3))

#define BTREE_IS_SAME_DB_TYPE(type1, type2)                     \
  (((type1) == (type2))                                         \
    || (((type1) == DB_TYPE_OBJECT || (type1) == DB_TYPE_OID)   \
	  && ((type2) == DB_TYPE_OBJECT || (type2) == DB_TYPE_OID)))

/*
 * Page header information related defines
 */
#define NOT_FOUND -1

/* B'0001 0000 0000 0000' */
#define BTREE_LEAF_RECORD_FENCE 0x1000
/* B'0010 0000 0000 0000' */
#define BTREE_LEAF_RECORD_OVERFLOW_OIDS 0x2000
/* B'0100 0000 0000 0000' */
#define BTREE_LEAF_RECORD_OVERFLOW_KEY 0x4000
/* B'1000 0000 0000 0000' */
#define BTREE_LEAF_RECORD_CLASS_OID 0x8000
/* B'1111 0000 0000 0000' */
#define BTREE_LEAF_RECORD_MASK 0xF000

/* B'0100 0000 0000 0000' */
#define BTREE_OID_HAS_MVCC_INSID 0x4000
/* B'1000 0000 0000 0000' */
#define BTREE_OID_HAS_MVCC_DELID 0x8000
/* B'1100 0000 0000 0000' */
#define BTREE_OID_MVCC_FLAGS_MASK 0xC000

#define BTREE_OID_HAS_MVCC_INSID_AND_DELID \
  (BTREE_OID_HAS_MVCC_INSID | BTREE_OID_HAS_MVCC_DELID)

/* The maximum number of OID's in a page */
#define BTREE_MAX_OID_COUNT IO_MAX_PAGE_SIZE / OR_OID_SIZE

/* Clear MVCC flags from object OID */
#define BTREE_OID_CLEAR_MVCC_FLAGS(oid_ptr) \
  ((oid_ptr)->volid &= ~BTREE_OID_MVCC_FLAGS_MASK)
/* Clear record flags from object OID */
#define BTREE_OID_CLEAR_RECORD_FLAGS(oid_ptr) \
  ((oid_ptr)->slotid &= ~BTREE_LEAF_RECORD_MASK)
/* Clear all flags (mvcc & record) from object OID. */
#define BTREE_OID_CLEAR_ALL_FLAGS(oid_ptr) \
  do \
    { \
      BTREE_OID_CLEAR_MVCC_FLAGS (oid_ptr); \
      BTREE_OID_CLEAR_RECORD_FLAGS (oid_ptr); \
    } \
  while (0)
/* Check if MVCC flags are set into b-tree object OID. */
#define BTREE_OID_IS_MVCC_FLAG_SET(oid_ptr, mvcc_flag) \
  (((oid_ptr)->volid & (mvcc_flag)) == (mvcc_flag))
/* Check if record flags are set into b-tree object OID. */
#define BTREE_OID_IS_RECORD_FLAG_SET(oid_ptr, mvcc_flag) \
  (((oid_ptr)->slotid & (mvcc_flag)) == (mvcc_flag))

/* Set b-tree flags into an OID. */
#define BTREE_OID_SET_MVCC_FLAG(oid_ptr, mvcc_flag) \
  ((oid_ptr)->volid |= (mvcc_flag))
#define BTREE_OID_SET_RECORD_FLAG(oid_ptr, mvcc_flag) \
  ((oid_ptr)->slotid |= (mvcc_flag))

/* Get b-tree flags from an OID. */
#define BTREE_OID_GET_MVCC_FLAGS(oid_ptr) \
  ((oid_ptr)->volid & BTREE_OID_MVCC_FLAGS_MASK)
#define BTREE_OID_GET_RECORD_FLAGS(oid_ptr) \
  ((oid_ptr)->slotid & BTREE_LEAF_RECORD_MASK)

/* Check MVCC flags in an b-tree MVCC info. */
#define BTREE_MVCC_INFO_HAS_INSID(mvcc_info) \
  (((mvcc_info)->flags & BTREE_OID_HAS_MVCC_INSID) != 0)
#define BTREE_MVCC_INFO_HAS_DELID(mvcc_info) \
  (((mvcc_info)->flags & BTREE_OID_HAS_MVCC_DELID) != 0)

/* CLear MVCC flags in a b-tree MVCC info. */
#define BTREE_MVCC_INFO_CLEAR_INSID(mvcc_info) \
  ((mvcc_info)->flags &= ~BTREE_OID_HAS_MVCC_INSID)
#define BTREE_MVCC_INFO_CLEAR_DELID(mvcc_info) \
  ((mvcc_info)->flags &= ~BTREE_OID_HAS_MVCC_DELID)

/* Check if insert MVCCID is valid but not visible to everyone. */
#define BTREE_MVCC_INFO_IS_INSID_NOT_ALL_VISIBLE(mvcc_info) \
  (BTREE_MVCC_INFO_HAS_INSID (mvcc_info) \
   && MVCCID_IS_NOT_ALL_VISIBLE ((mvcc_info)->insert_mvccid))
/* Check if delete MVCCID is valid. */
#define BTREE_MVCC_INFO_IS_DELID_VALID(mvcc_info) \
  (BTREE_MVCC_INFO_HAS_DELID (mvcc_info) \
   && (mvcc_info)->delete_mvccid != MVCCID_NULL)

/* Insert MVCCID based on b-tree mvcc info. */
#define BTREE_MVCC_INFO_INSID(mvcc_info) \
  (BTREE_MVCC_INFO_HAS_INSID (mvcc_info) ? \
   (mvcc_info)->insert_mvccid : MVCCID_ALL_VISIBLE)
/* Delete MVCC based on b-tree mvcc info. */
#define BTREE_MVCC_INFO_DELID(mvcc_info) \
  (BTREE_MVCC_INFO_HAS_DELID (mvcc_info) ? \
   (mvcc_info)->delete_mvccid : MVCCID_NULL)

/* Set b-tree MVCC info as if it has fixed size (it includes both insert and
 * delete MVCCID.
 */
#define BTREE_MVCC_INFO_SET_FIXED_SIZE(mvcc_info) \
  do \
    { \
      if (!BTREE_MVCC_INFO_HAS_INSID (mvcc_info)) \
	{ \
	  (mvcc_info)->insert_mvccid = MVCCID_ALL_VISIBLE; \
	} \
      if (!BTREE_MVCC_INFO_HAS_DELID (mvcc_info)) \
	{ \
	  (mvcc_info)->delete_mvccid = MVCCID_NULL; \
	} \
      (mvcc_info)->flags = BTREE_OID_HAS_MVCC_INSID_AND_DELID; \
    } while (false)

/* Clear unnecessary flags from b-tree MVCC info. */
#define BTREE_MVCC_INFO_CLEAR_FIXED_SIZE(mvcc_info) \
  do \
    { \
      if (!BTREE_MVCC_INFO_IS_INSID_NOT_ALL_VISIBLE (mvcc_info)) \
	{ \
	  BTREE_MVCC_INFO_CLEAR_INSID(mvcc_info); \
	} \
      if (!BTREE_MVCC_INFO_IS_DELID_VALID (mvcc_info)) \
	{ \
	  BTREE_MVCC_INFO_CLEAR_DELID(mvcc_info); \
	} \
    } while (false)

/* Set insert MVCCID into b-tree mvcc info. */
#define BTREE_MVCC_INFO_SET_INSID(mvcc_info, insid) \
  do \
    { \
      (mvcc_info)->flags |= BTREE_OID_HAS_MVCC_INSID; \
      (mvcc_info)->insert_mvccid = insid; \
    } while (false)
/* Set delete MVCCID into b-tree mvcc info. */
#define BTREE_MVCC_INFO_SET_DELID(mvcc_info, delid) \
  do \
    { \
      (mvcc_info)->flags |= BTREE_OID_HAS_MVCC_DELID; \
      (mvcc_info)->delete_mvccid = delid; \
    } while (false)

/* Get an object OID from a b-tree record. If MVCC is enabled, mvcc flags are
 * cleared.
 */
#define BTREE_GET_OID(buf, oid_ptr) \
  do \
    { \
      OR_GET_OID (buf, oid_ptr); \
      BTREE_OID_CLEAR_MVCC_FLAGS (oid_ptr); \
    } while (0)
/* Get an object OID from b-tree record. MVCC flags are not removed */
#define BTREE_GET_OID_WITH_MVCC_FLAGS(buf, oid_ptr) \
  OR_GET_OID (buf, oid_ptr)

/* Get a class OID from b-tree record. */
#define BTREE_GET_CLASS_OID(buf, class_oid_ptr) \
  OR_GET_OID (buf, class_oid_ptr)

/* The size of an object in cases it has fixed size (includes all required
 * info).
 * In case of unique: OID, class OID, insert and delete MVCCID.
 * In case of non-unique: OID, insert and delete MVCCID.
 *
 * Fixed size is used when:
 * 1. object is saved in overflow page.
 * 2. object is non-first in leaf record.
 * 3. object is first in a leaf record that has overflow pages.
 */
#define BTREE_OBJECT_FIXED_SIZE(btree_info) \
  (BTREE_IS_UNIQUE ((btree_info)->unique_pk) ? \
   2 * OR_OID_SIZE + 2 * OR_MVCCID_SIZE : OR_OID_SIZE + 2 * OR_MVCCID_SIZE)
/* Maximum possible size for one b-tree object including all its information.
 */
#define BTREE_OBJECT_MAX_SIZE (2 * OR_OID_SIZE + 2 * OR_MVCCID_SIZE)

/* Initialize OR_BUF to process a b-tree record. */
#define BTREE_RECORD_OR_BUF_INIT(buf, btree_rec) \
  do \
    { \
      int size = (btree_rec)->length; \
      if (btree_leaf_is_flaged (btree_rec, BTREE_LEAF_RECORD_OVERFLOW_OIDS)) \
	{ \
	  size -= DB_ALIGN (DISK_VPID_SIZE, BTREE_MAX_ALIGN); \
	} \
      OR_BUF_INIT (buf, (btree_rec)->data, size); \
    } while (false)

/* Get MVCC size from leaf record flags */
/* Size is:
 * 2 * OR_MVCCID_SIZE if both MVCC flags are set
 * 0 if no MVCC flags are set
 * OR_MVCCID_SIZE otherwise (one flag is set).
 */
#define BTREE_GET_MVCC_INFO_SIZE_FROM_FLAGS(mvcc_flags) \
  (((mvcc_flags) & BTREE_OID_HAS_MVCC_INSID_AND_DELID) \
   == BTREE_OID_HAS_MVCC_INSID_AND_DELID ? \
   2 * OR_MVCCID_SIZE : \
   ((mvcc_flags) == 0 ? 0 : OR_MVCCID_SIZE))

/* Check if page is a valid b-tree leaf node. Usually called after unfix and
 * re-fix without validation.
 */
/* TODO: Is this expensive? Can we find a faster way to check it?
 *	 (e.g. save last deallocation page LSA - it will be then enough to
 *	 compare with an LSA saved before unfixing).
 */
#define BTREE_IS_PAGE_VALID_LEAF(thread_p, page) \
  ((page) != NULL \
   && pgbuf_is_valid_page (thread_p, pgbuf_get_vpid_ptr (page), true, NULL, \
			   NULL) \
   && pgbuf_get_page_ptype (thread_p, page) == PAGE_BTREE \
   && (btree_get_node_header (page))->node_level == 1)

/*
 * Recovery structures
 */
typedef struct pageid_struct PAGEID_STRUCT;
struct pageid_struct
{				/* Recovery pageid structure */
  VFID vfid;			/* Volume id in which page resides */
  VPID vpid;			/* Virtual page identifier */
};

typedef struct recset_header RECSET_HEADER;
struct recset_header
{				/* Recovery set of recdes structure */
  INT16 rec_cnt;		/* number of RECDESs stored */
  INT16 first_slotid;		/* first slot id */
};

typedef enum
{
  LEAF_RECORD_REGULAR = 1,
  LEAF_RECORD_OVERFLOW
} LEAF_RECORD_TYPE;

typedef enum
{
  BTREE_BOUNDARY_FIRST = 1,
  BTREE_BOUNDARY_LAST,
} BTREE_BOUNDARY;

typedef enum
{
  BTREE_MERGE_NO = 0,
  BTREE_MERGE_L_EMPTY,
  BTREE_MERGE_R_EMPTY,
  BTREE_MERGE_SIZE,
  BTREE_MERGE_SIZE_FORCE,
} BTREE_MERGE_STATUS;

/* Redo recovery of insert structure and flags */
#define BTREE_INSERT_RCV_FLAG_OID_INSERTED	0x8000
#define BTREE_INSERT_RCV_FLAG_OVFL_CHANGED	0x4000
#define BTREE_INSERT_RCV_FLAG_NEW_OVFLPG	0x2000
#define BTREE_INSERT_RCV_FLAG_REC_TYPE		0x1000
#define BTREE_INSERT_RCV_FLAG_INSOID_MODE	0x0C00
#define BTREE_INSERT_RCV_FLAG_UNIQUE		0x0200
#define BTREE_INSERT_RCV_FLAG_KEY_DOMAIN	0x0100
#define BTREE_INSERT_RCV_FLAG_HAS_INSID		0x0080
#define BTREE_INSERT_RCV_FLAG_HAS_DELID		0x0040

/* Check flag bits are set */
#define BTREE_INSERT_RCV_IS_OID_INSERTED(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_OID_INSERTED) != 0)
#define BTREE_INSERT_RCV_IS_OVFL_CHANGED(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_OVFL_CHANGED) != 0)
#define BTREE_INSERT_RCV_IS_NEW_OVFLPG(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_NEW_OVFLPG) != 0)
#define BTREE_INSERT_RCV_IS_UNIQUE(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_UNIQUE) != 0)
#define BTREE_INSERT_RCV_HAS_KEY_DOMAIN(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_KEY_DOMAIN) != 0)
#define BTREE_INSERT_RCV_HAS_INSID(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_HAS_INSID) != 0)
#define BTREE_INSERT_RCV_HAS_DELID(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_HAS_DELID) != 0)

/* Set redo insert recovery flags. */
#define BTREE_INSERT_RCV_SET_FLAGS(recins, flags_) \
  ((recins)->flags |= (flags_))

/* Check record type */
#define BTREE_INSERT_RCV_IS_RECORD_REGULAR(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_REC_TYPE) != 0)
#define BTREE_INSERT_RCV_IS_RECORD_OVERFLOW(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_REC_TYPE) == 0)
/* Set record type */
#define BTREE_INSERT_RCV_SET_RECORD_REGULAR(recins) \
  ((recins)->flags |= BTREE_INSERT_RCV_FLAG_REC_TYPE)
#define BTREE_INSERT_RCV_SET_RECORD_OVERFLOW(recins) \
  ((recins)->flags &= ~BTREE_INSERT_RCV_FLAG_REC_TYPE)

/* Check insert OID mode */
#define BTREE_INSERT_OID_MODE_UNIQUE_MOVE_TO_END	0x0800
#define BTREE_INSERT_OID_MODE_UNIQUE_REPLACE_FIRST	0x0400
#define BTREE_INSERT_OID_MODE_DEFAULT			0x0C00

/* Insert mode: insert new object as first in record and move current first
 * to the end of record.
 */
#define BTREE_INSERT_RCV_IS_INSMODE_UNIQUE_MOVE_TO_END(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_INSOID_MODE) \
   == BTREE_INSERT_OID_MODE_UNIQUE_MOVE_TO_END)
/* Insert mode: replace current first object with new object. */
#define BTREE_INSERT_RCV_IS_INSMODE_UNIQUE_REPLACE_FIRST(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_INSOID_MODE) \
   == BTREE_INSERT_OID_MODE_UNIQUE_REPLACE_FIRST)
/* Insert mode default: at the end of record for leaf records,
 * ordered for overflow records.
 */
#define BTREE_INSERT_RCV_IS_INSMODE_DEFAULT(recins) \
  (((recins)->flags & BTREE_INSERT_RCV_FLAG_INSOID_MODE) \
   == BTREE_INSERT_OID_MODE_DEFAULT)

/* Set insert OID mode */
#define BTREE_INSERT_RCV_SET_INSMODE_UNIQUE_MOVE_TO_END(recins) \
  ((recins)->flags = \
   (((recins)->flags & (~BTREE_INSERT_RCV_FLAG_INSOID_MODE)) \
    | BTREE_INSERT_OID_MODE_UNIQUE_MOVE_TO_END))

#define BTREE_INSERT_RCV_SET_INSMODE_UNIQUE_REPLACE_FIRST(recins) \
  ((recins)->flags = \
   (((recins)->flags & (~BTREE_INSERT_RCV_FLAG_INSOID_MODE)) \
    | BTREE_INSERT_OID_MODE_UNIQUE_REPLACE_FIRST))

#define BTREE_INSERT_RCV_SET_INSMODE_DEFAULT(recins) \
  ((recins)->flags = \
   (((recins)->flags & (~BTREE_INSERT_RCV_FLAG_INSOID_MODE)) \
    | BTREE_INSERT_OID_MODE_DEFAULT))

/* Get insert OID mode. */
#define BTREE_INSERT_RCV_GET_INSMODE(recins) \
  ((recins)->flags & BTREE_INSERT_RCV_FLAG_INSOID_MODE)

/* RECINS_STRUCT - redo b-tree insert recovery structure.
 */
typedef struct recins_struct RECINS_STRUCT;
struct recins_struct
{				/* Recovery leaf record oid insertion structure */
  OID class_oid;		/* class oid only in case of unique index */
  OID oid;			/* oid to be inserted to the record    */
  VPID ovfl_vpid;		/* Next Overflow pageid  */
  INT16 flags;			/* Flags to describe different context of
				 * recovered insert object:
				 * - oid inserted
				 * - is overflow changed
				 * - is new overflow
				 * - record type (regular or overflow)
				 * - insert OID mode
				 */
};
#define RECINS_STRUCT_INITIALIZER \
  { OID_INITIALIZER, OID_INITIALIZER, VPID_INITIALIZER, 0 }

/* Redo recovery of insert delete MVCCID */
#define BTREE_INSERT_DELID_RCV_FLAG_UNIQUE	0x80000000
#define BTREE_INSERT_DELID_RCV_FLAG_OVERFLOW	0x40000000
#define BTREE_INSERT_DELID_RCV_FLAG_KEY_DOMAIN	0x20000000

#define BTREE_INSERT_DELID_RCV_FLAG_MASK	0xE0000000

#define BTREE_INSERT_DELID_RCV_IS_UNIQUE(offset) \
  (((offset) & BTREE_INSERT_DELID_RCV_FLAG_UNIQUE) != 0)
#define BTREE_INSERT_DELID_RCV_IS_OVERFLOW(offset) \
  (((offset) & BTREE_INSERT_DELID_RCV_FLAG_OVERFLOW) != 0)
#define BTREE_INSERT_DELID_RCV_HAS_KEY_DOMAIN(offset) \
  (((offset) & BTREE_INSERT_DELID_RCV_FLAG_KEY_DOMAIN) != 0)

#define BTREE_INSERT_DELID_RCV_CLEAR_FLAGS(offset) \
  ((offset) = (offset) & (~BTREE_INSERT_DELID_RCV_FLAG_MASK))

#define BTID_DOMAIN_BUFFER_SIZE 64
#define BTID_DOMAIN_CHECK_MAX_SIZE 1024

/* Offset of the fields in the Leaf/NonLeaf Record Recovery Log Data */
#define OFFS1  0		/* Node Type Offset: Leaf/NonLeaf Information */
#define OFFS2  2		/* RECDES Type Offset */
#define OFFS3  4		/* RECDES Data Offset */

/* for Leaf Page Key Insertions */
#define LOFFS1  0		/* Key Len Offset */
#define LOFFS2  2		/* Node Type Offset: Leaf/NonLeaf Information */
#define LOFFS3  4		/* RECDES Type Offset */
#define LOFFS4  6		/* RECDES Data Offset */

/* B+tree statistical information environment */
typedef struct btree_stats_env BTREE_STATS_ENV;
struct btree_stats_env
{
  BTREE_SCAN btree_scan;	/* BTS */
  BTREE_STATS *stat_info;
  int pkeys_val_num;
  DB_VALUE pkeys_val[BTREE_STATS_PKEYS_NUM];	/* partial key-value */
};

/* Structure used by btree_range_search to initialize and handle variables
 * needed throughout the process.
 */
typedef struct btree_range_search_helper BTREE_RANGE_SEARCH_HELPER;
struct btree_range_search_helper
{
  OID *mem_oid_ptr;		/* Pointer to OID memory storage */
  int pg_oid_cnt;		/* The capacity of OID memory storage */
  int oids_cnt;			/* Current count of stored OID's */
  int oid_size;			/* Size of one OID */
  int cp_oid_cnt;		/* The OID count that can be stored in
				 * the current step
				 */
  int rec_oid_cnt;		/* The OID count in current record */
  char *rec_oid_ptr;		/* Pointer in record to current OID */
  bool swap_key_range;		/* Swaps key range if true */
  bool is_key_range_satisfied;	/* Does current key satisfy range */
  bool is_key_filter_satisfied;	/* Does current key satisfy filter */
  bool is_condition_satisfied;	/* Does current key satisfy range and
				 * filter
				 */
  RECDES rec;			/* Current record */
  LEAF_REC leaf_pnt;		/* Leaf record pointer OID overflows */
  int offset;			/* Offset in record to the first OID */
  OID class_oid;		/* Class identifier for current object */
  OID inst_oid;			/* Current object identifier */
  BTREE_NODE_TYPE node_type;	/* Current node type: leaf or overflow */
  bool iss_get_first_result_only;	/* Index skip scan special case */
  bool restart_on_first;	/* restart after first OID */
  int CLS_satisfied;		/* All conditions are satisfied */
  OID saved_class_oid;		/* Saved class identifier */
  OID saved_inst_oid;		/* Saved object identifier */
  char oid_space[2 * OR_OID_SIZE];	/* OID buffer to store "last" index key */
  DB_VALUE prev_key;		/* Previous key */
  bool clear_prev_key;		/* Previous key needs clear if true */
  LOG_LSA prev_leaf_lsa;	/* LSA of previous page */
  LOG_LSA ovfl_page_lsa;	/* LSA of overflow page */
  bool keep_on_copying;		/* True when OID storage exceeds it's
				 * default maximum size and need to
				 * stop current iteration of range
				 * search after this key
				 */
  OID ck_pseudo_oid;		/* Current key pseudo identifier */
  OID saved_ck_pseudo_oid;	/* Saved current key pseudo identifier */
  OID nk_pseudo_oid;		/* Next key pseudo identifier */
  OID saved_nk_pseudo_oid;	/* Saved next key pseudo identifier */
  OID saved_nk_class_oid;	/* Saved class oid for next key */

  bool end_of_leaf_level;	/* True if end of leaf level was reached */
  bool curr_key_locked;		/* Is current key locked */
  bool next_key_locked;		/* Is next key locked */
  bool current_lock_request;	/* Current key needs locking */
  bool read_prev_key;		/* Previous key is read */
};

typedef struct show_index_scan_ctx SHOW_INDEX_SCAN_CTX;
struct show_index_scan_ctx
{
  int indexes_count;		/* The total of indexes */
  bool is_all;			/* Get all indexes or get a specified index */
  char *index_name;		/* Index name which user specified  */
  OID *class_oids;		/* Class oids array */
  int class_oid_count;		/* The count of above oids array */
  int show_type;		/* Show type */
};

/* BTREE_SEARCH_KEY_HELPER -
 * Structure usually used to return the result of search key functions.
 */
typedef struct btree_search_key_helper BTREE_SEARCH_KEY_HELPER;
struct btree_search_key_helper
{
  BTREE_SEARCH result;		/* Result of key search. */
  PGSLOTID slotid;		/* Slot ID of found key or slot ID of the
				 * biggest key smaller then key (if not
				 * found).
				 */
};
/* BTREE_SEARCH_KEY_HELPER static initializer. */
#define BTREE_SEARCH_KEY_HELPER_INITIALIZER \
  { BTREE_KEY_NOTFOUND, NULL_SLOTID }

/* BTREE_FIND_UNIQUE_HELPER -
 * Structure used by find unique functions.
 *
 * Functions:
 * btree_key_find_unique_version_oid.
 * btree_key_find_and_lock_unique.
 * btree_key_find_and_lock_unique_of_unique.
 * btree_key_find_and_lock_unique_of_non_unique.
 */
typedef struct btree_find_unique_helper BTREE_FIND_UNIQUE_HELPER;
struct btree_find_unique_helper
{
  OID oid;			/* OID of found object (if found). */
  OID match_class_oid;		/* Object is only considered if its class
				 * OID matches this class OID.
				 */
  LOCK lock_mode;		/* Lock mode for found unique object. */
  MVCC_SNAPSHOT *snapshot;	/* Snapshot used to filter objects not
				 * visible. If NULL, objects are not filtered.
				 */
  bool found_object;		/* Set to true if object was found. */

#if defined (SERVER_MODE)
  OID locked_oid;		/* Locked object. */
  OID locked_class_oid;		/* Locked object class OID. */
#endif				/* SERVER_MODE */
};
/* BTREE_FIND_UNIQUE_HELPER static initializer. */
#if defined (SERVER_MODE)
#define BTREE_FIND_UNIQUE_HELPER_INITIALIZER \
  { OID_INITIALIZER, /* oid */ \
    OID_INITIALIZER, /* match_class_oid */ \
    NULL_LOCK, /* lock_mode */ \
    NULL, /* snapshot */ \
    false, /* found_object */ \
    OID_INITIALIZER, /* locked_oid */ \
    OID_INITIALIZER /* locked_class_oid */ \
  }
#else	/* !SERVER_MODE */		   /* SA_MODE */
#define BTREE_FIND_UNIQUE_HELPER_INITIALIZER \
  { OID_INITIALIZER, /* oid */ \
    OID_INITIALIZER, /* match_class_oid */ \
    NULL_LOCK, /* lock_mode */ \
    NULL, /* snapshot */ \
    false /* found_object */ \
  }
#endif /* !SA_MODE */

/* BTREE_REC_FIND_OBJ_HELPER -
 * Structure used to find an object in a b-tree record.
 * TODO: Use it?
 */
typedef struct btree_rec_find_obj_helper BTREE_REC_FIND_OBJ_HELPER;
struct btree_rec_find_obj_helper
{
  OID oid;			/* OID of searched object. */
  MVCCID check_insert;		/* Non-NULL MVCCID if insert MVCCID should also
				 * match.
				 */
  MVCCID check_delete;		/* Non-NULL MVCCID if delete MVCCID should also
				 * match.
				 */
  int found_offset;		/* Object's offset in record. */
};
/* BTREE_REC_FIND_OBJ_HELPER static initializer. */
#define BTREE_RECORD_FIND_OBJECT_ARGS_INITIALIZER \
  { OID_INITIALIZER, MVCCID_NULL, MVCCID_NULL, NOT_FOUND }

/* BTREE_REC_SATISFIES_SNAPSHOT_HELPER -
 * Structure used as helper for btree_record_satisfies_snapshot function.
 */
typedef struct btree_rec_satisfies_snapshot_helper
  BTREE_REC_SATISFIES_SNAPSHOT_HELPER;
struct btree_rec_satisfies_snapshot_helper
{
  MVCC_SNAPSHOT *snapshot;	/* Input: MVCC snapshot used to filter objects
				 * not visible for current transaction.
				 */
  OID match_class_oid;		/* Object class OID must match this class OID.
				 * Can be applied only to unique indexes.
				 */
  OID *oid_ptr;			/* OID buffer used to output OID's of visible
				 * objects.
				 */
  int oid_cnt;			/* Visible OID counter. */
};
/* BTREE_REC_SATISFIES_SNAPSHOT_HELPER static initializer. */
#define BTREE_REC_SATISFIES_SNAPSHOT_HELPER_INITIALIZER \
  { NULL /* snapshot */, OID_INITIALIZER /* match_class_oid */, \
    NULL /* oid_ptr */, 0 /* oid_cnt */}

/*
 * btree_search_key_and_apply_functions () argument functions.
 */

/* BTREE_ROOT_WITH_KEY_FUNCTION -
 * btree_search_key_and_apply_functions internal function called on root page,
 * before starting to advance on pages.
 *
 * Arguments:
 * thread_p (in)       : Thread entry.
 * btid (in)	       : B-tree identifier.
 * btid_int (out)      : Output b-tree info.
 * key (in)	       : Key value.
 * is_leaf (out)       : Output true if root is leaf node.
 * key_slotid (out)    : Output slotid of key if found, NULL_SLOTID otherwise.
 * stop (out)	       : Output true when advancing in b-tree should be
 *			 stopped.
 * restart (out)       : Output true when advancing in b-tree must be
 *			 restarted starting with root.
 * other_args (in/out) : Function specific arguments.
 *
 * List of functions:
 * btree_get_root_with_key.
 * btree_fix_root_for_insert.
 */
typedef int BTREE_ROOT_WITH_KEY_FUNCTION (THREAD_ENTRY * thread_p,
					  BTID * btid,
					  BTID_INT * btid_int,
					  DB_VALUE * key,
					  PAGE_PTR * root_page,
					  bool * is_leaf,
					  BTREE_SEARCH_KEY_HELPER *
					  search_key,
					  bool * stop,
					  bool * restart, void *other_args);

/* BTREE_ADVANCE_WITH_KEY_FUNCTION -
 * btree_search_key_and_apply_functions internal function called to advance
 * from root to leaf level of b-tree by following the key. The function can
 * modify the structure of b-tree, will advance one level at each call and
 * it will ultimately stop at the leaf page were the key belongs or would
 * belong if it existed.
 * 
 * Arguments:
 * thread_p (in)       : Thread entry.
 * btid_int (int)      : B-tree info.
 * key (in)	       : Key value.
 * is_leaf (out)       : Output true if root is leaf node.
 * key_slotid (out)    : Output slotid of key if found, NULL_SLOTID otherwise.
 * stop (out)	       : Output true when advancing in b-tree should be
 *			 stopped.
 * restart (out)       : Output true when advancing in b-tree must be
 *			 restarted starting with root.
 * other_args (in/out) : Function specific arguments.
 * 
 * List of functions:
 * btree_advance_and_find_key.
 * btree_split_node_and_advance.
 */
typedef int BTREE_ADVANCE_WITH_KEY_FUNCTION (THREAD_ENTRY * thread_p,
					     BTID_INT * btid_int,
					     DB_VALUE * key,
					     PAGE_PTR * crt_page,
					     PAGE_PTR * advance_to_page,
					     bool * is_leaf,
					     BTREE_SEARCH_KEY_HELPER *
					     search_key,
					     bool * stop,
					     bool * restart,
					     void *other_args);

/* BTREE_PROCESS_KEY_FUNCTION -
 * btree_search_key_and_apply_functions internal function called after
 * advancing in leaf page. It should process or manipulate data for the given
 * key.
 *
 * Arguments:
 * thread_p (in)   : Thread entry.
 * btid_int (int)  : B-tree info.
 * key (in)	   : Key value.
 * key_slotid (in) : Slot ID of key if it was found, NULL_SLOTID otherwise.
 * restart (out)   : Output true when advancing in b-tree must be restarted
 *		     starting with root.
 * args (in/out)   : Function specific arguments.
 *
 * Functions:
 * btree_key_find_unique_version_oid.
 * btree_key_find_and_lock_unique
 * btree_key_find_and_lock_unique_of_unique.
 * btree_key_find_and_lock_unique_of_non_unique
 * btree_key_insert_new_object.
 * btree_key_insert_delete_mvccid.
 */
typedef int BTREE_PROCESS_KEY_FUNCTION (THREAD_ENTRY * thread_p,
					BTID_INT * btid_int,
					DB_VALUE * key,
					PAGE_PTR * leaf_page,
					BTREE_SEARCH_KEY_HELPER * search_key,
					bool * restart, void *other_args);

/* BTREE_PROCESS_OBJECT_FUNCTION -
 * btree_record_process_objects internal function called for each object found
 * in b-tree leaf/overflow records. It should process or manipulate object
 * data in record.
 *
 * Functions:
 * btree_record_object_compare.
 * btree_record_satisfies_snapshot.
 * btree_select_visible_object_for_range_scan.
 * btree_fk_object_does_exist.
 */
typedef int BTREE_PROCESS_OBJECT_FUNCTION (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   RECDES * record, char *object_ptr,
					   OID * oid, OID * class_oid,
					   BTREE_MVCC_INFO * mvcc_info,
					   bool * stop, void *args);

/* Type of b-tree scans. */
/* Covering index. */
#define BTS_IS_INDEX_COVERED(bts) \
  ((bts) != NULL && (bts)->index_scan_idp != NULL \
   && SCAN_IS_INDEX_COVERED((bts)->index_scan_idp))
/* Multiple ranges optimization. */
#define BTS_IS_INDEX_MRO(bts) \
  ((bts) != NULL && (bts)->index_scan_idp != NULL \
   && SCAN_IS_INDEX_MRO((bts)->index_scan_idp))
/* Index skip scan. */
#define BTS_IS_INDEX_ISS(bts) \
  ((bts) != NULL && (bts)->index_scan_idp != NULL \
   && SCAN_IS_INDEX_ISS((bts)->index_scan_idp))
/* Index loose scan. */
#define BTS_IS_INDEX_ILS(bts) \
  ((bts) != NULL && (bts)->index_scan_idp != NULL \
   && SCAN_IS_INDEX_ILS((bts)->index_scan_idp))
#define BTS_NEED_COUNT_ONLY(bts) \
  ((bts) != NULL && (bts)->index_scan_idp != NULL \
   && (bts)->index_scan_idp->need_count_only)

/* Increment read OID counters for b-tree scan. */
#define BTS_INCREMENT_READ_OIDS(bts) \
  do \
    { \
      (bts)->n_oids_read++; \
      (bts)->n_oids_read_last_iteration++; \
    } while (false)

/* Soft capacity of OID buffer. It is used to stop one scan iteration as a
 * general rule. There is an exception when hard capacity is applied.
 */
#define BTS_IS_SOFT_CAPACITY_ENOUGH(bts, count) \
  ((count) \
   <= (BTS_IS_INDEX_COVERED (bts) ? \
       /* Covering index: use max tuples as soft limit. */ \
       (bts)->index_scan_idp->indx_cov.max_tuples \
       /* Normal scan: use max_oid_cnt as soft limit. */ \
       : (bts)->index_scan_idp->oid_list->max_oid_cnt))
/* Hard capacity is the maximum number that can fit the OID buffer. It is
 * used when the number of objects in a single key does not fit the soft
 * capacity.
 * This key objects will be selected from leaf and overflows as long as they
 * fit the hard capacity. This provides enough space for at least one leaf
 * and one overflow page or for two full overflow pages.
 * There is no limit as hard capacity for covering index. Since it uses a
 * list file, its capacity is considered infinite.
 */
#define BTS_IS_HARD_CAPACITY_ENOUGH(bts, count) \
  (BTS_IS_INDEX_COVERED (bts) ? \
   /* Covering index: no hard limit. */ \
   true \
   /* Normal scan: use buffer capacity as hard limit. */ \
   : (count)  <= (bts)->index_scan_idp->oid_list->capacity)

/* Save an object selected during scan into object buffer. This can only be
 * used by two types of scans:
 * 1. Regular range scans.
 * 2. Index skip scan, if current operations is ISS_OP_DO_RANGE_SEARCH.
 */
#define BTS_SAVE_OID_IN_BUFFER(bts, oid) \
  do \
    { \
      /* Assert this is not used in an inappropriate context. */ \
      assert (!BTS_IS_INDEX_COVERED (bts)); \
      assert (!BTS_IS_INDEX_MRO (bts)); \
      assert (!BTS_IS_INDEX_ISS (bts) \
	      || bts->index_scan_idp->iss.current_op \
		 == ISS_OP_DO_RANGE_SEARCH); \
      COPY_OID ((bts)->oid_ptr, oid); \
      (bts)->oid_ptr++; \
      BTS_INCREMENT_READ_OIDS (bts); \
      assert ((bts)->n_oids_read_last_iteration \
	      <= (bts)->index_scan_idp->oid_list->capacity); \
      assert (((bts)->oid_ptr - (bts)->index_scan_idp->oid_list->oidp) \
	      <= (bts)->index_scan_idp->oid_list->capacity); \
      /* Should we also increment (bts)->index_scan_idp->oid_list.oid_cnt? */ \
    } while (false)

/* Reset b-tree scan for a new range scan (it can be called internally by
 * btree_range_scan).
 */
#define BTS_RESET_SCAN(bts) \
  do \
    { \
      /* Reset bts->is_scan_started. */ \
      (bts)->is_scan_started = false; \
      /* No current leaf node. */ \
      VPID_SET_NULL (&(bts)->C_vpid); \
      if (bts->C_page != NULL) \
	{ \
	  pgbuf_unfix_and_init (NULL, bts->C_page); \
	} \
      /* No current key. */ \
      btree_scan_clear_key (bts); \
    } while (false)

/* BTREE_FIND_FK_OBJECT -
 * Structure used to find if a key of foreign key index has any objects.
 */
typedef struct btree_find_fk_object BTREE_FIND_FK_OBJECT;
struct btree_find_fk_object
{
  OID found_oid;
#if defined (SERVER_MODE)
  OID locked_object;
  LOCK lock_mode;
#endif				/* SERVER_MODE */
};
/* BTREE_FIND_FK_OBJECT static initializer */
#if defined (SERVER_MODE)
#define BTREE_FIND_FK_OBJECT_INITIALIZER \
  { OID_INITIALIZER, OID_INITIALIZER, NULL_LOCK }
#else	/* !SERVER_MODE */		   /* SA_MODE */
#define BTREE_FIND_FK_OBJECT_INITIALIZER \
  { OID_INITIALIZER }
#endif /* SA_MODE */

/* BTREE_INSERT_HELPER -
 * Structure used inside btree_insert_internal functions to group required
 * data into one argument.
 */
typedef struct btree_insert_helper BTREE_INSERT_HELPER;
struct btree_insert_helper
{
  BTREE_OBJECT_INFO obj_info;	/* B-tree object info. */
  BTREE_OP_PURPOSE purpose;	/* Purpose/context for calling
				 * btree_insert_internal.
				 */
  int op_type;			/* Single-multi insert/modify
				 * operation type.
				 */
  BTREE_UNIQUE_STATS *unique_stats_info;	/* Unique statistics kept when
						 * operation type is not single.
						 */
  int key_len_in_page;		/* Packed length of key being
				 * inserted.
				 */

  PGBUF_LATCH_MODE nonleaf_latch_mode;	/* Default page latch mode while
					 * advancing through non-leaf
					 * nodes.
					 */

  bool is_first_try;		/* True if this is first attempt to
				 * fix root page. B-tree information
				 * is loaded only first time.
				 */
  bool need_update_max_key_len;	/* Set to true when a node max key
				 * length must be updated. All
				 * children nodes will also update
				 * max key length.
				 */
  bool is_crt_node_write_latched;	/* Set to true when a node is
					 * latched exclusively. Then
					 * promotion will not be required.
					 */
  bool is_root;			/* True if current node is root. */

  bool is_unique_key_added_or_deleted;	/* Set to true when keys are
					 * inserted for the first time or
					 * when they are deleted.
					 */
  bool is_unique_multi_update;	/* Multi-update of unique index.
				 * More than one visible object
				 * may be allowed, as long as at
				 * the end of execution, unique
				 * constraint is not violated.
				 */
  bool is_ha_enabled;		/* An exception to above rule. If
				 * HA is enabled, no unique
				 * constraint violation is allowed
				 * at any time.
				 */
  bool is_relocation_for_unique;	/* Parameter to let insert know how to
					 * handle logging for appending an
					 * object.
					 */

  bool log_operations;		/* er_log. */
  bool is_null;			/* is key NULL. */
  char *printed_key;		/* Printed key. */

  /* Recovery data. */
  LOG_DATA_ADDR leaf_addr;
  LOG_RCVINDEX rcvindex;
  char *rv_undo_data;
  int rv_undo_data_length;
  char *rv_redo_data;
  char *rv_redo_data_ptr;

#if defined (SERVER_MODE)
  OID saved_locked_oid;		/* Save locked object from unique index key.
				 */
  OID saved_locked_class_oid;	/* Save class of locked object. */
#endif				/* SERVER_MODE */
};
/* BTREE_INSERT_HELPER initializer. SERVER_MODE includes additional fields
 * used for locking.
 */
#if defined (SERVER_MODE)
#define BTREE_INSERT_HELPER_INITIALIZER \
  { \
    BTREE_OBJECT_INFO_INITIALIZER /* obj_info */, \
    BTREE_OP_NO_OP /* purpose */, \
    0 /* op_type */, \
    NULL /* unique_stats_info */, \
    0 /* key_len_in_page */, \
    PGBUF_LATCH_READ /* latch_mode */, \
    true /* is_first_try */, \
    false /* need_update_max_key_len */, \
    false /* is_crt_node_write_latched */, \
    false /* is_root */, \
    true /* is_unique_key_added_or_deleted */, \
    false /* is_unique_multi_update */, \
    false /* is_ha_enabled */, \
    false /* is_relocation_for_unique */, \
    false /* log_operations */, \
    false /* is_null */, \
    NULL /* printed_key */, \
    LOG_DATA_ADDR_INITIALIZER /* leaf_addr */, \
    RV_NOT_DEFINED /* rcvindex */, \
    NULL /* rv_undo_data */, \
    0 /* rv_undo_data_length */, \
    NULL /* rv_redo_data */, \
    NULL /* rv_redo_data_ptr */, \
    OID_INITIALIZER /* saved_locked_oid */, \
    OID_INITIALIZER /* saved_locked_class_oid */ \
  }
#else	/* !SERVER_MODE */		   /* SA_MODE */
#define BTREE_INSERT_HELPER_INITIALIZER \
  { \
    BTREE_OBJECT_INFO_INITIALIZER /* obj_info */, \
    BTREE_OP_NO_OP /* purpose */, \
    0 /* op_type */, \
    NULL /* unique_stats_info */, \
    0 /* key_len_in_page */, \
    PGBUF_LATCH_READ /* latch_mode */, \
    true /* is_first_try */, \
    false /* need_update_max_key_len */, \
    false /* is_crt_node_write_latched */, \
    false /* is_root */, \
    true /* is_unique_key_added_or_deleted */, \
    false /* is_unique_multi_update */, \
    false /* is_ha_enabled */, \
    false /* is_relocation_for_unique */, \
    false /* log_operations */, \
    false /* is_null */, \
    NULL /* printed_key */, \
    LOG_DATA_ADDR_INITIALIZER /* leaf_addr */, \
    RV_NOT_DEFINED /* rcvindex */, \
    NULL /* rv_undo_data */, \
    0 /* rv_undo_data_length */, \
    NULL /* rv_redo_data */, \
    NULL /* rv_redo_data_ptr */ \
  }
#endif /* SA_MODE */

#define BTREE_INSERT_OID(ins_helper) \
  (&((ins_helper)->obj_info.oid))
#define BTREE_INSERT_CLASS_OID(ins_helper) \
  (&((ins_helper)->obj_info.class_oid))
#define BTREE_INSERT_MVCC_INFO(ins_helper) \
  (&((ins_helper)->obj_info.mvcc_info))

/* BTREE_DELETE_HELPER -
 * Structure used inside btree_delete_internal functions to group required
 * data into one argument.
 */
typedef struct btree_delete_helper BTREE_DELETE_HELPER;
struct btree_delete_helper
{
  BTREE_OBJECT_INFO object_info;	/* Object info required for b-tree. */
  BTREE_OP_PURPOSE purpose;	/* Purpose of delete operation. */
  PGBUF_LATCH_MODE nonleaf_latch_mode;	/* Latch mode used to for non-leaf
					 * nodes.
					 */
  int op_type;			/* Operation type. */
  BTREE_UNIQUE_STATS *unique_stats_info;	/* Used to collect statistics
						 * of multi-row operations in
						 * unique indexes.
						 */
  MVCCID match_mvccid;		/* MVCCID to be matched by either insert
				 * or delete MVCCID when looking for
				 * an object.
				 */
  OR_BUF *buffered_key;		/* Buffered key value. */
  char *printed_key;		/* Key printed value. */
  bool log_operations;		/* Debugging purpose logging. */
  bool is_root;			/* True if current node is root. */
  bool is_first_search;		/* True for the first b-tree traversal.
				 */
  bool check_key_deleted;	/* Set to true if it is possible to have
				 * more than one visible object in
				 * unique key (MULTI_ROW_UPDATE).
				 */
  bool is_key_deleted;		/* Used to correct collected statistics
				 * when key is not actually deleted.
				 */

  /* Recovery structures. */
  LOG_DATA_ADDR leaf_addr;
  char *rv_undo_data;
  int rv_undo_data_length;
  char *rv_redo_data;
  char *rv_redo_data_ptr;
};
#define BTREE_DELETE_HELPER_INITIALIZER \
  { \
    BTREE_OBJECT_INFO_INITIALIZER /* object_info */, \
    BTREE_OP_NO_OP /* purpose */, \
    PGBUF_LATCH_READ /* non_leaf_latch_mode */, \
    SINGLE_ROW_DELETE /* op_type */, \
    NULL /* unique_stats_info */, \
    MVCCID_NULL /* match_mvccid */, \
    NULL /* buffered_key */, \
    NULL /* printed_key */, \
    false /* log_operations */, \
    false /* is_root */, \
    true /* is_first_search */, \
    false /* check_key_deleted */, \
    false /* is_key_deleted */, \
    LOG_DATA_ADDR_INITIALIZER /* leaf_addr */, \
    NULL /* rv_undo_data */, \
    0 /* rv_undo_data_length */, \
    NULL /* rv_redo_data */, \
    NULL /* rv_redo_data_ptr */ \
  }

#define BTREE_DELETE_OID(helper) \
  (&((helper)->object_info.oid))
#define BTREE_DELETE_CLASS_OID(helper) \
  (&((helper)->object_info.class_oid))
#define BTREE_DELETE_MVCC_INFO(helper) \
  (&((helper)->object_info.mvcc_info))

/* B-tree redo recovery flags. They are additional to
 * LOG_RV_RECORD_MODIFY_MASK.
 */
/* Flag record belongs to overflow node. */
#define BTREE_RV_REDO_OVERFLOW_FLAG		0x2000
/* Flag redo data contains debugging info. */
#define BTREE_RV_REDO_DEBUG_INFO_FLAG		0x1000
/* Exclusive b-tree redo flags mask. */
#define BTREE_RV_REDO_EXCLUSIVE_FLAGS_MASK	0x3C00
/* The available flags are 0x0800 and 0x0400. B-tree recovery needs 10 bits
 * for around 820 maximum possible slots.
 * IO_MAX_PAGE_SIZE / (slot size + min record size) = 16k/20 ~= 820.
 */

/* B-tree redo recovery flags mask. */
#define BTREE_RV_REDO_FLAGS_MASK \
  (LOG_RV_RECORD_MODIFY_MASK | BTREE_RV_REDO_EXCLUSIVE_FLAGS_MASK)

/* Set overflow flag for redo. */
#define BTREE_RV_REDO_SET_OVERFLOW_NODE(addr) \
  ((addr)->offset |= BTREE_RV_REDO_OVERFLOW_FLAG)
#if !defined (NDEBUG)
/* Set debug info for redo.*/
#define BTREE_RV_REDO_SET_DEBUG_INFO(addr, rv_ptr, btid_int, id) \
  do \
    { \
      assert ((addr) != NULL); \
      assert ((rv_ptr) != NULL); \
      assert ((btid_int) != NULL); \
      assert (!BTREE_RV_REDO_HAS_DEBUG_INFO ((addr)->offset)); \
      if (or_packed_domain_size (btid_int->key_type, 0) \
	  > BTID_DOMAIN_CHECK_MAX_SIZE) \
        { \
	  /* Too much space required. Give up packing debug info. */ \
	  break; \
	} \
      /* Put debug ID. */ \
      OR_PUT_INT (rv_ptr, id); \
      (rv_ptr) += OR_INT_SIZE; \
      /* Put unique_pk */ \
      OR_PUT_INT (rv_ptr, (btid_int)->unique_pk); \
      (rv_ptr) += OR_INT_SIZE; \
      if (BTREE_IS_UNIQUE ((btid_int)->unique_pk)) \
	{ \
	  /* Put topclass_oid. */ \
	  OR_PUT_OID (rv_ptr, &(btid_int)->topclass_oid); \
	  (rv_ptr) += OR_OID_SIZE; \
	} \
      /* Put key type. */ \
      (rv_ptr) = or_pack_domain (rv_ptr, btid_int->key_type, 0, 0); \
      (rv_ptr) = PTR_ALIGN (rv_ptr, INT_ALIGNMENT); \
      (addr)->offset |= BTREE_RV_REDO_DEBUG_INFO_FLAG; \
    } \
  while (false)
#define BTREE_RV_REDO_DEBUG_INFO_MAX_SIZE \
  (OR_INT_SIZE /* Debug ID. */ \
   + OR_INT_SIZE /* unique_pk */ \
   + OR_OID_SIZE /* topclass_oid */ \
   + BTID_DOMAIN_CHECK_MAX_SIZE /* key_type. */)
#endif /* !NDEBUG */

/* Is flag for overflow node set? */
#define BTREE_RV_REDO_IS_OVERFLOW_NODE(flags) \
  ((flags & BTREE_RV_REDO_OVERFLOW_FLAG) != 0)
/* Is flag for debug info set? */
#define BTREE_RV_REDO_HAS_DEBUG_INFO(flags) \
  ((flags & BTREE_RV_REDO_DEBUG_INFO_FLAG) != 0)

/* Flag used only in context of insert new key. */
#define BTREE_RV_REDO_UPDATE_MAX_KEY_LEN 0x0800
#define BTREE_RV_REDO_SET_UPDATE_MAX_KEY_LEN(addr) \
  ((addr)->offset |= BTREE_RV_REDO_UPDATE_MAX_KEY_LEN)
#define BTREE_RV_REDO_IS_UPDATE_MAX_KEY_LEN(flags) \
  ((flags & BTREE_RV_REDO_UPDATE_MAX_KEY_LEN) != 0)

/* Default buffer size of redo recovery changes. Should cover all cases. */
/* Just a rough estimation */
#if defined (NDEBUG)
#define BTREE_RV_REDO_BUFFER_SIZE \
  ((int) (3 * LOG_RV_RECORD_UPDPARTIAL_ALIGNED_SIZE (BTREE_OBJECT_MAX_SIZE)))
#else /* !NDEBUG */
#define BTREE_RV_REDO_BUFFER_SIZE \
  ((int) (3 * LOG_RV_RECORD_UPDPARTIAL_ALIGNED_SIZE (BTREE_OBJECT_MAX_SIZE) \
   + BTREE_RV_REDO_DEBUG_INFO_MAX_SIZE))
#endif /* !NDEBUG */

/* Debug identifiers to help with detecting recovery issues. */
typedef enum btree_rv_debug_id BTREE_RV_DEBUG_ID;
enum btree_rv_debug_id
{
  BTREE_RV_REDO_NO_ID = 0,
  BTREE_RV_DEBUG_ID_INSERT_DELID,
  BTREE_RV_DEBUG_ID_START_OVF,
  BTREE_RV_DEBUG_ID_INS_NEW_OVF,
  BTREE_RV_DEBUG_ID_INS_OLD_OVF,
  BTREE_RV_DEBUG_ID_UNIQUE,
  BTREE_RV_DEBUG_ID_NON_UNIQUE,
  BTREE_RV_DEBUG_ID_REM_INSID,
  BTREE_RV_DEBUG_ID_REM_DELID_UNIQUE,
  BTREE_RV_DEBUG_ID_REM_DELID_NON_UNIQUE,
  BTREE_RV_DEBUG_ID_OVF_REPLACE,
  BTREE_RV_DEBUG_ID_SWAP_OVF,
  BTREE_RV_DEBUG_ID_SWAP_LEAF,
  BTREE_RV_DEBUG_ID_OVF_LINK,
  BTREE_RV_DEBUG_ID_LAST_OID,
  BTREE_RV_DEBUG_ID_REM_OBJ,
  BTREE_RV_DEBUG_ID_INS_KEY
};

/*
 * Static functions
 */

STATIC_INLINE PAGE_PTR
btree_fix_root_with_info (THREAD_ENTRY * thread_p, BTID * btid,
			  PGBUF_LATCH_MODE latch_mode, VPID * root_vpid_p,
			  BTREE_ROOT_HEADER ** root_header_p,
			  BTID_INT * btid_int_p)
__attribute__ ((ALWAYS_INLINE));

STATIC_INLINE bool
btree_is_fence_key (PAGE_PTR leaf_page, PGSLOTID slotid)
__attribute__ ((ALWAYS_INLINE));

STATIC_INLINE int
btree_count_oids (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
		  RECDES * record, char *object_ptr, OID * oid,
		  OID * class_oid, MVCC_REC_HEADER * mvcc_header, bool * stop,
		  void *args) __attribute__ ((ALWAYS_INLINE));

static int btree_store_overflow_key (THREAD_ENTRY * thread_p,
				     BTID_INT * btid, DB_VALUE * key,
				     int size, BTREE_NODE_TYPE node_type,
				     VPID * firstpg_vpid);
static int btree_load_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    VPID * firstpg_vpid, DB_VALUE * key,
				    BTREE_NODE_TYPE node_type);
static int btree_delete_overflow_key (THREAD_ENTRY * thread_p,
				      BTID_INT * btid, PAGE_PTR page_ptr,
				      INT16 slot_id,
				      BTREE_NODE_TYPE node_type);
static void btree_write_fixed_portion_of_non_leaf_record (RECDES * rec,
							  NON_LEAF_REC *
							  nlf_rec);
static void btree_read_fixed_portion_of_non_leaf_record (RECDES * rec,
							 NON_LEAF_REC *
							 nlf_rec);
static void btree_write_fixed_portion_of_non_leaf_record_to_orbuf (OR_BUF *
								   buf,
								   NON_LEAF_REC
								   * nlf_rec);
static int btree_read_fixed_portion_of_non_leaf_record_from_orbuf (OR_BUF *
								   buf,
								   NON_LEAF_REC
								   * nlf_rec);
static void btree_append_oid (RECDES * rec, OID * oid);
STATIC_INLINE void btree_add_mvcc_delid (RECDES * rec, int oid_offset,
					 int mvcc_delid_offset,
					 MVCCID * p_mvcc_delid,
					 char **rv_redo_data_ptr)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void btree_set_mvcc_delid (RECDES * rec, int mvcc_delid_offset,
					 MVCCID * p_mvcc_delid,
					 char **rv_redo_data_ptr)
  __attribute__ ((ALWAYS_INLINE));
static void btree_record_append_object (THREAD_ENTRY * thread_p,
					BTID_INT * btid_int, RECDES * record,
					BTREE_NODE_TYPE node_type,
					BTREE_OBJECT_INFO * object_info,
					char **rv_redo_data_ptr);
static void btree_insert_object_ordered_by_oid (RECDES * record,
						BTID_INT * btid_int,
						BTREE_OBJECT_INFO *
						object_info,
						char **rv_redo_data_ptr);
static int btree_start_overflow_page (THREAD_ENTRY * thread_p,
				      BTID_INT * btid_int,
				      BTREE_OBJECT_INFO * object_info,
				      VPID * first_overflow_vpid,
				      VPID * near_vpid, VPID * new_vpid,
				      PAGE_PTR * new_page_ptr);
static int btree_read_record_without_decompression (THREAD_ENTRY * thread_p,
						    BTID_INT * btid,
						    RECDES * Rec,
						    DB_VALUE * key,
						    void *rec_header,
						    int node_type,
						    bool * clear_key,
						    int *offset, int copy);
static PAGE_PTR btree_get_new_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    VPID * vpid, VPID * near_vpid);
static int btree_dealloc_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
			       VPID * vpid);
static bool btree_initialize_new_page (THREAD_ENTRY * thread_p,
				       const VFID * vfid,
				       const FILE_TYPE file_type,
				       const VPID * vpid, INT32 ignore_npages,
				       void *args);
static int btree_search_nonleaf_page (THREAD_ENTRY * thread_p,
				      BTID_INT * btid, PAGE_PTR page_ptr,
				      DB_VALUE * key, INT16 * slot_id,
				      VPID * child_vpid);
static int btree_search_leaf_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
				   PAGE_PTR page_ptr, DB_VALUE * key,
				   BTREE_SEARCH_KEY_HELPER * search_key);
static int xbtree_test_unique (THREAD_ENTRY * thread_p, BTID * btid);
#if defined(ENABLE_UNUSED_FUNCTION)
static int btree_get_subtree_stats (THREAD_ENTRY * thread_p,
				    BTID_INT * btid, PAGE_PTR pg_ptr,
				    BTREE_STATS_ENV * env);
#endif
static int btree_get_stats_midxkey (THREAD_ENTRY * thread_p,
				    BTREE_STATS_ENV * env,
				    DB_MIDXKEY * midxkey);
static int btree_get_stats_key (THREAD_ENTRY * thread_p,
				BTREE_STATS_ENV * env,
				MVCC_SNAPSHOT * mvcc_snapshot);
static int btree_get_stats_with_AR_sampling (THREAD_ENTRY * thread_p,
					     BTREE_STATS_ENV * env);
static int btree_get_stats_with_fullscan (THREAD_ENTRY * thread_p,
					  BTREE_STATS_ENV * env);
static DISK_ISVALID btree_check_page_key (THREAD_ENTRY * thread_p,
					  const OID * class_oid_p,
					  BTID_INT * btid, const char *btname,
					  PAGE_PTR page_ptr,
					  VPID * page_vpid);
static DISK_ISVALID btree_check_pages (THREAD_ENTRY * thread_p,
				       BTID_INT * btid, PAGE_PTR pg_ptr,
				       VPID * pg_vpid);
static DISK_ISVALID btree_verify_subtree (THREAD_ENTRY * thread_p,
					  const OID * class_oid_p,
					  BTID_INT * btid, const char *btname,
					  PAGE_PTR pg_ptr, VPID * pg_vpid,
					  BTREE_NODE_INFO * INFO);
static int btree_get_subtree_capacity (THREAD_ENTRY * thread_p,
				       BTID_INT * btid, PAGE_PTR pg_ptr,
				       BTREE_CAPACITY * cpc);
static void btree_print_space (FILE * fp, int n);
static int btree_delete_meta_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
				     PAGE_PTR page_ptr, int slot_id);
static int btree_merge_root (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR P, PAGE_PTR Q, PAGE_PTR R);
static int btree_merge_node (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR P, PAGE_PTR Q, PAGE_PTR R,
			     INT16 p_slot_id, VPID * child_vpid,
			     BTREE_MERGE_STATUS status);
static int btree_node_size_uncompressed (THREAD_ENTRY * thread_p,
					 BTID_INT * btid, PAGE_PTR page_ptr);
static BTREE_MERGE_STATUS btree_node_mergeable (THREAD_ENTRY * thread_p,
						BTID_INT * btid, PAGE_PTR L,
						PAGE_PTR R);
static DB_VALUE *btree_find_split_point (THREAD_ENTRY * thread_p,
					 BTID_INT * btid, PAGE_PTR page_ptr,
					 int *mid_slot, DB_VALUE * key,
					 bool * clear_midkey);
static int btree_split_next_pivot (BTREE_NODE_SPLIT_INFO * split_info,
				   float new_value, int max_index);
static int btree_split_find_pivot (int total,
				   BTREE_NODE_SPLIT_INFO * split_info);
static int btree_split_node (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR P, PAGE_PTR Q, PAGE_PTR R,
			     VPID * P_vpid, VPID * Q_vpid, VPID * R_vpid,
			     INT16 p_slot_id, BTREE_NODE_TYPE node_type,
			     DB_VALUE * key, VPID * child_vpid);
static int btree_split_root (THREAD_ENTRY * thread_p, BTID_INT * btid,
			     PAGE_PTR P, PAGE_PTR Q, PAGE_PTR R,
			     VPID * P_vpid, VPID * Q_vpid, VPID * R_vpid,
			     BTREE_NODE_TYPE node_type, DB_VALUE * key,
			     VPID * child_vpid);
static PAGE_PTR btree_locate_key (THREAD_ENTRY * thread_p,
				  BTID_INT * btid_int, DB_VALUE * key,
				  VPID * pg_vpid, INT16 * slot_id,
				  bool * found_p);
static int btree_find_lower_bound_leaf (THREAD_ENTRY * thread_p,
					BTREE_SCAN * BTS,
					BTREE_STATS * stat_info_p);
static PAGE_PTR btree_find_leftmost_leaf (THREAD_ENTRY * thread_p,
					  BTID * btid, VPID * pg_vpid,
					  BTREE_STATS * stat_info_p);
static PAGE_PTR btree_find_rightmost_leaf (THREAD_ENTRY * thread_p,
					   BTID * btid, VPID * pg_vpid,
					   BTREE_STATS * stat_info_p);
static PAGE_PTR btree_find_AR_sampling_leaf (THREAD_ENTRY * thread_p,
					     BTID * btid, VPID * pg_vpid,
					     BTREE_STATS * stat_info_p,
					     bool * found_p);
static PAGE_PTR btree_find_boundary_leaf (THREAD_ENTRY * thread_p,
					  BTID * btid, VPID * pg_vpid,
					  BTREE_STATS * stat_info,
					  BTREE_BOUNDARY where);
static int btree_find_next_index_record (THREAD_ENTRY * thread_p,
					 BTREE_SCAN * bts);
static int btree_find_next_index_record_holding_current (THREAD_ENTRY *
							 thread_p,
							 BTREE_SCAN * bts,
							 RECDES * peek_rec);
static int btree_find_next_index_record_holding_current_helper (THREAD_ENTRY *
								thread_p,
								BTREE_SCAN *
								bts,
								PAGE_PTR
								first_page);
static int btree_apply_key_range_and_filter (THREAD_ENTRY * thread_p,
					     BTREE_SCAN * bts, bool is_iss,
					     bool * key_range_satisfied,
					     bool * key_filter_satisfied,
					     bool need_to_check_null);
static int btree_dump_curr_key (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
				FILTER_INFO * filter, OID * oid,
				INDX_SCAN_ID * iscan_id);
#if 0				/* TODO: currently, unused */
static int btree_get_prev_keyvalue (BTREE_SCAN * bts,
				    DB_VALUE * prev_key, int *prev_clr_key);
#endif
static DISK_ISVALID btree_find_key_from_leaf (THREAD_ENTRY * thread_p,
					      BTID_INT * btid,
					      PAGE_PTR pg_ptr, int key_cnt,
					      OID * oid, DB_VALUE * key,
					      bool * clear_key);
static DISK_ISVALID btree_find_key_from_nleaf (THREAD_ENTRY * thread_p,
					       BTID_INT * btid,
					       PAGE_PTR pg_ptr, int key_cnt,
					       OID * oid, DB_VALUE * key,
					       bool * clear_key);
static DISK_ISVALID btree_find_key_from_page (THREAD_ENTRY * thread_p,
					      BTID_INT * btid,
					      PAGE_PTR pg_ptr, OID * oid,
					      DB_VALUE * key,
					      bool * clear_key);

/* Dump & verify routines */
static void btree_dump_root_header (FILE * fp, PAGE_PTR page_ptr);
static void btree_dump_leaf_record (THREAD_ENTRY * thread_p, FILE * fp,
				    BTID_INT * btid, RECDES * rec, int n);
static void btree_dump_non_leaf_record (THREAD_ENTRY * thread_p,
					FILE * fp, BTID_INT * btid,
					RECDES * rec, int n, int print_key);
static void btree_dump_page (THREAD_ENTRY * thread_p, FILE * fp,
			     const OID * class_oid_p, BTID_INT * btid,
			     const char *btname, PAGE_PTR page_ptr,
			     VPID * pg_vpid, int depth, int level);

static void btree_dump_page_with_subtree (THREAD_ENTRY * thread_p,
					  FILE * fp, BTID_INT * btid,
					  PAGE_PTR pg_ptr,
					  VPID * pg_vpid, int depth,
					  int level);

#if !defined(NDEBUG)
static DB_VALUE *btree_set_split_point (THREAD_ENTRY * thread_p,
					BTID_INT * btid, PAGE_PTR page_ptr,
					INT16 mid_slot, DB_VALUE * key,
					bool * clear_midkey);
static void btree_split_test (THREAD_ENTRY * thread_p, BTID_INT * btid,
			      DB_VALUE * key, VPID * S_vpid, PAGE_PTR S_page,
			      BTREE_NODE_TYPE node_type);
static int btree_verify_node (THREAD_ENTRY * thread_p,
			      BTID_INT * btid_int, PAGE_PTR page_ptr);
static int btree_verify_nonleaf_node (THREAD_ENTRY * thread_p,
				      BTID_INT * btid_int, PAGE_PTR page_ptr);
static int btree_verify_leaf_node (THREAD_ENTRY * thread_p,
				   BTID_INT * btid_int, PAGE_PTR page_ptr);
#endif

static void btree_set_unknown_key_error (THREAD_ENTRY * thread_p,
					 BTID * btid, DB_VALUE * key,
					 const char *debug_msg);
static int btree_rv_write_log_record_for_key_insert (char *log_rec,
						     int *log_length,
						     INT16 key_len,
						     RECDES * recp);

static int btree_rv_write_log_record (char *log_rec, int *log_length,
				      RECDES * recp,
				      BTREE_NODE_TYPE node_type);

static int btree_find_oid_and_its_page (THREAD_ENTRY * thread_p,
					BTID_INT * btid_int, OID * oid,
					PAGE_PTR leaf_page,
					BTREE_OP_PURPOSE purpose,
					MVCCID * match_mvccid,
					RECDES * leaf_record,
					LEAF_REC * leaf_rec_info,
					int after_key_offset,
					PAGE_PTR * found_page,
					PAGE_PTR * prev_page,
					int *offset_to_object,
					BTREE_MVCC_INFO * object_mvcc_info);
static int btree_find_oid_does_mvcc_info_match (THREAD_ENTRY * thread_p,
						BTREE_MVCC_INFO * mvcc_info,
						BTREE_OP_PURPOSE purpose,
						MVCCID * match_mvccid,
						bool * is_match);
static int btree_find_oid_from_leaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
				     RECDES * leaf_record,
				     int after_key_offset, OID * oid,
				     MVCCID * match_mvccid,
				     BTREE_OP_PURPOSE purpose,
				     int *offset_to_object,
				     BTREE_MVCC_INFO * mvcc_info);
static int btree_find_oid_from_ovfl (THREAD_ENTRY * thread_p,
				     BTID_INT * btid_int,
				     PAGE_PTR overflow_page, OID * oid,
				     BTREE_OP_PURPOSE purpose,
				     MVCCID * match_mvccid,
				     int *offset_to_object,
				     BTREE_MVCC_INFO * mvcc_info);
static int btree_leaf_get_vpid_for_overflow_oids (RECDES * rec, VPID * vpid);
#if defined(ENABLE_UNUSED_FUNCTION)
static int btree_leaf_put_first_oid (RECDES * recp, OID * oidp,
				     short record_flag);
#endif
static int btree_record_get_last_object (THREAD_ENTRY * thread_p,
					 BTID_INT * btid_int, RECDES * recp,
					 BTREE_NODE_TYPE node_type,
					 int after_key_offset,
					 OID * oidp, OID * class_oid,
					 BTREE_MVCC_INFO * mvcc_info,
					 int *last_oid_mvcc_offset);
static void btree_record_remove_last_object (THREAD_ENTRY * thread_p,
					     BTID_INT * btid, RECDES * recp,
					     BTREE_NODE_TYPE node_type,
					     int last_oid_mvcc_offset,
					     char **rv_redo_data_ptr);
static char *btree_leaf_get_nth_oid_ptr (BTID_INT * btid, RECDES * recp,
					 BTREE_NODE_TYPE node_type,
					 int oid_list_offset, int n);
static void btree_leaf_set_flag (RECDES * recp, short record_flag);
static void btree_leaf_clear_flag (RECDES * recp, short record_flag);
static short btree_leaf_get_flag (RECDES * recp);
static bool btree_leaf_is_flaged (RECDES * recp, short record_flag);
static void btree_record_object_set_mvcc_flags (char *data, short mvcc_flags);
static void btree_record_object_clear_mvcc_flags (char *rec_data,
						  short mvcc_flags);
static INLINE short btree_record_object_get_mvcc_flags (char *data)
  __attribute__ ((ALWAYS_INLINE));
static INLINE bool btree_record_object_is_flagged (char *data,
						   short mvcc_flag)
  __attribute__ ((ALWAYS_INLINE));
static void btree_leaf_rebuild_mvccids_in_record (RECDES * recp, int offset,
						  int mvcc_old_oid_mvcc_flags,
						  int oid_size,
						  MVCC_REC_HEADER *
						  p_mvcc_rec_header);
static void btree_leaf_record_handle_first_overflow (RECDES * recp,
						     BTID_INT * btid_int,
						     char **rv_redo_data_ptr);
static int btree_record_get_num_oids (THREAD_ENTRY * thread_p,
				      BTID_INT * btid_int, RECDES * rec,
				      int offset, BTREE_NODE_TYPE node_type);
static int btree_get_num_visible_from_leaf_and_ovf (THREAD_ENTRY * thread_p,
						    BTID_INT * btid_int,
						    RECDES * leaf_record,
						    int offset_after_key,
						    LEAF_REC * leaf_info,
						    int *max_visible_oids,
						    MVCC_SNAPSHOT *
						    mvcc_snapshot);
static int btree_record_get_num_visible_oids (THREAD_ENTRY * thread_p,
					      BTID_INT * btid, RECDES * rec,
					      int oid_offset,
					      BTREE_NODE_TYPE node_type,
					      int *max_visible_oids,
					      MVCC_SNAPSHOT * mvcc_snapshot);
static int btree_get_num_visible_oids_from_all_ovf (THREAD_ENTRY * thread_p,
						    BTID_INT * btid,
						    VPID * first_ovfl_vpid,
						    int *num_visible_oids,
						    int *max_visible_oids,
						    MVCC_SNAPSHOT *
						    mvcc_snapshot);
static void btree_write_default_split_info (BTREE_NODE_SPLIT_INFO * info);
static int btree_set_vpid_previous_vpid (THREAD_ENTRY * thread_p,
					 BTID_INT * btid, PAGE_PTR page_p,
					 VPID * prev);
static int btree_get_next_page_vpid (THREAD_ENTRY * thread_p,
				     PAGE_PTR leaf_page, VPID * next_vpid);
static PAGE_PTR btree_get_next_page (THREAD_ENTRY * thread_p,
				     PAGE_PTR page_p);
static int btree_range_opt_check_add_index_key (THREAD_ENTRY * thread_p,
						BTREE_SCAN * bts,
						MULTI_RANGE_OPT *
						multi_range_opt,
						OID * p_new_oid,
						bool * key_added);
static int btree_top_n_items_binary_search (RANGE_OPT_ITEM ** top_n_items,
					    int *att_idxs,
					    TP_DOMAIN ** domains,
					    bool * desc_order,
					    DB_VALUE * new_key_values,
					    int no_keys, int first, int last,
					    int *new_pos);
static int btree_iss_set_key (BTREE_SCAN * bts, INDEX_SKIP_SCAN * iss);
static int btree_insert_mvcc_delid_into_page (THREAD_ENTRY * thread_p,
					      BTID_INT * btid,
					      PAGE_PTR page_ptr,
					      BTREE_NODE_TYPE node_type,
					      DB_VALUE * key,
					      BTREE_INSERT_HELPER *
					      insert_helper,
					      PGSLOTID slot_id,
					      RECDES * rec, int oid_offset);
static int
btree_key_append_object_as_new_overflow (THREAD_ENTRY * thread_p,
					 BTID_INT * btid_int,
					 PAGE_PTR leaf_page,
					 BTREE_OBJECT_INFO * object_info,
					 BTREE_INSERT_HELPER * insert_helper,
					 BTREE_SEARCH_KEY_HELPER * search_key,
					 RECDES * leaf_rec,
					 VPID * first_ovfl_vpid);
static int btree_key_append_object_to_overflow (THREAD_ENTRY * thread_p,
						BTID_INT * btid_int,
						PAGE_PTR ovfl_page,
						BTREE_OBJECT_INFO *
						object_info,
						BTREE_INSERT_HELPER *
						insert_helper);
static int btree_find_free_overflow_oids_page (THREAD_ENTRY * thread_p,
					       BTID_INT * btid,
					       VPID * first_ovfl_vpid,
					       PAGE_PTR * overflow_page);

static int btree_delete_key_from_leaf (THREAD_ENTRY * thread_p,
				       BTID_INT * btid, PAGE_PTR leaf_pg,
				       LEAF_REC * leafrec_pnt,
				       BTREE_DELETE_HELPER * delete_helper,
				       BTREE_SEARCH_KEY_HELPER * search_key);
static int btree_swap_first_oid_with_ovfl_rec (THREAD_ENTRY * thread_p,
					       BTID_INT * btid,
					       BTREE_DELETE_HELPER *
					       delete_helper,
					       PAGE_PTR leaf_page,
					       BTREE_SEARCH_KEY_HELPER *
					       search_key,
					       RECDES * leaf_rec,
					       VPID * ovfl_vpid);
static int btree_modify_leaf_ovfl_vpid (THREAD_ENTRY * thread_p,
					BTID_INT * btid_int,
					BTREE_DELETE_HELPER * delete_helper,
					PAGE_PTR leaf_page,
					RECDES * leaf_record,
					BTREE_SEARCH_KEY_HELPER * search_key,
					VPID * next_ovfl_vpid);
static int btree_modify_overflow_link (THREAD_ENTRY * thread_p,
				       BTID_INT * btid_int,
				       BTREE_DELETE_HELPER * delete_helper,
				       PAGE_PTR ovfl_page,
				       VPID * next_ovfl_vpid);

static DISK_ISVALID btree_repair_prev_link_by_btid (THREAD_ENTRY * thread_p,
						    BTID * btid, bool repair,
						    char *index_name);
static DISK_ISVALID btree_repair_prev_link_by_class_oid (THREAD_ENTRY *
							 thread_p, OID * oid,
							 BTID * idx_btid,
							 bool repair);
static bool btree_node_is_compressed (THREAD_ENTRY * thread_p,
				      BTID_INT * btid, PAGE_PTR page_ptr);
static int btree_node_common_prefix (THREAD_ENTRY * thread_p, BTID_INT * btid,
				     PAGE_PTR page_ptr);
static int btree_compress_records (THREAD_ENTRY * thread_p, BTID_INT * btid,
				   RECDES * rec, int key_cnt);
static int btree_compress_node (THREAD_ENTRY * thread_p, BTID_INT * btid,
				PAGE_PTR page_ptr);
static const char *node_type_to_string (short node_type);
static SCAN_CODE btree_scan_for_show_index_header (THREAD_ENTRY * thread_p,
						   DB_VALUE ** out_values,
						   int out_cnt,
						   const char *class_name,
						   const char *index_name,
						   BTID * btid_p);
static SCAN_CODE btree_scan_for_show_index_capacity (THREAD_ENTRY * thread_p,
						     DB_VALUE ** out_values,
						     int out_cnt,
						     const char *class_name,
						     const char *index_name,
						     BTID * btid_p);
static bool btree_leaf_lsa_eq (THREAD_ENTRY * thread_p, LOG_LSA * a,
			       LOG_LSA * b);

#if !defined(NDEBUG)
static int btree_get_node_level (PAGE_PTR page_ptr);
#endif

static BTREE_SEARCH
btree_key_find_first_visible_row (THREAD_ENTRY * thread_p,
				  BTID_INT * btid_int, RECDES * rec,
				  int offset, BTREE_NODE_TYPE node_type,
				  OID * oid, OID * class_oid, int max_oids);
static BTREE_SEARCH
btree_key_find_first_visible_row_from_all_ovf (THREAD_ENTRY * thread_p,
					       BTID_INT * btid_int,
					       VPID * first_ovfl_vpid,
					       OID * oid, OID * class_oid);

static int btree_or_put_mvccinfo (OR_BUF * buf, BTREE_MVCC_INFO * mvcc_info);
static int btree_or_get_mvccinfo (OR_BUF * buf, BTREE_MVCC_INFO * mvcc_info,
				  short btree_mvcc_flags);
static int btree_or_put_object (OR_BUF * buf, BTID_INT * btid_int,
				BTREE_NODE_TYPE node_type,
				OID * oid, OID * class_oid,
				BTREE_MVCC_INFO * mvcc_info);
static int btree_or_get_object (OR_BUF * buf, BTID_INT * btid_int,
				BTREE_NODE_TYPE node_type,
				int after_key_offset,
				OID * oid, OID * class_oid,
				BTREE_MVCC_INFO * mvcc_info);
static char *btree_unpack_object (char *ptr, BTID_INT * btid_int,
				  BTREE_NODE_TYPE node_type, RECDES * record,
				  int after_key_offset, OID * oid,
				  OID * class_oid,
				  BTREE_MVCC_INFO * mvcc_info);
static char *btree_pack_object (char *ptr, BTID_INT * btid_int,
				BTREE_NODE_TYPE node_type, RECDES * record,
				OID * oid, OID * class_oid,
				BTREE_MVCC_INFO * mvcc_info);

static int
btree_search_key_and_apply_functions (THREAD_ENTRY * thread_p, BTID * btid,
				      BTID_INT * btid_int, DB_VALUE * key,
				      BTREE_ROOT_WITH_KEY_FUNCTION *
				      root_fnct,
				      void *root_args,
				      BTREE_ADVANCE_WITH_KEY_FUNCTION *
				      advance_fnct,
				      void *advance_args,
				      BTREE_PROCESS_KEY_FUNCTION *
				      leaf_fnct,
				      void *process_key_args,
				      BTREE_SEARCH_KEY_HELPER * search_key,
				      PAGE_PTR * leaf_page_ptr);
static int btree_get_root_with_key (THREAD_ENTRY * thread_p, BTID * btid,
				    BTID_INT * btid_int, DB_VALUE * key,
				    PAGE_PTR * root_page, bool * is_leaf,
				    BTREE_SEARCH_KEY_HELPER * search_key,
				    bool * stop, bool * restart,
				    void *other_args);
static int btree_advance_and_find_key (THREAD_ENTRY * thread_p,
				       BTID_INT * btid_int,
				       DB_VALUE * key,
				       PAGE_PTR * crt_page,
				       PAGE_PTR * advance_to_page,
				       bool * is_leaf,
				       BTREE_SEARCH_KEY_HELPER * search_key,
				       bool * stop,
				       bool * restart, void *other_args);
static int btree_key_find_unique_version_oid (THREAD_ENTRY * thread_p,
					      BTID_INT * btid_int,
					      DB_VALUE * key,
					      PAGE_PTR * leaf_page,
					      BTREE_SEARCH_KEY_HELPER *
					      search_key,
					      bool * restart,
					      void *other_args);
static int btree_key_find_and_lock_unique (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   DB_VALUE * key,
					   PAGE_PTR * leaf_page,
					   BTREE_SEARCH_KEY_HELPER *
					   search_key,
					   bool * restart, void *other_args);
static int
btree_key_find_and_lock_unique_of_unique (THREAD_ENTRY * thread_p,
					  BTID_INT * btid_int, DB_VALUE * key,
					  PAGE_PTR * leaf_page,
					  BTREE_SEARCH_KEY_HELPER *
					  search_key,
					  bool * restart, void *other_args);
static int
btree_key_find_and_lock_unique_of_non_unique (THREAD_ENTRY * thread_p,
					      BTID_INT * btid_int,
					      DB_VALUE * key,
					      PAGE_PTR * leaf_page,
					      BTREE_SEARCH_KEY_HELPER *
					      search_key,
					      bool * restart,
					      void *other_args);
#if defined (SERVER_MODE)
static int btree_key_lock_object (THREAD_ENTRY * thread_p,
				  BTID_INT * btid_int, DB_VALUE * key,
				  PAGE_PTR * leaf_page,
				  PAGE_PTR * overflow_page, OID * oid,
				  OID * class_oid, LOCK lock_mode,
				  BTREE_SEARCH_KEY_HELPER * search_key,
				  bool try_cond_lock, bool * restart,
				  bool * was_page_refixed);
#endif /* SERVER_MODE */

static int btree_key_process_objects (THREAD_ENTRY * thread_p,
				      BTID_INT * btid_int,
				      RECDES * leaf_record,
				      int after_key_offset,
				      LEAF_REC * leaf_info,
				      BTREE_PROCESS_OBJECT_FUNCTION * func,
				      void *args);
static int btree_record_process_objects (THREAD_ENTRY * thread_p,
					 BTID_INT * btid_int,
					 BTREE_NODE_TYPE node_type,
					 RECDES * record,
					 int after_key_offset, bool * stop,
					 BTREE_PROCESS_OBJECT_FUNCTION *
					 func, void *args);
static int btree_record_object_compare (THREAD_ENTRY * thread_p,
					BTID_INT * btid_int, RECDES * record,
					char *object_ptr, OID * oid,
					OID * class_oid,
					BTREE_MVCC_INFO * mvcc_info,
					bool * stop, void *other_args);
static int btree_record_satisfies_snapshot (THREAD_ENTRY * thread_p,
					    BTID_INT * btid_int,
					    RECDES * record,
					    char *object_ptr,
					    OID * oid, OID * class_oid,
					    BTREE_MVCC_INFO * mvcc_info,
					    bool * stop, void *args);

static int btree_range_scan_read_record (THREAD_ENTRY * thread_p,
					 BTREE_SCAN * bts);
static int btree_range_scan_advance_over_filtered_keys (THREAD_ENTRY *
							thread_p,
							BTREE_SCAN * bts);
static int btree_range_scan_descending_fix_prev_leaf (THREAD_ENTRY * thread_p,
						      BTREE_SCAN * bts,
						      int *key_count,
						      BTREE_NODE_HEADER **
						      node_header_ptr,
						      VPID * next_vpid);
static int btree_range_scan_start (THREAD_ENTRY * thread_p, BTREE_SCAN * bts);
static int btree_range_scan_resume (THREAD_ENTRY * thread_p,
				    BTREE_SCAN * bts);
static int btree_range_scan_count_oids_leaf_and_one_ovf (THREAD_ENTRY *
							 thread_p,
							 BTREE_SCAN * bts);
static int btree_scan_update_range (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
				    KEY_VAL_RANGE * key_val_range);
static int btree_ils_adjust_range (THREAD_ENTRY * thread_p, BTREE_SCAN * bts);

static int btree_select_visible_object_for_range_scan (THREAD_ENTRY *
						       thread_p,
						       BTID_INT * btid_int,
						       RECDES * record,
						       char *object_ptr,
						       OID * oid,
						       OID * class_oid,
						       BTREE_MVCC_INFO *
						       mvcc_info,
						       bool * stop,
						       void *args);
static int btree_range_scan_find_fk_any_object (THREAD_ENTRY * thread_p,
						BTREE_SCAN * bts);
static int btree_fk_object_does_exist (THREAD_ENTRY * thread_p,
				       BTID_INT * btid_int, RECDES * record,
				       char *object_ptr, OID * oid,
				       OID * class_oid,
				       BTREE_MVCC_INFO * mvcc_info,
				       bool * stop, void *args);

static int btree_insert_internal (THREAD_ENTRY * thread_p, BTID * btid,
				  DB_VALUE * key, OID * class_oid, OID * oid,
				  int op_type,
				  BTREE_UNIQUE_STATS * unique_stat_info,
				  int *unique, BTREE_MVCC_INFO * mvcc_info,
				  BTREE_OP_PURPOSE purpose);
static int btree_undo_delete_physical (THREAD_ENTRY * thread_p, BTID * btid,
				       DB_VALUE * key, OID * class_oid,
				       OID * oid,
				       BTREE_MVCC_INFO * mvcc_info);
static int btree_fix_root_for_insert (THREAD_ENTRY * thread_p, BTID * btid,
				      BTID_INT * btid_int, DB_VALUE * key,
				      PAGE_PTR * root_page, bool * is_leaf,
				      BTREE_SEARCH_KEY_HELPER * search_key,
				      bool * stop, bool * restart,
				      void *other_args);
static int btree_split_node_and_advance (THREAD_ENTRY * thread_p,
					 BTID_INT * btid_int,
					 DB_VALUE * key, PAGE_PTR * crt_page,
					 PAGE_PTR * advance_to_page,
					 bool * is_leaf,
					 BTREE_SEARCH_KEY_HELPER * search_key,
					 bool * stop, bool * restart,
					 void *other_args);
static int btree_key_insert_new_object (THREAD_ENTRY * thread_p,
					BTID_INT * btid_int, DB_VALUE * key,
					PAGE_PTR * leaf_page,
					BTREE_SEARCH_KEY_HELPER * search_key,
					bool * restart, void *other_args);
static int btree_key_insert_new_key (THREAD_ENTRY * thread_p,
				     BTID_INT * btid_int, DB_VALUE * key,
				     PAGE_PTR leaf_page,
				     BTREE_INSERT_HELPER * insert_helper,
				     BTREE_SEARCH_KEY_HELPER * search_key);
static int btree_key_insert_delete_mvccid (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   DB_VALUE * key,
					   PAGE_PTR * leaf_page,
					   BTREE_SEARCH_KEY_HELPER *
					   search_key,
					   bool * restart, void *other_args);
static int btree_key_append_object_unique (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   DB_VALUE * key, PAGE_PTR * leaf,
					   bool * restart,
					   BTREE_SEARCH_KEY_HELPER *
					   search_key,
					   BTREE_INSERT_HELPER *
					   insert_helper,
					   RECDES * leaf_record);
static int btree_key_append_object_non_unique (THREAD_ENTRY * thread_p,
					       BTID_INT * btid_int,
					       DB_VALUE * key, PAGE_PTR leaf,
					       BTREE_SEARCH_KEY_HELPER *
					       search_key,
					       RECDES * leaf_record,
					       int offset_after_key,
					       LEAF_REC * leaf_info,
					       BTREE_OBJECT_INFO * btree_obj,
					       BTREE_INSERT_HELPER *
					       insert_helper);
#if defined (SERVER_MODE)
static bool btree_key_insert_does_leaf_need_split (THREAD_ENTRY * thread_p,
						   BTID_INT * btid_int,
						   PAGE_PTR leaf_page,
						   BTREE_INSERT_HELPER *
						   insert_helper,
						   BTREE_SEARCH_KEY_HELPER *
						   search_key);
#endif /* SERVER_MODE */
#if !defined (NDEBUG)
static void btree_key_record_check_no_visible (THREAD_ENTRY * thread_p,
					       BTID_INT * btid_int,
					       PAGE_PTR leaf_page,
					       PGSLOTID slotid);
#endif /* !NDEBUG */

static int btree_delete_internal (THREAD_ENTRY * thread_p, BTID * btid,
				  OID * oid, OID * class_oid,
				  BTREE_MVCC_INFO * mvcc_info,
				  DB_VALUE * key, OR_BUF * buffered_key,
				  int *unique, int op_type,
				  BTREE_UNIQUE_STATS * unique_stat_info,
				  MVCCID match_mvccid,
				  BTREE_OP_PURPOSE purpose);
static int btree_fix_root_for_delete (THREAD_ENTRY * thread_p, BTID * btid,
				      BTID_INT * btid_int, DB_VALUE * key,
				      PAGE_PTR * root_page, bool * is_leaf,
				      BTREE_SEARCH_KEY_HELPER * search_key,
				      bool * stop, bool * restart,
				      void *other_args);
static int btree_merge_node_and_advance (THREAD_ENTRY * thread_p,
					 BTID_INT * btid_int,
					 DB_VALUE * key, PAGE_PTR * crt_page,
					 PAGE_PTR * advance_to_page,
					 bool * is_leaf,
					 BTREE_SEARCH_KEY_HELPER * search_key,
					 bool * stop, bool * restart,
					 void *other_args);
static int btree_key_delete_remove_object (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   DB_VALUE * key,
					   PAGE_PTR * leaf_page,
					   BTREE_SEARCH_KEY_HELPER *
					   search_key,
					   bool * restart, void *other_args);
static int btree_leaf_record_replace_first_with_last (THREAD_ENTRY * thread_p,
						      BTID_INT * btid_int,
						      BTREE_DELETE_HELPER *
						      delete_helper,
						      PAGE_PTR leaf_page,
						      RECDES * leaf_record,
						      BTREE_SEARCH_KEY_HELPER
						      * search_key,
						      OID * last_oid,
						      OID * last_class_oid,
						      BTREE_MVCC_INFO *
						      last_mvcc_info,
						      int
						      offset_to_last_object);
static int btree_record_remove_object (THREAD_ENTRY * thread_p,
				       BTID_INT * btid_int,
				       BTREE_DELETE_HELPER * delete_helper,
				       PAGE_PTR page, RECDES * record,
				       BTREE_SEARCH_KEY_HELPER * search_key,
				       BTREE_NODE_TYPE node_type,
				       int offset_to_object,
				       LOG_DATA_ADDR * addr);
static int btree_overflow_remove_object (THREAD_ENTRY * thread_p,
					 BTID_INT * btid_int,
					 BTREE_DELETE_HELPER * delete_helper,
					 PAGE_PTR * overflow_page,
					 PAGE_PTR prev_page,
					 PAGE_PTR leaf_page,
					 RECDES * leaf_record,
					 BTREE_SEARCH_KEY_HELPER * search_key,
					 int offset_to_object);
static int btree_leaf_remove_object (THREAD_ENTRY * thread_p,
				     BTID_INT * btid_int,
				     BTREE_DELETE_HELPER * delete_helper,
				     PAGE_PTR leaf_page, RECDES * leaf_record,
				     LEAF_REC * leaf_rec_info,
				     int offset_after_key,
				     BTREE_SEARCH_KEY_HELPER * search_key,
				     int offset_to_object);
static int btree_key_remove_insert_mvccid (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   DB_VALUE * key,
					   PAGE_PTR * leaf_page,
					   BTREE_SEARCH_KEY_HELPER *
					   search_key, bool * restart,
					   void *other_args);
static int btree_key_remove_delete_mvccid (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   DB_VALUE * key,
					   PAGE_PTR * leaf_page,
					   BTREE_SEARCH_KEY_HELPER *
					   search_key, bool * restart,
					   void *other_args);
static int btree_undo_mvcc_delete (THREAD_ENTRY * thread_p, BTID * btid,
				   OR_BUF * buffered_key, OID * oid,
				   OID * class_oid, MVCCID delete_mvccid);
static int btree_undo_insert_object (THREAD_ENTRY * thread_p, BTID * btid,
				     OR_BUF * buffered_key, OID * oid,
				     OID * class_oid, MVCCID insert_mvccid);
static int btree_key_remove_delete_mvccid_unique (THREAD_ENTRY * thread_p,
						  BTID_INT * btid_int,
						  BTREE_DELETE_HELPER *
						  delete_helper,
						  BTREE_SEARCH_KEY_HELPER *
						  search_key,
						  PAGE_PTR leaf_page,
						  RECDES * leaf_record,
						  PAGE_PTR overflow_page,
						  RECDES * overflow_record,
						  BTREE_NODE_TYPE node_type,
						  int offset_to_object);
static int btree_key_remove_delete_mvccid_non_unique (THREAD_ENTRY * thread_p,
						      BTID_INT * btid_int,
						      BTREE_DELETE_HELPER *
						      delete_helper,
						      PAGE_PTR page,
						      RECDES * record,
						      PGSLOTID slotid,
						      BTREE_NODE_TYPE
						      node_type,
						      int offset_to_object);
static int btree_overflow_record_replace_object (THREAD_ENTRY * thread_p,
						 BTID_INT * btid_int,
						 BTREE_DELETE_HELPER *
						 delete_helper,
						 PAGE_PTR overflow_page,
						 RECDES * overflow_record,
						 int
						 offset_to_replaced_object,
						 BTREE_OBJECT_INFO *
						 replacing_object);

/*
 * btree_fix_root_with_info () - Fix b-tree root page and output its VPID,
 *				 header and b-tree info if requested.
 *
 * return	       : Root page pointer or NULL if fix failed.
 * thread_p (in)       : Thread entry.
 * btid (in)	       : B-tree identifier.
 * latch_mode (in)     : Page latch mode.
 * root_vpid (out)     : Output VPID of root page if not NULL.
 * root_header_p (out) : Output root header if not NULL.
 * btid_int_p (out)    : Output b-tree data if not NULL.
 */
STATIC_INLINE PAGE_PTR
btree_fix_root_with_info (THREAD_ENTRY * thread_p, BTID * btid,
			  PGBUF_LATCH_MODE latch_mode, VPID * root_vpid_p,
			  BTREE_ROOT_HEADER ** root_header_p,
			  BTID_INT * btid_int_p)
{
  PAGE_PTR root_page = NULL;
  VPID vpid;
  BTREE_ROOT_HEADER *root_header = NULL;

  /* Assert expected arguments. */
  assert (btid != NULL);

  /* Get root page VPID. */
  if (root_vpid_p == NULL)
    {
      root_vpid_p = &vpid;
    }
  root_vpid_p->pageid = btid->root_pageid;
  root_vpid_p->volid = btid->vfid.volid;

  /* Fix root page. */
  root_page =
    pgbuf_fix (thread_p, root_vpid_p, OLD_PAGE, latch_mode,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (root_page == NULL)
    {
      /* Failed fixing root page. */
      ASSERT_ERROR ();
      return NULL;
    }

  /* Get root header */
  root_header = btree_get_root_header (root_page);
  if (root_header == NULL)
    {
      /* Error getting root header. */
      assert (false);
      pgbuf_unfix (thread_p, root_page);
      return NULL;
    }
  if (root_header_p != NULL)
    {
      /* Output root header. */
      *root_header_p = root_header;
    }

  if (btid_int_p != NULL)
    {
      /* Get b-tree info. */
      btid_int_p->sys_btid = btid;
      if (btree_glean_root_header_info (thread_p, root_header, btid_int_p)
	  != NO_ERROR)
	{
	  assert (false);
	  pgbuf_unfix (thread_p, root_page);
	  return NULL;
	}
    }

  /* Return fixed root page. */
  return root_page;
}

/*
 * btree_is_fence_key () - Return whether the key is fence or not.
 *
 * return	  : True if key is fence.
 * leaf_page (in) : Leaf node page.
 * slotid (in)	  : Key slot ID.
 */
STATIC_INLINE bool
btree_is_fence_key (PAGE_PTR leaf_page, PGSLOTID slotid)
{
  SPAGE_SLOT *slotp = NULL;
  OID first_oid;

  assert (leaf_page != NULL);

  /* Get slot. */
  slotp = spage_get_slot (leaf_page, slotid);
  if (slotp == NULL)
    {
      /* Unexpected error. */
      assert (false);
      return false;
    }
  assert (slotp->offset_to_record > 0
	  && slotp->offset_to_record < (unsigned int) DB_PAGESIZE);
  assert (slotp->record_type == REC_HOME);
  assert (slotp->record_length >= OR_OID_SIZE);

  (void) or_unpack_oid (leaf_page + slotp->offset_to_record, &first_oid);

  /* Return if fence record flag is set. */
  return BTREE_OID_IS_RECORD_FLAG_SET (&first_oid, BTREE_LEAF_RECORD_FENCE);
}

#if !defined(NDEBUG)
/*
 * btree_get_node_level () -
 *
 *   return:
 *   page_ptr(in):
 *
 */
static int
btree_get_node_level (PAGE_PTR page_ptr)
{
  BTREE_NODE_HEADER *header = NULL;

  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      return -1;
    }

  assert (header->node_level > 0);

  return header->node_level;
}
#endif

/*
 * btree_clear_key_value () -
 *   return: cleared flag
 *   clear_flag (in/out):
 *   key_value (in/out):
 */
bool
btree_clear_key_value (bool * clear_flag, DB_VALUE * key_value)
{
  if (*clear_flag == true)
    {
      pr_clear_value (key_value);
      *clear_flag = false;
    }

  return *clear_flag;
}

/*
 * btree_create_overflow_key_file () -
 *   return: NO_ERROR
 *   btid(in):
 *
 * Note: An overflow key file is created (permanently) and the VFID
 * is written to the root header for the btree.
 */
int
btree_create_overflow_key_file (THREAD_ENTRY * thread_p, BTID_INT * btid)
{
  FILE_OVF_BTREE_DES btdes_ovf;

  /*
   * Create the overflow file. Try to create the overflow file in the
   * same volume where the btree was defined
   */

  btid->ovfid.volid = btid->sys_btid->vfid.volid;

  /* Initialize description of overflow heap file */
  btdes_ovf.btid = *btid->sys_btid;	/* structure copy */

  if (file_create (thread_p, &btid->ovfid, 3, FILE_BTREE_OVERFLOW_KEY,
		   &btdes_ovf, NULL, 0) == NULL)
    {
      VFID_SET_NULL (&btid->ovfid);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * btree_store_overflow_key () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   key(in): Pointer to the overflow key memory area
 *   size(in): Overflow key memory area size
 *   node_type(in): Type of node
 *   first_overflow_page_vpid(out): Set to the first overflow key page identifier
 *
 * Note: The overflow key given is stored in a chain of pages.
 */
static int
btree_store_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  DB_VALUE * key, int size, BTREE_NODE_TYPE node_type,
			  VPID * first_overflow_page_vpid)
{
  RECDES rec;
  OR_BUF buf;
  VFID overflow_file_vfid;
  int ret = NO_ERROR;
  TP_DOMAIN *tp_domain;
  PR_TYPE *pr_type;
  DB_TYPE src_type, dst_type;
  DB_VALUE new_key;
  DB_VALUE *key_ptr = key;

  assert (!VFID_ISNULL (&btid->ovfid));

  if (node_type == BTREE_LEAF_NODE)
    {
      tp_domain = btid->key_type;
    }
  else
    {
      tp_domain = btid->nonleaf_key_type;
    }

  pr_type = tp_domain->type;

  src_type = DB_VALUE_DOMAIN_TYPE (key);
  dst_type = pr_type->id;

  if (src_type != dst_type)
    {
      TP_DOMAIN_STATUS status;

      assert (pr_is_string_type (src_type));
      assert (pr_is_string_type (dst_type));

      key_ptr = &new_key;
      status = tp_value_cast (key, key_ptr, tp_domain, false);
      if (status != DOMAIN_COMPATIBLE)
	{
	  assert (false);
	  goto exit_on_error;
	}

      size = btree_get_disk_size_of_key (key_ptr);
    }

  overflow_file_vfid = btid->ovfid;	/* structure copy */

  rec.area_size = size;
  rec.data = (char *) db_private_alloc (thread_p, size);
  if (rec.data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (size_t) size);
      goto exit_on_error;
    }

  or_init (&buf, rec.data, rec.area_size);

  if ((*(pr_type->index_writeval)) (&buf, key_ptr) != NO_ERROR)
    {
      goto exit_on_error;
    }

  rec.length = (int) (buf.ptr - buf.buffer);

  /* don't need undo log because undo log of btree insert/delete is logical log */
  if (overflow_insert_without_undo_logging (thread_p, &overflow_file_vfid,
					    first_overflow_page_vpid,
					    &rec, NULL) == NULL)
    {
      goto exit_on_error;
    }

  if (rec.data)
    {
      db_private_free_and_init (thread_p, rec.data);
    }

  if (key_ptr != key)
    {
      db_value_clear (key_ptr);
    }

  return ret;

exit_on_error:

  if (rec.data)
    {
      db_private_free_and_init (thread_p, rec.data);
    }

  if (key_ptr != key)
    {
      db_value_clear (key_ptr);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_load_overflow_key () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   first_overflow_page_vpid(in): Overflow key first page identifier
 *   key(out): Set to the overflow key memory area
 *
 * Note: The overflow key is loaded from the pages.
 */
static int
btree_load_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
			 VPID * first_overflow_page_vpid, DB_VALUE * key,
			 BTREE_NODE_TYPE node_type)
{
  RECDES rec;
  OR_BUF buf;
  PR_TYPE *pr_type;
  int ret = NO_ERROR;

  if (node_type == BTREE_LEAF_NODE)
    {
      pr_type = btid->key_type->type;
    }
  else
    {
      pr_type = btid->nonleaf_key_type->type;
    }

  rec.area_size = overflow_get_length (thread_p, first_overflow_page_vpid);
  if (rec.area_size == -1)
    {
      return ER_FAILED;
    }

  rec.data = (char *) db_private_alloc (thread_p, rec.area_size);
  if (rec.data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (size_t) rec.area_size);
      goto exit_on_error;
    }

  if (overflow_get (thread_p, first_overflow_page_vpid, &rec, NULL) !=
      S_SUCCESS)
    {
      goto exit_on_error;
    }

  or_init (&buf, rec.data, rec.length);

  /* we always copy overflow keys */
  if ((*(pr_type->index_readval)) (&buf, key, btid->key_type, -1, true,
				   NULL, 0) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (rec.data)
    {
      db_private_free_and_init (thread_p, rec.data);
    }

  return NO_ERROR;

exit_on_error:

  if (rec.data)
    {
      db_private_free_and_init (thread_p, rec.data);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_delete_overflow_key () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   page_ptr(in): Page that contains the overflow key
 *   slot_id(in): Slot that contains the overflow key
 *   node_type(in): Leaf or NonLeaf page
 *
 * Note: The overflow key is deleted. This routine will not delete the
 * btree slot containing the key.
 */
static int
btree_delete_overflow_key (THREAD_ENTRY * thread_p, BTID_INT * btid,
			   PAGE_PTR page_ptr, INT16 slot_id,
			   BTREE_NODE_TYPE node_type)
{
  RECDES rec;
  VPID page_vpid;
  char *start_ptr;
  OR_BUF buf;
  int rc = NO_ERROR;

  assert (slot_id > 0);

  rec.area_size = -1;

  /* first read the record to get first page identifier */
  if (spage_get_record (page_ptr, slot_id, &rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  /* get first page identifier */
  if (node_type == BTREE_LEAF_NODE)
    {
      int mvccids_size = 0;
      assert (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_OVERFLOW_KEY));

      if (btree_record_object_is_flagged (rec.data, BTREE_OID_HAS_MVCC_INSID))
	{
	  mvccids_size += OR_MVCCID_SIZE;
	}

      if (btree_record_object_is_flagged (rec.data, BTREE_OID_HAS_MVCC_DELID))
	{
	  mvccids_size += OR_MVCCID_SIZE;
	}

      if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_CLASS_OID))
	{
	  start_ptr = rec.data + (2 * OR_OID_SIZE) + mvccids_size;
	}
      else
	{
	  start_ptr = rec.data + OR_OID_SIZE + mvccids_size;
	}
    }
  else
    {
      start_ptr = rec.data + NON_LEAF_RECORD_SIZE;
    }

  or_init (&buf, start_ptr, DISK_VPID_SIZE);

  page_vpid.pageid = or_get_int (&buf, &rc);
  if (rc == NO_ERROR)
    {
      page_vpid.volid = or_get_short (&buf, &rc);
    }
  if (rc != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (overflow_delete (thread_p, &(btid->ovfid), &page_vpid) == NULL)
    {
      goto exit_on_error;
    }

  return NO_ERROR;

exit_on_error:

  return (rc == NO_ERROR && (rc = er_errid ()) == NO_ERROR) ? ER_FAILED : rc;
}

/*
 * Common utility routines
 */



/*
 * btree_leaf_get_vpid_for_overflow_oids () -
 *   return: error code or NO_ERROR
 *   rec(in):
 *   ovfl_vpid(out):
 */
static int
btree_leaf_get_vpid_for_overflow_oids (RECDES * rec, VPID * ovfl_vpid)
{
  OR_BUF buf;
  int rc = NO_ERROR;

  assert (btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_OVERFLOW_OIDS));

  or_init (&buf, rec->data + rec->length - DISK_VPID_ALIGNED_SIZE,
	   DISK_VPID_SIZE);

  ovfl_vpid->pageid = or_get_int (&buf, &rc);
  if (rc == NO_ERROR)
    {
      ovfl_vpid->volid = or_get_short (&buf, &rc);
    }

  return rc;
}

/*
 * btree_leaf_record_change_overflow_link () - Modify the link of leaf record.
 *
 * return		  : Void.
 * thread_p (in)	  : Thread entry.
 * btid_int (in)	  : B-tree info.
 * leaf_record (in)	  : Leaf record.
 * new_overflow_vpid (in) : New overflow link.
 * rv_redo_data_ptr (in)  : If not null, outputs redo recovery data for
 *			    changes made.
 */
void
btree_leaf_record_change_overflow_link (THREAD_ENTRY * thread_p,
					BTID_INT * btid_int,
					RECDES * leaf_record,
					VPID * new_overflow_vpid,
					char **rv_redo_data_ptr)
{
  char *ovf_link_ptr = NULL;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (leaf_record != NULL);
  assert (new_overflow_vpid != NULL);
  assert (rv_redo_data_ptr == NULL || *rv_redo_data_ptr != NULL);

  if (btree_leaf_is_flaged (leaf_record, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
    {
      /* Leaf record already had overflow link. */
      if (VPID_ISNULL (new_overflow_vpid))
	{
	  /* Remove overflow VPID. */
	  leaf_record->length -= DISK_VPID_ALIGNED_SIZE;

	  /* Both insert and delete MVCCID's should exist. */
	  assert (btree_record_object_is_flagged
		  (leaf_record->data, BTREE_OID_HAS_MVCC_INSID_AND_DELID));
	  /* TODO: Object is no longer fixed size and we may be able to
	   *       remove unnecessary info.
	   */

	  /* Clear BTREE_LEAF_RECORD_OVERFLOW_OIDS flag. */
	  btree_leaf_clear_flag (leaf_record,
				 BTREE_LEAF_RECORD_OVERFLOW_OIDS);

	  /* Redo logging. */
	  if (rv_redo_data_ptr != NULL)
	    {
	      /* Log link removal. */
	      *rv_redo_data_ptr =
		log_rv_pack_redo_record_changes (*rv_redo_data_ptr,
						 leaf_record->length,
						 DISK_VPID_ALIGNED_SIZE,
						 0, NULL /* just remove. */ );

	      /* Log clear flag. */
	      *rv_redo_data_ptr =
		log_rv_pack_redo_record_changes (*rv_redo_data_ptr,
						 OR_OID_SLOTID, OR_SHORT_SIZE,
						 OR_SHORT_SIZE,
						 leaf_record->data
						 + OR_OID_SLOTID);
	    }
	}
      else
	{
	  /* Update existing link. */
	  ovf_link_ptr =
	    leaf_record->data + leaf_record->length - DISK_VPID_ALIGNED_SIZE;
	  OR_PUT_VPID_ALIGNED (ovf_link_ptr, new_overflow_vpid);

	  /* Redo logging. */
	  if (rv_redo_data_ptr != NULL)
	    {
	      *rv_redo_data_ptr =
		log_rv_pack_redo_record_changes (*rv_redo_data_ptr,
						 leaf_record->length
						 - DISK_VPID_ALIGNED_SIZE,
						 DISK_VPID_ALIGNED_SIZE,
						 DISK_VPID_ALIGNED_SIZE,
						 ovf_link_ptr);
	    }
	}
    }
  else
    {
      /* Leaf record didn't have overflow link. */
      assert (!VPID_ISNULL (new_overflow_vpid));

      /* Add overflow VPID. */
      ovf_link_ptr = leaf_record->data + leaf_record->length;
      OR_PUT_VPID_ALIGNED (ovf_link_ptr, new_overflow_vpid);
      /* Update record length. */
      leaf_record->length += DISK_VPID_ALIGNED_SIZE;

      /* Redo logging for added link. */
      if (rv_redo_data_ptr != NULL)
	{
	  *rv_redo_data_ptr =
	    log_rv_pack_redo_record_changes (*rv_redo_data_ptr,
					     leaf_record->length
					     - DISK_VPID_ALIGNED_SIZE,
					     0, DISK_VPID_ALIGNED_SIZE,
					     ovf_link_ptr);
	}

      /* First object must be fixed size to provide enough space if objects
       * are swapped from overflow.
       */
      btree_leaf_record_handle_first_overflow (leaf_record, btid_int,
					       rv_redo_data_ptr);
    }

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, leaf_record,
				   BTREE_LEAF_NODE, NULL);
#endif /* !NDEBUG */
}

/*
 * btree_leaf_get_first_object () - Get first object of leaf record.
 *
 * return	   : Error code.
 * btid (in)	   : B-tree info.
 * recp (in)	   : Leaf record.
 * oidp (out)	   : First object OID.
 * class_oid (out) : First object class OID.
 * mvcc_info (out) : First object MVCC info.
 */
int
btree_leaf_get_first_object (BTID_INT * btid, RECDES * recp, OID * oidp,
			     OID * class_oid, BTREE_MVCC_INFO * mvcc_info)
{
  OR_BUF record_buffer;
  int error_code = NO_ERROR;
  int dummy_offset = 0;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (oidp != NULL);
  /* TODO: consider class_oid and mvcc_info. Should they be required? */

  BTREE_RECORD_OR_BUF_INIT (record_buffer, recp);
  error_code =
    btree_or_get_object (&record_buffer, btid, BTREE_LEAF_NODE, dummy_offset,
			 oidp, class_oid, mvcc_info);
  /* We expect first object can be successfully obtained. */
  assert (error_code == NO_ERROR);
  return error_code;
}

/*
 * btree_get_num_visible_oids_from_all_ovf () - Get the number of visible
 *					        objects according to snapshot
 *						in all overflow pages.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * btid (in)		  : B-tree info.
 * first_ovfl_vpid (in)   : VPID of first overflow page.
 * num_visible_oids (out) : The number of visible objects.
 * max_visible_oids (in)  : Maximum allowed number of visible objects.
 * mvcc_snapshot (in)	  : Snapshot used for visibility check.
 */
static int
btree_get_num_visible_oids_from_all_ovf (THREAD_ENTRY * thread_p,
					 BTID_INT * btid,
					 VPID * first_ovfl_vpid,
					 int *num_visible_oids,
					 int *max_visible_oids,
					 MVCC_SNAPSHOT * mvcc_snapshot)
{
  RECDES ovfl_copy_rec;
  char ovfl_copy_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  VPID next_ovfl_vpid;
  PAGE_PTR ovfl_page = NULL;
  int ret;
  int num_node_visible_oids = 0;
  int max_page_visible_oids = 0, *p_max_page_visible_oids = NULL;

  if (max_visible_oids != NULL)
    {
      max_page_visible_oids = *max_visible_oids;
      p_max_page_visible_oids = &max_page_visible_oids;
    }
  /* not found in leaf page - search in overflow page */
  ovfl_page = NULL;

  ovfl_copy_rec.area_size = DB_PAGESIZE;
  ovfl_copy_rec.data = PTR_ALIGN (ovfl_copy_rec_buf, BTREE_MAX_ALIGN);

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (num_visible_oids != NULL);
  assert (first_ovfl_vpid != NULL);

  *num_visible_oids = 0;
  ovfl_page = NULL;
  next_ovfl_vpid = *first_ovfl_vpid;
  /* search for OID into overflow page */
  while (!VPID_ISNULL (&next_ovfl_vpid))
    {
      ovfl_page = pgbuf_fix (thread_p, &next_ovfl_vpid, OLD_PAGE,
			     PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (ovfl_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (ret);
	  goto error;
	}

      (void) pgbuf_check_page_ptype (thread_p, ovfl_page, PAGE_BTREE);

      if (spage_get_record (ovfl_page, 1, &ovfl_copy_rec, COPY) != S_SUCCESS)
	{
	  goto error;
	}
      assert (ovfl_copy_rec.length % 4 == 0);

      num_node_visible_oids =
	btree_record_get_num_visible_oids (thread_p, btid, &ovfl_copy_rec, 0,
					   BTREE_OVERFLOW_NODE,
					   p_max_page_visible_oids,
					   mvcc_snapshot);
      if (num_node_visible_oids < 0)
	{
	  goto error;
	}
      (*num_visible_oids) += num_node_visible_oids;

      if (max_visible_oids)
	{
	  if ((*num_visible_oids) >= (*max_visible_oids))
	    {
	      pgbuf_unfix_and_init (thread_p, ovfl_page);
	      return NO_ERROR;
	    }

	  /* update remaining visible oids to search for */
	  max_page_visible_oids -= num_node_visible_oids;
	}

      btree_get_next_overflow_vpid (ovfl_page, &next_ovfl_vpid);
      pgbuf_unfix_and_init (thread_p, ovfl_page);
    }

  return NO_ERROR;

error:

  if (ovfl_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, ovfl_page);
    }

  ret = er_errid ();
  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
}

/*
 * btree_get_num_visible_from_leaf_and_ovf () - Get the number of visible
 *						objects in record.
 *
 * return		 : Number of visible objects or negative value for
 *			   errors.
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree info.
 * leaf_record (in)	 : Leaf record descriptor.
 * offset_after_key (in) : Offset to where packed key is ended.
 * leaf_info (in)	 : Leaf record information (VPID of first overflow).
 * max_visible_oids (in) : Non-null value if there is limit of objects to
 *			   count. If limit is reached, counting is stopped and
 *			   current count is returned.
 * mvcc_snapshot (in)	 : Snapshot for visibility test.
 */
static int
btree_get_num_visible_from_leaf_and_ovf (THREAD_ENTRY * thread_p,
					 BTID_INT * btid_int,
					 RECDES * leaf_record,
					 int offset_after_key,
					 LEAF_REC * leaf_info,
					 int *max_visible_oids,
					 MVCC_SNAPSHOT * mvcc_snapshot)
{
  int num_visible = 0;		/* Count visible objects in record. */
  int error_code = 0;		/* Error code. */
  int num_ovf_visible = 0;	/* Overflow pages visible objects count. */

  /* Get number of visible objects from leaf record. */
  num_visible =
    btree_record_get_num_visible_oids (thread_p, btid_int, leaf_record,
				       offset_after_key, BTREE_LEAF_NODE,
				       max_visible_oids, mvcc_snapshot);
  if (num_visible < 0)
    {
      /* Error occurred */
      ASSERT_ERROR ();
      return num_visible;
    }
  if (max_visible_oids != NULL)
    {
      (*max_visible_oids) -= num_visible;
      if (*max_visible_oids <= 0)
	{
	  /* The maximum count of visible objects has been reached. Stop now.
	   */
	  return num_visible;
	}
    }

  /* Get number of visible objects from overflow. */
  if (!VPID_ISNULL (&leaf_info->ovfl))
    {
      error_code =
	btree_get_num_visible_oids_from_all_ovf (thread_p, btid_int,
						 &leaf_info->ovfl,
						 &num_ovf_visible,
						 max_visible_oids,
						 mvcc_snapshot);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      /* Safe guard. */
      assert (num_ovf_visible >= 0);
    }

  /* Return result */
  return (num_visible + num_ovf_visible);
}

/*
 * btree_record_get_num_visible_oids () - get number of visible OIDS
 *   return: number of visible OIDs
 *   thread_p(in): thread entry
 *   btid(in): B+tree index identifier
 *   rec(in): record descriptor
 *   oid_offset(in):  OID offset
 *   node_type(in): node type
 *   max_visible_oids(in): max visible oids to search for
 *   mvcc_snapshot(in): MVCC snapshot
 */
static int
btree_record_get_num_visible_oids (THREAD_ENTRY * thread_p, BTID_INT * btid,
				   RECDES * rec, int oid_offset,
				   BTREE_NODE_TYPE node_type,
				   int *max_visible_oids,
				   MVCC_SNAPSHOT * mvcc_snapshot)
{
  int mvcc_flags = 0, rec_oid_cnt = 0, length = 0;
  bool have_mvcc_fixed_size = false;
  MVCC_REC_HEADER mvcc_rec_header;
  BTREE_MVCC_INFO mvcc_info;
  OR_BUF buf;
  bool is_first = true;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (rec != NULL);
  assert (oid_offset >= 0);
  assert (node_type != BTREE_NON_LEAF_NODE);

  if (mvcc_snapshot == NULL)
    {
      return -1;
    }
  length = rec->length;
  if (btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
    {
      length -= DB_ALIGN (DISK_VPID_SIZE, INT_ALIGNMENT);
    }

  or_init (&buf, rec->data, length);
  while (buf.ptr < buf.endptr)
    {
      /* Get MVCC flags */
      mvcc_flags = btree_record_object_get_mvcc_flags (buf.ptr);

      /* Skip object OID */
      if (or_advance (&buf, OR_OID_SIZE) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (BTREE_IS_UNIQUE (btid->unique_pk)
	  && (node_type == BTREE_OVERFLOW_NODE || !is_first
	      || btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_CLASS_OID)))
	{
	  /* Skip class OID */
	  if (or_advance (&buf, OR_OID_SIZE) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      /* Get MVCC information */
      if (btree_or_get_mvccinfo (&buf, &mvcc_info, mvcc_flags) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      /* TODO */
      /* Isn't it better to create snapshot function for BTREE_MVCC_INFO? */
      btree_mvcc_info_to_heap_mvcc_header (&mvcc_info, &mvcc_rec_header);

      /* Check snapshot */
      if ((mvcc_snapshot)->
	  snapshot_fnc (thread_p, &mvcc_rec_header, mvcc_snapshot))
	{
	  /* Satisfies snapshot so counter must be incremented */
	  rec_oid_cnt++;
	}

      if (max_visible_oids != NULL)
	{
	  if (rec_oid_cnt >= *max_visible_oids)
	    {
	      return rec_oid_cnt;
	    }
	}

      if (node_type == BTREE_LEAF_NODE && is_first)
	{
	  /* Must skip over the key value to the next object */
	  or_seek (&buf, oid_offset);
	}
      is_first = false;
    }

  return rec_oid_cnt;
}

/*
 * btree_record_get_num_oids () - Compute the total number of objects in a
 *				  leaf or overflow record.
 *
 * return		 : Number of objects or error code.
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree info.
 * rec (in)		 : Record descriptor.
 * after_key_offset (in) : Offset where packed key ends. Is used only in case
 *			   of leaf records.
 * node_type (in)	 : Leaf/overflow node type.
 */
static int
btree_record_get_num_oids (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			   RECDES * rec, int after_key_offset,
			   BTREE_NODE_TYPE node_type)
{
  int rec_oid_cnt, vpid_size = 0;
  short mvcc_flag;
  OR_BUF buf;
  int fixed_object_size;

  assert (rec != NULL && after_key_offset >= 0
	  && node_type != BTREE_NON_LEAF_NODE);

  if (node_type == BTREE_LEAF_NODE)
    {
      /* Leaf record. */

      /* There is one object before key. */
      rec_oid_cnt = 1;

      BTREE_RECORD_OR_BUF_INIT (buf, rec);
      or_seek (&buf, after_key_offset);
      /* Pointing after key. */

      if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	{
	  /* All remaining objects are fixed size. It is enough to divide
	   * remaining record size by object size.
	   */
	  fixed_object_size = BTREE_OBJECT_FIXED_SIZE (btid_int);
	  /* Safe guard */
	  assert ((CAST_BUFLEN (buf.endptr - buf.ptr) % fixed_object_size)
		  == 0);
	  rec_oid_cnt +=
	    CEIL_PTVDIV (buf.endptr - buf.ptr, fixed_object_size);
	  return rec_oid_cnt;
	}
      /* Not unique. Objects have variable size. Count them manually. */

      /* Count oids */
      while (buf.ptr < buf.endptr)
	{
	  mvcc_flag = btree_record_object_get_mvcc_flags (buf.ptr);
	  buf.ptr +=
	    OR_OID_SIZE + BTREE_GET_MVCC_INFO_SIZE_FROM_FLAGS (mvcc_flag);
	  rec_oid_cnt++;
	}
      assert (buf.ptr == buf.endptr);
      return rec_oid_cnt;
    }

  /* Overflow. Object have fixed size. */
  fixed_object_size = BTREE_OBJECT_FIXED_SIZE (btid_int);
  assert (rec->length % fixed_object_size == 0);
  rec_oid_cnt = CEIL_PTVDIV (rec->length, fixed_object_size);
  return rec_oid_cnt;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * btree_leaf_put_first_oid () -
 *   return: NO_ERROR
 *   recp(in/out):
 *   oidp(in):
 *   record_flag(in):
 */
static int
btree_leaf_put_first_oid (RECDES * recp, OID * oidp, short record_flag)
{
  OR_BUF buf;
  int rc;

  assert ((short) (record_flag & ~BTREE_LEAF_RECORD_MASK) == 0);

  or_init (&buf, recp->data, OR_OID_SIZE);

  rc = or_put_int (&buf, oidp->pageid);
  assert (rc == NO_ERROR);
  rc = or_put_short (&buf, oidp->slotid | record_flag);
  assert (rc == NO_ERROR);
  rc = or_put_short (&buf, oidp->volid);
  assert (rc == NO_ERROR);

  return NO_ERROR;
}
#endif

/*
 * btree_leaf_change_first_object () - Replace first object in record with
 *				       given object.
 *
 * return		  : Void
 * recp (in/out)	  : B-tree leaf record.
 * btid (in)		  : B-tree info.
 * oidp (in)		  : Replacing instance OID.
 * class_oidp (in)	  : Replacing class OID.
 * mvcc_info (in)	  : Replacing MVCC info.
 * key_offset (in)	  : Output new offset to key.
 * rv_redo_data_ptr (out) : If not NULL, output redo logging of this change.
 */
void
btree_leaf_change_first_object (RECDES * recp, BTID_INT * btid, OID * oidp,
				OID * class_oidp, BTREE_MVCC_INFO * mvcc_info,
				int *key_offset, char **rv_redo_data_ptr)
{
  short old_rec_flag = 0, new_rec_flag = 0, mvcc_flags = 0;
  char *src = NULL, *desc = NULL;
  int old_object_size, new_object_size;
  bool new_has_insid = false, new_has_delid = false;
  bool new_has_class_oid = false;
  OR_BUF buffer;
  BTREE_MVCC_INFO local_mvcc_info;

  /* Get old record flags */
  old_rec_flag = btree_leaf_get_flag (recp);
  /* Initialize new record flags same as old record flags */
  new_rec_flag = old_rec_flag;

  /* Get the size of old object */
  if (BTREE_IS_UNIQUE (btid->unique_pk)
      && btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_CLASS_OID))
    {
      /* Also class OID is saved */
      old_object_size = 2 * OR_OID_SIZE;
    }
  else
    {
      old_object_size = OR_OID_SIZE;
    }

  /* Compute the required size for the new object */
  if (BTREE_IS_UNIQUE (btid->unique_pk)
      && !OID_EQ (&btid->topclass_oid, class_oidp))
    {
      /* Also class OID is saved */
      new_object_size = 2 * OR_OID_SIZE;
      /* Add BTREE_LEAF_RECORD_CLASS_OID flag */
      new_rec_flag |= BTREE_LEAF_RECORD_CLASS_OID;
      new_has_class_oid = true;
    }
  else
    {
      new_object_size = OR_OID_SIZE;
      /* Clear BTREE_LEAF_RECORD_CLASS_OID flag */
      new_rec_flag &= ~BTREE_LEAF_RECORD_CLASS_OID;
    }

  /* insert/delete MVCCID's may be present or may be added */
  if (mvcc_info == NULL)
    {
      /* Use empty MVCC info */
      mvcc_info = &local_mvcc_info;
      mvcc_info->flags = 0;
    }

  if (old_rec_flag & BTREE_LEAF_RECORD_OVERFLOW_OIDS)
    {
      /* First object must have fixed size */
      old_object_size += 2 * OR_MVCCID_SIZE;
      new_object_size += 2 * OR_MVCCID_SIZE;
      new_has_insid = true;
      new_has_delid = true;
      BTREE_MVCC_INFO_SET_FIXED_SIZE (mvcc_info);
      if (BTREE_IS_UNIQUE (btid->unique_pk) && !new_has_class_oid)
	{
	  /* Fixed size is required, so force adding class OID also */
	  new_rec_flag |= BTREE_LEAF_RECORD_CLASS_OID;
	  new_object_size += OR_OID_SIZE;
	  new_has_class_oid = true;
	}
      new_rec_flag |= BTREE_LEAF_RECORD_OVERFLOW_OIDS;
    }
  else
    {
      /* Check new MVCCID's */
      if (BTREE_MVCC_INFO_HAS_INSID (mvcc_info))
	{
	  new_object_size += OR_MVCCID_SIZE;
	  new_has_insid = true;
	}
      if (BTREE_MVCC_INFO_HAS_DELID (mvcc_info))
	{
	  new_object_size += OR_MVCCID_SIZE;
	  new_has_delid = true;
	}
      /* Check old MVCCID's */
      if (btree_record_object_is_flagged (recp->data,
					  BTREE_OID_HAS_MVCC_INSID))
	{
	  old_object_size += OR_MVCCID_SIZE;
	}
      if (btree_record_object_is_flagged (recp->data,
					  BTREE_OID_HAS_MVCC_DELID))
	{
	  old_object_size += OR_MVCCID_SIZE;
	}
    }

  /* Key and any other OID's may need to be moved */
  RECORD_MOVE_DATA (recp, new_object_size, old_object_size);
  if (key_offset)
    {
      *key_offset = (new_object_size - old_object_size);
    }

  /* Add new data */
  or_init (&buffer, recp->data, new_object_size);
  /* Object OID first */
  if (or_put_oid (&buffer, oidp) != NO_ERROR)
    {
      assert_release (false);
      return;
    }
  /* Set record flags */
  btree_leaf_set_flag (recp, new_rec_flag);

  if (new_has_class_oid)
    {
      /* Add class OID */
      if (or_put_oid (&buffer, class_oidp) != NO_ERROR)
	{
	  assert_release (false);
	  return;
	}
    }


  /* Add MVCC info */
  if (new_has_insid)
    {
      assert (MVCCID_IS_VALID (mvcc_info->insert_mvccid)
	      && mvcc_id_precedes (mvcc_info->insert_mvccid,
				   log_Gl.hdr.mvcc_next_id));
      /* Add insert MVCCID */
      if (or_put_mvccid (&buffer, mvcc_info->insert_mvccid) != NO_ERROR)
	{
	  assert_release (false);
	  return;
	}
      mvcc_flags |= BTREE_OID_HAS_MVCC_INSID;
    }
  if (new_has_delid)
    {
      assert (mvcc_info->delete_mvccid == MVCCID_NULL
	      || mvcc_id_precedes (mvcc_info->delete_mvccid,
				   log_Gl.hdr.mvcc_next_id));
      /* Add delete MVCCID */
      if (or_put_mvccid (&buffer, mvcc_info->delete_mvccid) != NO_ERROR)
	{
	  assert_release (false);
	}
      mvcc_flags |= BTREE_OID_HAS_MVCC_DELID;
    }
  if (mvcc_flags != 0)
    {
      /* Set MVCC flags */
      btree_record_object_set_mvcc_flags (recp->data, mvcc_flags);
    }

  /* Make sure everything was packed correctly */
  assert_release (buffer.ptr == buffer.endptr);

  /* If MVCC is enabled and if b-tree is unique and if the record has overflow
   * OID's, the first oid must have fixed size and should also contain class
   * OID (marked as BTREE_LEAF_RECORD_CLASS_OID).
   */
  assert (!BTREE_IS_UNIQUE (btid->unique_pk)
	  || !btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_OVERFLOW_OIDS)
	  || btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_CLASS_OID));

#if !defined (NDEBUG)
  (void) btree_check_valid_record (NULL, btid, recp, BTREE_LEAF_NODE, NULL);
#endif

  /* Redo log changes of first object. */
  if (rv_redo_data_ptr != NULL)
    {
      assert (*rv_redo_data_ptr != NULL);
      *rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (*rv_redo_data_ptr, 0,
					 old_object_size, new_object_size,
					 recp->data);
    }
}

/*
 * btree_leaf_record_handle_first_overflow () - Set fixed size for first
 *						object and update record.
 *
 * return		  : Void.
 * recp (in)		  : Leaf record.
 * btid_int (in)	  : B-tree info.
 * rv_redo_data_ptr (out) : If not null, outputs redo recovery data for the
 *			    changes made to record.
 */
static void
btree_leaf_record_handle_first_overflow (RECDES * recp, BTID_INT * btid_int,
					 char **rv_redo_data_ptr)
{
  int old_mvcc_flags;
  int old_object_size = OR_OID_SIZE;
  int new_object_size = BTREE_OBJECT_FIXED_SIZE (btid_int);
  MVCCID delid, insid;
  char *ptr = NULL;

  /* Assert expected arguments. */
  assert (recp != NULL);
  assert (btid_int != NULL);
  assert (rv_redo_data_ptr == NULL || *rv_redo_data_ptr != NULL);

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      if (btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_CLASS_OID))
	{
	  old_object_size += OR_OID_SIZE;
	}
    }

  old_mvcc_flags = btree_record_object_get_mvcc_flags (recp->data);
  if (old_mvcc_flags & BTREE_OID_HAS_MVCC_INSID)
    {
      OR_GET_MVCCID (recp->data + old_object_size, &insid);
      old_object_size += OR_MVCCID_SIZE;
    }
  else
    {
      insid = MVCCID_ALL_VISIBLE;
    }

  if (old_mvcc_flags & BTREE_OID_HAS_MVCC_DELID)
    {
      OR_GET_MVCCID (recp->data + old_object_size, &delid);
      old_object_size += OR_MVCCID_SIZE;
    }
  else
    {
      delid = MVCCID_NULL;
    }

  /* Set overflow OID's flag. */
  btree_leaf_set_flag (recp, BTREE_LEAF_RECORD_OVERFLOW_OIDS);

  if (old_object_size == new_object_size)
    {
      /* All information is already here */

#if !defined (NDEBUG)
      (void) btree_check_valid_record (NULL, btid_int, recp, BTREE_LEAF_NODE,
				       NULL);
#endif

      /* Log setting flag. */
      if (rv_redo_data_ptr != NULL)
	{
	  /* Leaf record flags are kept in SLOTID field of first OID. */
	  *rv_redo_data_ptr =
	    log_rv_pack_redo_record_changes (*rv_redo_data_ptr, OR_OID_SLOTID,
					     OR_SHORT_SIZE, OR_SHORT_SIZE,
					     recp->data + OR_OID_SLOTID);
	}
      return;
    }

  /* Must free space to add extra info */
  RECORD_MOVE_DATA (recp, new_object_size, old_object_size);

  /* Set pointer after object OID. */
  ptr = recp->data + OR_OID_SIZE;

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* Class OID must exist. */
      if (!btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_CLASS_OID))
	{
	  /* Add top class OID */
	  OR_PUT_OID (recp->data + OR_OID_SIZE, &btid_int->topclass_oid);
	  btree_leaf_set_flag (recp, BTREE_LEAF_RECORD_CLASS_OID);
	}
      else
	{
	  /* Already here. */
	}
      ptr += OR_OID_SIZE;
    }

  /* Add both insert MVCCID and delete MVCCID */
  OR_PUT_MVCCID (ptr, &insid);
  ptr += OR_MVCCID_SIZE;
  OR_PUT_MVCCID (ptr, &delid);
  ptr += OR_MVCCID_SIZE;

  /* Set MVCC flags where OID is packed. */
  btree_record_object_set_mvcc_flags (recp->data,
				      BTREE_OID_HAS_MVCC_INSID_AND_DELID);

#if !defined (NDEBUG)
  (void) btree_check_valid_record (NULL, btid_int, recp, BTREE_LEAF_NODE,
				   NULL);
#endif

  /* Redo logging. */
  if (rv_redo_data_ptr != NULL)
    {
      /* Object OID could not change. Log only changes after its OID. */
      *rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (*rv_redo_data_ptr, 0,
					 old_object_size, new_object_size,
					 recp->data);
    }
}

/*
 * btree_leaf_get_nth_oid_ptr () - Advance to the nth object in b-tree key
 *				   record data and return pointer.
 *
 * return		: Pointer to nth object in record data.
 * btid (in)		: B-tree data.
 * recp (in)		: Record data.
 * node_type (in)	: Node type (leaf or overflow).
 * oid_list_offset (in) : Offset to list of objects (for leaf it must skip the
 *			  packed key).
 * n (in)		: Required object index.
 *
 * TODO: This function is no longer used due to changes in b-tree range scan.
 *	 However it may prove useful in the future, so better don't remove it.
 */
static char *
btree_leaf_get_nth_oid_ptr (BTID_INT * btid, RECDES * recp,
			    BTREE_NODE_TYPE node_type, int oid_list_offset,
			    int n)
{
  OR_BUF buf;
  int fixed_size;
  int oids_size;
  int vpid_size;
  int mvcc_info_size;
  short mvcc_flags;
  bool is_fixed_size;

  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_OVERFLOW_NODE);

  if (n == 0)
    {
      /* First object is always first in record data */
      return recp->data;
    }

  vpid_size = (btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_OVERFLOW_OIDS)
	       ? DB_ALIGN (DISK_VPID_SIZE, INT_ALIGNMENT) : 0);

  if (BTREE_IS_UNIQUE (btid->unique_pk))
    {
      oids_size = 2 * OR_OID_SIZE;
    }
  else
    {
      oids_size = OR_OID_SIZE;
    }

  is_fixed_size =
    (node_type == BTREE_OVERFLOW_NODE || BTREE_IS_UNIQUE (btid->unique_pk));
  if (is_fixed_size)
    {
      /* Each object has fixed size */
      fixed_size = BTREE_OBJECT_FIXED_SIZE (btid);

      if (node_type == BTREE_OVERFLOW_NODE)
	{
	  assert (oid_list_offset == 0);
	  assert (n * fixed_size + vpid_size < recp->length);
	  return recp->data + n * fixed_size;
	}
      else			/* node_type == BTREE_LEAF_NODE */
	{
	  assert ((oid_list_offset + (n - 1) * fixed_size + vpid_size)
		  < recp->length);
	  return recp->data + oid_list_offset + (n - 1) * fixed_size;
	}
    }
  /* Not fixed size. */
  assert (node_type == BTREE_LEAF_NODE);
  /* Count objects after key (without first). */
  n = n - 1;
  BTREE_RECORD_OR_BUF_INIT (buf, recp);

  while (n > 0)
    {
      /* Skip object */
      mvcc_flags = btree_record_object_get_mvcc_flags (buf.ptr);
      mvcc_info_size = BTREE_GET_MVCC_INFO_SIZE_FROM_FLAGS (mvcc_flags);

      if (or_advance (&buf, oids_size + mvcc_info_size) != NO_ERROR)
	{
	  assert_release (false);
	  return NULL;
	}

      n--;
    }

  /* buf.ptr points to nth object */
  assert (buf.ptr < buf.endptr);

  return buf.ptr;
}

/*
 * btree_record_get_last_object () - Get last object in record.
 *
 * return		       : Error code.
 * thread_p (in)	       : Thread entry.
 * btid_int (in)	       : B-tree info.
 * recp (in)		       : B-tree leaf/overflow record.
 * node_type (in)	       : Leaf/overflow node type.
 * offset_after_key (in)       : For leaf record, offset to where packed key is
 *				 ended.
 * oidp (in)		       : Output last object OID.
 * class_oid (in)	       : Output last object class OID.
 * mvcc_info (in)	       : Output last object MVCC info.
 * offset_to_last_object (out) : Output offset in record to last object.
 */
static int
btree_record_get_last_object (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			      RECDES * recp, BTREE_NODE_TYPE node_type,
			      int offset_after_key, OID * oidp,
			      OID * class_oid, BTREE_MVCC_INFO * mvcc_info,
			      int *offset_to_last_object)
{
  char *offset = NULL;		/* Pointer in record data. */
  OR_BUF buf;			/* Buffer used to parse record. */
  int error_code = NO_ERROR;	/* Error code. */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (recp != NULL);
  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_OVERFLOW_NODE);
  assert (oidp != NULL);
  assert (class_oid != NULL);
  assert (mvcc_info != NULL);
  assert (offset_to_last_object != NULL);

  /* Create a buffer from b-tree record. */
  BTREE_RECORD_OR_BUF_INIT (buf, recp);

  /* Is last object fixed size? True if:
   * 1. Overflow record.
   * 2. Unique leaf record has more than object.
   */
  if (node_type == BTREE_OVERFLOW_NODE
      || (BTREE_IS_UNIQUE (btid_int->unique_pk)
	  && offset_after_key != CAST_BUFLEN (buf.endptr - buf.buffer)))
    {
      /* Overflow nodes have fixed size objects. Also leaf records of unique
       * indexes have fixed size object after packed key.
       * Just compute offset to last object and get object from there.
       */
      /* Set offset to end of record (without leaf link to first overflow). */
      offset = buf.endptr;
      /* Go back on fixed object size. */
      offset -= BTREE_OBJECT_FIXED_SIZE (btid_int);

      /* Save offset to last object offset. */
      *offset_to_last_object = CAST_BUFLEN (offset - recp->data);

      /* Unpack last object. */
      offset =
	btree_unpack_object (offset, btid_int, node_type, recp,
			     offset_after_key, oidp, class_oid, mvcc_info);
      assert (offset == buf.endptr);
      return NO_ERROR;
    }
  /* Leaf node. */
  assert (node_type == BTREE_LEAF_NODE);

  if (CAST_BUFLEN (buf.endptr - buf.buffer) == offset_after_key)
    {
      /* Only one object! */
      /* Get offset to object. */
      *offset_to_last_object = 0;

      (void) btree_unpack_object (recp->data, btid_int, node_type, recp,
				  offset_after_key, oidp, class_oid,
				  mvcc_info);
      return NO_ERROR;
    }
  /* Leaf node and more than one object. */
  /* Unique should have been already handled. */
  assert (!BTREE_IS_UNIQUE (btid_int->unique_pk));

  /* Objects have variable size, depending on the stored MVCC info. We have to
   * process existing objects until reaching the end of record.
   */

  /* Start looking for last object after key. */
  or_seek (&buf, offset_after_key);

  /* Advance until record is consumed. */
  while (buf.ptr < buf.endptr)
    {
      offset = buf.ptr;
      error_code =
	btree_or_get_object (&buf, btid_int, node_type, offset_after_key,
			     oidp, class_oid, mvcc_info);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  assert_release (false);
	  return error_code;
	}
    }
  /* Safe guard: record was consumed. */
  assert (buf.ptr == buf.endptr);

  /* Output offset to last object. */
  *offset_to_last_object = CAST_BUFLEN (offset - recp->data);
  return NO_ERROR;
}

/*
 * btree_record_remove_last_object () - Remove last object from b-tree record.
 *
 * return		      : Void.
 * thread_p (in)	      : Thread entry.
 * btid (in)		      : B-tree info.
 * recp (in)		      : B-tree record.
 * node_type (in)	      : Leaf or overflow node type.
 * offset_to_last_object (in) : Offset to last object (must be known).
 * rv_redo_data_ptr (out)     : If not null, output the packed redo logging
 *				for this change.
 */
static void
btree_record_remove_last_object (THREAD_ENTRY * thread_p, BTID_INT * btid,
				 RECDES * recp, BTREE_NODE_TYPE node_type,
				 int offset_to_last_object,
				 char **rv_redo_data_ptr)
{
  char *first_ovf_vpid_src;
  char *first_ovf_vpid_dest;
  int save_length;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (recp != NULL);
  assert (offset_to_last_object > 0);
  assert (recp->length > offset_to_last_object);
  assert (rv_redo_data_ptr == NULL || *rv_redo_data_ptr != NULL);

  save_length = recp->length;
  if (node_type == BTREE_LEAF_NODE
      && btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
    {
      /* Remove last object and save first overflow VPID. */
      first_ovf_vpid_src = recp->data + recp->length - DISK_VPID_ALIGNED_SIZE;
      first_ovf_vpid_dest = recp->data + offset_to_last_object;
      assert ((first_ovf_vpid_src - first_ovf_vpid_dest)
	      >= DISK_VPID_ALIGNED_SIZE);
      VPID_COPY ((VPID *) first_ovf_vpid_dest, (VPID *) first_ovf_vpid_src);
      recp->length = offset_to_last_object + DISK_VPID_ALIGNED_SIZE;
    }
  else
    {
      /* Just cut off the last object. */
      recp->length = offset_to_last_object;
    }

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid, recp, node_type, NULL);
#endif /* !NDEBUG */

  /* Redo logging. */
  if (rv_redo_data_ptr != NULL)
    {
      /* Pack redo recovery of change: remove old_size bytes from
       * offset_to_last_object.
       */
      int old_size = save_length - recp->length;
      *rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (*rv_redo_data_ptr,
					 offset_to_last_object, old_size,
					 0, NULL /* just remove */ );
    }
}

/*
 * btree_leaf_get_flag () -
 *   return: flag of leaf record
 *   recp(in):
 */
static short
btree_leaf_get_flag (RECDES * recp)
{
  short slot_id;

  slot_id = OR_GET_SHORT (recp->data + OR_OID_SLOTID);

  return slot_id & BTREE_LEAF_RECORD_MASK;
}

/*
 * btree_record_object_get_mvcc_flags () - get MVCC flag for key oid
 *   return: MVCC flag for key oid
 *   data(in): pointer to OID into key buffer
 */
STATIC_INLINE short
btree_record_object_get_mvcc_flags (char *data)
{
  short vol_id;

  assert (data != NULL);
  vol_id = OR_GET_SHORT (data + OR_OID_VOLID);

  return vol_id & BTREE_OID_MVCC_FLAGS_MASK;
}

/*
 * btree_leaf_is_flaged () -
 *   return:
 *   recp(in):
 *   record_flag(in):
 */
static bool
btree_leaf_is_flaged (RECDES * recp, short record_flag)
{
  assert ((short) (record_flag & ~BTREE_LEAF_RECORD_MASK) == 0);

  return ((OR_GET_SHORT (recp->data + OR_OID_SLOTID) & record_flag)
	  == record_flag);
}

/*
 * btree_record_object_is_flagged () - Check whether object found at rec_data
 *				       has the MVCC flag.
 *
 * return	  : True if flag is set, false otherwise.
 * rec_data (in)  : Pointer to an object in b-tree record.
 * mvcc_flag (in) : MVCC flag.
 */
STATIC_INLINE bool
btree_record_object_is_flagged (char *rec_data, short mvcc_flag)
{
  assert ((short) (mvcc_flag & ~BTREE_OID_MVCC_FLAGS_MASK) == 0);

  return ((OR_GET_SHORT (rec_data + OR_OID_VOLID) & mvcc_flag) == mvcc_flag);
}

/*
 * btree_leaf_set_flag () -
 *   return:
 *   recp(in/out):
 *   record_flag(in):
 */
static void
btree_leaf_set_flag (RECDES * recp, short record_flag)
{
  short slot_id;

  assert ((short) (record_flag & ~BTREE_LEAF_RECORD_MASK) == 0);

  slot_id = OR_GET_SHORT (recp->data + OR_OID_SLOTID);

  OR_PUT_SHORT (recp->data + OR_OID_SLOTID, slot_id | record_flag);
}

/*
 * btree_record_object_set_mvcc_flags () - Set MVCC flags to an object in a
 *					   b-tree record.
 *
 * return	   : Void.
 * rec_data (in)   : Pointer to an object in a b-tree record.
 * mvcc_flags (in) : MVCC flags.
 */
static void
btree_record_object_set_mvcc_flags (char *rec_data, short mvcc_flags)
{
  short vol_id;

  assert ((short) (mvcc_flags & ~BTREE_OID_MVCC_FLAGS_MASK) == 0);

  vol_id = OR_GET_SHORT (rec_data + OR_OID_VOLID);

  OR_PUT_SHORT (rec_data + OR_OID_VOLID, vol_id | mvcc_flags);
}

/*
 * btree_leaf_clear_flag () - clear leaf key oid flag
 *   return:  nothing
 *   recp(in/out):
 *   record_flag(in):
 */
static void
btree_leaf_clear_flag (RECDES * recp, short record_flag)
{
  short slot_id;

  assert ((short) (record_flag & ~BTREE_LEAF_RECORD_MASK) == 0);

  slot_id = OR_GET_SHORT (recp->data + OR_OID_SLOTID);

  OR_PUT_SHORT (recp->data + OR_OID_SLOTID, slot_id & ~record_flag);
}

/*
 * btree_record_object_clear_mvcc_flags () - Clear MVCC flags from an object
 *					     in a b-tree record.
 *
 * return	   : Void.
 * rec_data (in)   : Pointer to an object in a b-tree record.
 * mvcc_flags (in) : MVCC flags to clear.
 */
static void
btree_record_object_clear_mvcc_flags (char *rec_data, short mvcc_flags)
{
  short vol_id;

  assert ((short) (mvcc_flags & ~BTREE_OID_MVCC_FLAGS_MASK) == 0);

  vol_id = OR_GET_SHORT (rec_data + OR_OID_VOLID);

  OR_PUT_SHORT (rec_data + OR_OID_VOLID, vol_id & ~mvcc_flags);
}

/*
 * btree_write_fixed_portion_of_non_leaf_record () -
 *   return:
 *   rec(in):
 *   non_leaf_rec(in):
 *
 * Note: Writes the fixed portion (preamble) of a non leaf record.
 * rec must be long enough to hold the header info.
 */
static void
btree_write_fixed_portion_of_non_leaf_record (RECDES * rec,
					      NON_LEAF_REC * non_leaf_rec)
{
  char *ptr = rec->data;

  assert (!VPID_ISNULL (&(non_leaf_rec->pnt)));

  OR_PUT_INT (ptr, non_leaf_rec->pnt.pageid);
  ptr += OR_INT_SIZE;

  OR_PUT_SHORT (ptr, non_leaf_rec->pnt.volid);
  ptr += OR_SHORT_SIZE;

  OR_PUT_SHORT (ptr, non_leaf_rec->key_len);
}

/*
 * btree_read_fixed_portion_of_non_leaf_record () -
 *   return:
 *   rec(in):
 *   non_leaf_rec(in):
 *
 * Note: Reads the fixed portion (preamble) of a non leaf record.
 */
static void
btree_read_fixed_portion_of_non_leaf_record (RECDES * rec,
					     NON_LEAF_REC * non_leaf_rec)
{
  char *ptr = rec->data;

  non_leaf_rec->pnt.pageid = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  non_leaf_rec->pnt.volid = OR_GET_SHORT (ptr);
  ptr += OR_SHORT_SIZE;

  assert (!VPID_ISNULL (&(non_leaf_rec->pnt)));

  non_leaf_rec->key_len = OR_GET_SHORT (ptr);
}

/*
 * btree_write_fixed_portion_of_non_leaf_record_to_orbuf () -
 *   return:
 *   buf(in):
 *   nlf_rec(in):
 *
 * Note: Writes the fixed portion (preamble) of a non leaf record using
 * the OR_BUF stuff.
 */
static void
btree_write_fixed_portion_of_non_leaf_record_to_orbuf (OR_BUF * buf,
						       NON_LEAF_REC *
						       non_leaf_rec)
{
  assert (!VPID_ISNULL (&(non_leaf_rec->pnt)));

  or_put_int (buf, non_leaf_rec->pnt.pageid);
  or_put_short (buf, non_leaf_rec->pnt.volid);
  or_put_short (buf, non_leaf_rec->key_len);
}

/*
 * btree_read_fixed_portion_of_non_leaf_record_from_orbuf () -
 *   return: NO_ERROR
 *   buf(in):
 *   non_leaf_rec(in):
 *
 * Note: Reads the fixed portion (preamble) of a non leaf record using
 * the OR_BUF stuff.
 */
static int
btree_read_fixed_portion_of_non_leaf_record_from_orbuf (OR_BUF * buf,
							NON_LEAF_REC *
							non_leaf_rec)
{
  int rc = NO_ERROR;

  non_leaf_rec->pnt.pageid = or_get_int (buf, &rc);

  if (rc == NO_ERROR)
    {
      non_leaf_rec->pnt.volid = or_get_short (buf, &rc);
    }

  assert (!VPID_ISNULL (&(non_leaf_rec->pnt)));

  if (rc == NO_ERROR)
    {
      non_leaf_rec->key_len = or_get_short (buf, &rc);
    }

  return rc;
}

/*
 * btree_append_oid () -
 *   return:
 *   rec(in):
 *   oid(in):
 *
 * Note: Appends an OID onto the record.  rec is assumed to have room
 * for the new OID and rec.length points to the end of the record
 * where the new OID will go and is word aligned.
 */
static void
btree_append_oid (RECDES * rec, OID * oid)
{
  char *ptr;

  ptr = rec->data + rec->length;
  OR_PUT_OID (ptr, oid);
  rec->length += OR_OID_SIZE;
}

/*
 * btree_add_mvcc_delid () - Add delete MVCCID in b-tree record.
 *
 * return		  : Void.
 * rec (in)		  : B-tree record.
 * oid_offset (in)	  : Offset to object (where MVCC flag is set).
 * mvcc_delid_offset (in) : Add MVCCID at this offset.
 * p_mvcc_delid (in)	  : Pointer to MVCCID value.
 * rv_redo_data_ptr (out) : Outputs redo recovery data for changing the
 *			    record.
 */
STATIC_INLINE void
btree_add_mvcc_delid (RECDES * rec, int oid_offset, int mvcc_delid_offset,
		      MVCCID * p_mvcc_delid, char **rv_redo_data_ptr)
{
  int dest_offset;
  char *mvcc_delid_ptr = NULL;
  char *oid_ptr = NULL;

  assert (rec != NULL && p_mvcc_delid != NULL && oid_offset >= 0
	  && mvcc_delid_offset > 0 && oid_offset < mvcc_delid_offset);
  assert (!btree_record_object_is_flagged (rec->data + oid_offset,
					   BTREE_OID_HAS_MVCC_DELID));
  assert (rec->length + OR_MVCCID_SIZE < rec->area_size);
  assert (rv_redo_data_ptr != NULL && *rv_redo_data_ptr != NULL);

  dest_offset = mvcc_delid_offset + OR_MVCCID_SIZE;
  RECORD_MOVE_DATA (rec, dest_offset, mvcc_delid_offset);

  /* Set MVCC flag. */
  oid_ptr = rec->data + oid_offset;
  btree_record_object_set_mvcc_flags (oid_ptr, BTREE_OID_HAS_MVCC_DELID);

  /* Set delete MVCCID. */
  mvcc_delid_ptr = rec->data + mvcc_delid_offset;
  OR_PUT_MVCCID (mvcc_delid_ptr, p_mvcc_delid);

  /* Redo logging: changed flag (is kept in object volume ID). */
  *rv_redo_data_ptr =
    log_rv_pack_redo_record_changes (*rv_redo_data_ptr,
				     oid_offset + OR_OID_VOLID,
				     OR_SHORT_SIZE, OR_SHORT_SIZE,
				     rec->data + oid_offset + OR_OID_VOLID);
  /* Redo logging: added MVCCID. */
  *rv_redo_data_ptr =
    log_rv_pack_redo_record_changes (*rv_redo_data_ptr, mvcc_delid_offset,
				     0, OR_MVCCID_SIZE, mvcc_delid_ptr);
}

/*
 * btree_set_mvcc_delid () - Set delete MVCCID instead of existing one.
 *
 * return		  : Error code.
 * rec (in)		  : Record data.
 * mvcc_delid_offset (in) : Offset of old delete MVCCID.
 * p_mvcc_delid (in)	  : New delete MVCCID.
 * rv_redo_data_ptr (in)  : Outputs redo recovery data for changing the
 *			    record.
 */
STATIC_INLINE void
btree_set_mvcc_delid (RECDES * rec, int mvcc_delid_offset,
		      MVCCID * p_mvcc_delid, char **rv_redo_data_ptr)
{
  char *mvcc_delid_ptr = NULL;

  assert (rec != NULL && mvcc_delid_offset > 0 && p_mvcc_delid != NULL);
  assert (rv_redo_data_ptr != NULL && *rv_redo_data_ptr != NULL);

  mvcc_delid_ptr = rec->data + mvcc_delid_offset;
  OR_PUT_MVCCID (mvcc_delid_ptr, p_mvcc_delid);

  /* Redo logging: replace MVCCID. */
  *rv_redo_data_ptr =
    log_rv_pack_redo_record_changes (*rv_redo_data_ptr, mvcc_delid_offset,
				     OR_MVCCID_SIZE, OR_MVCCID_SIZE,
				     mvcc_delid_ptr);
}

/*
 * btree_record_append_object () - Append an object and all its info to
 *				   record.
 *
 * return		  : Void.
 * thread_p (in)	  : Thread entry.
 * btid_int (in)	  : B-tree info.
 * record (in)		  : Leaf/overflow record.
 * node_type (in)	  : Note type.
 * object_info (in)	  : Object & info to append.
 * rv_redo_data_ptr (out) : If not NULL, outputs redo log recovery data for
 *			    the change.
 */
static void
btree_record_append_object (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			    RECDES * record, BTREE_NODE_TYPE node_type,
			    BTREE_OBJECT_INFO * object_info,
			    char **rv_redo_data_ptr)
{
  char *append_at = NULL;
  VPID ovf_vpid = VPID_INITIALIZER;
  int offset_to_object = 0;
  int new_data_size = 0;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (record != NULL);
  assert (object_info != NULL);

  /* Set append pointer at the end of record. */
  append_at = record->data + record->length;

  /* Make sure to keep the link to overflow pages at the end. */
  if (node_type == BTREE_LEAF_NODE && record->length > 0
      && btree_leaf_is_flaged (record, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
    {
      /* Set append pointer before the overflow link. */
      append_at -= DISK_VPID_ALIGNED_SIZE;
      /* Save overflow link. */
      OR_GET_VPID (append_at, &ovf_vpid);
      assert (!VPID_ISNULL (&ovf_vpid));
    }
  offset_to_object = CAST_BUFLEN (append_at - record->data);

  /* Pack object. */
  append_at =
    btree_pack_object (append_at, btid_int, node_type, record,
		       &object_info->oid, &object_info->class_oid,
		       &object_info->mvcc_info);
  new_data_size = CAST_BUFLEN (append_at - record->data) - offset_to_object;

  if (!VPID_ISNULL (&ovf_vpid))
    {
      /* Pack VPID again. */
      OR_PUT_VPID_ALIGNED (append_at, &ovf_vpid);
      append_at += DISK_VPID_ALIGNED_SIZE;
    }

  /* Update record length. */
  record->length = CAST_BUFLEN (append_at - record->data);

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, record,
				   node_type, NULL);
#endif

  /* Redo logging. */
  if (rv_redo_data_ptr != NULL)
    {
      assert (*rv_redo_data_ptr != NULL);
      *rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (*rv_redo_data_ptr, offset_to_object,
					 0 /* just insert data */ ,
					 new_data_size,
					 record->data + offset_to_object);
    }
}

/*
 * btree_insert_object_ordered_by_oid () - Insert in record by keeping objects
 *					   ordered by OID's.
 *
 * return		 : Error code.
 * record (in)		 : B-tree (overflow) record.
 * btid_int (in)	 : B-tree info.
 * object_info (in)	 : Object & info being inserted.
 * rv_redo_data_ptr (in) : If not null, outputs redo recovery data for the
 *			   changes made to record.
 */
static void
btree_insert_object_ordered_by_oid (RECDES * record, BTID_INT * btid_int,
				    BTREE_OBJECT_INFO * object_info,
				    char **rv_redo_data_ptr)
{
  OID *oid = NULL;
  char *oid_ptr = NULL;
  int min, mid, max, num;
  OID mid_oid;
  int size = BTREE_OBJECT_FIXED_SIZE (btid_int);
  int offset_to_object = 0;
  bool duplicate_oid = false;

  /* Assert expected arguments. */
  assert (record != NULL);
  assert (btid_int != NULL);
  assert (object_info != NULL);
  assert (BTREE_MVCC_INFO_HAS_INSID (&object_info->mvcc_info)
	  && BTREE_MVCC_INFO_HAS_DELID (&object_info->mvcc_info));

  assert (record->length % size == 0);
  num = CEIL_PTVDIV (record->length, size);
  assert (num >= 0);

  if (num == 0)
    {
      /* Add at the beginning of the record */
      oid_ptr = record->data;
      return;
    }
  /* At least one object. */

  /* Binary search for the right position to keep the order */
  min = 0;
  max = num - 1;
  mid = 0;
  oid = &object_info->oid;

  while (min <= max)
    {
      mid = (min + max) / 2;
      oid_ptr = record->data + (size * mid);

      /* Get MID object OID. */
      BTREE_GET_OID (oid_ptr, &mid_oid);

      /* Is same OID? */
      if (OID_EQ (oid, &mid_oid))
	{
	  /* With MVCC, this case is possible if some conditions are met:
	   * 1. OID is reusable.
	   * 2. Vacuum cleaned heap entry but didn't clean b-tree entry.
	   * 3. A new record is inserted in the same slot.
	   * 4. The key for old record and new record is the same.
	   * Just replace the old OID's insert/delete information.
	   */
	  duplicate_oid = true;
	  break;
	}
      else if (OID_GT (oid, &mid_oid))
	{
	  min = mid + 1;
	  mid++;
	}
      else
	{
	  max = mid - 1;
	}
    }

  offset_to_object = size * mid;
  oid_ptr = record->data + offset_to_object;

  /* oid_ptr points to the address where the new object should be saved */
  if (!duplicate_oid)
    {
      /* Make room for a new OID */
      RECORD_MOVE_DATA (record, offset_to_object + size, offset_to_object);
    }

  (void) btree_pack_object (oid_ptr, btid_int, BTREE_OVERFLOW_NODE, record,
			    &object_info->oid, &object_info->class_oid,
			    &object_info->mvcc_info);

#if !defined (NDEBUG)
  (void) btree_check_valid_record (NULL, btid_int, record,
				   BTREE_OVERFLOW_NODE, NULL);
#endif

  /* Log changes. */
  if (rv_redo_data_ptr != NULL)
    {
      *rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (*rv_redo_data_ptr, offset_to_object,
					 duplicate_oid ? size : 0,
					 size, oid_ptr);
    }
}

/*
 * btree_start_overflow_page () - Create a new overflow page when OID cannot
 *				  be inserted elsewhere. New page is always
 *				  inserted "after" leaf (and before other
 *				  existing overflow pages).
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * btid_int (in)	    : B-tree info.
 * object_info (in)	    :
 * first_overflow_vpid (in) :
 * near_vpid (in)	    :
 * new_vpid (out)	    :
 * new_page_ptr (out)	    :
 */
static int
btree_start_overflow_page (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			   BTREE_OBJECT_INFO * object_info,
			   VPID * first_overflow_vpid, VPID * near_vpid,
			   VPID * new_vpid, PAGE_PTR * new_page_ptr)
{
  int ret = NO_ERROR;
  RECDES rec;
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  BTREE_OVERFLOW_HEADER ovf_header_info;
  LOG_DATA_ADDR addr;
  int error_code = NO_ERROR;

  /* Redo recovery. */
  char rv_redo_data_buffer[BTREE_RV_REDO_BUFFER_SIZE + MAX_ALIGNMENT];
  char *rv_redo_data = PTR_ALIGN (rv_redo_data_buffer, MAX_ALIGNMENT);
  char *rv_redo_data_ptr = NULL;
  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (object_info != NULL);
  assert (first_overflow_vpid != NULL);
  assert (new_vpid != NULL);
  assert (new_page_ptr != NULL);
  assert (BTREE_MVCC_INFO_HAS_INSID (&object_info->mvcc_info)
	  && BTREE_MVCC_INFO_HAS_DELID (&object_info->mvcc_info));

  /* Get a new overflow page */
  *new_page_ptr =
    btree_get_new_page (thread_p, btid_int, new_vpid, near_vpid);
  if (*new_page_ptr == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  VPID_COPY (&ovf_header_info.next_vpid, first_overflow_vpid);

  error_code =
    btree_init_overflow_header (thread_p, *new_page_ptr, &ovf_header_info);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* Insert the value in the new overflow page */
  /* Prepare record */
  rec.type = REC_HOME;
  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);
  rec.length = 0;
  btree_record_append_object (thread_p, btid_int, &rec, BTREE_OVERFLOW_NODE,
			      object_info, NULL);

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, &rec,
				   BTREE_OVERFLOW_NODE, NULL);
#endif /* !NDEBUG */

  /* Insert in page. */
  if (spage_insert_at (thread_p, *new_page_ptr, 1, &rec) != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  /* Initialized log data address */
  addr.offset = 0;
  addr.pgptr = *new_page_ptr;
  addr.vfid = &btid_int->sys_btid->vfid;

  /* Redo log. */
  rv_redo_data_ptr = rv_redo_data;
  LOG_RV_RECORD_SET_MODIFY_MODE (&addr, LOG_RV_RECORD_INSERT);
  BTREE_RV_REDO_SET_OVERFLOW_NODE (&addr);
#if !defined (NDEBUG)
  BTREE_RV_REDO_SET_DEBUG_INFO (&addr, rv_redo_data_ptr, btid_int,
				BTREE_RV_DEBUG_ID_START_OVF);
#endif /* !NDEBUG */
  /* Save first overflow link. */
  OR_PUT_VPID_ALIGNED (rv_redo_data_ptr, first_overflow_vpid);
  rv_redo_data_ptr += DISK_VPID_ALIGNED_SIZE;
  /* Save overflow record data. */
  memcpy (rv_redo_data_ptr, rec.data, rec.length);
  rv_redo_data_ptr += rec.length;

  rv_redo_data_length = CAST_BUFLEN (rv_redo_data_ptr - rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);

  log_append_redo_data (thread_p, RVBT_RECORD_MODIFY_NO_UNDO, &addr,
			rv_redo_data_length, rv_redo_data);

  pgbuf_set_dirty (thread_p, *new_page_ptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_get_disk_size_of_key () -
 *   return:
 *   key(in):
 */
int
btree_get_disk_size_of_key (DB_VALUE * key)
{
  if (key == NULL || DB_IS_NULL (key))
    {
      assert (key != NULL && !DB_IS_NULL (key));
      return 0;
    }

  return pr_index_writeval_disk_size (key);
}

/*
 * btree_write_record () -
 *   return: NO_ERROR
 *   btid(in):
 *   node_rec(in):
 *   key(in):
 *   node_type(in):
 *   key_type(in):
 *   key_len(in):
 *   during_loading(in):
 *   class_oid(in):
 *   oid(in):
 *   p_mvcc_rec_header(in): MVCC record header
 *   rec(out):
 *
 * Note: This routine forms a btree record for both leaf and non leaf pages.
 *
 * node_rec is a NON_LEAF_REC * if we are writing a non leaf page,
 * otherwise it is a LEAF_REC *. ovfl_key indicates whether the key will
 * be written to the page or stored by the overflow manager. If we are
 * writing a non leaf record, oid should be NULL and will be ignored in
 * any case.
 */
int
btree_write_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
		    void *node_rec, DB_VALUE * key, int node_type,
		    int key_type, int key_len, bool during_loading,
		    OID * class_oid, OID * oid,
		    BTREE_MVCC_INFO * mvcc_info, RECDES * rec)
{
  VPID key_vpid;
  OR_BUF buf;
  int error_code = NO_ERROR;
  short rec_type = 0;

  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_NON_LEAF_NODE);
  assert (key_type == BTREE_NORMAL_KEY || key_type == BTREE_OVERFLOW_KEY);
  assert (rec != NULL);

  or_init (&buf, rec->data, rec->area_size);

  if (node_type == BTREE_LEAF_NODE)
    {
      /* first instance oid */
      error_code = or_put_oid (&buf, oid);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}
      if (BTREE_IS_UNIQUE (btid->unique_pk)
	  && !OID_EQ (&btid->topclass_oid, class_oid))
	{
	  /* write the subclass OID */
	  error_code = or_put_oid (&buf, class_oid);
	  if (error_code != NO_ERROR)
	    {
	      assert_release (false);
	      return error_code;
	    }
	  btree_leaf_set_flag (rec, BTREE_LEAF_RECORD_CLASS_OID);
	}

      if (mvcc_info != NULL)
	{
	  if (BTREE_MVCC_INFO_HAS_INSID (mvcc_info))
	    {
	      error_code = or_put_mvccid (&buf, mvcc_info->insert_mvccid);
	      if (error_code != NO_ERROR)
		{
		  assert_release (false);
		  return error_code;
		}
	    }
	  if (BTREE_MVCC_INFO_HAS_DELID (mvcc_info))
	    {
	      error_code = or_put_mvccid (&buf, mvcc_info->delete_mvccid);
	      if (error_code != NO_ERROR)
		{
		  assert_release (false);
		  return error_code;
		}
	    }
	  btree_record_object_set_mvcc_flags (rec->data, mvcc_info->flags);
	}
    }
  else
    {
      NON_LEAF_REC *non_leaf_rec = (NON_LEAF_REC *) node_rec;

      btree_write_fixed_portion_of_non_leaf_record_to_orbuf (&buf,
							     non_leaf_rec);
    }

  /* write the key */
  if (key_type == BTREE_NORMAL_KEY)
    {				/* key fits in page */
      PR_TYPE *pr_type;

      if (node_type == BTREE_LEAF_NODE)
	{
	  pr_type = btid->key_type->type;
	}
      else
	{
	  pr_type = btid->nonleaf_key_type->type;
	}

      error_code = (*(pr_type->index_writeval)) (&buf, key);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}
    }
  else
    {
      /* overflow key */
      if (node_type == BTREE_LEAF_NODE)
	{
	  btree_leaf_set_flag (rec, BTREE_LEAF_RECORD_OVERFLOW_KEY);
	}

      error_code =
	btree_store_overflow_key (thread_p, btid, key, key_len, node_type,
				  &key_vpid);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}

      /* write the overflow VPID as the key */
      error_code = or_put_int (&buf, key_vpid.pageid);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}
      error_code = or_put_short (&buf, key_vpid.volid);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return error_code;
	}
    }

  error_code = or_put_align32 (&buf);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      return error_code;
    }

  rec->length = CAST_BUFLEN (buf.ptr - buf.buffer);
  rec->type = REC_HOME;

  /* Success. */
  return NO_ERROR;
}

/*
 * btree_read_record () -
 *   return:
 *   btid(in):
 *   pgptr(in):
 *   rec(in):
 *   key(in):
 *   rec_header(in):
 *   node_type(in):
 *   clear_key(in):
 *   offset(in):
 *   copy_key(in):
 *
 * Note: This routine reads a btree record for both leaf and non leaf pages.
 *
 * copy_key indicates whether strings should be malloced and copied
 * or just returned via pointer.  offset will point to the oid(s) following
 * the key for leaf pages.  clear_key will indicate whether the key needs
 * to be cleared via pr_clear_value by the caller.  If this record is
 * a leaf record, rec_header will be filled in with the LEAF_REC,
 * otherwise, rec_header will be filled in with the NON_LEAF_REC for this
 * record.
 *
 * If you don't want to actually read the key (possibly incurring a
 * malloc for the string), you can send a NULL pointer for the key.
 * index_readval() will do the right thing and simply skip the key in this case.
 */
int
btree_read_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
		   PAGE_PTR pgptr, RECDES * rec, DB_VALUE * key,
		   void *rec_header, int node_type,
		   bool * clear_key, int *offset, int copy_key,
		   BTREE_SCAN * bts)
{
  int n_prefix = COMMON_PREFIX_UNKNOWN;
  int error;

  assert (pgptr != NULL);
  assert (rec != NULL);
  assert (rec->type == REC_HOME);
  assert (bts == NULL || bts->common_prefix == -1
	  || bts->common_prefix == btree_node_common_prefix (thread_p, btid,
							     pgptr));

  if (bts != NULL)
    {
      n_prefix = bts->common_prefix;
    }

  error = btree_read_record_without_decompression (thread_p, btid, rec, key,
						   rec_header, node_type,
						   clear_key, offset,
						   copy_key);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (key != NULL &&
      node_type == BTREE_LEAF_NODE &&
      !btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_OVERFLOW_KEY) &&
      !btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_FENCE))
    {
      if (n_prefix == COMMON_PREFIX_UNKNOWN)
	{
	  /* recalculate n_prefix */
	  n_prefix = btree_node_common_prefix (thread_p, btid, pgptr);
	}

      assert (n_prefix >= 0);

      if (n_prefix > 0)
	{
	  RECDES peek_rec;
	  DB_VALUE lf_key, result;
	  bool lf_clear_key;
	  LEAF_REC leaf_pnt;
	  int dummy_offset;

	  (void) spage_get_record (pgptr, 1, &peek_rec, PEEK);
	  error = btree_read_record_without_decompression (thread_p, btid,
							   &peek_rec,
							   &lf_key, &leaf_pnt,
							   BTREE_LEAF_NODE,
							   &lf_clear_key,
							   &dummy_offset,
							   PEEK_KEY_VALUE);
	  if (error != NO_ERROR)
	    {
	      btree_clear_key_value (clear_key, key);
	      return error;
	    }

	  assert (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE));
	  assert (DB_VALUE_TYPE (&lf_key) == DB_TYPE_MIDXKEY);

	  pr_midxkey_add_prefix (&result, &lf_key, key, n_prefix);

	  btree_clear_key_value (clear_key, key);
	  btree_clear_key_value (&lf_clear_key, &lf_key);

	  *key = result;
	  *clear_key = true;
	}
      else if (n_prefix < 0)
	{
	  return n_prefix;
	}
    }

  return NO_ERROR;
}

/*
 * btree_read_record_without_decompression () -
 *   return:
 *   btid(in):
 *   rec(in):
 *   key(in):
 *   rec_header(in):
 *   node_type(in):
 *   clear_key(in):
 *   offset(in):
 *   copy_key(in):
 *
 */
static int
btree_read_record_without_decompression (THREAD_ENTRY * thread_p,
					 BTID_INT * btid, RECDES * rec,
					 DB_VALUE * key, void *rec_header,
					 int node_type, bool * clear_key,
					 int *offset, int copy_key)
{
  OR_BUF buf;
  VPID overflow_vpid;
  char *copy_key_buf;
  int copy_key_buf_len;
  int rc = NO_ERROR;
  int key_type = BTREE_NORMAL_KEY;
  LEAF_REC *leaf_rec = NULL;
  NON_LEAF_REC *non_leaf_rec = NULL;

  assert (rec != NULL);
  assert (rec->type == REC_HOME);

  if (key != NULL)
    {
      db_make_null (key);
    }

  *clear_key = false;

#if !defined(NDEBUG)
  if (!rec || !rec->data)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_read_record_without_decompression: null node header pointer."
		    " Operation Ignored.");
      return rc;
    }
#endif

  assert (rec_header != NULL);

  or_init (&buf, rec->data, rec->length);

  /*
   * Find the beginning position of the key within the record and read
   * the key length.
   */
  if (node_type == BTREE_LEAF_NODE)
    {
      leaf_rec = (LEAF_REC *) rec_header;
      leaf_rec->key_len = -1;

      rc = or_advance (&buf, OR_OID_SIZE);	/* skip instance oid */
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      if (BTREE_IS_UNIQUE (btid->unique_pk))
	{
	  if (btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_CLASS_OID))
	    {
	      rc = or_advance (&buf, OR_OID_SIZE);	/* skip class oid */
	      if (rc != NO_ERROR)
		{
		  return rc;
		}
	    }
	}

      if (btree_record_object_is_flagged (rec->data,
					  BTREE_OID_HAS_MVCC_INSID))
	{
	  rc = or_advance (&buf, OR_MVCCID_SIZE);	/* skip insert mvccid */
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}

      if (btree_record_object_is_flagged (rec->data,
					  BTREE_OID_HAS_MVCC_DELID))
	{
	  rc = or_advance (&buf, OR_MVCCID_SIZE);	/* skip delete mvccid */
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}

      if (btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_OVERFLOW_KEY))
	{
	  key_type = BTREE_OVERFLOW_KEY;
	}

      if (btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
	{
	  btree_leaf_get_vpid_for_overflow_oids (rec, &leaf_rec->ovfl);
	}
      else
	{
	  VPID_SET_NULL (&leaf_rec->ovfl);
	}

      assert (leaf_rec->key_len == -1);
    }
  else
    {
      non_leaf_rec = (NON_LEAF_REC *) rec_header;
      non_leaf_rec->key_len = -1;

      rc =
	btree_read_fixed_portion_of_non_leaf_record_from_orbuf (&buf,
								non_leaf_rec);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      if (non_leaf_rec->key_len < 0)
	{
	  key_type = BTREE_OVERFLOW_KEY;
	}
    }

  if (key_type == BTREE_NORMAL_KEY)
    {				/* key is within page */
      TP_DOMAIN *key_domain;
      PR_TYPE *pr_type;
      char *old_ptr;

      if (node_type == BTREE_LEAF_NODE)
	{
	  key_domain = btid->key_type;
	}
      else
	{
	  key_domain = btid->nonleaf_key_type;
	}
      pr_type = key_domain->type;

      copy_key_buf = NULL;
      copy_key_buf_len = 0;

      /*
       * When we read the key, must copy in two cases:
       *   1) we are told to via the copy_key flag, or 2) it is a set.
       */
      if (key != NULL && copy_key == COPY_KEY_VALUE)
	{
	  *clear_key = true;
	}
      else
	{
	  *clear_key = false;
	}

      if (*clear_key)
	{			/* need to copy the key */
	  if (btid->copy_buf != NULL)
	    {			/* check for copy_buf */
	      if (pr_type->id == DB_TYPE_MIDXKEY
		  || QSTR_IS_ANY_CHAR_OR_BIT (pr_type->id))
		{		/* check for the key type */
		  /* read key_val image into the copy_buf */
		  copy_key_buf = btid->copy_buf;
		  copy_key_buf_len = btid->copy_buf_len;
		}
	    }
	}

      old_ptr = buf.ptr;
      rc = (*(pr_type->index_readval)) (&buf, key, key_domain, -1, *clear_key,
					copy_key_buf, copy_key_buf_len);
      if (rc != NO_ERROR)
	{
	  return rc;
	}
      assert (CAST_BUFLEN (buf.ptr - old_ptr) > 0);
      if (key != NULL)
	{
	  assert (!DB_IS_NULL (key));
	}

      if (node_type == BTREE_LEAF_NODE)
	{
	  leaf_rec->key_len = CAST_BUFLEN (buf.ptr - old_ptr);
	}
      else
	{
	  non_leaf_rec->key_len = CAST_BUFLEN (buf.ptr - old_ptr);
	}
    }
  else
    {
      /* follow the chain of overflow key page pointers and fetch key */
      overflow_vpid.pageid = or_get_int (&buf, &rc);
      if (rc == NO_ERROR)
	{
	  overflow_vpid.volid = or_get_short (&buf, &rc);
	  if (rc != NO_ERROR)
	    {
	      assert (false);

	      if (key != NULL)
		{
		  db_make_null (key);
		}

	      return rc;
	    }
	}
      else
	{
	  return rc;
	}

      if (key != NULL)
	{
	  rc = btree_load_overflow_key (thread_p, btid, &overflow_vpid, key,
					node_type);
	  if (rc != NO_ERROR)
	    {
	      db_make_null (key);
	      return rc;
	    }

	  assert (!DB_IS_NULL (key));

	  /* we always clear overflow keys */
	  *clear_key = true;
	}
      else
	{
	  /* we aren't copying the key so they don't have to clear it */
	  *clear_key = false;
	}
    }

  buf.ptr = PTR_ALIGN (buf.ptr, OR_INT_SIZE);

  *offset = CAST_BUFLEN (buf.ptr - buf.buffer);

  return rc;
}

/*
 * btree_dump_root_header () -
 *   return:
 *   rec(in):
 */
static void
btree_dump_root_header (FILE * fp, PAGE_PTR page_ptr)
{
  OR_BUF buf;
  BTREE_ROOT_HEADER *root_header = NULL;
  TP_DOMAIN *key_type;

  root_header = btree_get_root_header (page_ptr);
  if (root_header == NULL)
    {
      fprintf (fp, "btree_dump_root_header: get root header failure\n");

      return;
    }

  or_init (&buf, root_header->packed_key_domain, -1);
  key_type = or_get_domain (&buf, NULL, NULL);

  fprintf (fp, "==============    R O O T    P A G E   ================\n\n");
  fprintf (fp, " Key_Type: %s\n", pr_type_name (TP_DOMAIN_TYPE (key_type)));
  fprintf (fp, " Num OIDs: %d, Num NULLs: %d, Num keys: %d\n",
	   root_header->num_oids, root_header->num_nulls,
	   root_header->num_keys);
  fprintf (fp, " Topclass_oid: (%d %d %d)\n",
	   root_header->topclass_oid.volid, root_header->topclass_oid.pageid,
	   root_header->topclass_oid.slotid);
  fprintf (fp, " Unique: ");
  if (BTREE_IS_UNIQUE (root_header->unique_pk))
    {
      fprintf (fp, "1");
      if (BTREE_IS_PRIMARY_KEY (root_header->unique_pk))
	{
	  fprintf (fp, " (Primary Key)");
	}
    }
  else
    {
      assert (!BTREE_IS_PRIMARY_KEY (root_header->unique_pk));
      fprintf (fp, "0");
    }
  fprintf (fp, "\n");
  fprintf (fp, " OVFID: %d|%d\n", root_header->ovfid.fileid,
	   root_header->ovfid.volid);
  fprintf (fp, " Btree Revision Level: %d\n", root_header->rev_level);
  fprintf (fp, " Reserved: %d\n", root_header->reverse_reserved);	/* unused */
  fprintf (fp, "\n");
}

/*
 * btree_dump_key () -
 *   return:
 *   key(in):
 */
void
btree_dump_key (FILE * fp, DB_VALUE * key)
{
  DB_TYPE key_type = DB_VALUE_DOMAIN_TYPE (key);
  PR_TYPE *pr_type = PR_TYPE_FROM_ID (key_type);

  assert (pr_type != NULL);

  if (pr_type)
    {
#if 1
      fprintf (fp, " ");
      (*(pr_type->fptrfunc)) (fp, key);
      fprintf (fp, " ");

#else /* debug routine - DO NOT DELETE ME */
      /* simple dump for debug */
      /* dump '      ' to ' +' */
      char buff[4096];
      int i, j, c;

      (*(pr_type->sptrfunc)) (key, buff, 4096);

      for (i = 0, j = 0; i < 4096; i++, j++)
	{
	  buff[j] = buff[i];

	  if (buff[i] == 0)
	    {
	      break;
	    }

	  c = 0;
	  while (buff[i] == ' ' && i > 1 && buff[i - 1] == ' ')
	    {
	      c++;
	      i++;
	    }

	  if (c > 1)
	    {
	      j++;
	      buff[j] = '+';
	    }
	}

      fprintf (fp, " ");
      fprintf (fp, buff);
      fprintf (fp, " ");
#endif
    }
}

/*
 * btree_dump_leaf_record () -
 *   return: nothing
 *   btid(in): B+tree index identifier
 *   rec(in): Pointer to a record in a leaf page of the tree
 *   n(in): Indentation left margin (number of preceding blanks)
 *
 * Note: Dumps the content of a leaf record, namely key and the set of
 * values for the key.
 */
static void
btree_dump_leaf_record (THREAD_ENTRY * thread_p, FILE * fp,
			BTID_INT * btid, RECDES * rec, int depth)
{
  OR_BUF buf;
  LEAF_REC leaf_record = {
    {
     NULL_PAGEID, NULL_VOLID}, 0
  };
  int i, k, oid_cnt;
  OID class_oid;
  OID oid;
  int key_len, offset;
  VPID overflow_vpid;
  DB_VALUE key;
  bool clear_key;
  int oid_size;
  MVCCID mvccid;
  int error;
  BTREE_MVCC_INFO mvcc_info;

  if (BTREE_IS_UNIQUE (btid->unique_pk))
    {
      oid_size = (2 * OR_OID_SIZE);
    }
  else
    {
      oid_size = OR_OID_SIZE;
    }

  /* output the leaf record structure content */
  btree_print_space (fp, depth * 4 + 1);

  error = btree_read_record_without_decompression (thread_p, btid, rec, &key,
						   &leaf_record,
						   BTREE_LEAF_NODE,
						   &clear_key, &offset,
						   PEEK_KEY_VALUE);
  if (error != NO_ERROR)
    {
      return;
    }
  key_len = btree_get_disk_size_of_key (&key);

  fprintf (fp, "Key_Len: %d Ovfl_Page: {%d , %d} ",
	   key_len, leaf_record.ovfl.volid, leaf_record.ovfl.pageid);

  fprintf (fp, "Key: ");
  btree_dump_key (fp, &key);

  btree_clear_key_value (&clear_key, &key);

  overflow_vpid = leaf_record.ovfl;

  /* output the values */
  fprintf (fp, "  Values: ");

  oid_cnt =
    btree_record_get_num_oids (thread_p, btid, rec, offset, BTREE_LEAF_NODE);
  fprintf (fp, "Oid_Cnt: %d ", oid_cnt);

  /* output first oid */
  (void) btree_leaf_get_first_object (btid, rec, &oid, &class_oid,
				      &mvcc_info);
  if (BTREE_IS_UNIQUE (btid->unique_pk))
    {
      fprintf (fp, " (%d %d %d : %d, %d, %d) ",
	       class_oid.volid, class_oid.pageid, class_oid.slotid,
	       oid.volid, oid.pageid, oid.slotid);
    }
  else
    {
      fprintf (fp, " (%d, %d, %d) ", oid.volid, oid.pageid, oid.slotid);
    }

  /* output MVCCIDs of first OID */
  if (mvcc_info.flags != 0)
    {
      fprintf (fp, " (");
      if (mvcc_info.flags & BTREE_OID_HAS_MVCC_INSID)
	{
	  /* Get and print insert MVCCID */
	  OR_GET_MVCCID (rec->data + oid_size, &mvccid);
	  fprintf (fp, "insid=%llu", (long long) mvccid);

	  if (mvcc_info.flags & BTREE_OID_HAS_MVCC_DELID)
	    {
	      /* Get and print delete MVCCID */
	      OR_GET_MVCCID (rec->data + oid_size + OR_MVCCID_SIZE, &mvccid);
	      fprintf (fp, ", delid=%llu", (long long) mvccid);
	    }
	}
      else
	{
	  /* Safe guard */
	  assert (mvcc_info.flags & BTREE_OID_HAS_MVCC_DELID);

	  /* Get and print delete MVCCID */
	  OR_GET_MVCCID (rec->data + oid_size, &mvccid);
	  fprintf (fp, "delid=%llu", (long long) mvccid);
	}

      fprintf (fp, ")  ");
    }

  /* output remainder OIDs */
  or_init (&buf, rec->data + offset, rec->length - offset);
  if (BTREE_IS_UNIQUE (btid->unique_pk))
    {
      for (k = 1; k < oid_cnt; k++)
	{
	  /* values stored within the record */
	  if (k % 2 == 0)
	    {
	      fprintf (fp, "\n");
	    }

	  /* Get OID */
	  or_get_oid (&buf, &oid);
	  BTREE_OID_CLEAR_MVCC_FLAGS (&oid);

	  /* Get class OID */
	  or_get_oid (&buf, &class_oid);

	  /* Print OID and class OID */
	  fprintf (fp, " (%d %d %d : %d, %d, %d) ",
		   class_oid.volid, class_oid.pageid, class_oid.slotid,
		   oid.volid, oid.pageid, oid.slotid);

	  /* Since objects are fixed size, they contain both insert and delete
	   * MVCCID's.
	   */
	  fprintf (fp, " (");

	  /* Get and print insert MVCCID */
	  (void) or_get_mvccid (&buf, &mvccid);
	  fprintf (fp, "insid=%llu", (long long) mvccid);

	  /* Get and print delete MVCCID */
	  (void) or_get_mvccid (&buf, &mvccid);
	  fprintf (fp, ", delid=%llu", (long long) mvccid);

	  fprintf (fp, ")  ");
	}
    }
  else
    {
      for (k = 1; k < oid_cnt; k++)
	{
	  /* values stored within the record */
	  if (k % 4 == 0)
	    {
	      fprintf (fp, "\n");
	    }

	  /* Get MVCC flags */
	  mvcc_info.flags = btree_record_object_get_mvcc_flags (buf.ptr);

	  or_get_oid (&buf, &oid);
	  BTREE_OID_CLEAR_MVCC_FLAGS (&oid);

	  fprintf (fp, " (%d, %d, %d) ", oid.volid, oid.pageid, oid.slotid);

	  if (mvcc_info.flags != 0)
	    {
	      fprintf (fp, " (");
	      if (mvcc_info.flags & BTREE_OID_HAS_MVCC_INSID)
		{
		  /* Get and print insert MVCCID */
		  (void) or_get_mvccid (&buf, &mvccid);
		  fprintf (fp, "insid=%llu", (long long) mvccid);

		  if (mvcc_info.flags & BTREE_OID_HAS_MVCC_DELID)
		    {
		      /* Get and print delete MVCCID */
		      (void) or_get_mvccid (&buf, &mvccid);
		      fprintf (fp, ", delid=%llu", (long long) mvccid);
		    }
		}
	      else
		{
		  /* Safe guard */
		  assert (mvcc_info.flags & BTREE_OID_HAS_MVCC_DELID);

		  /* Get and print delete MVCCID */
		  (void) or_get_mvccid (&buf, &mvccid);
		  fprintf (fp, "delid=%llu", (long long) mvccid);
		}
	      fprintf (fp, ")  ");
	    }
	}
    }

  if (!VPID_ISNULL (&overflow_vpid))
    {
      /* record has an overflow page continuation */
      RECDES overflow_rec;
      PAGE_PTR overflow_page_ptr = NULL;
      char overflow_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

      overflow_rec.area_size = DB_PAGESIZE;
      overflow_rec.data = PTR_ALIGN (overflow_rec_buf, BTREE_MAX_ALIGN);

      fprintf (fp,
	       "\n\n=======    O V E R F L O W   P A G E S     =========\n");
      fflush (fp);

      /* get all the overflow pages and output their value content */
      while (!VPID_ISNULL (&overflow_vpid))
	{
	  fprintf (fp, "\n ------ Overflow Page {%d , %d} \n",
		   overflow_vpid.volid, overflow_vpid.pageid);
	  overflow_page_ptr = pgbuf_fix (thread_p, &overflow_vpid, OLD_PAGE,
					 PGBUF_LATCH_READ,
					 PGBUF_UNCONDITIONAL_LATCH);
	  if (overflow_page_ptr == NULL)
	    {
	      ASSERT_ERROR ();
	      return;
	    }

	  btree_get_next_overflow_vpid (overflow_page_ptr, &overflow_vpid);

	  (void) spage_get_record (overflow_page_ptr, 1, &overflow_rec, COPY);

	  oid_cnt =
	    btree_record_get_num_oids (thread_p, btid, &overflow_rec, 0,
				       BTREE_OVERFLOW_NODE);
	  or_init (&buf, overflow_rec.data, overflow_rec.length);
	  fprintf (fp, "Oid_Cnt: %d ", oid_cnt);

	  for (i = 0; i < oid_cnt; i++)
	    {
	      if (i % 4 == 0)
		{
		  fprintf (stdout, "\n");
		}

	      or_get_oid (&buf, &oid);
	      BTREE_OID_CLEAR_MVCC_FLAGS (&oid);

	      fprintf (fp, " (%d, %d, %d) ", oid.volid, oid.pageid,
		       oid.slotid);

	      if (BTREE_IS_UNIQUE (btid->unique_pk))
		{
		  or_get_oid (&buf, &class_oid);
		  fprintf (fp, " (%d, %d, %d) ", class_oid.volid,
			   class_oid.pageid, class_oid.slotid);
		}

	      fprintf (fp, " (");
	      (void) or_get_mvccid (&buf, &mvccid);
	      fprintf (fp, "insid=%llu", (long long) mvccid);
	      (void) or_get_mvccid (&buf, &mvccid);
	      fprintf (fp, ", delid=%llu", (long long) mvccid);
	      fprintf (fp, ")  ");
	    }

	  pgbuf_unfix_and_init (thread_p, overflow_page_ptr);
	}
    }

  fprintf (fp, "\n");
  fflush (fp);
}

/*
 * btree_dump_non_leaf_record () -
 *   return: void
 *   btid(in):
 *   rec(in): Pointer to a record in a non_leaf page
 *   n(in): Indentation left margin (number of preceding blanks)
 *   print_key(in):
 *
 * Note: Dumps the content of a nonleaf record, namely key and child
 * page identifier.
 */
static void
btree_dump_non_leaf_record (THREAD_ENTRY * thread_p, FILE * fp,
			    BTID_INT * btid, RECDES * rec, int depth,
			    int print_key)
{
  NON_LEAF_REC non_leaf_record;
  int key_len, offset;
  DB_VALUE key;
  bool clear_key;
  int error;

  VPID_SET_NULL (&(non_leaf_record.pnt));

  /* output the non_leaf record structure content */
  error = btree_read_record_without_decompression (thread_p, btid, rec, &key,
						   &non_leaf_record,
						   BTREE_NON_LEAF_NODE,
						   &clear_key, &offset,
						   PEEK_KEY_VALUE);
  if (error != NO_ERROR)
    {
      return;
    }

  btree_print_space (fp, depth * 4);
  fprintf (fp, "Child_Page: {%d , %d} ",
	   non_leaf_record.pnt.volid, non_leaf_record.pnt.pageid);

  if (print_key)
    {
      key_len = btree_get_disk_size_of_key (&key);
      fprintf (fp, "Key_Len: %d  Key: ", key_len);
      btree_dump_key (fp, &key);
    }
  else
    {
      fprintf (fp, "No Key");
    }

  btree_clear_key_value (&clear_key, &key);

  fprintf (fp, "\n");
  fflush (fp);
}

/*
 * btree_get_new_page () - GET a NEW PAGE for the B+tree index
 *   return: The pointer to a newly allocated page for the given
 *           B+tree or NULL.
 *           The parameter vpid is set to the page identifier.
 *   btid(in): B+tree index identifier
 *   vpid(out): Set to the page identifier for the newly allocated page
 *   near_vpid(in): A page identifier that may be used in a nearby page
 *                  allocation. (It may be ignored.)
 *
 * Note: Allocates a new page for the B+tree
 */
static PAGE_PTR
btree_get_new_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
		    VPID * vpid, VPID * near_vpid)
{
  PAGE_PTR page_ptr = NULL;
  unsigned short alignment;

  alignment = BTREE_MAX_ALIGN;
  if (file_alloc_pages
      (thread_p, &(btid->sys_btid->vfid), vpid, 1, near_vpid,
       btree_initialize_new_page, (void *) (&alignment)) == NULL)
    {
      return NULL;
    }

  /*
   * Note: we fetch the page as old since it was initialized during the
   * allocation by btree_initialize_new_page, therefore, we care about
   * the current content of the page.
   */
  page_ptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      (void) btree_dealloc_page (thread_p, btid, vpid);
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_BTREE);

  return page_ptr;
}

/*
 * btree_dealloc_page () -
 *   return: NO_ERROR or error code
 *
 *   btid(in):
 *   vpid(in):
 */
static int
btree_dealloc_page (THREAD_ENTRY * thread_p, BTID_INT * btid, VPID * vpid)
{
  int error = NO_ERROR;

  if (log_start_system_op (thread_p) == NULL)
    {
      return ER_FAILED;
    }

  error = file_dealloc_page (thread_p, &btid->sys_btid->vfid, vpid);

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

  return error;
}

/*
 * btree_initialize_new_page () -
 *   return: bool
 *   vfid(in): File where the new page belongs
 *   file_type(in):
 *   vpid(in): The new page
 *   ignore_npages(in): Number of contiguous allocated pages
 *                      (Ignored in this function. We allocate only one page)
 *   ignore_args(in): More arguments to function.
 *                    Ignored at this moment.
 *
 * Note: Initialize a newly allocated btree page.
 */
static bool
btree_initialize_new_page (THREAD_ENTRY * thread_p, const VFID * vfid,
			   const FILE_TYPE file_type, const VPID * vpid,
			   INT32 ignore_npages, void *args)
{
  PAGE_PTR pgptr;
  unsigned short alignment;

  /*
   * fetch and initialize the new page. The parameter UNANCHORED_KEEP_
   * SEQUENCE indicates that the order of records will be preserved
   * during insertions and deletions.
   */

  pgptr = pgbuf_fix (thread_p, vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return false;
    }

  (void) pgbuf_set_page_ptype (thread_p, pgptr, PAGE_BTREE);

  alignment = *((unsigned short *) args);
  spage_initialize (thread_p, pgptr, UNANCHORED_KEEP_SEQUENCE,
		    alignment, DONT_SAFEGUARD_RVSPACE);
  log_append_redo_data2 (thread_p, RVBT_GET_NEWPAGE, vfid, pgptr, -1,
			 sizeof (alignment), &alignment);
  pgbuf_set_dirty (thread_p, pgptr, FREE);

  return true;
}

/*
 * btree_search_nonleaf_page () -
 *   return: NO_ERROR
 *   btid(in):
 *   page_ptr(in): Pointer to the non_leaf page to be searched
 *   key(in): Key to find
 *   slot_id(out): Set to the record number that contains the key
 *   child_vpid(out): Set to the child page identifier to be followed,
 *                    or NULL_PAGEID
 *
 * Note: Binary search the page to locate the record that contains the
 * child page pointer to be followed to locate the key, and
 * return the page identifier for this child page.
 */
static int
btree_search_nonleaf_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
			   PAGE_PTR page_ptr, DB_VALUE * key,
			   INT16 * slot_id, VPID * child_vpid)
{
  int key_cnt, offset;
  int c;
  bool clear_key;
  /* the start position of non-equal-value column */
  int start_col, left_start_col, right_start_col;
  INT16 left, right;
  INT16 middle = 0;
  DB_VALUE temp_key;
  RECDES rec;
  NON_LEAF_REC non_leaf_rec;

  /* initialize child page identifier */
  VPID_SET_NULL (child_vpid);

#if !defined(NDEBUG)
  if (!page_ptr || !key || DB_IS_NULL (key))
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_search_nonleaf_page: null page/key pointer."
		    " Operation Ignored.");
      return ER_FAILED;
    }
#endif

  key_cnt = btree_node_number_of_keys (page_ptr);
  assert (key_cnt > 0);

  if (key_cnt <= 0)
    {				/* node record underflow */
      er_log_debug (ARG_FILE_LINE,
		    "btree_search_nonleaf_page: node key count underflow: %d",
		    key_cnt);
      return ER_FAILED;
    }

  if (key_cnt == 1)
    {
      /*
       * node has dummy neg-inf keys, but a child page pointer
       * So, follow this pointer
       */
      if (spage_get_record (page_ptr, 1, &rec, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      btree_read_fixed_portion_of_non_leaf_record (&rec, &non_leaf_rec);

      *slot_id = 1;
      *child_vpid = non_leaf_rec.pnt;

      return NO_ERROR;
    }

  /* binary search the node to find the child page pointer to be followed */
  c = 0;

  /* for non-compressed midxkey; separator is not compressed */
  left_start_col = right_start_col = 0;

  left = 2;			/* Ignore dummy key (neg-inf or 1st key) */
  right = key_cnt;

  while (left <= right)
    {
      middle = CEIL_PTVDIV ((left + right), 2);	/* get the middle record */

      assert (middle > 0);
      if (spage_get_record (page_ptr, middle, &rec, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      if (btree_read_record_without_decompression (thread_p, btid, &rec,
						   &temp_key, &non_leaf_rec,
						   BTREE_NON_LEAF_NODE,
						   &clear_key, &offset,
						   PEEK_KEY_VALUE) !=
	  NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
	{
	  start_col = MIN (left_start_col, right_start_col);
	}

      c = btree_compare_key (key, &temp_key, btid->key_type,
			     1, 1, &start_col);

      btree_clear_key_value (&clear_key, &temp_key);

      if (c == DB_UNK)
	{
	  return ER_FAILED;
	}

      if (c == 0)
	{
	  /* child page to be followed has been found */
	  *slot_id = middle;
	  *child_vpid = non_leaf_rec.pnt;

	  return NO_ERROR;
	}
      else if (c < 0)
	{
	  right = middle - 1;
	  right_start_col = start_col;
	}
      else
	{
	  left = middle + 1;
	  left_start_col = start_col;
	}
    }

  if (c < 0)
    {
      /* child page is the one pointed by the record left to the middle  */
      assert (middle - 1 > 0);
      if (spage_get_record (page_ptr, middle - 1, &rec, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      btree_read_fixed_portion_of_non_leaf_record (&rec, &non_leaf_rec);

      *slot_id = middle - 1;
      *child_vpid = non_leaf_rec.pnt;
    }
  else
    {
      /* child page is the one pointed by the middle record */
      *slot_id = middle;
      *child_vpid = non_leaf_rec.pnt;
    }

  return NO_ERROR;
}

/*
 * btree_search_leaf_page () - Search key in page and return result.
 *   return	      : Error code.
 *   btid (in)	      : B-tree info.
 *   page_ptr (in)    : Leaf node page pointer.
 *   key (in)	      : Search key.
 *   search_key (out) : Output result:
 *			- BTREE_KEY_FOUND and slotid of key.
 *			- BTREE_KEY_NOTFOUND (unknown compare result).
 *			- BTREE_KEY_SMALLER (smaller than any key in page,
 *			  slotid = 0).
 *			- BTREE_KEY_BIGGER (bigger than any key in page,
 *			  slotid = key_cnt + 1).
 *			- BTREE_KEY_BETWEEN (key is not found, but it would
 *			  belong to this page if it existed. slotid of next
 *			  bigger key, where the searched key should be
 *			  inserted).
 *
 * Note: Binary search the page to find the location of the key.
 *
 * NOTE: If this function is called after advancing through the index from
 *	 root and if page has upper fence key, under no circumstance can key
 *	 argument be equal to it. The advance algorithm should have pointed
 *	 to next leaf node.
 *	 However this case is possible when accessing leaf node directly
 *	 (e.g. after unfixing-refixing leaf node). In this case, the key
 *	 is not considered equal to fence key, but rather bigger than all
 *	 keys in page. The caller should know to go to next page.
 */
static int
btree_search_leaf_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
			PAGE_PTR page_ptr, DB_VALUE * key,
			BTREE_SEARCH_KEY_HELPER * search_key)
{
  int key_cnt = 0, offset = 0;
  int c = 0, n_prefix = 0;
  bool clear_key = false;
  /* the start position of non-equal-value column */
  int start_col = 0, left_start_col = 0, right_start_col = 0;
  INT16 left = 0, right = 0, middle = 0;
  DB_VALUE temp_key;
  RECDES rec;
  LEAF_REC leaf_pnt;
  int error = NO_ERROR;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (key != NULL && !DB_IS_NULL (key));
  assert (page_ptr != NULL);
  assert (search_key != NULL);

  /* Initialize search results. */
  search_key->result = BTREE_KEY_NOTFOUND;
  search_key->slotid = NULL_SLOTID;

  key_cnt = btree_node_number_of_keys (page_ptr);
  if (key_cnt < 0)
    {
      assert (false);
      er_log_debug (ARG_FILE_LINE,
		    "btree_search_leaf_page: node key count underflow: %d.",
		    key_cnt);
      return ER_FAILED;
    }

  /* for compressed midxkey */
  n_prefix = btree_node_common_prefix (thread_p, btid, page_ptr);
  if (n_prefix < 0)
    {
      /* Error case? */
      return n_prefix;
    }
  left_start_col = right_start_col = n_prefix;

  /*
   * binary search the node to find if the key exists and in which record it
   * exists, or if it doesn't exist , the in which record it should have been
   * located to preserve the order of keys
   */

  /* Initialize binary search range to first and last key in page. */
  left = 1;
  right = key_cnt;

  /* Loop while binary search range has at least one key. */
  while (left <= right)
    {
      /* Get current range middle record */
      middle = CEIL_PTVDIV ((left + right), 2);
      /* Safe guard. */
      assert (middle > 0);

      /* Get current middle key. */
      if (spage_get_record (page_ptr, middle, &rec, PEEK) != S_SUCCESS)
	{
	  /* Unexpected error. */
	  er_log_debug (ARG_FILE_LINE,
			"btree_search_leaf_page: sp_getrec fails for middle "
			"record.");
	  assert (false);
	  return ER_FAILED;
	}
      error = btree_read_record_without_decompression (thread_p, btid, &rec,
						       &temp_key,
						       &leaf_pnt,
						       BTREE_LEAF_NODE,
						       &clear_key, &offset,
						       PEEK_KEY_VALUE);
      if (error != NO_ERROR)
	{
	  /* Error! */
	  ASSERT_ERROR ();
	  return error;
	}

      if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
	{
	  start_col = MIN (left_start_col, right_start_col);
	}

      /* Compare searched key with current middle key. */
      c =
	btree_compare_key (key, &temp_key, btid->key_type, 1, 1, &start_col);

      /* Clear current middle key. */
      btree_clear_key_value (&clear_key, &temp_key);

      if (c == DB_UNK)
	{
	  /* Unknown compare result? */
	  search_key->result = BTREE_KEY_NOTFOUND;
	  search_key->slotid = NULL_SLOTID;

	  /* Is this an error case? */
	  ASSERT_ERROR_AND_SET (error);
	  return error;
	}

      if (c == DB_EQ)
	{
	  /* Current middle key is equal to searched key. */
	  if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
	    {
	      /* Fence key! */
	      assert (middle == 1 || middle == key_cnt);
	      if (middle == 1)
		{
		  /* Set c = DB_GT and fall through to compare with next key.
		   */
		  c = DB_GT;
		}
	      else if (middle == key_cnt)
		{
		  /* Key is bigger than all in page??
		   * I have to understand these fence keys better.
		   */
		  search_key->result = BTREE_KEY_BIGGER;
		  search_key->slotid = key_cnt;
		  return NO_ERROR;
		}
	    }
	  else
	    {
	      /* Key exists in page in current middle slot. */
	      search_key->result = BTREE_KEY_FOUND;
	      search_key->slotid = middle;
	      return NO_ERROR;
	    }
	}
      /* Key not found yet. Keep searching. */
      if (c < 0)
	{
	  /* Continue search between left and current middle. */
	  right = middle - 1;
	  right_start_col = start_col;
	}
      else
	{
	  /* Continue search between current middle and right. */
	  left = middle + 1;
	  left_start_col = start_col;
	}
    }

  if (c < 0)
    {
      /* Key doesn't exist and is smaller than current middle key. */
      if (middle == 1)
	{
	  /* Key is smaller than all records in page. */
	  search_key->result = BTREE_KEY_SMALLER;
	}
      else
	{
	  /* Key doesn't exist but should belong to this page. */
	  search_key->result = BTREE_KEY_BETWEEN;
	}
      search_key->slotid = middle;

      return NO_ERROR;
    }
  else
    {
      /* Key doesn't exist and is bigger than current middle key. */
      if (middle == key_cnt)
	{
	  search_key->result = BTREE_KEY_BIGGER;
	}
      else
	{
	  search_key->result = BTREE_KEY_BETWEEN;
	}
      search_key->slotid = middle + 1;

      return NO_ERROR;
    }

  /* Impossible to reach. */
  assert (false);
  return ER_FAILED;
}

/*
 * xbtree_add_index () - ADD (create) a new B+tree INDEX
 *   return: BTID * (btid on success and NULL on failure)
 *   btid(out): Set to the created B+tree index identifier
 *              (Note: btid->vfid.volid should be set by the caller)
 *   key_type(in): Key type of the index to be created.
 *   class_oid(in): OID of the class for which the index is created
 *   attr_id(in): Identifier of the attribute of the class for which the
 *                index is created.
 *   unique_pk(in):
 *   num_oids(in):
 *   num_nulls(in):
 *   num_keys(in):
 *
 * Note: Creates the B+tree index. A file identifier (index identifier)
 * is defined on the given volume. This identifier is used by
 * insertion, deletion and search routines, for the created
 * index. The routine allocates the root page of the tree and
 * initializes the root header information.
 */
BTID *
xbtree_add_index (THREAD_ENTRY * thread_p, BTID * btid, TP_DOMAIN * key_type,
		  OID * class_oid, int attr_id, int unique_pk,
		  int num_oids, int num_nulls, int num_keys)
{
  BTREE_ROOT_HEADER root_header_info, *root_header = NULL;
  VPID first_page_vpid, root_vpid;
  PAGE_PTR page_ptr = NULL;
  FILE_BTREE_DES btree_descriptor;
  bool is_file_created = false;
  unsigned short alignment;
  LOG_DATA_ADDR addr;
  bool btree_id_complete = false;

  root_header = &root_header_info;

  if (class_oid == NULL || OID_ISNULL (class_oid))
    {
      return NULL;
    }

  if (log_start_system_op (thread_p) == NULL)
    {
      return NULL;
    }

  /* create a file descriptor */
  COPY_OID (&btree_descriptor.class_oid, class_oid);
  btree_descriptor.attr_id = attr_id;

  /* create a file descriptor, allocate and initialize the root page */
  if (file_create (thread_p, &btid->vfid, 2, FILE_BTREE, &btree_descriptor,
		   &first_page_vpid, 1) == NULL || btree_get_root_page
      (thread_p, btid, &root_vpid) == NULL)
    {
      goto error;
    }

  assert (VPID_EQ (&first_page_vpid, &root_vpid));

  is_file_created = true;

  vacuum_log_add_dropped_file (thread_p, &btid->vfid,
			       VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

  alignment = BTREE_MAX_ALIGN;
  if (btree_initialize_new_page
      (thread_p, &btid->vfid, FILE_BTREE, &root_vpid, 1,
       (void *) &alignment) == false)
    {
      goto error;
    }

  /*
   * Note: we fetch the page as old since it was initialized by
   * btree_initialize_new_page; we want the current contents of
   * the page.
   */
  page_ptr =
    pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == NULL)
    {
      goto error;
    }

  (void) pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_BTREE);

  /* form the root header information */
  root_header->node.split_info.pivot = 0.0f;
  root_header->node.split_info.index = 0;
  VPID_SET_NULL (&(root_header->node.prev_vpid));
  VPID_SET_NULL (&(root_header->node.next_vpid));
  root_header->node.node_level = 1;
  root_header->node.max_key_len = 0;

  if (unique_pk)
    {
      root_header->num_oids = num_oids;
      root_header->num_nulls = num_nulls;
      root_header->num_keys = num_keys;
      root_header->unique_pk = unique_pk;

      assert (BTREE_IS_UNIQUE (root_header->unique_pk));
      assert (BTREE_IS_PRIMARY_KEY (root_header->unique_pk)
	      || !BTREE_IS_PRIMARY_KEY (root_header->unique_pk));
    }
  else
    {
      root_header->num_oids = -1;
      root_header->num_nulls = -1;
      root_header->num_keys = -1;
      root_header->unique_pk = 0;
    }

  COPY_OID (&(root_header->topclass_oid), class_oid);

  VFID_SET_NULL (&(root_header->ovfid));
  root_header->rev_level = BTREE_CURRENT_REV_LEVEL;

  root_header->reverse_reserved = 0;	/* unused */

  if (btree_init_root_header
      (thread_p, &btid->vfid, page_ptr, root_header, key_type) != NO_ERROR)
    {
      goto error;
    }

  pgbuf_set_dirty (thread_p, page_ptr, FREE);
  page_ptr = NULL;

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

  file_new_declare_as_old (thread_p, &btid->vfid);

  /* set the B+tree index identifier */
  btid->root_pageid = root_vpid.pageid;
  btree_id_complete = true;

  addr.vfid = NULL;
  addr.pgptr = NULL;
  addr.offset = 0;
  log_append_undo_data (thread_p, RVBT_CREATE_INDEX, &addr, sizeof (BTID),
			btid);

  /* Already append a vacuum undo logging when file was created, but
   * since that was included in the system operation which just got
   * committed, we need to do it again in case of rollback.
   */
  vacuum_log_add_dropped_file (thread_p, &btid->vfid,
			       VACUUM_LOG_ADD_DROPPED_FILE_UNDO);

  return btid;

error:
  if (btree_id_complete)
    {
      logtb_delete_global_unique_stats (thread_p, btid);
    }
  if (page_ptr)
    {
      pgbuf_unfix_and_init (thread_p, page_ptr);
    }

  if (is_file_created)
    {
      (void) file_destroy (thread_p, &btid->vfid);
    }

  VFID_SET_NULL (&btid->vfid);
  btid->root_pageid = NULL_PAGEID;

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);

  return NULL;
}

/*
 * xbtree_delete_index () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *
 * Note: Removes the B+tree index. All pages associated with the index
 * are removed. After the routine is called, the index identifier
 * is not valid any more.
 */
int
xbtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid)
{
  PAGE_PTR P = NULL;
  VPID P_vpid;
  BTREE_ROOT_HEADER *root_header = NULL;
  VFID ovfid;
  LOG_TRAN_BTID_UNIQUE_STATS *unique_stats = NULL;
  RECDES redo_rec, undo_rec;
  char redo_rec_buf[OR_INT_SIZE + OR_BTID_ALIGNED_SIZE + BTREE_MAX_ALIGN],
    *datap = NULL;
  char undo_rec_buf[(4 * OR_INT_SIZE) + OR_BTID_ALIGNED_SIZE +
		    BTREE_MAX_ALIGN];
  int ret = NO_ERROR;
  int num_nulls, num_oids, num_keys, unique_pk;

  P_vpid.volid = btid->vfid.volid;	/* read the root page */
  P_vpid.pageid = btid->root_pageid;
  P = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		 PGBUF_UNCONDITIONAL_LATCH);
  if (P == NULL)
    {
      return (((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret);
    }

  (void) pgbuf_check_page_ptype (thread_p, P, PAGE_BTREE);

  /* read the header record */
  root_header = btree_get_root_header (P);
  if (root_header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, P);
      return (((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret);
    }

  ovfid = root_header->ovfid;

  unique_pk = root_header->unique_pk;

  if (unique_pk)
    {
      /* mark the statistics associated with deleted B-tree as deleted */
      unique_stats = logtb_tran_find_btid_stats (thread_p, btid, true);
      if (unique_stats == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, P);
	  return ER_FAILED;
	}
      else
	{
	  unique_stats->deleted = true;
	}

      ret =
	logtb_get_global_unique_stats (thread_p, btid, &num_oids, &num_nulls,
				       &num_keys);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, P);
	  return ret;
	}

      ret = logtb_delete_global_unique_stats (thread_p, btid);
      if (ret != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, P);
	  return ret;
	}
    }

  pgbuf_unfix_and_init (thread_p, P);

  /* This is used by unique indexes at recovery to remove the entry from global
   * hash, and at rollback to restore statistics in global hash. However, a
   * special log for such an important step as index deletion will be useful,
   * also, for other purposes.
   */

  /* undo */
  undo_rec.data = NULL;
  undo_rec.area_size = 4 * OR_INT_SIZE + OR_BTID_ALIGNED_SIZE;
  undo_rec.data = PTR_ALIGN (undo_rec_buf, BTREE_MAX_ALIGN);

  undo_rec.length = 0;
  datap = (char *) undo_rec.data;
  OR_PUT_BTID (datap, btid);
  datap += OR_BTID_ALIGNED_SIZE;
  OR_PUT_INT (datap, unique_pk);
  datap += OR_INT_SIZE;
  if (unique_pk)
    {
      OR_PUT_INT (datap, num_nulls);
      datap += OR_INT_SIZE;
      OR_PUT_INT (datap, num_oids);
      datap += OR_INT_SIZE;
      OR_PUT_INT (datap, num_keys);
      datap += OR_INT_SIZE;
    }
  undo_rec.length = CAST_BUFLEN (datap - undo_rec.data);

  /* redo */
  redo_rec.data = NULL;
  redo_rec.area_size = OR_BTID_ALIGNED_SIZE;
  redo_rec.data = PTR_ALIGN (redo_rec_buf, BTREE_MAX_ALIGN);

  redo_rec.length = 0;
  datap = (char *) redo_rec.data;
  OR_PUT_BTID (datap, btid);
  datap += OR_BTID_ALIGNED_SIZE;
  OR_PUT_INT (datap, unique_pk);
  datap += OR_INT_SIZE;
  redo_rec.length = CAST_BUFLEN (datap - redo_rec.data);

  log_append_undoredo_data2 (thread_p, RVBT_DELETE_INDEX, NULL, NULL, -1,
			     undo_rec.length, redo_rec.length, undo_rec.data,
			     redo_rec.data);

  btid->root_pageid = NULL_PAGEID;

  vacuum_log_add_dropped_file (thread_p, &btid->vfid,
			       VACUUM_LOG_ADD_DROPPED_FILE_POSTPONE);

  ret = file_destroy (thread_p, &btid->vfid);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  if (!VFID_ISNULL (&ovfid))
    {
      ret = file_destroy (thread_p, &ovfid);
    }

  return ret;
}

/*
 * btree_generate_prefix_domain () -
 *   return:
 *   btid(in):
 *
 * Note: This routine returns a varying domain of the same precision
 * for fixed domains which are one of the string types.  For all other
 * domains, it returns the same domain.
 */
TP_DOMAIN *
btree_generate_prefix_domain (BTID_INT * btid)
{
  TP_DOMAIN *domain = btid->key_type;
  TP_DOMAIN *var_domain;
  DB_TYPE dbtype;
  DB_TYPE vartype;

  dbtype = TP_DOMAIN_TYPE (domain);

  /* varying domains did not come into use until btree revision level 1 */
  if (!pr_is_variable_type (dbtype) && pr_is_string_type (dbtype))
    {
      switch (dbtype)
	{
	case DB_TYPE_CHAR:
	  vartype = DB_TYPE_VARCHAR;
	  break;
	case DB_TYPE_NCHAR:
	  vartype = DB_TYPE_VARNCHAR;
	  break;
	case DB_TYPE_BIT:
	  vartype = DB_TYPE_VARBIT;
	  break;
	default:
	  assert (false);
#if defined(CUBRID_DEBUG)
	  printf ("Corrupt domain in btree_generate_prefix_domain\n");
#endif /* CUBRID_DEBUG */
	  return NULL;
	}

      var_domain = tp_domain_resolve (vartype, domain->class_mop,
				      domain->precision, domain->scale,
				      domain->setdomain,
				      domain->collation_id);
    }
  else
    {
      var_domain = domain;
    }

  return var_domain;
}

/*
 * btree_glean_root_header_info () -
 *   return:
 *   root_header(in):
 *   btid(in):
 *
 * Note: This captures the interesting header info into the BTID_INT structure.
 */
int
btree_glean_root_header_info (THREAD_ENTRY * thread_p,
			      BTREE_ROOT_HEADER * root_header,
			      BTID_INT * btid)
{
  int rc;
  OR_BUF buf;

  rc = NO_ERROR;

  btid->unique_pk = root_header->unique_pk;

  or_init (&buf, root_header->packed_key_domain, -1);
  btid->key_type = or_get_domain (&buf, NULL, NULL);

  COPY_OID (&btid->topclass_oid, &root_header->topclass_oid);

  btid->ovfid = root_header->ovfid;	/* structure copy */

  /* check for the last element domain of partial-key is desc;
   * for btree_range_search, part_key_desc is re-set at btree_prepare_bts
   */
  btid->part_key_desc = 0;

  /* init index key copy_buf info */
  btid->copy_buf = NULL;
  btid->copy_buf_len = 0;

  /* this must be discovered after the Btree key_type */
  btid->nonleaf_key_type = btree_generate_prefix_domain (btid);

  btid->rev_level = root_header->rev_level;

  return rc;
}

/*
 * xbtree_delete_with_unique_key -
 *   btid (in):
 *   class_oid (in):
 *   key_value (in):
 *   return:
 */
int
xbtree_delete_with_unique_key (THREAD_ENTRY * thread_p, BTID * btid,
			       OID * class_oid, DB_VALUE * key_value)
{
  int error = NO_ERROR;
  OID unique_oid;
  HEAP_SCANCACHE scan_cache;
  BTREE_SEARCH r;

  r = xbtree_find_unique (thread_p, btid, S_DELETE, key_value,
			  class_oid, &unique_oid, true);

  if (r == BTREE_KEY_FOUND)
    {
      HFID hfid;
      int force_count;

      error = heap_get_hfid_from_class_oid (thread_p, class_oid, &hfid);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = heap_scancache_start_modify (thread_p, &scan_cache, &hfid,
					   class_oid, SINGLE_ROW_DELETE,
					   NULL);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = locator_delete_force (thread_p, &hfid, &unique_oid, btid, false,
				    true, SINGLE_ROW_DELETE, &scan_cache,
				    &force_count, NULL);
      if (error == NO_ERROR)
	{
	  /* monitor */
	  mnt_qm_deletes (thread_p);
	}

      heap_scancache_end_modify (thread_p, &scan_cache);
    }
  else if (r == BTREE_KEY_NOTFOUND)
    {
      btree_set_unknown_key_error (thread_p, btid, key_value,
				   "xbtree_delete_with_unique_key: "
				   "current key not found.");
      error = ER_BTREE_UNKNOWN_KEY;
    }
  else
    {
      /* r == BTREE_ERROR_OCCURRED */
      ASSERT_ERROR_AND_SET (error);
    }

  return error;
}

/*
 * xbtree_find_multi_uniques () - search a list of unique indexes for
 *				  specified values
 * return : search return code
 * thread_p (in)  : handler thread
 * class_oid (in) : class oid
 * pruning_type (in) : pruning type
 * btids (in)	  : indexes to search
 * values (in)	  : values to search for
 * count (in)	  : number of indexes
 * op_type (in)	  : operation for which this search is performed
 * oids (in/out)  : found OIDs
 * oids_count (in): number of OIDs found
 *
 * Note: This function assumes that the indexes it searches are unique
 *  indexes. If the operation is S_UPDATE, this function stops at the first
 *  oid it finds in order to comply with the behavior of ON DUPLICATE KEY
 *  UPDATE statement.
 */
BTREE_SEARCH
xbtree_find_multi_uniques (THREAD_ENTRY * thread_p, OID * class_oid,
			   int pruning_type, BTID * btids, DB_VALUE * values,
			   int count, SCAN_OPERATION_TYPE op_type,
			   OID ** oids, int *oids_count)
{
  BTREE_SEARCH result = BTREE_KEY_FOUND;
  OID *found_oids = NULL;
  int i, idx, error = NO_ERROR;
  bool is_at_least_one = false;
  BTID pruned_btid;
  OID pruned_class_oid;
  HFID pruned_hfid;
  PRUNING_CONTEXT context;
  bool is_global_index = false;

  partition_init_pruning_context (&context);

  found_oids = (OID *) db_private_alloc (thread_p, count * sizeof (OID));
  if (found_oids == NULL)
    {
      return BTREE_ERROR_OCCURRED;
    }

  if (pruning_type != DB_NOT_PARTITIONED_CLASS)
    {
      error = partition_load_pruning_context (thread_p, class_oid,
					      pruning_type, &context);
      if (error != NO_ERROR)
	{
	  result = BTREE_ERROR_OCCURRED;
	  goto error_return;
	}
    }

  idx = 0;
  for (i = 0; i < count; i++)
    {
      is_global_index = false;
      BTID_COPY (&pruned_btid, &btids[i]);
      COPY_OID (&pruned_class_oid, class_oid);

      if (pruning_type)
	{
	  /* At this point, there's no way of knowing if btids[i] refers a
	   * global unique index or a local one. Perform pruning and use the
	   * partition BTID, even if it is the same one as the BTID we
	   * received
	   */
	  error = partition_prune_unique_btid (&context, &values[i],
					       &pruned_class_oid,
					       &pruned_hfid, &pruned_btid);
	  if (error != NO_ERROR)
	    {
	      result = BTREE_ERROR_OCCURRED;
	      goto error_return;
	    }
	}

      result = xbtree_find_unique (thread_p, &pruned_btid, op_type,
				   &values[i], &pruned_class_oid,
				   &found_oids[idx], is_global_index);

      if (result == BTREE_KEY_FOUND)
	{
	  if (pruning_type == DB_PARTITION_CLASS)
	    {
	      if (!OID_EQ (&pruned_class_oid, class_oid))
		{
		  /* Found a constraint violation on a different partition:
		   * throw invalid partition
		   */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_INVALID_DATA_FOR_PARTITION, 0);
		  error = ER_INVALID_DATA_FOR_PARTITION;
		  result = BTREE_ERROR_OCCURRED;
		  goto error_return;
		}
	    }
	  is_at_least_one = true;
	  idx++;
	  if (op_type == S_UPDATE)
	    {
	      break;
	    }
	}
      else if (result == BTREE_ERROR_OCCURRED)
	{
	  goto error_return;
	}
      else
	{
	  /* result == BTREE_KEY_NOTFOUND */
	  ;			/* just go to the next one */
	}
    }

  if (is_at_least_one)
    {
      *oids_count = idx;
      *oids = found_oids;
      result = BTREE_KEY_FOUND;
    }
  else
    {
      result = BTREE_KEY_NOTFOUND;
      db_private_free_and_init (thread_p, found_oids);
      *oids = NULL;
      *oids_count = 0;
    }
  partition_clear_pruning_context (&context);
  return result;

error_return:
  if (found_oids != NULL)
    {
      db_private_free_and_init (thread_p, found_oids);
    }
  *oids_count = 0;
  *oids = NULL;
  partition_clear_pruning_context (&context);
  return BTREE_ERROR_OCCURRED;
}

/*
 * btree_find_foreign_key () - Find and lock any existing object in foreign
 *			       key. Used to check that delete/update on
 *			       primary key is allowed.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid (in)	   : B-tree ID.
 * key (in)	   : Key value.
 * class_oid (in)  : Class OID.
 * found_oid (out) : Outputs OID of found object. If no object is found
 *		     outputs NULL.
 */
int
btree_find_foreign_key (THREAD_ENTRY * thread_p, BTID * btid,
			DB_VALUE * key, OID * class_oid, OID * found_oid)
{
  BTREE_SCAN btree_scan;
  int error_code = NO_ERROR;
  KEY_VAL_RANGE key_val_range;
  BTREE_FIND_FK_OBJECT find_fk_object = BTREE_FIND_FK_OBJECT_INITIALIZER;

  assert (btid != NULL);
  assert (key != NULL);
  assert (class_oid != NULL);
  assert (found_oid != NULL);

  /* Find if key has any objects. */

  /* Define range of scan. */
  PR_SHARE_VALUE (key, &key_val_range.key1);
  PR_SHARE_VALUE (key, &key_val_range.key2);
  key_val_range.range = GE_LE;
  key_val_range.num_index_term = 0;

  /* Initialize not found. */
  OID_SET_NULL (&find_fk_object.found_oid);

#if defined (SERVER_MODE)
  /* Use S_LOCK to block found object. */
  find_fk_object.lock_mode = S_LOCK;
#endif /* SERVER_MODE */
  /* Prepare scan. */
  BTREE_INIT_SCAN (&btree_scan);
  error_code =
    btree_prepare_bts (thread_p, &btree_scan, btid, NULL, &key_val_range,
		       NULL, NULL, NULL, NULL, false, &find_fk_object);
  if (error_code != NO_ERROR)
    {
      assert_release (false);
      return error_code;
    }
  /* Execute scan. */
  error_code =
    btree_range_scan (thread_p, &btree_scan,
		      btree_range_scan_find_fk_any_object);
  assert (error_code == NO_ERROR || er_errid () != NO_ERROR);

  /* Output found object. */
  COPY_OID (found_oid, &find_fk_object.found_oid);

#if defined (SERVER_MODE)
  if (error_code != NO_ERROR || OID_ISNULL (&find_fk_object.found_oid))
    {
      /* Release lock on object if any. */
      if (!OID_ISNULL (&find_fk_object.locked_object))
	{
	  lock_unlock_object_donot_move_to_non2pl (thread_p,
						   &find_fk_object.
						   locked_object,
						   &btree_scan.btid_int.
						   topclass_oid, S_LOCK);
	}
    }
#endif /* SERVER_MODE */
  return error_code;
}

/*
 * btree_scan_clear_key () -
 *   return:
 *   btree_scan(in):
 */
void
btree_scan_clear_key (BTREE_SCAN * btree_scan)
{
  btree_clear_key_value (&btree_scan->clear_cur_key, &btree_scan->cur_key);
}

/*
 * btree_is_unique_type () -
 *   return: Whether the given BTREE_TYPE corresponds to a unique index B+tree
 *   type(in):
 */
bool
btree_is_unique_type (BTREE_TYPE type)
{
  if (type == BTREE_UNIQUE || type == BTREE_REVERSE_UNIQUE
      || type == BTREE_PRIMARY_KEY)
    {
      return true;
    }
  return false;
}

/*
 * xbtree_class_test_unique () -
 *   return: int
 *   buf(in):
 *   buf_size(in):
 *
 * Note: Return NO_ERROR if the btrees given are unique.
 * Return ER_BTREE_UNIQUE_FAILED if one of the unique tests failed.
 * This is used for interpreter and xasl batch checking of uniqueness.
 */
int
xbtree_class_test_unique (THREAD_ENTRY * thread_p, char *buf, int buf_size)
{
  int status = NO_ERROR;
  char *bufp, *buf_endptr;
  BTID btid;

  bufp = buf;
  buf_endptr = (buf + buf_size);

  while ((bufp < buf_endptr) && (status == NO_ERROR))
    {
      /* unpack the BTID */
      bufp = or_unpack_btid (bufp, &btid);

      /* check if the btree is unique */
      if ((status == NO_ERROR) && (xbtree_test_unique (thread_p, &btid) != 1))
	{
	  BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, NULL, NULL,
					    NULL, &btid, NULL);
	  status = ER_BTREE_UNIQUE_FAILED;
	}
    }

  return status;
}

/*
 * xbtree_test_unique () -
 *   return: int
 *   btid(in): B+tree index identifier
 *
 * Note: Return 1 (true) if the index is unique, return 0 if
 * the index is not unique, return -1 if the btree isn't
 * keeping track of unique statistics (a regular, plain jane btree).
 */
static int
xbtree_test_unique (THREAD_ENTRY * thread_p, BTID * btid)
{
  INT32 num_oids, num_nulls, num_keys;

  if (logtb_get_global_unique_stats (thread_p, btid, &num_oids,
				     &num_nulls, &num_keys) != NO_ERROR)
    {
      return 0;
    }

  if (num_nulls == -1)
    {
      return -1;
    }
  else if ((num_nulls + num_keys) != num_oids)
    {
      assert (false);
      return 0;
    }
  else
    {
      return 1;
    }
}

/*
 * xbtree_get_unique_pk () -
 *   return:
 *   btid(in):
 */
int
xbtree_get_unique_pk (THREAD_ENTRY * thread_p, BTID * btid)
{
  VPID root_vpid;
  PAGE_PTR root = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  int unique_pk;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      return 0;
    }

  (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

  root_header = btree_get_root_header (root);
  if (root_header == NULL)
    {
      return 0;
    }

  unique_pk = root_header->unique_pk;

  pgbuf_unfix_and_init (thread_p, root);

  return unique_pk;
}

/*
 * btree_get_unique_statistics_for_count () - gets unique statistics
 *   return:
 *   btid(in): B+tree index identifier
 *   oid_cnt(in/out): no. of oids
 *   null_cnt(in/out): no. of nulls
 *   key_cnt(in/out): no. of keys
 *
 * Note: In MVCC the statistics are taken from memory structures. In non-mvcc
 *	 from B-tree header
 */
int
btree_get_unique_statistics_for_count (THREAD_ENTRY * thread_p, BTID * btid,
				       int *oid_cnt, int *null_cnt,
				       int *key_cnt)
{
  LOG_TRAN_BTID_UNIQUE_STATS *unique_stats = NULL, *part_stats = NULL;

  unique_stats = logtb_tran_find_btid_stats (thread_p, btid, true);
  if (unique_stats == NULL)
    {
      return ER_FAILED;
    }
  *oid_cnt =
    unique_stats->tran_stats.num_oids + unique_stats->global_stats.num_oids;
  *key_cnt =
    unique_stats->tran_stats.num_keys + unique_stats->global_stats.num_keys;
  *null_cnt =
    unique_stats->tran_stats.num_nulls + unique_stats->global_stats.num_nulls;

  return NO_ERROR;
}

/*
 * btree_get_unique_statistics () -
 *   returns: NO_ERROR
 *   btid(in):
 *   oid_cnt(in):
 *   null_cnt(in):
 *   key_cnt(in):
 *
 * Note: Reads the unique btree statistics from the root header.  If
 * the btree is not a unique btree, all the stats will be -1.
 */
int
btree_get_unique_statistics (THREAD_ENTRY * thread_p, BTID * btid,
			     int *oid_cnt, int *null_cnt, int *key_cnt)
{
  VPID root_vpid;
  PAGE_PTR root = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  int ret;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      return (((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret);
    }

  (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

  root_header = btree_get_root_header (root);
  if (root_header == NULL)
    {
      return (((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret);
    }

  assert ((root_header->
	   unique_pk & (BTREE_CONSTRAINT_UNIQUE |
			BTREE_CONSTRAINT_PRIMARY_KEY)) != 0);

  *oid_cnt = root_header->num_oids;
  *null_cnt = root_header->num_nulls;
  *key_cnt = root_header->num_keys;

  pgbuf_unfix_and_init (thread_p, root);

  return NO_ERROR;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * btree_get_subtree_stats () -
 *   return: NO_ERROR
 *   btid(in):
 *   pg_ptr(in):
 *   env(in):
 */
static int
btree_get_subtree_stats (THREAD_ENTRY * thread_p, BTID_INT * btid,
			 PAGE_PTR page_ptr, BTREE_STATS_ENV * stats_env)
{
  int key_cnt;
  int i, j;
  NON_LEAF_REC non_leaf_rec;
  VPID page_vpid;
  PAGE_PTR page = NULL;
  RECDES rec;
  DB_DOMAIN *key_type;
  int ret = NO_ERROR;
  BTREE_NODE_HEADER *header = NULL;

  key_type = btid->key_type;
  key_cnt = btree_node_number_of_keys (page_ptr);

  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_get_subtree_stats: get node header failure: %d",
		    key_cnt);
      goto exit_on_error;
    }

  if (header->node_level > 1)	/* BTREE_NON_LEAF_NODE */
    {
      if (key_cnt < 0)
	{
	  er_log_debug (ARG_FILE_LINE,
			"btree_get_subtree_stats: node key count"
			" underflow: %d", key_cnt);
	  goto exit_on_error;
	}

      /*
       * traverse all the subtrees of this non_leaf page and accumulate
       * the statistical data in the environment structure
       */
      for (i = 1; i <= key_cnt + 1; i++)
	{
	  if (spage_get_record (page_ptr, i, &rec, PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }

	  btree_read_fixed_portion_of_non_leaf_record (&rec, &non_leaf_rec);
	  page_vpid = non_leaf_rec.pnt;

	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, page, PAGE_BTREE);

	  ret = btree_get_subtree_stats (thread_p, btid, page, stats_env);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  pgbuf_unfix_and_init (thread_p, page);
	}

      stats_env->stat_info->height++;
    }
  else
    {
      DB_VALUE key, elem;
      LEAF_REC leaf_rec;
      bool clear_key;
      int offset;
      int k;
      DB_MIDXKEY *midxkey;
      int prev_j_index, prev_k_index;
      char *prev_j_ptr, *prev_k_ptr;

      stats_env->stat_info->leafs++;
      stats_env->stat_info->keys += key_cnt;
      stats_env->stat_info->height = 1;	/* init */

      if (stats_env->pkeys)
	{
	  if (TP_DOMAIN_TYPE (key_type) != DB_TYPE_MIDXKEY)
	    {
	      /* single column index */
	      stats_env->stat_info->pkeys[0] += key_cnt;
	    }
	  else
	    {
	      for (i = 1; i <= key_cnt; i++)
		{
		  if (spage_get_record (page_ptr, i, &rec, PEEK) != S_SUCCESS)
		    {
		      goto exit_on_error;
		    }

		  /* read key-value */
		  btree_read_record (thread_p, page_ptr, btid, &rec, &key,
				     &leaf_rec, BTREE_LEAF_NODE, &clear_key,
				     &offset, PEEK_KEY_VALUE, NULL);

		  /* extract the sequence of the key-value */
		  midxkey = DB_GET_MIDXKEY (&key);
		  if (midxkey == NULL)
		    {
		      goto exit_on_error;
		    }

		  prev_j_index = 0;
		  prev_j_ptr = NULL;

		  assert (stats_env->stat_info->pkeys_size <=
			  BTREE_STATS_PKEYS_NUM);
		  for (j = 0; j < stats_env->stat_info->pkeys_size; j++)
		    {
		      /* extract the element of the midxkey */
		      ret = pr_midxkey_get_element_nocopy (midxkey, j, &elem,
							   &prev_j_index,
							   &prev_j_ptr);
		      if (ret != NO_ERROR)
			{
			  goto exit_on_error;
			}

		      if (tp_value_compare (&(stats_env->pkeys[j]), &elem,
					    0, 1) != DB_EQ)
			{
			  /* found different value */
			  stats_env->stat_info->pkeys[j] += 1;
			  pr_clear_value (&(stats_env->pkeys[j]));	/* clear saved */
			  pr_clone_value (&elem, &(stats_env->pkeys[j]));	/* save */

			  /* propagate to the following partial key-values */
			  prev_k_index = prev_j_index;
			  prev_k_ptr = prev_j_ptr;

			  assert (stats_env->stat_info->pkeys_size <=
				  BTREE_STATS_PKEYS_NUM);
			  for (k = j + 1;
			       k < stats_env->stat_info->pkeys_size; k++)
			    {
			      ret = pr_midxkey_get_element_nocopy (midxkey,
								   k,
								   &elem,
								   &prev_k_index,
								   &prev_k_ptr);
			      if (ret != NO_ERROR)
				{
				  goto exit_on_error;
				}

			      stats_env->stat_info->pkeys[k]++;
			      pr_clear_value (&(stats_env->pkeys[k]));
			      pr_clone_value (&elem, &(stats_env->pkeys[k]));	/* save */
			    }

			  /* go to the next key */
			  break;
			}
		    }

		  btree_clear_key_value (&clear_key, &key);
		}
	    }
	}
    }

  stats_env->stat_info->pages++;

  return ret;

exit_on_error:

  if (page)
    {
      pgbuf_unfix_and_init (thread_p, page);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}
#endif

/*
 * btree_get_stats_midxkey () -
 *   return: NO_ERROR
 *   thread_p(in);
 *   env(in/out): Structure to store and return the statistical information
 *   midxkey(in);
 */
static int
btree_get_stats_midxkey (THREAD_ENTRY * thread_p, BTREE_STATS_ENV * env,
			 DB_MIDXKEY * midxkey)
{
  int i, k;
  int prev_i_index, prev_k_index;
  char *prev_i_ptr, *prev_k_ptr;
  DB_VALUE elem;
  int ret = NO_ERROR;

  if (midxkey == NULL)
    {
      assert_release (false);
      goto exit_on_error;
    }

  prev_i_index = 0;
  prev_i_ptr = NULL;
  for (i = 0; i < env->pkeys_val_num; i++)
    {
      /* extract the element of the key */
      ret = pr_midxkey_get_element_nocopy (midxkey, i, &elem,
					   &prev_i_index, &prev_i_ptr);
      if (ret != NO_ERROR)
	{
	  assert_release (false);
	  goto exit_on_error;
	}

      if (tp_value_compare (&(env->pkeys_val[i]), &elem, 0, 1) != DB_EQ)
	{
	  /* found different value */
	  env->stat_info->pkeys[i]++;
	  pr_clear_value (&(env->pkeys_val[i]));	/* clear saved */
	  pr_clone_value (&elem, &(env->pkeys_val[i]));	/* save */

	  /* propagate to the following partial key-values */
	  prev_k_index = prev_i_index;
	  prev_k_ptr = prev_i_ptr;
	  for (k = i + 1; k < env->pkeys_val_num; k++)
	    {
	      ret = pr_midxkey_get_element_nocopy (midxkey,
						   k,
						   &elem,
						   &prev_k_index,
						   &prev_k_ptr);
	      if (ret != NO_ERROR)
		{
		  assert_release (false);
		  goto exit_on_error;
		}

	      env->stat_info->pkeys[k]++;
	      pr_clear_value (&(env->pkeys_val[k]));	/* clear saved */
	      pr_clone_value (&elem, &(env->pkeys_val[k]));	/* save */
	    }

	  break;
	}
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_get_stats_key () -
 *   return: NO_ERROR
 *   thread_p(in);
 *   env(in/out): Structure to store and return the statistical information
 */
static int
btree_get_stats_key (THREAD_ENTRY * thread_p, BTREE_STATS_ENV * env,
		     MVCC_SNAPSHOT * mvcc_snapshot)
{
  BTREE_SCAN *BTS;
  RECDES rec;
  DB_VALUE key_value;
  LEAF_REC leaf_pnt;
  bool clear_key = false;
  int offset;
  int ret = NO_ERROR;

  assert (env != NULL);

  db_make_null (&key_value);

  if (mvcc_snapshot != NULL)
    {
      BTREE_SEARCH result = BTREE_KEY_NOTFOUND;
      int max_visible_oids = 1;
      int num_visible_oids = 0;

      BTS = &(env->btree_scan);

      if (BTS->C_page == NULL)
	{
	  goto exit_on_error;
	}

      assert (BTS->slot_id > 0);
      if (spage_get_record (BTS->C_page, BTS->slot_id, &rec, PEEK) !=
	  S_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* filter out fence_key */
      if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
	{
	  goto count_keys;
	}

      /* read key-value */
      assert (clear_key == false);

      if (btree_read_record (thread_p, &BTS->btid_int, BTS->C_page, &rec,
			     &key_value, (void *) &leaf_pnt, BTREE_LEAF_NODE,
			     &clear_key, &offset, PEEK_KEY_VALUE,
			     NULL) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* Is there any visible objects? */
      max_visible_oids = 1;
      num_visible_oids =
	btree_get_num_visible_from_leaf_and_ovf (thread_p, &BTS->btid_int,
						 &rec, offset, &leaf_pnt,
						 &max_visible_oids,
						 mvcc_snapshot);
      if (num_visible_oids < 0)
	{
	  /* Error. */
	  goto exit_on_error;
	}
      else if (num_visible_oids == 0)
	{
	  /* No visible object. */
	  goto end;
	}
    }

count_keys:
  env->stat_info->keys++;

  if (env->pkeys_val_num <= 0)
    {
      ;				/* do not request pkeys info; go ahead */
    }
  else if (env->pkeys_val_num == 1)
    {
      /* single column index */
      env->stat_info->pkeys[0]++;
    }
  else
    {
      /* multi column index */

      if (mvcc_snapshot != NULL)
	{
	  /* filter out fence_key */
	  if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
	    {
	      assert (ret == NO_ERROR);

	      env->stat_info->keys--;
	      assert (env->stat_info->keys >= 0);

	      goto end;
	    }

	  /* key_value already computed */
	  assert (!DB_IS_NULL (&key_value));

	  /* get pkeys info */
	  ret = btree_get_stats_midxkey (thread_p, env,
					 DB_GET_MIDXKEY (&key_value));
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  goto end;
	}

      BTS = &(env->btree_scan);

      if (BTS->C_page == NULL)
	{
	  goto exit_on_error;
	}

      assert (BTS->slot_id > 0);
      if (spage_get_record (BTS->C_page, BTS->slot_id, &rec, PEEK) !=
	  S_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* filter out fence_key */
      if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
	{
	  assert (ret == NO_ERROR);

	  env->stat_info->keys--;
	  assert (env->stat_info->keys >= 0);

	  goto end;
	}

      /* read key-value */

      assert (clear_key == false);

      if (btree_read_record (thread_p, &BTS->btid_int, BTS->C_page, &rec,
			     &key_value, (void *) &leaf_pnt, BTREE_LEAF_NODE,
			     &clear_key, &offset, PEEK_KEY_VALUE,
			     NULL) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* get pkeys info */
      ret = btree_get_stats_midxkey (thread_p, env,
				     DB_GET_MIDXKEY (&key_value));
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

end:

  if (clear_key)
    {
      pr_clear_value (&key_value);
      clear_key = false;
    }

  return ret;

exit_on_error:

  ret = (ret == NO_ERROR
	 && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;

  goto end;
}

/*
 * btree_get_stats_with_AR_sampling () - Do Acceptance/Rejection Sampling
 *   return: NO_ERROR
 *   env(in/out): Structure to store and return the statistical information
 */
static int
btree_get_stats_with_AR_sampling (THREAD_ENTRY * thread_p,
				  BTREE_STATS_ENV * env)
{
  BTREE_SCAN *BTS;
  int n, i;
  bool found;
  int key_cnt;
  int exp_ratio;
  int ret = NO_ERROR;
#if !defined(NDEBUG)
  BTREE_NODE_HEADER *header = NULL;
#endif

  assert (env != NULL);
  assert (env->stat_info != NULL);

  BTS = &(env->btree_scan);
  BTS->use_desc_index = 0;	/* init */

  for (n = 0; n < STATS_SAMPLING_THRESHOLD; n++)
    {
      if (env->stat_info->leafs >= STATS_SAMPLING_LEAFS_MAX)
	{
	  break;		/* found all samples */
	}

      BTS->C_page = btree_find_AR_sampling_leaf (thread_p,
						 BTS->btid_int.sys_btid,
						 &BTS->C_vpid,
						 env->stat_info, &found);
      if (BTS->C_page == NULL)
	{
	  goto exit_on_error;
	}

      /* found sampling leaf page */
      if (found)
	{
	  key_cnt = btree_node_number_of_keys (BTS->C_page);
	  assert_release (key_cnt >= 0);

#if !defined(NDEBUG)
	  header = btree_get_node_header (BTS->C_page);

	  assert (header != NULL);
	  assert (header->node_level == 1);	/* BTREE_LEAF_NODE */
#endif

	  if (key_cnt > 0)
	    {
	      env->stat_info->leafs++;

	      BTS->slot_id = 1;
	      BTS->oid_pos = 0;

	      assert_release (BTS->slot_id <= key_cnt);

	      for (i = 0; i < key_cnt; i++)
		{
		  ret = btree_get_stats_key (thread_p, env, NULL);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }

		  /* get the next index record */
		  ret = btree_find_next_index_record (thread_p, BTS);
		  if (ret != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		}
	    }
	}

      if (BTS->P_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, BTS->P_page);
	}

      if (BTS->C_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, BTS->C_page);
	}

      if (BTS->O_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, BTS->O_page);
	}
    }				/* for (n = 0; ... ) */

  /* apply distributed expension */
  if (env->stat_info->leafs > 0)
    {
      exp_ratio = env->stat_info->pages / env->stat_info->leafs;

      env->stat_info->leafs *= exp_ratio;
      if (env->stat_info->leafs < 0)
	{
	  env->stat_info->leafs = INT_MAX;
	}

      env->stat_info->keys *= exp_ratio;
      if (env->stat_info->keys < 0)
	{
	  env->stat_info->keys = INT_MAX;
	}

      for (i = 0; i < env->pkeys_val_num; i++)
	{
	  env->stat_info->pkeys[i] *= exp_ratio;
	  if (env->stat_info->pkeys[i] < 0)
	    {			/* multiply-overflow defence */
	      env->stat_info->pkeys[i] = INT_MAX;
	    }
	}
    }

end:

  if (BTS->P_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->P_page);
    }

  if (BTS->C_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->C_page);
    }

  if (BTS->O_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->O_page);
    }

  return ret;

exit_on_error:

  ret = (ret == NO_ERROR
	 && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;

  goto end;
}

/*
 * btree_get_stats_with_fullscan () - Do Full Scan
 *   return: NO_ERROR
 *   env(in/out): Structure to store and return the statistical information
 */
static int
btree_get_stats_with_fullscan (THREAD_ENTRY * thread_p, BTREE_STATS_ENV * env)
{
  BTREE_SCAN *BTS;
  VPID C_vpid;			/* vpid of current leaf page */
  int ret = NO_ERROR;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  assert (env != NULL);
  assert (env->stat_info != NULL);

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      ASSERT_ERROR_AND_SET (ret);
      goto exit_on_error;
    }

  BTS = &(env->btree_scan);
  BTS->use_desc_index = 0;	/* get the left-most leaf page */

  ret = btree_find_lower_bound_leaf (thread_p, BTS, env->stat_info);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  VPID_SET_NULL (&C_vpid);	/* init */

  while (!BTREE_END_OF_SCAN (BTS))
    {
      /* move on another leaf page */
      if (!VPID_EQ (&(BTS->C_vpid), &C_vpid))
	{
	  VPID_COPY (&C_vpid, &(BTS->C_vpid));	/* keep current leaf vpid */

	  env->stat_info->leafs++;
	}

      ret = btree_get_stats_key (thread_p, env, mvcc_snapshot);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}

      /* get the next index record */
      ret = btree_find_next_index_record (thread_p, BTS);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
    }

end:

  if (BTS->P_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->P_page);
    }

  if (BTS->C_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->C_page);
    }

  if (BTS->O_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->O_page);
    }

  return ret;

exit_on_error:

  assert_release (ret != NO_ERROR);

  goto end;
}

/*
 * btree_get_root_page () -
 *   return: pageid or NULL_PAGEID
 *   btid(in): B+tree index identifier
 *   first_vpid(out):
 *
 * Note: get the page identifier of the first allocated page of the given file.
 */
VPID *
btree_get_root_page (THREAD_ENTRY * thread_p, BTID * btid, VPID * root_vpid)
{
  VPID *vpid;

  assert (!VFID_ISNULL (&btid->vfid));

  vpid = file_get_first_alloc_vpid (thread_p, &btid->vfid, root_vpid);

  assert (!VPID_ISNULL (root_vpid));

  return vpid;
}

/*
 * btree_get_stats () - Get Statistical Information about the B+tree index
 *   return: NO_ERROR
 *   stat_info_p(in/out): Structure to store and
 *                        return the statistical information
 *   with_fullscan(in): true iff WITH FULLSCAN
 *
 * Note: Computes and returns statistical information about B+tree
 * which consist of the number of leaf pages, total number of
 * pages, number of keys and the height of the tree.
 */
int
btree_get_stats (THREAD_ENTRY * thread_p, BTREE_STATS * stat_info_p,
		 bool with_fullscan)
{
  int npages;
  BTREE_STATS_ENV stat_env, *env;
  VPID root_vpid;
  PAGE_PTR root_page_ptr = NULL;
  DB_TYPE dom_type;
  BTREE_ROOT_HEADER *root_header = NULL;
  int i;
  int ret = NO_ERROR;

  assert_release (stat_info_p != NULL);
  assert_release (!BTID_IS_NULL (&stat_info_p->btid));

  npages = file_get_numpages (thread_p, &(stat_info_p->btid.vfid));
  if (npages < 0)
    {
      npages = INT_MAX;
    }
  assert_release (npages >= 1);

  /* For the optimization of the sampling,
   * if the btree file has currently the same pages as we gathered
   * statistics, we guess the btree file has not been modified;
   * So, we take current stats as it is
   */
  if (!with_fullscan)
    {
      /* check if the stats has been gathered */
      if (stat_info_p->keys > 0)
	{
	  /* guess the stats has not been modified */
	  if (npages == stat_info_p->pages)
	    {
	      return NO_ERROR;
	    }
	}
    }

  /* set environment variable */
  env = &stat_env;
  BTREE_INIT_SCAN (&(env->btree_scan));
  env->btree_scan.btid_int.sys_btid = &(stat_info_p->btid);
  env->stat_info = stat_info_p;
  env->pkeys_val_num = stat_info_p->pkeys_size;

  assert (env->pkeys_val_num <= BTREE_STATS_PKEYS_NUM);
  for (i = 0; i < env->pkeys_val_num; i++)
    {
      DB_MAKE_NULL (&(env->pkeys_val[i]));
    }

  root_vpid.pageid = env->stat_info->btid.root_pageid;	/* read root page */
  root_vpid.volid = env->stat_info->btid.vfid.volid;

  root_page_ptr = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
  if (root_page_ptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, root_page_ptr, PAGE_BTREE);

  root_header = btree_get_root_header (root_page_ptr);
  if (root_header == NULL)
    {
      goto exit_on_error;
    }

  ret = btree_glean_root_header_info (thread_p, root_header,
				      &(env->btree_scan.btid_int));
  if (ret != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, root_page_ptr);
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, root_page_ptr);

  dom_type = TP_DOMAIN_TYPE (env->btree_scan.btid_int.key_type);
  if (env->pkeys_val_num <= 0)
    {
      /* do not request pkeys info; go ahead */
      if (!tp_valid_indextype (dom_type) && dom_type != DB_TYPE_MIDXKEY)
	{
	  assert_release (false);
	  goto exit_on_error;
	}
    }
  else if (env->pkeys_val_num == 1)
    {
      /* single column index */
      if (!tp_valid_indextype (dom_type))
	{
	  assert_release (false);
	  goto exit_on_error;
	}
    }
  else
    {
      /* multi column index */
      if (dom_type != DB_TYPE_MIDXKEY)
	{
	  assert_release (false);
	  goto exit_on_error;
	}
    }

  /* initialize environment stat_info structure */
  env->stat_info->pages = npages;
  env->stat_info->leafs = 0;
  env->stat_info->height = 0;
  env->stat_info->keys = 0;

  for (i = 0; i < env->pkeys_val_num; i++)
    {
      env->stat_info->pkeys[i] = 0;	/* clear old stats */
    }

  if (with_fullscan || npages <= STATS_SAMPLING_THRESHOLD)
    {
      /* do fullscan at small table */
      ret = btree_get_stats_with_fullscan (thread_p, env);
    }
  else
    {
      ret = btree_get_stats_with_AR_sampling (thread_p, env);
    }

  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* check for emptiness */
  for (i = 0; i < env->pkeys_val_num; i++)
    {
      assert_release (env->stat_info->keys >= env->stat_info->pkeys[i]);

      if (env->stat_info->keys <= 0)
	{
	  /* is empty */
	  assert_release (env->stat_info->pkeys[i] == 0);
	  env->stat_info->pkeys[i] = 0;
	}
      else
	{
	  env->stat_info->pkeys[i] = MAX (env->stat_info->pkeys[i], 1);
	}
    }

  /* check for leaf pages */
  env->stat_info->leafs = MAX (1, env->stat_info->leafs);
  env->stat_info->leafs = MIN (env->stat_info->leafs,
			       npages - (env->stat_info->height - 1));

  assert_release (env->stat_info->pages >= 1);
  assert_release (env->stat_info->leafs >= 1);
  assert_release (env->stat_info->height >= 1);
  assert_release (env->stat_info->keys >= 0);

end:

  if (root_page_ptr)
    {
      pgbuf_unfix_and_init (thread_p, root_page_ptr);
    }

  /* clear partial key-values */
  for (i = 0; i < env->pkeys_val_num; i++)
    {
      pr_clear_value (&(env->pkeys_val[i]));
    }

  mnt_bt_get_stats (thread_p);

  return ret;

exit_on_error:

  ret = (ret == NO_ERROR
	 && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;

  goto end;
}

/*
 * xbtree_get_key_type () - Obtains index key type.
 *
 * return	  : Error code.
 * thread_p (in)  : Thread entry
 * btid (in)	  : B-tree identifier.
 * key_type (out) : Index key type.
 */
int
xbtree_get_key_type (THREAD_ENTRY * thread_p, BTID btid,
		     TP_DOMAIN ** key_type)
{
  VPID root_vpid;
  PAGE_PTR root_page;
  BTREE_ROOT_HEADER *root_header = NULL;
  OR_BUF buf;

  root_vpid.pageid = btid.root_pageid;
  root_vpid.volid = btid.vfid.volid;

  assert (key_type != NULL);
  *key_type = NULL;

  root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (root_page == NULL)
    {
      return ER_FAILED;
    }

  root_header = btree_get_root_header (root_page);
  or_init (&buf, root_header->packed_key_domain, -1);
  *key_type = or_get_domain (&buf, NULL, NULL);

  pgbuf_unfix (thread_p, root_page);

  if (*key_type == NULL)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * btree_check_page_key () - Check (verify) page
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   btid(in):
 *   page_ptr(in): Page pointer
 *   page_vpid(in): Page identifier
 *
 * Note: Verifies the correctness of the specified page of the B+tree.
 * Tests include checking the order of the keys in the page,
 * checking the key count and maximum key length values stored page header.
 */
static DISK_ISVALID
btree_check_page_key (THREAD_ENTRY * thread_p, const OID * class_oid_p,
		      BTID_INT * btid, const char *btname,
		      PAGE_PTR page_ptr, VPID * page_vpid)
{
  int key_cnt, offset;
  RECDES peek_rec1, peek_rec2;
  DB_VALUE key1, key2;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  int k, overflow_key1 = 0, overflow_key2 = 0;
  bool clear_key1, clear_key2;
  LEAF_REC leaf_pnt;
  NON_LEAF_REC nleaf_pnt;
  DISK_ISVALID valid = DISK_ERROR;
  int c;
  char err_buf[LINE_MAX];

  /* initialize */
  leaf_pnt.key_len = 0;
  VPID_SET_NULL (&leaf_pnt.ovfl);
  nleaf_pnt.key_len = 0;
  VPID_SET_NULL (&nleaf_pnt.pnt);

  DB_MAKE_NULL (&key1);
  DB_MAKE_NULL (&key2);

  key_cnt = btree_node_number_of_keys (page_ptr);

  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      snprintf (err_buf, LINE_MAX, "btree_check_page_key: "
		"get node header failure: %d\n", key_cnt);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMERGENCY_ERROR, 1,
	      err_buf);
      valid = DISK_INVALID;
      goto error;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if ((node_type == BTREE_NON_LEAF_NODE && key_cnt <= 0)
      || (node_type == BTREE_LEAF_NODE && key_cnt < 0))
    {
      snprintf (err_buf, LINE_MAX, "btree_check_page_key: "
		"node key count underflow: %d\n", key_cnt);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMERGENCY_ERROR, 1,
	      err_buf);
      valid = DISK_INVALID;
      goto error;
    }

  if (key_cnt == 0)
    {
      return DISK_VALID;
    }

  if (key_cnt == 1)
    {
      /* there is only one key, so no order check */
      if (spage_get_record (page_ptr, 1, &peek_rec1, PEEK) != S_SUCCESS)
	{
	  valid = DISK_ERROR;
	  goto error;
	}

      return DISK_VALID;
    }

  for (k = 1; k < key_cnt; k++)
    {
      if (spage_get_record (page_ptr, k, &peek_rec1, PEEK) != S_SUCCESS)
	{
	  valid = DISK_ERROR;
	  goto error;
	}

      if (btree_leaf_is_flaged (&peek_rec1, BTREE_LEAF_RECORD_FENCE))
	{
	  continue;
	}

      /* read the current record key */
      if (node_type == BTREE_LEAF_NODE)
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &peek_rec1, &key1,
				 (void *) &leaf_pnt, BTREE_LEAF_NODE,
				 &clear_key1, &offset, PEEK_KEY_VALUE,
				 NULL) != NO_ERROR)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }
	  overflow_key1 = (leaf_pnt.key_len < 0);
	}
      else
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &peek_rec1, &key1,
				 (void *) &nleaf_pnt, BTREE_NON_LEAF_NODE,
				 &clear_key1, &offset, PEEK_KEY_VALUE,
				 NULL) != NO_ERROR)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }
	  overflow_key1 = (nleaf_pnt.key_len < 0);
	}

      if ((!overflow_key1
	   && (btree_get_disk_size_of_key (&key1) > header->max_key_len))
	  || (overflow_key1 && (DISK_VPID_SIZE > header->max_key_len)))
	{
	  btree_dump_page (thread_p, stdout, class_oid_p, btid, btname,
			   page_ptr, page_vpid, 2, 2);

	  snprintf (err_buf, LINE_MAX, "btree_check_page_key: "
		    "--- max key length test failed for page "
		    "{%d , %d}. Check key_rec = %d\n",
		    page_vpid->volid, page_vpid->pageid, k);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMERGENCY_ERROR, 1,
		  err_buf);
	  valid = DISK_INVALID;
	  goto error;
	}

      if (spage_get_record (page_ptr, k + 1, &peek_rec2, PEEK) != S_SUCCESS)
	{
	  valid = DISK_ERROR;
	  goto error;
	}

      if (btree_leaf_is_flaged (&peek_rec2, BTREE_LEAF_RECORD_FENCE))
	{
	  btree_clear_key_value (&clear_key1, &key1);
	  continue;
	}

      /* read the next record key */
      if (node_type == BTREE_LEAF_NODE)
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &peek_rec2, &key2,
				 (void *) &leaf_pnt, BTREE_LEAF_NODE,
				 &clear_key2, &offset, PEEK_KEY_VALUE,
				 NULL) != NO_ERROR)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }
	  overflow_key2 = (leaf_pnt.key_len < 0);
	}
      else
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &peek_rec2, &key2,
				 (void *) &nleaf_pnt, BTREE_NON_LEAF_NODE,
				 &clear_key2, &offset, PEEK_KEY_VALUE,
				 NULL) != NO_ERROR)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }
	  overflow_key2 = (nleaf_pnt.key_len < 0);
	}

      if ((!overflow_key2
	   && (btree_get_disk_size_of_key (&key2) > header->max_key_len))
	  || (overflow_key2 && (DISK_VPID_SIZE > header->max_key_len)))
	{
	  btree_dump_page (thread_p, stdout, class_oid_p, btid, btname,
			   page_ptr, page_vpid, 2, 2);

	  snprintf (err_buf, LINE_MAX, "btree_check_page_key: "
		    "--- max key length test failed for page "
		    "{%d , %d}. Check key_rec = %d\n",
		    page_vpid->volid, page_vpid->pageid, k + 1);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMERGENCY_ERROR, 1,
		  err_buf);
	  valid = DISK_INVALID;
	  goto error;
	}

      if (k == 1 && node_type == BTREE_NON_LEAF_NODE)
	{
	  c = DB_LT;		/* TODO - may compare with neg-inf sep */
	}
      else
	{
	  /* compare the keys for the order */
	  c = btree_compare_key (&key1, &key2, btid->key_type, 1, 1, NULL);
	}

      if (c != DB_LT)
	{
	  btree_dump_page (thread_p, stdout, class_oid_p, btid, btname,
			   page_ptr, page_vpid, 2, 2);

	  snprintf (err_buf, LINE_MAX, "btree_check_page_key:"
		    "--- key order test failed for page"
		    " {%d , %d}. Check key_recs = %d and %d\n",
		    page_vpid->volid, page_vpid->pageid, k, k + 1);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMERGENCY_ERROR, 1,
		  err_buf);
	  valid = DISK_INVALID;
	  goto error;
	}

      btree_clear_key_value (&clear_key1, &key1);
      btree_clear_key_value (&clear_key2, &key2);
    }

  /* page check passed */
  return DISK_VALID;

error:

  btree_clear_key_value (&clear_key1, &key1);
  btree_clear_key_value (&clear_key2, &key2);

  return valid;
}

/*
 * btree_verify_subtree () - Check (verify) a page and its subtrees
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   btid(in): B+tree index identifier
 *   pg_ptr(in): Page pointer for the subtree root page
 *   pg_vpid(in): Page identifier for the subtree root page
 *   INFO(in):
 *
 * Note: Verifies the correctness of the content of the given page
 * together with its subtree
 */
static DISK_ISVALID
btree_verify_subtree (THREAD_ENTRY * thread_p, const OID * class_oid_p,
		      BTID_INT * btid, const char *btname, PAGE_PTR pg_ptr,
		      VPID * pg_vpid, BTREE_NODE_INFO * INFO)
{
  int key_cnt;
  NON_LEAF_REC nleaf_ptr;
  VPID page_vpid;
  PAGE_PTR page = NULL;
  RECDES rec;
  DB_VALUE curr_key;
  int offset;
  bool clear_key = false;
  int i;
  DISK_ISVALID valid = DISK_ERROR;
  BTREE_NODE_INFO INFO2;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  char err_buf[LINE_MAX];

  db_make_null (&INFO2.max_key);

  /* test the page for the order of the keys within the page and get the
   * biggest key of this page
   */
  valid = btree_check_page_key (thread_p, class_oid_p, btid, btname, pg_ptr,
				pg_vpid);
  if (valid != DISK_VALID)
    {
      goto error;
    }

  key_cnt = btree_node_number_of_keys (pg_ptr);

  header = btree_get_node_header (pg_ptr);
  if (header == NULL)
    {
      valid = DISK_INVALID;
      goto error;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if ((node_type == BTREE_NON_LEAF_NODE && key_cnt <= 0)
      || (node_type == BTREE_LEAF_NODE && key_cnt < 0))
    {
      btree_dump_page (thread_p, stdout, class_oid_p, btid, btname,
		       pg_ptr, pg_vpid, 2, 2);
      snprintf (err_buf, LINE_MAX, "btree_verify_subtree: "
		"node key count underflow: %d\n", key_cnt);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMERGENCY_ERROR, 1,
	      err_buf);
      valid = DISK_INVALID;
      goto error;
    }

  /* initialize INFO structure */
  INFO->max_key_len = header->max_key_len;
  INFO->height = 0;
  INFO->tot_key_cnt = 0;
  INFO->page_cnt = 0;
  INFO->leafpg_cnt = 0;
  INFO->nleafpg_cnt = 0;
  db_make_null (&INFO->max_key);

  if (node_type == BTREE_NON_LEAF_NODE)
    {				/* a non-leaf page */
      if (key_cnt < 0)
	{
	  btree_dump_page (thread_p, stdout, class_oid_p, btid, btname,
			   pg_ptr, pg_vpid, 2, 2);

	  snprintf (err_buf, LINE_MAX, "btree_verify_subtree: "
		    "node key count underflow: %d\n", key_cnt);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EMERGENCY_ERROR, 1,
		  err_buf);
	  valid = DISK_INVALID;
	  goto error;
	}

      INFO2.key_area_len = 0;
      db_make_null (&INFO2.max_key);

      /* traverse all the subtrees of this non_leaf page and accumulate
       * the statistical data in the INFO structure
       */
      for (i = 1; i <= key_cnt; i++)
	{
	  if (spage_get_record (pg_ptr, i, &rec, PEEK) != S_SUCCESS)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }

	  if (btree_read_record (thread_p, btid, pg_ptr, &rec, &curr_key,
				 &nleaf_ptr, BTREE_NON_LEAF_NODE, &clear_key,
				 &offset, PEEK_KEY_VALUE, NULL) != NO_ERROR)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }

	  page_vpid = nleaf_ptr.pnt;

	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      valid = DISK_ERROR;
	      goto error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, page, PAGE_BTREE);

	  valid = btree_verify_subtree (thread_p, class_oid_p, btid, btname,
					page, &page_vpid, &INFO2);
	  if (valid != DISK_VALID)
	    {
	      goto error;
	    }

	  /* accumulate results */
	  INFO->height = INFO2.height + 1;
	  INFO->tot_key_cnt += INFO2.tot_key_cnt;
	  INFO->page_cnt += INFO2.page_cnt;
	  INFO->leafpg_cnt += INFO2.leafpg_cnt;
	  INFO->nleafpg_cnt += INFO2.nleafpg_cnt;

	  pgbuf_unfix_and_init (thread_p, page);
	  btree_clear_key_value (&clear_key, &curr_key);
	}
      INFO->page_cnt += 1;
      INFO->nleafpg_cnt += 1;
    }
  else
    {				/* a leaf page */
      /* form the INFO structure from the header information */
      INFO->height = 1;
      INFO->tot_key_cnt = key_cnt;
      INFO->page_cnt = 1;
      INFO->leafpg_cnt = 1;
      INFO->nleafpg_cnt = 0;
    }

  return DISK_VALID;

error:
  btree_clear_key_value (&clear_key, &curr_key);

  if (page)
    {
      pgbuf_unfix_and_init (thread_p, page);
    }

  return valid;
}

/*
 * btree_verify_tree () - Check (verify) tree
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   btid_int(in): B+tree index identifier
 *
 * Note: Verifies the correctness of the B+tree index . During tree
 * traversal,  several tests are  conducted, such as checking
 * the order of keys on a page or among pages that are in a
 * father-child relationship.
 */
DISK_ISVALID
btree_verify_tree (THREAD_ENTRY * thread_p, const OID * class_oid_p,
		   BTID_INT * btid_int, const char *btname)
{
  VPID p_vpid;
  PAGE_PTR root = NULL;
  BTREE_NODE_INFO INFO;
  DISK_ISVALID valid = DISK_ERROR;

  p_vpid.pageid = btid_int->sys_btid->root_pageid;	/* read root page */
  p_vpid.volid = btid_int->sys_btid->vfid.volid;
  root = pgbuf_fix (thread_p, &p_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      valid = DISK_ERROR;
      goto error;
    }

  (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

  /* traverse the tree and store the statistical data in the INFO structure */
  valid =
    btree_verify_subtree (thread_p, class_oid_p, btid_int, btname, root,
			  &p_vpid, &INFO);
  if (valid != DISK_VALID)
    {
      goto error;
    }

  pgbuf_unfix_and_init (thread_p, root);

  return DISK_VALID;

error:

  if (root)
    {
      pgbuf_unfix_and_init (thread_p, root);
    }

  return valid;
}

/*
 *       		 db_check consistency routines
 */

/*
 * btree_check_pages () -
 *   return: DISK_VALID, DISK_VALID or DISK_ERROR
 *   btid(in): B+tree index identifier
 *   pg_ptr(in): Page pointer
 *   pg_vpid(in): Page identifier
 *
 * Note: Verify that given page and all its subpages are valid.
 */
static DISK_ISVALID
btree_check_pages (THREAD_ENTRY * thread_p, BTID_INT * btid,
		   PAGE_PTR pg_ptr, VPID * pg_vpid)
{
  VPID page_vpid;		/* Child page identifier */
  PAGE_PTR page = NULL;		/* Child page pointer */
  RECDES rec;			/* Record descriptor for page node records */
  DISK_ISVALID vld = DISK_ERROR;	/* Validity return code from subtree */
  int key_cnt;			/* Number of keys in the page */
  int i;			/* Loop counter */
  NON_LEAF_REC nleaf;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;

  /* Verify the given page */
  vld = file_isvalid_page_partof (thread_p, pg_vpid, &btid->sys_btid->vfid);
  if (vld != DISK_VALID)
    {
      goto error;
    }

#ifdef SPAGE_DEBUG
  if (spage_check (thread_p, pg_ptr) != NO_ERROR)
    {
      vld = DISK_ERROR;
      goto error;
    }
#endif /* SPAGE_DEBUG */

  /* Verify subtree child pages */

  key_cnt = btree_node_number_of_keys (pg_ptr);

  header = btree_get_node_header (pg_ptr);
  if (header == NULL)
    {
      vld = DISK_ERROR;
      goto error;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if (node_type == BTREE_NON_LEAF_NODE)
    {				/* non-leaf page */
      for (i = 1; i <= key_cnt; i++)
	{
	  if (spage_get_record (pg_ptr, i, &rec, PEEK) != S_SUCCESS)
	    {
	      vld = DISK_ERROR;
	      goto error;
	    }
	  btree_read_fixed_portion_of_non_leaf_record (&rec, &nleaf);
	  page_vpid = nleaf.pnt;

	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      vld = DISK_ERROR;
	      goto error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, page, PAGE_BTREE);

	  vld = btree_check_pages (thread_p, btid, page, &page_vpid);
	  if (vld != DISK_VALID)
	    {
	      goto error;
	    }
	  pgbuf_unfix_and_init (thread_p, page);
	}
    }

  return DISK_VALID;

error:

  if (page)
    {
      pgbuf_unfix_and_init (thread_p, page);
    }
  return vld;

}

/*
 * btree_check_tree () -
 *   return: DISK_VALID, DISK_INVALID or DISK_ERROR
 *   btid(in): B+tree index identifier
 *
 * Note: Verify that all the pages of the specified index are valid.
 */
DISK_ISVALID
btree_check_tree (THREAD_ENTRY * thread_p, const OID * class_oid_p,
		  BTID * btid, const char *btname)
{
  DISK_ISVALID valid = DISK_ERROR;
  VPID r_vpid;			/* root page identifier */
  PAGE_PTR r_pgptr = NULL;	/* root page pointer */
  BTID_INT btid_int;
  BTREE_ROOT_HEADER *root_header = NULL;

  /* Fetch the root page */
  r_vpid.pageid = btid->root_pageid;
  r_vpid.volid = btid->vfid.volid;
  r_pgptr = pgbuf_fix (thread_p, &r_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		       PGBUF_UNCONDITIONAL_LATCH);
  if (r_pgptr == NULL)
    {
      valid = DISK_ERROR;
      goto error;
    }

  (void) pgbuf_check_page_ptype (thread_p, r_pgptr, PAGE_BTREE);

  root_header = btree_get_root_header (r_pgptr);
  if (root_header == NULL)
    {
      valid = DISK_ERROR;
      goto error;
    }

  btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (thread_p, root_header, &btid_int) !=
      NO_ERROR)
    {
      goto error;
    }

  valid = btree_check_pages (thread_p, &btid_int, r_pgptr, &r_vpid);
  if (valid != DISK_VALID)
    {
      goto error;
    }

  pgbuf_unfix_and_init (thread_p, r_pgptr);

  /* Now check for the logical correctness of the tree */
  return btree_verify_tree (thread_p, class_oid_p, &btid_int, btname);

error:

  if (r_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, r_pgptr);
    }
  return valid;
}

/*
 * btree_check_by_btid () -
 *   btid(in): B+tree index identifier
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *
 * Note: Verify that all pages of a btree indices are valid.
 */
DISK_ISVALID
btree_check_by_btid (THREAD_ENTRY * thread_p, BTID * btid)
{
  DISK_ISVALID valid = DISK_ERROR;
  char area[FILE_DUMP_DES_AREA_SIZE];
  char *fd = area;
  int fd_size = FILE_DUMP_DES_AREA_SIZE, size;
  FILE_BTREE_DES *btree_des;
  char *btname;
  VPID vpid;

  size = file_get_descriptor (thread_p, &btid->vfid, fd, fd_size);
  if (size < 0)
    {
      fd_size = -size;
      fd = (char *) db_private_alloc (thread_p, fd_size);
      if (fd == NULL)
	{
	  fd = area;
	  fd_size = FILE_DUMP_DES_AREA_SIZE;
	}
      else
	{
	  size = file_get_descriptor (thread_p, &btid->vfid, fd, fd_size);
	}
    }
  btree_des = (FILE_BTREE_DES *) fd;

  /* get the index name of the index key */
  if (heap_get_indexinfo_of_btid (thread_p, &(btree_des->class_oid),
				  btid, NULL, NULL, NULL,
				  NULL, &btname, NULL) != NO_ERROR)
    {
      goto exit_on_end;
    }

  if (btree_get_root_page (thread_p, btid, &vpid) == NULL)
    {
      goto exit_on_end;
    }

  btid->root_pageid = vpid.pageid;

  valid = btree_check_tree (thread_p, &(btree_des->class_oid), btid, btname);

exit_on_end:
  if (fd != area)
    {
      db_private_free_and_init (thread_p, fd);
    }
  if (btname)
    {
      free_and_init (btname);
    }

  return valid;
}

int
btree_get_pkey_btid (THREAD_ENTRY * thread_p, OID * cls_oid, BTID * pkey_btid)
{
  OR_CLASSREP *cls_repr;
  OR_INDEX *curr_idx;
  int cache_idx = -1;
  int i;
  int error = NO_ERROR;

  assert (pkey_btid != NULL);

  BTID_SET_NULL (pkey_btid);

  cls_repr =
    heap_classrepr_get (thread_p, cls_oid, NULL, NULL_REPRID, &cache_idx);
  if (cls_repr == NULL)
    {
      ASSERT_ERROR_AND_SET (error);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  for (i = 0, curr_idx = cls_repr->indexes; i < cls_repr->n_indexes;
       i++, curr_idx++)
    {
      if (curr_idx == NULL)
	{
	  error = ER_UNEXPECTED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  break;
	}

      if (curr_idx->type == BTREE_PRIMARY_KEY)
	{
	  BTID_COPY (pkey_btid, &curr_idx->btid);
	  break;
	}
    }

  if (cls_repr != NULL)
    {
      heap_classrepr_free (cls_repr, &cache_idx);
    }

  return error;
}

/*
 * btree_check_by_class_oid () -
 *   cls_oid(in):
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *
 */
DISK_ISVALID
btree_check_by_class_oid (THREAD_ENTRY * thread_p, OID * cls_oid,
			  BTID * idx_btid)
{
  OR_CLASSREP *cls_repr;
  OR_INDEX *curr;
  BTID btid;
  int i;
  int cache_idx = -1;
  DISK_ISVALID rv = DISK_VALID;

  cls_repr = heap_classrepr_get (thread_p, cls_oid, NULL,
				 NULL_REPRID, &cache_idx);
  if (cls_repr == NULL)
    {
      ASSERT_ERROR ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_errid (), 0);
      return DISK_ERROR;
    }

  for (i = 0, curr = cls_repr->indexes; i < cls_repr->n_indexes; i++, curr++)
    {
      if (curr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 0);
	  rv = DISK_ERROR;
	  break;
	}

      if (idx_btid != NULL && !BTID_IS_EQUAL (&curr->btid, idx_btid))
	{
	  continue;
	}

      BTID_COPY (&btid, &curr->btid);
      if (btree_check_by_btid (thread_p, &btid) != DISK_VALID)
	{
	  rv = DISK_ERROR;
	  break;
	}
    }

  if (cls_repr)
    {
      heap_classrepr_free (cls_repr, &cache_idx);
    }

  return rv;
}

/*
 * btree_repair_prev_link_by_btid () -
 *   btid(in) :
 *   repair(in) :
 *   index_name(in) :
 *   return:
 */
static DISK_ISVALID
btree_repair_prev_link_by_btid (THREAD_ENTRY * thread_p, BTID * btid,
				bool repair, char *index_name)
{
  PAGE_PTR current_pgptr, next_pgptr, root_pgptr;
  VPID current_vpid, next_vpid;
  int valid = DISK_VALID;
  int request_mode;
  int retry_count = 0;
  int retry_max = 20;
  char output[LINE_MAX];
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;

  VPID_SET_NULL (&next_vpid);

  snprintf (output, LINE_MAX, "%s - %s... ",
	    repair ? "repair index" : "check index", index_name);
  xcallback_console_print (thread_p, output);

  current_pgptr = NULL;
  next_pgptr = NULL;
  root_pgptr = NULL;

  request_mode = repair ? PGBUF_LATCH_WRITE : PGBUF_LATCH_READ;

  /* root page */
  VPID_SET (&current_vpid, btid->vfid.volid, btid->root_pageid);
  root_pgptr = pgbuf_fix (thread_p, &current_vpid, OLD_PAGE, request_mode,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (root_pgptr == NULL)
    {
      valid = DISK_ERROR;
      goto exit_repair;
    }

  (void) pgbuf_check_page_ptype (thread_p, root_pgptr, PAGE_BTREE);

retry_repair:
  if (retry_count >= retry_max)
    {
      valid = DISK_ERROR;
      goto exit_repair;
    }

#if defined(SERVER_MODE)
  if (retry_count > 0)
    {
      thread_sleep (10);
    }
#endif

  if (current_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, current_pgptr);
    }
  if (next_pgptr)
    {
      pgbuf_unfix_and_init (thread_p, next_pgptr);
    }

  while (!VPID_ISNULL (&current_vpid))
    {
      current_pgptr = pgbuf_fix (thread_p, &current_vpid, OLD_PAGE,
				 request_mode, PGBUF_CONDITIONAL_LATCH);
      if (current_pgptr == NULL)
	{
	  retry_count++;
	  goto retry_repair;
	}

      (void) pgbuf_check_page_ptype (thread_p, current_pgptr, PAGE_BTREE);

      header = btree_get_node_header (current_pgptr);
      if (header == NULL)
	{
	  goto exit_repair;
	}

      node_type =
	(header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

      if (node_type == BTREE_LEAF_NODE)
	{
	  next_vpid = header->next_vpid;
	  break;
	}
      else
	{
	  RECDES rec;
	  NON_LEAF_REC non_leaf_rec;

	  if (spage_get_record (current_pgptr, 1, &rec, PEEK) != S_SUCCESS)
	    {
	      valid = DISK_ERROR;
	      goto exit_repair;
	    }
	  btree_read_fixed_portion_of_non_leaf_record (&rec, &non_leaf_rec);
	  current_vpid = non_leaf_rec.pnt;
	  pgbuf_unfix_and_init (thread_p, current_pgptr);
	}
    }

  assert (node_type == BTREE_LEAF_NODE);
  assert (header != NULL);

  while (!VPID_ISNULL (&next_vpid))
    {
      next_pgptr = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE,
			      request_mode, PGBUF_CONDITIONAL_LATCH);
      if (next_pgptr == NULL)
	{
	  retry_count++;
	  goto retry_repair;
	}

      (void) pgbuf_check_page_ptype (thread_p, next_pgptr, PAGE_BTREE);

      header = btree_get_node_header (next_pgptr);
      if (header == NULL)
	{
	  goto exit_repair;
	}

      if (!VPID_EQ (&header->prev_vpid, &current_vpid))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BTREE_CORRUPT_PREV_LINK, 3, index_name,
		  next_vpid.volid, next_vpid.pageid);

	  if (repair)
	    {
	      BTID_INT bint;

	      log_start_system_op (thread_p);
	      bint.sys_btid = btid;
	      if (btree_set_vpid_previous_vpid (thread_p, &bint, next_pgptr,
						&current_vpid) != NO_ERROR)
		{
		  valid = DISK_ERROR;
		  log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
		  goto exit_repair;
		}
	      valid = DISK_INVALID;
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		      ER_BTREE_REPAIR_PREV_LINK, 3, index_name,
		      next_vpid.volid, next_vpid.pageid);
	    }
	  else
	    {
	      valid = DISK_INVALID;
	      goto exit_repair;
	    }
	}
      pgbuf_unfix_and_init (thread_p, current_pgptr);

      /* move to next page */
      current_vpid = next_vpid;
      next_vpid = header->next_vpid;
      current_pgptr = next_pgptr;
      next_pgptr = NULL;
    }

exit_repair:
  if (root_pgptr)
    {
      pgbuf_unfix (thread_p, root_pgptr);
    }
  if (current_pgptr)
    {
      pgbuf_unfix (thread_p, current_pgptr);
    }
  if (next_pgptr)
    {
      pgbuf_unfix (thread_p, next_pgptr);
    }

  if (valid == DISK_ERROR)
    {
      xcallback_console_print (thread_p, (char *) "error\n");
    }
  else if (valid == DISK_VALID)
    {
      xcallback_console_print (thread_p, (char *) "pass\n");
    }
  else
    {
      if (repair)
	{
	  xcallback_console_print (thread_p, (char *) "repaired\n");
	}
      else
	{
	  xcallback_console_print (thread_p, (char *) "repair needed\n");
	}
    }

  return valid;
}

/*
 * btree_repair_prev_link_by_class_oid () -
 *   oid(in) :
 *   repair(in) :
 *   return:
 */
static DISK_ISVALID
btree_repair_prev_link_by_class_oid (THREAD_ENTRY * thread_p, OID * oid,
				     BTID * index_btid, bool repair)
{
  OR_CLASSREP *cls_repr;
  OR_INDEX *curr;
  int i;
  int cache_idx = -1;
  DISK_ISVALID valid = DISK_VALID;
  char *index_name;

  cls_repr = heap_classrepr_get (thread_p, oid, NULL,
				 NULL_REPRID, &cache_idx);

  if (cls_repr == NULL)
    {
      return DISK_ERROR;
    }

  for (i = 0, curr = cls_repr->indexes;
       i < cls_repr->n_indexes && curr && valid == DISK_VALID; i++, curr++)
    {
      if (index_btid != NULL && !BTID_IS_EQUAL (&curr->btid, index_btid))
	{
	  continue;
	}

      heap_get_indexinfo_of_btid (thread_p, oid, &curr->btid, NULL, NULL,
				  NULL, NULL, &index_name, NULL);
      valid =
	btree_repair_prev_link_by_btid (thread_p, &curr->btid, repair,
					index_name);
      if (index_name)
	{
	  free_and_init (index_name);
	}
    }

  if (cls_repr)
    {
      heap_classrepr_free (cls_repr, &cache_idx);
    }

  return valid;
}

/*
 * btree_repair_prev_link () -
 *   oid(in) :
 *   index_btid(in) :
 *   repair(in) :
 *   return:
 */
DISK_ISVALID
btree_repair_prev_link (THREAD_ENTRY * thread_p, OID * oid, BTID * index_btid,
			bool repair)
{
  int num_files;
  BTID btid;
  FILE_TYPE file_type;
  DISK_ISVALID valid;
  int i;
  char *index_name;
  VPID vpid;

  if (oid != NULL && !OID_ISNULL (oid))
    {
      return btree_repair_prev_link_by_class_oid (thread_p, oid, index_btid,
						  repair);
    }

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files < 0)
    {
      return DISK_ERROR;
    }

  valid = DISK_VALID;

  /* Go to each file, check only the btree files */
  for (i = 0; i < num_files && valid != DISK_ERROR; i++)
    {
      INT64 fix_count = 0;
      char area[FILE_DUMP_DES_AREA_SIZE];
      char *fd = area;
      int fd_size = FILE_DUMP_DES_AREA_SIZE, size;
      FILE_BTREE_DES *btree_des;

      if (file_find_nthfile (thread_p, &btid.vfid, i) != 1)
	{
	  valid = DISK_ERROR;
	  break;
	}

      file_type = file_get_type (thread_p, &btid.vfid);
      if (file_type == FILE_UNKNOWN_TYPE)
	{
	  valid = DISK_ERROR;
	  break;
	}

      if (file_type != FILE_BTREE)
	{
	  continue;
	}

      size = file_get_descriptor (thread_p, &btid.vfid, fd, fd_size);
      if (size < 0)
	{
	  fd_size = -size;
	  fd = (char *) malloc (fd_size);
	  if (fd == NULL)
	    {
	      return DISK_ERROR;
	    }
	  size = file_get_descriptor (thread_p, &btid.vfid, fd, fd_size);
	}
      btree_des = (FILE_BTREE_DES *) fd;

      /* get the index name of the index key */
      if (heap_get_indexinfo_of_btid (thread_p, &(btree_des->class_oid),
				      &btid, NULL, NULL, NULL,
				      NULL, &index_name, NULL) != NO_ERROR)
	{
	  if (fd != area)
	    {
	      free_and_init (fd);
	    }
	  return DISK_ERROR;
	}

      if (btree_get_root_page (thread_p, &btid, &vpid) == NULL)
	{
	  if (fd != area)
	    {
	      free_and_init (fd);
	    }
	  if (index_name)
	    {
	      free_and_init (index_name);
	    }
	  return DISK_ERROR;
	}

      btid.root_pageid = vpid.pageid;
      valid = btree_repair_prev_link_by_btid (thread_p, &btid, repair,
					      index_name);

      if (fd != area)
	{
	  free_and_init (fd);
	}
      if (index_name)
	{
	  free_and_init (index_name);
	}
    }

  return valid;
}

/*
 * btree_check_all () -
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *
 * Note: Verify that all pages of all btree indices are valid.
 */
DISK_ISVALID
btree_check_all (THREAD_ENTRY * thread_p)
{
  int num_files;		/* Number of files in the system */
  int curr_num_files;
  DISK_ISVALID valid, allvalid;	/* Validation return code        */
  FILE_TYPE file_type;		/* TYpe of file                  */
  int i;			/* Loop counter                  */
  int error_code;
  VPID vpid;
  VFID *trk_vfid;
  BTID btid;
  FILE_BTREE_DES btdes;
  PAGE_PTR trk_fhdr_pgptr = NULL;
  HEAP_SCANCACHE scan_cache;
  RECDES peek_recdes;

  trk_vfid = file_get_tracker_vfid ();

  /* check file tracker */
  if (trk_vfid == NULL)
    {
      goto exit_on_error;
    }

  vpid.volid = trk_vfid->volid;
  vpid.pageid = trk_vfid->fileid;

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files < 0)
    {
      goto exit_on_error;
    }

  allvalid = DISK_VALID;
  /* Go to each file, check only the btree files */
  for (i = 0; i < num_files && allvalid != DISK_ERROR; i++)
    {
      /* check # of file, check file type, get file descriptor */
      trk_fhdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				  PGBUF_LATCH_READ,
				  PGBUF_UNCONDITIONAL_LATCH);
      if (trk_fhdr_pgptr == NULL)
	{
	  goto exit_on_error;
	}

      curr_num_files = file_get_numfiles (thread_p);
      if (curr_num_files <= i)
	{
	  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);
	  break;
	}

      if (file_find_nthfile (thread_p, &btid.vfid, i) != 1)
	{
	  goto exit_on_error;
	}

      file_type = file_get_type (thread_p, &btid.vfid);
      if (file_type == FILE_UNKNOWN_TYPE)
	{
	  goto exit_on_error;
	}

      if (file_type != FILE_BTREE)
	{
	  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);
	  continue;
	}

      if ((file_get_descriptor (thread_p, &btid.vfid, &btdes,
				sizeof (FILE_BTREE_DES)) <= 0)
	  || OID_ISNULL (&btdes.class_oid))
	{
	  pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);
	  continue;
	}
      pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);

      if (lock_object (thread_p, &btdes.class_oid, oid_Root_class_oid,
		       IS_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
	{
	  goto exit_on_error;
	}

      error_code =
	heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);
      if (error_code != NO_ERROR)
	{
	  lock_unlock_object (thread_p, &btdes.class_oid, oid_Root_class_oid,
			      IS_LOCK, true);
	  goto exit_on_error;
	}

      /* Check heap file is really exist. It can be removed. */
      if (heap_get (thread_p, &btdes.class_oid, &peek_recdes, &scan_cache,
		    PEEK, NULL_CHN) != S_SUCCESS)
	{
	  heap_scancache_end (thread_p, &scan_cache);
	  lock_unlock_object (thread_p, &btdes.class_oid, oid_Root_class_oid,
			      IS_LOCK, true);
	  continue;
	}
      heap_scancache_end (thread_p, &scan_cache);

      /* Check BTree file */
      valid = btree_check_by_btid (thread_p, &btid);
      if (valid != DISK_VALID)
	{
	  allvalid = valid;
	}

      lock_unlock_object (thread_p, &btdes.class_oid, oid_Root_class_oid,
			  IS_LOCK, true);
    }

  assert (trk_fhdr_pgptr == NULL);

  return allvalid;

exit_on_error:
  if (trk_fhdr_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, trk_fhdr_pgptr);
    }

  return ((allvalid == DISK_VALID) ? DISK_ERROR : allvalid);
}

/*
 * btree_keyoid_checkscan_start () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   btscan(out): Set to key-oid check scan structure.
 *
 * Note: Start a <key-oid> check scan on the index.
 */
int
btree_keyoid_checkscan_start (THREAD_ENTRY * thread_p, BTID * btid,
			      BTREE_CHECKSCAN * btscan)
{
  assert (btid != NULL);

  /* initialize scan structure */
  btscan->btid = *btid;
  BTREE_INIT_SCAN (&btscan->btree_scan);

  /* Initialize OID list. */
  btscan->oid_list.oidp = (OID *) os_malloc (ISCAN_OID_BUFFER_CAPACITY);
  if (btscan->oid_list.oidp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, ISCAN_OID_BUFFER_CAPACITY);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  btscan->oid_list.oid_cnt = 0;
  btscan->oid_list.capacity = ISCAN_OID_BUFFER_CAPACITY / OR_OID_SIZE;
  btscan->oid_list.max_oid_cnt = btscan->oid_list.capacity;
  btscan->oid_list.next_list = NULL;

  return NO_ERROR;
}

/*
 * btree_keyoid_checkscan_check () -
 *   return: either: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   btscan(in): B+tree key-oid check scan structure.
 *   cls_oid(in):
 *   key(in): Key pointer
 *   oid(in): Object identifier for the key
 *
 * Note: Check if the given key-oid pair exists in the index.
 */
DISK_ISVALID
btree_keyoid_checkscan_check (THREAD_ENTRY * thread_p,
			      BTREE_CHECKSCAN * btscan, OID * cls_oid,
			      DB_VALUE * key, OID * oid)
{
  int k;			/* Loop iteration variable */
  INDX_SCAN_ID isid;
  DISK_ISVALID status;
  KEY_VAL_RANGE key_val_range;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      ASSERT_ERROR ();
      return DISK_INVALID;
    }

  /* initialize scan structure */
  BTREE_INIT_SCAN (&btscan->btree_scan);

  scan_init_index_scan (&isid, &btscan->oid_list, mvcc_snapshot);

  assert (!pr_is_set_type (DB_VALUE_DOMAIN_TYPE (key)));

  PR_SHARE_VALUE (key, &key_val_range.key1);
  PR_SHARE_VALUE (key, &key_val_range.key2);
  key_val_range.range = GE_LE;
  key_val_range.num_index_term = 0;

  do
    {
      /* search index */
      btscan->oid_list.oid_cnt =
	btree_keyval_search (thread_p, &btscan->btid, S_SELECT,
			     &btscan->btree_scan, &key_val_range, cls_oid,
			     NULL, &isid, false);
      assert (btscan->oid_list.oid_cnt <= btscan->oid_list.capacity);

      if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY
	  && key->data.midxkey.domain == NULL)
	{
	  /* set the appropriate domain, as it might be needed for printing
	   * if the given key-oid pair does not exist in the index. */
	  key->data.midxkey.domain = btscan->btree_scan.btid_int.key_type;
	}

      if (btscan->oid_list.oid_cnt < 0)
	{
	  status = DISK_ERROR;
	  goto end;
	}

      /* search current set of OIDs to see if given <key-oid> pair exists */
      for (k = 0; k < btscan->oid_list.oid_cnt; k++)
	{
	  if (OID_EQ (&btscan->oid_list.oidp[k], oid))
	    {			/* <key-oid> pair found */
	      status = DISK_VALID;
	      goto end;
	    }
	}
    }
  while (!BTREE_END_OF_SCAN (&btscan->btree_scan));

  /* indicate <key_oid> pair is not found */
  status = DISK_INVALID;

end:

  btree_scan_clear_key (&btscan->btree_scan);

  /* do not use copy_buf for key-val scan, only use for key-range scan */

  return status;
}

/*
 * btree_keyoid_checkscan_end () -
 *   return:
 *   btscan(in): B+tree key-oid check scan structure.
 *
 * Note: End the <key-oid> check scan on the index.
 */
void
btree_keyoid_checkscan_end (THREAD_ENTRY * thread_p, BTREE_CHECKSCAN * btscan)
{
  /* Deallocate allocated areas */
  if (btscan->oid_list.oidp)
    {
      os_free_and_init (btscan->oid_list.oidp);
      btscan->oid_list.capacity = 0;
      btscan->oid_list.max_oid_cnt = 0;
    }
}

/*
 *       		     b+tree space routines
 */

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * btree_estimate_total_numpages () -
 *   return: int
 *   dis_key_cnt(in): Distinct number of key values
 *   avg_key_len(in): Average key length
 *   tot_val_cnt(in): Total value count
 *   blt_pgcnt_est(out): Set to index built(not-loaded) total page cnt estimate
 *   blt_wrs_pgcnt_est(out): Set to index built(not-loaded) worst case pgcnt
 *                           estimate
 *
 * Note: Estimate and return total number of pages for the B+tree to
 * be constructed.
 */
int
btree_estimate_total_numpages (THREAD_ENTRY * thread_p, int dis_key_cnt,
			       int avg_key_len, int tot_val_cnt,
			       int *blt_pgcnt_est, int *blt_wrs_pgcnt_est)
{
  int load_pgcnt_est;
  int rec_oid_cnt;
  int avg_rec_len;
  int avg_nrec_len;
  int page_size;
  int ovfl_page_size;
  int nrecs_leaf_page;
  int nrecs_nleaf_page;
  int num_leaf_pages;
  int num_ovfl_pages;
  int num_nleaf_pages;
  int order;
  int nlevel_cnt;
  int nlevel_pg_cnt;
  int num_pages;
  int k, s;
  float unfill_factor;

  /* initializations */
  load_pgcnt_est = -1;
  *blt_pgcnt_est = -1;
  *blt_wrs_pgcnt_est = -1;

  /* check for passed parameters */
  if (dis_key_cnt == 0)
    {
      dis_key_cnt++;
    }
  if (tot_val_cnt < dis_key_cnt)
    {
      tot_val_cnt = dis_key_cnt;
    }

  /* find average leaf record length */
  /* LEAF RECORD: Key-Length : Ovfl_vpid :    key   :  oid1 : oid2 ... */
  rec_oid_cnt = CEIL_PTVDIV (tot_val_cnt, dis_key_cnt);
  rec_oid_cnt = MAX (1, rec_oid_cnt);
  avg_rec_len = LEAF_RECORD_SIZE;
  avg_rec_len += avg_key_len;
  avg_rec_len = DB_ALIGN (avg_rec_len, OR_INT_SIZE);	/* OK */
  avg_rec_len += (rec_oid_cnt * OR_OID_SIZE);

  /* find average non-leaf record length */
  /* NLEAF RECORD: Child_vpid : key_len : key */
  avg_nrec_len = NON_LEAF_RECORD_SIZE;
  avg_nrec_len += avg_key_len;

  /* find actually available page size for index records:
   * The index pages are usually 80% full and each one contains
   * a node header (sizeof (BTREE_NODE_HEADER)).
   *
   * Reserved space: page-header-overhead + header record +
   *                 one record size (the one not to be inserted) +
   *                 free area reserved in the page
   */

  /* Do the estimations for three cases.
   * Regular index loading, use index unfill factor,
   * Regular index built (one at a time), assume 30% free in pages,
   * Worst case index built, assume 50% free space in pages.
   */
  for (s = 0; s < 3; s++)
    {
      if (s == 0)
	{
	  unfill_factor =
	    (float) (prm_get_float_value (PRM_ID_BT_UNFILL_FACTOR) + 0.05);
	}
      else if (s == 1)
	{
	  unfill_factor = (float) (0.30 + 0.05);
	}
      else
	{
	  unfill_factor = (float) (0.50 + 0.05);
	}
      page_size = (int) (DB_PAGESIZE - (spage_header_size () +
					(sizeof (BTREE_NODE_HEADER) +
					 spage_slot_size ()) +
					(DB_PAGESIZE * unfill_factor)));

      /* find the number of records per index page */
      if (avg_rec_len >= page_size)
	{
	  /* records will use overflow pages, so each leaf page will get
	   * one record, plus number overflow pages
	   */
	  nrecs_leaf_page = 1;
	  ovfl_page_size = DB_PAGESIZE - (spage_header_size () +
					  (DISK_VPID_SIZE +
					   spage_slot_size ()) +
					  spage_slot_size ());
	  num_ovfl_pages =
	    dis_key_cnt *
	    (CEIL_PTVDIV (avg_rec_len - page_size, ovfl_page_size));
	}
      else
	{
	  /* consider the last record size not to be put in page */
	  page_size -= (avg_rec_len + spage_slot_size ());
	  nrecs_leaf_page = page_size / (avg_rec_len + spage_slot_size ());
	  nrecs_leaf_page = MAX (1, nrecs_leaf_page);
	  num_ovfl_pages = 0;
	}
      nrecs_nleaf_page = page_size / (avg_nrec_len + spage_slot_size ());
      nrecs_nleaf_page = MAX (2, nrecs_nleaf_page);

      /* find the number of leaf pages */
      num_leaf_pages = CEIL_PTVDIV (dis_key_cnt, nrecs_leaf_page);
      num_leaf_pages = MAX (1, num_leaf_pages);

      /* find the number of nleaf pages */
      num_nleaf_pages = 1;
      order = 1;
      do
	{
	  nlevel_cnt = 1;
	  for (k = 0; k < order; k++)
	    {
	      nlevel_cnt *= ((int) nrecs_nleaf_page);
	    }
	  nlevel_pg_cnt = (num_leaf_pages / nlevel_cnt);
	  num_nleaf_pages += nlevel_pg_cnt;
	  order++;
	}
      while (nlevel_pg_cnt > 1);

      /* find total number of index tree pages, one page is added for the
       * file manager overhead.
       */
      num_pages = num_leaf_pages + num_ovfl_pages + num_nleaf_pages;
      num_pages += file_guess_numpages_overhead (thread_p, NULL, num_pages);

      /* record corresponding estimation */
      if (s == 0)
	{
	  load_pgcnt_est = num_pages;
	}
      else if (s == 1)
	{
	  *blt_pgcnt_est = num_pages;
	}
      else
	{
	  *blt_wrs_pgcnt_est = num_pages;
	}

    }				/* for */

  /* make sure that built tree estimations are not lower than loaded
   * tree estimations.
   */
  if (*blt_pgcnt_est < load_pgcnt_est)
    {
      *blt_pgcnt_est = load_pgcnt_est;
    }
  if (*blt_wrs_pgcnt_est < *blt_pgcnt_est)
    {
      *blt_wrs_pgcnt_est = *blt_pgcnt_est;
    }

  return load_pgcnt_est;
}
#endif

/*
 * btree_get_subtree_capacity () -
 *   return: NO_ERROR
 *   btid(in):
 *   pg_ptr(in):
 *   cpc(in):
 */
static int
btree_get_subtree_capacity (THREAD_ENTRY * thread_p, BTID_INT * btid,
			    PAGE_PTR pg_ptr, BTREE_CAPACITY * cpc)
{
  RECDES rec;			/* Page record descriptor */
  int free_space;		/* Total free space of the Page */
  int key_cnt;			/* Page key count */
  NON_LEAF_REC nleaf_ptr;	/* NonLeaf Record pointer */
  VPID page_vpid;		/* Child page identifier */
  PAGE_PTR page = NULL;		/* Child page pointer */
  int i;			/* Loop counter */
  int offset;			/* Offset to the beginning of OID list */
  int oid_cnt;			/* Number of OIDs */
  VPID ovfl_vpid;		/* Overflow page identifier */
  RECDES orec;			/* Overflow record descriptor */
  LEAF_REC leaf_pnt;

  bool clear_key = false;
  PAGE_PTR ovfp = NULL;
  DB_VALUE key1;
  int ret = NO_ERROR;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;

  /* initialize */
  leaf_pnt.key_len = 0;
  VPID_SET_NULL (&leaf_pnt.ovfl);

  /* initialize capacity structure */
  cpc->dis_key_cnt = 0;
  cpc->tot_val_cnt = 0;
  cpc->avg_val_per_key = 0;
  cpc->leaf_pg_cnt = 0;
  cpc->nleaf_pg_cnt = 0;
  cpc->tot_pg_cnt = 0;
  cpc->height = 0;
  cpc->sum_rec_len = 0;
  cpc->sum_key_len = 0;
  cpc->avg_key_len = 0;
  cpc->avg_rec_len = 0;
  cpc->tot_free_space = 0;
  cpc->tot_space = 0;
  cpc->tot_used_space = 0;
  cpc->avg_pg_key_cnt = 0;
  cpc->avg_pg_free_sp = 0;

  free_space = spage_get_free_space (thread_p, pg_ptr);

  key_cnt = btree_node_number_of_keys (pg_ptr);

  header = btree_get_node_header (pg_ptr);
  if (header == NULL)
    {
      goto exit_on_error;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if (node_type == BTREE_NON_LEAF_NODE)
    {				/* a non-leaf page */
      BTREE_CAPACITY cpc2;

      /* traverse all the subtrees of this non_leaf page and accumulate
       * the statistical data in the cpc structure
       */
      for (i = 1; i <= key_cnt; i++)
	{
	  if (spage_get_record (pg_ptr, i, &rec, PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }
	  btree_read_fixed_portion_of_non_leaf_record (&rec, &nleaf_ptr);
	  page_vpid = nleaf_ptr.pnt;
	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, page, PAGE_BTREE);

	  ret = btree_get_subtree_capacity (thread_p, btid, page, &cpc2);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  /* form the cpc structure for a non-leaf node page */
	  cpc->dis_key_cnt += cpc2.dis_key_cnt;
	  cpc->tot_val_cnt += cpc2.tot_val_cnt;
	  cpc->leaf_pg_cnt += cpc2.leaf_pg_cnt;
	  cpc->nleaf_pg_cnt += cpc2.nleaf_pg_cnt;
	  cpc->tot_pg_cnt += cpc2.tot_pg_cnt;
	  cpc->height = cpc2.height + 1;
	  cpc->sum_rec_len += cpc2.sum_rec_len;
	  cpc->sum_key_len += cpc2.sum_key_len;
	  cpc->tot_free_space += cpc2.tot_free_space;
	  cpc->tot_space += cpc2.tot_space;
	  cpc->tot_used_space += cpc2.tot_used_space;
	  pgbuf_unfix_and_init (thread_p, page);
	}			/* for */
      cpc->avg_val_per_key = ((cpc->dis_key_cnt > 0) ?
			      (cpc->tot_val_cnt / cpc->dis_key_cnt) : 0);
      cpc->nleaf_pg_cnt += 1;
      cpc->tot_pg_cnt += 1;
      cpc->tot_free_space += free_space;
      cpc->tot_space += DB_PAGESIZE;
      cpc->tot_used_space += (DB_PAGESIZE - free_space);
      cpc->avg_key_len = ((cpc->dis_key_cnt > 0) ?
			  ((int) (cpc->sum_key_len / cpc->dis_key_cnt)) : 0);
      cpc->avg_rec_len = ((cpc->dis_key_cnt > 0) ?
			  ((int) (cpc->sum_rec_len / cpc->dis_key_cnt)) : 0);
      cpc->avg_pg_key_cnt = ((cpc->leaf_pg_cnt > 0) ?
			     ((int) (cpc->dis_key_cnt / cpc->leaf_pg_cnt)) :
			     0);
      cpc->avg_pg_free_sp = ((cpc->tot_pg_cnt > 0) ?
			     (cpc->tot_free_space / cpc->tot_pg_cnt) : 0);
    }
  else
    {				/* a leaf page */

      /* form the cpc structure for a leaf node page */
      cpc->dis_key_cnt = key_cnt;
      cpc->leaf_pg_cnt = 1;
      cpc->nleaf_pg_cnt = 0;
      cpc->tot_pg_cnt = 1;
      cpc->height = 1;
      for (i = 1; i <= cpc->dis_key_cnt; i++)
	{
	  if (spage_get_record (pg_ptr, i, &rec, PEEK) != S_SUCCESS)
	    {
	      goto exit_on_error;
	    }
	  cpc->sum_rec_len += rec.length;

	  /* read the current record key */
	  if (btree_read_record
	      (thread_p, btid, pg_ptr, &rec, &key1, &leaf_pnt,
	       BTREE_LEAF_NODE, &clear_key, &offset, PEEK_KEY_VALUE,
	       NULL) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  cpc->sum_key_len += btree_get_disk_size_of_key (&key1);
	  btree_clear_key_value (&clear_key, &key1);

	  /* find the value (OID) count for the record */
	  oid_cnt =
	    btree_record_get_num_oids (thread_p, btid, &rec, offset,
				       BTREE_LEAF_NODE);

	  ovfl_vpid = leaf_pnt.ovfl;
	  if (!VPID_ISNULL (&ovfl_vpid))
	    {			/* overflow pages exist */
	      do
		{
		  ovfp = pgbuf_fix (thread_p, &ovfl_vpid, OLD_PAGE,
				    PGBUF_LATCH_READ,
				    PGBUF_UNCONDITIONAL_LATCH);
		  if (ovfp == NULL)
		    {
		      goto exit_on_error;
		    }

		  (void) pgbuf_check_page_ptype (thread_p, ovfp, PAGE_BTREE);

		  btree_get_next_overflow_vpid (ovfp, &ovfl_vpid);

		  if (spage_get_record (ovfp, 1, &orec, PEEK) != S_SUCCESS)
		    {
		      goto exit_on_error;
		    }

		  oid_cnt +=
		    btree_record_get_num_oids (thread_p, btid, &orec, 0,
					       BTREE_OVERFLOW_NODE);
		  pgbuf_unfix_and_init (thread_p, ovfp);
		}
	      while (!VPID_ISNULL (&ovfl_vpid));
	    }			/* if */
	  cpc->tot_val_cnt += oid_cnt;

	}			/* for */
      cpc->avg_val_per_key = ((cpc->dis_key_cnt > 0) ?
			      (cpc->tot_val_cnt / cpc->dis_key_cnt) : 0);
      cpc->avg_key_len = ((cpc->dis_key_cnt > 0) ?
			  ((int) (cpc->sum_key_len / cpc->dis_key_cnt)) : 0);
      cpc->avg_rec_len = ((cpc->dis_key_cnt > 0) ?
			  ((int) (cpc->sum_rec_len / cpc->dis_key_cnt)) : 0);
      cpc->tot_free_space = (float) free_space;
      cpc->tot_space = DB_PAGESIZE;
      cpc->tot_used_space = (cpc->tot_space - cpc->tot_free_space);
      cpc->avg_pg_key_cnt = ((cpc->leaf_pg_cnt > 0) ?
			     (cpc->dis_key_cnt / cpc->leaf_pg_cnt) : 0);
      cpc->avg_pg_free_sp = ((cpc->tot_pg_cnt > 0) ?
			     (cpc->tot_free_space / cpc->tot_pg_cnt) : 0);

    }				/* if-else */

  return ret;

exit_on_error:

  if (page)
    {
      pgbuf_unfix_and_init (thread_p, page);
    }
  if (ovfp)
    {
      pgbuf_unfix_and_init (thread_p, ovfp);
    }

  btree_clear_key_value (&clear_key, &key1);

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_index_capacity () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   cpc(out): Set to contain index capacity information
 *
 * Note: Form and return index capacity/space related information
 */
int
btree_index_capacity (THREAD_ENTRY * thread_p, BTID * btid,
		      BTREE_CAPACITY * cpc)
{
  VPID root_vpid;		/* root page identifier */
  PAGE_PTR root = NULL;		/* root page pointer */
  BTID_INT btid_int;
  BTREE_ROOT_HEADER *root_header = NULL;
  int ret = NO_ERROR;

  /* read root page */
  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;
  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

  root_header = btree_get_root_header (root);
  if (root_header == NULL)
    {
      goto exit_on_error;
    }

  btid_int.sys_btid = btid;
  ret = btree_glean_root_header_info (thread_p, root_header, &btid_int);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* traverse the tree and store the capacity info */
  ret = btree_get_subtree_capacity (thread_p, &btid_int, root, cpc);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, root);

  return ret;

exit_on_error:

  if (root)
    {
      pgbuf_unfix_and_init (thread_p, root);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_dump_capacity () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *
 * Note: Dump index capacity/space information.
 */
int
btree_dump_capacity (THREAD_ENTRY * thread_p, FILE * fp, BTID * btid)
{
  BTREE_CAPACITY cpc;
  int ret = NO_ERROR;
  char area[FILE_DUMP_DES_AREA_SIZE];
  char *file_des = NULL;
  char *index_name = NULL;
  char *class_name = NULL;
  int file_des_size = 0;
  int size = 0;
  OID class_oid;

  assert (fp != NULL && btid != NULL);

  /* get index capacity information */
  ret = btree_index_capacity (thread_p, btid, &cpc);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* get class_name and index_name */
  file_des = area;
  file_des_size = FILE_DUMP_DES_AREA_SIZE;

  size = file_get_descriptor (thread_p, &btid->vfid, file_des, file_des_size);
  if (size <= 0)
    {
      goto exit_on_error;
    }

  class_oid = ((FILE_HEAP_DES *) file_des)->class_oid;

  class_name = heap_get_class_name (thread_p, &class_oid);

  /* get index name */
  if (heap_get_indexinfo_of_btid (thread_p, &class_oid, btid,
				  NULL, NULL, NULL, NULL, &index_name,
				  NULL) != NO_ERROR)
    {
      goto exit_on_error;
    }

  fprintf (fp,
	   "\n--------------------------------------------------"
	   "-----------\n");
  fprintf (fp, "BTID: {{%d, %d}, %d}, %s ON %s, CAPACITY INFORMATION:\n",
	   btid->vfid.volid, btid->vfid.fileid, btid->root_pageid,
	   (index_name == NULL) ? "*UNKOWN_INDEX*" : index_name,
	   (class_name == NULL) ? "*UNKOWN_CLASS*" : class_name);

  /* dump the capacity information */
  fprintf (fp, "\nDistinct Key Count: %d\n", cpc.dis_key_cnt);
  fprintf (fp, "Total Value Count: %d\n", cpc.tot_val_cnt);
  fprintf (fp, "Average Value Count Per Key: %d\n", cpc.avg_val_per_key);
  fprintf (fp, "Total Page Count: %d\n", cpc.tot_pg_cnt);
  fprintf (fp, "Leaf Page Count: %d\n", cpc.leaf_pg_cnt);
  fprintf (fp, "NonLeaf Page Count: %d\n", cpc.nleaf_pg_cnt);
  fprintf (fp, "Height: %d\n", cpc.height);
  fprintf (fp, "Average Key Length: %d\n", cpc.avg_key_len);
  fprintf (fp, "Average Record Length: %d\n", cpc.avg_rec_len);
  fprintf (fp, "Total Index Space: %.0f bytes\n", cpc.tot_space);
  fprintf (fp, "Used Index Space: %.0f bytes\n", cpc.tot_used_space);
  fprintf (fp, "Free Index Space: %.0f bytes\n", cpc.tot_free_space);
  fprintf (fp, "Average Page Free Space: %.0f bytes\n", cpc.avg_pg_free_sp);
  fprintf (fp, "Average Page Key Count: %d\n", cpc.avg_pg_key_cnt);
  fprintf (fp, "--------------------------------------------------"
	   "-----------\n");

end:

  if (class_name != NULL)
    {
      free_and_init (class_name);
    }

  if (index_name != NULL)
    {
      free_and_init (index_name);
    }

  return ret;

exit_on_error:

  assert (ret != NO_ERROR);
  if (ret == NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (ret);
    }

  goto end;
}

/*
 * btree_dump_capacity_all () -
 *   return: NO_ERROR
 *
 * Note: Dump the capacity/space information of all indices.
 */
int
btree_dump_capacity_all (THREAD_ENTRY * thread_p, FILE * fp)
{
  int num_files;		/* Number of files in the system */
  BTID btid;			/* Btree index identifier */
  VPID vpid;			/* Index root page identifier */
  int i;			/* Loop counter */
  int ret = NO_ERROR;

  /* Find number of files */
  num_files = file_get_numfiles (thread_p);
  if (num_files < 0)
    {
      goto exit_on_error;
    }

  /* Go to each file, check only the btree files */
  for (i = 0; i < num_files; i++)
    {
      if (file_find_nthfile (thread_p, &btid.vfid, i) != 1)
	{
	  break;
	}

      if (file_get_type (thread_p, &btid.vfid) != FILE_BTREE)
	{
	  continue;
	}

      if (btree_get_root_page (thread_p, &btid, &vpid) == NULL)
	{
	  goto exit_on_error;
	}

      btid.root_pageid = vpid.pageid;

      ret = btree_dump_capacity (thread_p, fp, &btid);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }				/* for */

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * b+tree dump routines
 */

/*
 * btree_print_space () -
 *   return:
 *   n(in):
 */
static void
btree_print_space (FILE * fp, int n)
{

  while (n--)			/* print n space character */
    {
      fprintf (fp, " ");
    }

}

/*
 * btree_dump_page () -
 *   return: nothing
 *   btid(in): B+tree index identifier
 *   page_ptr(in): Page pointer
 *   pg_vpid(in): Page identifier
 *   n(in): Identation left margin (number of preceding blanks)
 *   level(in):
 *
 * Note: Dumps the content of the given page of the tree.
 */
static void
btree_dump_page (THREAD_ENTRY * thread_p, FILE * fp,
		 const OID * class_oid_p, BTID_INT * btid,
		 const char *btname, PAGE_PTR page_ptr, VPID * pg_vpid,
		 int depth, int level)
{
  int key_cnt;
  int i;
  RECDES rec;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  VPID vpid;

  if (pg_vpid == NULL)
    {
      pgbuf_get_vpid (page_ptr, &vpid);
      pg_vpid = &vpid;
    }

  key_cnt = btree_node_number_of_keys (page_ptr);

  /* get the header record */
  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      btree_print_space (fp, depth * 4);
      fprintf (fp, "btree_dump_page: get node header failure: %d\n", key_cnt);
      return;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  btree_print_space (fp, depth * 4);
  fprintf (fp,
	   "[%s PAGE {%d, %d}, level: %d, depth: %d, keys: %d, "
	   "Prev: {%d, %d}, Next: {%d, %d}, Max key len: %d]\n",
	   node_type_to_string (node_type),
	   pg_vpid->volid, pg_vpid->pageid,
	   header->node_level, depth, key_cnt,
	   header->prev_vpid.volid, header->prev_vpid.pageid,
	   header->next_vpid.volid, header->next_vpid.pageid,
	   header->max_key_len);

  if (class_oid_p && !OID_ISNULL (class_oid_p))
    {
      char *class_name_p = NULL;
      class_name_p = heap_get_class_name (thread_p, class_oid_p);

      btree_print_space (fp, depth * 4);
      fprintf (fp, "INDEX %s ON CLASS %s (CLASS_OID:%2d|%4d|%2d) \n\n",
	       (btname) ? btname : "*UNKNOWN-INDEX*",
	       (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*",
	       class_oid_p->volid, class_oid_p->pageid, class_oid_p->slotid);
      if (class_name_p)
	{
	  free_and_init (class_name_p);
	}
    }

  fflush (fp);

  if (key_cnt < 0)
    {
      btree_print_space (fp, depth * 4);
      fprintf (fp,
	       "btree_dump_page: node key count underflow: %d\n", key_cnt);
      return;
    }

  if (level > 1)
    {
      /* output the content of each record */
      for (i = 1; i <= key_cnt; i++)
	{
	  (void) spage_get_record (page_ptr, i, &rec, PEEK);
	  if (node_type == BTREE_LEAF_NODE)
	    {
	      if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
		{
		  fprintf (fp, "(F)");
		}
	      else
		{
		  fprintf (fp, "   ");
		}

	      btree_dump_leaf_record (thread_p, fp, btid, &rec, depth);
	    }
	  else
	    {
	      btree_dump_non_leaf_record (thread_p, fp, btid, &rec, depth, 1);
	    }
	  /* fprintf (fp, "\n"); */
	}
    }

  fprintf (fp, "\n");
}

/*
 * btree_dump_page_with_subtree () -
 *   return: nothing
 *   btid(in): B+tree index identifier
 *   pg_ptr(in): Page pointer
 *   pg_vpid(in): Page identifier
 *   n(in): Identation left margin (number of preceding blanks)
 *   level(in):
 *
 * Note: Dumps the content of the given page together with its subtrees
 */
static void
btree_dump_page_with_subtree (THREAD_ENTRY * thread_p, FILE * fp,
			      BTID_INT * btid, PAGE_PTR pg_ptr,
			      VPID * pg_vpid, int depth, int level)
{
  int key_cnt;
  int i;
  NON_LEAF_REC nleaf_ptr;
  VPID page_vpid;
  PAGE_PTR page = NULL;
  RECDES rec;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;

  key_cnt = btree_node_number_of_keys (pg_ptr);

  btree_dump_page (thread_p, fp, NULL, btid, NULL, pg_ptr, pg_vpid, depth, level);	/* dump current page */

  /* get the header record */
  header = btree_get_node_header (pg_ptr);
  if (header == NULL)
    {
      fprintf (fp,
	       "btree_dump_page_with_subtree: get node header failure: %d.\n",
	       key_cnt);
      return;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if (node_type == BTREE_NON_LEAF_NODE)
    {				/* page is non_leaf */
#if !defined(NDEBUG)
      if (key_cnt < 0)
	{
	  fprintf (fp,
		   "btree_dump_page_with_subtree: node key count underflow: %d.\n",
		   key_cnt);
	  return;
	}
#endif

      /* for each child page pointer in this non_leaf page,
       * dump the corresponding subtree
       */
      for (i = 1; i <= key_cnt; i++)
	{
	  (void) spage_get_record (pg_ptr, i, &rec, PEEK);
	  btree_read_fixed_portion_of_non_leaf_record (&rec, &nleaf_ptr);
	  page_vpid = nleaf_ptr.pnt;
	  page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			    PGBUF_UNCONDITIONAL_LATCH);
	  if (page == NULL)
	    {
	      return;
	    }
	  btree_dump_page_with_subtree (thread_p, fp, btid, page, &page_vpid,
					depth + 1, level);
	  pgbuf_unfix_and_init (thread_p, page);
	}
    }

  return;
}

/*
 * btree_dump () -
 *   return: nothing
 *   btid(in): B+tree index identifier
 *   level(in):
 *
 * Note: Dumps the content of the each page in the B+tree by
 * traversing the tree in an "inorder" manner. The header
 * information, as well as the content of each record in a page
 * are dumped. The header information for a non_leaf page
 * contains the key count and maximum key length information.
 * Maximum key length refers to the longest key in the page and
 * in its subtrees. The header information for a leaf page
 * contains also the next_page information, which is the page
 * identifier of the next sibling page, and the overflow page
 * count information. root header information contains
 * statistical data for the whole tree. These consist of total
 * key count of the tree, total page count, leaf page count,
 * non_leaf page count, total overflow page count and the height
 * of the tree. Total key count refers only to those keys that
 * are stored in the leaf pages of the tree. The index key type
 * is also stored in the root header.
 */
void
btree_dump (THREAD_ENTRY * thread_p, FILE * fp, BTID * btid, int level)
{
  VPID p_vpid;
  PAGE_PTR root = NULL;
  BTID_INT btid_int;
  BTREE_ROOT_HEADER *root_header = NULL;

  p_vpid.pageid = btid->root_pageid;	/* read root page */
  p_vpid.volid = btid->vfid.volid;
  root = pgbuf_fix (thread_p, &p_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      return;
    }

  root_header = btree_get_root_header (root);
  if (root_header == NULL)
    {
      goto end;			/* do nothing */
    }

  btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (thread_p, root_header, &btid_int) !=
      NO_ERROR)
    {
      goto end;			/* do nothing */
    }

  fprintf (fp,
	   "\n------------ The B+Tree Index Dump Start ---------------------\n\n\n");
  btree_dump_root_header (fp, root);	/* output root header information */

  if (level != 0)
    {
      btree_dump_page_with_subtree (thread_p, fp, &btid_int, root, &p_vpid, 0,
				    level);
    }

  fprintf (fp,
	   "\n------------ The B+Tree Index Dump End ---------------------\n\n\n");

end:
  pgbuf_unfix_and_init (thread_p, root);

  return;
}


/*
 * btree_read_key_type () -
 *   return:
 *   btid(in):
 */
TP_DOMAIN *
btree_read_key_type (THREAD_ENTRY * thread_p, BTID * btid)
{
  VPID p_vpid;
  PAGE_PTR root = NULL;
  TP_DOMAIN *key_type = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;

  p_vpid.pageid = btid->root_pageid;	/* read root page */
  p_vpid.volid = btid->vfid.volid;
  root = pgbuf_fix (thread_p, &p_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      return NULL;
    }

  (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

  root_header = btree_get_root_header (root);
  if (root_header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, root);
      return NULL;
    }

  (void) or_unpack_domain (root_header->packed_key_domain, &key_type, 0);

  pgbuf_unfix_and_init (thread_p, root);

  return key_type;
}

/*
 * btree_delete_key_from_leaf () - Delete a b-tree key completely.
 *
 * return		     : Error code.
 * thread_p (in)	     : Thread entry.
 * btid (in)		     : B-tree info.
 * leaf_pg (in)		     : Leaf page.
 * leafrec_pnt (in)	     : Leaf record info.
 * delete_helper (in)	     : B-tree delete helper.
 * search_key (in)	     : Search key result.
 */
static int
btree_delete_key_from_leaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
			    PAGE_PTR leaf_pg, LEAF_REC * leafrec_pnt,
			    BTREE_DELETE_HELPER * delete_helper,
			    BTREE_SEARCH_KEY_HELPER * search_key)
{
  int ret = NO_ERROR;		/* Error code. */
  int key_cnt;			/* Node key count. */
  BTREE_NODE_HEADER *header = NULL;	/* Node header. */
  bool is_system_op_started = false;	/* Set to true if system operation is
					 * started to provide atomicity.
					 */

  /* Is this an overflow key? Should we delete it too? */
  /* If this is undo of inserted object, overflow key deletion will be handled
   * automatically. Don't delete here.
   */
  if (leafrec_pnt->key_len < 0
      && delete_helper->purpose != BTREE_OP_DELETE_UNDO_INSERT)
    {
      if (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT)
	{
	  /* Vacuum. Since there is no transaction, delete atomicity must be
	   * provided by a system operation.
	   */
	  assert (VACUUM_IS_THREAD_VACUUM_WORKER (thread_p));
	  if (log_start_system_op (thread_p) == NULL)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }
	  is_system_op_started = true;
	}
      /* Delete overflow key. */
      ret =
	btree_delete_overflow_key (thread_p, btid, leaf_pg,
				   search_key->slotid, BTREE_LEAF_NODE);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      /* Overflow key deleted. */
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  header = btree_get_node_header (leaf_pg);
  if (header == NULL)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }

  /* now delete the btree slot */
  assert (search_key->slotid > 0);
  if (spage_delete (thread_p, leaf_pg, search_key->slotid)
      != search_key->slotid)
    {
      ASSERT_ERROR_AND_SET (ret);
      goto exit_on_error;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* key deleted, update node header */
  key_cnt = btree_node_number_of_keys (leaf_pg);
  if (key_cnt == 0)
    {
      header->max_key_len = 0;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully executed delete "
		     "object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s, slotid=%d, page=%d|%d, "
		     "lsa=%lld|%d, in index (%d, %d|%d). "
		     "Key was removed completely.\n",
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.delete_mvccid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     search_key->slotid,
		     pgbuf_get_volume_id (leaf_pg),
		     pgbuf_get_page_id (leaf_pg),
		     (long long int) pgbuf_get_lsa (leaf_pg)->pageid,
		     (int) pgbuf_get_lsa (leaf_pg)->offset,
		     btid->sys_btid->root_pageid,
		     btid->sys_btid->vfid.volid, btid->sys_btid->vfid.fileid);
    }

  /* Save redo logging. */
  /* Flag recovery that key is being removed completely. */
  LOG_RV_RECORD_SET_MODIFY_MODE (&delete_helper->leaf_addr,
				 LOG_RV_RECORD_DELETE);
  /* Since this is completely removed, no debugging info is required. */
  assert (!BTREE_RV_REDO_HAS_DEBUG_INFO (delete_helper->leaf_addr.offset));

  /* Add logging. */
  if (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL)
    {
      /* Undo-redo logging. No actual redo data, since the record is removed
       * completely (and only a flag saved in leaf_addr.offset is used.
       */
      log_append_undoredo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
				&delete_helper->leaf_addr,
				delete_helper->rv_undo_data_length, 0,
				delete_helper->rv_undo_data, NULL);
    }
  else if (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT)
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (leaf_pg),
			     delete_helper->leaf_addr.offset, leaf_pg,
			     0, NULL, LOG_FIND_CURRENT_TDES (thread_p));
    }
  else				/* BTREE_OP_DELETE_VACUUM_OBJECT */
    {
      /* We now know everything is successfully executed. Vacuum no longer
       * needs undo logging.
       */
      log_append_redo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
			    &delete_helper->leaf_addr, 0, NULL);
    }
  pgbuf_set_dirty (thread_p, leaf_pg, DONT_FREE);

  if (is_system_op_started)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }

#if !defined(NDEBUG)
  (void) btree_verify_node (thread_p, btid, leaf_pg);
#endif

  return ret;

exit_on_error:

  if (is_system_op_started)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  assert_release (ret != NO_ERROR);
  return ret;
}

/*
 * btree_swap_first_oid_with_ovfl_rec () - Replace the object in leaf page
 *					   with an object from the first
 *					   overflow page.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * btid (in)		    : B-tree info.
 * delete_helper (in)	    : B-tree delete helper.
 * leaf_page (in)	    : Leaf page.
 * search_key (in)	    : Search key result.
 * leaf_rec (in)	    : Key leaf record.
 * ovfl_vpid (in)	    : VPID of first overflow page.
 */
static int
btree_swap_first_oid_with_ovfl_rec (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    BTREE_DELETE_HELPER * delete_helper,
				    PAGE_PTR leaf_page,
				    BTREE_SEARCH_KEY_HELPER * search_key,
				    RECDES * leaf_rec, VPID * ovfl_vpid)
{
  int ret = NO_ERROR;		/* Error code. */
  OID last_oid, last_class_oid;	/* Last object OID and class OID. */
  BTREE_MVCC_INFO last_mvcc_info;	/* Last object MVCC info. */
  PAGE_PTR ovfl_page = NULL;	/* First overflow page. */
  RECDES ovfl_copy_rec;		/* Overflow record. */
  /* Buffer to store overflow record data. */
  char ovfl_copy_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  VPID next_ovfl_vpid;		/* VPID of second overflow page. */
  int oid_cnt;			/* OID count in first overflow. */
  int offset_to_last_object = 0;	/* Offset to last object. */
  int oid_size = OR_OID_SIZE;	/* OID + class OID size. */
  bool need_dealloc_overflow_page = false;	/* Set to true if first overflow
						 * page is being deallocated.
						 */
  bool is_system_op_started = false;	/* Se to true when system operation
					 * is started.
					 */
  LOG_DATA_ADDR ovf_addr;	/* Log address for overflow page. */

  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (leaf_page != NULL);
  assert (search_key != NULL && search_key->result == BTREE_KEY_FOUND
	  && search_key->slotid > 0);
  assert (delete_helper != NULL);
  assert (leaf_rec != NULL);
  assert (ovfl_vpid != NULL);

  if (BTREE_IS_UNIQUE (btid->unique_pk))
    {
      /* Class OID is also stored. */
      oid_size += OR_OID_SIZE;
    }

  /* Get overflow record. */
  ovfl_copy_rec.area_size = DB_PAGESIZE;
  ovfl_copy_rec.data = PTR_ALIGN (ovfl_copy_rec_buf, BTREE_MAX_ALIGN);
  ovfl_page =
    pgbuf_fix (thread_p, ovfl_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (ovfl_page == NULL)
    {
      ASSERT_ERROR_AND_SET (ret);
      goto exit_on_error;
    }

#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, ovfl_page, PAGE_BTREE);
#endif /* !NDEBUG */

  if (spage_get_record (ovfl_page, 1, &ovfl_copy_rec, COPY) != S_SUCCESS)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid, &ovfl_copy_rec,
				   BTREE_OVERFLOW_NODE, NULL);
#endif /* NDEBUG */

  /* Save VPID of next overflow. */
  btree_get_next_overflow_vpid (ovfl_page, &next_ovfl_vpid);

  /* Get OID count. */
  oid_cnt =
    btree_record_get_num_oids (thread_p, btid, &ovfl_copy_rec, 0,
			       BTREE_OVERFLOW_NODE);
  assert (oid_cnt >= 1);
  if (oid_cnt == 1)
    {
      /* Last page OID. It can be deallocated after swap. */
      need_dealloc_overflow_page = true;
    }

  /* Get last object. */
  ret =
    btree_record_get_last_object (thread_p, btid, &ovfl_copy_rec,
				  BTREE_OVERFLOW_NODE, 0, &last_oid,
				  &last_class_oid, &last_mvcc_info,
				  &offset_to_last_object);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }
  /* Last object obtained. */

  /* Vacuum worker will need an atomic operation to deallocate page. */
  if (need_dealloc_overflow_page == true
      && (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT))
    {
      if (log_start_system_op (thread_p) == NULL)
	{
	  goto exit_on_error;
	}
      is_system_op_started = true;
    }

  /* Initialize overflow log data address. */
  ovf_addr.offset = 1;
  ovf_addr.pgptr = ovfl_page;
  ovf_addr.vfid = &btid->sys_btid->vfid;

  if (need_dealloc_overflow_page == false)
    {
      /* Just remove the object from overflow record. */
      assert (oid_cnt > 1);

      /* Create redo logging for overflow. */
      /* Overflow node. */
      LOG_RV_RECORD_SET_MODIFY_MODE (&ovf_addr, LOG_RV_RECORD_UPDATE_PARTIAL);
      BTREE_RV_REDO_SET_OVERFLOW_NODE (&ovf_addr);
#if !defined (NDEBUG)
      /* For debugging recovery. */
      BTREE_RV_REDO_SET_DEBUG_INFO (&ovf_addr,
				    delete_helper->rv_redo_data_ptr, btid,
				    BTREE_RV_DEBUG_ID_SWAP_OVF);
#endif /* !NDEBUG */

      /* Safe guard. */
      assert (delete_helper->rv_redo_data_ptr != NULL);

      /* Remove last object (the change is also logged). */
      btree_record_remove_last_object (thread_p, btid, &ovfl_copy_rec,
				       BTREE_OVERFLOW_NODE,
				       offset_to_last_object,
				       &delete_helper->rv_redo_data_ptr);

      /* Update record. */
      if (spage_update (thread_p, ovfl_page, 1, &ovfl_copy_rec) != SP_SUCCESS)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto exit_on_error;
	}

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

      rv_redo_data_length =
	CAST_BUFLEN (delete_helper->rv_redo_data_ptr
		     - delete_helper->rv_redo_data);
      assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);

      /* This is an overflow page. */
      log_append_redo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL, &ovf_addr,
			    rv_redo_data_length, delete_helper->rv_redo_data);
      /* Reset redo recovery data pointer. */
      delete_helper->rv_redo_data_ptr = delete_helper->rv_redo_data;

#if !defined (NDEBUG)
      /* For debugging recovery. */
      BTREE_RV_REDO_SET_DEBUG_INFO (&delete_helper->leaf_addr,
				    delete_helper->rv_redo_data_ptr, btid,
				    BTREE_RV_DEBUG_ID_SWAP_LEAF);
#endif /* !NDEBUG */

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

      pgbuf_set_dirty (thread_p, ovfl_page, FREE);
      ovfl_page = NULL;
    }
  else
    {
      /* Deallocate first overflow page. */
      pgbuf_unfix_and_init (thread_p, ovfl_page);

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

      ret = file_dealloc_page (thread_p, &btid->sys_btid->vfid, ovfl_vpid);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

#if !defined (NDEBUG)
      /* For debugging recovery. */
      BTREE_RV_REDO_SET_DEBUG_INFO (&delete_helper->leaf_addr,
				    delete_helper->rv_redo_data_ptr, btid,
				    BTREE_RV_DEBUG_ID_SWAP_LEAF);
#endif /* !NDEBUG */

      btree_leaf_record_change_overflow_link (thread_p, btid, leaf_rec,
					      &next_ovfl_vpid,
					      &delete_helper->
					      rv_redo_data_ptr);
    }

  /* Replace first object in leaf with swapped object. */
  btree_leaf_change_first_object (leaf_rec, btid, &last_oid, &last_class_oid,
				  &last_mvcc_info, NULL,
				  &delete_helper->rv_redo_data_ptr);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Update slot */
  if (spage_update (thread_p, leaf_page, search_key->slotid, leaf_rec)
      != SP_SUCCESS)
    {
      assert_release (false);
      ret = ER_FAILED;
      /* TODO: Add object back to overflow page. Although it should never
       *       happen.
       */
      goto exit_on_error;
    }

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully executed delete "
		     "object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s, slotid=%d, page=%d|%d, "
		     "lsa=%lld|%d, in index (%d, %d|%d). "
		     "Object was replaced with overflow OID.\n",
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.delete_mvccid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     search_key->slotid,
		     pgbuf_get_volume_id (leaf_page),
		     pgbuf_get_page_id (leaf_page),
		     (long long int) pgbuf_get_lsa (leaf_page)->pageid,
		     (int) pgbuf_get_lsa (leaf_page)->offset,
		     btid->sys_btid->root_pageid,
		     btid->sys_btid->vfid.volid, btid->sys_btid->vfid.fileid);
    }

  /* Logging. */
  /* Safe guard. */
  LOG_RV_RECORD_SET_MODIFY_MODE (&delete_helper->leaf_addr,
				 LOG_RV_RECORD_UPDATE_PARTIAL);
  assert (delete_helper->rv_redo_data != NULL
	  && delete_helper->rv_redo_data_ptr != NULL);
  /* Get redo data length. */
  rv_redo_data_length =
    CAST_BUFLEN (delete_helper->rv_redo_data_ptr
		 - delete_helper->rv_redo_data);
  /* Safe guard. */
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  if (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL)
    {
      /* Append undoredo logging. */
      log_append_undoredo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
				&delete_helper->leaf_addr,
				delete_helper->rv_undo_data_length,
				rv_redo_data_length,
				delete_helper->rv_undo_data,
				delete_helper->rv_redo_data);
    }
  else if (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT)
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (leaf_page),
			     delete_helper->leaf_addr.offset, leaf_page,
			     rv_redo_data_length, delete_helper->rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }
  else				/* BTREE_OP_DELETE_VACUUM_OBJECT */
    {
      /* Append redo logging. */
      /* We now know everything is successfully executed. Vacuum no longer
       * needs undo logging.
       */
      log_append_redo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
			    &delete_helper->leaf_addr, rv_redo_data_length,
			    delete_helper->rv_redo_data);
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, leaf_page, DONT_FREE);

  if (is_system_op_started)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
    }

  if (ovfl_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, ovfl_page);
    }

  return NO_ERROR;

exit_on_error:

  if (is_system_op_started)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  assert_release (ret != NO_ERROR);
  return ret;
}

/*
 * btree_modify_leaf_ovfl_vpid () - Modify the link to first overflow page in
 *				    leaf record.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * btid_int (in)	    : B-tree info.
 * delete_helper (in)	    : B-tree delete helper.
 * leaf_page (in)	    : Leaf page.
 * leaf_record (in)	    : Leaf record.
 * search_key (in)	    : Search key result.
 * next_ovfl_vpid (in)	    : New link to first overflow page.
 */
static int
btree_modify_leaf_ovfl_vpid (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			     BTREE_DELETE_HELPER * delete_helper,
			     PAGE_PTR leaf_page, RECDES * leaf_record,
			     BTREE_SEARCH_KEY_HELPER * search_key,
			     VPID * next_ovfl_vpid)
{
  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (delete_helper != NULL);
  assert (leaf_page != NULL);
  assert (search_key != NULL && search_key->result == BTREE_KEY_FOUND
	  && search_key->slotid > 0);
  assert (leaf_record != NULL);
  assert (next_ovfl_vpid != NULL);
  assert (delete_helper->rv_redo_data != NULL
	  && delete_helper->rv_redo_data_ptr != NULL);

#if !defined (NDEBUG)
  /* For debugging recovery. */
  BTREE_RV_REDO_SET_DEBUG_INFO (&delete_helper->leaf_addr,
				delete_helper->rv_redo_data_ptr, btid_int,
				BTREE_RV_DEBUG_ID_OVF_LINK);
#endif /* !NDEBUG */
  LOG_RV_RECORD_SET_MODIFY_MODE (&delete_helper->leaf_addr,
				 LOG_RV_RECORD_UPDATE_PARTIAL);

  btree_leaf_record_change_overflow_link (thread_p, btid_int, leaf_record,
					  next_ovfl_vpid,
					  &delete_helper->rv_redo_data_ptr);

  if (spage_update (thread_p, leaf_page, search_key->slotid, leaf_record)
      != SP_SUCCESS)
    {
      /* Unexpected. */
      assert_release (false);
      return ER_FAILED;
    }

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully executed delete "
		     "object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s, slotid=%d, page=%d|%d, "
		     "lsa=%lld|%d, in index (%d, %d|%d). "
		     "First overflow page was deallocated.\n",
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.delete_mvccid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     search_key->slotid,
		     pgbuf_get_volume_id (leaf_page),
		     pgbuf_get_page_id (leaf_page),
		     (long long int) pgbuf_get_lsa (leaf_page)->pageid,
		     (int) pgbuf_get_lsa (leaf_page)->offset,
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  /* Add logging. */
  rv_redo_data_length =
    CAST_BUFLEN (delete_helper->rv_redo_data_ptr
		 - delete_helper->rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);

  if (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL)
    {
      /* Add undo/redo logging. */
      log_append_undoredo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
				&delete_helper->leaf_addr,
				delete_helper->rv_undo_data_length,
				rv_redo_data_length,
				delete_helper->rv_undo_data,
				delete_helper->rv_redo_data);
    }
  else if (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT)
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (leaf_page),
			     delete_helper->leaf_addr.offset, leaf_page,
			     rv_redo_data_length, delete_helper->rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }
  else				/* BTREE_OP_DELETE_VACUUM_OBJECT */
    {
      /* Change was successful. Vacuum no longer needs undo logging. */
      log_append_redo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
			    &delete_helper->leaf_addr,
			    rv_redo_data_length, delete_helper->rv_redo_data);
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, leaf_page, DONT_FREE);

  /* Success. */
  return NO_ERROR;
}

/*
 * btree_modify_overflow_link () - Modify next overflow link in overflow page
 *				   header.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * btid_int (in)	    : B-tree info.
 * delete_helper (in)	    : B-tree delete helper.
 * ovfl_page (in)	    : Overflow page.
 * next_ovfl_vpid (in)	    : New link to next overflow.
 */
static int
btree_modify_overflow_link (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			    BTREE_DELETE_HELPER * delete_helper,
			    PAGE_PTR ovfl_page, VPID * next_ovfl_vpid)
{
  LOG_DATA_ADDR ovf_addr;
  BTREE_OVERFLOW_HEADER ovf_header_info;
  RECDES overflow_header_record;
  int rv_redo_data_length;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (delete_helper != NULL);
  assert (ovfl_page != NULL);
  assert (next_ovfl_vpid != NULL);
  assert (delete_helper->rv_redo_data != NULL
	  && delete_helper->rv_redo_data_ptr != NULL);

  /* Update overflow header info. */
  VPID_COPY (&ovf_header_info.next_vpid, next_ovfl_vpid);

  /* Create record with new overflow header info and update page. */
  overflow_header_record.data = (char *) &ovf_header_info;
  overflow_header_record.length = sizeof (BTREE_OVERFLOW_HEADER);

  if (spage_update (thread_p, ovfl_page, HEADER,
		    &overflow_header_record) != SP_SUCCESS)
    {
      /* Unexpected. */
      assert_release (false);
      return ER_FAILED;
    }

  /* Log the change. */
  ovf_addr.offset = HEADER;
  ovf_addr.pgptr = ovfl_page;
  ovf_addr.vfid = &btid_int->sys_btid->vfid;

  /* Redo logging. */
  BTREE_RV_REDO_SET_OVERFLOW_NODE (&ovf_addr);
  /* Update entire record. */
  LOG_RV_RECORD_SET_MODIFY_MODE (&ovf_addr, LOG_RV_RECORD_UPDATE_ALL);
  /* Pack record data. */
  memcpy (delete_helper->rv_redo_data_ptr, overflow_header_record.data,
	  overflow_header_record.length);
  delete_helper->rv_redo_data_ptr += overflow_header_record.length;

  /* Add logging. */
  rv_redo_data_length =
    CAST_BUFLEN (delete_helper->rv_redo_data_ptr
		 - delete_helper->rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);

  if (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL)
    {
      /* Add undo/redo logging. */
      log_append_undoredo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
				&ovf_addr, delete_helper->rv_undo_data_length,
				rv_redo_data_length,
				delete_helper->rv_undo_data,
				delete_helper->rv_redo_data);
    }
  else if (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT)
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (ovfl_page), ovf_addr.offset,
			     ovfl_page, rv_redo_data_length,
			     delete_helper->rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }
  else				/* BTREE_OP_DELETE_VACUUM_OBJECT */
    {
      log_append_redo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL, &ovf_addr,
			    rv_redo_data_length, delete_helper->rv_redo_data);
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, ovfl_page, DONT_FREE);

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully executed delete "
		     "object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s in index (%d, %d|%d). "
		     "Non-first overflow page was deallocated.\n",
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.delete_mvccid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  return NO_ERROR;
}

/*
 * btree_delete_meta_record - delete record for merge
 *
 *   return: (error code)
 *   thread_p(in):
 *   btid(in): B+tree index identifier
 *   page_ptr(in):
 *   slot_id(in):
 *   node_type:
 *
 */
static int
btree_delete_meta_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  PAGE_PTR page_ptr, int slot_id)
{
  int ret = NO_ERROR;
  RECDES rec;
  int dummy_offset;
  bool dummy_clear_key;
  NON_LEAF_REC nleaf_pnt = { {NULL_PAGEID, NULL_VOLID}, 0 };
  LEAF_REC leaf_pnt = { {NULL_PAGEID, NULL_VOLID}, 0 };
  char *recset_data;
  int recset_data_length;
  PGLENGTH log_addr_offset;
  char recset_data_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;

  /* init */
  recset_data = PTR_ALIGN (recset_data_buf, BTREE_MAX_ALIGN);

  assert (slot_id > 0);
  if (spage_get_record (page_ptr, slot_id, &rec, PEEK) != S_SUCCESS)
    {
      goto exit_on_error;
    }

  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      goto exit_on_error;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  assert (node_type == BTREE_NON_LEAF_NODE);

  if (node_type == BTREE_NON_LEAF_NODE)
    {
      btree_read_fixed_portion_of_non_leaf_record (&rec, &nleaf_pnt);

      /* prepare undo log record */
      btree_rv_write_log_record (recset_data, &recset_data_length, &rec,
				 BTREE_NON_LEAF_NODE);

      if (nleaf_pnt.key_len < 0)
	{			/* overflow key */
	  /* get the overflow manager to delete the key */
	  ret = btree_delete_overflow_key (thread_p, btid, page_ptr, slot_id,
					   BTREE_NON_LEAF_NODE);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }
  else
    {
      ret = btree_read_record (thread_p, btid, page_ptr, &rec, NULL,
			       &leaf_pnt, BTREE_LEAF_NODE, &dummy_clear_key,
			       &dummy_offset, PEEK_KEY_VALUE, NULL);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* prepare undo log record */
      btree_rv_write_log_record (recset_data, &recset_data_length, &rec,
				 BTREE_LEAF_NODE);

      if (leaf_pnt.key_len < 0)
	{			/* overflow key */
	  /* get the overflow manager to delete the key */
	  ret = btree_delete_overflow_key (thread_p, btid, page_ptr, slot_id,
					   BTREE_LEAF_NODE);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

  assert (slot_id > 0);
  if (spage_delete (thread_p, page_ptr, slot_id) != slot_id)
    {
      goto exit_on_error;
    }

  /* log the deleted slot_id for undo/redo purposes */
  log_addr_offset = slot_id;
  log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_DEL,
			     &btid->sys_btid->vfid, page_ptr, log_addr_offset,
			     recset_data_length,
			     sizeof (log_addr_offset), recset_data,
			     &log_addr_offset);

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_write_default_split_info() -
 *   return:
 *   info(in/out):
 */
static void
btree_write_default_split_info (BTREE_NODE_SPLIT_INFO * info)
{
  assert (info != NULL);

  info->pivot = BTREE_SPLIT_DEFAULT_PIVOT;
  info->index = 1;
}

/*
 * btree_merge_root () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   P(in): Page pointer for the root to be merged
 *   Q(in): Page pointer for the root child page to be merged
 *   R(in): Page pointer for the root child page to be merged
 *   P_vpid(in): Page identifier for page P
 *   Q_vpid(in): Page identifier for page Q
 *   R_vpid(in): Page identifier for page R
 *
 * Note: When the root page has only two children (non_leaf)
 * that can be merged together, then they are merged through
 * this specific root merge operation. The main distinction of
 * this routine from the regular merge operation is that in this
 * the content of the two child pages are moved to the root, in
 * order not to change the originial root page. The root can also
 * be a specific non-leaf page, that is, it may have only one key
 * and one child page pointer. In this case, R_id, the page
 * identifier for the page R is NULL_PAGEID. In both cases, the
 * height of the tree is reduced by one, after the merge
 * operation. The two (one) child pages are not deallocated by
 * this routine. Deallocation of these pages are left to the
 * calling routine.
 *
 * Note:  Page Q and Page R contents are not changed by this routine,
 * since these pages will be deallocated by the calling routine.
 */
static int
btree_merge_root (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR P,
		  PAGE_PTR Q, PAGE_PTR R)
{
  int left_cnt, right_cnt;
  RECDES peek_rec;
  NON_LEAF_REC nleaf_pnt = { {NULL_PAGEID, NULL_VOLID}, 0 };
  int i, j;
  char *recset_data;		/* for recovery purposes */
  int recset_length;		/* for recovery purposes */
  RECSET_HEADER recset_header;	/* for recovery purposes */
  int ret = NO_ERROR;
  char recset_data_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  LOG_DATA_ADDR addr;
  BTREE_ROOT_HEADER *root_header = NULL;
  int Q_end, R_start;
#if !defined(NDEBUG)
  int p_level, q_level, r_level;
#endif

#if !defined(NDEBUG)
  if (prm_get_integer_value (PRM_ID_ER_BTREE_DEBUG) & BTREE_DEBUG_DUMP_SIMPLE)
    {
      VPID *P_vpid = pgbuf_get_vpid_ptr (P);
      VPID *Q_vpid = pgbuf_get_vpid_ptr (Q);
      VPID *R_vpid = pgbuf_get_vpid_ptr (R);

      printf ("btree_merge_root: P{%d, %d}, Q{%d, %d}, R{%d, %d}\n",
	      P_vpid->volid, P_vpid->pageid,
	      Q_vpid->volid, Q_vpid->pageid, R_vpid->volid, R_vpid->pageid);
    }

  p_level = btree_get_node_level (P);
  assert (p_level > 2);

  q_level = btree_get_node_level (Q);
  assert (q_level > 1);

  r_level = btree_get_node_level (R);
  assert (r_level > 1);

  assert (q_level == r_level);
  assert (p_level == q_level + 1);
  assert (p_level == r_level + 1);

  btree_verify_node (thread_p, btid, P);
  btree_verify_node (thread_p, btid, Q);
  btree_verify_node (thread_p, btid, R);
#endif

  /* initializations */
  recset_data = NULL;

  /* log the P record contents for undo purposes,
   * if a crash happens the records of root page P will be inserted
   * back. There is no need for undo logs for pages Q and R,
   * since they are not changed by this routine, because they will be
   * deallocated after a successful merge operation. There is also no need
   * for redo logs for pages Q and R, since these pages will be deallocated
   * by the caller routine.
   */

  /* for recovery purposes */
  recset_data = PTR_ALIGN (recset_data_buf, BTREE_MAX_ALIGN);
  assert (recset_data != NULL);

  /* remove fence key for merge */
  left_cnt = btree_node_number_of_keys (Q);
  right_cnt = btree_node_number_of_keys (R);

  Q_end = left_cnt;
  if (left_cnt > 0)
    {
      /* read the last record to check upper fence_key */
      if (spage_get_record (Q, left_cnt, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* delete left page upper fence_key before merge */
      if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE))
	{
	  assert_release (left_cnt >= 1);
	  assert (!btree_leaf_is_flaged
		  (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_KEY));
	  Q_end--;
	}
    }

  left_cnt = Q_end;

  R_start = 1;
  if (right_cnt > 0)
    {
      /* read the first record to check lower fence_key */
      if (spage_get_record (R, 1, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      /* delete right page lower fence_key before merge */
      if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE))
	{
	  assert_release (right_cnt >= 1);
	  assert (!btree_leaf_is_flaged
		  (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_KEY));
	  R_start++;
	}
    }

  right_cnt = right_cnt - (R_start + 1);

  /* delete all records in P (should be just 2)
   */

  /* delete second record */
  ret = btree_delete_meta_record (thread_p, btid, P, 2);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* delete first record */
  ret = btree_delete_meta_record (thread_p, btid, P, 1);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Log the page Q records for undo/redo purposes on page P.
   */
  recset_header.rec_cnt = left_cnt;
  recset_header.first_slotid = 1;
  ret = btree_rv_util_save_page_records (Q, 1, Q_end, 1, recset_data,
					 &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* move content of the left page to the root page */
  for (i = 1; i <= Q_end; i++)
    {
      if (spage_get_record (Q, i, &peek_rec, PEEK) != S_SUCCESS
	  || spage_insert_at (thread_p, P, i, &peek_rec) != SP_SUCCESS)
	{
	  if (i > 1)
	    {
	      recset_header.rec_cnt = i - 1;
	      recset_header.first_slotid = 1;
	      log_append_undo_data2 (thread_p, RVBT_INS_PGRECORDS,
				     &btid->sys_btid->vfid, P, -1,
				     sizeof (RECSET_HEADER), &recset_header);
	    }
	  goto exit_on_error;
	}
    }				/* for */

  log_append_undoredo_data2 (thread_p, RVBT_INS_PGRECORDS,
			     &btid->sys_btid->vfid, P, -1,
			     sizeof (RECSET_HEADER), recset_length,
			     &recset_header, recset_data);

  /* increment lsa of the page to be deallocated */
  addr.vfid = NULL;
  addr.pgptr = Q;
  addr.offset = 0;
  log_skip_logging_set_lsa (thread_p, &addr);
  pgbuf_set_dirty (thread_p, Q, DONT_FREE);

  /* Log the page R records for undo purposes on page P.
   */
  right_cnt = btree_node_number_of_keys (R);

  recset_header.rec_cnt = right_cnt;
  recset_header.first_slotid = left_cnt + 1;

  ret = btree_rv_util_save_page_records (R, R_start, right_cnt, left_cnt + 1,
					 recset_data, &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* move content of the right page to the root page */
  assert (R_start > 0);
  for (i = R_start, j = 1; j <= right_cnt; i++, j++)
    {
      assert (left_cnt + j > 0);
      if (spage_get_record (R, i, &peek_rec, PEEK) != S_SUCCESS
	  || spage_insert_at (thread_p, P, left_cnt + j, &peek_rec)
	  != SP_SUCCESS)
	{
	  if (j > 1)
	    {
	      recset_header.rec_cnt = j - 1;
	      recset_header.first_slotid = left_cnt + 1;
	      log_append_undo_data2 (thread_p, RVBT_INS_PGRECORDS,
				     &btid->sys_btid->vfid, P, -1,
				     sizeof (RECSET_HEADER), &recset_header);
	    }
	  goto exit_on_error;
	}
    }				/* for */

  log_append_undoredo_data2 (thread_p, RVBT_INS_PGRECORDS,
			     &btid->sys_btid->vfid, P, -1,
			     sizeof (RECSET_HEADER), recset_length,
			     &recset_header, recset_data);

  /* increment lsa of the page to be deallocated */
  addr.vfid = NULL;
  addr.pgptr = R;
  addr.offset = 0;
  log_skip_logging_set_lsa (thread_p, &addr);
  pgbuf_set_dirty (thread_p, R, DONT_FREE);

  /* update root page */
  root_header = btree_get_root_header (P);
  if (root_header == NULL)
    {
      goto exit_on_error;
    }

  btree_node_header_undo_log (thread_p, &btid->sys_btid->vfid, P);

  VPID_SET_NULL (&root_header->node.prev_vpid);
  VPID_SET_NULL (&root_header->node.next_vpid);
  btree_write_default_split_info (&(root_header->node.split_info));
  root_header->node.node_level--;
  assert_release (root_header->node.node_level > 1);

  btree_node_header_redo_log (thread_p, &btid->sys_btid->vfid, P);

#if !defined(NDEBUG)
  {
    BTREE_NODE_HEADER *qheader = NULL, *rheader = NULL;

    qheader = btree_get_node_header (Q);
    assert (qheader != NULL);

    rheader = btree_get_node_header (R);
    assert (rheader != NULL);

    assert (root_header->node.max_key_len ==
	    MAX (qheader->max_key_len, rheader->max_key_len));
  }
#endif

  pgbuf_set_dirty (thread_p, P, DONT_FREE);

  mnt_bt_merges (thread_p);

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, P);
  btree_verify_node (thread_p, btid, Q);
  btree_verify_node (thread_p, btid, R);
#endif

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_merge_node () -
 *   return: NO_ERROR
 *   btid(in): The B+tree index identifier
 *   P(in): Page pointer for the parent page of page Q
 *   Q(in): Page pointer for the child page of P that will be merged
 *   R(in): Page pointer for the left or right sibling of page Q
 *   next_page(in):
 *   P_vpid(in): Page identifier for page P
 *   Q_vpid(in): Page identifier for page Q
 *   R_vpid(in): Page identifier for page R
 *   p_slot_id(in): The slot of parent page P which points page to
		be merged (right page)
 *   child_vpid(in): Child page identifier to be followed, Q or R.
 *
 * Note: Page Q is merged with page R which may be its left or right
 * sibling. Depending on the efficiency of the merge operation
 * the merge operation may take place on Page Q or on page R to
 * reduce the size of the data that will moved. After the merge
 * operation either page Q or page R becomes ready for
 * deallocation. Deallocation is left to the calling routine.
 *
 * Note:  The page which will be deallocated by the caller after a
 * successful merge operation is not changed by this routine.
 */
static int
btree_merge_node (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR P,
		  PAGE_PTR left_pg, PAGE_PTR right_pg, INT16 p_slot_id,
		  VPID * child_vpid, BTREE_MERGE_STATUS status)
{
  int left_cnt, right_cnt;
  int i, ret = NO_ERROR;
  int key_len;
  VPID *left_vpid = pgbuf_get_vpid_ptr (left_pg);

  /* record decoding */
  RECDES peek_rec;
  NON_LEAF_REC nleaf_pnt;
  LEAF_REC leaf_pnt;
  DB_VALUE key, lf_key;
  bool clear_key = false, lf_clear_key = false;
  int offset;

  OID instance_oid, class_oid;

  /* recovery */
  LOG_DATA_ADDR addr;
  char *recset_data;		/* for recovery purposes */
  int recset_length;		/* for recovery purposes */
  char recset_data_buf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  BTREE_NODE_HEADER *qheader = NULL, *rheader = NULL;

  /* merge & recompress buff */
  DB_VALUE uncomp_key;
  int L_prefix, R_prefix;
  int L_used, R_used, total_size;
  RECDES rec[MAX_LEAF_REC_NUM];
  int rec_idx;
  char merge_buf[(IO_MAX_PAGE_SIZE * 4) + MAX_ALIGNMENT];
  char *merge_buf_ptr = merge_buf;
  int merge_buf_size = sizeof (merge_buf);
  int merge_idx = 0;
  bool fence_record;
  bool left_upper_fence = false;

  PGSLOTID sp_id;
  int sp_ret;

  BTREE_MVCC_INFO mvcc_info;

  PAGE_PTR next_page_after_merged = NULL;

#if !defined(NDEBUG)
  if (prm_get_integer_value (PRM_ID_ER_BTREE_DEBUG) & BTREE_DEBUG_DUMP_SIMPLE)
    {
      VPID *P_vpid = pgbuf_get_vpid_ptr (P);
      VPID *right_vpid = pgbuf_get_vpid_ptr (right_pg);

      printf ("btree_merge_node: P{%d, %d}, Q{%d, %d}, R{%d, %d}\n",
	      P_vpid->volid, P_vpid->pageid,
	      left_vpid->volid, left_vpid->pageid, right_vpid->volid,
	      right_vpid->pageid);
    }
#endif

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, P);
  btree_verify_node (thread_p, btid, left_pg);
  btree_verify_node (thread_p, btid, right_pg);
#endif

  /***********************************************************
   ***  STEP 0: initializations, save undo image of left
   *** 		calculate uncompress size & memory alloc
   ***********************************************************/
  /* initializations */
  VPID_SET_NULL (child_vpid);
  recset_data = PTR_ALIGN (recset_data_buf, MAX_ALIGNMENT);

  left_cnt = btree_node_number_of_keys (left_pg);
  right_cnt = btree_node_number_of_keys (right_pg);

  L_used = btree_node_size_uncompressed (thread_p, btid, left_pg);
  if (L_used < 0)
    {
      ASSERT_ERROR ();
      return L_used;
    }
  L_prefix = btree_node_common_prefix (thread_p, btid, left_pg);
  if (L_prefix < 0)
    {
      ASSERT_ERROR ();
      return L_prefix;
    }

  R_used = btree_node_size_uncompressed (thread_p, btid, right_pg);
  if (R_used < 0)
    {
      ASSERT_ERROR ();
      return R_used;
    }
  R_prefix = btree_node_common_prefix (thread_p, btid, right_pg);
  if (R_prefix < 0)
    {
      ASSERT_ERROR ();
      return R_prefix;
    }

  total_size = L_used + R_used + MAX_MERGE_ALIGN_WASTE;
  if (total_size > (int) sizeof (merge_buf))
    {
      merge_buf_size = total_size;
      merge_buf_ptr = (char *) db_private_alloc (thread_p, merge_buf_size);
    }

  /* this means left (or right) empty or only one fence key remain */
  if (status == BTREE_MERGE_L_EMPTY)
    {
      left_cnt = 0;
    }

  if (status == BTREE_MERGE_R_EMPTY)
    {
      right_cnt = 0;
    }

  /***********************************************************
   ***  STEP 1: uncompress left
   ***********************************************************/

  for (i = 1, rec_idx = 0; i <= left_cnt; i++, rec_idx++)
    {
      rec[rec_idx].data =
	PTR_ALIGN (&merge_buf_ptr[merge_idx], BTREE_MAX_ALIGN);
      merge_idx = CAST_BUFLEN (rec[rec_idx].data - merge_buf_ptr);	/* advance align */
      rec[rec_idx].area_size = merge_buf_size - merge_idx;
      rec[rec_idx].type = REC_HOME;

      /* peek original record */
      spage_get_record (left_pg, i, &peek_rec, PEEK);
      fence_record =
	btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE);

      if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_KEY) ||
	  (fence_record == false && L_prefix == 0) ||
	  status == BTREE_MERGE_R_EMPTY)
	{
	  /* use peeked data for overflow key */
	  memcpy (rec[rec_idx].data, peek_rec.data, peek_rec.length);
	  rec[rec_idx].length = peek_rec.length;
	  merge_idx += rec[rec_idx].length;
	  continue;
	}

      if (fence_record == true && i == left_cnt)
	{
	  if (i == 1)
	    {
	      RECDES tmp_rec;
	      spage_get_record (right_pg, 1, &tmp_rec, PEEK);
	      if (btree_leaf_is_flaged (&tmp_rec, BTREE_LEAF_RECORD_FENCE))
		{
		  /* skip upper fence key */
		  left_upper_fence = true;
		  break;
		}
	      else
		{
		  /* lower fence key */
		}
	    }
	  else
	    {
	      /* skip upper fence key */
	      left_upper_fence = true;
	      break;
	    }
	}

      /* read original key */
      ret = btree_read_record_without_decompression (thread_p, btid,
						     &peek_rec, &key,
						     &leaf_pnt,
						     BTREE_LEAF_NODE,
						     &clear_key, &offset,
						     PEEK_KEY_VALUE);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}

      ret =
	btree_leaf_get_first_object (btid, &peek_rec, &instance_oid,
				     &class_oid, &mvcc_info);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}

      if (fence_record == true)
	{
	  assert (i == 1);	/* lower fence key */

	  lf_key = key;

	  /* use current fence record */
	  memcpy (rec[rec_idx].data, peek_rec.data, peek_rec.length);
	  rec[rec_idx].length = peek_rec.length;
	  merge_idx += rec[rec_idx].length;
	  continue;
	}

      /* uncompress key */
      assert (L_prefix > 0);

      pr_midxkey_add_prefix (&uncomp_key, &lf_key, &key, L_prefix);
      btree_clear_key_value (&clear_key, &key);

      key_len = btree_get_disk_size_of_key (&uncomp_key);
      clear_key = true;

      /* make record */
      btree_write_record (thread_p, btid, NULL, &uncomp_key, BTREE_LEAF_NODE,
			  BTREE_NORMAL_KEY, key_len, false, &class_oid,
			  &instance_oid, &mvcc_info, &rec[rec_idx]);
      btree_clear_key_value (&clear_key, &uncomp_key);

      /* save oid lists (& overflow vpid if exists) */
      memcpy (rec[rec_idx].data + rec[rec_idx].length, peek_rec.data + offset,
	      peek_rec.length - offset);
      rec[rec_idx].length += peek_rec.length - offset;

      if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
	{
	  btree_leaf_record_handle_first_overflow (&rec[rec_idx], btid, NULL);
	}

#if !defined (NDEBUG)
      btree_check_valid_record (thread_p, btid, &rec[rec_idx],
				BTREE_LEAF_NODE, NULL);
#endif

      merge_idx += rec[rec_idx].length;

      assert (merge_idx < merge_buf_size);
      assert (rec_idx < (int) (sizeof (rec) / sizeof (rec[0])));
    }

  btree_clear_key_value (&lf_clear_key, &lf_key);

  /***********************************************************
   ***  STEP 2: uncompress right
   ***********************************************************/

  for (i = 1; i <= right_cnt; i++, rec_idx++)
    {
      rec[rec_idx].data = PTR_ALIGN (&merge_buf[merge_idx], BTREE_MAX_ALIGN);
      merge_idx = CAST_BUFLEN (rec[rec_idx].data - merge_buf);	/* advance align */
      rec[rec_idx].area_size = sizeof (merge_buf) - merge_idx;
      rec[rec_idx].type = REC_HOME;

      /* peek original record */
      spage_get_record (right_pg, i, &peek_rec, PEEK);
      fence_record =
	btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE);

      if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_KEY) ||
	  (fence_record == false && R_prefix == 0) ||
	  status == BTREE_MERGE_L_EMPTY)
	{
	  /* use peeked data for overflow key */
	  memcpy (rec[rec_idx].data, peek_rec.data, peek_rec.length);
	  rec[rec_idx].length = peek_rec.length;
	  merge_idx += rec[rec_idx].length;
	  continue;
	}

      if (fence_record && i == right_cnt)
	{
	  if (i == 1 && left_upper_fence == true)
	    {
	      /* lower fence key */
	    }
	  else
	    {
	      /* upper fence key, use current fence record */
	      memcpy (rec[rec_idx].data, peek_rec.data, peek_rec.length);
	      rec[rec_idx].length = peek_rec.length;
	      merge_idx += rec[rec_idx].length;
	      continue;
	    }
	}

      /* read original key */
      ret = btree_read_record_without_decompression (thread_p, btid,
						     &peek_rec, &key,
						     &leaf_pnt,
						     BTREE_LEAF_NODE,
						     &clear_key, &offset,
						     PEEK_KEY_VALUE);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      ret =
	btree_leaf_get_first_object (btid, &peek_rec, &instance_oid,
				     &class_oid, &mvcc_info);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}

      if (fence_record)
	{
	  assert (i == 1);	/* lower fence key */

	  lf_key = key;

	  /* skip lower fence key */
	  rec_idx--;
	  continue;
	}

      /* uncompress key */
      assert (R_prefix > 0);

      pr_midxkey_add_prefix (&uncomp_key, &lf_key, &key, R_prefix);
      btree_clear_key_value (&clear_key, &key);

      key_len = btree_get_disk_size_of_key (&key);
      clear_key = true;

      /* make record */
      btree_write_record (thread_p, btid, NULL, &uncomp_key, BTREE_LEAF_NODE,
			  BTREE_NORMAL_KEY, key_len, false, &class_oid,
			  &instance_oid, &mvcc_info, &rec[rec_idx]);
      btree_clear_key_value (&clear_key, &uncomp_key);

      /* save oid lists (& overflow vpid if exists) */
      memcpy (rec[rec_idx].data + rec[rec_idx].length, peek_rec.data + offset,
	      peek_rec.length - offset);
      rec[rec_idx].length += peek_rec.length - offset;

      if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
	{
	  btree_leaf_record_handle_first_overflow (&rec[rec_idx], btid, NULL);
	}

#if !defined (NDEBUG)
      btree_check_valid_record (thread_p, btid, &rec[rec_idx],
				BTREE_LEAF_NODE, NULL);
#endif

      merge_idx += rec[rec_idx].length;

      assert (merge_idx < merge_buf_size);
      assert (rec_idx < (int) (sizeof (rec) / sizeof (rec[0])));
    }

  btree_clear_key_value (&lf_clear_key, &lf_key);

  /***********************************************************
   ***  STEP 3: recompress all
   ***
   ***    recompress records with changed prefix
   ***********************************************************/

  if (TP_DOMAIN_TYPE (btid->key_type) == DB_TYPE_MIDXKEY
      && btree_leaf_is_flaged (&rec[0], BTREE_LEAF_RECORD_FENCE)
      && btree_leaf_is_flaged (&rec[rec_idx - 1], BTREE_LEAF_RECORD_FENCE))
    {
      /* recompress */
      ret = btree_compress_records (thread_p, btid, rec, rec_idx);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
    }

  /***********************************************************
   ***  STEP 4: replace left slots
   ***
   ***    remove all records
   ***    insert recompressed records
   ***********************************************************/

  /* add undo logging for left_pg */
  log_append_undo_data2 (thread_p, RVBT_COPYPAGE,
			 &btid->sys_btid->vfid, left_pg, -1,
			 DB_PAGESIZE, left_pg);

  for (i = left_cnt; i >= 1; i--)
    {
      assert (i > 0);
      if (spage_delete (thread_p, left_pg, i) != i)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto exit_on_error;
	}
    }

  assert (spage_number_of_records (left_pg) == 1);

  /* Log the right page records for undo purposes on the left page. */
  for (i = 0; i < rec_idx; i++)
    {
      sp_ret = spage_insert (thread_p, left_pg, &rec[i], &sp_id);
      if (sp_ret != SP_SUCCESS)
	{
#if !defined(NDEBUG)
	  btree_dump_page (thread_p, stdout, NULL, btid,
			   NULL, left_pg, left_vpid, 2, 2);
#endif
	  assert_release (false);
	  ret = ER_FAILED;
	  goto exit_on_error;
	}

      assert (sp_id > 0);
    }

  /***********************************************************
   ***  STEP 5: update child link of page P
   ***********************************************************/

  /* get and log the old node record to be deleted for undo purposes */
  assert (p_slot_id > 0);
  if (spage_get_record (P, p_slot_id, &peek_rec, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }

  btree_read_fixed_portion_of_non_leaf_record (&peek_rec, &nleaf_pnt);

  if (nleaf_pnt.key_len < 0)
    {				/* overflow key */
      /* get the overflow manager to delete the key */
      ret = btree_delete_overflow_key (thread_p, btid, P, p_slot_id,
				       BTREE_NON_LEAF_NODE);
      if (ret != NO_ERROR)
	{
	  assert_release (false);
	  goto exit_on_error;
	}
    }

  btree_rv_write_log_record (recset_data, &recset_length,
			     &peek_rec, BTREE_NON_LEAF_NODE);
  log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_DEL,
			     &btid->sys_btid->vfid, P, p_slot_id,
			     recset_length,
			     sizeof (p_slot_id), recset_data, &p_slot_id);
  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  assert (p_slot_id > 0);
  if (spage_delete (thread_p, P, p_slot_id) != p_slot_id)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }

  pgbuf_set_dirty (thread_p, P, DONT_FREE);

  *child_vpid = *left_vpid;

  /***********************************************************
   ***  STEP 6: update left page header info
   ***          write redo log for left
   ***********************************************************/

  /* get right page header information */
  rheader = btree_get_node_header (right_pg);
  if (rheader == NULL)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }

  /* update left page header information */
  qheader = btree_get_node_header (left_pg);
  if (qheader == NULL)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }

  qheader->next_vpid = rheader->next_vpid;
  qheader->max_key_len = MAX (qheader->max_key_len, rheader->max_key_len);
  btree_write_default_split_info (&(qheader->split_info));

  /* add redo logging for left_pg */
  log_append_redo_data2 (thread_p, RVBT_COPYPAGE,
			 &btid->sys_btid->vfid, left_pg, -1,
			 DB_PAGESIZE, left_pg);

  pgbuf_set_dirty (thread_p, left_pg, DONT_FREE);

  /***********************************************************
   ***  STEP 7: increment lsa of right page to be deallocated
   ***          verify P, left, right
   ***********************************************************/

  addr.vfid = NULL;
  addr.pgptr = right_pg;
  addr.offset = 0;
  log_skip_logging_set_lsa (thread_p, &addr);
  pgbuf_set_dirty (thread_p, right_pg, DONT_FREE);

  mnt_bt_merges (thread_p);

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, P);
  btree_verify_node (thread_p, btid, left_pg);
  btree_verify_node (thread_p, btid, right_pg);
#endif

  /***********************************************************
   ***  STEP 8: update link for next leaf node after the
   ***		merged nodes to point to the left page.
   ***********************************************************/
  next_page_after_merged = btree_get_next_page (thread_p, left_pg);
  if (next_page_after_merged != NULL)
    {
      /* Update previous link. */
#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, next_page_after_merged,
				     PAGE_BTREE);
#endif /* !NDEBUG */
      ret =
	btree_set_vpid_previous_vpid (thread_p, btid, next_page_after_merged,
				      left_vpid);
      pgbuf_unfix_and_init (thread_p, next_page_after_merged);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
    }

  if (merge_buf_ptr != merge_buf)
    {
      db_private_free_and_init (thread_p, merge_buf_ptr);
    }

  /* Success. */
  return NO_ERROR;

exit_on_error:

  assert_release (ret != NO_ERROR);

  if (merge_buf_ptr != merge_buf)
    {
      db_private_free_and_init (thread_p, merge_buf_ptr);
    }

  return ret;
}

/*
 * btree_node_size_uncompressed -
 *
 *   return:
 *   thread_p(in):
 *   btid(in):
 *   page_ptr(in):
 *
 */
static int
btree_node_size_uncompressed (THREAD_ENTRY * thread_p, BTID_INT * btid,
			      PAGE_PTR page_ptr)
{
  int used_size, key_cnt = 0, prefix, prefix_len, offset;
  RECDES rec;
  DB_VALUE key;
  bool clear_key = false;
  DB_MIDXKEY *midx_key = NULL;
  LEAF_REC leaf_pnt;
  int error;

  used_size = DB_PAGESIZE - spage_get_free_space (thread_p, page_ptr);

  prefix = btree_node_common_prefix (thread_p, btid, page_ptr);
  if (prefix > 0)
    {
#if !defined(NDEBUG)
      BTREE_NODE_HEADER *header = NULL;

      header = btree_get_node_header (page_ptr);

      assert (header != NULL);
      assert (header->node_level == 1);	/* BTREE_LEAF_NODE */
#endif

      error = spage_get_record (page_ptr, 1, &rec, PEEK);
      if (error != S_SUCCESS)
	{
	  assert (false);
	  return error;
	}

      error = btree_read_record_without_decompression (thread_p, btid, &rec,
						       &key, &leaf_pnt,
						       BTREE_LEAF_NODE,
						       &clear_key, &offset,
						       PEEK_KEY_VALUE);
      if (error != NO_ERROR)
	{
	  return error;
	}

      assert (!btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_OVERFLOW_KEY));
      assert (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE));
      assert (DB_VALUE_TYPE (&key) == DB_TYPE_MIDXKEY);

      midx_key = DB_PULL_MIDXKEY (&key);

      btree_clear_key_value (&clear_key, &key);

      prefix_len = pr_midxkey_get_element_offset (midx_key, prefix);

      /* at here, we can not calculate aligned size of uncompressed rec.
       * alignment is already included in CAN_MERGE_WHEN_EMPTY
       */
      key_cnt = btree_node_number_of_keys (page_ptr);
      used_size += (key_cnt - 2) * prefix_len;
    }
  else if (prefix < 0)
    {
      return prefix;
    }

  return used_size;
}

/*
 * btree_node_mergeable -
 *
 *   return:
 *   thread_p(in):
 *   btid(in):
 *   L_page(in):
 *   R_page(in):
 *
 */
static BTREE_MERGE_STATUS
btree_node_mergeable (THREAD_ENTRY * thread_p, BTID_INT * btid,
		      PAGE_PTR L_page, PAGE_PTR R_page)
{
  BTREE_NODE_HEADER *l_header = NULL, *r_header = NULL;
  BTREE_NODE_TYPE l_node_type, r_node_type;
  int L_used, R_used, L_cnt, R_cnt;

  /* case 1 : one of page is empty */

  L_cnt = btree_node_number_of_keys (L_page);
  R_cnt = btree_node_number_of_keys (R_page);

  if (L_cnt == 0)
    {
      return BTREE_MERGE_L_EMPTY;
    }

  if (R_cnt == 0)
    {
      return BTREE_MERGE_R_EMPTY;
    }

  /* case 2 : size */

  l_header = btree_get_node_header (L_page);
  if (l_header == NULL)
    {
      return BTREE_MERGE_NO;
    }

  r_header = btree_get_node_header (R_page);
  if (r_header == NULL)
    {
      return BTREE_MERGE_NO;
    }

  l_node_type =
    (l_header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;
  r_node_type =
    (r_header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  assert (l_node_type == r_node_type);

  L_used = DB_PAGESIZE - spage_get_free_space (thread_p, L_page);
  R_used = DB_PAGESIZE - spage_get_free_space (thread_p, R_page);
  if (L_used + R_used + CAN_MERGE_WHEN_EMPTY < DB_PAGESIZE)
    {
      /* check uncompressed size */
      if (l_node_type == BTREE_LEAF_NODE)
	{
	  /* recalculate uncompressed left size */
	  L_used = btree_node_size_uncompressed (thread_p, btid, L_page);
	  if (L_used < 0)
	    {
	      return BTREE_MERGE_NO;
	    }

	  if (L_used + R_used + CAN_MERGE_WHEN_EMPTY >= DB_PAGESIZE)
	    {
	      return BTREE_MERGE_NO;
	    }

	  /* recalculate uncompressed right size */
	  R_used = btree_node_size_uncompressed (thread_p, btid, R_page);

	  if (L_used + R_used + CAN_MERGE_WHEN_EMPTY >= DB_PAGESIZE)
	    {
	      /* can recalculate used size after recompress with new fence key
	       * (can return true in some cases)
	       *
	       * but split and merge will be done more frequently
	       * (trade off of space and SMO)
	       */
	      return BTREE_MERGE_NO;
	    }
	}

      if (L_used + R_used + FORCE_MERGE_WHEN_EMPTY < DB_PAGESIZE)
	{
	  /* Merge must be executed. */
	  return BTREE_MERGE_SIZE_FORCE;
	}

      /* Merge can be executed. If promotions fail, it will be skipped. */
      return BTREE_MERGE_SIZE;
    }

  return BTREE_MERGE_NO;
}

/*
 * btree_key_append_object_as_new_overflow () - Insert object into a new
 *						overflow page.
 *
 * return		     : Error code.
 * thread_p (in)	     : Thread entry.
 * btid_int (in)	     : B-tree info.
 * leaf_page (in)	     : Leaf page.
 * object_info (in)	     : Object & info.
 * insert_helper (in)	     : B-tree insert helper.
 * search_key (in)	     : Search key result.
 * leaf_rec (in)	     : Leaf record.
 * first_ovfl_vpid (in)	     : VPID of first overflow.
 */
static int
btree_key_append_object_as_new_overflow (THREAD_ENTRY * thread_p,
					 BTID_INT * btid_int,
					 PAGE_PTR leaf_page,
					 BTREE_OBJECT_INFO * object_info,
					 BTREE_INSERT_HELPER * insert_helper,
					 BTREE_SEARCH_KEY_HELPER * search_key,
					 RECDES * leaf_rec,
					 VPID * first_ovfl_vpid)
{
  int ret = NO_ERROR;
  VPID ovfl_vpid;
  PAGE_PTR ovfl_page = NULL;
  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (leaf_page != NULL);
  assert (object_info != NULL);
  assert (insert_helper != NULL);
  assert (search_key != NULL);
  assert (leaf_rec != NULL);
  assert (first_ovfl_vpid != NULL);
  assert (insert_helper->rv_redo_data != NULL
	  && insert_helper->rv_redo_data_ptr != NULL);

  /* Create overflow page. */
  ret =
    btree_start_overflow_page (thread_p, btid_int, object_info,
			       first_ovfl_vpid,
			       pgbuf_get_vpid_ptr (leaf_page), &ovfl_vpid,
			       &ovfl_page);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ret;
    }

#if !defined (NDEBUG)
  if (!insert_helper->is_relocation_for_unique)
    {
      BTREE_RV_REDO_SET_DEBUG_INFO (&insert_helper->leaf_addr,
				    insert_helper->rv_redo_data_ptr,
				    btid_int, BTREE_RV_DEBUG_ID_INS_NEW_OVF);
    }
#endif /* !NDEBUG */
  LOG_RV_RECORD_SET_MODIFY_MODE (&insert_helper->leaf_addr,
				 LOG_RV_RECORD_UPDATE_PARTIAL);

  /* Update link in leaf record. */
  btree_leaf_record_change_overflow_link (thread_p, btid_int, leaf_rec,
					  &ovfl_vpid,
					  &insert_helper->rv_redo_data_ptr);

  /* Update record in page. */
  if (spage_update (thread_p, leaf_page, search_key->slotid, leaf_rec)
      != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  if (insert_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Successfully %s by creating "
		     "new overflow the object %d|%d|%d, "
		     "class_oid %d|%d|%d, mvcc_info=%llu|%llu, "
		     "in overflow %d|%d slotid=%d, key=%s, slotid=%d, "
		     "leaf_page=%d|%d, lsa=%lld|%d, in index (%d, %d|%d).\n",
		     insert_helper->is_relocation_for_unique ?
		     "relocated" : "inserted",
		     object_info->oid.volid, object_info->oid.pageid,
		     object_info->oid.slotid, object_info->class_oid.volid,
		     object_info->class_oid.pageid,
		     object_info->class_oid.slotid,
		     (unsigned long long int)
		     object_info->mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     object_info->mvcc_info.delete_mvccid,
		     pgbuf_get_volume_id (ovfl_page),
		     pgbuf_get_page_id (ovfl_page), search_key->slotid,
		     insert_helper->printed_key != NULL ?
		     insert_helper->printed_key : "(unknown)",
		     search_key->slotid,
		     pgbuf_get_volume_id (leaf_page),
		     pgbuf_get_page_id (leaf_page),
		     (long long int) pgbuf_get_lsa (leaf_page)->pageid,
		     (int) pgbuf_get_lsa (leaf_page)->offset,
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  /* Logging changes on leaf. */
  rv_redo_data_length =
    CAST_BUFLEN (insert_helper->rv_redo_data_ptr
		 - insert_helper->rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  if (insert_helper->is_relocation_for_unique)
    {
      /* Let caller handle all leaf logging. */
    }
  else if (insert_helper->purpose == BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE)
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (leaf_page),
			     insert_helper->leaf_addr.offset, leaf_page,
			     rv_redo_data_length, insert_helper->rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }
  else				/* BTREE_OP_INSERT_NEW_OBJECT */
    {
      log_append_undoredo_data (thread_p, insert_helper->rcvindex,
				&insert_helper->leaf_addr,
				insert_helper->rv_undo_data_length,
				rv_redo_data_length,
				insert_helper->rv_undo_data,
				insert_helper->rv_redo_data);
    }

  /* Mark pages dirty and free overflow page. */
  pgbuf_set_dirty (thread_p, ovfl_page, FREE);
  pgbuf_set_dirty (thread_p, leaf_page, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_key_append_object_to_overflow () - Insert object in an existing
 *					    overflow page. The page must be
 *					    checked for free space before
 *					    calling this function.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * btid_int (in)	    : B-tree info.
 * ovfl_page (in)	    : Overflow page.
 * object_info (in)	    : Object & info to insert.
 * insert_helper (in)	    : B-tree insert helper.
 */
static int
btree_key_append_object_to_overflow (THREAD_ENTRY * thread_p,
				     BTID_INT * btid_int, PAGE_PTR ovfl_page,
				     BTREE_OBJECT_INFO * object_info,
				     BTREE_INSERT_HELPER * insert_helper)
{
  int ret = NO_ERROR;
  RECDES ovfl_rec;
  char ovfl_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  LOG_DATA_ADDR addr;
  OID *oid = NULL;

  /* Redo recovery. */
  /* Do not use insert_helper redo recovery data since this is not leaf. */
  char rv_redo_data_buffer[BTREE_RV_REDO_BUFFER_SIZE + BTREE_MAX_ALIGN];
  char *rv_redo_data = PTR_ALIGN (rv_redo_data_buffer, BTREE_MAX_ALIGN);
  char *rv_redo_data_ptr = rv_redo_data;
  int rv_redo_data_length;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (ovfl_page != NULL);
  assert (object_info != NULL);
  assert (insert_helper != NULL);

  /* Prepare record. */
  ovfl_rec.type = REC_HOME;
  ovfl_rec.area_size = DB_PAGESIZE;
  ovfl_rec.data = PTR_ALIGN (ovfl_rec_buf, BTREE_MAX_ALIGN);

  /* Get record. */
  if (spage_get_record (ovfl_page, 1, &ovfl_rec, COPY) != S_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  /* Prepare logging. */
  addr.offset = 1;
  addr.pgptr = ovfl_page;
  addr.vfid = &btid_int->sys_btid->vfid;

#if !defined (NDEBUG)
  /* For debugging recovery. */
  BTREE_RV_REDO_SET_DEBUG_INFO (&addr, rv_redo_data_ptr, btid_int,
				BTREE_RV_DEBUG_ID_INS_OLD_OVF);
#endif /* NDEBUG */
  BTREE_RV_REDO_SET_OVERFLOW_NODE (&addr);
  LOG_RV_RECORD_SET_MODIFY_MODE (&addr, LOG_RV_RECORD_UPDATE_PARTIAL);

  btree_insert_object_ordered_by_oid (&ovfl_rec, btid_int, object_info,
				      &rv_redo_data_ptr);

  if (spage_update (thread_p, ovfl_page, 1, &ovfl_rec) != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  rv_redo_data_length = CAST_BUFLEN (rv_redo_data_ptr - rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  if (insert_helper->is_relocation_for_unique)
    {
      /* Append only redo data. */
      log_append_redo_data (thread_p, RVBT_RECORD_MODIFY_NO_UNDO, &addr,
			    CAST_BUFLEN (rv_redo_data_ptr - rv_redo_data),
			    rv_redo_data);
    }
  else if (insert_helper->purpose == BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE)
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (ovfl_page), addr.offset,
			     ovfl_page, rv_redo_data_length, rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }
  else				/* BTREE_OP_INSERT_NEW_OBJECT */
    {
      log_append_undoredo_data (thread_p, insert_helper->rcvindex, &addr,
				insert_helper->rv_undo_data_length,
				rv_redo_data_length,
				insert_helper->rv_undo_data, rv_redo_data);
    }

  if (insert_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Successfully %s at the "
		     "end of record the object %d|%d|%d, "
		     "class_oid %d|%d|%d, mvcc_info=%llu|%llu, "
		     "in overflow %d|%d  key=%s, in index (%d, %d|%d).\n",
		     insert_helper->is_relocation_for_unique ?
		     "relocated" : "inserted",
		     object_info->oid.volid, object_info->oid.pageid,
		     object_info->oid.slotid, object_info->class_oid.volid,
		     object_info->class_oid.pageid,
		     object_info->class_oid.slotid,
		     (unsigned long long int)
		     object_info->mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     object_info->mvcc_info.delete_mvccid,
		     pgbuf_get_volume_id (ovfl_page),
		     pgbuf_get_page_id (ovfl_page),
		     insert_helper->printed_key != NULL ?
		     insert_helper->printed_key : "(unknown)",
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  pgbuf_set_dirty (thread_p, ovfl_page, DONT_FREE);

  return NO_ERROR;
}

/*
 *
 */
static int
btree_rv_write_log_record_for_key_insert (char *log_rec, int *log_length,
					  INT16 key_len, RECDES * recp)
{
  *(INT16 *) ((char *) log_rec + LOFFS1) = key_len;
  *(INT16 *) ((char *) log_rec + LOFFS2) = BTREE_LEAF_NODE;
  *(INT16 *) ((char *) log_rec + LOFFS3) = recp->type;
  memcpy ((char *) log_rec + LOFFS4, recp->data, recp->length);

  *log_length = recp->length + LOFFS4;

  return NO_ERROR;
}

static int
btree_rv_write_log_record (char *log_rec, int *log_length, RECDES * recp,
			   BTREE_NODE_TYPE node_type)
{
  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_NON_LEAF_NODE);

  *(INT16 *) ((char *) log_rec + OFFS1) = node_type;
  *(INT16 *) ((char *) log_rec + OFFS2) = recp->type;
  memcpy ((char *) log_rec + OFFS3, recp->data, recp->length);

  *log_length = recp->length + OFFS3;

  return NO_ERROR;
}

/*
 * btree_find_free_overflow_oids_page () - Find overflow page that has enough
 *					   free space to store a new object.
 *
 * return		: Error code.
 * thread_p (in)	: Thread entry.
 * btid (in)		: B-tree info.
 * first_ovfl_vpid (in) : VPID of first overflow page (or VPID NULL if there
 *			  is no overflow).
 * overflow_page (out)	: Output overflow page with enough free space.
 */
static int
btree_find_free_overflow_oids_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    VPID * first_ovfl_vpid,
				    PAGE_PTR * overflow_page)
{
  VPID ovfl_vpid;
  int space_needed = BTREE_OBJECT_FIXED_SIZE (btid);
  int error_code = NO_ERROR;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (first_ovfl_vpid != NULL);
  assert (overflow_page != NULL && *overflow_page == NULL);

  ovfl_vpid = *first_ovfl_vpid;

  while (!VPID_ISNULL (&ovfl_vpid))
    {
      *overflow_page =
	pgbuf_fix (thread_p, &ovfl_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (*overflow_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}

#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, *overflow_page, PAGE_BTREE);
#endif /* !NDEBUG */

      if (spage_max_space_for_new_record (thread_p, *overflow_page)
	  > space_needed)
	{
	  return NO_ERROR;
	}

      btree_get_next_overflow_vpid (*overflow_page, &ovfl_vpid);

      pgbuf_unfix_and_init (thread_p, *overflow_page);
    }

  return NO_ERROR;
}

/*
 * btree_find_oid_and_its_page () - Find OID in leaf/overflow pages and output
 *				    its position.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * btid_int (in)	  : B-tree info.
 * oid (in)		  : Object OID.
 * leaf_page (in)	  : Fixed leaf page (where object's key is found).
 * purpose (in)		  : Purpose/context for the function call.
 * match_mvccid (in)	  : Pointer to MVCCID to be matched by insert or
 *			    delete MVCCID.
 * leaf_record (in)	  : Key leaf record.
 * leaf_rec_info (in)	  : Key leaf record info.
 * after_key_offset (in)  : Offset in leaf record where packed key is ended.
 * found_page (out)	  : Outputs leaf or overflow page where object is
 *			    found.
 * prev_page (out)	  : Previous page of the overflow page where object
 *			    object is found. If object is in leaf it will
 *			    output NULL. If object is in first overflow, it
 *			    will output leaf page. If argument is NULL,
 *			    previous overflow page is unfixed.
 * offset_to_object (out) : Offset to object in the record of leaf/overflow.
 */
static int
btree_find_oid_and_its_page (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			     OID * oid, PAGE_PTR leaf_page,
			     BTREE_OP_PURPOSE purpose, MVCCID * match_mvccid,
			     RECDES * leaf_record, LEAF_REC * leaf_rec_info,
			     int after_key_offset, PAGE_PTR * found_page,
			     PAGE_PTR * prev_page, int *offset_to_object,
			     BTREE_MVCC_INFO * object_mvcc_info)
{
  int error_code = NO_ERROR;
  VPID overflow_vpid;
  PAGE_PTR overflow_page = NULL;
  PAGE_PTR prev_overflow_page = NULL;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (oid != NULL);
  assert (leaf_page != NULL);
  assert (leaf_record != NULL);
  assert (leaf_rec_info != NULL);
  assert (after_key_offset > 0);
  assert (prev_page == NULL || *prev_page == NULL);
  assert (found_page != NULL && *found_page == NULL);
  assert (offset_to_object != NULL);

  /* Find object in leaf. */
  error_code =
    btree_find_oid_from_leaf (thread_p, btid_int, leaf_record,
			      after_key_offset, oid, match_mvccid,
			      purpose, offset_to_object, object_mvcc_info);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (*offset_to_object != NOT_FOUND)
    {
      /* Found object. */
      *found_page = leaf_page;
      return NO_ERROR;
    }
  if (VPID_ISNULL (&leaf_rec_info->ovfl))
    {
      /* Not found. */
      return NO_ERROR;
    }
  /* Search through overflow pages. */
  VPID_COPY (&overflow_vpid, &leaf_rec_info->ovfl);
  do
    {
      overflow_page =
	pgbuf_fix (thread_p, &overflow_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (overflow_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
      error_code =
	btree_find_oid_from_ovfl (thread_p, btid_int, overflow_page, oid,
				  purpose, match_mvccid, offset_to_object,
				  object_mvcc_info);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      if (*offset_to_object != NOT_FOUND)
	{
	  /* Object was found. Stop looking. */
	  break;
	}
      /* Object was not found. Go to next overflow page. */
      if (prev_overflow_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, prev_overflow_page);
	}
      prev_overflow_page = overflow_page;
      overflow_page = NULL;

      error_code =
	btree_get_next_overflow_vpid (prev_overflow_page, &overflow_vpid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }
  while (!VPID_ISNULL (&overflow_vpid));

  if (*offset_to_object == NOT_FOUND)
    {
      /* Not found. */
      assert (overflow_page == NULL);
      if (prev_overflow_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, prev_overflow_page);
	}
    }
  else
    {
      assert (overflow_page != NULL);
      *found_page = overflow_page;
      if (prev_page != NULL)
	{
	  *prev_page =
	    prev_overflow_page != NULL ? prev_overflow_page : leaf_page;
	}
      else if (prev_overflow_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, prev_overflow_page);
	}
    }
  return NO_ERROR;

error:
  assert_release (error_code != NO_ERROR);

  if (overflow_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, overflow_page);
    }
  if (prev_overflow_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, prev_overflow_page);
    }
  return error_code;
}

/*
 * btree_find_oid_does_mvcc_info_match () - Match an object by its MVCC info
 *					    and the purpose of search.
 *
 * return	     : Error code.
 * thread_p (in)     : Thread entry.
 * mvcc_info (in)    : Object MVCC info.
 * purpose (in)	     : Btree operation purpose.
 * match_mvccid (in) : Pointer to MVCCID to be matched by insert or by delete
 *		       MVCCID.
 * is_match (out)    : Outputs true if object MVCC info matches the
 *		       expectations.
 *
 * NOTE: This function can handle mismatches between information stored in
 *	 heap and b-tree. Because vacuum system doesn't clean the entries
 *	 for one object in both heap and b-trees, the information found in
 *	 them can be different (e.g. one can have insert MVCCID cleaned while
 *	 the other doesn't).
 *	 Moreover, if the object OID's are reusable, there can be duplicate
 *	 OID's in b-tree (one is deleted and must be vacuumed and one is
 *	 newer and can be recently inserted or even recently deleted).
 *	 Based on purpose of the search, we try to match the insert MVCCID or
 *	 delete MVCCID or just check that object doesn't have a valid delete
 *	 MVCCID.
 */
static int
btree_find_oid_does_mvcc_info_match (THREAD_ENTRY * thread_p,
				     BTREE_MVCC_INFO * mvcc_info,
				     BTREE_OP_PURPOSE purpose,
				     MVCCID * match_mvccid, bool * is_match)
{
  /* Assert expected arguments. */
  assert (mvcc_info != NULL);
  assert (is_match != NULL);

  *is_match = false;
  switch (purpose)
    {
    case BTREE_OP_DELETE_VACUUM_INSID:
      /* Match insert MVCCID to vacuum. */
      assert (match_mvccid != NULL
	      && MVCCID_IS_NOT_ALL_VISIBLE (*match_mvccid));
      if (BTREE_MVCC_INFO_HAS_INSID (mvcc_info)
	  && mvcc_info->insert_mvccid == *match_mvccid)
	{
	  /* This is the insert MVCCID to be vacuumed. */
	  *is_match = true;
	}
      else
	{
	  /* Not a match. */
	}
      return NO_ERROR;

    case BTREE_OP_DELETE_VACUUM_OBJECT:
      /* Match delete MVCCID to not remove the wrong object (reused). */
      assert (match_mvccid != NULL
	      && MVCCID_IS_NOT_ALL_VISIBLE (*match_mvccid));
      if (BTREE_MVCC_INFO_HAS_DELID (mvcc_info)
	  && mvcc_info->delete_mvccid == *match_mvccid)
	{
	  /* This is the object to be vacuumed. */
	  *is_match = true;
	}
      else
	{
	  /* Not a match. */
	}
      return NO_ERROR;

    case BTREE_OP_DELETE_UNDO_INSERT_DELID:
      /* We want to rollback an MVCC delete. Just removing the delete MVCCID
       * is enough. If delete MVCCID does not match, it means it must be
       * an older object, before being reused, which was not vacuumed yet.
       */
      assert (match_mvccid != NULL
	      && MVCCID_IS_NOT_ALL_VISIBLE (*match_mvccid));
      if (BTREE_MVCC_INFO_HAS_DELID (mvcc_info))
	{
	  if (mvcc_info->delete_mvccid == *match_mvccid)
	    {
	      *is_match = true;
	    }
	  else
	    {
	      /* Not a match. */
	    }
	  return NO_ERROR;
	}
      /* No delete MVCCID. */
      /* This is actually not expected. We could expect a deleted object,
       * not yet vacuumed, but we cannot expect an inserted object, alive,
       * different than the one we just deleted and want to rollback.
       */
      assert_release (false);
      return ER_FAILED;

    case BTREE_OP_DELETE_UNDO_INSERT:
      /* We just inserted this object and want to remove it. Insert MVCCID
       * must match and should not be deleted.
       */
      if (BTREE_MVCC_INFO_IS_DELID_VALID (mvcc_info))
	{
	  /* This must be an old object, reused, but not yet vacuumed. */
	  /* Not a match. */
	  return NO_ERROR;
	}
      if (match_mvccid != NULL && MVCCID_IS_NOT_ALL_VISIBLE (*match_mvccid))
	{
	  /* We must match insert MVCCID. */
	  if (BTREE_MVCC_INFO_INSID (mvcc_info) == *match_mvccid)
	    {
	      /* This is a match. */
	      *is_match = true;
	      return NO_ERROR;
	    }
	  else
	    {
	      /* Insert MVCCID is different or doesn't exist. We don't expect
	       * such case.
	       */
	      assert_release (false);
	      return NO_ERROR;
	    }
	}
      /* We don't have an insert MVCCID to be matched. Object MVCC info should
       * either have no insert MVCCID or it should be all visible.
       */
      if (BTREE_MVCC_INFO_IS_INSID_NOT_ALL_VISIBLE (mvcc_info))
	{
	  /* We don't expect this case. */
	  assert_release (false);
	  return ER_FAILED;
	}
      else
	{
	  /* This is a match. */
	  *is_match = true;
	  return NO_ERROR;
	}
      /* Unreachable. */
      assert_release (false);
      return ER_FAILED;

    case BTREE_OP_DELETE_OBJECT_PHYSICAL:
    case BTREE_OP_INSERT_MVCC_DELID:
    default:
      /* We are looking for an object not deleted. It is possible to find same
       * OID, but deleted, if reusable.
       */
      if (!BTREE_MVCC_INFO_IS_DELID_VALID (mvcc_info))
	{
	  /* This is the object we want to delete. */
	  *is_match = true;
	}
      else
	{
	  /* This must be same OID but before being reused. It should be
	   * vacuumed soon.
	   */
	  /* Not a match. */
	}
      return NO_ERROR;
    }
}

/*
 * btree_find_oid_from_leaf () - Find OID in leaf record and output its
 *				 offset and MVCC info.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * btid (in)		  : B-tree info.
 * leaf_record (in)	  : Leaf record.
 * after_key_offset (in)  : Offset in record where packed key is ended.
 * oid (in)		  : OID of object to find.
 * match_mvccid (in)	  : Non-null value to be matched by either insert or
 *			    delete MVCCID.
 * purpose (in)		  : Purpose/context for the call.
 * offset_to_object (out) : Output offset to found object or NOT_FOUND.
 * mvcc_info (out)	  : Output object MVCC info when found.
 */
static int
btree_find_oid_from_leaf (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  RECDES * leaf_record, int after_key_offset,
			  OID * oid, MVCCID * match_mvccid,
			  BTREE_OP_PURPOSE purpose, int *offset_to_object,
			  BTREE_MVCC_INFO * mvcc_info)
{
  OR_BUF buf;			/* Buffer to read record. */
  OID inst_oid;			/* OID read from record. */
  OID class_oid;		/* Class OID read from record. */
  BTREE_MVCC_INFO local_mvcc_info;	/* Local MVCC info. */
  int error_code = NO_ERROR;	/* Error code. */
  bool is_match = false;	/* Set to true when OID is found and
				 * MVCC info matches expectations.
				 */
  bool is_first = true;		/* Used to skip packed key. */

  assert (btid != NULL);
  assert (leaf_record != NULL);
  assert (after_key_offset > 0);
  assert (oid != NULL);
  assert (offset_to_object != NULL);

  if (mvcc_info == NULL)
    {
      /* MVCC info is not for output but is required internally. */
      mvcc_info = &local_mvcc_info;
    }

  BTREE_RECORD_OR_BUF_INIT (buf, leaf_record);
  while (buf.ptr < buf.endptr)
    {
      /* If the object has fixed size, it is forced to have both insert MVCCID
       * and delete MVCCID. This can happen if:
       * 1. The index is unique, and this is not the key's first object.
       * 2. The keys has overflow OID's and this is the first object.
       * In any other cases follow the MVCC flags.
       */
      *offset_to_object = CAST_BUFLEN (buf.ptr - buf.buffer);

      /* Get object and all its information from record. */
      if (btree_or_get_object (&buf, btid, BTREE_LEAF_NODE, after_key_offset,
			       &inst_oid, &class_oid, mvcc_info) != NO_ERROR)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}

      /* Is the OID we're searching? */
      if (OID_EQ (&inst_oid, oid))
	{
	  /* OID matches. */
	  /* Is MVCC info according to expectations? */
	  error_code =
	    btree_find_oid_does_mvcc_info_match (thread_p, mvcc_info,
						 purpose, match_mvccid,
						 &is_match);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	  if (is_match)
	    {
	      /* Object is a match. */
	      return NO_ERROR;
	    }
	  /* Not our object. */
	  /* Continue looking. */
	}
      if (is_first)
	{
	  /* Skip key. */
	  or_seek (&buf, after_key_offset);
	  is_first = false;
	}
    }
  /* Object was not found. */
  *offset_to_object = NOT_FOUND;
  return NO_ERROR;

error:
  assert_release (error_code != NO_ERROR);
  *offset_to_object = NOT_FOUND;
  return error_code;
}

/*
 * btree_find_oid_from_ovfl () - Find object in overflow page.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * btid_int (in)	  : B-tree info.
 * overflow_page (in)	  : Overflow page.
 * oid (in)		  : OID to find.
 * purpose (in)		  : Purpose of call.
 * match_mvccid (in)	  : If not NULL, this MVCCID may need to be matched
 *			    by either insert MVCCID or delete MVCCID.
 * offset_to_object (out) : If object is found, it saves the offset to object.
 *			    Otherwise, NOT_FOUND is output.
 * mvcc_info (out)	  : Output MVCC info if object is found.
 */
static int
btree_find_oid_from_ovfl (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			  PAGE_PTR overflow_page, OID * oid,
			  BTREE_OP_PURPOSE purpose, MVCCID * match_mvccid,
			  int *offset_to_object, BTREE_MVCC_INFO * mvcc_info)
{
  OID inst_oid;			/* OID read from record. */
  int min, mid, max;		/* min, mid, max values used for
				 * binary search.
				 */
  int num_oids;			/* Number of objects in record. */
  int size;			/* Object and all its info size */
  int error_code = NO_ERROR;	/* Error code. */
  char *oid_ptr = NULL, *ptr = NULL;	/* Pointer in record data. */
  BTREE_MVCC_INFO local_mvcc_info;	/* Local MVCC info. */
  RECDES ovf_record;		/* Overflow record. */
  bool is_match = false;	/* Set to true if object is found and
				 * its MVCC info matches expectations.
				 */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (overflow_page != NULL);
  assert (oid != NULL);
  assert (offset_to_object != NULL);

  if (mvcc_info == NULL)
    {
      /* MVCC info is not for output but is required internally. */
      mvcc_info = &local_mvcc_info;
    }

  *offset_to_object = NOT_FOUND;
  if (spage_get_record (overflow_page, 1, &ovf_record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  /* Try early out: check first oid */
  BTREE_GET_OID (ovf_record.data, &inst_oid);
  assert ((inst_oid.slotid & BTREE_LEAF_RECORD_MASK) == 0);
  assert ((inst_oid.volid & BTREE_OID_MVCC_FLAGS_MASK) == 0);

  if (OID_LT (oid, &inst_oid))
    {
      /* Not in this page. */
      return NO_ERROR;
    }
  else if (OID_EQ (oid, &inst_oid))
    {
      /* OID matched. */
      /* Check MVCC info. */
      ptr = ovf_record.data + OR_OID_SIZE;
      if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	{
	  ptr += OR_OID_SIZE;
	}
      (void) btree_unpack_mvccinfo (ptr, mvcc_info,
				    BTREE_OID_HAS_MVCC_INSID_AND_DELID);
      error_code =
	btree_find_oid_does_mvcc_info_match (thread_p, mvcc_info, purpose,
					     match_mvccid, &is_match);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (is_match)
	{
	  /* First object is a match. */
	  *offset_to_object = 0;
	  return NO_ERROR;
	}
    }
  /* First object is not a match. */

  /* Try early out: check last object. */

  /* Find last object. */
  /* Compute size of one object and all its info. */
  size = BTREE_OBJECT_FIXED_SIZE (btid_int);
  /* Get last object. */
  oid_ptr = ovf_record.data + (ovf_record.length - size);
  BTREE_GET_OID (oid_ptr, &inst_oid);
  assert ((inst_oid.slotid & BTREE_LEAF_RECORD_MASK) == 0);
  assert ((inst_oid.volid & BTREE_OID_MVCC_FLAGS_MASK) == 0);

  if (OID_GT (oid, &inst_oid))
    {
      /* Not in this page. */
      return NO_ERROR;
    }
  else if (OID_EQ (oid, &inst_oid))
    {
      /* OID matched. */
      /* Check MVCC info. */
      ptr = oid_ptr + OR_OID_SIZE;
      if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	{
	  ptr += OR_OID_SIZE;
	}
      (void) btree_unpack_mvccinfo (ptr, mvcc_info,
				    BTREE_OID_HAS_MVCC_INSID_AND_DELID);
      error_code =
	btree_find_oid_does_mvcc_info_match (thread_p, mvcc_info, purpose,
					     match_mvccid, &is_match);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (is_match)
	{
	  /* Last object is a match. */
	  *offset_to_object = CAST_BUFLEN (oid_ptr - ovf_record.data);
	  return NO_ERROR;
	}
    }
  /* Early outs failed. Do a binary search after OID. */

  num_oids =
    btree_record_get_num_oids (thread_p, btid_int, &ovf_record, 0,
			       BTREE_OVERFLOW_NODE);

  /* First and last object were already checked. Try others. */
  min = 1;
  max = num_oids - 2;

  /* Search OID. */
  while (min <= max)
    {
      /* Get MID object. */
      mid = (min + max) / 2;
      oid_ptr = ovf_record.data + (size * mid);
      BTREE_GET_OID (oid_ptr, &inst_oid);
      assert ((inst_oid.slotid & BTREE_LEAF_RECORD_MASK) == 0);
      assert ((inst_oid.volid & BTREE_OID_MVCC_FLAGS_MASK) == 0);

      /* Check OID. */
      if (OID_EQ (oid, &inst_oid))
	{
	  /* OID matched. */
	  /* Check MVCC info. */
	  ptr = oid_ptr + OR_OID_SIZE;
	  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	    {
	      ptr += OR_OID_SIZE;
	    }
	  (void) btree_unpack_mvccinfo (ptr, mvcc_info,
					BTREE_OID_HAS_MVCC_INSID_AND_DELID);
	  error_code =
	    btree_find_oid_does_mvcc_info_match (thread_p, mvcc_info,
						 purpose, match_mvccid,
						 &is_match);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  if (is_match)
	    {
	      /* Object is a match. */
	      *offset_to_object = CAST_BUFLEN (oid_ptr - ovf_record.data);
	    }
	  /* Not in this page. */
	  return NO_ERROR;
	}
      else if (OID_GT (oid, &inst_oid))
	{
	  /* Try between mid + 1 and max. */
	  min = mid + 1;
	}
      else
	{
	  /* Try between min and mid - 1. */
	  assert (OID_LT (oid, &inst_oid));
	  max = mid - 1;
	}
    }

  /* Not found. */
  return NO_ERROR;
}

/*
 * btree_get_prefix_separator () -
 *   return: db_value containing the prefix key.  This must be
 *           cleared when it is done being used.
 *   key1(in): first key
 *   key2(in): second key
 *   prefix_key(in):
 *
 * Note: This function finds the prefix (the separator) of two strings.
 * Currently this is only done for one of the six string types,
 * but with multi-column indexes and uniques coming, we may want
 * to do prefix keys for sequences as well.
 *
 * The purpose of this routine is to find a prefix that is
 * greater than or equal to the first key but strictly less
 * than the second key.  This routine assumes that the second
 * key is strictly greater than the first key.
 *
 * If this function could not generate common prefix key
 * (ex: key domain == integer)
 * copy key2 to prefix_key (because Index separator use key2 in general case)
 */
/* TODO: change key generation
 * (db_string_unique_prefix, pr_midxkey_unique_prefix)
 */
int
btree_get_prefix_separator (const DB_VALUE * key1, const DB_VALUE * key2,
			    DB_VALUE * prefix_key, TP_DOMAIN * key_domain)
{
  int c;
  int err = NO_ERROR;

  assert (DB_IS_NULL (key1) ||
	  (DB_VALUE_DOMAIN_TYPE (key1) == DB_VALUE_DOMAIN_TYPE (key2)));
  assert (!DB_IS_NULL (key2));
  assert_release (key_domain != NULL);

#if !defined(NDEBUG)
  c =
    btree_compare_key ((DB_VALUE *) key1, (DB_VALUE *) key2, key_domain, 1, 1,
		       NULL);
  assert (c == DB_LT);
#endif

  if (DB_VALUE_DOMAIN_TYPE (key1) == DB_TYPE_MIDXKEY)
    {
      assert_release (TP_DOMAIN_TYPE (key_domain) == DB_TYPE_MIDXKEY);

      err = pr_midxkey_unique_prefix (key1, key2, prefix_key);
    }
  else if (pr_is_string_type (DB_VALUE_DOMAIN_TYPE (key1)))
    {
      assert_release (TP_DOMAIN_TYPE (key_domain) != DB_TYPE_MIDXKEY);

      err = db_string_unique_prefix (key1, key2, prefix_key, key_domain);
    }
  else
    {
      /* In this case, key2 is used as separator in B+tree
       * so, copy key2 to prefix_key
       */
      err = pr_clone_value (key2, prefix_key);
    }

  if (err != NO_ERROR)
    {
      assert_release (false);
      return ER_FAILED;
    }

  c =
    btree_compare_key ((DB_VALUE *) key1, prefix_key, key_domain, 1, 1, NULL);

  if (c != DB_LT)
    {
      assert_release (false);
      return ER_FAILED;
    }

  c =
    btree_compare_key (prefix_key, (DB_VALUE *) key2, key_domain, 1, 1, NULL);

  if (!(c == DB_LT || c == DB_EQ))
    {
      assert_release (false);
      return ER_FAILED;
    }

  return err;
}

/*
 * btree_find_split_point () -
 *   return: the key or key separator (prefix) to be moved to the
 *           parent page, or NULL_KEY. The length of the returned
 *           key, or prefix, is set in mid_keylen. The parameter
 *           mid_slot is set to the record number of the split point record.
 *   btid(in):
 *   page_ptr(in): Pointer to the page
 *   mid_slot(out): Set to contain the record number for the split point slot
 *   key(in): Key to be inserted to the index
 *   clear_midkey(in):
 *
 * Note: Finds the split point of the given page by considering the
 * length of the existing records and the length of the key.
 * For a leaf page split operation, if there are n keys in the
 * page, then mid_slot can be set to :
 *
 *              0 : all the records in the page are to be moved to the newly
 *                  allocated page, key is to be inserted into the original
 *                  page. Mid_key is between key and the first record key.
 *
 *              n : all the records will be kept in the original page. Key is
 *                  to be inserted to the newly allocated page. Mid_key is
 *                  between the last record key and the key.
 *      otherwise : slot point is in the range 1 to n-1, inclusive. The page
 *                  is to be split into half.
 *
 * Note: the returned db_value should be cleared and FREED by the caller.
 */
static DB_VALUE *
btree_find_split_point (THREAD_ENTRY * thread_p,
			BTID_INT * btid, PAGE_PTR page_ptr,
			int *mid_slot, DB_VALUE * key, bool * clear_midkey)
{
  RECDES rec;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  INT16 slot_id = NULL_SLOTID;
  int new_ent_size = 0, new_fence_size = 0;
  int key_cnt = 0, key_len = 0, max_key_len = 0, offset = 0;
  INT16 tot_rec = 0;
  int i = 0, mid_size = 0;
  bool m_clear_key = false, n_clear_key = false;
  DB_VALUE *mid_key = NULL, *next_key = NULL, *prefix_key = NULL, *tmp_key;
  bool is_key_added_to_left = false, found = false;
  NON_LEAF_REC nleaf_pnt;
  LEAF_REC leaf_pnt;
  BTREE_SEARCH_KEY_HELPER search_key;
  int stop_at = 0, start_with = 0;
  int left_fence_size = 0;
  int right_fence_size = 0;
  int left_size = 0;
  int left_max_size = 0;
  int left_min_size = 0;
  int right_max_size = 0;
  int record_size = 0;

  key_cnt = btree_node_number_of_keys (page_ptr);
  if (key_cnt <= 0)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_find_split_point: node key count underflow: %d",
		    key_cnt);
      goto error;
    }

  /* get the page header */
  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_find_split_point: get node header failure: %d",
		    key_cnt);
      goto error;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  key_len = btree_get_disk_size_of_key (key);
  key_len = BTREE_GET_KEY_LEN_IN_PAGE (key_len);

  /* find the slot position of the key if it is to be located in the page */
  if (node_type == BTREE_LEAF_NODE)
    {
      if (btree_search_leaf_page (thread_p, btid, page_ptr, key, &search_key)
	  != NO_ERROR)
	{
	  assert (false);
	  goto error;
	}
      found = (search_key.result == BTREE_KEY_FOUND);
      slot_id = search_key.slotid;
      if (slot_id == NULL_SLOTID)	/* leaf search failed */
	{
	  assert (false);
	  goto error;
	}
    }
  else
    {
      found = 0;
      slot_id = NULL_SLOTID;
    }

  /* Start splitting records into left and right nodes. The algorithm
   * must consider next rules:
   * 1. The size of split records should be done as close as possible to the
   *    size indicated by split_info. Split info is not applied to page
   *    header and fences.
   * 2. Left and right nodes should have enough space for new data
   *    required by insert, and also for new fences. This applies only to
   *    leaf nodes.
   * 3. After split & insert, both nodes must have at least one non-fence
   *    record.
   */

  /* Compute maximum page size considering headers. */
  left_max_size = BTREE_NODE_MAX_SPLIT_SIZE (page_ptr);
  right_max_size = left_max_size;

  /* Compute total record size. Filter out fences here. */
  start_with = 1;
  if (btree_is_fence_key (page_ptr, start_with))
    {
      assert (node_type == BTREE_LEAF_NODE);
      left_fence_size = spage_get_space_for_record (page_ptr, start_with);

      /* Left fence will be included in left leaf. Subtract its size from
       * the maximum size allowed.
       */
      left_max_size -= left_fence_size;
      start_with++;
    }

  stop_at = key_cnt;
  if (btree_is_fence_key (page_ptr, stop_at))
    {
      assert (node_type == BTREE_LEAF_NODE);
      right_fence_size = spage_get_space_for_record (page_ptr, stop_at);

      /* Right fence will be included in right leaf. Subtract its size from
       * the maximum size allowed.
       */
      right_max_size -= right_fence_size;
      stop_at--;
    }

  if (node_type == BTREE_LEAF_NODE)
    {
      if (found)
	{
	  /* New object is added to existing key. */
	  new_ent_size = BTREE_OBJECT_FIXED_SIZE (btid);
	}
      else
	{
	  /* New key will be inserted into leaves. */
	  new_ent_size = LEAF_ENTRY_MAX_SIZE (key_len);
	}

      /* Until we know where new entity belongs, we must reserve enough
       * space in both left and right leaf.
       */
      left_max_size -= new_ent_size;
      right_max_size -= new_ent_size;

      /* New fences are added to both leaves: an upper fence for left leaf and
       * lower fence for right leaf. We don't know their size yet, but we
       * can estimate the largest size using node maximum key length.
       */
      /* TODO: Fences currently optimize only midxkey key types. Save storage
       *       by not using fence keys when they are not required.
       */
      max_key_len = MAX (key_len, header->max_key_len);
      new_fence_size = LEAF_FENCE_MAX_SIZE (max_key_len);

      /* Adjust maximum size for both leaves. */
      left_max_size -= new_fence_size;
      right_max_size -= new_fence_size;
    }
  else
    {
      /* New key is not added to non-leaf. */
      new_ent_size = 0;

      /* No fences in non-leaf. */
      new_fence_size = 0;
    }

  /* First find out the size of the data on the page, don't count the
   * header record or fences.
   */
  for (i = start_with, tot_rec = 0; i <= stop_at; i++)
    {
      tot_rec += spage_get_space_for_record (page_ptr, i);
    }
  tot_rec += new_ent_size;

  /* Compute mid_size, the desired size of left node according to split info.
   */
  mid_size = btree_split_find_pivot (tot_rec, &(header->split_info));

  /* Split records and new entity considering mid_size, left_max_size, and
   * right_max_size.
   * Since we work with left node, translate right_max_size into left_min_size
   * by subtracting from total records size.
   */
  left_min_size = tot_rec - right_max_size;
  /* Safe guard. */
  assert (left_min_size < left_max_size);

  /* Find the last slot ID belonging to left node (and save it in the mid_slot
   * pointer).
   */
  for (i = start_with, left_size = 0; true; i++)
    {
      if (node_type == BTREE_LEAF_NODE && i == slot_id)
	{
	  /* New entity belongs to left leaf. Ignore it for non-leaf. */
	  is_key_added_to_left = true;
	  left_size += new_ent_size;

	  /* Adjust leaf sizes now that we know the key belongs to left leaf.
	   */
	  left_max_size += new_ent_size;
	  right_max_size += new_ent_size;
	  left_min_size -= new_ent_size;
	}
      record_size = spage_get_space_for_record (page_ptr, i);
      if (left_size < left_min_size)
	{
	  /* Right node is too large. Keep adding records to left node. */
	  left_size += record_size;
	  continue;
	}
      /* Add new record to left and check its new size. */
      left_size += record_size;
      if (left_size > MIN (left_max_size, mid_size))
	{
	  /* We reached the desired size, or the maximum size allowed for left
	   * node. Stop one record before this.
	   */
	  *mid_slot = i - 1;
#if !defined (NDEBUG)
	  /* Update left_size for debug checks. */
	  left_size -= spage_get_space_for_record (page_ptr, i);
#endif /* !NDEBUG */
	  break;
	}
      if (i == stop_at)
	{
	  /* All non-fence records have been processed. Stop. */
	  *mid_slot = i;
	  break;
	}
    }

  /* Adjust mid_slot according to rule #3. */
  if (*mid_slot == (start_with - 1)
      && (node_type == BTREE_NON_LEAF_NODE || !is_key_added_to_left))
    {
      /* There are no records in the left node. Adjust mid_slot. */
      (*mid_slot)++;

#if !defined (NDEBUG)
      /* Update left_size for debug checks. */
      left_size += spage_get_space_for_record (page_ptr, *mid_slot);
#endif /* !NDEBUG */
    }
  if (*mid_slot == stop_at
      && (node_type == BTREE_NON_LEAF_NODE || is_key_added_to_left))
    {
      /* There are no records in the right leaf. Adjust mid_slot. */
      (*mid_slot)--;
#if !defined (NDEBUG)
      /* Update left_size for debug checks. */
      left_size -= spage_get_space_for_record (page_ptr, *mid_slot);
#endif /* !NDEBUG */
    }

  /* Safe guard: Rule #2. */
  assert (left_size <= left_max_size);
  assert (left_size >= left_min_size);
  assert (tot_rec - left_size <= right_max_size);
  assert (left_size + (is_key_added_to_left ? new_ent_size : 0)
	  + new_fence_size <= BTREE_NODE_MAX_SPLIT_SIZE (page_ptr));
  assert (tot_rec - left_size + (!is_key_added_to_left ? new_ent_size : 0)
	  + new_fence_size <= BTREE_NODE_MAX_SPLIT_SIZE (page_ptr));

  /* Safe guard: Rules #3. */
  /* Left node will have at least one non-fence record. */
  assert (*mid_slot >= start_with
	  || (*mid_slot == (start_with - 1) && node_type == BTREE_LEAF_NODE
	      && is_key_added_to_left));
  /* Right node will have at least one non-fence record. */
  assert (*mid_slot < stop_at
	  || (*mid_slot == stop_at && node_type == BTREE_LEAF_NODE
	      && !is_key_added_to_left));

  /* TODO: Optimize memory usage. We don't need to allocated/deallocate all
   *       DB_VALUE types and their content in all cases.
   */
  mid_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (mid_key == NULL)
    {
      goto error;
    }

  db_make_null (mid_key);

  if (*mid_slot == (slot_id - 1) && is_key_added_to_left && !found)
    {
      /* the new key is the split key */
      PR_TYPE *pr_type;

      /* Safe guard. */
      assert (*mid_slot != key_cnt);

      if (node_type == BTREE_LEAF_NODE)
	{
	  pr_type = btid->key_type->type;
	}
      else
	{
	  pr_type = btid->nonleaf_key_type->type;
	}

      m_clear_key = false;

      (*(pr_type->setval)) (mid_key, key, m_clear_key);
    }
  else
    {
      /* the split key is one of the keys on the page */
      assert (*mid_slot > 0);
      if (spage_get_record (page_ptr, *mid_slot, &rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      /* we copy the key here because rec lives on the stack and mid_key
       * is returned from this routine.
       */
      if (node_type == BTREE_LEAF_NODE)
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &rec, mid_key,
				 (void *) &leaf_pnt, node_type, &m_clear_key,
				 &offset, COPY_KEY_VALUE, NULL) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &rec, mid_key,
				 (void *) &nleaf_pnt, node_type, &m_clear_key,
				 &offset, COPY_KEY_VALUE, NULL) != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  /* The determination of the prefix key is dependent on the next key */
  next_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (next_key == NULL)
    {
      goto error;
    }

  db_make_null (next_key);

  if (*mid_slot == key_cnt && slot_id == (key_cnt + 1))
    {
      assert (node_type == BTREE_LEAF_NODE);

      n_clear_key = true;
      if (pr_clone_value (key, next_key) != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      /* The next key is one of the keys on the page */
      assert ((*mid_slot) + 1 > 0);
      if (spage_get_record (page_ptr, (*mid_slot) + 1, &rec, PEEK)
	  != S_SUCCESS)
	{
	  goto error;
	}

      /* we copy the key here because rec lives on the stack and mid_key
       * is returned from this routine.
       */
      if (node_type == BTREE_LEAF_NODE)
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &rec, next_key,
				 (void *) &leaf_pnt, node_type, &n_clear_key,
				 &offset, COPY_KEY_VALUE, NULL) != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &rec, next_key,
				 (void *) &nleaf_pnt, node_type, &n_clear_key,
				 &offset, COPY_KEY_VALUE, NULL) != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  /* now that we have the mid key and the next key, we can determine the
   * prefix key.
   */
  prefix_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (prefix_key == NULL)
    {
      goto error;
    }

  /* Check if we can make use of prefix keys.  We can't use them in the
   * upper levels of the trees because the algorithm will fall apart.  We
   * can only use them when splitting a leaf page.
   */
  if (node_type == BTREE_LEAF_NODE)
    {
      if ((btree_get_disk_size_of_key (mid_key) >= BTREE_MAX_KEYLEN_INPAGE)
	  || (btree_get_disk_size_of_key (next_key)
	      >= BTREE_MAX_KEYLEN_INPAGE))
	{
	  /* if one of key is overflow key
	   * prefix key could be longer then max_key_len in page
	   * (that means insert could be failed)
	   * so, in this case use next key itself as prefix key
	   */
	  pr_clone_value (next_key, prefix_key);
	}
      else
	{
	  if (btree_get_prefix_separator (mid_key, next_key, prefix_key,
					  btid->key_type) != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }
  else
    {
      /* return the next_key */
      pr_clone_value (next_key, prefix_key);
    }

  *clear_midkey = true;		/* we must always clear prefix keys */

  /* replace the mid_key with the prefix_key */
  tmp_key = mid_key;
  mid_key = prefix_key;
  prefix_key = tmp_key;		/* this makes sure we clear/free the old mid key */
  goto success;

  /* error handling and cleanup. */
error:

  if (mid_key)
    {
      btree_clear_key_value (&m_clear_key, mid_key);
      db_private_free_and_init (thread_p, mid_key);
    }
  mid_key = NULL;

  /* fall through */

success:

  if (next_key)
    {
      btree_clear_key_value (&n_clear_key, next_key);
      db_private_free_and_init (thread_p, next_key);
    }
  if (prefix_key)
    {
      pr_clear_value (prefix_key);
      db_private_free_and_init (thread_p, prefix_key);
    }

  return mid_key;
}

/*
 * btree_split_find_pivot () -
 *   return:
 *   total(in):
 *   split_info(in):
 */
static int
btree_split_find_pivot (int total, BTREE_NODE_SPLIT_INFO * split_info)
{
  int split_point;

  if (split_info->pivot == 0
      || (split_info->pivot > BTREE_SPLIT_LOWER_BOUND
	  && split_info->pivot < BTREE_SPLIT_UPPER_BOUND))
    {
      split_point = CEIL_PTVDIV (total, 2);
    }
  else
    {
      split_point = (int) (total * MAX (MIN (split_info->pivot,
					     BTREE_SPLIT_MAX_PIVOT),
					BTREE_SPLIT_MIN_PIVOT));
    }

  return split_point;
}

/*
 * btree_split_next_pivot () -
 *   return:
 *   split_info(in):
 *   new_value(in):
 *   max_index(in):
 */
static int
btree_split_next_pivot (BTREE_NODE_SPLIT_INFO * split_info,
			float new_value, int max_index)
{
  float new_pivot;

  assert (0.0f <= split_info->pivot);
  assert (split_info->pivot <= 1.0f);

  split_info->index = MIN (split_info->index + 1, max_index);

  if (split_info->pivot == 0)
    {
      new_pivot = new_value;
    }
  else
    {
      /* cumulative moving average(running average) */
      new_pivot = split_info->pivot;
      new_pivot = (new_pivot + ((new_value - new_pivot) / split_info->index));
    }

  split_info->pivot = MAX (0.0f, MIN (1.0f, new_pivot));

  return NO_ERROR;
}

static bool
btree_node_is_compressed (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  PAGE_PTR page_ptr)
{
  int key_cnt;
  RECDES peek_rec;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;

  assert (btid != NULL);

  if (TP_DOMAIN_TYPE (btid->key_type) != DB_TYPE_MIDXKEY)
    {
      return false;
    }

  key_cnt = btree_node_number_of_keys (page_ptr);
  if (key_cnt < 2)
    {
      return false;
    }

  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      return false;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if (node_type == BTREE_NON_LEAF_NODE)
    {
      return false;
    }

  /* check if lower fence key */
  if (spage_get_record (page_ptr, 1, &peek_rec, PEEK) != S_SUCCESS)
    {
      assert (false);
    }
  if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE) == false)
    {
      return false;
    }

  /* check if upper fence key */
  assert (key_cnt > 0);
  if (spage_get_record (page_ptr, key_cnt, &peek_rec, PEEK) != S_SUCCESS)
    {
      assert (false);
    }
  if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE) == false)
    {
      return false;
    }

  return true;
}

static int
btree_node_common_prefix (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  PAGE_PTR page_ptr)
{
  RECDES peek_rec;
  int diff_column;
  int key_cnt;
  bool dom_is_desc = false, next_dom_is_desc = false;

  DB_VALUE lf_key, uf_key;
  bool lf_clear_key = false, uf_clear_key = false;
  int offset;
  LEAF_REC leaf_pnt;
  int error = NO_ERROR;

  if (btree_node_is_compressed (thread_p, btid, page_ptr) == false)
    {
      return 0;
    }

  key_cnt = btree_node_number_of_keys (page_ptr);
  assert (key_cnt >= 2);

  spage_get_record (page_ptr, 1, &peek_rec, PEEK);
  error = btree_read_record_without_decompression (thread_p, btid, &peek_rec,
						   &lf_key, &leaf_pnt,
						   BTREE_LEAF_NODE,
						   &lf_clear_key, &offset,
						   PEEK_KEY_VALUE);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  assert (!btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_KEY));
  assert (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE));
  assert (DB_VALUE_TYPE (&lf_key) == DB_TYPE_MIDXKEY);

  assert (key_cnt > 0);
  spage_get_record (page_ptr, key_cnt, &peek_rec, PEEK);
  error = btree_read_record_without_decompression (thread_p, btid, &peek_rec,
						   &uf_key, &leaf_pnt,
						   BTREE_LEAF_NODE,
						   &uf_clear_key, &offset,
						   PEEK_KEY_VALUE);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  assert (!btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_KEY));
  assert (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE));
  assert (DB_VALUE_TYPE (&uf_key) == DB_TYPE_MIDXKEY);

  diff_column = pr_midxkey_common_prefix (&lf_key, &uf_key);

cleanup:
  /* clean up */
  btree_clear_key_value (&lf_clear_key, &lf_key);
  btree_clear_key_value (&uf_clear_key, &uf_key);

  if (error == NO_ERROR)
    {
      return diff_column;
    }
  else
    {
      return error;
    }
}

static int
btree_compress_records (THREAD_ENTRY * thread_p, BTID_INT * btid,
			RECDES * rec, int key_cnt)
{
  int key_len, new_key_len;
  int i, diff_column;
  DB_VALUE key, lf_key, uf_key;
  bool clear_key = false, lf_clear_key = false, uf_clear_key = false;
  int offset, new_offset;
  int key_type = BTREE_NORMAL_KEY;
  LEAF_REC leaf_pnt;
  int error = NO_ERROR;

  assert (rec != NULL);
  assert (key_cnt >= 2);
  assert (!btree_leaf_is_flaged (&rec[0], BTREE_LEAF_RECORD_OVERFLOW_KEY));
  assert (btree_leaf_is_flaged (&rec[0], BTREE_LEAF_RECORD_FENCE));
  assert (!btree_leaf_is_flaged
	  (&rec[key_cnt - 1], BTREE_LEAF_RECORD_OVERFLOW_KEY));
  assert (btree_leaf_is_flaged (&rec[key_cnt - 1], BTREE_LEAF_RECORD_FENCE));

  error = btree_read_record_without_decompression (thread_p, btid, &rec[0],
						   &lf_key, &leaf_pnt,
						   BTREE_LEAF_NODE,
						   &lf_clear_key, &offset,
						   PEEK_KEY_VALUE);
  if (error != NO_ERROR)
    {
      return error;
    }
  assert (lf_clear_key == false);

  error = btree_read_record_without_decompression (thread_p, btid,
						   &rec[key_cnt - 1], &uf_key,
						   &leaf_pnt, BTREE_LEAF_NODE,
						   &uf_clear_key, &offset,
						   PEEK_KEY_VALUE);
  if (error != NO_ERROR)
    {
      return error;
    }
  assert (uf_clear_key == false);

  diff_column = pr_midxkey_common_prefix (&lf_key, &uf_key);

  if (diff_column > 0)
    {
      /* compress prefix */
      for (i = 1; i < key_cnt - 1; i++)
	{
	  assert (!btree_leaf_is_flaged (&rec[i], BTREE_LEAF_RECORD_FENCE));

	  if (btree_leaf_is_flaged (&rec[i], BTREE_LEAF_RECORD_OVERFLOW_KEY))
	    {
	      /* do not compress overflow key */
	      continue;
	    }

	  error = btree_read_record_without_decompression (thread_p, btid,
							   &rec[i], &key,
							   &leaf_pnt,
							   BTREE_LEAF_NODE,
							   &clear_key,
							   &offset,
							   PEEK_KEY_VALUE);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  assert (clear_key == false);

	  key_len = btree_get_disk_size_of_key (&key);
	  pr_midxkey_remove_prefix (&key, diff_column);
	  new_key_len = btree_get_disk_size_of_key (&key);

	  new_key_len = DB_ALIGN (new_key_len, INT_ALIGNMENT);
	  key_len = DB_ALIGN (key_len, INT_ALIGNMENT);

	  new_offset = offset + (new_key_len - key_len);

	  /* move remaining part of oids */
	  memmove (rec[i].data + new_offset, rec[i].data + offset,
		   rec[i].length - offset);
	  rec[i].length = new_offset + (rec[i].length - offset);

#if !defined (NDEBUG)
	  btree_check_valid_record (thread_p, btid, &rec[i], BTREE_LEAF_NODE,
				    NULL);
#endif

	  btree_clear_key_value (&clear_key, &key);
	}
    }

  return error;
}

static int
btree_compress_node (THREAD_ENTRY * thread_p, BTID_INT * btid,
		     PAGE_PTR page_ptr)
{
  int i, key_cnt, diff_column;
  RECDES peek_rec, rec;
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  DB_VALUE key;
  bool clear_key = false;
  int offset, new_offset, key_len, new_key_len;
  int key_type = BTREE_NORMAL_KEY;
  LEAF_REC leaf_pnt;
  int error = NO_ERROR;

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, page_ptr);
#endif

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);

  key_cnt = btree_node_number_of_keys (page_ptr);

  diff_column = btree_node_common_prefix (thread_p, btid, page_ptr);
  if (diff_column == 0)
    {
      return 0;
    }
  else if (diff_column < 0)
    {
      return diff_column;
    }

  /* compress prefix */
  for (i = 2; i < key_cnt; i++)
    {
      (void) spage_get_record (page_ptr, i, &peek_rec, PEEK);

      assert (!btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_FENCE));

      if (btree_leaf_is_flaged (&peek_rec, BTREE_LEAF_RECORD_OVERFLOW_KEY))
	{
	  /* do not compress overflow key */
	  continue;
	}

      (void) spage_get_record (page_ptr, i, &rec, COPY);

      error = btree_read_record_without_decompression (thread_p, btid, &rec,
						       &key, &leaf_pnt,
						       BTREE_LEAF_NODE,
						       &clear_key, &offset,
						       PEEK_KEY_VALUE);
      if (error != NO_ERROR)
	{
	  return error;
	}
      assert (clear_key == false);

      key_len = btree_get_disk_size_of_key (&key);
      pr_midxkey_remove_prefix (&key, diff_column);
      new_key_len = btree_get_disk_size_of_key (&key);

      new_key_len = DB_ALIGN (new_key_len, INT_ALIGNMENT);
      key_len = DB_ALIGN (key_len, INT_ALIGNMENT);

      new_offset = offset + new_key_len - key_len;

      if (new_offset != offset)
	{
	  /* move the remaining part of record */
	  memmove (rec.data + new_offset, rec.data + offset,
		   rec.length - offset);
	  rec.length = new_offset + (rec.length - offset);
	}

#if !defined (NDEBUG)
      btree_check_valid_record (thread_p, btid, &rec, BTREE_LEAF_NODE, &key);
#endif

      spage_update (thread_p, page_ptr, i, &rec);
      btree_clear_key_value (&clear_key, &key);
    }

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, page_ptr);
#endif

  return error;
}

/*
 * btree_split_node () -
 *   return: NO_ERROR
 *           child_vpid is set to page identifier for the child page to be
 *           followed, Q or R, or the page identifier of a newly allocated
 *           page to insert the key, or NULL_PAGEID. The parameter key is
 *           set to the middle key that will be put into the parent page P.
 *   btid(in): The index identifier
 *   P(in): Page pointer for the parent page of page Q
 *   Q(in): Page pointer for the page to be split
 *   R(in): Page pointer for the newly allocated page
 *   next_page(in):
 *   P_vpid(in): Page identifier for page Q
 *   Q_vpid(in): Page identifier for page Q
 *   R_vpid(in): Page identifier for page R
 *   p_slot_id(in): The slot of parent page P which points to page Q
 *   node_type(in): shows whether page Q is a leaf page, or not
 *   key(out): Set to contain the middle key of the split operation
 *   child_vpid(out): Set to the child page identifier
 *
 * Note: Page Q is split into two pages: Q and R. The second half of
 * of the page Q is move to page R. The middle key of of the
 * split operation is moved to parent page P. Depending on the
 * split point, the whole page Q may be moved to page R, or the
 * whole page content may be kept in page Q. If the key can not
 * fit into one of the pages after the split, a new page is
 * allocated for the key and its page identifier is returned.
 * The headers of all pages are updated, accordingly.
 */
static int
btree_split_node (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR P,
		  PAGE_PTR Q, PAGE_PTR R, VPID * P_vpid,
		  VPID * Q_vpid, VPID * R_vpid, INT16 p_slot_id,
		  BTREE_NODE_TYPE node_type, DB_VALUE * key,
		  VPID * child_vpid)
{
  int key_cnt, leftcnt, rightcnt;
  RECDES peek_rec, rec;
  NON_LEAF_REC nleaf_rec;
  BTREE_NODE_HEADER *pheader = NULL, *qheader = NULL;
  BTREE_NODE_HEADER right_header_info, *rheader = NULL;
  int i, j, c;
  int sep_key_len, key_len;
  bool clear_sep_key;
  DB_VALUE *sep_key;
  int ret = NO_ERROR;
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  int key_type;

  bool flag_fence_insert = false;
  OID dummy_oid = { NULL_PAGEID, 0, 0 };
  int leftsize, rightsize;
  VPID right_next_vpid;
  int right_max_key_len;

  /* for recovery purposes */
  char *p_redo_data;
  int p_redo_length;
  char p_redo_data_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

  PAGE_PTR page_after_right = NULL;

  rheader = &right_header_info;

  /***********************************************************
   ***  STEP 0: initializations
   ***********************************************************/
  p_redo_data = NULL;

  rec.data = NULL;

  /* initialize child page identifier */
  VPID_SET_NULL (child_vpid);
  sep_key = NULL;

  /* Assert expected arguments. */
  assert (P != NULL);
  assert (Q != NULL);
  assert (R != NULL);
  assert (!VPID_ISNULL (P_vpid));
  assert (!VPID_ISNULL (Q_vpid));
  assert (!VPID_ISNULL (R_vpid));

#if !defined(NDEBUG)
  if (prm_get_integer_value (PRM_ID_ER_BTREE_DEBUG) & BTREE_DEBUG_DUMP_SIMPLE)
    {
      printf ("btree_split_node: P{%d, %d}, Q{%d, %d}, R{%d, %d}\n",
	      P_vpid->volid, P_vpid->pageid,
	      Q_vpid->volid, Q_vpid->pageid, R_vpid->volid, R_vpid->pageid);
    }
#endif

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, P);
  btree_verify_node (thread_p, btid, Q);
#endif
  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);

  key_cnt = btree_node_number_of_keys (Q);
  if (key_cnt <= 0)
    {
      ASSERT_ERROR_AND_SET (ret);
      goto exit_on_error;
    }

#if !defined(NDEBUG)
  if (prm_get_integer_value (PRM_ID_ER_BTREE_DEBUG) & BTREE_DEBUG_TEST_SPLIT)
    {
      btree_split_test (thread_p, btid, key, Q_vpid, Q, node_type);
    }
#endif

  /********************************************************************
   ***  STEP 1: find split point & sep_key
   ***          make fence key to be inserted
   ***
   ***   find the middle record of the page Q and find the number of
   ***   keys after split in pages Q and R, respectively
   ********************************************************************/
  qheader = btree_get_node_header (Q);
  if (qheader == NULL)
    {
      assert_release (false);
      goto exit_on_error;
    }

  sep_key = btree_find_split_point (thread_p, btid, Q, &leftcnt, key,
				    &clear_sep_key);
  assert (leftcnt < key_cnt && leftcnt >= 1);
  if (sep_key == NULL || DB_IS_NULL (sep_key))
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_split_node: Null middle key after split."
		    " Operation Ignored.\n");
      ASSERT_ERROR_AND_SET (ret);
      goto exit_on_error;
    }

  /* make fence record */
  if (node_type == BTREE_LEAF_NODE)
    {
      PR_TYPE *pr_type = btid->key_type->type;
      if (pr_type->index_lengthval)
	{
	  sep_key_len = (*pr_type->index_lengthval) (sep_key);
	}
      else
	{
	  sep_key_len = pr_type->disksize;
	}

      if (sep_key_len < BTREE_MAX_KEYLEN_INPAGE
	  && sep_key_len <= qheader->max_key_len)
	{
	  ret = btree_write_record (thread_p, btid, NULL, sep_key,
				    BTREE_LEAF_NODE, BTREE_NORMAL_KEY,
				    sep_key_len, false,
				    &btid->topclass_oid, &dummy_oid,
				    NULL, &rec);
	  if (ret != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }

	  btree_leaf_set_flag (&rec, BTREE_LEAF_RECORD_FENCE);

	  flag_fence_insert = true;
	}
      else
	{
	  /* do not insert fence key if sep_key is overflow key */
	  flag_fence_insert = false;
	}
    }

  if (prm_get_bool_value (PRM_ID_USE_BTREE_FENCE_KEY) == false)
    {
      flag_fence_insert = false;
    }

  rightcnt = key_cnt - leftcnt;

  assert (leftcnt > 0);

  /*********************************************************************
   ***  STEP 2: save undo image of Q
   ***		update Q, R header info
   *********************************************************************/
  /* add undo logging for page Q */
  log_append_undo_data2 (thread_p, RVBT_COPYPAGE,
			 &btid->sys_btid->vfid, Q, -1, DB_PAGESIZE, Q);

  /* We may need to update the max_key length if the mid key is larger than
   * the max key length.  This can happen due to disk padding when the
   * prefix key length approaches the fixed key length.
   */
  sep_key_len = btree_get_disk_size_of_key (sep_key);
  sep_key_len = BTREE_GET_KEY_LEN_IN_PAGE (sep_key_len);
  qheader->max_key_len = MAX (sep_key_len, qheader->max_key_len);

  /* set rheader max_key_len as qheader max_key_len */
  right_max_key_len = qheader->max_key_len;
  right_next_vpid = qheader->next_vpid;

  if (node_type == BTREE_LEAF_NODE)
    {
      qheader->next_vpid = *R_vpid;
    }
  else
    {
      VPID_SET_NULL (&qheader->next_vpid);
    }

  assert_release (leftcnt != 0);
  if (leftcnt == 0)		/* defence */
    {
      qheader->max_key_len = 0;
    }

  qheader->split_info.index = 1;

  rheader->node_level = qheader->node_level;
  rheader->max_key_len = right_max_key_len;
  if (key_cnt - leftcnt == 0 && flag_fence_insert == false)
    {
      rheader->max_key_len = 0;
    }

  rheader->next_vpid = right_next_vpid;

  if (node_type == BTREE_LEAF_NODE)
    {
      rheader->prev_vpid = *Q_vpid;
    }
  else
    {
      VPID_SET_NULL (&(rheader->prev_vpid));
    }

  rheader->split_info = qheader->split_info;

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  ret =
    btree_init_node_header (thread_p, &btid->sys_btid->vfid, R, rheader,
			    false);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  /*******************************************************************
   ***   STEP 3: move second half of page Q to page R
   ***           insert fence key to Q
   ***           make redo image for Q
   *******************************************************************/
  /* lower fence key for R */
  rightsize = 0;
  j = 1;
  if (flag_fence_insert == true)
    {
      rightsize = j;
      assert (j > 0);
      if (spage_insert_at (thread_p, R, j++, &rec) != SP_SUCCESS)
	{
	  goto exit_on_error;
	}
    }

  /* move the second half of page Q to page R */
  for (i = 1; i <= rightcnt; i++, j++)
    {
      assert (leftcnt + 1 > 0);
      if (spage_get_record (Q, leftcnt + 1, &peek_rec, PEEK) != S_SUCCESS)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto exit_on_error;
	}

      assert (j > 0);
      if (spage_insert_at (thread_p, R, j, &peek_rec) != SP_SUCCESS)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto exit_on_error;
	}

      rightsize = j;

      assert (leftcnt + 1 > 0);
      if (spage_delete (thread_p, Q, leftcnt + 1) != leftcnt + 1)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto exit_on_error;
	}
    }

  leftsize = leftcnt;
  /* upper fence key for Q */
  if (flag_fence_insert == true)
    {
      assert (leftcnt + 1 > 0);
      if (spage_insert_at (thread_p, Q, leftcnt + 1, &rec) != SP_SUCCESS)
	{
	  assert_release (false);
	  ret = ER_FAILED;
	  goto exit_on_error;
	}
      leftsize++;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  ret = btree_compress_node (thread_p, btid, Q);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  /* add redo logging for page Q */
  log_append_redo_data2 (thread_p, RVBT_COPYPAGE,
			 &btid->sys_btid->vfid, Q, -1, DB_PAGESIZE, Q);

  /***************************************************************************
   ***   STEP 4: add redo log for R
   ***    Log the second half of page Q for redo purposes on Page R,
   ***    the records on the second half of page Q will be inserted to page R
   ***************************************************************************/

  ret = btree_compress_node (thread_p, btid, R);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  log_append_redo_data2 (thread_p, RVBT_COPYPAGE, &btid->sys_btid->vfid,
			 R, -1, DB_PAGESIZE, R);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /****************************************************************************
   ***   STEP 5: insert sep_key to P
   ***           add undo/redo log for page P
   ***
   ***    update the parent page P to keep the middle key and to point to
   ***    pages Q and R.  Remember that this mid key will be on a non leaf page
   ***    regardless of whether we are splitting a leaf or non leaf page.
   ****************************************************************************/
  nleaf_rec.pnt = *R_vpid;
  key_len = btree_get_disk_size_of_key (sep_key);
  if (key_len < BTREE_MAX_KEYLEN_INPAGE)
    {
      key_type = BTREE_NORMAL_KEY;
      nleaf_rec.key_len = key_len;
    }
  else
    {
      key_type = BTREE_OVERFLOW_KEY;
      nleaf_rec.key_len = -1;
    }

  ret = btree_write_record (thread_p, btid, &nleaf_rec, sep_key,
			    BTREE_NON_LEAF_NODE, key_type, key_len, false,
			    NULL, NULL, NULL, &rec);
  if (ret != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  p_slot_id++;

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* add undo/redo logging for page P */
  assert (p_slot_id > 0);
  if (spage_insert_at (thread_p, P, p_slot_id, &rec) != SP_SUCCESS)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }

  p_redo_data = PTR_ALIGN (p_redo_data_buf, BTREE_MAX_ALIGN);

  btree_rv_write_log_record (p_redo_data, &p_redo_length, &rec,
			     BTREE_NON_LEAF_NODE);
  log_append_undoredo_data2 (thread_p, RVBT_NDRECORD_INS,
			     &btid->sys_btid->vfid, P, p_slot_id,
			     sizeof (p_slot_id), p_redo_length, &p_slot_id,
			     p_redo_data);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  key_cnt = btree_node_number_of_keys (P);
  assert_release (key_cnt > 0);

  pheader = btree_get_node_header (P);
  if (pheader == NULL)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }

  assert_release (pheader->split_info.pivot >= 0);

  btree_node_header_undo_log (thread_p, &btid->sys_btid->vfid, P);

  btree_split_next_pivot (&pheader->split_info, (float) p_slot_id / key_cnt,
			  key_cnt);

  /* We may need to update the max_key length if the mid key is larger than
   * the max key length. This can happen due to disk padding when the
   * prefix key length approaches the fixed key length.
   */
  sep_key_len = btree_get_disk_size_of_key (sep_key);
  sep_key_len = BTREE_GET_KEY_LEN_IN_PAGE (sep_key_len);
  pheader->max_key_len = MAX (sep_key_len, pheader->max_key_len);

  btree_node_header_redo_log (thread_p, &btid->sys_btid->vfid, P);

  /* find the child page to be followed */
  c = btree_compare_key (key, sep_key, btid->key_type, 1, 1, NULL);
  assert (c == DB_LT || c == DB_EQ || c == DB_GT);

  if (c == DB_UNK)
    {
      assert_release (false);
      ret = ER_FAILED;
      goto exit_on_error;
    }
  else if (c < 0)
    {
      /* set child page pointer */
      *child_vpid = *Q_vpid;
    }
  else
    {
      /* set child page pointer */
      *child_vpid = *R_vpid;
    }

  /* TODO : update child_vpid max_key_len */
  if (sep_key)
    {
      btree_clear_key_value (&clear_sep_key, sep_key);
      db_private_free_and_init (thread_p, sep_key);
    }

  pgbuf_set_dirty (thread_p, P, DONT_FREE);
  pgbuf_set_dirty (thread_p, Q, DONT_FREE);
  pgbuf_set_dirty (thread_p, R, DONT_FREE);

  if (rheader->node_level == 1)
    {
      /* Since leaf level can be processed in reversed order, we need to
       * update the prev link of next page after the one that was split.
       */
      page_after_right = btree_get_next_page (thread_p, R);
      if (page_after_right != NULL)
	{
	  ret =
	    btree_set_vpid_previous_vpid (thread_p, btid, page_after_right,
					  R_vpid);
	  /* We don't expect any errors here. */
	  assert (ret == NO_ERROR);
	  pgbuf_unfix_and_init (thread_p, page_after_right);
	}
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  mnt_bt_splits (thread_p);

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, P);
  btree_verify_node (thread_p, btid, Q);
  btree_verify_node (thread_p, btid, R);
#endif

  return ret;

exit_on_error:

  if (sep_key)
    {
      btree_clear_key_value (&clear_sep_key, sep_key);
      db_private_free_and_init (thread_p, sep_key);
    }

  assert (ret != NO_ERROR);
  return ret;
}

#if !defined(NDEBUG)
/*
 * btree_set_split_point () -
 *   return: the key or key separator (prefix) to be moved to the
 *           parent page, or NULL_KEY. The length of the returned
 *           key, or prefix, is set in mid_keylen. The parameter
 *           mid_slot is set to the record number of the split point record.
 *   btid(in):
 *   page_ptr(in): Pointer to the page
 *   mid_slot(in): Set to contain the record number for the split point slot
 *   key(in): Key to be inserted to the index
 *   clear_midkey(in):
 *
 */
static DB_VALUE *
btree_set_split_point (THREAD_ENTRY * thread_p,
		       BTID_INT * btid, PAGE_PTR page_ptr,
		       INT16 mid_slot, DB_VALUE * key, bool * clear_midkey)
{
  RECDES rec;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  INT16 slot_id;
  int key_cnt, offset;
  bool m_clear_key, n_clear_key;
  DB_VALUE *mid_key = NULL, *next_key = NULL, *prefix_key = NULL, *tmp_key;
  NON_LEAF_REC nleaf_pnt;
  LEAF_REC leaf_pnt;
  BTREE_SEARCH_KEY_HELPER search_key;

  key_cnt = btree_node_number_of_keys (page_ptr);
  if (key_cnt <= 0)
    {
      assert (false);
    }

  /* get the page header */
  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      assert (false);
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  /* find the slot position of the key if it is to be located in the page */
  if (node_type == BTREE_LEAF_NODE)
    {
      if (btree_search_leaf_page (thread_p, btid, page_ptr, key, &search_key)
	  != NO_ERROR)
	{
	  assert (false);
	}
      slot_id = search_key.slotid;
      if (slot_id == NULL_SLOTID)	/* leaf search failed */
	{
	  assert (false);
	}
    }
  else
    {
      slot_id = NULL_SLOTID;
    }

  mid_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (mid_key == NULL)
    {
      assert (false);
    }

  db_make_null (mid_key);

  /* the split key is one of the keys on the page */
  assert (mid_slot > 0);
  if (spage_get_record (page_ptr, mid_slot, &rec, PEEK) != S_SUCCESS)
    {
      assert (false);
    }

  /* we copy the key here because rec lives on the stack and mid_key
   * is returned from this routine.
   */
  if (node_type == BTREE_LEAF_NODE)
    {
      if (btree_read_record (thread_p, btid, page_ptr, &rec, mid_key,
			     (void *) &leaf_pnt, node_type, &m_clear_key,
			     &offset, COPY_KEY_VALUE, NULL) != NO_ERROR)
	{
	  assert (false);
	}
    }
  else
    {
      if (btree_read_record (thread_p, btid, page_ptr, &rec, mid_key,
			     (void *) &nleaf_pnt, node_type, &m_clear_key,
			     &offset, COPY_KEY_VALUE, NULL) != NO_ERROR)
	{
	  assert (false);
	}
    }

  /* The determination of the prefix key is dependent on the next key */
  next_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (next_key == NULL)
    {
      assert (false);
    }

  db_make_null (next_key);

  if (mid_slot == key_cnt && slot_id == (key_cnt + 1))
    {
      /* the next key is the new key, we don't have to read it */
      n_clear_key = true;
      if (pr_clone_value (key, next_key) != NO_ERROR)
	{
	  assert (false);
	}
    }
  else
    {
      /* The next key is one of the keys on the page */
      assert (mid_slot + 1 > 0);
      if (spage_get_record (page_ptr, mid_slot + 1, &rec, PEEK) != S_SUCCESS)
	{
	  assert (false);
	}

      /* we copy the key here because rec lives on the stack and mid_key
       * is returned from this routine.
       */
      if (node_type == BTREE_LEAF_NODE)
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &rec, next_key,
				 (void *) &leaf_pnt, node_type, &n_clear_key,
				 &offset, COPY_KEY_VALUE, NULL) != NO_ERROR)
	    {
	      assert (false);
	    }
	}
      else
	{
	  if (btree_read_record (thread_p, btid, page_ptr, &rec, next_key,
				 (void *) &nleaf_pnt, node_type, &n_clear_key,
				 &offset, COPY_KEY_VALUE, NULL) != NO_ERROR)
	    {
	      assert (false);
	    }
	}
    }

  /* now that we have the mid key and the next key, we can determine the
   * prefix key.
   */

  prefix_key = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
  if (prefix_key == NULL)
    {
      assert (false);
    }

  /* Check if we can make use of prefix keys.  We can't use them in the
   * upper levels of the trees because the algorithm will fall apart.  We
   * can only use them when splitting a leaf page.
   */
  if (node_type == BTREE_LEAF_NODE)
    {
      if (btree_get_prefix_separator (mid_key, next_key, prefix_key,
				      btid->key_type) != NO_ERROR)
	{
	  assert (false);
	}
    }
  else
    {
      /* return the next_key */
      pr_clone_value (next_key, prefix_key);
    }

  *clear_midkey = true;		/* we must always clear prefix keys */

  /* replace the mid_key with the prefix_key */
  tmp_key = mid_key;
  mid_key = prefix_key;
  prefix_key = tmp_key;		/* this makes sure we clear/free the old mid key */

  if (next_key)
    {
      btree_clear_key_value (&n_clear_key, next_key);
      db_private_free_and_init (thread_p, next_key);
    }
  if (prefix_key)
    {
      pr_clear_value (prefix_key);
      db_private_free_and_init (thread_p, prefix_key);
    }

  return mid_key;
}

/*
 * btree_split_test () -
 *
 *   btid(in):
 *   key(in):
 *   S_vpid(in):
 *   S_page(in):
 *   node_type(in):
 */
static void
btree_split_test (THREAD_ENTRY * thread_p, BTID_INT * btid, DB_VALUE * key,
		  VPID * S_vpid, PAGE_PTR S_page, BTREE_NODE_TYPE node_type)
{
  RECDES rec, peek_rec;
  int i, j, key_cnt, lcnt, rcnt, sep_key_len, ret;
  PAGE_PTR L_page, R_page;
  VPID L_vpid, R_vpid;
  BTREE_NODE_HEADER header_info, *header = NULL;
  DB_VALUE *sep_key;
  bool fence_insert = false;
  bool clear_sep_key = true;
  OID dummy_oid = { NULL_PAGEID, 0, 0 };
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];

  header = &header_info;

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);

  key_cnt = btree_node_number_of_keys (S_page);
  assert (key_cnt > 0);

  L_page = btree_get_new_page (thread_p, btid, &L_vpid, S_vpid);
  R_page = btree_get_new_page (thread_p, btid, &R_vpid, S_vpid);

  /* dummy header */
  memset (header, 0, sizeof (BTREE_NODE_HEADER));
  btree_init_node_header (thread_p, &btid->sys_btid->vfid,
			  L_page, header, false);
  btree_init_node_header (thread_p, &btid->sys_btid->vfid,
			  R_page, header, false);

  for (lcnt = 1; lcnt < key_cnt; i++)
    {
      fence_insert = false;
      sep_key = btree_set_split_point (thread_p, btid, S_page, lcnt, key,
				       &clear_sep_key);
      assert (sep_key != NULL);

      if (node_type == BTREE_LEAF_NODE)
	{
	  PR_TYPE *pr_type;

	  pr_type = btid->key_type->type;
	  sep_key_len = pr_type->disksize;

	  if (pr_type->index_lengthval)
	    {
	      sep_key_len = (*pr_type->index_lengthval) (sep_key);
	    }

	  if (sep_key_len < BTREE_MAX_KEYLEN_INPAGE)
	    {
	      ret = btree_write_record (thread_p, btid, NULL, sep_key,
					BTREE_LEAF_NODE, BTREE_NORMAL_KEY,
					sep_key_len, false,
					&btid->topclass_oid, &dummy_oid,
					NULL, &rec);

	      btree_leaf_set_flag (&rec, BTREE_LEAF_RECORD_FENCE);
	      fence_insert = true;
	    }
	}

      rcnt = key_cnt - lcnt;

      /* Right page test */
      j = 1;
      /* lower fence key for Right */
      if (fence_insert == true)
	{
	  assert (j > 0);
	  ret = spage_insert_at (thread_p, R_page, j++, &rec);
	  if (ret != SP_SUCCESS)
	    {
	      assert (false);
	    }
	}

      /* move the second half of page P to page R */
      for (i = 1; i <= rcnt; i++, j++)
	{
	  assert (lcnt + i > 0);
	  ret = spage_get_record (S_page, lcnt + i, &peek_rec, PEEK);
	  if (ret != S_SUCCESS)
	    {
	      assert (false);
	    }

	  assert (j > 0);
	  ret = spage_insert_at (thread_p, R_page, j, &peek_rec);
	  if (ret != SP_SUCCESS)
	    {
	      assert (false);
	    }
	}

      /* Left page test */
      for (i = 1; i <= lcnt; i++)
	{
	  ret = spage_get_record (S_page, i, &peek_rec, PEEK);
	  if (ret != S_SUCCESS)
	    {
	      assert (false);
	    }

	  ret = spage_insert_at (thread_p, L_page, i, &peek_rec);
	  if (ret != SP_SUCCESS)
	    {
	      assert (false);
	    }
	}

      /* upper fence key for Left */
      if (fence_insert == true)
	{
	  assert (i > 0);
	  ret = spage_insert_at (thread_p, L_page, i, &rec);
	  if (ret != SP_SUCCESS)
	    {
	      assert (false);
	    }
	}

      /* clean up */
      if (fence_insert == true)
	{
	  lcnt++, rcnt++;
	}

      assert (btree_node_number_of_keys (L_page) == lcnt);
      assert (btree_node_number_of_keys (R_page) == rcnt);

      for (i = 1; i <= lcnt; i++)
	{
	  ret = spage_delete (thread_p, L_page, 1);
	  if (ret != 1)
	    {
	      assert (false);
	    }
	}

      for (i = 1; i <= rcnt; i++)
	{
	  ret = spage_delete (thread_p, R_page, 1);
	  if (ret != 1)
	    {
	      assert (false);
	    }
	}

      assert (btree_node_number_of_keys (L_page) == 0);
      assert (btree_node_number_of_keys (R_page) == 0);

      btree_clear_key_value (&clear_sep_key, sep_key);
      db_private_free_and_init (thread_p, sep_key);
    }

  pgbuf_unfix_and_init (thread_p, L_page);
  pgbuf_unfix_and_init (thread_p, R_page);

  if (file_dealloc_page (thread_p, &btid->sys_btid->vfid, &L_vpid) !=
      NO_ERROR)
    {
      assert (false);
    }

  if (file_dealloc_page (thread_p, &btid->sys_btid->vfid, &R_vpid) !=
      NO_ERROR)
    {
      assert (false);
    }
}
#endif

/*
 * btree_split_root () -
 *   return: NO_ERROR
 *           child_vpid parameter is set to the child page to be followed
 *           after the split operation, or the page identifier of a newly
 *           allocated page for future key insertion, or NULL_PAGEID.
 *           The parameter key is set to the middle key of the split operation.
 *   btid(in): B+tree index identifier
 *   P(in): Page pointer for the root to be split
 *   Q(in): Page pointer for the newly allocated page
 *   R(in): Page pointer for the newly allocated page
 *   P_vpid(in): Page identifier for root page P
 *   Q_vpid(in): Page identifier for page Q
 *   R_vpid(in): Page identifier for page R
 *   node_type(in): shows whether root is currenly a leaf page,
 *                  or not
 *   key(out): Set to contain the middle key of the split operation
 *   child_vpid(out): Set to the child page identifier
 *
 * Note: The root page P is split into two pages: Q and R. In order
 * not to change the actual root page, the first half of the page
 * is moved to page Q and the second half is moved to page R.
 * Depending on the split point found, the whole root page may be
 * moved to Q, or R, leaving the other one empty for future  key
 * insertion. If the key cannot fit into either Q or R after the
 * split, a new page is allocated and its page identifier is
 * returned. Two new records are formed within root page to point
 * to pages Q and R. The headers of all pages are updated.
 */
static int
btree_split_root (THREAD_ENTRY * thread_p, BTID_INT * btid, PAGE_PTR P,
		  PAGE_PTR Q, PAGE_PTR R, VPID * P_vpid,
		  VPID * Q_vpid, VPID * R_vpid, BTREE_NODE_TYPE node_type,
		  DB_VALUE * key, VPID * child_vpid)
{
  int key_cnt, leftcnt, rightcnt;
  RECDES rec, peek_rec;
  NON_LEAF_REC nleaf_rec;
  BTREE_ROOT_HEADER *pheader = NULL;
  BTREE_NODE_HEADER q_header_info, *qheader = NULL;
  BTREE_NODE_HEADER r_header_info, *rheader = NULL;
  int i, j, c;
  int sep_key_len, key_len;
  bool clear_sep_key;
  DB_VALUE *sep_key;
  DB_VALUE *neg_inf_key = NULL;
  char *recset_data;		/* for recovery purposes */
  RECSET_HEADER recset_header;	/* for recovery purposes */
  int recset_length;		/* for recovery purposes */
  int sp_success;
  PGLENGTH log_addr_offset;
  int ret = NO_ERROR;
  char rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  char recset_data_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  int key_type;
  BTREE_NODE_SPLIT_INFO split_info;
  int node_level;
  bool flag_fence_insert = false;
  OID dummy_oid = { NULL_PAGEID, 0, 0 };
  int leftsize, rightsize;

  qheader = &q_header_info;
  rheader = &r_header_info;

  /***********************************************************
   ***  STEP 0: initializations
   ***********************************************************/
  recset_data = NULL;
  rec.data = NULL;

  /* initialize child page identifier */
  VPID_SET_NULL (child_vpid);
  sep_key = NULL;

#if !defined(NDEBUG)
  if ((!P || !Q || !R) || VPID_ISNULL (P_vpid)
      || VPID_ISNULL (Q_vpid) || VPID_ISNULL (R_vpid))
    {
      goto exit_on_error;
    }
#endif

#if !defined(NDEBUG)
  if (prm_get_integer_value (PRM_ID_ER_BTREE_DEBUG) & BTREE_DEBUG_DUMP_SIMPLE)
    {
      printf ("btree_split_root: P{%d, %d}, Q{%d, %d}, R{%d, %d}\n",
	      P_vpid->volid, P_vpid->pageid,
	      Q_vpid->volid, Q_vpid->pageid, R_vpid->volid, R_vpid->pageid);
    }
#endif

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, P);
#endif

  /* initializations */
  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (rec_buf, BTREE_MAX_ALIGN);

  /* log the whole root page P for undo purposes. */
  log_append_undo_data2 (thread_p, RVBT_COPYPAGE, &btid->sys_btid->vfid, P,
			 -1, DB_PAGESIZE, P);

  /* get the number of keys in the root page P */
  key_cnt = btree_node_number_of_keys (P);
  if (key_cnt <= 0)
    {
      goto exit_on_error;
    }

#if !defined(NDEBUG)
  node_level = btree_get_node_level (P);
  assert (node_level >= 1);
#endif

  pheader = btree_get_root_header (P);
  if (pheader == NULL)
    {
      goto exit_on_error;
    }

  split_info = pheader->node.split_info;
  split_info.index = 1;

#if !defined(NDEBUG)
  if (prm_get_integer_value (PRM_ID_ER_BTREE_DEBUG) & BTREE_DEBUG_TEST_SPLIT)
    {
      btree_split_test (thread_p, btid, key, P_vpid, P, node_type);
    }
#endif

  /********************************************************************
   ***  STEP 1: find split point & sep_key
   ***          make fence key to be inserted
   ***
   ***   find the middle record of the page Q and find the number of
   ***   keys after split in pages Q and R, respectively
   ********************************************************************/

  sep_key = btree_find_split_point (thread_p, btid, P, &leftcnt, key,
				    &clear_sep_key);
  assert (leftcnt < key_cnt && leftcnt >= 1);
  if (sep_key == NULL || DB_IS_NULL (sep_key))
    {
      er_log_debug (ARG_FILE_LINE,
		    "btree_split_root: Null middle key after split."
		    " Operation Ignored.\n");
      goto exit_on_error;
    }

  /* make fence record */
  if (node_type == BTREE_LEAF_NODE)
    {
      PR_TYPE *pr_type;

      pr_type = btid->key_type->type;

      if (pr_type->index_lengthval)
	{
	  sep_key_len = (*pr_type->index_lengthval) (sep_key);
	}
      else
	{
	  sep_key_len = pr_type->disksize;
	}

      if (sep_key_len < BTREE_MAX_KEYLEN_INPAGE
	  && sep_key_len <= pheader->node.max_key_len)
	{
	  ret = btree_write_record (thread_p, btid, NULL, sep_key,
				    BTREE_LEAF_NODE, BTREE_NORMAL_KEY,
				    sep_key_len, false,
				    &btid->topclass_oid, &dummy_oid,
				    NULL, &rec);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  btree_leaf_set_flag (&rec, BTREE_LEAF_RECORD_FENCE);
	  flag_fence_insert = true;
	}
      else
	{
	  /* do not insert fence key if sep_key is overflow key */
	  flag_fence_insert = false;
	}
    }

  if (prm_get_bool_value (PRM_ID_USE_BTREE_FENCE_KEY) == false)
    {
      flag_fence_insert = false;
    }

  /* neg-inf key is dummy key which is not used in comparison
   * so set it as sep_key */
  neg_inf_key = sep_key;

  rightcnt = key_cnt - leftcnt;

  /*********************************************************************
   ***  STEP 2: update P, Q, R header info
   *********************************************************************/
  /* update page P header */
  pheader->node.node_level++;

  /* We may need to update the max_key length if the sep key is larger than
   * the max key length. This can happen due to disk padding when the
   * prefix key length approaches the fixed key length.
   */
  sep_key_len = btree_get_disk_size_of_key (sep_key);
  sep_key_len = BTREE_GET_KEY_LEN_IN_PAGE (sep_key_len);
  pheader->node.max_key_len = MAX (sep_key_len, pheader->node.max_key_len);
  btree_write_default_split_info (&(pheader->node.split_info));

  btree_node_header_redo_log (thread_p, &btid->sys_btid->vfid, P);

  /* update page Q header */
  qheader->node_level = pheader->node.node_level - 1;
  qheader->max_key_len = pheader->node.max_key_len;
  if (leftcnt == 0 && flag_fence_insert == false)
    {
      qheader->max_key_len = 0;
    }

  VPID_SET_NULL (&(qheader->prev_vpid));	/* non leaf or first leaf node */

  if (node_type == BTREE_LEAF_NODE)
    {
      qheader->next_vpid = *R_vpid;
    }
  else
    {
      VPID_SET_NULL (&(qheader->next_vpid));
    }

  qheader->split_info = split_info;

  if (btree_init_node_header
      (thread_p, &btid->sys_btid->vfid, Q, qheader, true) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* update page R header */
  rheader->node_level = pheader->node.node_level - 1;
  rheader->max_key_len = pheader->node.max_key_len;
  if (key_cnt - leftcnt == 0 && flag_fence_insert == false)
    {
      rheader->max_key_len = 0;
    }

  VPID_SET_NULL (&(rheader->next_vpid));	/* non leaf or last leaf node */

  if (node_type == BTREE_LEAF_NODE)
    {
      rheader->prev_vpid = *Q_vpid;
    }
  else
    {
      VPID_SET_NULL (&(rheader->prev_vpid));
    }

  rheader->split_info = split_info;

  if (btree_init_node_header
      (thread_p, &btid->sys_btid->vfid, R, rheader, true) != NO_ERROR)
    {
      goto exit_on_error;
    }


  /*******************************************************************
   ***   STEP 3: move second half of page P to page R
   ***           insert fence key to R
   ***           add undo / redo log for R
   *******************************************************************/
  /* move the second half of root page P to page R
   */
  assert (btree_node_number_of_keys (P) == leftcnt + rightcnt);

  j = 1;
  /* lower fence key for page R */
  if (flag_fence_insert == true)
    {
      rightsize = j;
      assert (j > 0);
      if (spage_insert_at (thread_p, R, j++, &rec) != SP_SUCCESS)
	{
	  goto exit_on_error;
	}
    }

  for (i = 1; i <= rightcnt; i++, j++)
    {
      assert (leftcnt + 1 > 0);
      if (spage_get_record (P, leftcnt + 1, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      assert (j > 0);
      sp_success = spage_insert_at (thread_p, R, j, &peek_rec);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}
      rightsize = j;

      assert (leftcnt + 1 > 0);
      if (spage_delete (thread_p, P, leftcnt + 1) != leftcnt + 1)
	{
	  goto exit_on_error;
	}
    }

  /* for recovery purposes */
  recset_data = PTR_ALIGN (recset_data_buf, BTREE_MAX_ALIGN);

  /* Log page R records for redo purposes */
  ret = btree_rv_util_save_page_records (R, 1, j - 1, 1, recset_data,
					 &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  log_append_redo_data2 (thread_p, RVBT_INS_PGRECORDS, &btid->sys_btid->vfid,
			 R, -1, recset_length, recset_data);


  /*******************************************************************
   ***   STEP 4: move first half of page P to page Q
   ***           insert fence key to Q
   ***           add undo / redo log for Q
   *******************************************************************/
  /* move the first half of root page P to page Q */

  for (i = 1; i <= leftcnt; i++)
    {
      if (spage_get_record (P, 1, &peek_rec, PEEK) != S_SUCCESS)
	{
	  goto exit_on_error;
	}

      sp_success = spage_insert_at (thread_p, Q, i, &peek_rec);
      if (sp_success != SP_SUCCESS)
	{
	  goto exit_on_error;
	}
      leftsize = i;

      if (spage_delete (thread_p, P, 1) != 1)
	{
	  goto exit_on_error;
	}
    }

  /* upper fence key for Q */
  if (flag_fence_insert == true)
    {
      assert (i > 0);
      if (spage_insert_at (thread_p, Q, i, &rec) != SP_SUCCESS)
	{
	  goto exit_on_error;
	}
      leftsize = i;
    }
  else
    {
      i--;
    }

  /* Log page Q records for redo purposes */
  ret = btree_rv_util_save_page_records (Q, 1, i, 1, recset_data,
					 &recset_length);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }
  log_append_redo_data2 (thread_p, RVBT_INS_PGRECORDS, &btid->sys_btid->vfid,
			 Q, -1, recset_length, recset_data);

  /****************************************************************************
   ***   STEP 5: insert sep_key to P
   ***           add redo log for page P
   ****************************************************************************/

  /* Log deletion of all page P records (except the header!!)
   * for redo purposes
   */
  recset_header.rec_cnt = key_cnt;
  recset_header.first_slotid = 1;
  log_append_redo_data2 (thread_p, RVBT_DEL_PGRECORDS, &btid->sys_btid->vfid,
			 P, -1, sizeof (RECSET_HEADER), &recset_header);

  /* update the root page P to keep the middle key and to point to
   * page Q and R.  Remember that this mid key will be on a non leaf page
   * regardless of whether we are splitting a leaf or non leaf page.
   */
  nleaf_rec.pnt = *Q_vpid;
  key_len = btree_get_disk_size_of_key (neg_inf_key);
  if (key_len < BTREE_MAX_KEYLEN_INPAGE)
    {
      key_type = BTREE_NORMAL_KEY;
      nleaf_rec.key_len = key_len;
    }
  else
    {
      key_type = BTREE_OVERFLOW_KEY;
      nleaf_rec.key_len = -1;
    }

  ret = btree_write_record (thread_p, btid, &nleaf_rec, neg_inf_key,
			    BTREE_NON_LEAF_NODE, key_type, key_len, false,
			    NULL, NULL, NULL, &rec);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (spage_insert_at (thread_p, P, 1, &rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the inserted record for undo/redo purposes, */
  btree_rv_write_log_record (recset_data, &recset_length, &rec,
			     BTREE_NON_LEAF_NODE);

  log_addr_offset = 1;
  log_append_redo_data2 (thread_p, RVBT_NDRECORD_INS,
			 &btid->sys_btid->vfid, P, log_addr_offset,
			 recset_length, recset_data);

  nleaf_rec.pnt = *R_vpid;
  key_len = btree_get_disk_size_of_key (sep_key);
  if (key_len < BTREE_MAX_KEYLEN_INPAGE)
    {
      key_type = BTREE_NORMAL_KEY;
      nleaf_rec.key_len = key_len;
    }
  else
    {
      key_type = BTREE_OVERFLOW_KEY;
      nleaf_rec.key_len = -1;
    }

  ret = btree_write_record (thread_p, btid, &nleaf_rec, sep_key,
			    BTREE_NON_LEAF_NODE, key_type, key_len, false,
			    NULL, NULL, NULL, &rec);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (spage_insert_at (thread_p, P, 2, &rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  /* log the inserted record for undo/redo purposes, */
  btree_rv_write_log_record (recset_data, &recset_length, &rec,
			     BTREE_NON_LEAF_NODE);

  log_addr_offset = 2;
  log_append_redo_data2 (thread_p, RVBT_NDRECORD_INS,
			 &btid->sys_btid->vfid, P, log_addr_offset,
			 recset_length, recset_data);

  /* find the child page to be followed */

  c = btree_compare_key (key, sep_key, btid->key_type, 1, 1, NULL);
  assert (c == DB_LT || c == DB_EQ || c == DB_GT);

  if (c == DB_UNK)
    {
      goto exit_on_error;
    }
  else if (c < 0)
    {
      /* set child page identifier */
      *child_vpid = *Q_vpid;

    }
  else
    {
      /* set child page identifier */
      *child_vpid = *R_vpid;
    }

  if (sep_key)
    {
      btree_clear_key_value (&clear_sep_key, sep_key);
      db_private_free_and_init (thread_p, sep_key);
    }

  pgbuf_set_dirty (thread_p, P, DONT_FREE);
  pgbuf_set_dirty (thread_p, Q, DONT_FREE);
  pgbuf_set_dirty (thread_p, R, DONT_FREE);

  mnt_bt_splits (thread_p);

#if !defined(NDEBUG)
  btree_verify_node (thread_p, btid, P);
  btree_verify_node (thread_p, btid, Q);
  btree_verify_node (thread_p, btid, R);
#endif

  return ret;

exit_on_error:

  if (sep_key)
    {
      btree_clear_key_value (&clear_sep_key, sep_key);
      db_private_free_and_init (thread_p, sep_key);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_update () -
 *   return: NO_ERROR
 *   btid(in): B+tree index identifier
 *   old_key(in): Old key value
 *   new_key(in): New key value
 *   locked_keys(in): keys already locked by the current transaction
 *		      when search
 *   cls_oid(in):
 *   oid(in): Object identifier to be updated
 *   new_oid(in): Object identifier after it was updated
 *   op_type(in):
 *   unique_stat_info(in):
 *   unique(in):
 *   p_mvcc_rec_header(in/out): array of MVCC_REC_HEADER of size 2 or NULL
 *
 * Note: Deletes the <old_key, oid> key-value pair from the B+tree
 * index and inserts the <new_key, oid> key-value pair to the
 * B+tree index which results in the update of the specified
 * index entry for the given object identifier.
 */
int
btree_update (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * old_key,
	      DB_VALUE * new_key, OID * cls_oid, OID * oid, OID * new_oid,
	      int op_type, BTREE_UNIQUE_STATS * unique_stat_info, int *unique,
	      MVCC_REC_HEADER * p_mvcc_rec_header)
{
  int ret = NO_ERROR;

  assert (old_key != NULL);
  assert (new_key != NULL);
  assert (unique != NULL);

#if !defined (SERVER_MODE)
  assert_release (p_mvcc_rec_header == NULL);
#endif /* SERVER_MODE */

  if (p_mvcc_rec_header != NULL && !OID_EQ (oid, new_oid))
    {
      /* in MVCC, logical deletion means DEL_ID insertion */
      /* Note that it is possible that update "in-place" is done instead of
       * standard MVCC update, in which case the "logical" deletion is no
       * longer required.
       */
      ret =
	btree_mvcc_delete (thread_p, btid, old_key, cls_oid, oid, op_type,
			   unique_stat_info, unique, &p_mvcc_rec_header[0]);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
    }
  else
    {
      /* In-place update. Remove object physically. */
      ret =
	btree_physical_delete (thread_p, btid, old_key, oid, cls_oid, unique,
			       op_type, unique_stat_info);
      if (ret != NO_ERROR)
	{
	  /* if the btree we are updating is a btree for unique attributes
	   * it is possible that the btree update has already been performed
	   * via the template unique checking.
	   * In this case, we will ignore the error from btree_delete
	   */
	  if (*unique && er_errid () == ER_BTREE_UNKNOWN_KEY)
	    {
	      /* Is this still true? */
	      goto end;
	    }
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
    }

  {
    MVCC_REC_HEADER *p_local_rec_header = NULL;
    if (p_mvcc_rec_header != NULL)
      {
	p_local_rec_header = &p_mvcc_rec_header[1];
      }

    ret =
      btree_insert (thread_p, btid, new_key, cls_oid, new_oid, op_type,
		    unique_stat_info, unique, p_local_rec_header);
    if (ret != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto exit_on_error;
      }
  }

#if 0
  {
    BTREE_CHECKSCAN bt_checkscan;
    DISK_ISVALID isvalid = DISK_VALID;

    /* start a check-scan on index */
    ret = btree_keyoid_checkscan_start (thread_p, btid, &bt_checkscan);
    if (ret != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto exit_on_error;
      }

    if (!DB_IS_NULL (old_key) && !btree_multicol_key_is_null (old_key))
      {
	isvalid =
	  btree_keyoid_checkscan_check (thread_p, &bt_checkscan, cls_oid,
					old_key, oid);

	if (er_errid () == ER_INTERRUPTED)
	  {
	    /* in case of user interrupt */
	    ;			/* do not check isvalid */
	  }
	else
	  {
	    assert (isvalid == DISK_INVALID);	/* not found */
	  }
      }

    if (!DB_IS_NULL (new_key) && !btree_multicol_key_is_null (new_key))
      {
	isvalid =
	  btree_keyoid_checkscan_check (thread_p, &bt_checkscan, cls_oid,
					new_key, new_oid);

	if (er_errid () == ER_INTERRUPTED)
	  {
	    /* in case of user interrupt */
	    ;			/* do not check isvalid */
	  }
	else
	  {
	    assert (isvalid == DISK_VALID);	/* found */
	  }
      }

    /* close the index check-scan */
    btree_keyoid_checkscan_end (thread_p, &bt_checkscan);
  }
#endif

end:

  mnt_bt_updates (thread_p);

  return ret;

exit_on_error:
  mnt_bt_updates (thread_p);

  assert_release (ret != NO_ERROR);
  return ret;
}

/*
 * btree_reflect_global_unique_statistics () - reflects the global statistical
 *					       information into btree header
 *   return: NO_ERROR
 *   unique_stat_info(in):
 *   only_active_tran(in): if true then reflect statistics only if transaction
 *			   is active
 *
 * Note: We don't need to log the changes at this point because the changes were
 *	 already logged at commit stage.
 */
int
btree_reflect_global_unique_statistics (THREAD_ENTRY * thread_p,
					GLOBAL_UNIQUE_STATS *
					unique_stat_info,
					bool only_active_tran)
{
  VPID root_vpid;
  PAGE_PTR root = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  char *redo_data = NULL;
  int ret = NO_ERROR;
  LOG_LSA *page_lsa = NULL;

  /* check if unique_stat_info is NULL */
  if (unique_stat_info == NULL)
    {
      return ER_FAILED;
    }

  /* fix the root page */
  root_vpid.pageid = unique_stat_info->btid.root_pageid;
  root_vpid.volid = unique_stat_info->btid.vfid.volid;
  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

  /* read the root information */
  root_header = btree_get_root_header (root);
  if (root_header == NULL)
    {
      goto exit_on_error;
    }

  if (root_header->num_nulls != -1)
    {
      assert_release (BTREE_IS_UNIQUE (root_header->unique_pk));

      if (!only_active_tran || logtb_is_current_active (thread_p))
	{
	  /* update header information */
	  root_header->num_nulls = unique_stat_info->unique_stats.num_nulls;
	  root_header->num_oids = unique_stat_info->unique_stats.num_oids;
	  root_header->num_keys = unique_stat_info->unique_stats.num_keys;

	  page_lsa = pgbuf_get_lsa (root);
	  /* update the page's LSA to the last global unique statistics change
	   * that was made at commit, only if it is newer than the last change
	   * recorded in the page's LSA.
	   */
	  if (LSA_LT (page_lsa, &unique_stat_info->last_log_lsa))
	    {
	      if (pgbuf_set_lsa
		  (thread_p, root, &unique_stat_info->last_log_lsa) == NULL)
		{
		  goto exit_on_error;
		}
	    }

	  /* set the root page as dirty page */
	  pgbuf_set_dirty (thread_p, root, DONT_FREE);
	}
    }

  /* free the root page */
  pgbuf_unfix_and_init (thread_p, root);

  return ret;

exit_on_error:

  if (root != NULL)
    {
      pgbuf_unfix_and_init (thread_p, root);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_locate_key () - Locate leaf node in b-tree for the given key.
 *   return: Leaf node page pointer.
 *   btid_int (in) : B+tree index info.
 *   key (in) : Key to locate
 *   pg_vpid (out) : Outputs Leaf node page VPID.
 *   slot_id (out) : Outputs slot ID of key if found, or slot ID of key if it
 *		     was to be inserted.
 *   found_p (out) : Outputs true if key was found and false otherwise.
 *
 * Note: Search the B+tree index to locate the page and record that contains
 *	 the key, or would contain the key if the key was to be located.
 */
static PAGE_PTR
btree_locate_key (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
		  DB_VALUE * key, VPID * pg_vpid, INT16 * slot_id,
		  bool * found_p)
{
  PAGE_PTR leaf_page = NULL;	/* Leaf node page pointer. */
  /* Search key result. */
  BTREE_SEARCH_KEY_HELPER search_key = BTREE_SEARCH_KEY_HELPER_INITIALIZER;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (btid_int->sys_btid != NULL);
  assert (found_p != NULL);
  assert (slot_id != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (!BTREE_INVALID_INDEX_ID (btid_int->sys_btid));

  *found_p = false;

  /* Advance in b-tree following key until leaf node is reached. */
  if (btree_search_key_and_apply_functions (thread_p, btid_int->sys_btid,
					    NULL, key, NULL, NULL,
					    btree_advance_and_find_key,
					    slot_id, NULL, NULL, &search_key,
					    &leaf_page) != NO_ERROR)
    {
      /* Error */
      ASSERT_ERROR ();
      assert (leaf_page == NULL);
      return NULL;
    }
  assert (leaf_page != NULL);

  /* Output found and slot ID. */
  *found_p = (search_key.result == BTREE_KEY_FOUND);
  *slot_id = search_key.slotid;
  if (pg_vpid != NULL)
    {
      /* Output leaf node page VPID. */
      pgbuf_get_vpid (leaf_page, pg_vpid);
    }
  /* Return leaf node page pointer. */
  return leaf_page;
}

/*
 * btree_find_lower_bound_leaf () -
 *   return: NO_ERROR
 *   BTS(in):
 *   stat_info(in):
 *
 * Note: Find the first/last leaf page of the B+tree index.
 */
static int
btree_find_lower_bound_leaf (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
			     BTREE_STATS * stat_info_p)
{
  int key_cnt;
  int ret = NO_ERROR;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  RECDES rec;

  if (bts->use_desc_index)
    {
      assert_release (stat_info_p == NULL);
      bts->C_page = btree_find_rightmost_leaf (thread_p,
					       bts->btid_int.sys_btid,
					       &bts->C_vpid, stat_info_p);
    }
  else
    {
      bts->C_page = btree_find_leftmost_leaf (thread_p,
					      bts->btid_int.sys_btid,
					      &bts->C_vpid, stat_info_p);
    }

  if (bts->C_page == NULL)
    {
      goto exit_on_error;
    }

  /* get header information (key_cnt) */
  key_cnt = btree_node_number_of_keys (bts->C_page);

  header = btree_get_node_header (bts->C_page);
  if (header == NULL)
    {
      goto exit_on_error;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if (node_type != BTREE_LEAF_NODE)
    {
      assert_release (false);
      goto exit_on_error;
    }

  /* set slot id and OID position */
  if (bts->use_desc_index)
    {
      bts->slot_id = key_cnt;
    }
  else
    {
      bts->slot_id = 1;
    }

  if (key_cnt == 0)
    {
      /* tree is empty; need to unfix current leaf page */
      ret = btree_find_next_index_record (thread_p, bts);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      assert_release (BTREE_END_OF_SCAN (bts));
    }
  else
    {
      /* Key may be fence and fences must be filtered out. */
      if (spage_get_record (bts->C_page, bts->slot_id, &rec, PEEK) !=
	  S_SUCCESS)
	{
	  assert (false);
	  goto exit_on_error;
	}
      assert (rec.length % 4 == 0);

      if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
	{
	  /* Filter out fence key. */
	  ret = btree_find_next_index_record (thread_p, bts);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }
	}
      else
	{
	  bts->oid_pos = 0;
	  assert_release (bts->slot_id <= key_cnt);
	}
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_find_leftmost_leaf () -
 *   return: page pointer
 *   btid(in):
 *   pg_vpid(in):
 *   stat_info_p(in):
 *
 * Note: Find the page identifier for the first leaf page of the B+tree index.
 */
static PAGE_PTR
btree_find_leftmost_leaf (THREAD_ENTRY * thread_p, BTID * btid,
			  VPID * pg_vpid, BTREE_STATS * stat_info)
{
  return btree_find_boundary_leaf (thread_p, btid, pg_vpid, stat_info,
				   BTREE_BOUNDARY_FIRST);
}

/*
 * btree_find_rightmost_leaf () -
 *   return: page pointer
 *   btid(in):
 *   pg_vpid(in):
 *   stat_info(in):
 *
 * Note: Find the page identifier for the last leaf page of the B+tree index.
 */
static PAGE_PTR
btree_find_rightmost_leaf (THREAD_ENTRY * thread_p, BTID * btid,
			   VPID * pg_vpid, BTREE_STATS * stat_info)
{
  return btree_find_boundary_leaf (thread_p, btid, pg_vpid, stat_info,
				   BTREE_BOUNDARY_LAST);
}

/*
 * btree_find_boundary_leaf () -
 *   return: page pointer
 *   btid(in):
 *   pg_vpid(in):
 *   stat_info(in):
 *
 * Note: Find the page identifier for the first/last leaf page
 *       of the B+tree index.
 */
static PAGE_PTR
btree_find_boundary_leaf (THREAD_ENTRY * thread_p, BTID * btid,
			  VPID * pg_vpid, BTREE_STATS * stat_info,
			  BTREE_BOUNDARY where)
{
  PAGE_PTR P_page = NULL, C_page = NULL;
  VPID P_vpid, C_vpid;
  BTREE_ROOT_HEADER *root_header = NULL;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  NON_LEAF_REC nleaf;
  RECDES rec;
  int key_cnt = 0, index = 0;
  int root_level = 0, depth = 0;

  VPID_SET_NULL (pg_vpid);

  /* read the root page */
  P_vpid.volid = btid->vfid.volid;
  P_vpid.pageid = btid->root_pageid;
  P_page = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		      PGBUF_UNCONDITIONAL_LATCH);
  if (P_page == NULL)
    {
      ASSERT_ERROR ();
      goto error;
    }

  (void) pgbuf_check_page_ptype (thread_p, P_page, PAGE_BTREE);

  root_header = btree_get_root_header (P_page);
  if (root_header == NULL)
    {
      goto error;
    }

  root_level = root_header->node.node_level;
  node_type = (root_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  while (node_type == BTREE_NON_LEAF_NODE)
    {
      key_cnt = btree_node_number_of_keys (P_page);
      if (key_cnt <= 0)
	{			/* node record underflow */
	  er_log_debug (ARG_FILE_LINE,
			"btree_find_boundary_leaf: node key count"
			" underflow: %d.Operation Ignored.", key_cnt);
	  goto error;
	}

      assert (where == BTREE_BOUNDARY_FIRST || where == BTREE_BOUNDARY_LAST);
      if (where == BTREE_BOUNDARY_FIRST)
	{
	  index = 1;
	}
      else
	{
	  index = key_cnt;
	}

      depth++;

      /* get the child page to flow */
      assert (index > 0);
      if (spage_get_record (P_page, index, &rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      btree_read_fixed_portion_of_non_leaf_record (&rec, &nleaf);
      C_vpid = nleaf.pnt;
      C_page = pgbuf_fix (thread_p, &C_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
      if (C_page == NULL)
	{
	  ASSERT_ERROR ();
	  goto error;
	}

      (void) pgbuf_check_page_ptype (thread_p, C_page, PAGE_BTREE);

      pgbuf_unfix_and_init (thread_p, P_page);

      key_cnt = btree_node_number_of_keys (C_page);

      header = btree_get_node_header (C_page);
      if (header == NULL)
	{
	  goto error;
	}

      node_type =
	(header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

      P_page = C_page;
      C_page = NULL;
      P_vpid = C_vpid;
    }

  if (key_cnt != 0)
    {
      goto end;			/* OK */
    }

again:

  header = btree_get_node_header (P_page);
  if (header == NULL)
    {
      goto error;
    }

  /* fix the next leaf page and set slot_id and oid_pos if it exists. */
  assert (where == BTREE_BOUNDARY_FIRST || where == BTREE_BOUNDARY_LAST);
  if (where == BTREE_BOUNDARY_FIRST)
    {
      C_vpid = header->next_vpid;	/* move backward */
    }
  else
    {
      C_vpid = header->prev_vpid;	/* move foward */
    }

  if (!VPID_ISNULL (&C_vpid))
    {
      C_page = pgbuf_fix (thread_p, &C_vpid, OLD_PAGE,
			  PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (C_page == NULL)
	{
	  ASSERT_ERROR ();
	  goto error;
	}

      (void) pgbuf_check_page_ptype (thread_p, C_page, PAGE_BTREE);

      /* unfix the previous leaf page if it is fixed. */
      if (P_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, P_page);
	  /* do not clear bts->P_vpid for UNCONDITIONAL lock request handling */
	}
    }

  /* check if the current leaf page has valid slots */
  if (C_page != NULL)
    {
      key_cnt = btree_node_number_of_keys (C_page);

      if (key_cnt <= 0)
	{			/* empty page */
	  P_page = C_page;
	  C_page = NULL;
	  goto again;
	}

      P_vpid = C_vpid;
      P_page = C_page;
    }

  /* NOTE that we do NOT release the page latch on P here */
end:

  *pg_vpid = P_vpid;

  assert_release (root_level == depth + 1);

  if (stat_info)
    {
      stat_info->height = root_level;
    }

  return P_page;

error:

  if (P_page)
    {
      pgbuf_unfix_and_init (thread_p, P_page);
    }
  if (C_page)
    {
      pgbuf_unfix_and_init (thread_p, C_page);
    }

  return NULL;
}

/*
 * btree_find_AR_sampling_leaf () -
 *   return: page pointer
 *   btid(in):
 *   pg_vpid(in):
 *   stat_info_p(in):
 *   found_p(out):
 *
 * Note: Find the page identifier via the Acceptance/Rejection Sampling
 *       leaf page of the B+tree index.
 * Note: Random Sampling from Databases
 *       (Chapter 3. Random Sampling from B+ Trees)
 */
static PAGE_PTR
btree_find_AR_sampling_leaf (THREAD_ENTRY * thread_p, BTID * btid,
			     VPID * pg_vpid, BTREE_STATS * stat_info_p,
			     bool * found_p)
{
  PAGE_PTR P_page = NULL, C_page = NULL;
  VPID P_vpid, C_vpid;
  int slot_id;
  BTREE_ROOT_HEADER *root_header = NULL;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  NON_LEAF_REC nleaf;
  RECDES rec;
  int est_page_size, free_space;
  int key_cnt = 0;
  int root_level = 0, depth = 0;
  double prob = 1.0;		/* Acceptance probability */

  assert (stat_info_p != NULL);
  assert (found_p != NULL);

  *found_p = false;		/* init */

  VPID_SET_NULL (pg_vpid);

  /* read the root page */
  P_vpid.volid = btid->vfid.volid;
  P_vpid.pageid = btid->root_pageid;
  P_page = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		      PGBUF_UNCONDITIONAL_LATCH);
  if (P_page == NULL)
    {
      goto error;
    }

  (void) pgbuf_check_page_ptype (thread_p, P_page, PAGE_BTREE);

  key_cnt = btree_node_number_of_keys (P_page);

  root_header = btree_get_root_header (P_page);
  if (root_header == NULL)
    {
      goto error;
    }

  root_level = root_header->node.node_level;
  node_type = (root_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  est_page_size = (int) (DB_PAGESIZE - (spage_header_size () +
					sizeof (BTREE_NODE_HEADER) +
					spage_slot_size ()));
  assert (est_page_size > 0);

  while (node_type == BTREE_NON_LEAF_NODE)
    {
      depth++;

      /* get the randomized child page to follow */

      if (key_cnt <= 0)
	{			/* node record underflow */
	  er_log_debug (ARG_FILE_LINE, "btree_find_AR_sampling_leaf:"
			" node key count underflow: %d. Operation Ignored.",
			key_cnt);
	  goto error;
	}

      slot_id = (int) (drand48 () * key_cnt);
      slot_id = MAX (slot_id, 1);

      assert (slot_id > 0);
      if (spage_get_record (P_page, slot_id, &rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}

      btree_read_fixed_portion_of_non_leaf_record (&rec, &nleaf);
      C_vpid = nleaf.pnt;
      C_page = pgbuf_fix (thread_p, &C_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			  PGBUF_UNCONDITIONAL_LATCH);
      if (C_page == NULL)
	{
	  goto error;
	}

      (void) pgbuf_check_page_ptype (thread_p, C_page, PAGE_BTREE);

      pgbuf_unfix_and_init (thread_p, P_page);

      key_cnt = btree_node_number_of_keys (C_page);

      header = btree_get_node_header (C_page);
      if (header == NULL)
	{
	  goto error;
	}

      node_type =
	(header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

      /* update Acceptance probability */

      free_space = spage_max_space_for_new_record (thread_p, C_page);
      assert (est_page_size > free_space);

      prob *=
	(((double) est_page_size) - free_space) / ((double) est_page_size);

      P_page = C_page;
      C_page = NULL;
      P_vpid = C_vpid;
    }

  if (key_cnt != 0)
    {
      goto end;			/* OK */
    }

again:

  header = btree_get_node_header (P_page);
  if (header == NULL)
    {
      goto error;
    }

  /* fix the next leaf page and set slot_id and oid_pos if it exists. */
  C_vpid = header->next_vpid;
  if (!VPID_ISNULL (&C_vpid))
    {
      C_page = pgbuf_fix (thread_p, &C_vpid, OLD_PAGE,
			  PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (C_page == NULL)
	{
	  goto error;
	}

      (void) pgbuf_check_page_ptype (thread_p, C_page, PAGE_BTREE);

      /* unfix the previous leaf page if it is fixed. */
      if (P_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, P_page);
	  /* do not clear bts->P_vpid for UNCONDITIONAL lock request handling */
	}
    }

  /* check if the current leaf page has valid slots */
  if (C_page != NULL)
    {
      key_cnt = btree_node_number_of_keys (C_page);

      if (key_cnt <= 0)
	{			/* empty page */
	  P_page = C_page;
	  C_page = NULL;
	  goto again;
	}
      P_vpid = C_vpid;
      P_page = C_page;
    }

  /* NOTE that we do NOT release the page latch on P here */
end:

  *pg_vpid = P_vpid;

  assert_release (root_level == depth + 1);

  stat_info_p->height = root_level;

  /* do Acceptance/Rejection sampling */
  if (drand48 () < prob)
    {
      /* Acceptance */
      *found_p = true;
    }
  else
    {
      /* Rejection */
      assert (*found_p == false);
    }

  return P_page;

error:

  if (P_page)
    {
      pgbuf_unfix_and_init (thread_p, P_page);
    }
  if (C_page)
    {
      pgbuf_unfix_and_init (thread_p, C_page);
    }

  return NULL;
}

/*
 * btree_keyval_search () -
 *   return: the number of object identifiers in the set pointed
 *           at by oids_ptr, or -1 if an error occurs. Since there can be
 *           many object identifiers for the given key, to avoid main
 *           memory limitations, the set of object identifiers are returned
 *           iteratively. At each call, the btree_scan is modified, to
 *           remember the old search position.
 *   btid(in):
 *      btid: B+tree index identifier
 *   scan_op_type(in):
 *   bts(in/out): Btree range search scan structure
 *   key(in): Key to be searched for its object identifier set
 *   class_oid(in):
 *   oids_ptr(in): Points to the already allocated storage area to store oids
 *   oids_size(in): Size of allocated area for oid set storage
 *   filter(in):
 *   isidp(in):
 *   is_all_class_srch(in):
 *
 * Note: Finds the set of object identifiers for the given key.
 * if the key is not found, a 0 count is returned. Otherwise,
 * the area pointed at by oids_ptr is filled with one group of
 * object identifiers.
 *
 * Note: the btree_scan structure must first be initialized by using the macro
 * BTREE_INIT_SCAN() defined in bt.h
 *
 * Note: After the first iteration, caller can use BTREE_END_OF_SCAN() macro
 * defined in bt.h to understand the end of range.
 *
 * NOTE: Instead of range scan, this can be replaced with a different function
 *	 to go to key directly.
 */
int
btree_keyval_search (THREAD_ENTRY * thread_p, BTID * btid,
		     SCAN_OPERATION_TYPE scan_op_type,
		     BTREE_SCAN * bts,
		     KEY_VAL_RANGE * key_val_range, OID * class_oid,
		     FILTER_INFO * filter, INDX_SCAN_ID * isidp,
		     bool is_all_class_srch)
{
  /* this is just a GE_LE range search with the same key */
  int rc;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (bts != NULL);
  assert (isidp != NULL);
  assert (isidp->need_count_only == false);
  assert (key_val_range != NULL);
  /* If a class must be matched, class_oid argument must be a valid OID. */
  assert (is_all_class_srch
	  || (class_oid != NULL && !OID_ISNULL (class_oid)));

  /* Execute range scan */
  rc =
    btree_prepare_bts (thread_p, bts, btid, isidp, key_val_range, filter,
		       is_all_class_srch ? class_oid : NULL, NULL, NULL,
		       false, NULL);
  if (rc != NO_ERROR)
    {
      ASSERT_ERROR ();
      return rc;
    }
  rc = btree_range_scan (thread_p, bts, btree_range_scan_select_visible_oids);
  if (rc != NO_ERROR)
    {
      ASSERT_ERROR ();
      return rc;
    }
  assert (bts->n_oids_read_last_iteration >= 0);

  return bts->n_oids_read_last_iteration;
}

/*
 * btree_coerce_key () -
 *   return: NO_ERROR or error code
 *   src_keyp(in/out):
 *   keysize(in): term# associated with index key range
 *   btree_domainp(in): B+tree index domain
 *   key_minmax(in): MIN_VALUE or MAX_VALUE
 *
 * Note:
 */
int
btree_coerce_key (DB_VALUE * keyp, int keysize,
		  TP_DOMAIN * btree_domainp, int key_minmax)
{
  DB_TYPE stype, dtype;
  int ssize, dsize;
  TP_DOMAIN *dp;
  DB_VALUE value;
  DB_MIDXKEY *midxkey;
  TP_DOMAIN *partial_dom;
  int minmax;
  int err = NO_ERROR;
  bool part_key_desc = false;

  /* assuming all parameters are not NULL pointer, and 'src_key' is not NULL
     value */
  stype = DB_VALUE_TYPE (keyp);
  dtype = TP_DOMAIN_TYPE (btree_domainp);

  if (stype == DB_TYPE_MIDXKEY && dtype == DB_TYPE_MIDXKEY)
    {
      /* if multi-column index */
      /* The type of B+tree key domain can be DB_TYPE_MIDXKEY only in the
         case of multi-column index. And, if it is, query optimizer makes
         the search key('src_key') as sequence type even if partial key was
         specified. One more assumption is that query optimizer make the
         search key(either complete or partial) in the same order (of
         sequence) of B+tree key domain. */

      /* get number of elements of sequence type of the 'src_key' */
      midxkey = DB_PULL_MIDXKEY (keyp);
      ssize = midxkey->ncolumns;

      /* count number of elements of sequence type of the B+tree key domain */
      for (dp = btree_domainp->setdomain, dsize = 0; dp;
	   dp = dp->next, dsize++)
	{
	  ;
	}

      if (ssize < 0 || ssize > dsize || dsize == 0 || ssize > keysize)
	{
	  /* something wrong with making search key in query optimizer */
	  err = ER_FAILED;	/* error */
	}
      else if (ssize == dsize)
	{
	  if (midxkey->domain == NULL)	/* checkdb */
	    {
	      midxkey->domain = btree_domainp;
	    }

	  return NO_ERROR;
	}
      else
	{
	  /* do coercing, append min or max value of the coressponding domain
	     type to the partial search key value */
	  DB_VALUE *dbvals = NULL;
	  int num_dbvals;

	  num_dbvals = dsize - ssize;

	  if (num_dbvals == 1)
	    {
	      dbvals = &value;
	    }
	  else if (num_dbvals > 1)
	    {
	      dbvals = (DB_VALUE *) db_private_alloc (NULL,
						      num_dbvals *
						      sizeof (DB_VALUE));
	      if (dbvals == NULL)
		{
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}
	    }
	  else
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	      fprintf (stderr, "Error: btree_coerce_key(num_dbval %d)\n",
		       num_dbvals);
	      return ER_GENERIC_ERROR;
	    }

	  /* get the last domain element of partial-key */
	  for (dp = btree_domainp->setdomain, dsize = 1;
	       dsize < keysize && dp; dsize++, dp = dp->next)
	    {
	      ;			/* nop */
	    }

	  if (dsize < keysize || dp == NULL)
	    {
	      if (dbvals != &value)
		{
		  db_private_free_and_init (NULL, dbvals);
		}
	      return ER_FAILED;
	    }

	  part_key_desc = dp->is_desc;

	  for (dp = btree_domainp->setdomain, dsize = 0; dp && dsize < ssize;
	       dp = dp->next, dsize++)
	    {
	      ;
	    }

	  num_dbvals = 0;
	  partial_dom = dp;

	  for (err = NO_ERROR; dp && err == NO_ERROR; dp = dp->next, dsize++)
	    {
	      /* server doesn't treat DB_TYPE_OBJECT, so that convert it to
	         DB_TYPE_OID */
	      DB_TYPE type;

	      type = (TP_DOMAIN_TYPE (dp) == DB_TYPE_OBJECT) ? DB_TYPE_OID
		: TP_DOMAIN_TYPE (dp);

	      minmax = key_minmax;	/* init */
	      if (minmax == BTREE_COERCE_KEY_WITH_MIN_VALUE)
		{
		  if (!part_key_desc)
		    {		/* CASE 1, 2 */
		      if (dp->is_desc != true)
			{	/* CASE 1 */
			  ;	/* nop */
			}
		      else
			{	/* CASE 2 */
			  minmax = BTREE_COERCE_KEY_WITH_MAX_VALUE;
			}
		    }
		  else
		    {		/* CASE 3, 4 */
		      if (dp->is_desc != true)
			{	/* CASE 3 */
			  minmax = BTREE_COERCE_KEY_WITH_MAX_VALUE;
			}
		      else
			{	/* CASE 4 */
			  ;	/* nop */
			}
		    }
		}
	      else if (minmax == BTREE_COERCE_KEY_WITH_MAX_VALUE)
		{
		  if (!part_key_desc)
		    {		/* CASE 1, 2 */
		      if (dp->is_desc != true)
			{	/* CASE 1 */
			  ;	/* nop */
			}
		      else
			{	/* CASE 2 */
			  minmax = BTREE_COERCE_KEY_WITH_MIN_VALUE;
			}
		    }
		  else
		    {		/* CASE 3, 4 */
		      if (dp->is_desc != true)
			{	/* CASE 3 */
			  minmax = BTREE_COERCE_KEY_WITH_MIN_VALUE;
			}
		      else
			{	/* CASE 4 */
			  ;	/* nop */
			}
		    }
		}

	      if (minmax == BTREE_COERCE_KEY_WITH_MIN_VALUE)
		{
		  if (dsize < keysize)
		    {
		      err = db_value_domain_min (&dbvals[num_dbvals], type,
						 dp->precision, dp->scale,
						 dp->codeset,
						 dp->collation_id,
						 &dp->enumeration);
		    }
		  else
		    {
		      err = db_value_domain_init (&dbvals[num_dbvals], type,
						  dp->precision, dp->scale);
		    }
		}
	      else if (minmax == BTREE_COERCE_KEY_WITH_MAX_VALUE)
		{
		  err = db_value_domain_max (&dbvals[num_dbvals], type,
					     dp->precision, dp->scale,
					     dp->codeset, dp->collation_id,
					     &dp->enumeration);
		}
	      else
		{
		  err = ER_FAILED;
		}

	      num_dbvals++;
	    }

	  if (err == NO_ERROR)
	    {
	      err = pr_midxkey_add_elements (keyp, dbvals, num_dbvals,
					     partial_dom);
	    }

	  if (dbvals != &value)
	    {
	      db_private_free_and_init (NULL, dbvals);
	    }
	}

    }
  else if (
	    /* check if they are string or bit type */
	    /* compatible if two types are same (except for sequence type) */
	    (stype == dtype)
	    /* CHAR type and VARCHAR type are compatible with each other */
	    || ((stype == DB_TYPE_CHAR || stype == DB_TYPE_VARCHAR)
		&& (dtype == DB_TYPE_CHAR || dtype == DB_TYPE_VARCHAR))
	    /* NCHAR type and VARNCHAR type are compatible with each other */
	    || ((stype == DB_TYPE_NCHAR || stype == DB_TYPE_VARNCHAR)
		&& (dtype == DB_TYPE_NCHAR || dtype == DB_TYPE_VARNCHAR))
	    /* BIT type and VARBIT type are compatible with each other */
	    || ((stype == DB_TYPE_BIT || stype == DB_TYPE_VARBIT)
		&& (dtype == DB_TYPE_BIT || dtype == DB_TYPE_VARBIT))
	    /* OID type and OBJECT type are compatible with each other */
	    /* Keys can come in with a type of DB_TYPE_OID, but the B+tree domain
	       itself will always be a DB_TYPE_OBJECT. The comparison routines
	       can handle OID and OBJECT as compatible type with each other . */
	    || (stype == DB_TYPE_OID || stype == DB_TYPE_OBJECT))
    {
      err = NO_ERROR;
    }
  else
    {
      DB_VALUE temp_val;

      DB_MAKE_NULL (&temp_val);

      if (tp_more_general_type (dtype, stype) > 0)
	{
	  /* the other case, do real coercing using 'tp_value_coerce()' */
	  if (tp_value_coerce (keyp, &temp_val,
			       btree_domainp) == DOMAIN_COMPATIBLE)
	    {
	      pr_clear_value (keyp);
	      pr_clone_value (&temp_val, keyp);
	    }

	  pr_clear_value (&temp_val);
	}
      else if (TP_IS_NUMERIC_TYPE (dtype) || TP_IS_DATE_OR_TIME_TYPE (dtype))
	{
	  /* try to strict cast keyp to dtype */
	  err = tp_value_coerce_strict (keyp, &temp_val, btree_domainp);
	  if (err == NO_ERROR)
	    {
	      pr_clear_value (keyp);
	      pr_clone_value (&temp_val, keyp);
	    }
	  else
	    {
	      /* unsuccessful,  */
	      err = NO_ERROR;
	    }

	  pr_clear_value (&temp_val);
	}
      else
	{
	  err = NO_ERROR;
	}
    }

  if (err != NO_ERROR)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
    }

  /* return result */
  return err;
}

/*
 * btree_prepare_bts () - Prepare b-tree scan structure before starting
 *			  index scan.
 *
 * return		   : Error code.
 * thread_p (in)	   : Thread entry.
 * bts (in)		   : B-tree scan structure.
 * btid (in)		   : B-tree identifier.
 * index_scan_id_p (in)	   : Index scan info.
 * key_val_range (in)	   : Range of scan.
 * filter (in)		   : Key filter.
 * match_class_oid (in)	   : Non-NULL value if class must be matched (unique
 *			     indexes).
 * key_limit_upper (in)	   : Pointer to upper key limit. NULL if there is no
 *			     upper key limit.
 * key_limit_lower (in)	   : Pointer to lower key limit. NULL if there is no
 *			     lower key limit.
 * need_to_check_null (in) : True if midxkey NULL needs to be checked.
 * bts_other (in/out)	   : Sets the argument specific to one type of range
 *			     search.
 */
int
btree_prepare_bts (THREAD_ENTRY * thread_p, BTREE_SCAN * bts, BTID * btid,
		   INDX_SCAN_ID * index_scan_id_p,
		   KEY_VAL_RANGE * key_val_range, FILTER_INFO * filter,
		   const OID * match_class_oid,
		   DB_BIGINT * key_limit_upper, DB_BIGINT * key_limit_lower,
		   bool need_to_check_null, void *bts_other)
{
  KEY_VAL_RANGE inf_key_val_range;
  PAGE_PTR root_page = NULL;
  VPID root_vpid;
  int error_code = NO_ERROR;
  DB_MIDXKEY *midxkey = NULL;
  DB_VALUE *swap_key = NULL;
  int i = 0;

  /* Assert expected arguments. */
  assert (bts != NULL);
  /* If b-tree info is valid, then topclass_oid must not be NULL. */
  assert (!bts->is_btid_int_valid
	  || !OID_ISNULL (&bts->btid_int.topclass_oid));

  if (bts->is_scan_started)
    {
      /* B-tree scan must have been initialized already. */
      return NO_ERROR;
    }

  assert (VPID_ISNULL (&bts->C_vpid));

  if (key_val_range == NULL)
    {
      /* NULL key_val_range argument means a full range scan */
      DB_MAKE_NULL (&inf_key_val_range.key1);
      DB_MAKE_NULL (&inf_key_val_range.key2);
      inf_key_val_range.range = INF_INF;
      inf_key_val_range.num_index_term = 0;
      inf_key_val_range.is_truncated = false;

      key_val_range = &inf_key_val_range;
    }

  if (!bts->is_btid_int_valid)
    {
      root_vpid.pageid = btid->root_pageid;
      root_vpid.volid = btid->vfid.volid;
      root_page =
	btree_fix_root_with_info (thread_p, btid, PGBUF_LATCH_READ, NULL,
				  NULL, &bts->btid_int);
      if (root_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      /* B-tree info successfully obtained. */
      /* Root page is no longer needed. */
      pgbuf_unfix_and_init (thread_p, root_page);

      /* TODO: Why is the below code here? What does constructing btid have to
       *       do with the issue described below? Shouldn't this be always
       *       verified? It doesn't look like belonging here.
       */
      /*
       * The asc/desc properties in midxkey from log_applier may be
       * inaccurate. therefore, we should use btree header's domain while
       * processing btree search request from log_applier.
       */
      if (DB_VALUE_TYPE (&key_val_range->key1) == DB_TYPE_MIDXKEY)
	{
	  midxkey = DB_PULL_MIDXKEY (&key_val_range->key1);
	  if (midxkey->domain == NULL || LOG_CHECK_LOG_APPLIER (thread_p)
	      || LOG_CHECK_LOG_PREFETCHER (thread_p))
	    {
	      /*
	       * The asc/desc properties in midxkey from log_applier may be
	       * inaccurate. therefore, we should use btree header's domain
	       * while processing btree search request from log_applier.
	       */
	      if (midxkey->domain)
		{
		  tp_domain_free (midxkey->domain);
		}
	      midxkey->domain = bts->btid_int.key_type;
	    }
	}
      if (DB_VALUE_TYPE (&key_val_range->key2) == DB_TYPE_MIDXKEY)
	{
	  midxkey = DB_PULL_MIDXKEY (&key_val_range->key2);
	  if (midxkey->domain == NULL || LOG_CHECK_LOG_APPLIER (thread_p)
	      || LOG_CHECK_LOG_PREFETCHER (thread_p))
	    {
	      if (midxkey->domain)
		{
		  tp_domain_free (midxkey->domain);
		}
	      midxkey->domain = bts->btid_int.key_type;
	    }
	}

      /* TODO: What does this assert mean? */
      /* is from keyval_search; checkdb or find_unique */
      assert_release (key_val_range->num_index_term == 0);

      /* B-tree scan btid_int is now valid. */
      bts->is_btid_int_valid = true;
    }

  if (index_scan_id_p)
    {
      bts->index_scan_idp = index_scan_id_p;
      if (index_scan_id_p->indx_info != NULL)
	{
	  bts->use_desc_index = index_scan_id_p->indx_info->use_desc_index;
	}
      else
	{
	  bts->use_desc_index = false;
	}
      bts->oid_ptr =
	bts->index_scan_idp->oid_list != NULL ?
	bts->index_scan_idp->oid_list->oidp : NULL;

      /* set index key copy_buf info;
       * is allocated at btree_keyval_search() or scan_open_index_scan().
       */
      /* TODO: Use index_scan_id_p->copy_buf directly. */
      bts->btid_int.copy_buf = index_scan_id_p->copy_buf;
      bts->btid_int.copy_buf_len = index_scan_id_p->copy_buf_len;
    }

  /* initialize the key range with given information */
  switch (key_val_range->range)
    {
    case EQ_NA:
    case GT_LT:
    case GT_LE:
    case GE_LT:
    case GE_LE:
    case GE_INF:
    case GT_INF:
    case INF_LE:
    case INF_LT:
    case INF_INF:
      break;
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_RANGE, 0);
      return ER_BTREE_INVALID_RANGE;
    }

  /* Set up the keys and make sure that they have the proper domain
   * (by coercing, if necessary). Open-ended searches will have one or
   * both of key1 or key2 set to NULL so that we no longer have to do
   * DB_IS_NULL() tests on them.
   */
  /* TODO: fix multi-column index NULL problem */
  /* Only used for multi-column index with PRM_ORACLE_STYLE_EMPTY_STRING,
   * otherwise set as zero
   */

  /* Set key range. */
  bts->key_range.num_index_term = key_val_range->num_index_term;

  /* re-check for partial-key domain is desc */
  if (!BTREE_IS_PART_KEY_DESC (&(bts->btid_int)))
    {
      TP_DOMAIN *dom;

      dom = bts->btid_int.key_type;
      if (TP_DOMAIN_TYPE (dom) == DB_TYPE_MIDXKEY)
	{
	  dom = dom->setdomain;
	}

      /* get the last domain element of partial-key */
      for (i = 1; i < key_val_range->num_index_term && dom;
	   i++, dom = dom->next)
	{
	  ;			/* nop */
	}

      if (i < key_val_range->num_index_term || dom == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      bts->btid_int.part_key_desc = dom->is_desc;
    }

#if !defined(NDEBUG)
  if (DB_VALUE_TYPE (&key_val_range->key1) == DB_TYPE_MIDXKEY)
    {
      midxkey = DB_PULL_MIDXKEY (&key_val_range->key1);
      assert (midxkey->ncolumns == midxkey->domain->precision);
    }
  if (DB_VALUE_TYPE (&key_val_range->key2) == DB_TYPE_MIDXKEY)
    {
      midxkey = DB_PULL_MIDXKEY (&key_val_range->key2);
      assert (midxkey->ncolumns == midxkey->domain->precision);
    }
#endif

  /* lower bound key and upper bound key */
  if (DB_IS_NULL (&key_val_range->key1)
      || btree_multicol_key_is_null (&key_val_range->key1))
    {
      bts->key_range.lower_key = NULL;
    }
  else
    {
      bts->key_range.lower_key = &key_val_range->key1;
    }

  if (DB_IS_NULL (&key_val_range->key2)
      || btree_multicol_key_is_null (&key_val_range->key2))
    {
      bts->key_range.upper_key = NULL;
    }
  else
    {
      bts->key_range.upper_key = &key_val_range->key2;
    }

  /* range type */
  bts->key_range.range = key_val_range->range;

  /* Swap range for scan is descending. */
  if ((bts->use_desc_index && !BTREE_IS_PART_KEY_DESC (&bts->btid_int))
      || (!bts->use_desc_index && BTREE_IS_PART_KEY_DESC (&bts->btid_int)))
    {
      /* Reverse scan and its range. */
      RANGE_REVERSE (bts->key_range.range);
      swap_key = bts->key_range.lower_key;
      bts->key_range.lower_key = bts->key_range.upper_key;
      bts->key_range.upper_key = swap_key;
    }

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
    {
      /* TODO: A comment explaining this would be great. */
      int j, ids_size;

      if (filter)
	{
	  ids_size = 0;		/* init */
	  for (i = 0; i < key_val_range->num_index_term; i++)
	    {
	      filter->vstr_ids[i] = -1;	/* init to false */
	      for (j = 0; j < filter->scan_attrs->num_attrs; j++)
		{
		  if (filter->btree_attr_ids[i] ==
		      filter->scan_attrs->attr_ids[j])
		    {
		      filter->vstr_ids[i] = filter->btree_attr_ids[i];
		      ids_size = i + 1;
		      break;
		    }
		}
	    }

	  /* reset num of variable string attr in key range */
	  *(filter->num_vstr_ptr) = ids_size;
	}
    }
  /* Initialize key filter */
  if (filter)			/* Valid pointer or NULL */
    {
      bts->key_filter_storage = *filter;
      bts->key_filter = &bts->key_filter_storage;
    }
  else
    {
      bts->key_filter = NULL;
    }
  /* Reset key_range_max_value_equal */
  bts->key_range_max_value_equal = false;

  bts->read_keys = 0;
  bts->qualified_keys = 0;
  bts->n_oids_read = 0;
  bts->n_oids_read_last_iteration = 0;

  /* Key limits. */
  bts->key_limit_lower = key_limit_lower;
  bts->key_limit_upper = key_limit_upper;

  /* Should class OID be matched? (for hierarchical classes). */
  if (match_class_oid != NULL)
    {
      COPY_OID (&bts->match_class_oid, match_class_oid);
    }

  /* Need to check null? */
  bts->need_to_check_null = need_to_check_null;

  /* Set other arguments specific to scan type. */
  bts->bts_other = bts_other;

  /* Prepare successful. */
  return NO_ERROR;
}

/*
 * btree_scan_update_range () - Update range of b-tree scan.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * bts (in/out)	      : B-tree scan.
 * key_val_range (in) : New range.
 */
static int
btree_scan_update_range (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
			 KEY_VAL_RANGE * key_val_range)
{
  DB_MIDXKEY *midxkey = NULL;
  DB_VALUE *swap_key = NULL;

  /* Assert expected arguments. */
  assert (bts != NULL);
  assert (key_val_range != NULL);

  /* Check valid range. */
  switch (key_val_range->range)
    {
    case EQ_NA:
    case GT_LT:
    case GT_LE:
    case GE_LT:
    case GE_LE:
    case GE_INF:
    case GT_INF:
    case INF_LE:
    case INF_LT:
    case INF_INF:
      break;
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_RANGE, 0);
      return ER_BTREE_INVALID_RANGE;
    }

  /* Set key range. */
  bts->key_range.num_index_term = key_val_range->num_index_term;

#if !defined(NDEBUG)
  if (DB_VALUE_TYPE (&key_val_range->key1) == DB_TYPE_MIDXKEY)
    {
      midxkey = DB_PULL_MIDXKEY (&key_val_range->key1);
      assert (midxkey->ncolumns == midxkey->domain->precision);
    }
  if (DB_VALUE_TYPE (&key_val_range->key2) == DB_TYPE_MIDXKEY)
    {
      midxkey = DB_PULL_MIDXKEY (&key_val_range->key2);
      assert (midxkey->ncolumns == midxkey->domain->precision);
    }
#endif

  /* lower bound key and upper bound key */
  if (DB_IS_NULL (&key_val_range->key1)
      || btree_multicol_key_is_null (&key_val_range->key1))
    {
      bts->key_range.lower_key = NULL;
    }
  else
    {
      bts->key_range.lower_key = &key_val_range->key1;
    }

  if (DB_IS_NULL (&key_val_range->key2)
      || btree_multicol_key_is_null (&key_val_range->key2))
    {
      bts->key_range.upper_key = NULL;
    }
  else
    {
      bts->key_range.upper_key = &key_val_range->key2;
    }

  /* range type */
  bts->key_range.range = key_val_range->range;

  /* Swap range for scan is descending. */
  if ((bts->use_desc_index && !BTREE_IS_PART_KEY_DESC (&bts->btid_int))
      || (!bts->use_desc_index && BTREE_IS_PART_KEY_DESC (&bts->btid_int)))
    {
      /* Reverse scan and its range. */
      RANGE_REVERSE (bts->key_range.range);
      swap_key = bts->key_range.lower_key;
      bts->key_range.lower_key = bts->key_range.upper_key;
      bts->key_range.upper_key = swap_key;
    }

  return NO_ERROR;
}

/*
 * btree_find_next_index_record () -
 *   return: NO_ERROR
 *   bts(in):
 *
 * Note: This functions finds the next index record(or slot).
 * Then, it adjusts the slot_id and oid_pos information
 * about the oid-set contained in the found index slot.
 * If next records is located in next page, unfix current page
 * and change C_page as it.
 */
static int
btree_find_next_index_record (THREAD_ENTRY * thread_p, BTREE_SCAN * bts)
{
  PAGE_PTR first_page;
  int ret_val = NO_ERROR;

  first_page = bts->C_page;	/* init */

  ret_val =
    btree_find_next_index_record_holding_current (thread_p, bts, NULL);
#if 0				/* TODO - need to check return value */
  if (ret_val != NO_ERROR)
    {
      goto error;
    }
#endif

  if (first_page != bts->C_page)
    {
      /* reset common_prefix to recalculate */
      bts->common_prefix = COMMON_PREFIX_UNKNOWN;
    }

  /*
   * unfix first page if fix next page and move to it
   *
   *  case 1: P_page == NULL, C_page == first_page       x do not fix 1 next page
   *  case 2: P_page == first_page, C_page == NULL       x can't fix 1 next page
   *  case 3: P_page == first_page, C_page != first_pag  o fix 1 next
   *  case 4: P_page == NULL, C_page == NULL             o can't fix N next, unfix N-1 prev
   *  case 5: P_page == NULL, C_page != first_page       o fix N next, unfix N-1 prev
   *  other case: imppossible (assert)
   *
   *  in case of 3, 4, 5, unfix first_page
   */

#if !defined(NDEBUG)
  if ((bts->P_page == NULL && bts->C_page == first_page) ||
      (bts->P_page == first_page && bts->C_page == NULL) ||
      (bts->P_page == first_page && bts->C_page && bts->C_page != first_page)
      || (bts->P_page == NULL && bts->C_page == NULL) ||
      (bts->P_page == NULL && bts->C_page && bts->C_page != first_page))
    {
      /* case 1, 2, 3, 4, 5 */
    }
  else
    {
      assert (false);
    }
#endif

  if ((bts->C_page == NULL && bts->P_page == NULL)	/* case 4 */
      || (bts->C_page != NULL && bts->C_page != first_page))	/* case 3, 5 */
    {
      if (first_page == bts->P_page)
	{
	  /* prevent double unfix by caller */
	  bts->P_page = NULL;
	}

      if (first_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, first_page);
	}
    }

  return ret_val;
}

/*
 * btree_find_next_index_record_holding_current () -
 *   return: NO_ERROR
 *   bts(in):
 *
 * Note: This functions finds & peek next index record
 * this function does not unfix first page
 */
static int
btree_find_next_index_record_holding_current (THREAD_ENTRY * thread_p,
					      BTREE_SCAN * bts,
					      RECDES * peek_rec)
{
  RECDES rec;
  int ret = NO_ERROR;
  PAGE_PTR first_page = bts->C_page;

  rec.data = NULL;

  /*
   * Assumptions : last accessed leaf page is fixed.
   *    - bts->C_page != NULL
   *    - bts->O_page : NULL or NOT NULL
   *    - bts->P_page == NULL
   */

  /* unfix the overflow page if it is fixed. */
  if (bts->O_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, bts->O_page);
      VPID_SET_NULL (&(bts->O_vpid));
    }

  /* unfix the previous leaf page if it is fixed. */
  if (bts->P_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, bts->P_page);
      VPID_SET_NULL (&(bts->P_vpid));
    }

  if (bts->C_page == NULL)
    {
      return ER_FAILED;
    }

  bts->P_vpid = bts->C_vpid;	/* save started leaf vpid */

  while (bts->C_page != NULL)
    {
      ret = btree_find_next_index_record_holding_current_helper
	(thread_p, bts, first_page);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* filter out fence_key record */
      if (bts->C_page != NULL)
	{
	  assert (bts->slot_id > 0);
	  if ((bts->slot_id != 1
	       && bts->slot_id != btree_node_number_of_keys (bts->C_page))
	      || !btree_is_fence_key (bts->C_page, bts->slot_id))
	    {
	      /* Found. */
	      /* Safe guard: key cannot be fence if between 1 and key count.
	       */
	      assert (!btree_is_fence_key (bts->C_page, bts->slot_id));
	      break;
	    }
	  /* This is fence key. Continue searching. */
	}
    }

  if (VPID_EQ (&bts->P_vpid, &bts->C_vpid))
    {
      /* set bts->P_vpid to null for unconditional lock request handling */
      VPID_SET_NULL (&bts->P_vpid);
    }

  /* Safe guard: should not stop on fence key. */
  assert (bts->C_page == NULL
	  || !btree_is_fence_key (bts->C_page, bts->slot_id));

  if (bts->C_page == NULL)
    {
      assert (VPID_ISNULL (&bts->C_vpid));
      bts->end_scan = true;
      if (bts->P_page != NULL && bts->P_page != first_page)
	{
	  pgbuf_unfix_and_init (thread_p, bts->P_page);
	}
    }

  if (bts->C_page != NULL && peek_rec != NULL)
    {
      if (spage_get_record (bts->C_page, bts->slot_id, peek_rec, PEEK)
	  != S_SUCCESS)
	{
	  assert (false);
	  goto exit_on_error;
	}
    }

  return ret;

exit_on_error:

  assert (ret != NO_ERROR);

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_find_next_index_record_holding_current_helper () -
 *   return: NO_ERROR
 *   bts(in):
 *
 * Note: This functions finds the next index record(or slot).
 * Then, it adjusts the slot_id and oid_pos information
 * about the oid-set contained in the found index slot.
 */
static int
btree_find_next_index_record_holding_current_helper (THREAD_ENTRY * thread_p,
						     BTREE_SCAN * bts,
						     PAGE_PTR first_page)
{
  int key_cnt;
  int ret = NO_ERROR;
  PGBUF_LATCH_CONDITION latch_condition;
  BTREE_NODE_HEADER *header = NULL;

  /* get header information (key_cnt) from the current leaf page */
  key_cnt = btree_node_number_of_keys (bts->C_page);

#if !defined(NDEBUG)
  header = btree_get_node_header (bts->C_page);

  assert (header != NULL);
  assert (header->node_level == 1);	/* BTREE_LEAF_NODE */
#endif

  /*
   * If the next index record exists in the current leaf page,
   * the next index record(slot) and OID position can be identified easily.
   */
  if (key_cnt > 0)
    {
      if (bts->use_desc_index)
	{
	  if (bts->slot_id > 1)
	    {
	      bts->slot_id--;
	      bts->oid_pos = 0;
	      goto end;		/* OK */
	    }
	}
      else
	{
	  if (bts->slot_id < key_cnt)
	    {

	      bts->slot_id++;
	      bts->oid_pos = 0;
	      goto end;		/* OK */
	    }
	}
    }

  while (bts->C_page != NULL)
    {
      header = btree_get_node_header (bts->C_page);
      if (header == NULL)
	{
	  if (first_page != bts->P_page)
	    {
	      pgbuf_unfix_and_init (thread_p, bts->P_page);
	    }

	  goto exit_on_error;
	}

      if (bts->use_desc_index)
	{
	  bts->C_vpid = header->prev_vpid;
	  latch_condition = PGBUF_CONDITIONAL_LATCH;
	}
      else
	{
	  bts->C_vpid = header->next_vpid;
	  latch_condition = PGBUF_UNCONDITIONAL_LATCH;
	}

      bts->P_page = bts->C_page;
      bts->C_page = NULL;

      if (!VPID_ISNULL (&(bts->C_vpid)))
	{
	  bts->C_page = pgbuf_fix (thread_p, &bts->C_vpid, OLD_PAGE,
				   PGBUF_LATCH_READ, latch_condition);
	  if (bts->C_page == NULL)
	    {
	      if (bts->use_desc_index)
		{
		  assert (latch_condition == PGBUF_CONDITIONAL_LATCH);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_DESC_ISCAN_ABORTED, 3,
			  bts->btid_int.sys_btid->vfid.volid,
			  bts->btid_int.sys_btid->vfid.fileid,
			  bts->btid_int.sys_btid->root_pageid);
		}

	      if (first_page != bts->P_page)
		{
		  pgbuf_unfix_and_init (thread_p, bts->P_page);
		}

	      goto exit_on_error;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, bts->C_page, PAGE_BTREE);

	  /* unfix the previous leaf page */
	  assert (bts->P_page != NULL);

	  if (first_page != bts->P_page)
	    {
	      pgbuf_unfix_and_init (thread_p, bts->P_page);
	    }

	  /* do not clear bts->P_vpid for UNCONDITIONAL lock request handling */

	  key_cnt = btree_node_number_of_keys (bts->C_page);

	  if (key_cnt > 0)
	    {
	      if (bts->use_desc_index)
		{
		  bts->slot_id = key_cnt;
		  bts->oid_pos = 0;
		}
	      else
		{
		  bts->slot_id = 1;
		  bts->oid_pos = 0;
		}

	      goto end;		/* OK */
	    }
	}
      else
	{
	  if (first_page != bts->P_page)
	    {
	      pgbuf_unfix_and_init (thread_p, bts->P_page);
	    }
	}
    }

end:
  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_apply_key_range_and_filter () - Apply key range and key filter condition
 *   return: NO_ERROR
 *   bts(in)	: pointer to B+-tree scan structure
 *   is_iss(in) : true if this is an index skip scan
 *   is_key_range_satisfied(out): true, or false
 *   is_key_filter_satisfied(out): true, or false
 *
 * Note: This function applies key range condition and key filter condition
 * to the current key value saved in B+-tree scan structure.
 * The results of the evaluation of the given conditions are
 * returned throught key_range_satisfied and key_filter_satisfied.
 */
static int
btree_apply_key_range_and_filter (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
				  bool is_iss,
				  bool * is_key_range_satisfied,
				  bool * is_key_filter_satisfied,
				  bool need_to_check_null)
{
  int c;			/* comparison result */
  DB_LOGICAL ev_res;		/* evaluation result */
  DB_MIDXKEY *mkey;		/* midxkey ptr */
  DB_VALUE ep;			/* element ptr */
  bool allow_null_in_midxkey = false;
  DB_TYPE type;
  int ret = NO_ERROR;

  *is_key_range_satisfied = *is_key_filter_satisfied = false;
  bts->key_range_max_value_equal = false;	/* init as false */

  /* Key Range Checking */
  if (bts->key_range.upper_key == NULL)
    {
      c = DB_GT;
    }
  else
    {
      c = btree_compare_key (bts->key_range.upper_key, &bts->cur_key,
			     bts->btid_int.key_type, 1, 1, NULL);

      if (c == DB_UNK)
	{
	  /* error should have been set */
	  goto exit_on_error;
	}

      /* when using descending index the comparison should be changed again */
      if (bts->use_desc_index)
	{
	  c = -c;
	}
    }

  if (c < 0)
    {
      *is_key_range_satisfied = false;
    }
  else if (c == 0)
    {
      if (bts->key_range.range == GT_LE
	  || bts->key_range.range == GE_LE || bts->key_range.range == INF_LE)
	{
	  *is_key_range_satisfied = true;
	  bts->key_range_max_value_equal = true;
	}
      else
	{
	  *is_key_range_satisfied = false;
	}
    }
  else
    {
      *is_key_range_satisfied = true;
    }

  if (*is_key_range_satisfied)
    {
      if (need_to_check_null
	  && DB_VALUE_DOMAIN_TYPE (&bts->cur_key) == DB_TYPE_MIDXKEY
	  && bts->key_range.num_index_term > 0)
	{
	  mkey = DB_PULL_MIDXKEY (&(bts->cur_key));
	  /* get the last element from key range elements */
	  ret = pr_midxkey_get_element_nocopy (mkey,
					       bts->key_range.num_index_term -
					       1, &ep, NULL, NULL);
	  if (ret != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  if (DB_IS_NULL (&ep))
	    {
	      bool is_desc = false;

	      allow_null_in_midxkey = false;	/* init */

	      assert_release (bts->key_range.num_index_term == 1);

	      if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
		{
		  if (ep.need_clear)
		    {		/* need to check */
		      type = DB_VALUE_DOMAIN_TYPE (&ep);
		      if (QSTR_IS_ANY_CHAR_OR_BIT (type)
			  && ep.data.ch.medium.buf != NULL)
			{
			  allow_null_in_midxkey = true;	/* is Empty-string */
			}
		    }
		}

	      is_desc = (bts->use_desc_index ? true : false);
	      if (bts->btid_int.key_type && bts->btid_int.key_type->setdomain
		  && bts->btid_int.key_type->setdomain->is_desc)
		{
		  is_desc = !is_desc;
		}

	      if (is_iss && is_desc && bts->key_range.num_index_term == 1)
		{
		  /* We're inside an INDEX SKIP SCAN doing a descending scan. We
		   * allow the first term of a MIDXKEY to be NULL since ISS has
		   * to return the results for which the first column of
		   * the index is NULL.
		   */
		  allow_null_in_midxkey = true;
		}
	      if (!allow_null_in_midxkey)
		{
		  *is_key_filter_satisfied = false;
		  goto end;	/* give up */
		}
	    }
	}

      /*
       * Only in case that key_range_satisfied is true,
       * the key filter can be applied to the current key value.
       */
      *is_key_filter_satisfied = true;
      if (bts->key_filter && bts->key_filter->scan_pred->regu_list)
	{
	  ev_res = eval_key_filter (thread_p, &bts->cur_key, bts->key_filter);
	  if (ev_res != V_TRUE)
	    {
	      *is_key_filter_satisfied = false;
	    }

	  if (ev_res == V_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

end:
  assert ((*is_key_range_satisfied == false
	   && *is_key_filter_satisfied == false)
	  || (*is_key_range_satisfied == true
	      && *is_key_filter_satisfied == false)
	  || (*is_key_range_satisfied == true
	      && *is_key_filter_satisfied == true));

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * btree_attrinfo_read_dbvalues () -
 *      Find db_values of desired attributes of given key
 *
 *   curr_key(in): the current key
 *   btree_att_ids(in): the btree attributes ids
 *   btree_num_att(in): the btree attributes count
 *   attr_info(in/out): The attribute information structure which describe the
 *                      desired attributes
 *
 * Note: Find DB_VALUES of desired attributes of given key.
 * The attr_info structure must have already been initialized
 * with the desired attributes.
 */
int
btree_attrinfo_read_dbvalues (THREAD_ENTRY * thread_p,
			      DB_VALUE * curr_key,
			      int *btree_att_ids, int btree_num_att,
			      HEAP_CACHE_ATTRINFO * attr_info,
			      int func_index_col_id)
{
  int i, j, error = NO_ERROR;
  HEAP_ATTRVALUE *attr_value;
  bool found;

  if (curr_key == NULL || btree_att_ids == NULL || btree_num_att < 0
      || attr_info == NULL)
    {
      return ER_FAILED;
    }

  if (DB_VALUE_TYPE (curr_key) != DB_TYPE_MIDXKEY)
    {
      if (attr_info->num_values != 1 || btree_num_att != 1
	  || attr_info->values->attrid != btree_att_ids[0])
	{
	  return ER_FAILED;
	}

      if (pr_clear_value (&(attr_info->values->dbvalue)) != NO_ERROR)
	{
	  attr_info->values->state = HEAP_UNINIT_ATTRVALUE;
	  return ER_FAILED;
	}

      if (pr_clone_value (curr_key, &(attr_info->values->dbvalue)) !=
	  NO_ERROR)
	{
	  attr_info->values->state = HEAP_UNINIT_ATTRVALUE;
	  return ER_FAILED;
	}

      attr_info->values->state = HEAP_WRITTEN_ATTRVALUE;
    }
  else
    {
      attr_value = attr_info->values;
      for (i = 0; i < attr_info->num_values; i++)
	{
	  found = false;
	  for (j = 0; j < btree_num_att; j++)
	    {
	      if (attr_value->attrid == btree_att_ids[j])
		{
		  found = true;
		  break;
		}
	    }

	  if (found == false)
	    {
	      error = ER_FAILED;
	      goto error;
	    }

	  if (pr_clear_value (&(attr_value->dbvalue)) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto error;
	    }

	  if (func_index_col_id != -1)
	    {
	      /* consider that in the midxkey resides the function result,
	       * which must be skipped if we are interested in attributes
	       */
	      if (j >= func_index_col_id)
		{
		  j++;
		}
	    }
	  if (pr_midxkey_get_element_nocopy (DB_GET_MIDXKEY (curr_key), j,
					     &(attr_value->dbvalue),
					     NULL, NULL) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto error;
	    }

	  attr_value->state = HEAP_WRITTEN_ATTRVALUE;
	  attr_value++;
	}
    }

  return NO_ERROR;

error:

  attr_value = attr_info->values;
  for (i = 0; i < attr_info->num_values; i++)
    {
      attr_value->state = HEAP_UNINIT_ATTRVALUE;
    }

  return error;
}

/*
 * btree_dump_curr_key () -
 *      Dump the current key
 *
 *   bts(in): pointer to B+-tree scan structure
 *   filter(in): key filter
 *   oid(in): the current oid
 *   iscan_id(in): index scan id
 */
static int
btree_dump_curr_key (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
		     FILTER_INFO * filter, OID * oid, INDX_SCAN_ID * iscan_id)
{
  HEAP_CACHE_ATTRINFO *attr_info;
  REGU_VARIABLE_LIST regu_list;
  int error;

  if (bts == NULL || iscan_id == NULL
      || iscan_id->indx_cov.list_id == NULL
      || iscan_id->indx_cov.val_descr == NULL
      || iscan_id->indx_cov.output_val_list == NULL
      || iscan_id->indx_cov.tplrec == NULL)
    {
      return ER_FAILED;
    }

  if (iscan_id->rest_attrs.num_attrs > 0)
    {
      /* normal index scan or join index scan */
      attr_info = iscan_id->rest_attrs.attr_cache;
      regu_list = iscan_id->rest_regu_list;
    }
  else if (iscan_id->pred_attrs.num_attrs > 0)
    {
      /* rest_attrs.num_attrs == 0 if index scan term is
       * join index scan with always-true condition.
       * example: SELECT ... FROM X inner join Y on 1 = 1;
       */
      attr_info = iscan_id->pred_attrs.attr_cache;
      regu_list = iscan_id->scan_pred.regu_list;
    }
  else
    {
      assert_release (false);
      attr_info = NULL;
      regu_list = NULL;
    }

  error = btree_attrinfo_read_dbvalues (thread_p, &(bts->cur_key),
					filter->btree_attr_ids,
					filter->btree_num_attrs, attr_info,
					iscan_id->indx_cov.func_index_col_id);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = fetch_val_list (thread_p, regu_list,
			  iscan_id->indx_cov.val_descr, NULL, oid, NULL,
			  PEEK);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = qexec_insert_tuple_into_list (thread_p, iscan_id->indx_cov.list_id,
					iscan_id->indx_cov.output_val_list,
					iscan_id->indx_cov.val_descr,
					iscan_id->indx_cov.tplrec);
  if (error != NO_ERROR)
    {
      return error;
    }

  return NO_ERROR;
}

/*
 * btree_get_next_key_info () - Advance to next key in b-tree and obtain
 *				information.
 *
 * return		: Scan code.
 * thread_p (in)	: Thread entry.
 * btid (in)		: B-tree identifier.
 * bts (in)		: B-tree scan.
 * num_classes (in)	: Number of class in class_oid_ptr.
 * class_oids_ptr (in)	: Class Object identifiers.
 * index_scan_id_p (in) : Index scan data.
 * key_info (out)	: Array of value pointers to store key information.
 *
 * TODO: Handle unique on hierarchy indexes.
 */
SCAN_CODE
btree_get_next_key_info (THREAD_ENTRY * thread_p, BTID * btid,
			 BTREE_SCAN * bts, int num_classes,
			 OID * class_oids_ptr, INDX_SCAN_ID * index_scan_id_p,
			 DB_VALUE ** key_info)
{
  int error_code = NO_ERROR;
  SCAN_CODE result = S_SUCCESS;
  int oid_size;
  OID class_oid, oid;
  BTREE_SEARCH search_result = BTREE_KEY_NOTFOUND;

#if defined(BTREE_DEBUG)
  if (BTREE_INVALID_INDEX_ID (btid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BTREE_INVALID_INDEX_ID,
	      3, btid->vfid.fileid, btid->vfid.volid, btid->root_pageid);
      return -1;
    }
#endif /* BTREE_DEBUG */

  OID_SET_NULL (&class_oid);

  /* initialize key filter */
  bts->key_filter = NULL;

  /* copy use desc index information in the BTS to have it available in
   * the b-tree functions.
   */
  if (index_scan_id_p->indx_info)
    {
      bts->use_desc_index = index_scan_id_p->indx_info->use_desc_index;
    }
  else
    {
      bts->use_desc_index = 0;
    }

  if (bts->C_vpid.pageid == NULL_PAGEID)
    {
      /* first btree_get_next_key_info call, initialize bts */
      error_code =
	btree_prepare_bts (thread_p, bts, btid, index_scan_id_p, NULL, NULL,
			   &oid_Null_oid, NULL, NULL, false, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      error_code = btree_range_scan_start (thread_p, bts);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      /* search is positioned on the first key */
    }
  else
    {
      /* resume search */
      mnt_bt_resumes (thread_p);

      error_code = btree_range_scan_resume (thread_p, bts);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }

  if (bts->end_scan)
    {
      /* Reached the end of leaf level */
      result = S_END;
      goto end;
    }

  oid_size =
    BTREE_IS_UNIQUE (bts->btid_int.unique_pk) ? 2 * OR_OID_SIZE : OR_OID_SIZE;

  /* C_page should be already loaded */
  assert (bts->C_page != NULL);

  /* TODO: Fill the rest of key information here */
  /* TODO: Do we have to get all oids or should we just count them ?
   * Or maybe select only the first OID? Or a maximum number of OIDs...
   */

  DB_MAKE_INT (key_info[BTREE_KEY_INFO_VOLUMEID], bts->C_vpid.volid);
  DB_MAKE_INT (key_info[BTREE_KEY_INFO_PAGEID], bts->C_vpid.pageid);
  DB_MAKE_INT (key_info[BTREE_KEY_INFO_SLOTID], bts->slot_id);

  /* Get key */
  db_value_clear (key_info[BTREE_KEY_INFO_KEY]);
  db_value_clone (&bts->cur_key, key_info[BTREE_KEY_INFO_KEY]);

  /* Get overflow key and overflow oids */
  db_value_clear (key_info[BTREE_KEY_INFO_OVERFLOW_KEY]);
  DB_MAKE_STRING (key_info[BTREE_KEY_INFO_OVERFLOW_KEY],
		  btree_leaf_is_flaged (&bts->key_record,
					BTREE_LEAF_RECORD_OVERFLOW_KEY) ?
		  "true" : "false");
  db_value_clear (key_info[BTREE_KEY_INFO_OVERFLOW_OIDS]);
  DB_MAKE_STRING (key_info[BTREE_KEY_INFO_OVERFLOW_OIDS],
		  btree_leaf_is_flaged (&bts->key_record,
					BTREE_LEAF_RECORD_OVERFLOW_OIDS) ?
		  "true" : "false");

  /* Get OIDs count -> For now ignore the overflow OIDs */
  DB_MAKE_INT (key_info[BTREE_KEY_INFO_OID_COUNT],
	       btree_record_get_num_oids (thread_p, &bts->btid_int,
					  &bts->key_record, bts->offset,
					  BTREE_LEAF_NODE));

  /* Get OIDs -> For now just the first OID */
  search_result =
    btree_key_find_first_visible_row (thread_p, &bts->btid_int,
				      &bts->key_record, bts->offset,
				      BTREE_LEAF_NODE, &oid, &class_oid, -1);
  if (search_result == BTREE_KEY_NOTFOUND)
    {
      if (!VPID_ISNULL (&(bts->leaf_rec_info.ovfl)))
	{
	  /* search for visible OID into OID overflow page */
	  search_result =
	    btree_key_find_first_visible_row_from_all_ovf
	    (thread_p, &bts->btid_int, &(bts->leaf_rec_info.ovfl), &oid,
	     &class_oid);
	  if (search_result == BTREE_KEY_NOTFOUND)
	    {
	      OID_SET_NULL (&oid);
	    }
	}
      else
	{
	  OID_SET_NULL (&oid);
	}
    }
  DB_MAKE_OID (key_info[BTREE_KEY_INFO_FIRST_OID], &oid);

  /* Key was consumed. */
  bts->key_status = BTS_KEY_IS_CONSUMED;

end:
  if (bts->C_page != NULL)
    {
      LSA_COPY (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page));
      pgbuf_unfix_and_init (thread_p, bts->C_page);
    }

  if (bts->O_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, bts->C_page);
    }

  if (bts->P_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, bts->P_page);
    }

  if (result == S_END || result == S_ERROR)
    {
      btree_scan_clear_key (bts);
    }

  if (result == S_END)
    {
      VPID_SET_NULL (&bts->C_vpid);
    }

  return result;

error:
  result = S_ERROR;
  goto end;
}

/*
 * btree_find_min_or_max_key () -
 *   return: NO_ERROR
 *   btid(in):
 *   key(in):
 *   find_min_key(in):
 */
int
btree_find_min_or_max_key (THREAD_ENTRY * thread_p, BTID * btid,
			   DB_VALUE * key, int find_min_key)
{
  VPID root_vpid;
  PAGE_PTR root_page_ptr = NULL;
  int offset;
  bool clear_key = false;
  DB_VALUE key_value;
  BTREE_ROOT_HEADER *root_header = NULL;
  RECDES rec;
  LEAF_REC leaf_pnt;
  BTREE_SCAN btree_scan, *BTS;
  int ret = NO_ERROR;

  if (key == NULL)
    {
      return NO_ERROR;
    }

  DB_MAKE_NULL (key);

  BTS = &btree_scan;
  BTREE_INIT_SCAN (BTS);

  BTS->btid_int.sys_btid = btid;

  root_vpid.pageid = btid->root_pageid;
  root_vpid.volid = btid->vfid.volid;

  root_page_ptr = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
  if (root_page_ptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, root_page_ptr, PAGE_BTREE);

  root_header = btree_get_root_header (root_page_ptr);
  if (root_header == NULL)
    {
      goto exit_on_error;
    }

  ret = btree_glean_root_header_info (thread_p, root_header, &BTS->btid_int);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  pgbuf_unfix_and_init (thread_p, root_page_ptr);

  assert (tp_valid_indextype (TP_DOMAIN_TYPE (BTS->btid_int.key_type)));

  /*
   * in case of desc domain index,
   * we have to find the min/max key in opposite order.
   */
  if (BTS->btid_int.key_type->is_desc)
    {
      find_min_key = !find_min_key;
    }

  if (find_min_key)
    {
      BTS->use_desc_index = 0;
    }
  else
    {
      BTS->use_desc_index = 1;
    }

  ret = btree_find_lower_bound_leaf (thread_p, BTS, NULL);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (!BTREE_END_OF_SCAN (BTS))
    {
      assert (BTS->slot_id > 0);
      if (spage_get_record (BTS->C_page, BTS->slot_id, &rec, PEEK) !=
	  S_SUCCESS)
	{
	  goto exit_on_error;
	}

      if (btree_read_record (thread_p, &BTS->btid_int, BTS->C_page, &rec,
			     &key_value, (void *) &leaf_pnt, BTREE_LEAF_NODE,
			     &clear_key, &offset, PEEK_KEY_VALUE,
			     NULL) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      (void) pr_clone_value (&key_value, key);

      if (clear_key)
	{
	  pr_clear_value (&key_value);
	  clear_key = false;
	}
    }

end:

  if (BTS->P_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->P_page);
    }

  if (BTS->C_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->C_page);
    }

  if (BTS->O_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, BTS->O_page);
    }

  if (root_page_ptr)
    {
      pgbuf_unfix_and_init (thread_p, root_page_ptr);
    }

  if (clear_key)
    {
      pr_clear_value (&key_value);
      clear_key = false;
    }

  return ret;

exit_on_error:

  ret = (ret == NO_ERROR
	 && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;

  goto end;
}

/*
 * Recovery functions
 */

/*
 * btree_rv_util_save_page_records () - Save a set of page records
 *   return: int
 *   page_ptr(in): Page Pointer
 *   first_slotid(in): First Slot identifier to be saved
 *   rec_cnt(in): Number of slots to be saved
 *   ins_slotid(in): First Slot identifier to reinsert set of records
 *   data(in): Data area where the records will be stored
 *             (Enough space(DB_PAGESIZE) must have been allocated by caller
 *   length(in): Effective length of the data area after save is completed
 *
 * Note: Copy the set of records to designated data area.
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine
 */
int
btree_rv_util_save_page_records (PAGE_PTR page_ptr,
				 INT16 first_slotid,
				 int rec_cnt, INT16 ins_slotid,
				 char *data, int *length)
{
  RECDES rec;
  int i, offset, wasted;
  char *datap;
  int ret = NO_ERROR;

  *length = 0;
  datap = (char *) data + sizeof (RECSET_HEADER);
  offset = sizeof (RECSET_HEADER);
  wasted = DB_WASTED_ALIGN (offset, BTREE_MAX_ALIGN);
  datap += wasted;
  offset += wasted;

  for (i = 0; i < rec_cnt; i++)
    {
      assert (first_slotid + i > 0);
      if (spage_get_record (page_ptr, first_slotid + i, &rec, PEEK)
	  != S_SUCCESS)
	{
	  return ((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
	}

      *(INT16 *) datap = rec.length;
      datap += 2;
      offset += 2;

      *(INT16 *) datap = rec.type;
      datap += 2;
      offset += 2;

      memcpy (datap, rec.data, rec.length);
      datap += rec.length;
      offset += rec.length;
      wasted = DB_WASTED_ALIGN (offset, BTREE_MAX_ALIGN);
      datap += wasted;
      offset += wasted;
    }

  datap = data;
  ((RECSET_HEADER *) datap)->rec_cnt = rec_cnt;
  ((RECSET_HEADER *) datap)->first_slotid = ins_slotid;
  *length = offset;

  return NO_ERROR;
}

/*
 * btree_rv_save_keyval_for_undo () - Save a < key, value > pair and other
 *				      information for undo logical log
 *				      purposes.
 *
 * return	: Error code.
 * btid (in)	: B+tree index identifier.
 * key (in)	: Key to be saved.
 * cls_oid (in) : Class identifier.
 * oid (in)	: Object identifier.
 * mvcc_id (in) : MVCCID for operation (NULL if it is not an MVCC operation).
 * data (out)	: Data area where the above fields will be stored
 *		  (Note: The caller should FREE the allocated area.)
 * length (out) : Length of the data area after save is completed.
 *
 * Note: Copy the adequate key-value information to the data area and return
 *	 this data area.
 *	 The MVCCID is stored in buffer only if is not null. In this case, an
 *	 area at the beginning of recovery data si reserved for the log lsa
 *	 of previous MVCC operation (used by vacuum).
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine
 *
 * Warning: This routine assumes that the keyval is from a leaf page and not a
 *	    non-leaf page.  Because of this assumption, we use the index
 *	    domain and not the non-leaf domain to write out the key value.
 *	    Currently all calls to this routine are from leaf pages. Be
 *	    careful if you add a call to this routine.
 */
int
btree_rv_save_keyval_for_undo (BTID_INT * btid, DB_VALUE * key, OID * cls_oid,
			       OID * oid, BTREE_MVCC_INFO * mvcc_info,
			       BTREE_OP_PURPOSE purpose,
			       char *preallocated_buffer, char **data,
			       int *capacity, int *length)
{
  char *datap;
  int key_len;
  OR_BUF buf;
  PR_TYPE *pr_type;
  int ret = NO_ERROR;
  size_t size;
  OID oid_and_flags;

  assert (key != NULL);
  assert (cls_oid != NULL);
  assert (oid != NULL);

  *length = 0;

  key_len = (int) btree_get_disk_size_of_key (key);

  size = OR_BTID_ALIGNED_SIZE +	/* btid */
    BTREE_OBJECT_MAX_SIZE +	/* Object OID and all its info. */
    key_len +			/* key length */
    (2 * INT_ALIGNMENT);	/* extra space for alignment */

  /* Allocate enough memory to handle estimated size. */
  if (*data == NULL)
    {
      /* Initialize data */
      if (preallocated_buffer == NULL || *capacity < (int) size)
	{
	  /* No preallocated buffer or not enough capacity. */
	  *data = (char *) db_private_alloc (NULL, size);
	  if (*data == NULL)
	    {
	      ASSERT_ERROR_AND_SET (ret);
	      return ret;
	    }
	  *capacity = size;
	}
      else
	{
	  *data = preallocated_buffer;
	}
    }
  else if (*capacity < (int) size)
    {
      if (*data == preallocated_buffer)
	{
	  /* Allocate a new buffer. */
	  *data = (char *) db_private_alloc (NULL, size);
	  if (*data == NULL)
	    {
	      ASSERT_ERROR_AND_SET (ret);
	      return ret;
	    }
	  *capacity = size;
	}
      else
	{
	  /* Reallocate buffer. */
	  char *new_data = (char *) db_private_realloc (NULL, *data, size);
	  if (new_data == NULL)
	    {
	      ASSERT_ERROR_AND_SET (ret);
	      return ret;
	    }
	  *capacity = size;
	  *data = new_data;
	}
    }
  else
    {
      /* Current data buffer has enough space. */
    }

  /* Start packing recovery data. */
  datap = (char *) (*data);

  ASSERT_ALIGN (datap, INT_ALIGNMENT);

  datap = or_pack_btid (datap, btid->sys_btid);

  COPY_OID (&oid_and_flags, oid);

  /* Based on the purpose of recovery, some MVCC information may require
   * packing.
   */
  switch (purpose)
    {
    case BTREE_OP_NOTIFY_VACUUM:
      /* Pack delete MVCCID or insert MVCCID. Vacuum will then visit this
       * object and remove it or remove its insert MVCCID.
       */
      assert (mvcc_info != NULL);
      if (BTREE_MVCC_INFO_IS_DELID_VALID (mvcc_info))
	{
	  /* Save delete MVCCID. */
	  BTREE_OID_SET_MVCC_FLAG (&oid_and_flags, BTREE_OID_HAS_MVCC_DELID);
	}
      else
	{
	  /* Save insert MVCCID. */
	  assert (BTREE_MVCC_INFO_IS_INSID_NOT_ALL_VISIBLE (mvcc_info));
	  BTREE_OID_SET_MVCC_FLAG (&oid_and_flags, BTREE_OID_HAS_MVCC_INSID);
	}
      break;

    case BTREE_OP_DELETE_OBJECT_PHYSICAL:
      /* Object is being physically removed. Since on rollback we should
       * also recover MVCC information, it must be packed.
       */
      assert (mvcc_info != NULL);
      if (BTREE_MVCC_INFO_IS_INSID_NOT_ALL_VISIBLE (mvcc_info))
	{
	  /* Do not pack insert MVCCID if it is all visible. */
	  BTREE_OID_SET_MVCC_FLAG (&oid_and_flags, BTREE_OID_HAS_MVCC_INSID);
	}
      if (BTREE_MVCC_INFO_IS_DELID_VALID (mvcc_info))
	{
	  /* Do not pack delete MVCCID if it is NULL. */
	  BTREE_OID_SET_MVCC_FLAG (&oid_and_flags, BTREE_OID_HAS_MVCC_DELID);
	}
      break;

    default:
      /* No MVCC information needs packing. It doesn't exist or it can be
       * recovered in other ways.
       */
      break;
    }

  /* Pack class OID for unique indexes, if it is not the same with top class.
   * Undo function should know to treat null class OID as top class.
   */
  if (BTREE_IS_UNIQUE (btid->unique_pk)
      && !OID_EQ (cls_oid, &btid->topclass_oid))
    {
      BTREE_OID_SET_RECORD_FLAG (&oid_and_flags, BTREE_LEAF_RECORD_CLASS_OID);
    }

  /* Save OID and all flags. */
  OR_PUT_OID (datap, &oid_and_flags);
  datap += OR_OID_SIZE;

  if (BTREE_IS_UNIQUE (btid->unique_pk)
      && !OID_EQ (cls_oid, &btid->topclass_oid))
    {
      /* Save class OID. */
      OR_PUT_OID (datap, cls_oid);
      datap += OR_OID_SIZE;
    }

  /* Add required MVCC information. */
  if (BTREE_OID_IS_MVCC_FLAG_SET (&oid_and_flags, BTREE_OID_HAS_MVCC_INSID))
    {
      assert (mvcc_info != NULL
	      && MVCCID_IS_NOT_ALL_VISIBLE (mvcc_info->insert_mvccid));
      OR_PUT_MVCCID (datap, &mvcc_info->insert_mvccid);
      datap += OR_MVCCID_SIZE;
    }
  if (BTREE_OID_IS_MVCC_FLAG_SET (&oid_and_flags, BTREE_OID_HAS_MVCC_DELID))
    {
      assert (mvcc_info != NULL);
      assert (MVCCID_IS_VALID (mvcc_info->delete_mvccid));
      OR_PUT_MVCCID (datap, &mvcc_info->delete_mvccid);
      datap += OR_MVCCID_SIZE;
    }

  ASSERT_ALIGN (datap, INT_ALIGNMENT);

  /* Save key. */
  or_init (&buf, datap, key_len);
  pr_type = btid->key_type->type;
  ret = (*(pr_type->index_writeval)) (&buf, key);
  if (ret != NO_ERROR)
    {
      if (*data != preallocated_buffer)
	{
	  db_private_free_and_init (NULL, *data);
	}
      return ret;
    }
  datap += key_len;

  *length = CAST_BUFLEN (datap - *data);

  /* Safe guard. */
  assert (0 < *length);
  assert (*length <= (int) size);

  /* Success. */
  return NO_ERROR;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * btree_rv_util_dump_leafrec () -
 *   return: nothing
 *   btid(in):
 *   rec(in): Leaf Record
 *
 * Note: Dump a Tree Leaf Node Record
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine
 */
void
btree_rv_util_dump_leafrec (THREAD_ENTRY * thread_p, FILE * fp,
			    BTID_INT * btid, RECDES * rec)
{
  btree_dump_leaf_record (thread_p, fp, btid, rec, 2);
}

/*
 * btree_rv_util_dump_nleafrec () -
 *   return: nothing
 *   btid(in):
 *   rec(in): NonLeaf Record
 *
 * Note: Dump a Tree NonLeaf Node Record
 *
 * Note: This is a UTILITY routine, but not an actual recovery routine
 */
void
btree_rv_util_dump_nleafrec (THREAD_ENTRY * thread_p, FILE * fp,
			     BTID_INT * btid, RECDES * rec)
{
  btree_dump_non_leaf_record (thread_p, fp, btid, rec, 2, 1);
}
#endif


/*
 * btree_rv_mvcc_undo_redo_increments_update () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the in-memory unique statistics.
 */
int
btree_rv_mvcc_undo_redo_increments_update (THREAD_ENTRY * thread_p,
					   LOG_RCV * recv)
{
  char *datap;
  int num_nulls, num_oids, num_keys;
  BTID btid;

  assert (recv->length >= (3 * OR_INT_SIZE) + OR_BTID_ALIGNED_SIZE);

  /* unpack the root statistics */
  datap = (char *) recv->data;

  OR_GET_BTID (datap, &btid);
  datap += OR_BTID_ALIGNED_SIZE;

  num_keys = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  num_oids = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  num_nulls = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  if (logtb_tran_update_unique_stats
      (thread_p, &btid, num_keys, num_oids, num_nulls, false) != NO_ERROR)
    {
      goto error;
    }

  return NO_ERROR;

error:
  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_rv_roothdr_undo_update () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the root header statistics for undo purposes.
 */
int
btree_rv_roothdr_undo_update (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  char *datap;
  BTREE_ROOT_HEADER *root_header = NULL;

  if (recv->length < 3 * OR_INT_SIZE)
    {
      assert (false);
      goto error;
    }

  root_header = btree_get_root_header (recv->pgptr);
  assert (root_header != NULL);

  if (root_header != NULL)
    {
      /* unpack the root statistics */
      datap = (char *) recv->data;
      root_header->num_nulls += OR_GET_INT (datap);
      datap += OR_INT_SIZE;
      root_header->num_oids += OR_GET_INT (datap);
      datap += OR_INT_SIZE;
      root_header->num_keys += OR_GET_INT (datap);
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;

error:

  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_rv_roothdr_dump () -
 *   return:
 *   length(in):
 *   data(in):
 *
 * Note: Dump the root header statistics recovery information.
 */
void
btree_rv_roothdr_dump (FILE * fp, int length, void *data)
{
  char *datap;
  int max_key_len, null_delta, oid_delta, key_delta;

  /* unpack the root statistics */
  datap = (char *) data;
  max_key_len = OR_GET_INT (datap);
  datap += OR_INT_SIZE;
  null_delta = OR_GET_INT (datap);
  datap += OR_INT_SIZE;
  oid_delta = OR_GET_INT (datap);
  datap += OR_INT_SIZE;
  key_delta = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  fprintf (fp,
	   "\nMAX_KEY_LEN: %d NUM NULLS DELTA: %d"
	   " NUM OIDS DELTA: %d NUM KEYS DELTA: %d\n\n",
	   max_key_len, null_delta, oid_delta, key_delta);
}

/*
 * btree_rv_ovfid_undoredo_update () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the overflow VFID in the root header
 */
int
btree_rv_ovfid_undoredo_update (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  VFID ovfid;
  BTREE_ROOT_HEADER *root_header = NULL;

  if (recv->length < (int) sizeof (VFID))
    {
      assert (false);
      goto error;
    }

  root_header = btree_get_root_header (recv->pgptr);
  assert (root_header != NULL);

  if (root_header != NULL)
    {
      ovfid = *((VFID *) recv->data);	/* structure copy */
      root_header->ovfid = ovfid;
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;

error:

  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_rv_ovfid_dump () -
 *   return:
 *   length(in):
 *   data(in):
 *
 * Note: Dump the overflow VFID for the root header.
 */
void
btree_rv_ovfid_dump (FILE * fp, int length, void *data)
{
  VFID ovfid;

  ovfid = *((VFID *) data);	/* structure copy */

  fprintf (fp,
	   "\nOverflow key file VFID: %d|%d\n\n", ovfid.fileid, ovfid.volid);
}

/*
 * btree_rv_nodehdr_undoredo_update () - Recover an update to a node header. used either for
 *                         undo or redo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the update to a node header
 */
int
btree_rv_nodehdr_undoredo_update (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES rec;
#if !defined(NDEBUG)
  RECDES peek_rec;
#endif
  int sp_success;

  rec.area_size = rec.length = recv->length;
  rec.type = REC_HOME;
  rec.data = (char *) recv->data;

#if !defined(NDEBUG)
  if (spage_get_record (recv->pgptr, HEADER, &peek_rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  assert (rec.length == peek_rec.length);
#endif

  sp_success = spage_update (thread_p, recv->pgptr, HEADER, &rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_GENERIC_ERROR, 0);
	}
      ASSERT_ERROR ();
      assert (false);
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_nodehdr_redo_insert () - Recover a node header insertion. used for redo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a node header insertion by reinserting the node header
 * for redo purposes.
 */
int
btree_rv_nodehdr_redo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES rec;
  int sp_success;

  rec.area_size = rec.length = recv->length;
  rec.type = REC_HOME;
  rec.data = (char *) recv->data;
  sp_success = spage_insert_at (thread_p, recv->pgptr, HEADER, &rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_GENERIC_ERROR, 0);
	}
      ASSERT_ERROR ();
      assert (false);
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_nodehdr_undo_insert () - Recover a node header insertion. used for undo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a node header insertion by deletion  the node header
 * for undo purposes.
 */
int
btree_rv_nodehdr_undo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  PGSLOTID pg_slotid;

  pg_slotid = spage_delete (thread_p, recv->pgptr, HEADER);

  assert (pg_slotid != NULL_SLOTID);

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_noderec_undoredo_update () - Recover an update to a node record. used either
 *                         for undo or redo
 *   return:
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the update to a node record
 */
int
btree_rv_noderec_undoredo_update (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES rec;
  INT16 slotid;
  int sp_success;

  slotid = recv->offset;
  rec.type = *(INT16 *) ((char *) recv->data + OFFS2);
  rec.area_size = rec.length = recv->length - OFFS3;
  rec.data = (char *) (recv->data) + OFFS3;

  assert (slotid > 0);
  sp_success = spage_update (thread_p, recv->pgptr, slotid, &rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_GENERIC_ERROR, 0);
	}
      ASSERT_ERROR ();
      assert (false);
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_noderec_redo_insert () - Recover a node record insertion. used for redo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a node record insertion by reinserting the record for
 * redo purposes
 */
int
btree_rv_noderec_redo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES rec;
  INT16 slotid;
  int sp_success;

  slotid = recv->offset;
  rec.type = *(INT16 *) ((char *) recv->data + OFFS2);
  rec.area_size = rec.length = recv->length - OFFS3;
  rec.data = (char *) (recv->data) + OFFS3;

  assert (slotid > 0);
  sp_success = spage_insert_at (thread_p, recv->pgptr, slotid, &rec);
  if (sp_success != SP_SUCCESS)
    {
      if (sp_success != SP_ERROR)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_GENERIC_ERROR, 0);
	}
      ASSERT_ERROR ();
      assert (false);
      return er_errid ();
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_noderec_undo_insert () - Recover a node record insertion. used for undo
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover a node record insertion by deleting the record for
 * undo purposes
 */
int
btree_rv_noderec_undo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  INT16 slotid;
  PGSLOTID pg_slotid;

  slotid = recv->offset;
  assert (slotid > 0);
  pg_slotid = spage_delete_for_recovery (thread_p, recv->pgptr, slotid);

  assert (pg_slotid != NULL_SLOTID);

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_noderec_dump () - Dump node record recovery information
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump node record recovery information
 */
void
btree_rv_noderec_dump (FILE * fp, int length, void *data)
{
#if 0
  /* This needs to be fixed.  The easiest way is for the btid to be packed and
   * sent, but this increases the log record.  We may want to allow this
   * routine to know the layout of a node record.  TODO: ???
   */

  int Node_Type;
  RECDES rec;

  Node_Type = *(INT16 *) ((char *) data + OFFS1);
  rec.type = *(INT16 *) ((char *) data + OFFS2);
  rec.area_size = DB_PAGESIZE;
  rec.data = (char *) malloc (DB_PAGESIZE);
  memcpy (rec.data, (char *) data + OFFS3, rec.length);

  if (Node_Type == 0)
    {
      btree_rv_util_dump_leafrec (fp, btid, &rec);
    }
  else
    {
      btree_rv_util_dump_nleafrec (fp, btid, &rec);
    }

  free_and_init (rec.data);
#endif
}

/*
 * btree_rv_noderec_dump_slot_id () -
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump the slot id for the slot to be deleted for undo purposes
 */

void
btree_rv_noderec_dump_slot_id (FILE * fp, int length, void *data)
{
  fprintf (fp, " Slot_id: %d \n", *(INT16 *) data);
}

/*
 * btree_rv_pagerec_insert () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Put a set of records to the page
 */
int
btree_rv_pagerec_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECDES rec;
  RECSET_HEADER *recset_header;
  char *datap;
  int i, offset, wasted;
  int sp_success;

  /* initialization */
  recset_header = (RECSET_HEADER *) recv->data;

  /* insert back saved records */
  datap = (char *) recv->data + sizeof (RECSET_HEADER);
  offset = sizeof (RECSET_HEADER);
  wasted = DB_WASTED_ALIGN (offset, BTREE_MAX_ALIGN);
  datap += wasted;
  offset += wasted;
  for (i = 0; i < recset_header->rec_cnt; i++)
    {
      rec.area_size = rec.length = *(INT16 *) datap;
      datap += 2;
      offset += 2;
      rec.type = *(INT16 *) datap;
      datap += 2;
      offset += 2;
      rec.data = datap;
      datap += rec.length;
      offset += rec.length;
      wasted = DB_WASTED_ALIGN (offset, BTREE_MAX_ALIGN);
      datap += wasted;
      offset += wasted;

      assert (recset_header->first_slotid + i > 0);
      sp_success = spage_insert_at (thread_p, recv->pgptr,
				    recset_header->first_slotid + i, &rec);
      if (sp_success != SP_SUCCESS)
	{
	  if (sp_success != SP_ERROR)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	  assert (false);
	  goto error;
	}			/* if */
    }				/* for */

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;

error:

  ASSERT_ERROR ();
  return er_errid ();
}

/*
 * btree_rv_pagerec_delete () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Delete a set of records from the page for undo or redo purpose
 */
int
btree_rv_pagerec_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  RECSET_HEADER *recset_header;
  int i;

  recset_header = (RECSET_HEADER *) recv->data;

  /* delete all specified records from the page */
  for (i = 0; i < recset_header->rec_cnt; i++)
    {
      assert (recset_header->first_slotid > 0);
      if (spage_delete (thread_p, recv->pgptr,
			recset_header->first_slotid)
	  != recset_header->first_slotid)
	{
	  ASSERT_ERROR ();
	  assert (false);
	  return er_errid ();
	}
    }

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_newpage_redo_init () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Initialize a B+tree page.
 */
int
btree_rv_newpage_redo_init (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  unsigned short alignment;

  (void) pgbuf_set_page_ptype (thread_p, recv->pgptr, PAGE_BTREE);

  alignment = *(unsigned short *) recv->data;
  spage_initialize (thread_p, recv->pgptr,
		    UNANCHORED_KEEP_SEQUENCE, alignment,
		    DONT_SAFEGUARD_RVSPACE);

  return NO_ERROR;
}

/*
 * btree_rv_newpage_undo_alloc () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Undo a new page allocation
 */
int
btree_rv_newpage_undo_alloc (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  PAGEID_STRUCT *pageid_struct;
  int ret = NO_ERROR;

  pageid_struct = (PAGEID_STRUCT *) recv->data;

  ret =
    file_dealloc_page (thread_p, &pageid_struct->vfid, &pageid_struct->vpid);

  assert (ret == NO_ERROR);

  return NO_ERROR;
}

/*
 * btree_rv_newpage_dump_undo_alloc () -
 *   return: int
 *   length(in): Length of Recovery Data
 *   data(in): The data being logged
 *
 * Note: Dump undo information of new page creation
 */
void
btree_rv_newpage_dump_undo_alloc (FILE * fp, int length, void *data)
{
  PAGEID_STRUCT *pageid_struct = (PAGEID_STRUCT *) data;

  fprintf (fp,
	   "Deallocating page from Volid = %d, Fileid = %d\n",
	   pageid_struct->vfid.volid, pageid_struct->vfid.fileid);
}

/*
 * btree_rv_read_keyval_info_nocopy () - Recover key value and other
 *					 information on b-tree operation.
 *
 * return	    : Void.
 * thread_p (in)    : Thread entry.
 * datap (in)	    : Buffer containing recovery data.
 * data_size (in)   : Size of recovery data.
 * btid (out)	    : B-tree identifier.
 * cls_oid (out)    : Class identifier.
 * oid (out)	    : Object identifier.
 * mvcc_id (in/out) : Operation MVCCID. It must be NULL for non-MVCC
 *		      operations and not NULL for MVCC operations.
 * key (out)	    : Key value.
 *
 * Note: If it is an MVCC operation recovery (mvcc_id is not NULL), data will
 *	 start with a log lsa (of a previous MVCC operation in log), which
 *	 is used my vacuum only and must be skipped.
 *
 * Warning: This assumes that the key value has the index's domain and not the
 *	    non-leaf domain. This should be the case since this is a logical
 *	    operation and not a physical one.
 */
int
btree_rv_read_keyval_info_nocopy (THREAD_ENTRY * thread_p, char *datap,
				  int data_size, BTID_INT * btid,
				  OID * cls_oid, OID * oid,
				  BTREE_MVCC_INFO * mvcc_info, DB_VALUE * key)
{
  OR_BUF buf;
  PR_TYPE *pr_type;
  VPID root_vpid;
  PAGE_PTR root = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  int key_size = -1;
  DB_TYPE key_type = DB_TYPE_UNKNOWN;
  char *start = datap;
  DISK_ISVALID valid = DISK_ERROR;
  int error_code = NO_ERROR;

  assert (mvcc_info != NULL);

  btree_rv_read_keybuf_nocopy (thread_p, datap, data_size, btid, cls_oid, oid,
			       mvcc_info, &buf);

  root_vpid.pageid = btid->sys_btid->root_pageid;	/* read root page */
  root_vpid.volid = btid->sys_btid->vfid.volid;

  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE,
		    PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto error;
    }

  (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

  root_header = btree_get_root_header (root);
  if (root_header == NULL)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto error;
    }

  error_code = btree_glean_root_header_info (thread_p, root_header, btid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }

  pgbuf_unfix_and_init (thread_p, root);

  pr_type = btid->key_type->type;

  /* Do not copy the string--just use the pointer.  The pr_ routines
   * for strings and sets have different semantics for length.
   */
  if (pr_type->id == DB_TYPE_MIDXKEY)
    {
      key_size = CAST_BUFLEN (buf.endptr - buf.ptr);
    }

  error_code =
    (*(pr_type->index_readval)) (&buf, key, btid->key_type, key_size,
				 false /* not copy */ ,
				 NULL, 0);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }

  return NO_ERROR;

error:

  assert (error_code != NO_ERROR);
  if (root != NULL)
    {
      pgbuf_unfix_and_init (thread_p, root);
    }
  return error_code;
}

/*
 * btree_rv_read_keybuf_nocopy () - Initializes a buffer from recovery info
 *
 * return	    : Void.
 * thread_p (in)    : Thread entry.
 * datap (in)	    : Buffer containing recovery data.
 * data_size (in)   : Size of recovery data.
 * btid (out)	    : B-tree identifier.
 * cls_oid (out)    : Class identifier.
 * oid (out)	    : Object identifier.
 * mvcc_id (in/out) : Operation MVCCID. It must be NULL for non-MVCC
 *		      operations and not NULL for MVCC operations.
 * key_buf (out)    : buffer for packed key.
 *
 * Note: this should be prefered to btree_rv_read_keyval_info_nocopy
 *	 which performs an additional root page latch to retrieve key type.
 *	 Use this, if key type is already available.
 *
 * Warning: This assumes that the key value has the index's domain and not the
 *	    non-leaf domain. This should be the case since this is a logical
 *	    operation and not a physical one.
 */
void
btree_rv_read_keybuf_nocopy (THREAD_ENTRY * thread_p, char *datap,
			     int data_size, BTID_INT * btid,
			     OID * cls_oid, OID * oid,
			     BTREE_MVCC_INFO * mvcc_info, OR_BUF * key_buf)
{
  char *start = datap;

  assert (mvcc_info != NULL);

  /* extract the stored btid, key, oid data */
  datap = or_unpack_btid (datap, btid->sys_btid);

  OR_GET_OID (datap, oid);
  datap += OR_OID_SIZE;

  if (BTREE_OID_IS_RECORD_FLAG_SET (oid, BTREE_LEAF_RECORD_CLASS_OID))
    {
      /* Read class OID from record. */
      OR_GET_OID (datap, cls_oid);
      datap += OR_OID_SIZE;
    }
  else
    {
      /* Set class OID NULL. */
      OID_SET_NULL (cls_oid);
    }

  mvcc_info->flags = BTREE_OID_GET_MVCC_FLAGS (oid);
  if (mvcc_info->flags & BTREE_OID_HAS_MVCC_INSID)
    {
      OR_GET_MVCCID (datap, &mvcc_info->insert_mvccid);
      datap += OR_MVCCID_SIZE;
    }
  else
    {
      mvcc_info->insert_mvccid = MVCCID_ALL_VISIBLE;
    }
  if (mvcc_info->flags & BTREE_OID_HAS_MVCC_DELID)
    {
      OR_GET_MVCCID (datap, &mvcc_info->delete_mvccid);
      datap += OR_MVCCID_SIZE;
    }
  else
    {
      mvcc_info->delete_mvccid = MVCCID_NULL;
    }

  BTREE_OID_CLEAR_ALL_FLAGS (oid);

  datap = PTR_ALIGN (datap, INT_ALIGNMENT);
  or_init (key_buf, datap, (data_size - CAST_BUFLEN (datap - start)));
}

/*
 * btree_rv_keyval_undo_insert () - Undo insert operation.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * recv (in)		  : Recovery data.
 */
int
btree_rv_keyval_undo_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  BTID_INT btid;
  BTID sys_btid;
  OR_BUF key_buf;
  OID cls_oid;
  OID oid;
  char *datap;
  int datasize;
  BTREE_MVCC_INFO dummy_mvcc_info;
  int err = NO_ERROR;
  MVCCID insert_mvccid;

  /* btid needs a place to unpack the sys_btid into.  We'll use stack space. */
  btid.sys_btid = &sys_btid;

  /* extract the stored btid, key, oid data */
  datap = (char *) recv->data;
  datasize = recv->length;
  btree_rv_read_keybuf_nocopy (thread_p, datap, datasize, &btid,
			       &cls_oid, &oid, &dummy_mvcc_info, &key_buf);

  assert (!OID_ISNULL (&oid));

  if (MVCCID_IS_VALID (recv->mvcc_id))
    {
      insert_mvccid = recv->mvcc_id;
    }
  else
    {
      insert_mvccid = MVCCID_ALL_VISIBLE;
    }

  /* Undo insert: just delete object and all its information. */
  err =
    btree_undo_insert_object (thread_p, btid.sys_btid, &key_buf, &oid,
			      &cls_oid, insert_mvccid);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      assert (err == ER_BTREE_UNKNOWN_KEY || err == NO_ERROR
	      || err == ER_INTERRUPTED);
      return err;
    }

  return NO_ERROR;
}

/*
 * btree_rv_keyval_undo_insert_mvcc_delid () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Undo the insertion of a <key, val> pair to the B+tree,
 * by deleting the <key, val> pair from the tree.
 */
int
btree_rv_keyval_undo_insert_mvcc_delid (THREAD_ENTRY * thread_p,
					LOG_RCV * recv)
{
  BTID_INT btid;
  BTID sys_btid;
  OR_BUF key_buf;
  OID cls_oid;
  OID oid;
  char *datap;
  int datasize;
  BTREE_MVCC_INFO dummy_mvcc_info;
  int err = NO_ERROR;
  MVCCID delete_mvccid;

  /* btid needs a place to unpack the sys_btid into.  We'll use stack space. */
  btid.sys_btid = &sys_btid;

  /* extract the stored btid, key, oid data */
  datap = (char *) recv->data;
  datasize = recv->length;

  btree_rv_read_keybuf_nocopy (thread_p, datap, datasize, &btid, &cls_oid,
			       &oid, &dummy_mvcc_info, &key_buf);
  assert (!OID_ISNULL (&oid));

  delete_mvccid = recv->mvcc_id;
  assert (MVCCID_IS_NOT_ALL_VISIBLE (delete_mvccid));
  err =
    btree_undo_mvcc_delete (thread_p, btid.sys_btid, &key_buf, &oid, &cls_oid,
			    delete_mvccid);
  if (err != NO_ERROR)
    {
      ASSERT_ERROR ();
      assert (err == ER_BTREE_UNKNOWN_KEY || err == NO_ERROR
	      || err == ER_INTERRUPTED);
      return err;
    }

  return NO_ERROR;
}

/*
 * btree_rv_keyval_undo_delete () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: undo the deletion of a <key, val> pair to the B+tree,
 * by inserting the <key, val> pair to the tree.
 */
int
btree_rv_keyval_undo_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  BTID_INT btid;
  BTID sys_btid;
  DB_VALUE key;
  OID cls_oid;
  OID oid;
  char *datap;
  int datasize;
  BTREE_MVCC_INFO mvcc_info;
  int error_code = NO_ERROR;

  /* btid needs a place to unpack the sys_btid into.  We'll use stack space. */
  btid.sys_btid = &sys_btid;

  /* extract the stored btid, key, oid data */
  datap = (char *) recv->data;
  datasize = recv->length;
  error_code =
    btree_rv_read_keyval_info_nocopy (thread_p, datap, datasize, &btid,
				      &cls_oid, &oid, &mvcc_info, &key);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  assert (!OID_ISNULL (&oid));

  /* Insert object and all its info. */
  error_code =
    btree_undo_delete_physical (thread_p, btid.sys_btid, &key, &cls_oid, &oid,
				&mvcc_info);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      assert (error_code == ER_BTREE_DUPLICATE_OID
	      || error_code == ER_INTERRUPTED);
      return error_code;
    }

  return NO_ERROR;
}

/*
 * btree_rv_keyval_dump () - Dump undo information <key-value> insertion.
 *
 * return      : Void.
 * fp (in)     : File pointer.
 * length (in) : Data length.
 * data (in)   : Recovery data.
 */
void
btree_rv_keyval_dump (FILE * fp, int length, void *data)
{
  BTID btid;
  OID oid, class_oid;
  short mvcc_flags;
  MVCCID mvccid;

  data = or_unpack_btid (data, &btid);
  fprintf (fp, " BTID = { { %d , %d }, %d} \n ",
	   btid.vfid.volid, btid.vfid.fileid, btid.root_pageid);

  data = or_unpack_oid (data, &oid);
  mvcc_flags = BTREE_OID_GET_MVCC_FLAGS (&oid);

  if (BTREE_OID_IS_RECORD_FLAG_SET (&oid, BTREE_LEAF_RECORD_CLASS_OID))
    {
      data = or_unpack_oid (data, &class_oid);
      BTREE_OID_CLEAR_ALL_FLAGS (&oid);
      fprintf (fp, " OID = { %d, %d, %d } \n", oid.volid, oid.pageid,
	       oid.slotid);
      fprintf (fp, " CLASS_OID = { %d, %d, %d } \n", class_oid.volid,
	       class_oid.pageid, class_oid.slotid);
    }
  else
    {
      BTREE_OID_CLEAR_ALL_FLAGS (&oid);
      fprintf (fp, " OID = { %d, %d, %d } \n", oid.volid, oid.pageid,
	       oid.slotid);
    }

  if (mvcc_flags & BTREE_OID_HAS_MVCC_INSID)
    {
      data = or_unpack_mvccid (data, &mvccid);
      fprintf (fp, " INSERT MVCCID = %llu \n",
	       (long long unsigned int) mvccid);
    }
  if (mvcc_flags & BTREE_OID_HAS_MVCC_DELID)
    {
      data = or_unpack_mvccid (data, &mvccid);
      fprintf (fp, " DELETE MVCCID = %llu \n",
	       (long long unsigned int) mvccid);
    }
}

/*
 * btree_rv_undoredo_copy_page () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Copy a whole page back for undo or redo purposes
 */
int
btree_rv_undoredo_copy_page (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  (void) pgbuf_set_page_ptype (thread_p, recv->pgptr, PAGE_BTREE);	/* redo */

  (void) memcpy (recv->pgptr, recv->data, DB_PAGESIZE);

  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * btree_rv_nop () -
 *   return: int
 *   recv(in): Recovery structure
 *
 *
 * Note: Does nothing. This routine is used for to accompany some
 * compensating redo logs which are supposed to do nothing.
 */
int
btree_rv_nop (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  assert (recv->pgptr != NULL);
  pgbuf_set_dirty (thread_p, recv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * btree_multicol_key_is_null () -
 *   return: Return true if DB_VALUE is a NULL multi-column
 *           key and false otherwise.
 *   key(in): Pointer to multi-column key
 *
 * Note: Check the multi-column key for a NULL value. In terms of the B-tree,
 * a NULL multi-column key is a sequence in which each element is NULL.
 */
bool
btree_multicol_key_is_null (DB_VALUE * key)
{
  bool status = false;
  DB_MIDXKEY *midxkey;
  unsigned char *bits;
  int nbytes, i;

  if (DB_VALUE_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      assert (!DB_IS_NULL (key));

      midxkey = DB_GET_MIDXKEY (key);
      assert (midxkey != NULL);

      /* ncolumns == -1 means already constructing step */
      if (midxkey && midxkey->ncolumns != -1)
	{
	  bits = (unsigned char *) midxkey->buf;
	  nbytes = OR_MULTI_BOUND_BIT_BYTES (midxkey->ncolumns);
	  for (i = 0; i < nbytes; i++)
	    {
	      if (bits[i] != (unsigned char) 0)
		{
		  return false;
		}
	    }

	  status = true;
	}
    }

  return status;
}

/*
 * btree_multicol_key_has_null () -
 *   return: Return true if DB_VALUE is a multi-column key
 *           and has a NULL element in it and false otherwise.
 *   key(in): Pointer to multi-column key
 *
 * Note: Check the multi-column  key has a NULL element.
 */
int
btree_multicol_key_has_null (DB_VALUE * key)
{
  int status = 0;
  DB_MIDXKEY *midxkey;
  int i;

  if (DB_VALUE_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      assert (!DB_IS_NULL (key));

      midxkey = DB_GET_MIDXKEY (key);
      assert (midxkey != NULL);

      /* ncolumns == -1 means already constructing step */
      if (midxkey && midxkey->ncolumns != -1)
	{
	  for (i = 0; i < midxkey->ncolumns; i++)
	    {
	      if (OR_MULTI_ATT_IS_UNBOUND (midxkey->buf, i))
		{
		  return 1;
		}
	    }

	  return 0;
	}
    }

  return status;
}

/*
 * btree_find_key_from_leaf () -
 *   return:
 *   btid(in):
 *   pg_ptr(in):
 *   key_cnt(in):
 *   oid(in):
 *   key(in):
 *   clear_key(in):
 */
static DISK_ISVALID
btree_find_key_from_leaf (THREAD_ENTRY * thread_p,
			  BTID_INT * btid, PAGE_PTR pg_ptr,
			  int key_cnt, OID * oid,
			  DB_VALUE * key, bool * clear_key)
{
  RECDES rec;
  LEAF_REC leaf_pnt;
  VPID ovfl_vpid;
  int i, offset;
  int error_code;
  PAGE_PTR found_page = NULL;
  int offset_to_object = NOT_FOUND;

  VPID_SET_NULL (&leaf_pnt.ovfl);

  for (i = 1; i <= key_cnt; i++)
    {
      if (spage_get_record (pg_ptr, i, &rec, PEEK) != S_SUCCESS)
	{
	  return DISK_ERROR;
	}

      if (btree_read_record (thread_p, btid, pg_ptr, &rec, key, &leaf_pnt,
			     BTREE_LEAF_NODE, clear_key, &offset,
			     PEEK_KEY_VALUE, NULL) != NO_ERROR)
	{
	  return DISK_ERROR;
	}
      ovfl_vpid = leaf_pnt.ovfl;

      error_code =
	btree_find_oid_and_its_page (thread_p, btid, oid, pg_ptr,
				     BTREE_OP_DELETE_OBJECT_PHYSICAL, NULL,
				     &rec, &leaf_pnt, offset, &found_page,
				     NULL, &offset_to_object, NULL);
      if (error_code != NO_ERROR)
	{
	  assert (found_page == NULL);
	  return DISK_ERROR;
	}
      if (offset_to_object != NOT_FOUND)
	{
	  /* key will be cleared by caller */
	  assert (found_page != NULL);
	  if (found_page != pg_ptr)
	    {
	      pgbuf_unfix_and_init (thread_p, found_page);
	    }
	  return DISK_VALID;
	}

      btree_clear_key_value (clear_key, key);
    }

  return DISK_INVALID;
}

/*
 * btree_find_key_from_nleaf () -
 *   return:
 *   btid(in):
 *   pg_ptr(in):
 *   key_cnt(in):
 *   oid(in):
 *   key(in):
 *   clear_key(in):
 */
static DISK_ISVALID
btree_find_key_from_nleaf (THREAD_ENTRY * thread_p,
			   BTID_INT * btid, PAGE_PTR pg_ptr,
			   int key_cnt, OID * oid,
			   DB_VALUE * key, bool * clear_key)
{
  int i;
  NON_LEAF_REC nleaf_ptr;
  VPID page_vpid;
  PAGE_PTR page = NULL;
  RECDES rec;
  DISK_ISVALID status = DISK_INVALID;

  for (i = 1; i <= key_cnt; i++)
    {
      if (spage_get_record (pg_ptr, i, &rec, PEEK) != S_SUCCESS)
	{
	  return DISK_ERROR;
	}

      btree_read_fixed_portion_of_non_leaf_record (&rec, &nleaf_ptr);
      page_vpid = nleaf_ptr.pnt;

      page = pgbuf_fix (thread_p, &page_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			PGBUF_UNCONDITIONAL_LATCH);
      if (page == NULL)
	{
	  return DISK_ERROR;
	}

      (void) pgbuf_check_page_ptype (thread_p, page, PAGE_BTREE);

      status = btree_find_key_from_page (thread_p, btid, page, oid, key,
					 clear_key);
      pgbuf_unfix_and_init (thread_p, page);

      if (status == DISK_VALID)
	{
	  break;
	}
    }

  return status;
}

/*
 * btree_find_key_from_page () -
 *   return:
 *   btid(in):
 *   pg_ptr(in):
 *   oid(in):
 *   key(in):
 *   clear_key(in):
 */
static DISK_ISVALID
btree_find_key_from_page (THREAD_ENTRY * thread_p,
			  BTID_INT * btid, PAGE_PTR pg_ptr,
			  OID * oid, DB_VALUE * key, bool * clear_key)
{
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;
  int key_cnt;
  DISK_ISVALID status;

  key_cnt = btree_node_number_of_keys (pg_ptr);

  header = btree_get_node_header (pg_ptr);
  if (header == NULL)
    {
      return DISK_ERROR;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if (node_type == BTREE_NON_LEAF_NODE)
    {
      status = btree_find_key_from_nleaf (thread_p, btid, pg_ptr, key_cnt,
					  oid, key, clear_key);
    }
  else
    {
      status = btree_find_key_from_leaf (thread_p, btid, pg_ptr, key_cnt,
					 oid, key, clear_key);
    }

  return status;
}

/*
 * btree_find_key () -
 *   return:
 *   btid(in):
 *   oid(in):
 *   key(in):
 *   clear_key(in):
 */
DISK_ISVALID
btree_find_key (THREAD_ENTRY * thread_p, BTID * btid, OID * oid,
		DB_VALUE * key, bool * clear_key)
{
  VPID root_vpid;
  PAGE_PTR root = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  BTID_INT btid_int;
  DISK_ISVALID status;

  root_vpid.pageid = btid->root_pageid;	/* read root page */
  root_vpid.volid = btid->vfid.volid;
  root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		    PGBUF_UNCONDITIONAL_LATCH);
  if (root == NULL)
    {
      return DISK_ERROR;
    }

  (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

  root_header = btree_get_root_header (root);
  if (root_header == NULL)
    {
      status = DISK_ERROR;
      goto end;
    }

  btid_int.sys_btid = btid;
  btree_glean_root_header_info (thread_p, root_header, &btid_int);
  status = btree_find_key_from_page (thread_p, &btid_int, root, oid,
				     key, clear_key);

end:

  assert (root != NULL);
  pgbuf_unfix_and_init (thread_p, root);

  return status;
}


int
btree_set_error (THREAD_ENTRY * thread_p, DB_VALUE * key,
		 OID * obj_oid, OID * class_oid, BTID * btid,
		 const char *bt_name,
		 int severity, int err_id, const char *filename, int lineno)
{
  char btid_msg_buf[OID_MSG_BUF_SIZE];
  char class_oid_msg_buf[OID_MSG_BUF_SIZE];
  char oid_msg_buf[OID_MSG_BUF_SIZE];
  char *index_name;
  char *class_name;
  char *keyval;

  assert (btid != NULL);

  /* init as empty string */
  btid_msg_buf[0] = class_oid_msg_buf[0] = oid_msg_buf[0] = 0;
  index_name = class_name = keyval = NULL;

  /* fetch index name from the class representation */
  if (class_oid != NULL && !OID_ISNULL (class_oid))
    {
      if (heap_get_indexinfo_of_btid (thread_p,
				      class_oid, btid,
				      NULL, NULL, NULL, NULL,
				      &index_name, NULL) != NO_ERROR)
	{
	  index_name = NULL;
	}
    }

  if (index_name && btid)
    {
      /* print valid btid */
      snprintf (btid_msg_buf, OID_MSG_BUF_SIZE, "(B+tree: %d|%d|%d)",
		btid->vfid.volid, btid->vfid.fileid, btid->root_pageid);
    }

  if (class_oid != NULL && !OID_ISNULL (class_oid))
    {
      class_name = heap_get_class_name (thread_p, class_oid);
      if (class_name)
	{
	  snprintf (class_oid_msg_buf, OID_MSG_BUF_SIZE,
		    "(CLASS_OID: %d|%d|%d)", class_oid->volid,
		    class_oid->pageid, class_oid->slotid);
	}
    }

  if (key && obj_oid)
    {
      keyval = pr_valstring (key);
      if (keyval)
	{
	  snprintf (oid_msg_buf, OID_MSG_BUF_SIZE, "(OID: %d|%d|%d)",
		    obj_oid->volid, obj_oid->pageid, obj_oid->slotid);
	}
    }

  er_set (severity, filename, lineno, err_id, 6,
	  (index_name) ? index_name : ((bt_name) ? bt_name :
				       "*UNKNOWN-INDEX*"), btid_msg_buf,
	  (class_name) ? class_name : "*UNKNOWN-CLASS*", class_oid_msg_buf,
	  (keyval) ? keyval : "*UNKNOWN-KEY*", oid_msg_buf);

  if (keyval)
    {
      free_and_init (keyval);
    }
  if (class_name)
    {
      free_and_init (class_name);
    }
  if (index_name)
    {
      free_and_init (index_name);
    }

  return NO_ERROR;
}

/*
 * btree_get_asc_desc - get asc/desc for column index from BTREE
 *
 *   return:  error code
 *   thread_p(in): THREAD_ENTRY
 *   btid(in): BTID
 *   col_idx(in): column index
 *   asc_desc(out): asc/desc for column index
 */
int
btree_get_asc_desc (THREAD_ENTRY * thread_p, BTID * btid, int col_idx,
		    int *asc_desc)
{
  DISK_ISVALID valid = DISK_ERROR;
  VPID r_vpid;			/* root page identifier */
  PAGE_PTR r_pgptr = NULL;	/* root page pointer */
  BTID_INT btid_int;
  BTREE_ROOT_HEADER *root_header = NULL;
  TP_DOMAIN *domain;
  int k, ret = NO_ERROR;

  if (btid == NULL || asc_desc == NULL)
    {
      return ER_FAILED;
    }

  ret = NO_ERROR;
  *asc_desc = 0;

  r_vpid.pageid = btid->root_pageid;
  r_vpid.volid = btid->vfid.volid;

  r_pgptr = pgbuf_fix (thread_p, &r_vpid, OLD_PAGE,
		       PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (r_pgptr == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, r_pgptr, PAGE_BTREE);

  root_header = btree_get_root_header (r_pgptr);
  if (root_header == NULL)
    {
      goto exit_on_error;
    }

  btid_int.sys_btid = btid;

  ret = btree_glean_root_header_info (thread_p, root_header, &btid_int);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (btid_int.key_type->setdomain)
    {
      domain = btid_int.key_type->setdomain;
      for (k = 1; k <= col_idx; k++)
	{
	  domain = domain->next;
	  if (domain == NULL)
	    {
	      goto exit_on_error;
	    }
	}
    }
  else
    {
      domain = btid_int.key_type;
      if (col_idx != 0)
	{
	  return ER_FAILED;
	}
    }

  *asc_desc = domain->is_desc;
  pgbuf_unfix_and_init (thread_p, r_pgptr);

  return NO_ERROR;

exit_on_error:

  if (r_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, r_pgptr);
    }

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;

}

static void
btree_set_unknown_key_error (THREAD_ENTRY * thread_p,
			     BTID * btid, DB_VALUE * key,
			     const char *debug_msg)
{
  int severity;
  PR_TYPE *pr_type;
  char *err_key;

  assert (btid != NULL);
  assert (key != NULL);

  /* If this is vacuum worker, we can expect many such error. Don't spam the
   * log with them.
   */
  if (VACUUM_IS_THREAD_VACUUM_WORKER (thread_p))
    {
      return;
    }

  if (log_is_in_crash_recovery ())
    {
      severity = ER_WARNING_SEVERITY;
    }
  else
    {
      severity = ER_ERROR_SEVERITY;
    }

  err_key = pr_valstring (key);
  pr_type = PR_TYPE_FROM_ID (DB_VALUE_DOMAIN_TYPE (key));

  er_set (severity, ARG_FILE_LINE, ER_BTREE_UNKNOWN_KEY, 5,
	  (err_key != NULL) ? err_key : "_NULL_KEY",
	  btid->vfid.fileid, btid->vfid.volid, btid->root_pageid,
	  (pr_type != NULL) ? pr_type->name : "INVALID KEY TYPE");

  er_log_debug (ARG_FILE_LINE, debug_msg);

  if (err_key != NULL)
    {
      free_and_init (err_key);
    }
}

/*
 * btree_get_next_page_vpid () - Get VPID of next leaf node in b-tree.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * leaf_page (in)  : Leaf node.
 * next_vpid (out) : Outputs VPID of next leaf node.
 */
static int
btree_get_next_page_vpid (THREAD_ENTRY * thread_p, PAGE_PTR leaf_page,
			  VPID * next_vpid)
{
  BTREE_NODE_HEADER *header = NULL;

  assert (leaf_page != NULL);
  assert (btree_get_node_level (leaf_page) == 1);
  assert (next_vpid != NULL);

  header = btree_get_node_header (leaf_page);
  if (header == NULL)
    {
      assert (false);
      return ER_FAILED;
    }
  VPID_COPY (next_vpid, &header->next_vpid);
  return NO_ERROR;
}

/*
 * btree_get_next_page () -
 *   return:
 *
 *   page_p(in):
 */
static PAGE_PTR
btree_get_next_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p)
{
  BTREE_NODE_HEADER *header = NULL;
  PAGE_PTR next_page = NULL;
  VPID next_vpid;

  if (page_p == NULL)
    {
      assert (page_p != NULL);
      return NULL;
    }

  header = btree_get_node_header (page_p);
  if (header == NULL)
    {
      return NULL;
    }

  next_vpid = header->next_vpid;
  if (VPID_ISNULL (&next_vpid))
    {
      goto exit_on_error;
    }

  next_page = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (next_page == NULL)
    {
      goto exit_on_error;
    }

  (void) pgbuf_check_page_ptype (thread_p, next_page, PAGE_BTREE);

  return next_page;

exit_on_error:

  if (next_page)
    {
      pgbuf_unfix_and_init (thread_p, next_page);
    }
  return NULL;
}

/*
 * btree_set_vpid_previous_vpid () - Sets the prev VPID of a page
 *   return: error code
 *   btid(in): BTID
 *   page_p(in):
 *   prev(in): a vpid to be set as previous for the input page
 */
static int
btree_set_vpid_previous_vpid (THREAD_ENTRY * thread_p, BTID_INT * btid,
			      PAGE_PTR page_p, VPID * prev)
{
  BTREE_NODE_HEADER *header = NULL;

  if (page_p == NULL)
    {
      return NO_ERROR;
    }

  header = btree_get_node_header (page_p);
  if (header == NULL)
    {
      return ER_FAILED;
    }

  btree_node_header_undo_log (thread_p, &btid->sys_btid->vfid, page_p);
  header->prev_vpid = *prev;
  btree_node_header_redo_log (thread_p, &btid->sys_btid->vfid, page_p);

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  return NO_ERROR;
}

int
btree_compare_key (DB_VALUE * key1, DB_VALUE * key2,
		   TP_DOMAIN * key_domain,
		   int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  DB_TYPE key1_type, key2_type;
  DB_TYPE dom_type;
  int dummy_size1, dummy_size2, dummy_diff_column;
  bool dom_is_desc = false, dummy_next_dom_is_desc;
  bool comparable = true;

  assert (key1 != NULL && key2 != NULL && key_domain != NULL);

  key1_type = DB_VALUE_DOMAIN_TYPE (key1);
  key2_type = DB_VALUE_DOMAIN_TYPE (key2);
  dom_type = TP_DOMAIN_TYPE (key_domain);

  if (DB_IS_NULL (key1))
    {
      if (DB_IS_NULL (key2))
	{
	  assert (false);
	  return DB_UNK;
	}

      return DB_LT;
    }

  if (DB_IS_NULL (key2))
    {
      if (DB_IS_NULL (key1))
	{
	  assert (false);
	  return DB_UNK;
	}

      return DB_GT;
    }

  if (dom_type == DB_TYPE_MIDXKEY)
    {
      /* safe code */
      if (key1_type != DB_TYPE_MIDXKEY)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TP_CANT_COERCE, 2,
		  pr_type_name (key1_type), pr_type_name (dom_type));
	  assert (false);
	  return DB_UNK;
	}
      if (key2_type != DB_TYPE_MIDXKEY)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TP_CANT_COERCE, 2,
		  pr_type_name (key2_type), pr_type_name (dom_type));
	  assert (false);
	  return DB_UNK;
	}

      c = pr_midxkey_compare (DB_GET_MIDXKEY (key1), DB_GET_MIDXKEY (key2),
			      do_coercion, total_order, -1, start_colp,
			      &dummy_size1, &dummy_size2, &dummy_diff_column,
			      &dom_is_desc, &dummy_next_dom_is_desc);
      assert_release (c == DB_UNK || (DB_LT <= c && c <= DB_GT));

      if (dom_is_desc)
	{
	  c = ((c == DB_GT) ? DB_LT : (c == DB_LT) ? DB_GT : c);
	}
    }
  else
    {
      assert (key1_type != DB_TYPE_MIDXKEY);
      assert (key2_type != DB_TYPE_MIDXKEY);

      assert (tp_valid_indextype (key1_type));
      assert (tp_valid_indextype (key2_type));

      /* safe code */
      if (key1_type == DB_TYPE_MIDXKEY)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TP_CANT_COERCE, 2,
		  pr_type_name (key1_type), pr_type_name (dom_type));
	  assert (false);
	  return DB_UNK;
	}
      if (key2_type == DB_TYPE_MIDXKEY)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_TP_CANT_COERCE, 2,
		  pr_type_name (key2_type), pr_type_name (dom_type));
	  assert (false);
	  return DB_UNK;
	}

      if (TP_ARE_COMPARABLE_KEY_TYPES (key1_type, key2_type)
	  && TP_ARE_COMPARABLE_KEY_TYPES (key1_type, dom_type)
	  && TP_ARE_COMPARABLE_KEY_TYPES (key2_type, dom_type))
	{
	  c =
	    (*(key_domain->type->cmpval)) (key1, key2, do_coercion,
					   total_order, NULL,
					   key_domain->collation_id);
	}
      else
	{
	  c =
	    tp_value_compare_with_error (key1, key2, do_coercion, total_order,
					 &comparable);

	  if (!comparable)
	    {
	      return DB_UNK;
	    }
	}

      assert_release (c == DB_UNK || (DB_LT <= c && c <= DB_GT));

      /* for single-column desc index */
      if (key_domain->is_desc)
	{
	  c = ((c == DB_GT) ? DB_LT : (c == DB_LT) ? DB_GT : c);
	}
    }

  assert_release (c == DB_UNK || (DB_LT <= c && c <= DB_GT));

  return c;
}

/*
 * btree_range_opt_check_add_index_key () - Add key in the array of top N keys
 *					    for multiple range search
 *					    optimization.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * bts (in)		    : B-tree scan structure.
 * multi_range_opt (in/out) : Multiple range optimization structure.
 * p_new_oid (in)	    : New candidate OID for top N keys.
 * key_added (out)	    : Outputs true if object made it to top N keys.
 */
static int
btree_range_opt_check_add_index_key (THREAD_ENTRY * thread_p,
				     BTREE_SCAN * bts,
				     MULTI_RANGE_OPT * multi_range_opt,
				     OID * p_new_oid, bool * key_added)
{
  int compare_id = 0;
  DB_MIDXKEY *new_mkey = NULL;
  DB_VALUE *new_key_value = NULL;
  int error = NO_ERROR, i = 0;

  assert (multi_range_opt->use == true);

  if (DB_VALUE_DOMAIN_TYPE (&(bts->cur_key)) != DB_TYPE_MIDXKEY
      || multi_range_opt->sort_att_idx == NULL)
    {
      return ER_FAILED;
    }

  *key_added = true;

  assert (multi_range_opt->no_attrs != 0);
  if (multi_range_opt->no_attrs == 0)
    {
      return ER_FAILED;
    }

  new_mkey = DB_PULL_MIDXKEY (&(bts->cur_key));
  new_key_value =
    (DB_VALUE *) db_private_alloc (thread_p,
				   multi_range_opt->no_attrs *
				   sizeof (DB_VALUE));
  if (new_key_value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (DB_VALUE *) * multi_range_opt->no_attrs);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = 0; i < multi_range_opt->no_attrs; i++)
    {
      DB_MAKE_NULL (&new_key_value[i]);
      error = pr_midxkey_get_element_nocopy (new_mkey,
					     multi_range_opt->sort_att_idx[i],
					     &new_key_value[i], NULL, NULL);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }

  if (multi_range_opt->cnt == multi_range_opt->size)
    {
      int c = 0;
      DB_MIDXKEY *comp_mkey = NULL;
      DB_VALUE comp_key_value;
      bool reject_new_elem = false;
      RANGE_OPT_ITEM *last_item = NULL;

      last_item = multi_range_opt->top_n_items[multi_range_opt->size - 1];
      assert (last_item != NULL);

      comp_mkey = DB_PULL_MIDXKEY (&(last_item->index_value));

      /* if all keys are equal, the new element is rejected */
      reject_new_elem = true;
      for (i = 0; i < multi_range_opt->no_attrs; i++)
	{
	  DB_MAKE_NULL (&comp_key_value);
	  error =
	    pr_midxkey_get_element_nocopy (comp_mkey,
					   multi_range_opt->sort_att_idx[i],
					   &comp_key_value, NULL, NULL);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }

	  c =
	    (*(multi_range_opt->sort_col_dom[i]->type->cmpval))
	    (&comp_key_value, &new_key_value[i], 1, 1, NULL,
	     multi_range_opt->sort_col_dom[i]->collation_id);
	  if (c != 0)
	    {
	      /* see if new element should be rejected or accepted and stop
	       * checking keys
	       */
	      reject_new_elem =
		(multi_range_opt->is_desc_order[i]) ? (c > 0) : (c < 0);
	      break;
	    }
	}

      if (reject_new_elem)
	{
	  /* do not add */
	  *key_added = false;

	  if (new_key_value != NULL)
	    {
	      db_private_free_and_init (thread_p, new_key_value);
	    }

	  return NO_ERROR;
	}

      /* overwrite the last item with the new key and OIDs */
      db_value_clear (&(last_item->index_value));
      db_value_clone (&(bts->cur_key), &(last_item->index_value));
      COPY_OID (&(last_item->inst_oid), p_new_oid);
    }
  else
    {
      RANGE_OPT_ITEM *curr_item = NULL;
      /* just insert on last position available */
      assert (multi_range_opt->cnt < multi_range_opt->size);

      curr_item = db_private_alloc (thread_p, sizeof (RANGE_OPT_ITEM));
      if (curr_item == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      multi_range_opt->top_n_items[multi_range_opt->cnt] = curr_item;
      db_value_clone (&(bts->cur_key), &(curr_item->index_value));

      COPY_OID (&(curr_item->inst_oid), p_new_oid);

      multi_range_opt->cnt++;

      if (multi_range_opt->sort_col_dom == NULL)
	{
	  multi_range_opt->sort_col_dom =
	    (TP_DOMAIN **) db_private_alloc (thread_p,
					     multi_range_opt->no_attrs *
					     sizeof (TP_DOMAIN *));
	  if (multi_range_opt->sort_col_dom == NULL)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto exit;
	    }

	  for (i = 0; i < multi_range_opt->no_attrs; i++)
	    {
	      multi_range_opt->sort_col_dom[i] =
		tp_domain_resolve_value (&new_key_value[i], NULL);
	    }
	}
    }

  /* find the position for this element */
  /* if there is only one element => nothing to do */
  if (multi_range_opt->cnt > 1)
    {
      int pos = 0;
      error =
	btree_top_n_items_binary_search (multi_range_opt->top_n_items,
					 multi_range_opt->sort_att_idx,
					 multi_range_opt->sort_col_dom,
					 multi_range_opt->is_desc_order,
					 new_key_value,
					 multi_range_opt->no_attrs, 0,
					 multi_range_opt->cnt - 1, &pos);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      if (pos != multi_range_opt->cnt - 1)
	{
	  RANGE_OPT_ITEM *temp_item;
	  int mem_size =
	    (multi_range_opt->cnt - 1 - pos) * sizeof (RANGE_OPT_ITEM *);

	  /* copy last item to temp */
	  temp_item = multi_range_opt->top_n_items[multi_range_opt->cnt - 1];

	  /* move all items one position to the right in order to free the
	   * position for the new item
	   */
	  memcpy (multi_range_opt->buffer, &multi_range_opt->top_n_items[pos],
		  mem_size);
	  memcpy (&multi_range_opt->top_n_items[pos + 1],
		  multi_range_opt->buffer, mem_size);

	  /* put new item at its designated position */
	  multi_range_opt->top_n_items[pos] = temp_item;
	}
      else
	{
	  /* the new item is already in the correct position */
	}
    }

exit:
  if (new_key_value != NULL)
    {
      db_private_free_and_init (thread_p, new_key_value);
    }
  return error;
}

/*
 * btree_top_n_items_binary_search () - searches for the right position for
 *				        the keys in new_key_values in top
 *					N item list
 *
 * return	       : error code
 * top_n_items (in)    : current top N item list
 * att_idxs (in)       : indexes for midxkey attributes
 * domains (in)	       : domains for midxkey attributes
 * desc_order (in)     : is descending order for midxkey attributes
 *			 if NULL, ascending order will be considered
 * new_key_values (in) : key values for the new item
 * no_keys (in)	       : number of keys that are compared
 * first (in)	       : position of the first item in current range
 * last (in)	       : position of the last item in current range
 * new_pos (out)       : the position where the new item fits
 *
 * NOTE	: At each step, split current range in half and compare with the
 *	  middle item. If all keys are equal save the position of middle item.
 *	  If middle item is better, look between middle and last, otherwise
 *	  look between first and middle.
 *	  The recursion stops when the range cannot be split anymore
 *	  (first + 1 <= last), when normally first is better and last is worse
 *	  and the new item should replace last. There is a special case when
 *	  the new item is better than all items in top N. In this case,
 *	  first must be 0 and an extra compare is made (to see if new item
 *	  should in fact replace first).
 */
static int
btree_top_n_items_binary_search (RANGE_OPT_ITEM ** top_n_items,
				 int *att_idxs, TP_DOMAIN ** domains,
				 bool * desc_order, DB_VALUE * new_key_values,
				 int no_keys, int first, int last,
				 int *new_pos)
{
  DB_MIDXKEY *comp_mkey = NULL;
  DB_VALUE comp_key_value;
  RANGE_OPT_ITEM *comp_item;
  int i, c, error = NO_ERROR;

  int middle;

  assert (last >= first && new_pos != NULL);
  if (last <= first + 1)
    {
      if (first == 0)
	{
	  /* need to check if the new key is smaller than the first */
	  comp_item = top_n_items[0];
	  comp_mkey = DB_PULL_MIDXKEY (&(comp_item->index_value));

	  for (i = 0; i < no_keys; i++)
	    {
	      DB_MAKE_NULL (&comp_key_value);
	      error =
		pr_midxkey_get_element_nocopy (comp_mkey, att_idxs[i],
					       &comp_key_value, NULL, NULL);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	      c =
		(*(domains[i]->type->cmpval)) (&comp_key_value,
					       &new_key_values[i], 1, 1,
					       NULL,
					       domains[i]->collation_id);
	      if (c != 0)
		{
		  if ((desc_order != NULL && desc_order[i] ? c > 0 : c < 0))
		    {
		      /* new value is not better than the first */
		      break;
		    }
		  else
		    {
		      /* new value is better than the first */
		      new_pos = 0;
		      return NO_ERROR;
		    }
		}
	    }
	  /* new value is equal to first, fall through */
	}
      /* here: the new values should be between first and last */
      *new_pos = last;
      return NO_ERROR;
    }

  /* compare new value with the value in the middle of the current range */
  middle = (last + first) / 2;
  comp_item = top_n_items[middle];
  comp_mkey = DB_PULL_MIDXKEY (&(comp_item->index_value));

  for (i = 0; i < no_keys; i++)
    {
      DB_MAKE_NULL (&comp_key_value);
      error =
	pr_midxkey_get_element_nocopy (comp_mkey, att_idxs[i],
				       &comp_key_value, NULL, NULL);
      if (error != NO_ERROR)
	{
	  return error;
	}
      c =
	(*(domains[i]->type->cmpval)) (&comp_key_value, &new_key_values[i], 1,
				       1, NULL, domains[i]->collation_id);
      if (c != 0)
	{
	  if ((desc_order != NULL && desc_order[i] ? c > 0 : c < 0))
	    {
	      /* the new value is worse than the one in the middle */
	      first = middle;
	    }
	  else
	    {
	      /* the new value is better than the one in the middle */
	      last = middle;
	    }
	  return btree_top_n_items_binary_search (top_n_items, att_idxs,
						  domains, desc_order,
						  new_key_values, no_keys,
						  first, last, new_pos);
	}
    }
  /* all keys were equal, the new item can be put in current position */
  *new_pos = middle;
  return NO_ERROR;
}

/*
 * btree_iss_set_key () - save the current key
 *
 *   return: error code
 *   bts(in):
 *   iss(in):
 */
static int
btree_iss_set_key (BTREE_SCAN * bts, INDEX_SKIP_SCAN * iss)
{
  struct regu_variable_node *key = NULL;
  int ret = NO_ERROR;

  /* check environment */
  if (DB_VALUE_DOMAIN_TYPE (&bts->cur_key) != DB_TYPE_MIDXKEY
      || iss == NULL || iss->skipped_range == NULL)
    {
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return ER_FAILED;
    }

  /* get correct key to update value to (key1 for normal scan or key2 for
     reverse scan); the fetch range will have one of the keys NULLed */
  if (iss->skipped_range->key1 == NULL)
    {
      key = iss->skipped_range->key2;
    }
  else
    {
      key = iss->skipped_range->key1;
    }

  /* check the key */
  if (key == NULL || key->value.funcp == NULL
      || key->value.funcp->operand == NULL
      || key->value.funcp->operand->value.type != TYPE_DBVAL)
    {
      assert_release (false);
      return ER_FAILED;
    }

  /* save the found key as bound for next fetch */
  pr_clear_value (&key->value.funcp->operand->value.value.dbval);
  ret = pr_clone_value (&bts->cur_key,
			&key->value.funcp->operand->value.value.dbval);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  return NO_ERROR;
}

/*****************************************************************************/
/* For migrate_90beta_to_91                                                  */
/*****************************************************************************/
#define MIGRATE_90BETA_TO_91

#if defined(MIGRATE_90BETA_TO_91)

extern int btree_fix_overflow_oid_page_all_btrees (void);

static int btree_fix_ovfl_oid_pages_by_btid (THREAD_ENTRY * thread_p,
					     BTID * btid);
static int btree_fix_ovfl_oid_pages_tree (THREAD_ENTRY * thread_p,
					  BTID * btid, char *btname);
static int btree_fix_ovfl_oid_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
				    PAGE_PTR pg_ptr, char *btname);
static int btree_compare_oid (const void *oid_mem1, const void *oid_mem2);

static int fixed_pages;

int
btree_fix_overflow_oid_page_all_btrees (void)
{
  int num_files, i;
  BTID btid;
  FILE_TYPE file_type;
  THREAD_ENTRY *thread_p;

  printf ("Start to fix BTREE Overflow OID pages\n\n");

  thread_p = thread_get_thread_entry_info ();

  num_files = file_get_numfiles (thread_p);
  if (num_files < 0)
    {
      return ER_FAILED;
    }

  for (i = 0; i < num_files; i++)
    {
      if (file_find_nthfile (thread_p, &btid.vfid, i) != 1)
	{
	  break;
	}

      file_type = file_get_type (thread_p, &btid.vfid);
      if (file_type == FILE_UNKNOWN_TYPE)
	{
	  return ER_FAILED;
	}

      if (file_type != FILE_BTREE)
	{
	  continue;
	}

      if (btree_fix_ovfl_oid_pages_by_btid (thread_p, &btid) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

static int
btree_fix_ovfl_oid_pages_by_btid (THREAD_ENTRY * thread_p, BTID * btid)
{
  char area[FILE_DUMP_DES_AREA_SIZE];
  char *fd = area;
  int fd_size = FILE_DUMP_DES_AREA_SIZE, size;
  FILE_BTREE_DES *btree_des;
  char *btname;
  VPID vpid;
  int ret = NO_ERROR;

  size = file_get_descriptor (thread_p, &btid->vfid, fd, fd_size);
  if (size < 0)
    {
      fd_size = -size;
      fd = (char *) db_private_alloc (thread_p, fd_size);
      if (fd == NULL)
	{
	  fd = area;
	  fd_size = FILE_DUMP_DES_AREA_SIZE;
	}
      else
	{
	  size = file_get_descriptor (thread_p, &btid->vfid, fd, fd_size);
	}
    }

  btree_des = (FILE_BTREE_DES *) fd;

  /* get the index name of the index key */
  ret = heap_get_indexinfo_of_btid (thread_p, &(btree_des->class_oid),
				    btid, NULL, NULL, NULL,
				    NULL, &btname, NULL);
  if (ret != NO_ERROR)
    {
      goto exit_on_end;
    }

  if (btree_get_root_page (thread_p, btid, &vpid) == NULL)
    {
      ret = ER_FAILED;
      goto exit_on_end;
    }

  btid->root_pageid = vpid.pageid;
  ret = btree_fix_ovfl_oid_pages_tree (thread_p, btid, btname);

exit_on_end:

  if (fd != area)
    {
      db_private_free_and_init (thread_p, fd);
    }

  if (btname)
    {
      free_and_init (btname);
    }

  return ret;
}

static int
btree_fix_ovfl_oid_pages_tree (THREAD_ENTRY * thread_p, BTID * btid,
			       char *btname)
{
  DISK_ISVALID valid = DISK_ERROR;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  BTREE_NODE_HEADER *header = NULL;
  BTID_INT btid_int;

  /* fetch the root page */

  vpid.pageid = btid->root_pageid;
  vpid.volid = btid->vfid.volid;

  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_BTREE);

  root_header = btree_get_root_header (pgptr);
  if (root_header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
      return ER_FAILED;
    }

  btid_int.sys_btid = btid;
  if (btree_glean_root_header_info (thread_p, root_header,
				    &btid_int) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
      return ER_FAILED;
    }

  pgbuf_unfix_and_init (thread_p, pgptr);

  if (BTREE_IS_UNIQUE (btid_int.unique_pk))
    {
      return NO_ERROR;
    }

  pgptr = btree_find_leftmost_leaf (thread_p, btid, &vpid, NULL);
  if (pgptr == NULL)
    {
      return ER_FAILED;
    }

  fixed_pages = 0;
  fprintf (stdout, "Index: %-50s %8d", btname, fixed_pages);

  /* traverse leaf page links */

  while (true)
    {
      if (btree_fix_ovfl_oid_page (thread_p, &btid_int, pgptr,
				   btname) != NO_ERROR)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  fprintf (stdout, "\n");
	  return ER_FAILED;
	}

      header = btree_get_node_header (pgptr);
      if (header == NULL)
	{
	  pgbuf_unfix_and_init (thread_p, pgptr);
	  fprintf (stdout, "\n");
	  return ER_FAILED;
	}

      vpid = header->next_vpid;

      pgbuf_unfix_and_init (thread_p, pgptr);

      if (VPID_ISNULL (&vpid))
	{
	  break;
	}

      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  fprintf (stdout, "\n");
	  return ER_FAILED;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_BTREE);
    }

  fprintf (stdout, "\n");

  return NO_ERROR;
}

static int
btree_fix_ovfl_oid_page (THREAD_ENTRY * thread_p, BTID_INT * btid,
			 PAGE_PTR pg_ptr, char *btname)
{
  RECDES leaf_rec, ovfl_rec;
  int key_cnt, i, offset;
  LEAF_REC leaf_pnt;
  bool dummy;
  VPID ovfl_vpid;
  PAGE_PTR ovfl_page = NULL;
  char *rv_data = NULL;
  int rv_data_len;
  char rv_data_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  BTREE_NODE_HEADER *header = NULL;
  int size = BTREE_OBJECT_FIXED_SIZE (btid);

  rv_data = PTR_ALIGN (rv_data_buf, BTREE_MAX_ALIGN);

  key_cnt = btree_node_number_of_keys (pg_ptr);

  header = btree_get_node_header (pg_ptr);

  assert_release (header != NULL);
  assert_release (header->node_level == 1);	/* BTREE_LEAF_NODE */

  for (i = 1; i <= key_cnt; i++)
    {
      if (spage_get_record (pg_ptr, i, &leaf_rec, PEEK) != S_SUCCESS)
	{
	  return ER_FAILED;
	}

      VPID_SET_NULL (&leaf_pnt.ovfl);
      if (btree_read_record (thread_p, btid, pg_ptr, &leaf_rec, NULL,
			     &leaf_pnt, BTREE_LEAF_NODE, &dummy, &offset,
			     PEEK_KEY_VALUE, NULL) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      ovfl_vpid = leaf_pnt.ovfl;

      while (!VPID_ISNULL (&ovfl_vpid))
	{
	  ovfl_page = pgbuf_fix (thread_p, &ovfl_vpid, OLD_PAGE,
				 PGBUF_LATCH_WRITE,
				 PGBUF_UNCONDITIONAL_LATCH);
	  if (ovfl_page == NULL)
	    {
	      return ER_FAILED;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, ovfl_page, PAGE_BTREE);

	  btree_get_next_overflow_vpid (ovfl_page, &ovfl_vpid);

	  if (spage_get_record (ovfl_page, 1, &ovfl_rec, PEEK) != S_SUCCESS)
	    {
	      pgbuf_unfix_and_init (thread_p, ovfl_page);
	      return ER_FAILED;
	    }

	  /* undo log only */
	  btree_rv_write_log_record (rv_data, &rv_data_len, &ovfl_rec,
				     BTREE_LEAF_NODE);
	  log_append_undo_data2 (thread_p, RVBT_NDRECORD_UPD,
				 &btid->sys_btid->vfid, ovfl_page,
				 1, rv_data_len, rv_data);

	  qsort (ovfl_rec.data, CEIL_PTVDIV (ovfl_rec.length, size),
		 size, btree_compare_oid);

	  pgbuf_set_dirty (thread_p, ovfl_page, FREE);

	  fprintf (stdout, "\rIndex: %-50s %8d", btname, ++fixed_pages);
	  if (fixed_pages % 100 == 0)
	    {
	      fflush (stdout);
	    }
	}
    }

  fflush (stdout);
  return NO_ERROR;
}

static int
btree_compare_oid (const void *oid_mem1, const void *oid_mem2)
{
  OID oid1, oid2;

  BTREE_GET_OID (oid_mem1, &oid1);
  BTREE_OID_CLEAR_RECORD_FLAGS (&oid1);

  BTREE_GET_OID (oid_mem2, &oid2);
  BTREE_OID_CLEAR_RECORD_FLAGS (&oid2);

  return oid_compare (&oid1, &oid2);
}
#endif /* MIGRATE_90BETA_TO_91 */

#if !defined(NDEBUG)
static int
btree_verify_node (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
		   PAGE_PTR page_ptr)
{
  int ret = NO_ERROR;
  int key_cnt;
  BTREE_NODE_HEADER *header = NULL;
  BTREE_NODE_TYPE node_type;

  bool clear_key = false;
  int key_type = BTREE_NORMAL_KEY;

  assert_release (btid_int != NULL);
  assert_release (page_ptr != NULL);

  /* check header validation */

  key_cnt = btree_node_number_of_keys (page_ptr);

  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      return ER_FAILED;
    }

  if (key_cnt > 0)
    {
      assert (header->max_key_len > 0);
    }

  assert (header->split_info.pivot >= 0 && header->split_info.pivot <= 1);
  assert (header->split_info.index >= 0);
  assert (header->node_level > 0);

  assert (header->prev_vpid.volid >= NULL_VOLID);
  assert (header->prev_vpid.pageid >= NULL_PAGEID);
  assert (header->next_vpid.volid >= NULL_VOLID);
  assert (header->next_vpid.pageid >= NULL_PAGEID);

#if 0				/* DO NOT DELETE ME */
  /*
   * FOR TEST
   *   usually should admit below assertions.
   *   but assert is possible in normal case rarely.
   *   so, turn on this block in develop stage if you want.
   */

  assert (header->node_level < 20);

  assert (header->prev_vpid.volid < 1000);
  assert (header->prev_vpid.pageid < 1000000);
  assert (header->next_vpid.volid < 1000);
  assert (header->next_vpid.pageid < 1000000);
#endif

  if ((prm_get_integer_value (PRM_ID_ER_BTREE_DEBUG) &
       BTREE_DEBUG_HEALTH_FULL) == 0)
    {
      return NO_ERROR;
    }

  node_type =
    (header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if (node_type == BTREE_NON_LEAF_NODE)
    {
      ret = btree_verify_nonleaf_node (thread_p, btid_int, page_ptr);
    }
  else
    {
      ret = btree_verify_leaf_node (thread_p, btid_int, page_ptr);
    }

  assert_release (ret == NO_ERROR);

  return ret;
}

static int
btree_verify_nonleaf_node (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			   PAGE_PTR page_ptr)
{
  BTREE_NODE_HEADER *header = NULL;
  TP_DOMAIN *key_domain;
  int key_cnt;
  int i;
  int offset;
  int c;
  bool clear_prev_key, clear_curr_key;
  DB_VALUE prev_key, curr_key;
  RECDES rec;
  NON_LEAF_REC non_leaf_pnt;
  int error = NO_ERROR;

  assert_release (btid_int != NULL);
  assert_release (page_ptr != NULL);

  clear_prev_key = clear_curr_key = false;
  key_domain = btid_int->key_type;

  key_cnt = btree_node_number_of_keys (page_ptr);
  assert_release (key_cnt >= 1);

  /* check key order; exclude neg-inf separator */
  for (i = 1; i < key_cnt; i++)
    {
      if (spage_get_record (page_ptr, i, &rec, PEEK) != S_SUCCESS)
	{
	  assert (false);
	  return ER_FAILED;
	}

      error = btree_read_record_without_decompression (thread_p, btid_int,
						       &rec, &prev_key,
						       &non_leaf_pnt,
						       BTREE_NON_LEAF_NODE,
						       &clear_prev_key,
						       &offset,
						       PEEK_KEY_VALUE);
      if (error != NO_ERROR)
	{
	  assert (false);
	  return error;
	}

      if (spage_get_record (page_ptr, i + 1, &rec, PEEK) != S_SUCCESS)
	{
	  assert (false);
	  btree_clear_key_value (&clear_prev_key, &prev_key);
	  return ER_FAILED;
	}

      error = btree_read_record_without_decompression (thread_p, btid_int,
						       &rec, &curr_key,
						       &non_leaf_pnt,
						       BTREE_NON_LEAF_NODE,
						       &clear_curr_key,
						       &offset,
						       PEEK_KEY_VALUE);
      if (error != NO_ERROR)
	{
	  assert (false);
	  btree_clear_key_value (&clear_prev_key, &prev_key);
	  return error;
	}

      c = btree_compare_key (&prev_key, &curr_key, btid_int->key_type,
			     1, 1, NULL);

      btree_clear_key_value (&clear_curr_key, &curr_key);
      btree_clear_key_value (&clear_prev_key, &prev_key);

      if (c != DB_LT)
	{
	  if (i == 1)
	    {
	      header = btree_get_node_header (page_ptr);
	      if (header == NULL)
		{
		  return ER_FAILED;
		}

	      if (VPID_ISNULL (&(header->next_vpid)))
		{
		  /* This page is first non-leaf page.
		   * So, this key is neg-inf dummy key */

		  assert (header->next_vpid.volid == NULL_VOLID);

		  return NO_ERROR;
		}
	    }

	  btree_dump_page (thread_p, stdout, NULL, btid_int, NULL, page_ptr,
			   NULL, 2, 2);
	  assert (false);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

static int
btree_verify_leaf_node (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			PAGE_PTR page_ptr)
{
  BTREE_NODE_HEADER *header = NULL;
  TP_DOMAIN *key_domain;
  VPID prev_vpid, next_vpid;
  int key_cnt, offset, oid_cnt;
  int i, k, c;
  bool clear_prev_key, clear_curr_key;
  DB_VALUE prev_key, curr_key;
  RECDES rec;
  LEAF_REC leaf_pnt;
  OID oid, class_oid;
  OR_BUF buf;
  int oid_size;
  short mvcc_flags;
  int error = NO_ERROR;
  BTREE_MVCC_INFO mvcc_info;

  assert_release (btid_int != NULL);
  assert_release (page_ptr != NULL);

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      oid_size = (2 * OR_OID_SIZE);
    }
  else
    {
      oid_size = OR_OID_SIZE;
    }

  clear_prev_key = clear_curr_key = false;

  key_domain = btid_int->key_type;

  key_cnt = btree_node_number_of_keys (page_ptr);

  /* read the header record */
  header = btree_get_node_header (page_ptr);
  if (header == NULL)
    {
      return ER_FAILED;
    }

  prev_vpid = header->prev_vpid;
  next_vpid = header->next_vpid;

  /* check key order */
  for (i = 1; i < key_cnt; i++)
    {
      if (spage_get_record (page_ptr, i, &rec, PEEK) != S_SUCCESS)
	{
	  btree_dump_page (thread_p, stdout, NULL, btid_int, NULL, page_ptr,
			   NULL, 2, 2);
	  assert (false);
	  return ER_FAILED;
	}

      error = btree_read_record_without_decompression (thread_p, btid_int,
						       &rec, &prev_key,
						       &leaf_pnt,
						       BTREE_LEAF_NODE,
						       &clear_prev_key,
						       &offset,
						       PEEK_KEY_VALUE);
      if (error != NO_ERROR)
	{
	  btree_dump_page (thread_p, stdout, NULL, btid_int, NULL, page_ptr,
			   NULL, 2, 2);
	  assert (false);
	  return error;
	}
      /*
       * record oid check
       */
      oid_cnt =
	btree_record_get_num_oids (thread_p, btid_int, &rec, offset,
				   BTREE_LEAF_NODE);

      (void) btree_leaf_get_first_object (btid_int, &rec, &oid, &class_oid,
					  &mvcc_info);
      if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
	{
	  if (oid.pageid != NULL_PAGEID || oid.volid != 0 || oid.slotid != 0)
	    {
	      btree_dump_page (thread_p, stdout, NULL, btid_int, NULL,
			       page_ptr, NULL, 2, 2);
	      assert (false);
	    }
	  if (i > 1 && i < key_cnt)
	    {
	      /* Fence key cannot be in the middle of the node. */
	      btree_dump_page (thread_p, stdout, NULL, btid_int, NULL,
			       page_ptr, NULL, 2, 2);
	      assert (false);
	    }
	  if (i > 1 && VPID_ISNULL (&header->next_vpid))
	    {
	      /* Fence key cannot be at the end of index (unless it is the
	       * only record in page).
	       */
	      btree_dump_page (thread_p, stdout, NULL, btid_int, NULL,
			       page_ptr, NULL, 2, 2);
	      assert (false);
	    }
	  if (i < key_cnt && VPID_ISNULL (&header->prev_vpid))
	    {
	      /* Fence key cannot be at the beginning of index (unless it is
	       * the only record in page).
	       */
	      btree_dump_page (thread_p, stdout, NULL, btid_int, NULL,
			       page_ptr, NULL, 2, 2);
	      assert (false);
	    }
	}
      else
	{
	  if (oid.pageid <= NULL_PAGEID || oid.volid <= NULL_VOLID
	      || oid.slotid <= NULL_SLOTID)
	    {
	      btree_dump_page (thread_p, stdout, NULL, btid_int, NULL,
			       page_ptr, NULL, 2, 2);
	      assert (false);
	    }

	  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	    {
	      if (class_oid.pageid <= NULL_PAGEID
		  || class_oid.volid <= NULL_VOLID
		  || class_oid.slotid <= NULL_SLOTID)
		{
		  btree_dump_page (thread_p, stdout, NULL, btid_int, NULL,
				   page_ptr, NULL, 2, 2);
		  assert (false);
		}
	    }
	}

      or_init (&buf, rec.data + offset, rec.length - offset);
      {
	if ((rec.length - offset) == 4)
	  {
	    int key_len = btree_get_disk_size_of_key (&prev_key);
	    printf ("## key_len: %d, offset: %d, reclen: %d\n", key_len,
		    offset, rec.length);
	    db_value_print (&prev_key);
	    printf ("\n");
	    btree_dump_page (thread_p, stdout, NULL, btid_int, NULL, page_ptr,
			     NULL, 2, 2);
	    assert (false);
	  }

	for (k = 1; k < oid_cnt; k++)
	  {
	    mvcc_flags = btree_record_object_get_mvcc_flags (buf.ptr);
	    or_get_oid (&buf, &oid);
	    oid.volid = oid.volid & ~BTREE_OID_MVCC_FLAGS_MASK;

	    if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	      {
		or_get_oid (&buf, &class_oid);
	      }
	    buf.ptr += BTREE_GET_MVCC_INFO_SIZE_FROM_FLAGS (mvcc_flags);

	    if (oid.pageid <= NULL_PAGEID && oid.volid <= NULL_VOLID
		&& oid.slotid <= NULL_SLOTID)
	      {
		btree_dump_page (thread_p, stdout, NULL, btid_int, NULL,
				 page_ptr, NULL, 2, 2);
		assert (false);
	      }
	  }
      }

      /*
       * key order check
       */
      if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
	{
	  continue;
	}

      if (spage_get_record (page_ptr, i + 1, &rec, PEEK) != S_SUCCESS)
	{
	  btree_dump_page (thread_p, stdout, NULL, btid_int, NULL, page_ptr,
			   NULL, 2, 2);
	  assert (false);
	  btree_clear_key_value (&clear_prev_key, &prev_key);
	  return ER_FAILED;
	}

      if (btree_leaf_is_flaged (&rec, BTREE_LEAF_RECORD_FENCE))
	{
	  btree_clear_key_value (&clear_prev_key, &prev_key);
	  continue;
	}

      error = btree_read_record_without_decompression (thread_p, btid_int,
						       &rec, &curr_key,
						       &leaf_pnt,
						       BTREE_LEAF_NODE,
						       &clear_curr_key,
						       &offset,
						       PEEK_KEY_VALUE);
      if (error != NO_ERROR)
	{
	  btree_dump_page (thread_p, stdout, NULL, btid_int, NULL, page_ptr,
			   NULL, 2, 2);
	  assert (false);
	  btree_clear_key_value (&clear_prev_key, &prev_key);
	  return error;
	}

      c = btree_compare_key (&prev_key, &curr_key, btid_int->key_type,
			     1, 1, NULL);

      btree_clear_key_value (&clear_curr_key, &curr_key);
      btree_clear_key_value (&clear_prev_key, &prev_key);

      if (c != DB_LT)
	{
	  btree_dump_page (thread_p, stdout, NULL, btid_int, NULL, page_ptr,
			   NULL, 2, 2);
	  assert (false);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}
#endif

/*
 * btree_ils_adjust_range () - Adjust scanning range for loose index scan.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * bts (in/out)	 : B-tree scan.
 */
static int
btree_ils_adjust_range (THREAD_ENTRY * thread_p, BTREE_SCAN * bts)
{
  DB_VALUE new_key, *new_key_dbvals, *target_key;
  TP_DOMAIN *dom;
  DB_MIDXKEY midxkey;
  RANGE old_range;
  bool swap_ranges = false;
  int i;
  KEY_VAL_RANGE *key_range = NULL;
  DB_VALUE *curr_key = NULL;
  int prefix_len = 0;
  bool use_desc_index, part_key_desc;

  /* Assert expected arguments. */
  assert (bts != NULL);

  key_range = &bts->index_scan_idp->key_vals[bts->index_scan_idp->curr_keyno];
  curr_key = &bts->cur_key;
  prefix_len = bts->index_scan_idp->indx_info->ils_prefix_len;
  use_desc_index = bts->use_desc_index;
  part_key_desc = BTREE_IS_PART_KEY_DESC (&bts->btid_int);

  /* check environment */
  if (DB_VALUE_DOMAIN_TYPE (curr_key) != DB_TYPE_MIDXKEY)
    {
      assert_release (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return ER_FAILED;
    }

  /* fetch target key */
  if (use_desc_index)
    {
      if (!part_key_desc)
	{
	  swap_ranges = true;
	}
    }
  else
    {
      if (part_key_desc)
	{
	  swap_ranges = true;
	}
    }

  if (swap_ranges)
    {
      /* descending index scan, we adjust upper bound */
      target_key = &key_range->key2;
    }
  else
    {
      /* ascending index scan, we adjust lower bound */
      target_key = &key_range->key1;
    }

  /* allocate key buffer */
  new_key_dbvals =
    (DB_VALUE *) db_private_alloc (thread_p,
				   curr_key->data.midxkey.ncolumns
				   * sizeof (DB_VALUE));
  if (new_key_dbvals == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, curr_key->data.midxkey.ncolumns * sizeof (DB_VALUE));
      return ER_FAILED;
    }

  /* determine target key and adjust range */
  old_range = key_range->range;
  switch (key_range->range)
    {
    case INF_INF:
      if (swap_ranges)
	{
	  key_range->range = INF_LT;	/* (INF, INF) => (INF, ?) */
	}
      else
	{
	  key_range->range = GT_INF;	/* (INF, INF) => (?, INF) */
	}
      break;

    case INF_LE:
      if (swap_ranges)
	{
	  key_range->range = INF_LT;	/* (INF, ?] => (INF, ?) */
	}
      else
	{
	  key_range->range = GT_LE;	/* (INF, ?] => (?, ?] */
	}
      break;

    case INF_LT:
      if (swap_ranges)
	{
	  /* range remains unchanged */
	}
      else
	{
	  key_range->range = GT_LT;	/* (INF, ?) => (?, ?) */
	}
      break;

    case GE_LE:
      if (swap_ranges)
	{
	  key_range->range = GE_LT;	/* [?, ?] => [?, ?) */
	}
      else
	{
	  key_range->range = GT_LE;	/* [?, ?] => (?, ?] */
	}
      break;

    case GE_LT:
      if (swap_ranges)
	{
	  /* range remains unchanged */
	}
      else
	{
	  key_range->range = GT_LT;	/* [?, ?) => (?, ?) */
	}
      break;

    case GE_INF:
      if (swap_ranges)
	{
	  key_range->range = GE_LT;	/* [?, INF) => [?, ?) */
	}
      else
	{
	  key_range->range = GT_INF;	/* [?, INF) => (?, INF)  */
	}
      break;

    case GT_LE:
      if (swap_ranges)
	{
	  key_range->range = GT_LT;	/* (?, ?] => (?, ?) */
	}
      else
	{
	  /* range remains unchanged */
	}
      break;

    case GT_LT:
      /* range remains unchanged */
      break;

    case GT_INF:
      if (swap_ranges)
	{
	  key_range->range = GT_LT;	/* (?, INF) => (?, ?) */
	}
      else
	{
	  /* range remains unchanged */
	}
      break;

    default:
      assert_release (false);	/* should not happen */
      break;
    }

  /* copy prefix of current key into target key */
  for (i = 0; i < prefix_len; i++)
    {
      pr_midxkey_get_element_nocopy (&curr_key->data.midxkey, i,
				     &new_key_dbvals[i], NULL, NULL);
    }

  /* build suffix */

  dom = curr_key->data.midxkey.domain->setdomain;

  /* get to domain */
  for (i = 0; i < prefix_len; i++)
    {
      dom = dom->next;
    }

  /* minimum or maximum suffix */
  for (i = prefix_len; i < curr_key->data.midxkey.ncolumns; i++)
    {
      if ((dom->is_desc && !use_desc_index)
	  || (!dom->is_desc && use_desc_index))
	{
	  DB_MAKE_NULL (&new_key_dbvals[i]);
	}
      else
	{
	  db_value_domain_max (&new_key_dbvals[i], dom->type->id,
			       dom->precision, dom->scale, dom->codeset,
			       dom->collation_id, &dom->enumeration);
	}
      dom = dom->next;
    }

  /* build midxkey */
  midxkey.buf = NULL;
  midxkey.domain = curr_key->data.midxkey.domain;
  midxkey.ncolumns = 0;
  midxkey.size = 0;
  db_make_midxkey (&new_key, &midxkey);
  new_key.need_clear = true;
  pr_midxkey_add_elements (&new_key, new_key_dbvals,
			   curr_key->data.midxkey.ncolumns,
			   curr_key->data.midxkey.domain->setdomain);

#if !defined(NDEBUG)
  if (DB_IS_NULL (target_key))
    {
      assert (!DB_IS_NULL (&new_key));
    }
  else if (old_range == key_range->range)
    {
      int cmp_res;

      /* range did not modify, check if we're advancing */
      cmp_res =
	btree_compare_key (target_key, &new_key, midxkey.domain, 1, 1, NULL);
      if (use_desc_index)
	{
	  assert (cmp_res == DB_GT);
	}
      else
	{
	  assert (cmp_res == DB_LT);
	}
    }
#endif

  /* register key in range */
  pr_clear_value (target_key);
  pr_clone_value (&new_key, target_key);
  pr_clear_value (&new_key);

  db_private_free (thread_p, new_key_dbvals);

  /* all ok */
  return btree_scan_update_range (thread_p, bts, key_range);
}

/*
 * btree_get_next_node_info () - Scans b-tree node by node and obtains info.
 *
 * return	  : Scan code.
 * thread_p (in)  : Thread entry.
 * btid (in)	  : B-tree identifier.
 * btns (in)	  : B-tree node scan data.
 * node_info (in) : Array of value pointers to store b-tree node information.
 */
SCAN_CODE
btree_get_next_node_info (THREAD_ENTRY * thread_p, BTID * btid,
			  BTREE_NODE_SCAN * btns, DB_VALUE ** node_info)
{
  RECDES rec;
  SCAN_CODE result;
  BTREE_NODE_HEADER *node_header;
  BTREE_NODE_TYPE node_type;
  BTREE_NODE_SCAN_QUEUE_ITEM *new_item = NULL, *crt_item = NULL;
  int key_cnt, i;
  NON_LEAF_REC nleaf;
  LEAF_REC leaf_pnt;
  void *rec_header = NULL;
  DB_VALUE key_value;
  bool clear_key = false;
  int dummy;

  assert (btns->crt_page == NULL);

  if (BTREE_NODE_SCAN_IS_QUEUE_EMPTY (btns))
    {
      if (!btns->first_call)
	{
	  /* Finished scanning for b-tree pages */
	  result = S_END;
	  goto end;
	}

      /* First call */

      /* Add root page to queue */
      new_item =
	(BTREE_NODE_SCAN_QUEUE_ITEM *)
	malloc (sizeof (BTREE_NODE_SCAN_QUEUE_ITEM));
      if (new_item == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (BTREE_NODE_SCAN_QUEUE_ITEM));
	  goto error;
	}
      new_item->crt_vpid.pageid = btid->root_pageid;
      new_item->crt_vpid.volid = btid->vfid.volid;
      new_item->next = NULL;
      BTREE_NODE_SCAN_ADD_PAGE_TO_QUEUE (btns, new_item);

      btns->first_call = false;
    }

  BTREE_NODE_SCAN_POP_PAGE_FROM_QUEUE (btns, crt_item);
  btns->crt_vpid = crt_item->crt_vpid;
  btns->crt_page =
    pgbuf_fix (thread_p, &btns->crt_vpid, OLD_PAGE, PGBUF_LATCH_READ,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (btns->crt_page == NULL)
    {
      goto error;
    }

  node_header = btree_get_node_header (btns->crt_page);
  node_type =
    (node_header->node_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;
  key_cnt = btree_node_number_of_keys (btns->crt_page);


  rec_header =
    (node_type == BTREE_NON_LEAF_NODE) ? (void *) &nleaf : (void *) &leaf_pnt;

  if (node_type == BTREE_NON_LEAF_NODE)
    {
      /* Add children to queue */
      for (i = 1; i <= key_cnt; i++)
	{
	  if (spage_get_record (btns->crt_page, i, &rec, PEEK) != S_SUCCESS)
	    {
	      goto error;
	    }
	  btree_read_fixed_portion_of_non_leaf_record (&rec, &nleaf);
	  new_item =
	    (BTREE_NODE_SCAN_QUEUE_ITEM *)
	    malloc (sizeof (BTREE_NODE_SCAN_QUEUE_ITEM));
	  if (new_item == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      sizeof (BTREE_NODE_SCAN_QUEUE_ITEM));
	      goto error;
	    }
	  new_item->crt_vpid.pageid = nleaf.pnt.pageid;
	  new_item->crt_vpid.volid = nleaf.pnt.volid;
	  new_item->next = NULL;
	  BTREE_NODE_SCAN_ADD_PAGE_TO_QUEUE (btns, new_item);
	}
    }

  /* Get b-tree page info */

  /* Get volume id and page id */
  DB_MAKE_INT (node_info[BTREE_NODE_INFO_VOLUMEID], btns->crt_vpid.volid);
  DB_MAKE_INT (node_info[BTREE_NODE_INFO_PAGEID], btns->crt_vpid.pageid);

  /* Get node type */
  db_value_clear (node_info[BTREE_NODE_INFO_NODE_TYPE]);
  DB_MAKE_STRING (node_info[BTREE_NODE_INFO_NODE_TYPE],
		  (node_type == BTREE_NON_LEAF_NODE) ? "non-leaf" : "leaf");

  /* Get key count */
  DB_MAKE_INT (node_info[BTREE_NODE_INFO_KEY_COUNT], key_cnt);

  if (key_cnt > 0)
    {
      /* Get first key */
      if (spage_get_record (btns->crt_page, 1, &rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}
      if (btree_read_record (thread_p, &btns->btid_int, btns->crt_page, &rec,
			     &key_value, rec_header, node_type, &clear_key,
			     &dummy, PEEK_KEY_VALUE, NULL) != NO_ERROR)
	{
	  goto error;
	}
      db_value_clear (node_info[BTREE_NODE_INFO_FIRST_KEY]);
      db_value_clone (&key_value, node_info[BTREE_NODE_INFO_FIRST_KEY]);

      /* Get last key */
      if (spage_get_record (btns->crt_page, key_cnt, &rec, PEEK) != S_SUCCESS)
	{
	  goto error;
	}
      if (btree_read_record (thread_p, &btns->btid_int, btns->crt_page, &rec,
			     &key_value, rec_header, node_type, &clear_key,
			     &dummy, PEEK_KEY_VALUE, NULL) != NO_ERROR)
	{
	  goto error;
	}
      db_value_clear (node_info[BTREE_NODE_INFO_LAST_KEY]);
      db_value_clone (&key_value, node_info[BTREE_NODE_INFO_LAST_KEY]);
    }
  else
    {
      /* Empty node */
      db_value_clear (node_info[BTREE_NODE_INFO_FIRST_KEY]);
      DB_MAKE_NULL (node_info[BTREE_NODE_INFO_FIRST_KEY]);

      db_value_clear (node_info[BTREE_NODE_INFO_LAST_KEY]);
      DB_MAKE_NULL (node_info[BTREE_NODE_INFO_LAST_KEY]);
    }

  result = S_SUCCESS;

end:
  if (btns->crt_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, btns->crt_page);
    }

  if (crt_item != NULL)
    {
      free_and_init (crt_item);
    }
  return result;

error:
  result = S_ERROR;
  goto end;
}

static const char *
node_type_to_string (short node_type)
{
  return (node_type == BTREE_LEAF_NODE) ? "LEAF" : "NON_LEAF";
}

/*
 * btree_index_start_scan () -  start scan function for
 *                              show index header/capacity
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out): index header/capacity context
 */
int
btree_index_start_scan (THREAD_ENTRY * thread_p, int show_type,
			DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  int i, error = NO_ERROR;
  OID oid;
  OR_CLASSREP *classrep = NULL;
  int idx_in_cache;
  SHOW_INDEX_SCAN_CTX *ctx = NULL;
  LC_FIND_CLASSNAME status;
  OR_PARTITION *parts = NULL;
  int parts_count = 0;
  DB_CLASS_PARTITION_TYPE partition_type;
  char *class_name = NULL;

  *ptr = NULL;
  ctx =
    (SHOW_INDEX_SCAN_CTX *) db_private_alloc (thread_p,
					      sizeof (SHOW_INDEX_SCAN_CTX));
  if (ctx == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto cleanup;
    }
  memset (ctx, 0, sizeof (SHOW_INDEX_SCAN_CTX));

  ctx->show_type = show_type;
  ctx->is_all = (show_type == SHOWSTMT_ALL_INDEXES_HEADER
		 || show_type == SHOWSTMT_ALL_INDEXES_CAPACITY);

  class_name = db_get_string (arg_values[0]);

  status = xlocator_find_class_oid (thread_p, class_name, &oid, NULL_LOCK);
  if (status == LC_CLASSNAME_ERROR || status == LC_CLASSNAME_DELETED)
    {
      error = ER_LC_UNKNOWN_CLASSNAME;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, class_name);
      goto cleanup;
    }

  classrep = heap_classrepr_get (thread_p, &oid, NULL,
				 NULL_REPRID, &idx_in_cache);
  if (classrep == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto cleanup;
    }

  if (ctx->is_all)
    {
      assert (arg_cnt == 2);

      partition_type = (DB_CLASS_PARTITION_TYPE) db_get_int (arg_values[1]);
      ctx->indexes_count = classrep->n_indexes;
    }
  else
    {
      assert (arg_cnt == 3);

      /* get index name which user specified */
      ctx->index_name =
	db_private_strdup (thread_p, db_get_string (arg_values[1]));
      if (ctx->index_name == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto cleanup;
	}

      partition_type = (DB_CLASS_PARTITION_TYPE) db_get_int (arg_values[2]);
      ctx->indexes_count = 1;
    }

  /* save oids to context so that we can get btree info when scan next */
  if (partition_type == DB_PARTITIONED_CLASS)
    {
      error =
	heap_get_class_partitions (thread_p, &oid, &parts, &parts_count);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      ctx->class_oids =
	(OID *) db_private_alloc (thread_p, sizeof (OID) * parts_count);
      if (ctx->class_oids == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto cleanup;
	}

      for (i = 0; i < parts_count; i++)
	{
	  COPY_OID (&ctx->class_oids[i], &parts[i].class_oid);
	}

      ctx->class_oid_count = parts_count;
    }
  else
    {
      ctx->class_oids = (OID *) db_private_alloc (thread_p, sizeof (OID));
      if (ctx->class_oids == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto cleanup;
	}

      COPY_OID (&ctx->class_oids[0], &oid);
      ctx->class_oid_count = 1;
    }

  *ptr = ctx;
  ctx = NULL;

cleanup:

  if (classrep != NULL)
    {
      heap_classrepr_free (classrep, &idx_in_cache);
    }

  if (parts != NULL)
    {
      heap_clear_partition_info (thread_p, parts, parts_count);
    }

  if (ctx != NULL)
    {
      if (ctx->index_name != NULL)
	{
	  db_private_free_and_init (thread_p, ctx->index_name);
	}

      if (ctx->class_oids != NULL)
	{
	  db_private_free_and_init (thread_p, ctx->class_oids);
	}

      db_private_free_and_init (thread_p, ctx);
    }

  return error;
}

/*
 * btree_index_next_scan () -  next scan function for
 *                             show index header/capacity
 *   return: S_ERROR, S_SUCCESS, or S_END
 *
 *   thread_p(in):
 *   cursor(in):
 *   out_values(out):
 *   out_cnt(in):
 *   ptr(in): index header/capacity context
 */
SCAN_CODE
btree_index_next_scan (THREAD_ENTRY * thread_p, int cursor,
		       DB_VALUE ** out_values, int out_cnt, void *ptr)
{
  SCAN_CODE ret;
  BTID *btid_p;
  const char *btname;
  char *class_name = NULL;
  OR_CLASSREP *classrep = NULL;
  SHOW_INDEX_SCAN_CTX *ctx = NULL;
  OID *oid_p = NULL;
  int idx_in_cache;
  int selected_index = 0;
  int i, index_idx, oid_idx;

  ctx = (SHOW_INDEX_SCAN_CTX *) ptr;
  if (cursor >= ctx->indexes_count * ctx->class_oid_count)
    {
      return S_END;
    }

  assert (ctx->indexes_count >= 1);
  index_idx = cursor % ctx->indexes_count;
  oid_idx = cursor / ctx->indexes_count;

  oid_p = &ctx->class_oids[oid_idx];

  class_name = heap_get_class_name (thread_p, oid_p);
  if (class_name == NULL)
    {
      ret = S_ERROR;
      goto cleanup;
    }

  classrep = heap_classrepr_get (thread_p, oid_p, NULL,
				 NULL_REPRID, &idx_in_cache);
  if (classrep == NULL)
    {
      ret = S_ERROR;
      goto cleanup;
    }

  if (ctx->is_all)
    {
      btname = classrep->indexes[index_idx].btname;
      btid_p = &classrep->indexes[index_idx].btid;
    }
  else
    {
      selected_index = -1;
      for (i = 0; i < classrep->n_indexes; i++)
	{
	  if (intl_identifier_casecmp
	      (classrep->indexes[i].btname, ctx->index_name) == 0)
	    {
	      selected_index = i;
	      break;
	    }
	}

      if (selected_index == -1)
	{
	  /* it must be found since passed semantic check */
	  assert (false);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INDEX_NOT_FOUND,
		  0);
	  ret = S_ERROR;
	  goto cleanup;
	}

      btname = classrep->indexes[selected_index].btname;
      btid_p = &classrep->indexes[selected_index].btid;
    }

  if (ctx->show_type == SHOWSTMT_INDEX_HEADER
      || ctx->show_type == SHOWSTMT_ALL_INDEXES_HEADER)
    {
      ret =
	btree_scan_for_show_index_header (thread_p, out_values, out_cnt,
					  class_name, btname, btid_p);
    }
  else
    {
      assert (ctx->show_type == SHOWSTMT_INDEX_CAPACITY
	      || ctx->show_type == SHOWSTMT_ALL_INDEXES_CAPACITY);

      ret =
	btree_scan_for_show_index_capacity (thread_p, out_values, out_cnt,
					    class_name, btname, btid_p);
    }

cleanup:

  if (classrep != NULL)
    {
      heap_classrepr_free (classrep, &idx_in_cache);
    }

  if (class_name != NULL)
    {
      free_and_init (class_name);
    }

  return ret;
}

/*
 * btree_index_end_scan () -  end scan function
 *                            for show index header/capacity
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   ptr(in/out): index header/capacity context
 */
int
btree_index_end_scan (THREAD_ENTRY * thread_p, void **ptr)
{
  SHOW_INDEX_SCAN_CTX *ctx = NULL;

  ctx = (SHOW_INDEX_SCAN_CTX *) (*ptr);
  if (ctx != NULL)
    {
      if (ctx->index_name != NULL)
	{
	  db_private_free_and_init (thread_p, ctx->index_name);
	}

      if (ctx->class_oids != NULL)
	{
	  db_private_free_and_init (thread_p, ctx->class_oids);
	}

      db_private_free_and_init (thread_p, ctx);
    }

  *ptr = NULL;

  return NO_ERROR;
}

/*
 * btree_scan_for_show_index_header () - scan index header information
 *   return: S_ERROR, S_SUCCESS, or S_END
 *
 *   thread_p(in):
 *   out_values(out):
 *   out_cnt(in):
 *   class_name(in);
 *   index_name(in);
 *   btid_p(in);
 */
static SCAN_CODE
btree_scan_for_show_index_header (THREAD_ENTRY * thread_p,
				  DB_VALUE ** out_values, int out_cnt,
				  const char *class_name,
				  const char *index_name, BTID * btid_p)
{
  int idx = 0, root_level = 0;
  int error = NO_ERROR;
  VPID root_vpid;
  PAGE_PTR root_page_ptr = NULL;
  BTREE_ROOT_HEADER *root_header = NULL;
  BTREE_NODE_TYPE node_type;
  char buf[256] = { 0 };
  OR_BUF or_buf;
  TP_DOMAIN *key_type;
  int num_oids = 0, num_nulls = 0, num_keys = 0;

  /* get root header point */
  root_vpid.pageid = btid_p->root_pageid;
  root_vpid.volid = btid_p->vfid.volid;

  root_page_ptr =
    pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (root_page_ptr == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto cleanup;
    }

  root_header = btree_get_root_header (root_page_ptr);
  if (root_header == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto cleanup;
    }

  /* scan index header into out_values */
  error = db_make_string_copy (out_values[idx], class_name);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  error = db_make_string_copy (out_values[idx], index_name);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  (void) btid_to_string (buf, sizeof (buf), btid_p);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  (void) vpid_to_string (buf, sizeof (buf), &root_header->node.prev_vpid);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  (void) vpid_to_string (buf, sizeof (buf), &root_header->node.next_vpid);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  root_level = root_header->node.node_level;
  node_type = (root_level > 1) ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  error = db_make_string (out_values[idx], node_type_to_string (node_type));
  idx++;

  db_make_int (out_values[idx], root_header->node.max_key_len);
  idx++;

  if (root_header->unique_pk)
    {
      error =
	logtb_get_global_unique_stats (thread_p, btid_p, &num_oids,
				       &num_nulls, &num_keys);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      db_make_int (out_values[idx], num_oids);
      idx++;

      db_make_int (out_values[idx], num_nulls);
      idx++;

      db_make_int (out_values[idx], num_keys);
      idx++;
    }
  else
    {
      db_make_int (out_values[idx], root_header->num_oids);
      idx++;

      db_make_int (out_values[idx], root_header->num_nulls);
      idx++;

      db_make_int (out_values[idx], root_header->num_keys);
      idx++;
    }

  buf[0] = '\0';
  if (!OID_ISNULL (&root_header->topclass_oid))
    {
      oid_to_string (buf, sizeof (buf), &root_header->topclass_oid);
    }
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  db_make_int (out_values[idx], root_header->unique_pk);
  idx++;

  (void) vfid_to_string (buf, sizeof (buf), &root_header->ovfid);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  or_init (&or_buf, root_header->packed_key_domain, -1);
  key_type = or_get_domain (&or_buf, NULL, NULL);
  error =
    db_make_string_copy (out_values[idx],
			 pr_type_name (TP_DOMAIN_TYPE (key_type)));
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  assert (idx == out_cnt);

cleanup:

  if (root_page_ptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, root_page_ptr);
    }

  return (error == NO_ERROR) ? S_SUCCESS : S_ERROR;
}

/*
 * btree_key_find_first_visible_row () - MVCC find first visible row
 *   return: whether the visible row has been found
 *   btid(in): B+tree index identifier
 *   rec(in): Record descriptor
 *   offset(in): Offset of the second OID in key buffer
 *   node_type(in): node type
 *   oid(out): Object identifier of the visible row or NULL_OID
 *   class_oid(out): Object class identifier
 *   max_oids(in): max OIDs to search for
 */
static BTREE_SEARCH
btree_key_find_first_visible_row (THREAD_ENTRY * thread_p,
				  BTID_INT * btid_int, RECDES * rec,
				  int offset, BTREE_NODE_TYPE node_type,
				  OID * oid, OID * class_oid, int max_oids)
{
  PAGE_PTR pgptr = NULL, forward_pgptr = NULL;
  MVCC_REC_HEADER mvcc_rec_header;
  BTREE_MVCC_INFO mvcc_info;
  OR_BUF buf;
  int mvcc_flags = 0, length = 0;
  bool is_first = true;
  MVCC_SNAPSHOT mvcc_snapshot_dirty;
  int oids_count = 0;

  assert (btid_int != NULL && rec != NULL && rec->data != NULL
	  && oid != NULL && class_oid != NULL);

  OID_SET_NULL (oid);
  OID_SET_NULL (class_oid);
  mvcc_snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;

  length = rec->length;
  if (btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
    {
      length -= DB_ALIGN (DISK_VPID_SIZE, INT_ALIGNMENT);
    }

  or_init (&buf, rec->data, length);
  while (buf.ptr < buf.endptr)
    {
      /* Get MVCC flags */
      mvcc_flags = btree_record_object_get_mvcc_flags (buf.ptr);

      /* Read object OID */
      if (or_get_oid (&buf, oid) != NO_ERROR)
	{
	  goto error;
	}
      /* Clear flags */
      BTREE_OID_CLEAR_ALL_FLAGS (oid);

      if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	{
	  if (node_type == BTREE_OVERFLOW_NODE || !is_first
	      || btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_CLASS_OID))
	    {
	      /* Read class OID */
	      if (or_get_oid (&buf, class_oid) != NO_ERROR)
		{
		  goto error;
		}
	    }
	  else
	    {
	      /* Class OID is top class OID */
	      COPY_OID (class_oid, &btid_int->topclass_oid);
	    }
	}

      /* Get MVCC information */
      if (btree_or_get_mvccinfo (&buf, &mvcc_info, mvcc_flags) != NO_ERROR)
	{
	  goto error;
	}

      btree_mvcc_info_to_heap_mvcc_header (&mvcc_info, &mvcc_rec_header);
      if ((mvcc_snapshot_dirty).snapshot_fnc (thread_p, &mvcc_rec_header,
					      &mvcc_snapshot_dirty))
	{
	  /* visible row found it */
	  if (MVCCID_IS_VALID (mvcc_snapshot_dirty.lowest_active_mvccid)
	      || MVCCID_IS_VALID (mvcc_snapshot_dirty.
				  highest_completed_mvccid))
	    {
	      /* oid is modified by other active transaction */
	      return BTREE_ACTIVE_KEY_FOUND;
	    }
	  else
	    {
	      /* inserted by committed transaction */
	      return BTREE_KEY_FOUND;
	    }
	}

      if (max_oids > 0)
	{
	  oids_count++;
	  if (oids_count >= max_oids)
	    {
	      /* the maximum number of OIDs has been reached => key not found */
	      break;
	    }
	}

      if (node_type == BTREE_LEAF_NODE && is_first)
	{
	  /* Must skip over the key value to the next object */
	  or_seek (&buf, offset);
	}

      is_first = false;
    }

  return BTREE_KEY_NOTFOUND;

error:
  OID_SET_NULL (oid);
  OID_SET_NULL (class_oid);
  return BTREE_ERROR_OCCURRED;
}

/*
 * btree_insert_mvcc_delid_into_page () - Insert delete MVCCID info.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * btid (in)	      : B-tree info.
 * page_ptr (in)      : Leaf or overflow page.
 * node_type (in)     : Leaf or overflow node type.
 * key (in)	      : Key value.
 * insert_helper (in) : B-tree insert helper.
 * slot_id (in)	      : Slot ID for b-tree record.
 * rec (in)	      : B-tree record.
 * oid_offset (in)    : Offset to object being deleted.
 */
static int
btree_insert_mvcc_delid_into_page (THREAD_ENTRY * thread_p,
				   BTID_INT * btid, PAGE_PTR page_ptr,
				   BTREE_NODE_TYPE node_type,
				   DB_VALUE * key,
				   BTREE_INSERT_HELPER * insert_helper,
				   PGSLOTID slot_id, RECDES * rec,
				   int oid_offset)
{
  int ret = NO_ERROR;
  int oid_size = OR_OID_SIZE;
  int mvcc_delid_offset;
  bool have_mvcc_fixed_size = false;

  /* Recovery data. */
  LOG_DATA_ADDR addr;

  char *rv_undo_data = NULL;
  int rv_undo_data_length;
  int rv_undo_data_capacity = IO_MAX_PAGE_SIZE;
  char rv_undo_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  char *rv_undo_data_bufalign =
    PTR_ALIGN (rv_undo_data_buffer, BTREE_MAX_ALIGN);

  char rv_redo_data_buffer[BTREE_RV_REDO_BUFFER_SIZE + BTREE_MAX_ALIGN];
  char *rv_redo_data = PTR_ALIGN (rv_redo_data_buffer, BTREE_MAX_ALIGN);
  char *rv_redo_data_ptr = rv_redo_data;
  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (page_ptr != NULL);
  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_OVERFLOW_NODE);
  assert (key != NULL);
  assert (insert_helper != NULL);
  assert (slot_id > 0);
  assert (rec != NULL);
  assert (oid_offset >= 0);

  /* Prepare logging. */
  /* Initialize log address data */
  addr.pgptr = page_ptr;
  addr.offset = slot_id;
  addr.vfid = &btid->sys_btid->vfid;

  /* Undo logging. */
  rv_undo_data = rv_undo_data_bufalign;
  ret =
    btree_rv_save_keyval_for_undo (btid, key,
				   BTREE_INSERT_CLASS_OID (insert_helper),
				   BTREE_INSERT_OID (insert_helper),
				   BTREE_INSERT_MVCC_INFO (insert_helper),
				   BTREE_OP_INSERT_MVCC_DELID,
				   rv_undo_data_bufalign, &rv_undo_data,
				   &rv_undo_data_capacity,
				   &rv_undo_data_length);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* Redo logging. */
#if !defined (NDEBUG)
  /* For debugging recovery. */
  BTREE_RV_REDO_SET_DEBUG_INFO (&addr, rv_redo_data_ptr, btid,
				BTREE_RV_DEBUG_ID_INSERT_DELID);
#endif /* !NDEBUG */
  if (node_type == BTREE_OVERFLOW_NODE)
    {
      BTREE_RV_REDO_SET_OVERFLOW_NODE (&addr);
    }
  LOG_RV_RECORD_SET_MODIFY_MODE (&addr, LOG_RV_RECORD_UPDATE_PARTIAL);

  if (BTREE_IS_UNIQUE (btid->unique_pk))
    {
      if (node_type == BTREE_OVERFLOW_NODE || oid_offset > 0
	  || btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_CLASS_OID))
	{
	  /* unique index containing OID, CLASS OID */
	  oid_size += OR_OID_SIZE;
	}
    }

  mvcc_delid_offset = oid_offset + oid_size;
  if ((BTREE_IS_UNIQUE (btid->unique_pk) && oid_offset > 0)
      || (node_type == BTREE_OVERFLOW_NODE)
      || (btree_leaf_is_flaged (rec, BTREE_LEAF_RECORD_OVERFLOW_OIDS)
	  && oid_offset == 0))
    {
      have_mvcc_fixed_size = true;
      mvcc_delid_offset += OR_MVCCID_SIZE;
    }
  else if (btree_record_object_is_flagged (rec->data + oid_offset,
					   BTREE_OID_HAS_MVCC_INSID))
    {
      assert (node_type == BTREE_LEAF_NODE);
      mvcc_delid_offset += OR_MVCCID_SIZE;
    }

  /* Isn't the purpose of this function to always insert the MVCCID of
   * current transaction? Why not use logtb_get_current_mvccid?
   */
  if (have_mvcc_fixed_size
      || btree_record_object_is_flagged (rec->data + oid_offset,
					 BTREE_OID_HAS_MVCC_DELID))
    {
#if !defined(NDEBUG)
      MVCCID old_mvcc_delid;
      int mvcc_insid_size = 0;

      /* check that old MVCC DELID is NULL */
      if (have_mvcc_fixed_size
	  || btree_record_object_is_flagged (rec->data + oid_offset,
					     BTREE_OID_HAS_MVCC_INSID))
	{
	  mvcc_insid_size = OR_MVCCID_SIZE;
	}
      OR_GET_MVCCID (rec->data + oid_offset + oid_size + mvcc_insid_size,
		     &old_mvcc_delid);
      assert (!MVCCID_IS_VALID (old_mvcc_delid));
#endif

      btree_set_mvcc_delid (rec, mvcc_delid_offset,
			    &(BTREE_INSERT_MVCC_INFO (insert_helper)->
			      delete_mvccid), &rv_redo_data_ptr);
    }
  else
    {
      btree_add_mvcc_delid (rec, oid_offset, mvcc_delid_offset,
			    &(BTREE_INSERT_MVCC_INFO (insert_helper)->
			      delete_mvccid), &rv_redo_data_ptr);
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

#if !defined (NDEBUG)
  btree_check_valid_record (thread_p, btid, rec, node_type, key);
#endif

  if (spage_update (thread_p, page_ptr, slot_id, rec) != SP_SUCCESS)
    {
      goto exit_on_error;
    }

  if (insert_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Successfully added delete "
		     "MVCCID %llu to object %d|%d|%d, "
		     "class_oid %d|%d|%d, in %s page %d|%d slotid=%d, "
		     "lsa=%lld|%d, key=%s, in index (%d, %d|%d).\n",
		     insert_helper->obj_info.mvcc_info.delete_mvccid,
		     insert_helper->obj_info.oid.volid,
		     insert_helper->obj_info.oid.pageid,
		     insert_helper->obj_info.oid.slotid,
		     insert_helper->obj_info.class_oid.volid,
		     insert_helper->obj_info.class_oid.pageid,
		     insert_helper->obj_info.class_oid.slotid,
		     node_type == BTREE_OVERFLOW_NODE ? "overflow" : "leaf",
		     pgbuf_get_volume_id (page_ptr),
		     pgbuf_get_page_id (page_ptr), slot_id,
		     (long long int) pgbuf_get_lsa (page_ptr)->pageid,
		     (int) pgbuf_get_lsa (page_ptr)->offset,
		     insert_helper->printed_key != NULL ?
		     insert_helper->printed_key : "(unknown)",
		     btid->sys_btid->root_pageid,
		     btid->sys_btid->vfid.volid, btid->sys_btid->vfid.fileid);
    }

  /* Logging. */
  rv_redo_data_length = CAST_BUFLEN (rv_redo_data_ptr - rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  log_append_undoredo_data (thread_p, RVBT_MVCC_DELETE_OBJECT, &addr,
			    rv_undo_data_length, rv_redo_data_length,
			    rv_undo_data, rv_redo_data);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, page_ptr, DONT_FREE);

  if (rv_undo_data != NULL && rv_undo_data != rv_undo_data_bufalign)
    {
      db_private_free_and_init (thread_p, rv_undo_data);
    }

  return NO_ERROR;

exit_on_error:

  if (rv_undo_data != NULL && rv_undo_data != rv_undo_data_bufalign)
    {
      db_private_free_and_init (thread_p, rv_undo_data);
    }

  return ret;
}

/*
 * btree_set_mvcc_header_ids_for_update () - set ids of mvcc header for update
 *   return: nothing
 *   thread_p(in): thread entry
 *   do_delete_only(in):	true, if need to set del_id only
 *   do_insert_only(in): true, if need to set ins_id only
 *   mvcc_id(in): mvcc id to set
 *   mvcc_rec_header(in):  mvcc record header
 *
 * Note: do_delete_only and do_insert_only can't be both true
 */
void
btree_set_mvcc_header_ids_for_update (THREAD_ENTRY * thread_p,
				      bool do_delete_only,
				      bool do_insert_only,
				      MVCCID * mvcc_id,
				      MVCC_REC_HEADER * mvcc_rec_header)
{
  assert (mvcc_rec_header != NULL);
  assert (do_delete_only == false || do_insert_only == false);

  BTREE_INIT_MVCC_HEADER (&mvcc_rec_header[0]);
  if (do_delete_only == false && do_insert_only == false)
    {
      MVCC_SET_FLAG_BITS (&mvcc_rec_header[0], OR_MVCC_FLAG_VALID_DELID);
      MVCC_SET_DELID (&mvcc_rec_header[0], *mvcc_id);

      BTREE_INIT_MVCC_HEADER (&mvcc_rec_header[1]);
      MVCC_SET_FLAG_BITS (&mvcc_rec_header[1], OR_MVCC_FLAG_VALID_INSID);
      MVCC_SET_INSID (&mvcc_rec_header[1], *mvcc_id);

      return;
    }

  if (do_delete_only == true)
    {
      MVCC_SET_FLAG_BITS (&mvcc_rec_header[0], OR_MVCC_FLAG_VALID_DELID);
      MVCC_SET_DELID (&mvcc_rec_header[0], *mvcc_id);

      return;
    }

  /* insert only case */
  MVCC_SET_FLAG_BITS (&mvcc_rec_header[0], OR_MVCC_FLAG_VALID_INSID);
  MVCC_SET_INSID (&mvcc_rec_header[0], *mvcc_id);
}

/*
 * btree_unpack_mvccinfo () - Check b-tree MVCC flags and unpack any MVCC info
 *			      into MVCC header.
 *
 * return		 : Pointer after the packed MVCC info.
 * ptr (in)		 : Pointer to packed MVCC info.
 * mvcc_info (out)	 : Outputs MVCC info.
 * btree_mvcc_flags (in) : Flags that describe the packed MVCC info.
 */
char *
btree_unpack_mvccinfo (char *ptr, BTREE_MVCC_INFO * mvcc_info,
		       short btree_mvcc_flags)
{
  assert (mvcc_info != NULL && ptr != NULL);

  mvcc_info->flags = btree_mvcc_flags;
  mvcc_info->insert_mvccid = MVCCID_ALL_VISIBLE;
  mvcc_info->delete_mvccid = MVCCID_NULL;

  if (BTREE_MVCC_INFO_HAS_INSID (mvcc_info))
    {
      /* Get insert MVCCID */
      ptr = or_unpack_mvccid (ptr, &mvcc_info->insert_mvccid);
    }

  if (BTREE_MVCC_INFO_HAS_DELID (mvcc_info))
    {
      /* Get delete MVCCID */
      ptr = or_unpack_mvccid (ptr, &mvcc_info->delete_mvccid);
    }

  return ptr;
}

/*
 * btree_pack_mvccinfo () - Pack MVCC information into b-tree record.
 *
 * return	  : Pointer after the packed MVCC information.
 * ptr (in)	  : Pointer where MVCC information will be packed.
 * mvcc_info (in) : MVCC information (saved as a record header).
 */
char *
btree_pack_mvccinfo (char *ptr, BTREE_MVCC_INFO * mvcc_info)
{
  if (mvcc_info == NULL)
    {
      /* No MVCC info to pack */
      return ptr;
    }
  if (BTREE_MVCC_INFO_HAS_INSID (mvcc_info))
    {
      ptr = or_pack_mvccid (ptr, mvcc_info->insert_mvccid);
    }
  if (BTREE_MVCC_INFO_HAS_DELID (mvcc_info))
    {
      ptr = or_pack_mvccid (ptr, mvcc_info->delete_mvccid);
    }
  return ptr;
}

/*
 * btree_packed_mvccinfo_size () - Packed MVCC info size.
 *
 * return	  : Packed MVCC info size.
 * mvcc_info (in) : MVCC info.
 */
int
btree_packed_mvccinfo_size (BTREE_MVCC_INFO * mvcc_info)
{
  int size = 0;
  if (mvcc_info == NULL)
    {
      /* Nothing to pack */
      return size;
    }
  if (BTREE_MVCC_INFO_HAS_INSID (mvcc_info))
    {
      size += OR_MVCCID_SIZE;
    }
  if (BTREE_MVCC_INFO_HAS_DELID (mvcc_info))
    {
      size += OR_MVCCID_SIZE;
    }
  return size;
}

/*
 * btree_or_get_mvccinfo () - Check b-tree MVCC flags and unpack any MVCC info
 *			      into MVCC header.
 *
 * return		 : Error code.
 * buf (in/out)		 : OR Buffer.
 * mvcc_info (out)	 : MVCC Record header.
 * btree_mvcc_flags (in) : Flags that describe the packed MVCC info.
 */
static int
btree_or_get_mvccinfo (OR_BUF * buf, BTREE_MVCC_INFO * mvcc_info,
		       short btree_mvcc_flags)
{
  int size = BTREE_GET_MVCC_INFO_SIZE_FROM_FLAGS (btree_mvcc_flags);
  if (buf->ptr + size > buf->endptr)
    {
      /* Overflow error */
      return or_overflow (buf);
    }
  /* Unpack and update pointer */
  buf->ptr = btree_unpack_mvccinfo (buf->ptr, mvcc_info, btree_mvcc_flags);
  return NO_ERROR;
}

/*
 * btree_or_put_mvccinfo () - Set MVCC information into buffer (should be used
 *			      for b-tree records). Only insert/delete MVCCID's
 *			      will be set depending on MVCC flags.
 *
 * return	  : Error code.
 * buf (in/out)	  : OR Buffer.
 * mvcc_info (in) : MVCC info (saved as record header).
 */
static int
btree_or_put_mvccinfo (OR_BUF * buf, BTREE_MVCC_INFO * mvcc_info)
{
  int error_code = NO_ERROR;

  if (BTREE_MVCC_INFO_HAS_INSID (mvcc_info))
    {
      error_code = or_put_mvccid (buf, mvcc_info->insert_mvccid);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
  if (BTREE_MVCC_INFO_HAS_DELID (mvcc_info))
    {
      error_code = or_put_mvccid (buf, mvcc_info->delete_mvccid);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
  return error_code;
}

/*
 * btree_unpack_object () - Unpack a b-tree object from the given pointer.
 *			    Pointer should belong to a b-tree record.
 *
 * return		 : Error code.
 * ptr (in)		 : Pointer in b-tree record to unpack object.
 * btid_int (in)	 : B-tree info.
 * node_type (in)	 : Leaf or overflow node type.
 * record (in)		 : B-tree record.
 * after_key_offset (in) : Offset in record after packed key.
 * oid (out)		 : Unpacked OID.
 * class_oid (out)	 : Unpacked class OID.
 * mvcc_info (out)	 : Unpacked MVCC info.
 */
static char *
btree_unpack_object (char *ptr, BTID_INT * btid_int,
		     BTREE_NODE_TYPE node_type, RECDES * record,
		     int after_key_offset, OID * oid, OID * class_oid,
		     BTREE_MVCC_INFO * mvcc_info)
{
  OR_BUF buffer;

  BTREE_RECORD_OR_BUF_INIT (buffer, record);
  buffer.ptr = ptr;

  if (btree_or_get_object (&buffer, btid_int, node_type, after_key_offset,
			   oid, class_oid, mvcc_info) != NO_ERROR)
    {
      assert (false);
      return NULL;
    }
  return buffer.ptr;
}

/*
 * btree_pack_object () - Pack a b-tree object into the given pointer.
 *			  Pointer should belong to a b-tree record.
 *
 * return		 : Error code.
 * ptr (in)		 : Pointer in b-tree record to pack object.
 * btid_int (in)	 : B-tree info.
 * node_type (in)	 : Leaf or overflow node type.
 * record (in)		 : B-tree record.
 * oid (out)		 : OID to pack.
 * class_oid (out)	 : Class OID to pack.
 * mvcc_info (out)	 : MVCC info to pack.
 */
static char *
btree_pack_object (char *ptr, BTID_INT * btid_int, BTREE_NODE_TYPE node_type,
		   RECDES * record, OID * oid, OID * class_oid,
		   BTREE_MVCC_INFO * mvcc_info)
{
  OR_BUF buffer;

  OR_BUF_INIT (buffer, record->data, record->area_size);
  buffer.ptr = ptr;

  if (btree_or_put_object (&buffer, btid_int, node_type, oid, class_oid,
			   mvcc_info) != NO_ERROR)
    {
      assert (false);
      return NULL;
    }
  return buffer.ptr;
}

/*
 * btree_or_get_object () - Get object, class OID and its MVCC info from
 *			    buffer pointing in a b-tree record.
 *
 * return		  : Error code.
 * buf (in/out)		  : Buffer pointing to object in b-tree record.
 * btid_int (in)	  : B-tree info.
 * node_type (in)	  : Leaf or overflow node type.
 * after_key_offset (int) : Offset to end of packed key for leaf records.
 * oid (out)		  : Outputs OID of object.
 * class_oid (out)	  : Outputs OID of object's class.
 * mvcc_info (out)	  : Outputs MVCC info for object.
 *
 * NOTE: Buffer.buffer should point to start of b-tree record.
 * NOTE: Buffer pointer will be moved after read object.
 *	 If object is first in leaf record, buffer pointer will be moved after
 *	 the packed key (where second objects starts).
 */
static int
btree_or_get_object (OR_BUF * buf, BTID_INT * btid_int,
		     BTREE_NODE_TYPE node_type, int after_key_offset,
		     OID * oid, OID * class_oid, BTREE_MVCC_INFO * mvcc_info)
{
  short mvcc_flags = 0;		/* MVCC flags read from object OID. */
  int error_code = NO_ERROR;	/* Error code. */
  bool is_first_of_leaf;	/* True if the object is first in leaf
				 * record.
				 */

  /* Assert arguments meet expectations. */
  assert (buf != NULL);
  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_OVERFLOW_NODE);
  /* Should any of these be optional? */
  assert (oid != NULL);
  assert (class_oid != NULL);
  assert (mvcc_info != NULL);

  /* Assert buffer has expected alignment. */
  ASSERT_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* Is this the first object of leaf record? */
  is_first_of_leaf = buf->ptr == buf->buffer && node_type == BTREE_LEAF_NODE;

  /* Read MVCC flags. */
  mvcc_flags = btree_record_object_get_mvcc_flags (buf->ptr);

  /* Get OID. */
  error_code = or_get_oid (buf, oid);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* Get/set class OID. If this is the first object in leaf record and
       * if record is not marked with BTREE_LEAF_RECORD_CLASS_OID, class
       * OID must be top class OID.
       * Otherwise, read it from record.
       */
      if (is_first_of_leaf
	  && !BTREE_OID_IS_RECORD_FLAG_SET (oid, BTREE_LEAF_RECORD_CLASS_OID))
	{
	  COPY_OID (class_oid, &btid_int->topclass_oid);
	}
      else
	{
	  error_code = or_get_oid (buf, class_oid);
	  if (error_code != NO_ERROR)
	    {
	      assert (false);
	      return error_code;
	    }
	}
    }
  else
    {
      /* Non-unique indexes can be part only of top class. */
      COPY_OID (class_oid, &btid_int->topclass_oid);
    }

  /* Clear all flags from object. */
  BTREE_OID_CLEAR_ALL_FLAGS (oid);

  /* Read MVCC info */
  error_code = btree_or_get_mvccinfo (buf, mvcc_info, mvcc_flags);
  if (error_code != NO_ERROR)
    {
      assert (false);
      return error_code;
    }

  if (is_first_of_leaf)
    {
      /* Advance after the first key. */
      error_code = or_seek (buf, after_key_offset);
      if (error_code != NO_ERROR)
	{
	  assert (false);
	  return error_code;
	}
    }

  /* Successful read. */
  return NO_ERROR;
}

/*
 * btree_or_put_object () - Put object data in buffer (of b-tree record).
 *
 * return	  : Error code.
 * buf (in/out)	  : Buffer pointing to destination of object data.
 * btid_int (in)  : B-tree info.
 * node_type (in) : Leaf or overflow node type.
 * oid (in)	  : OID of object.
 * class_oid (in) : OID of object's class.
 * mvcc_info (in) : MVCC info of object.
 *
 * NOTE: Buffer will point at the end of packed object after execution.
 */
static int
btree_or_put_object (OR_BUF * buf, BTID_INT * btid_int,
		     BTREE_NODE_TYPE node_type,
		     OID * oid, OID * class_oid, BTREE_MVCC_INFO * mvcc_info)
{
  bool is_first_of_leaf;	/* True if object is first in a leaf record. */
  OID flagged_oid;		/* OID of object including flags. */
  int error_code = NO_ERROR;	/* Error code. */

  /* Assert expected arguments. */
  assert (oid != NULL);
  assert (!BTREE_IS_UNIQUE (btid_int->unique_pk) || class_oid != NULL);

  /* Is this the first object of leaf record? */
  is_first_of_leaf =
    (buf->ptr == buf->buffer) && node_type == BTREE_LEAF_NODE;

  /* Set MVCC flags into OID. */
  COPY_OID (&flagged_oid, oid);
  BTREE_OID_SET_MVCC_FLAG (&flagged_oid, mvcc_info->flags);

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* Class OID may have to be packed. */
      if (is_first_of_leaf)
	{
	  if (OID_EQ (&btid_int->topclass_oid, class_oid))
	    {
	      /* Don't add class oid. Top class OID will be considered as
	       * default.
	       */
	      error_code = or_put_oid (buf, &flagged_oid);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }
	  else
	    {
	      /* Flag object with BTREE_LEAF_RECORD_CLASS_OID and also add
	       * class OID.
	       */
	      flagged_oid.slotid |= BTREE_LEAF_RECORD_CLASS_OID;
	      error_code = or_put_oid (buf, &flagged_oid);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	      error_code = or_put_oid (buf, class_oid);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }
	}
      else
	{
	  /* Add oid and class OID. */
	  error_code = or_put_oid (buf, &flagged_oid);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  error_code = or_put_oid (buf, class_oid);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	}
    }
  else
    {
      /* Add OID only */
      error_code = or_put_oid (buf, &flagged_oid);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }

  /* Add MVCC info */
  error_code = btree_or_put_mvccinfo (buf, mvcc_info);
  return error_code;
}

/*
 * btree_set_mvcc_flags_into_oid () - Set MVCC info flags in the volid field
 *				      of OID.
 *
 * return	      : Void.
 * p_mvcc_header (in) : MVCC info.
 * oid (in/out)	      : Object identifier.
 */
void
btree_set_mvcc_flags_into_oid (MVCC_REC_HEADER * p_mvcc_header, OID * oid)
{
  if (p_mvcc_header == NULL)
    {
      /* No flag to set */
      return;
    }
  if (MVCC_IS_FLAG_SET (p_mvcc_header, OR_MVCC_FLAG_VALID_INSID))
    {
      oid->volid |= BTREE_OID_HAS_MVCC_INSID;
    }
  if (MVCC_IS_FLAG_SET (p_mvcc_header, OR_MVCC_FLAG_VALID_DELID))
    {
      oid->volid |= BTREE_OID_HAS_MVCC_DELID;
    }
}

/*
 * btree_clear_mvcc_flags_from_oid () -
 *
 * return	      : Void.
 * oid (in/out)	      : Object identifier.
 */
void
btree_clear_mvcc_flags_from_oid (OID * oid)
{
  oid->volid &= ~BTREE_OID_MVCC_FLAGS_MASK;
}

/*
 * btree_compare_btids () - B-tree identifier comparator.
 *
 * return	  : Positive value is the first identifier is bigger,
 *		    negative if the second identifier is bigger and 0 if the
 *		    identifiers are equal.
 * mem_btid1 (in) : Pointer to first btid value.
 * mem_btid2 (in) : Pointer to second btid value.
 */
int
btree_compare_btids (const void *mem_btid1, const void *mem_btid2)
{
  const BTID *btid1 = (const BTID *) mem_btid1;
  const BTID *btid2 = (const BTID *) mem_btid2;
  if (btid1 == btid2)
    {
      return 0;
    }

  if (btid1->root_pageid > btid2->root_pageid)
    {
      return 1;
    }
  else if (btid1->root_pageid < btid2->root_pageid)
    {
      return -1;
    }

  if (btid1->vfid.fileid > btid2->vfid.fileid)
    {
      return 1;
    }
  else if (btid1->vfid.fileid < btid2->vfid.fileid)
    {
      return -1;
    }

  if (btid1->vfid.volid > btid2->vfid.volid)
    {
      return 1;
    }
  else if (btid1->vfid.volid < btid2->vfid.volid)
    {
      return -1;
    }

  return 0;
}

/*
 * btree_check_valid_record () - Check that record data is valid.
 *
 * return		: Error code.
 * thread_p (in)	: Thread entry.
 * btid (in)		: B-tree data.
 * recp (in)		: Record descriptor.
 * node_type (in)	: Node type (overflow or leaf).
 * key (in)		: Expected key value (will be checked if not null,
 *			  and if node type is leaf and if key doesn't have
 *			  overflow pages).
 */
int
btree_check_valid_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
			  RECDES * recp, BTREE_NODE_TYPE node_type,
			  DB_VALUE * key)
{
#define BTREE_CHECK_VALID_PRINT_REC_MAX_LENGTH 1024
  OID oid, class_oid;
  MVCCID mvccid;
  int vpid_size = 0;
  OR_BUF buffer;
  short mvcc_flags;
  bool is_first_oid = true;
  bool has_fixed_size = false;
  bool has_overflow_pages = false;
  VPID first_overflow_vpid = VPID_INITIALIZER;

  if (btid == NULL || btid->key_type == NULL)
    {
      /* We don't have access to b-tree information to check the record. */
      return NO_ERROR;
    }

  if (btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
    {
      char *vpid_ptr = NULL;

      has_overflow_pages = true;
      vpid_size = DISK_VPID_ALIGNED_SIZE;

      vpid_ptr = recp->data + recp->length - vpid_size;
      OR_GET_VPID (vpid_ptr, &first_overflow_vpid);
      if (!log_is_in_crash_recovery ()
	  && pgbuf_is_valid_page (thread_p, &first_overflow_vpid, true, NULL,
				  NULL) == DISK_INVALID)
	{
	  assert (false);
	  return ER_FAILED;
	}
    }

  or_init (&buffer, recp->data, recp->length - vpid_size);
  while (buffer.ptr < buffer.endptr)
    {
      /* Get mvcc flags */
      mvcc_flags = btree_record_object_get_mvcc_flags (buffer.ptr);
      /* If MVCC is enabled, there are several cases when the object entry
       * must have fixed size, which means that insert/delete MVCCID must be
       * present:
       * 1. Overflow objects.
       * 2. First object if leaf record if there are overflow OID's.
       * 3. Any non-first object if index is unique.
       */
      has_fixed_size =
	((node_type == BTREE_OVERFLOW_NODE)
	 || (has_overflow_pages && is_first_oid)
	 || (BTREE_IS_UNIQUE (btid->unique_pk) && !is_first_oid));
      if (has_fixed_size)
	{
	  assert ((mvcc_flags & BTREE_OID_HAS_MVCC_INSID)
		  && (mvcc_flags & BTREE_OID_HAS_MVCC_DELID));
	}
      /* Get and check OID */
      if (or_get_oid (&buffer, &oid) != NO_ERROR)
	{
	  assert (false);
	  return ER_FAILED;
	}
      BTREE_OID_CLEAR_ALL_FLAGS (&oid);
      if (oid.pageid <= 0 || oid.slotid <= 0
	  || oid.slotid > ((short) (DB_PAGESIZE / sizeof (PGSLOTID)))
	  || oid.volid < 0)
	{
	  assert (false);
	  return ER_FAILED;
	}
      if (BTREE_IS_UNIQUE (btid->unique_pk)
	  && (node_type == BTREE_OVERFLOW_NODE || !is_first_oid
	      || btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_CLASS_OID)))
	{
	  /* Get and check class OID */
	  if (or_get_oid (&buffer, &class_oid) != NO_ERROR)
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	  if (class_oid.pageid <= 0 || class_oid.slotid <= 0
	      || class_oid.slotid >
	      ((short) (DB_PAGESIZE / sizeof (PGSLOTID)))
	      || class_oid.volid < 0)
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	}
      if (mvcc_flags & BTREE_OID_HAS_MVCC_INSID)
	{
	  /* Get and check insert MVCCID */
	  if (or_get_mvccid (&buffer, &mvccid) != NO_ERROR)
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	  if (!MVCCID_IS_VALID (mvccid))
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	  if (!mvcc_id_precedes (mvccid, log_Gl.hdr.mvcc_next_id)
	      && !log_is_in_crash_recovery ())
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	}
      if (mvcc_flags & BTREE_OID_HAS_MVCC_DELID)
	{
	  /* Get and check delete MVCCID */
	  if (or_get_mvccid (&buffer, &mvccid) != NO_ERROR)
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	  if (mvccid != MVCCID_NULL
	      && !mvcc_id_precedes (mvccid, log_Gl.hdr.mvcc_next_id)
	      && !log_is_in_crash_recovery ())
	    {
	      assert (false);
	      return ER_FAILED;
	    }
	}
      if (is_first_oid && (node_type == BTREE_LEAF_NODE))
	{
	  /* Key value is also saved */
	  if (!btree_leaf_is_flaged (recp, BTREE_LEAF_RECORD_OVERFLOW_KEY))
	    {
	      /* Get key value */
	      DB_VALUE rec_key_value;
	      TP_DOMAIN *key_domain = NULL;
	      PR_TYPE *pr_type = NULL;

	      DB_MAKE_NULL (&rec_key_value);
	      key_domain = btid->key_type;
	      pr_type = key_domain->type;
	      if ((*(pr_type->index_readval)) (&buffer, &rec_key_value,
					       key_domain, -1, true, NULL, 0)
		  != NO_ERROR)
		{
		  assert (false);
		  return ER_FAILED;
		}
	      if (key != NULL
		  && btree_compare_key (key, &rec_key_value, key_domain, 1, 1,
					NULL) != 0)
		{
		  /* Expected key is not the same with the key found in record
		   * data.
		   */
		  /* This is possible when key fence is used. Should disable
		   * this verification or should include the fence for compare
		   */
		  /* For now, do nothing */
		}
	      db_value_clear (&rec_key_value);
	    }
	  else
	    {
	      /* Skip overflow key vpid */
	      buffer.ptr += DISK_VPID_SIZE;
	    }
	  buffer.ptr = PTR_ALIGN (buffer.ptr, OR_INT_SIZE);
	}
      is_first_oid = false;
    }
  if (buffer.ptr != buffer.endptr)
    {
      assert (false);
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * btree_scan_for_show_index_capacity () - scan index capacity information
 *   return: S_ERROR, S_SUCCESS, or S_END
 *
 *   thread_p(in):
 *   out_values(out):
 *   out_cnt(in):
 *   class_name(in);
 *   index_name(in);
 *   btid_p(in);
 */
static SCAN_CODE
btree_scan_for_show_index_capacity (THREAD_ENTRY * thread_p,
				    DB_VALUE ** out_values, int out_cnt,
				    const char *class_name,
				    const char *index_name, BTID * btid_p)
{
  int idx = 0;
  int error = NO_ERROR;
  BTREE_CAPACITY cpc;
  PAGE_PTR root_page_ptr = NULL;
  VPID root_vpid;
  char buf[256] = { 0 };

  /* get btree capacity */
  root_vpid.pageid = btid_p->root_pageid;
  root_vpid.volid = btid_p->vfid.volid;
  root_page_ptr =
    pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (root_page_ptr == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto cleanup;
    }

  error = btree_index_capacity (thread_p, btid_p, &cpc);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  /* scan index capacity into out_values */
  error = db_make_string_copy (out_values[idx], class_name);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  error = db_make_string_copy (out_values[idx], index_name);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  (void) btid_to_string (buf, sizeof (buf), btid_p);
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  db_make_int (out_values[idx], cpc.dis_key_cnt);
  idx++;

  db_make_int (out_values[idx], cpc.tot_val_cnt);
  idx++;

  db_make_int (out_values[idx], cpc.avg_val_per_key);
  idx++;

  db_make_int (out_values[idx], cpc.leaf_pg_cnt);
  idx++;

  db_make_int (out_values[idx], cpc.nleaf_pg_cnt);
  idx++;

  db_make_int (out_values[idx], cpc.tot_pg_cnt);
  idx++;

  db_make_int (out_values[idx], cpc.height);
  idx++;

  db_make_int (out_values[idx], cpc.avg_key_len);
  idx++;

  db_make_int (out_values[idx], cpc.avg_rec_len);
  idx++;

  (void) util_byte_to_size_string (buf, 64, (UINT64) (cpc.tot_space));
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  (void) util_byte_to_size_string (buf, 64, (UINT64) (cpc.tot_used_space));
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  (void) util_byte_to_size_string (buf, 64, (UINT64) (cpc.tot_free_space));
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  db_make_int (out_values[idx], cpc.avg_pg_key_cnt);
  idx++;

  (void) util_byte_to_size_string (buf, 64, (UINT64) (cpc.avg_pg_free_sp));
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  assert (idx == out_cnt);

cleanup:

  if (root_page_ptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, root_page_ptr);
    }

  return (error == NO_ERROR) ? S_SUCCESS : S_ERROR;
}

static bool
btree_leaf_lsa_eq (THREAD_ENTRY * thread_p, LOG_LSA * a, LOG_LSA * b)
{
  assert (a != NULL);
  assert (b != NULL);

#if !defined(SERVER_MODE)
  assert_release (LSA_EQ (a, b));
#endif

  return LSA_EQ (a, b) ? true : false;
}

/*
 * btree_key_find_first_visible_row_from_all_ovf () - MVCC find first visible
 *						    row in OID overflow pages
 *   return: whether the visible row has been found
 *   btid_int(in): B+tree index identifier
 *   first_ovfl_vpid(in): First overflow vpid
 *   oid(out): Object identifier of the visible row or NULL_OID
 *   class_oid(out): Object class identifier
 */
static BTREE_SEARCH
btree_key_find_first_visible_row_from_all_ovf (THREAD_ENTRY * thread_p,
					       BTID_INT * btid_int,
					       VPID * first_ovfl_vpid,
					       OID * oid, OID * class_oid)
{
  RECDES ovfl_copy_rec;
  char ovfl_copy_rec_buf[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  VPID next_ovfl_vpid;
  PAGE_PTR ovfl_page = NULL;
  BTREE_SEARCH result = BTREE_KEY_NOTFOUND;

  assert (btid_int != NULL);
  assert (first_ovfl_vpid != NULL);
  assert (oid != NULL && class_oid != NULL);

  ovfl_copy_rec.area_size = DB_PAGESIZE;
  ovfl_copy_rec.data = PTR_ALIGN (ovfl_copy_rec_buf, BTREE_MAX_ALIGN);
  next_ovfl_vpid = *first_ovfl_vpid;

  /* find first visible OID into overflow page */
  while (!VPID_ISNULL (&next_ovfl_vpid))
    {
      ovfl_page = pgbuf_fix (thread_p, &next_ovfl_vpid, OLD_PAGE,
			     PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (ovfl_page == NULL)
	{
	  ASSERT_ERROR ();
	  goto error;
	}

      (void) pgbuf_check_page_ptype (thread_p, ovfl_page, PAGE_BTREE);

      if (spage_get_record (ovfl_page, 1, &ovfl_copy_rec, COPY) != S_SUCCESS)
	{
	  goto error;
	}
      assert (ovfl_copy_rec.length % 4 == 0);

      result = btree_key_find_first_visible_row (thread_p, btid_int,
						 &ovfl_copy_rec, 0,
						 BTREE_OVERFLOW_NODE,
						 oid, class_oid, -1);
      if (result == BTREE_ERROR_OCCURRED)
	{
	  goto error;
	}
      else if (result != BTREE_KEY_NOTFOUND)
	{
	  pgbuf_unfix_and_init (thread_p, ovfl_page);
	  return result;
	}

      btree_get_next_overflow_vpid (ovfl_page, &next_ovfl_vpid);
      pgbuf_unfix_and_init (thread_p, ovfl_page);
    }

  return BTREE_KEY_NOTFOUND;

error:

  if (ovfl_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, ovfl_page);
    }

  return BTREE_ERROR_OCCURRED;
}

/*
 * btree_rv_undo_global_unique_stats_commit () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Decrement the in-memory global unique statistics.
 */
int
btree_rv_undo_global_unique_stats_commit (THREAD_ENTRY * thread_p,
					  LOG_RCV * recv)
{
  char *datap;
  int num_nulls, num_oids, num_keys;
  BTID btid;

  assert (recv->length >= (3 * OR_INT_SIZE) + OR_BTID_ALIGNED_SIZE);

  /* unpack the root statistics */
  datap = (char *) recv->data;

  OR_GET_BTID (datap, &btid);
  datap += OR_BTID_ALIGNED_SIZE;

  num_nulls = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  num_oids = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  num_keys = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  /* Because this log record is logical, it will be processed even if the B-tree
   * was deleted. If the B-tree was deleted then skip update of unique
   * statistics in global hash.
   */
  if (log_Gl.rcv_phase == LOG_RECOVERY_UNDO_PHASE)
    {
      /* Only in recovery this is possible */
      if (disk_isvalid_page (thread_p, btid.vfid.volid, btid.root_pageid) !=
	  DISK_VALID)
	{
	  /* The B-tree was already deleted */
	  return NO_ERROR;
	}
    }
  else
    {
      /* This should not happen */
      assert (disk_isvalid_page (thread_p, btid.vfid.volid, btid.root_pageid)
	      == DISK_VALID);
    }
  if (logtb_update_global_unique_stats_by_delta
      (thread_p, &btid, -num_oids, -num_nulls, -num_keys, false) != NO_ERROR)
    {
      goto error;
    }

  return NO_ERROR;

error:
  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_rv_redo_global_unique_stats_commit () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Recover the in-memory global unique statistics.
 */
int
btree_rv_redo_global_unique_stats_commit (THREAD_ENTRY * thread_p,
					  LOG_RCV * recv)
{
  char *datap;
  int num_nulls, num_oids, num_keys;
  BTID btid;

  assert (recv->length >= (3 * OR_INT_SIZE) + OR_BTID_ALIGNED_SIZE);

  /* unpack the root statistics */
  datap = (char *) recv->data;

  OR_GET_BTID (datap, &btid);
  datap += OR_BTID_ALIGNED_SIZE;

  num_nulls = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  num_oids = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  num_keys = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  /* Because this log record is logical, it will be processed even if the B-tree
   * was deleted. If the B-tree was deleted then skip update of unique
   * statistics in global hash.
   */
  if (disk_isvalid_page (thread_p, btid.vfid.volid, btid.root_pageid) !=
      DISK_VALID)
    {
      /* The B-tree was already deleted */
      return NO_ERROR;
    }
  if (logtb_update_global_unique_stats_by_abs
      (thread_p, &btid, num_oids, num_nulls, num_keys, false) != NO_ERROR)
    {
      goto error;
    }

  return NO_ERROR;

error:
  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_search_key_and_apply_functions () - B-tree internal function to
 *					     traverse the tree in the
 *					     direction given by a key and
 *					     calling three types of function:
 *					     one to fix/handle root page,
 *					     one on the traversed nodes and
 *					     one on the leaf node pointed
 *					     by key.
 *
 * return		     : Error code.
 * thread_p (in)	     : Thread entry.
 * btid (in)		     : B-tree identifier.
 * btid_int (out)	     : Output b-tree info if not NULL.
 * key (in)		     : Search key value.
 * root_function (in)	     : Function called to fix/process root node.
 * root_args (in/out)	     : Arguments for root function.
 * advance_function (in)     : Function called to advance and process nodes
 *			       discovered nodes.
 * advance_args (in/out)     : Arguments for advance function.
 * key_function (in)	     : Function to process key record (and its leaf
 *			       and overflow nodes).
 * process_key_args (in/out) : Arguments for key function.
 * search_key (out)	     : Search key result.
 * leaf_page_ptr (out)	     : If not NULL, it will output the leaf node page
 *			       where key lead the search.
 */
static int
btree_search_key_and_apply_functions (THREAD_ENTRY * thread_p, BTID * btid,
				      BTID_INT * btid_int, DB_VALUE * key,
				      BTREE_ROOT_WITH_KEY_FUNCTION *
				      root_function,
				      void *root_args,
				      BTREE_ADVANCE_WITH_KEY_FUNCTION *
				      advance_function,
				      void *advance_args,
				      BTREE_PROCESS_KEY_FUNCTION *
				      key_function,
				      void *process_key_args,
				      BTREE_SEARCH_KEY_HELPER * search_key,
				      PAGE_PTR * leaf_page_ptr)
{
  PAGE_PTR crt_page = NULL;	/* Currently fixed page. */
  PAGE_PTR advance_page = NULL;	/* Next level page. */
  int error_code = NO_ERROR;	/* Error code. */
  BTID_INT local_btid_int;	/* Store b-tree info if b-tree
				 * info pointer argument is
				 * NULL.
				 */
  bool is_leaf = false;		/* Set to true if crt_page is a
				 * leaf node.
				 */
  bool stop = false;		/* Set to true to stop advancing
				 * in b-tree.
				 */
  bool restart = false;		/* Set to true to restart b-tree
				 * traversal from root.
				 */
  BTREE_SEARCH_KEY_HELPER local_search_key;	/* Store search key result if
						 * search key pointer argument
						 * is NULL.
						 */

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (key != NULL);
  assert (advance_function != NULL);

  if (leaf_page_ptr != NULL)
    {
      /* Initialize leaf_page_ptr as NULL. */
      *leaf_page_ptr = NULL;
    }

  if (btid_int == NULL)
    {
      /* Use local variable to store b-tree info. */
      btid_int = &local_btid_int;
    }
  if (search_key == NULL)
    {
      /* Use local variable to store search key result. */
      search_key = &local_search_key;
    }

start_btree_traversal:
  /* Traversal starting point. The function will try to locate key while
   * calling 3 types of manipulation functions:
   * 1. Root function: It may be used to fix and modify root page. If no 
   *    such function is provided, btree_get_root_with_key is used by default.
   * 2. Advance function: It is used to determine the path to follow in order
   *    to locate the key in leaf node. It can manipulate the nodes it passes
   *    (merge, split).
   * 3. Process key function: It must process the leaf and overflow key/OIDs
   *    pages where key is/should be found. It can be a read-only function or
   *    it can insert/delete/modify the key.
   */

  /* Reset restart flag. */
  restart = false;
  is_leaf = false;
  search_key->result = BTREE_KEY_NOTFOUND;
  search_key->slotid = NULL_SLOTID;

  /* Make sure current page has been unfixed before restarting traversal. */
  if (crt_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, crt_page);
    }

  /* Fix b-tree root page. */
  if (root_function == NULL)
    {
      /* No root function is provided. Use default function that gets root
       * page and b-tree data (btree_get_root_with_key).
       */
      root_function = btree_get_root_with_key;
    }
  /* Call root function. */
  error_code =
    root_function (thread_p, btid, btid_int, key, &crt_page, &is_leaf,
		   search_key, &stop, &restart, root_args);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }
  if (stop)
    {
      /* Stop condition was met. Do not advance. */
      goto end;
    }
  if (restart)
    {
      /* Restart from top. */
      goto start_btree_traversal;
    }
  /* Root page must be fixed. */
  assert (crt_page != NULL);

  /* Advance until leaf page is found. */
  while (!is_leaf)
    {
      /* Call advance function. */
      error_code =
	advance_function (thread_p, btid_int, key, &crt_page, &advance_page,
			  &is_leaf, search_key, &stop, &restart,
			  advance_args);
      if (error_code != NO_ERROR)
	{
	  /* Error! */
	  ASSERT_ERROR ();
	  goto error;
	}
      if (stop)
	{
	  /* Stop search here */
	  goto end;
	}
      if (restart)
	{
	  /* Search must be restarted from top. */
	  if (advance_page != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, advance_page);
	    }
	  goto start_btree_traversal;
	}

      /* Advance if not leaf. */
      if (!is_leaf)
	{
	  /* Free current node page and set advance_page as current. */
	  assert (advance_page != NULL);
	  if (crt_page != NULL)
	    {
	      pgbuf_unfix (thread_p, crt_page);
	    }
	  crt_page = advance_page;
	  advance_page = NULL;
	}
    }

  /* Leaf page is reached. */

  assert (is_leaf && !stop && !restart);
  assert (crt_page != NULL);
  assert (btree_get_node_header (crt_page) != NULL
	  && btree_get_node_header (crt_page)->node_level == 1);

  if (key_function != NULL)
    {
      /* Call key_function. */
      /* Key args must be also provided. */
      assert (process_key_args != NULL);
      error_code =
	key_function (thread_p, btid_int, key, &crt_page, search_key,
		      &restart, process_key_args);
      if (error_code != NO_ERROR)
	{
	  /* Error! */
	  ASSERT_ERROR ();
	  goto error;
	}
      if (restart)
	{
	  /* Search must be restarted. */
	  goto start_btree_traversal;
	}
    }

  /* Finished */

end:
  /* Safe guard: don't leak fixed pages. */
  assert (advance_page == NULL);

  if (is_leaf && leaf_page_ptr != NULL)
    {
      /* Output leaf page. */
      *leaf_page_ptr = crt_page;
    }
  else if (crt_page != NULL)
    {
      /* Unfix leaf page. */
      pgbuf_unfix (thread_p, crt_page);
    }
  return NO_ERROR;

error:
  /* Error! */
  /* Unfix all used pages. */
  if (crt_page != NULL)
    {
      pgbuf_unfix (thread_p, crt_page);
    }
  if (advance_page != NULL)
    {
      pgbuf_unfix (thread_p, advance_page);
    }
  assert (error_code != NO_ERROR);
  ASSERT_ERROR ();
  return error_code;
}

/*
 * btree_get_root_with_key () - BTREE_ROOT_WITH_KEY_FUNCTION used by default
 *				to read root page header and get b-tree data
 *				from header.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid (in)	       : B-tree identifier.
 * btid_int (out)      : BTID_INT (B-tree data).
 * key (in)	       : Key value.
 * root_page (out)     : Output b-tree root page.
 * is_leaf (out)       : Output true if root is leaf page.
 * search_key (out)    : Output key search result (if root is also leaf).
 * stop (out)	       : Output true if advancing in b-tree should stop.
 * restart (out)       : Output true if advancing in b-tree should be
 *			 restarted.
 * other_args (in/out) : BTREE_ROOT_WITH_KEY_ARGS (outputs BTID_INT).
 */
static int
btree_get_root_with_key (THREAD_ENTRY * thread_p, BTID * btid,
			 BTID_INT * btid_int, DB_VALUE * key,
			 PAGE_PTR * root_page, bool * is_leaf,
			 BTREE_SEARCH_KEY_HELPER * search_key, bool * stop,
			 bool * restart, void *other_args)
{
  BTREE_ROOT_HEADER *root_header = NULL;
  int error_code = NO_ERROR;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (key != NULL);
  assert (root_page != NULL && *root_page == NULL);
  assert (is_leaf != NULL);
  assert (search_key != NULL);

  /* Get root page and BTID_INT. */
  *root_page =
    btree_fix_root_with_info (thread_p, btid, PGBUF_LATCH_READ, NULL,
			      &root_header, btid_int);
  if (*root_page == NULL)
    {
      /* Error! */
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  assert (btid_int != NULL);

  if (DB_VALUE_TYPE (key) == DB_TYPE_MIDXKEY
      && key->data.midxkey.domain == NULL)
    {
      /* Use domain from b-tree info. */
      key->data.midxkey.domain = btid_int->key_type;
    }

  *is_leaf = (root_header->node.node_level == 1);
  if (*is_leaf)
    {
      /* Check if key is found in page. */
      error_code =
	btree_search_leaf_page (thread_p, btid_int, *root_page, key,
				search_key);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }
  /* Success. */
  return NO_ERROR;
}

/*
 * btree_advance_and_find_key () - Fix next node in b-tree following given key.
 *				   If argument is leaf-node, return if key
 *				   is found and the slot if key instead.
 *
 * return		 : Error code. 
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree data.
 * key (in)		 : Search key value.
 * crt_page (in)	 : Page of current node.
 * advance_to_page (out) : Fixed page of child node found by following key.
 * is_leaf (out)	 : Output true if current page is leaf node.
 * key_slotid (out)	 : Output slotid of key if found, otherwise
 *			   NULL_SLOTID.
 * stop (out)		 : Output true if advancing in b-tree should be
 *			   stopped.
 * restart (out)	 : Output true if advancing in b-tree should be
 *			   restarted from top.
 * other_args (in/out)	 : Not used.
 */
static int
btree_advance_and_find_key (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			    DB_VALUE * key, PAGE_PTR * crt_page,
			    PAGE_PTR * advance_to_page, bool * is_leaf,
			    BTREE_SEARCH_KEY_HELPER * search_key, bool * stop,
			    bool * restart, void *other_args)
{
  BTREE_NODE_HEADER *node_header;
  BTREE_NODE_TYPE node_type;
  VPID child_vpid;
  int error_code;

  assert (btid_int != NULL);
  assert (key != NULL);
  assert (crt_page != NULL && *crt_page != NULL);
  assert (advance_to_page != NULL && *advance_to_page == NULL);
  assert (search_key != NULL);

  /* Get node header. */
  node_header = btree_get_node_header (*crt_page);
  if (node_header == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }
  node_type =
    node_header->node_level > 1 ? BTREE_NON_LEAF_NODE : BTREE_LEAF_NODE;

  if (node_type == BTREE_LEAF_NODE)
    {
      /* Leaf level was reached, stop advancing. */
      *is_leaf = true;

      /* Is key in page */
      error_code =
	btree_search_leaf_page (thread_p, btid_int, *crt_page, key,
				search_key);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      /* Make sure slot ID is set if key was found. */
      assert (search_key->result != BTREE_KEY_FOUND
	      || search_key->slotid != NULL_SLOTID);
    }
  else
    {
      /* Non-leaf page. */
      *is_leaf = false;
      error_code =
	btree_search_nonleaf_page (thread_p, btid_int, *crt_page, key,
				   &search_key->slotid, &child_vpid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      /* Advance to child. */
      assert (!VPID_ISNULL (&child_vpid));
      *advance_to_page =
	pgbuf_fix (thread_p, &child_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (*advance_to_page == NULL)
	{
	  /* Error fixing child. */
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
    }

  /* Success. */
  return NO_ERROR;
}

/*
 * btree_key_find_unique_version_oid () - Find the visible object version from
 *					  key. Since the index is unique,
 *					  there must be at most one visible
 *					  version.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid_int (in)       : B-tree info.
 * key (in)	       : Key value.
 * leaf_page (in/out)  : Leaf page pointer.
 * search_key (in)     : Search key result.
 * restart (out)       : Set to true if index must be traversed again from
 *			 root node.
 * other_args (in/out) : BTREE_FIND_UNIQUE_HELPER *.
 */
static int
btree_key_find_unique_version_oid (THREAD_ENTRY * thread_p,
				   BTID_INT * btid_int, DB_VALUE * key,
				   PAGE_PTR * leaf_page,
				   BTREE_SEARCH_KEY_HELPER * search_key,
				   bool * restart, void *other_args)
{
  RECDES record;		/* Key record (leaf or overflow). */
  LEAF_REC leaf_info;		/* Leaf record info (key_len & ovfl).
				 */
  int error_code = NO_ERROR;	/* Error code. */
  int offset;			/* Offset in record data where key
				 * is ended and OID list is started.
				 */
  /* Helper used to process record and find visible object. */
  BTREE_REC_SATISFIES_SNAPSHOT_HELPER rec_process_helper =
    BTREE_REC_SATISFIES_SNAPSHOT_HELPER_INITIALIZER;
  /* Helper used to describe find unique process and to output results. */
  BTREE_FIND_UNIQUE_HELPER *find_unique_helper =
    (BTREE_FIND_UNIQUE_HELPER *) other_args;
  OID unique_oid = OID_INITIALIZER;	/* OID of unique object. */
  bool clear_key = false;	/* Clear key */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL);
  assert (leaf_page != NULL && *leaf_page != NULL);
  assert (find_unique_helper != NULL);

  /* Initialize find unique helper. */
  find_unique_helper->found_object = false;

  /* Normally, this function should be called only on unique indexes. However
   * there are some exceptions in catalog classes (e.g. db_user, db_class)
   * that have alternative mechanisms to ensure unicity. They still call
   * find unique on index to quickly get OID with name.
   * This function does work for these cases.
   * So, asserting index is unique is not necessary here.
   */

  if (search_key->result != BTREE_KEY_FOUND)
    {
      /* Key was not found. */
      return NO_ERROR;
    }

  /* Find unique visible object version. Since the index is unique, there can
   * only be one visible version in the key. Parse all key objects until the
   * one visible to current transaction is found.
   * NOTE: The newest object version in the key is always kept first. This is
   *       also usually the object being manipulated by running transactions.
   *       However, this isn't always the visible object for current
   *       transaction. The visible version for current transaction may be
   *       deleted by another, but still visible due to snapshot.
   */

  /* Get key leaf record. */
  if (spage_get_record (*leaf_page, search_key->slotid, &record, PEEK)
      != S_SUCCESS)
    {
      /* Unexpected error. */
      assert_release (false);
      return ER_FAILED;
    }
  /* Read key leaf record. */
  error_code =
    btree_read_record (thread_p, btid_int, *leaf_page, &record, NULL,
		       &leaf_info, BTREE_LEAF_NODE, &clear_key, &offset,
		       PEEK_KEY_VALUE, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* Initialize the helper for btree_record_satisfies_snapshot. */
  /* OID buffer. Only one OID will be copied. */
  rec_process_helper.oid_ptr = &unique_oid;
  /* MVCC snapshot. */
  rec_process_helper.snapshot = find_unique_helper->snapshot;

  /* Match class OID. */
  COPY_OID (&rec_process_helper.match_class_oid,
	    &find_unique_helper->match_class_oid);

  /* Call btree_record_satisfies_snapshot on each object found in key. */
  error_code =
    btree_key_process_objects (thread_p, btid_int, &record, offset,
			       &leaf_info, btree_record_satisfies_snapshot,
			       &rec_process_helper);
  if (error_code != NO_ERROR)
    {
      /* Error! */
      ASSERT_ERROR ();
      return error_code;
    }
  if (rec_process_helper.oid_cnt > 0)
    {
      /* Found visible object. */
      assert (rec_process_helper.oid_cnt == 1);
      assert (!OID_ISNULL (&unique_oid));
      find_unique_helper->found_object = true;
      COPY_OID (&find_unique_helper->oid, &unique_oid);
    }
  return NO_ERROR;
}

/*
 * btree_key_find_and_lock_unique () - Find key and lock its unique non-dirty
 *				       version.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid_int (in)       : B-tree info.
 * key (in)	       : Key value.
 * leaf_page (in/out)  : Leaf node page (where key would normally belong).
 * search_key (in)     : Search key result.
 * restart (out)       : Set to true if b-tree traversal must be restarted
 *			 from root.
 * other_args (in/out) : BTREE_FIND_UNIQUE_HELPER *.
 */
static int
btree_key_find_and_lock_unique (THREAD_ENTRY * thread_p,
				BTID_INT * btid_int,
				DB_VALUE * key,
				PAGE_PTR * leaf_page,
				BTREE_SEARCH_KEY_HELPER *
				search_key, bool * restart, void *other_args)
{
  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      return btree_key_find_and_lock_unique_of_unique (thread_p, btid_int,
						       key, leaf_page,
						       search_key, restart,
						       other_args);
    }
  else
    {
      return btree_key_find_and_lock_unique_of_non_unique (thread_p, btid_int,
							   key, leaf_page,
							   search_key,
							   restart,
							   other_args);
    }
}

/*
 * btree_key_find_and_lock_unique_of_unique () - Find key and lock its first
 *						 object (if not deleted or 
 *						 dirty).
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid_int (in)       : B-tree info.
 * key (in)	       : Key value.
 * leaf_page (in/out)  : Leaf node page (where key would normally belong).
 * search_key (in)     : Search key result.
 * restart (out)       : Set to true if b-tree traversal must be restarted
 *			 from root.
 * other_args (in/out) : BTREE_FIND_UNIQUE_HELPER *.
 */
static int
btree_key_find_and_lock_unique_of_unique (THREAD_ENTRY * thread_p,
					  BTID_INT * btid_int, DB_VALUE * key,
					  PAGE_PTR * leaf_page,
					  BTREE_SEARCH_KEY_HELPER *
					  search_key, bool * restart,
					  void *other_args)
{
  OID unique_oid, unique_class_oid;	/* Unique object OID and class
					 * OID.
					 */
  /* Unique object MVCC info. */
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;
  /* Converted from b-tree MVCC info to check if object satisfies delete. */
  MVCC_REC_HEADER mvcc_header = MVCC_REC_HEADER_INITIALIZER;
  /* Helper used to describe find unique process and to output results. */
  BTREE_FIND_UNIQUE_HELPER *find_unique_helper = NULL;
  RECDES record;		/* Key leaf record. */
  int error_code = NO_ERROR;	/* Error code. */
  MVCC_SATISFIES_DELETE_RESULT satisfies_delete;	/* Satisfies delete
							 * result.
							 */
#if defined (SERVER_MODE)
  /* Next variables are not required for stand-alone mode. */
  bool try_cond_lock = false;	/* Try conditional lock. */
  bool was_page_refixed = false;	/* Set to true if conditional lock
					 * failed and page had to be re-fixed.
					 */
#endif /* SERVER_MODE */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (BTREE_IS_UNIQUE (btid_int->unique_pk));
  assert (key != NULL);
  assert (leaf_page != NULL && *leaf_page != NULL);
  assert (restart != NULL);
  assert (other_args != NULL);

  /* other_args is find unique helper. */
  find_unique_helper = (BTREE_FIND_UNIQUE_HELPER *) other_args;

  /* Locking is required. */
  assert (find_unique_helper->lock_mode >= S_LOCK);

  /* Assume result is BTREE_KEY_NOTFOUND. It will be set to BTREE_KEY_FOUND
   * if key is found and its first object is successfully locked.
   */
  find_unique_helper->found_object = false;

  if (search_key->result != BTREE_KEY_FOUND)
    {
      /* Key doesn't exist. Exit. */
      goto error_or_not_found;
    }

  /* Lock key non-dirty version to protect it. Non-dirty or newest key version
   * is always kept first.
   *
   * Locking object is possible if object is not deleted and it is not dirty
   * (its inserter/deleter is not active).
   *
   * If inserter or deleter is active, or if conditional lock on object
   * failed, current transaction must suspend until the object lock holder is
   * completed. This also means unfixing leaf page first.
   *
   * Current algorithm tries to avoid traversing the b-tree back from root
   * after resume. If conditional lock on object fails, leaf node must be
   * unfixed and then fixed again after object is locked. If page no longer
   * exists or if the page is no longer usable (key is not in page), the
   * process is restarted (while holding the lock however).
   *
   * NOTE: Stand-alone mode doesn't require locking. It should only check
   *       whether the first key object is deleted or not.
   */

  /* Initialize unique_oid */
  OID_SET_NULL (&unique_oid);

  /* Loop until first object is successfully locked or until it is found as
   * deleted.
   */
  while (true)
    {
      /* Safe guard: leaf node page must be fixed. */
      assert (*leaf_page != NULL);

      /* Get key record. */
      if (spage_get_record (*leaf_page, search_key->slotid, &record, PEEK)
	  != S_SUCCESS)
	{
	  /* Unexpected error. */
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  error_code = ER_FAILED;
	  goto error_or_not_found;
	}
      /* Get first object */
      error_code =
	btree_leaf_get_first_object (btid_int, &record, &unique_oid,
				     &unique_class_oid, &mvcc_info);
      if (error_code != NO_ERROR)
	{
	  /* Error! */
	  ASSERT_ERROR ();
	  goto error_or_not_found;
	}

      if (!OID_ISNULL (&find_unique_helper->match_class_oid)
	  && !OID_EQ (&find_unique_helper->match_class_oid,
		      &unique_class_oid))
	{
	  /* Class OID didn't match. */
	  /* Consider key not found. */
	  goto error_or_not_found;
	}

#if defined (SERVER_MODE)
      /* Did we already lock an object and was the object changed?
       * If so, unlock it.
       */
      if (!OID_ISNULL (&find_unique_helper->locked_oid)
	  && !OID_EQ (&find_unique_helper->locked_oid, &unique_oid))
	{
	  lock_unlock_object_donot_move_to_non2pl (thread_p,
						   &find_unique_helper->
						   locked_oid,
						   &find_unique_helper->
						   locked_class_oid,
						   find_unique_helper->
						   lock_mode);
	  OID_SET_NULL (&find_unique_helper->locked_oid);
	}
#endif /* SERVER_MODE */

      /* Check whether object can be locked. */
      btree_mvcc_info_to_heap_mvcc_header (&mvcc_info, &mvcc_header);
      satisfies_delete = mvcc_satisfies_delete (thread_p, &mvcc_header);
      switch (satisfies_delete)
	{
	case DELETE_RECORD_INVISIBLE:
	case DELETE_RECORD_IN_PROGRESS:
#if defined (SA_MODE)
	  /* Impossible. */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error_or_not_found;
#else	/* !SA_MODE */	       /* SERVER_MODE */
	  /* Object is being inserted/deleted. We need to lock and suspend
	   * until it's fate is decided.
	   */
	  assert (!lock_has_lock_on_object (&unique_oid, &unique_class_oid,
					    thread_get_current_tran_index (),
					    find_unique_helper->lock_mode));
#endif /* SERVER_MODE */
	  /* Fall through. */
	case DELETE_RECORD_CAN_DELETE:
#if defined (SERVER_MODE)
	  /* Must lock object. */
	  if (!OID_ISNULL (&find_unique_helper->locked_oid))
	    {
	      /* Object already locked. */
	      /* Safe guard. */
	      assert (OID_EQ (&find_unique_helper->locked_oid, &unique_oid));
	      assert (satisfies_delete == DELETE_RECORD_CAN_DELETE);

	      /* Return result. */
	      COPY_OID (&find_unique_helper->oid, &unique_oid);
	      find_unique_helper->found_object = true;
	      return NO_ERROR;
	    }
	  /* Don't try conditional lock if DELETE_RECORD_INVISIBLE or
	   * DELETE_RECORD_IN_PROGRESS. Most likely it will fail.
	   */
	  try_cond_lock = (satisfies_delete == DELETE_RECORD_CAN_DELETE);
	  /* Lock object. */
	  error_code =
	    btree_key_lock_object (thread_p, btid_int, key, leaf_page, NULL,
				   &unique_oid, &unique_class_oid,
				   find_unique_helper->lock_mode,
				   search_key, try_cond_lock, restart,
				   &was_page_refixed);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error_or_not_found;
	    }
	  /* Object locked. */
	  assert (lock_has_lock_on_object (&unique_oid, &unique_class_oid,
					   thread_get_current_tran_index (),
					   find_unique_helper->lock_mode)
		  > 0);
	  COPY_OID (&find_unique_helper->locked_oid, &unique_oid);
	  COPY_OID (&find_unique_helper->locked_class_oid, &unique_class_oid);
	  if (*restart)
	    {
	      /* Need to restart from top. */
	      return NO_ERROR;
	    }
	  if (search_key->result == BTREE_KEY_BETWEEN)
	    {
	      /* Key no longer exist. */
	      goto error_or_not_found;
	    }
	  assert (search_key->result == BTREE_KEY_FOUND);
	  if (was_page_refixed)
	    {
	      /* Key was found but we still need to re-check first object.
	       * Since page was re-fixed, it may have changed.
	       */
	      break;		/* switch (satisfies_delete) */
	    }
	  else
	    {
	      /* Safe guard */
	      assert (satisfies_delete == DELETE_RECORD_CAN_DELETE);
	    }
#endif /* SERVER_MODE */

	  /* Object was found. Return result. */
	  COPY_OID (&find_unique_helper->oid, &unique_oid);
	  find_unique_helper->found_object = true;
	  return NO_ERROR;

	case DELETE_RECORD_DELETED:
	case DELETE_RECORD_SELF_DELETED:
	  /* Key object is deleted. */
	  goto error_or_not_found;

	default:
	  /* Unhandled/unexpected case. */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error_or_not_found;
	}			/* switch (satisfies_delete) */
    }
  /* Impossible to reach. Loop can only be broken by returns or jumps to
   * error_or_not_found label.
   */
  assert_release (false);
  error_code = ER_FAILED;
  /* Fall through. */

error_or_not_found:
  assert (find_unique_helper->found_object == false);

#if defined (SERVER_MODE)
  if (!OID_ISNULL (&find_unique_helper->locked_oid))
    {
      /* Unlock object. */
      lock_unlock_object_donot_move_to_non2pl (thread_p,
					       &find_unique_helper->
					       locked_oid,
					       &find_unique_helper->
					       locked_class_oid,
					       find_unique_helper->lock_mode);
      OID_SET_NULL (&find_unique_helper->locked_oid);
    }
#endif
  return error_code;
}

/*
 * btree_key_find_and_lock_unique_of_non_unique () - Find key non-dirty
 *						     version and lock it.
 *						     This is usually called in
 *						     indexes of system classes
 *						     that should be unique
 *						     but are not.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid_int (in)       : B-tree info.
 * key (in)	       : Key value.
 * leaf_page (in/out)  : Leaf node page (where key would normally belong).
 * search_key (in)     : Search key result.
 * restart (out)       : Set to true if b-tree traversal must be restarted
 *			 from root.
 * other_args (in/out) : BTREE_FIND_UNIQUE_HELPER *.
 */
static int
btree_key_find_and_lock_unique_of_non_unique (THREAD_ENTRY * thread_p,
					      BTID_INT * btid_int,
					      DB_VALUE * key,
					      PAGE_PTR * leaf_page,
					      BTREE_SEARCH_KEY_HELPER *
					      search_key,
					      bool * restart,
					      void *other_args)
{
  OID unique_oid, unique_class_oid;	/* Unique object OID and class
					 * OID.
					 */
  /* Unique object MVCC info. */
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;
  /* Converted from b-tree MVCC info to check if object satisfies delete. */
  MVCC_REC_HEADER mvcc_header = MVCC_REC_HEADER_INITIALIZER;
  /* Helper used to describe find unique process and to output results. */
  BTREE_FIND_UNIQUE_HELPER *find_unique_helper = NULL;
  RECDES record;		/* Key leaf record. */
  int error_code = NO_ERROR;	/* Error code. */
  MVCC_SATISFIES_DELETE_RESULT satisfies_delete;	/* Satisfies delete
							 * result.
							 */
  PAGE_PTR overflow_page = NULL;	/* Overflow page. */
  VPID next_overflow_vpid = VPID_INITIALIZER;	/* VPID of next overflow page.
						 */
  OR_BUF buf;			/* Buffer used to read b-tree
				 * records.
				 */
  bool start_reading_leaf_record = true;	/* Set to true when needs to start
						 * reading from first leaf object.
						 */
  PAGE_PTR prev_overflow_page = NULL;	/* Saved pointer to previous
					 * overflow page when next is fixed.
					 */
  int offset_after_key = 0;	/* For leaf record, offset where
				 * packed key is ended.
				 */
  BTREE_NODE_TYPE node_type;	/* Current node type. */
  LEAF_REC leaf_rec_info;	/* Leaf record info. */
  bool dummy_clear_key;		/* Dummy. */
#if defined (SERVER_MODE)
  /* Next variables are not required for stand-alone mode. */
  bool try_cond_lock = false;	/* Try conditional lock. */
  bool was_page_refixed = false;	/* Set to true if conditional lock
					 * failed and page had to be re-fixed.
					 */
#endif /* SERVER_MODE */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (!BTREE_IS_UNIQUE (btid_int->unique_pk));
  assert (key != NULL);
  assert (leaf_page != NULL && *leaf_page != NULL);
  assert (restart != NULL);
  assert (other_args != NULL);

  /* other_args is find unique helper. */
  find_unique_helper = (BTREE_FIND_UNIQUE_HELPER *) other_args;

  /* Locking is required. */
  assert (find_unique_helper->lock_mode >= S_LOCK);

  /* Assume result is BTREE_KEY_NOTFOUND. It will be set to BTREE_KEY_FOUND
   * if key is found and its first object is successfully locked.
   */
  find_unique_helper->found_object = false;

  if (search_key->result != BTREE_KEY_FOUND)
    {
      /* Key doesn't exist. Exit. */
      goto error_or_not_found;
    }

  /* Lock key non-dirty version to protect it. Since this is not an unique
   * index, but can have only one non-dirty version, this must be searched
   * through the leaf/overflow records.
   *
   * Locking object is possible if object is not deleted and it is not dirty
   * (its inserter/deleter is not active).
   *
   * If inserter or deleter is active, or if conditional lock on object
   * failed, current transaction must suspend until the object lock holder is
   * completed. This also means unfixing leaf/overflow pages first.
   *
   * Current algorithm tries to avoid traversing the b-tree back from root
   * after resume. If conditional lock on object fails, leaf/overflow nodes
   * must be unfixed and then fixed again after object is locked. If leaf page
   * no longer exists or if the page is no longer usable (key is not in page),
   * the process is restarted from root. If leaf page is still valid,
   * leaf/overflow records are processed again.
   *
   * NOTE: Stand-alone mode doesn't require locking. It should only check
   *       whether the first key object is deleted or not.
   */

  /* Initialize unique_oid */
  OID_SET_NULL (&unique_oid);
  OID_SET_NULL (&unique_class_oid);

  /* Loop until a visible object is successfully locked or if the entire key
   * is processed.
   */
  while (true)
    {
      /* Safe guard: leaf node page must be fixed. */
      assert (*leaf_page != NULL);

      if (start_reading_leaf_record)
	{
	  /* Read leaf record. */
	  if (spage_get_record (*leaf_page, search_key->slotid, &record, PEEK)
	      != S_SUCCESS)
	    {
	      assert_release (false);
	      error_code = ER_FAILED;
	      goto error_or_not_found;
	    }
	  node_type = BTREE_LEAF_NODE;
	  error_code =
	    btree_read_record (thread_p, btid_int, *leaf_page, &record, NULL,
			       &leaf_rec_info, node_type, &dummy_clear_key,
			       &offset_after_key, PEEK_KEY_VALUE, NULL);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error_or_not_found;
	    }
	  /* Get first overflow vpid. */
	  VPID_COPY (&next_overflow_vpid, &leaf_rec_info.ovfl);
	  /* Initialize buffer to read from record. */
	  BTREE_RECORD_OR_BUF_INIT (buf, &record);
	  /* Get first object. */
	  error_code =
	    btree_or_get_object (&buf, btid_int, node_type, offset_after_key,
				 &unique_oid, &unique_class_oid, &mvcc_info);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error_or_not_found;
	    }
	  start_reading_leaf_record = false;
	}
      else
	{
	  /* Get next object. */

	  if (buf.ptr == buf.endptr)
	    {
	      /* Processed all objects in this record. */
	      if (VPID_ISNULL (&next_overflow_vpid))
		{
		  /* Not other overflow pages. Not found. */
		  goto error_or_not_found;
		}
	      /* Fix next overflow page. */
	      if (overflow_page != NULL)
		{
		  /* Save this overflow page. */
		  prev_overflow_page = overflow_page;
		}
	      /* Fix next overflow page. */
	      overflow_page =
		pgbuf_fix (thread_p, &next_overflow_vpid, OLD_PAGE,
			   PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	      if (overflow_page == NULL)
		{
		  ASSERT_ERROR_AND_SET (error_code);
		  goto error_or_not_found;
		}
	      if (prev_overflow_page != NULL)
		{
		  pgbuf_unfix_and_init (thread_p, prev_overflow_page);
		}
	      /* Now read leaf record. */
	      if (spage_get_record (overflow_page, 1, &record, PEEK)
		  != S_SUCCESS)
		{
		  assert_release (false);
		  error_code = ER_FAILED;
		  goto error_or_not_found;
		}
	      /* Initialize buffer to read record. */
	      BTREE_RECORD_OR_BUF_INIT (buf, &record);
	      /* Key is not kept in overflow pages. */
	      offset_after_key = 0;
	      node_type = BTREE_OVERFLOW_NODE;
	      /* Get VPID of next overflow page. */
	      error_code =
		btree_get_next_overflow_vpid (overflow_page,
					      &next_overflow_vpid);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto error_or_not_found;
		}
	    }
	  /* Assert there are objects in current record. */
	  assert (buf.ptr < buf.endptr);
	  error_code =
	    btree_or_get_object (&buf, btid_int, node_type, offset_after_key,
				 &unique_oid, &unique_class_oid, &mvcc_info);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error_or_not_found;
	    }
	}
      /* Safe guard: object was read. */
      assert (!OID_ISNULL (&unique_oid));
      assert (!OID_ISNULL (&unique_class_oid));

      if (!OID_ISNULL (&find_unique_helper->match_class_oid)
	  && !OID_EQ (&find_unique_helper->match_class_oid,
		      &unique_class_oid))
	{
	  /* Class does not match. Try another object. */
	  continue;
	}

      /* Check whether object can be locked. */
      btree_mvcc_info_to_heap_mvcc_header (&mvcc_info, &mvcc_header);
      satisfies_delete = mvcc_satisfies_delete (thread_p, &mvcc_header);
      switch (satisfies_delete)
	{
	case DELETE_RECORD_INVISIBLE:
	case DELETE_RECORD_IN_PROGRESS:
#if defined (SA_MODE)
	  /* Impossible. */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error_or_not_found;
#else	/* !SA_MODE */	       /* SERVER_MODE */
	  /* Object is being inserted/deleted. We need to lock and suspend
	   * until it's fate is decided.
	   */
	  assert (!lock_has_lock_on_object (&unique_oid, &unique_class_oid,
					    thread_get_current_tran_index (),
					    find_unique_helper->lock_mode));
#endif /* SERVER_MODE */
	  /* Fall through. */
	case DELETE_RECORD_CAN_DELETE:
#if defined (SERVER_MODE)
	  /* Must lock object. */
	  if (!OID_ISNULL (&find_unique_helper->locked_oid))
	    {
	      if (OID_EQ (&find_unique_helper->locked_oid, &unique_oid))
		{
		  /* Object already locked. */
		  assert (satisfies_delete == DELETE_RECORD_CAN_DELETE);

		  /* Return result. */
		  COPY_OID (&find_unique_helper->oid, &unique_oid);
		  find_unique_helper->found_object = true;
		  if (overflow_page != NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, overflow_page);
		    }
		  /* Leaf page will be unfixed by caller. */
		  return NO_ERROR;
		}
	      else
		{
		  /* Unlock object. */
		  lock_unlock_object_donot_move_to_non2pl
		    (thread_p, &find_unique_helper->locked_oid,
		     &find_unique_helper->locked_class_oid,
		     find_unique_helper->lock_mode);
		  OID_SET_NULL (&find_unique_helper->locked_oid);
		}
	    }
	  /* Don't try conditional lock if DELETE_RECORD_INVISIBLE or
	   * DELETE_RECORD_IN_PROGRESS. Most likely it will fail.
	   */
	  try_cond_lock = (satisfies_delete == DELETE_RECORD_CAN_DELETE);
	  /* Lock object. */
	  error_code =
	    btree_key_lock_object (thread_p, btid_int, key, leaf_page,
				   &overflow_page, &unique_oid,
				   &unique_class_oid,
				   find_unique_helper->lock_mode,
				   search_key, try_cond_lock, restart,
				   &was_page_refixed);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error_or_not_found;
	    }
	  /* Object locked. */
	  assert (lock_has_lock_on_object (&unique_oid, &unique_class_oid,
					   thread_get_current_tran_index (),
					   find_unique_helper->lock_mode)
		  > 0);
	  COPY_OID (&find_unique_helper->locked_oid, &unique_oid);
	  COPY_OID (&find_unique_helper->locked_class_oid, &unique_class_oid);
	  if (*restart)
	    {
	      /* Need to restart from top. */
	      assert (overflow_page == NULL);
	      return NO_ERROR;
	    }
	  if (search_key->result == BTREE_KEY_BETWEEN)
	    {
	      /* Key no longer exist. */
	      goto error_or_not_found;
	    }
	  assert (search_key->result == BTREE_KEY_FOUND);
	  if (was_page_refixed)
	    {
	      /* Key was found but we still need to re-check objects.
	       * Since page was re-fixed, record may have changed (and also
	       * positions of objects).
	       */
	      start_reading_leaf_record = true;
	      was_page_refixed = false;
	      /* Safe guard: overflow page must be unfixed. */
	      assert (overflow_page == NULL);
	      break;		/* switch (satisfies_delete) */
	    }
	  else
	    {
	      /* Safe guard */
	      assert (satisfies_delete == DELETE_RECORD_CAN_DELETE);
	    }
#endif /* SERVER_MODE */

	  /* Object was found. Return result. */
	  COPY_OID (&find_unique_helper->oid, &unique_oid);
	  find_unique_helper->found_object = true;
	  if (overflow_page != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, overflow_page);
	    }
	  /* Leaf page will be unfixed by caller. */
	  return NO_ERROR;

	case DELETE_RECORD_DELETED:
	case DELETE_RECORD_SELF_DELETED:
	  /* This object is deleted. */
	  /* Continue to next object. */
	  break;

	default:
	  /* Unhandled/unexpected case. */
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error_or_not_found;
	}			/* switch (satisfies_delete) */
    }
  /* Impossible to reach. Loop can only be broken by returns or jumps to
   * error_or_not_found label.
   */
  assert_release (false);
  error_code = ER_FAILED;
  /* Fall through. */

error_or_not_found:
  assert (find_unique_helper->found_object == false);

  if (overflow_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, overflow_page);
    }
  if (prev_overflow_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, prev_overflow_page);
    }

#if defined (SERVER_MODE)
  if (!OID_ISNULL (&find_unique_helper->locked_oid))
    {
      /* Unlock object. */
      lock_unlock_object_donot_move_to_non2pl (thread_p,
					       &find_unique_helper->
					       locked_oid,
					       &find_unique_helper->
					       locked_class_oid,
					       find_unique_helper->lock_mode);
      OID_SET_NULL (&find_unique_helper->locked_oid);
    }
#endif
  return error_code;
}

#if defined (SERVER_MODE)
/*
 * btree_key_lock_object () - Lock object when its leaf page is held.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * btid_int (in)	  : B-tree identifier.
 * key (in)		  : Key.
 * leaf_page (in/out)     : Pointer to leaf node page.
 * overflow_page (in/out) : Pointer to fixed overflow page. If leaf page must
 *			    be unfixed, this will be unfixed too (without
 *			    fixing it again).
 * oid (in)		  : OID of object to lock.
 * class_oid (in)	  : Class OID of object to lock.
 * lock_mode (in)	  : Lock mode.
 * search_key (in.out)	  : Search key result. Can change if page is unfixed.
 * try_cond_lock (in)	  : True to try conditional lock first. If false,
 *			    page is unfixed directly.
 * restart (out)	  : Outputs true when page had to be unfixed and was
 *			    not considered valid to be reused.
 * was_page_refixed (out) : Outputs true if page had to be unfixed.
 *
 * TODO: Extend this function to handle overflow OID's page too.
 */
static int
btree_key_lock_object (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
		       DB_VALUE * key, PAGE_PTR * leaf_page,
		       PAGE_PTR * overflow_page,
		       OID * oid, OID * class_oid, LOCK lock_mode,
		       BTREE_SEARCH_KEY_HELPER * search_key,
		       bool try_cond_lock, bool * restart,
		       bool * was_page_refixed)
{
  VPID leaf_vpid;		/* VPID of leaf page. */
  int lock_result;		/* Result of tried locks. */
  int error_code = NO_ERROR;	/* Error code. */
  PGBUF_LATCH_MODE latch_mode;	/* Leaf page latch mode. */
  LOG_LSA page_lsa;		/* Leaf page LSA before is unfixed. */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (leaf_page != NULL && *leaf_page != NULL);
  assert (oid != NULL && !OID_ISNULL (oid));
  assert (class_oid != NULL && !OID_ISNULL (class_oid));
  assert (lock_mode >= S_LOCK);
  assert (search_key != NULL && search_key->result == BTREE_KEY_FOUND);

  if (try_cond_lock)
    {
      /* Try conditional lock. */
      lock_result =
	lock_object (thread_p, oid, class_oid, lock_mode, LK_COND_LOCK);
      if (lock_result == LK_GRANTED)
	{
	  /* Successful locking. */
	  return NO_ERROR;
	}
    }

  /* In order to avoid keeping latched pages while being suspended on locks,
   * leaf page must be unfixed.
   * After lock is obtained, page can be re-fixed and re-used.
   */
  /* If an overflow page is also fixed, when leaf page is unfixed, overflow
   * page is also unfixed. It is up to the caller to handle re-fix and resume.
   */
  if (overflow_page != NULL && *overflow_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, *overflow_page);
    }

  /* Save page VPID. */
  pgbuf_get_vpid (*leaf_page, &leaf_vpid);
  assert (!VPID_ISNULL (&leaf_vpid));
  /* Save page LSA. */
  LSA_COPY (&page_lsa, pgbuf_get_lsa (*leaf_page));
  /* Save page latch mode for re-fix. */
  latch_mode = pgbuf_get_latch_mode (*leaf_page);
  /* Unfix page. */
  pgbuf_unfix_and_init (thread_p, *leaf_page);
  if (was_page_refixed != NULL)
    {
      /* Output page was unfixed. */
      *was_page_refixed = true;
    }

  /* Lock object. */
  lock_result =
    lock_object (thread_p, oid, class_oid, lock_mode, LK_UNCOND_LOCK);
  if (lock_result != LK_GRANTED)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  /* Lock granted. */

  /* Try to re-fix page. */
  *leaf_page =
    pgbuf_fix_without_validation (thread_p, &leaf_vpid, OLD_PAGE, latch_mode,
				  PGBUF_UNCONDITIONAL_LATCH);
  if (*leaf_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto error;
    }
  /* Page successfully re-fixed. */

  /* Check if page is changed. */
  if (LSA_EQ (&page_lsa, pgbuf_get_lsa (*leaf_page)))
    {
      /* Page not changed. */
      return NO_ERROR;
    }
  /* Page has changed. */

  /* Check if page is still valid for our key. */
  if (!BTREE_IS_PAGE_VALID_LEAF (thread_p, *leaf_page))
    {
      /* Page was deallocated/reused for other purposes. Very unlikely, but
       * had to check.
       */
      *restart = true;
      return NO_ERROR;
    }
  /* Page is a b-tree leaf. */

  /* Search key. */
  error_code =
    btree_search_leaf_page (thread_p, btid_int, *leaf_page, key, search_key);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }
  switch (search_key->result)
    {
    case BTREE_KEY_FOUND:
    case BTREE_KEY_BETWEEN:
      /* Key belongs to this page. */
      return NO_ERROR;
    case BTREE_KEY_SMALLER:
    case BTREE_KEY_NOTFOUND:
    case BTREE_KEY_BIGGER:
      /* Key is no longer in this page. Need to restart. */
      *restart = true;
      return NO_ERROR;
    default:
      /* Unexpected. */
      assert_release (false);
      return ER_FAILED;
    }

  /* Shouldn't be here: Unhandled case. */
  assert_release (false);
  error_code = ER_FAILED;
  /* Fall through. */

error:
  assert (error_code != NO_ERROR);
  ASSERT_ERROR ();

  lock_unlock_object_donot_move_to_non2pl (thread_p, oid, class_oid,
					   lock_mode);
  return error_code;
}
#endif /* SERVER_MODE */

/*
 * btree_record_process_objects () - Generic routine to process the objects
 *				     of a record (leaf or overflow).
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree info.
 * node_type (in)	 : Node type - LEAF or OVERFLOW.
 * record (in)		 : Record descriptor.
 * after_key_offset (in) : Offset in record where key value is ended (for
 *			   leaf record).
 * func (in)		 : BTREE_PROCESS_OBJECT_FUNCTION *.
 * args (in/out)	 : Arguments for internal function.
 */
static int
btree_record_process_objects (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			      BTREE_NODE_TYPE node_type,
			      RECDES * record, int after_key_offset,
			      bool * stop,
			      BTREE_PROCESS_OBJECT_FUNCTION * func,
			      void *args)
{
  OR_BUF buffer;		/* Buffer used to process record data. */
  int error_code = NO_ERROR;	/* Error code. */
  char *object_ptr = NULL;	/* Pointer in record data where current
				 * object starts.
				 */
  OID oid;			/* OID of current object. */
  OID class_oid;		/* Class OID of current object. */
  BTREE_MVCC_INFO mvcc_info;	/* MVCC info of current object. */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_OVERFLOW_NODE);
  assert (record != NULL);
  assert (func != NULL);

  /* Initialize buffer. */
  BTREE_RECORD_OR_BUF_INIT (buffer, record);

  /* Loop for all objects in buffer. */
  while (buffer.ptr < buffer.endptr)
    {
      /* Save current object pointer in record data. */
      object_ptr = buffer.ptr;

      /* Get object data: OID, class OID and MVCC info. */
      error_code =
	btree_or_get_object (&buffer, btid_int, node_type, after_key_offset,
			     &oid, &class_oid, &mvcc_info);
      if (error_code != NO_ERROR)
	{
	  /* Unexpected error. */
	  assert (false);
	  return error_code;
	}

      /* Call internal function. */
      error_code =
	func (thread_p, btid_int, record, object_ptr, &oid, &class_oid,
	      &mvcc_info, stop, args);
      if (error_code != NO_ERROR)
	{
	  /* Error! */
	  return error_code;
	}

      if (*stop)
	{
	  /* Stop processing the record objects */
	  return NO_ERROR;
	}
    }
  /* Finished processing buffer. */

  /* Safe guard: current pointer should point to expected end of buffer. */
  assert (buffer.ptr == buffer.endptr);

  /* Success. */
  return NO_ERROR;
}

/*
 * btree_key_process_objects () - Generic key processing function that calls
 *				  given BTREE_PROCESS_OBJECT_FUNCTION function
 *				  on all key objects (unless an error or stop
 *				  argument forces an early out).
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree identifier.
 * leaf_record (in)	 : Leaf record.
 * after_key_offset (in) : Offset to OID list, where packed key is ended.
 * leaf_info (in)	 : Leaf info.
 * func (in)		 : Internal function to process each key object.
 * args (in/out)	 : Arguments for internal function.
 *
 * NOTE: Leaf record of objects must be obtained before calling this function.
 * TODO: Consider using write latch for overflow pages.
 */
static int
btree_key_process_objects (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			   RECDES * leaf_record, int after_key_offset,
			   LEAF_REC * leaf_info,
			   BTREE_PROCESS_OBJECT_FUNCTION * func, void *args)
{
  int error_code = NO_ERROR;
  bool stop = false;
  RECDES peeked_ovf_recdes;
  VPID ovf_vpid;
  PAGE_PTR ovf_page = NULL, prev_ovf_page = NULL;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (leaf_record != NULL);
  assert (leaf_info != NULL);
  assert (func != NULL);

  /* Start by processing leaf record. */
  error_code =
    btree_record_process_objects (thread_p, btid_int, BTREE_LEAF_NODE,
				  leaf_record, after_key_offset, &stop, func,
				  args);
  if (error_code != NO_ERROR || stop)
    {
      /* Error or just stop with NO_ERROR. */
      assert (error_code == NO_ERROR || er_errid () != NO_ERROR);
      return error_code;
    }

  /* Process overflow OID's. */
  VPID_COPY (&ovf_vpid, &leaf_info->ovfl);
  while (!VPID_ISNULL (&ovf_vpid))
    {
      /* Fix overflow page. */
      ovf_page =
	pgbuf_fix (thread_p, &ovf_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (ovf_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  if (prev_ovf_page != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, prev_ovf_page);
	    }
	  return error_code;
	}
      if (prev_ovf_page != NULL)
	{
	  /* Now unfix previous overflow page. */
	  pgbuf_unfix_and_init (thread_p, prev_ovf_page);
	}
      /* Get overflow OID's record. */
      if (spage_get_record (ovf_page, 1, &peeked_ovf_recdes, PEEK)
	  != S_SUCCESS)
	{
	  assert_release (false);
	  pgbuf_unfix_and_init (thread_p, ovf_page);
	  return ER_FAILED;
	}
      /* Call internal function on overflow record. */
      error_code =
	btree_record_process_objects (thread_p, btid_int, BTREE_OVERFLOW_NODE,
				      &peeked_ovf_recdes, 0, &stop, func,
				      args);
      if (error_code != NO_ERROR)
	{
	  /* Error . */
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init (thread_p, ovf_page);
	  return error_code;
	}
      else if (stop)
	{
	  /* Stop. */
	  pgbuf_unfix_and_init (thread_p, ovf_page);
	  return NO_ERROR;
	}
      /* Get VPID of next overflow page */
      error_code = btree_get_next_overflow_vpid (ovf_page, &ovf_vpid);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  pgbuf_unfix_and_init (thread_p, ovf_page);
	  return error_code;
	}
      /* Save overflow page until next one is fixed to protect the link
       * between them.
       */
      prev_ovf_page = ovf_page;
    }
  /* All objects have been processed. If overflow page is fixed, unfix it. */
  if (ovf_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, ovf_page);
    }
  /* Successfully processed all key objects. */
  return NO_ERROR;
}

/*
 * btree_record_object_compare () - BTREE_PROCESS_OBJECT_FUNCTION. It
 *				    will compare the record object with given
 *				    object (sometimes considering MVCC info).
 *				    When the right object is found, search
 *				    should stop.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid_int (in)       : B-tree info.
 * record (in)	       : B-tree leaf/overflow record.
 * object_ptr (in)     : Pointer in record data to current object.
 * oid (in)	       : Current object OID.
 * class_oid (in)      : Current object's class OID.
 * mvcc_info (in)      : Current object's MVCC info.
 * stop (out)	       : Set to true when object/MVCC info is matched.
 * other_args (in/out) : BTREE_REC_FIND_OBJ_HELPER *.
 */
static int
btree_record_object_compare (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			     RECDES * record, char *object_ptr, OID * oid,
			     OID * class_oid, BTREE_MVCC_INFO * mvcc_info,
			     bool * stop, void *other_args)
{
  BTREE_REC_FIND_OBJ_HELPER *helper = NULL;	/* Find object helper. */

  /* Assert expected arguments. */
  assert (oid != NULL);
  assert (other_args != NULL);
  assert (mvcc_info != NULL
	  || (!MVCCID_IS_VALID (helper->check_insert)
	      && !MVCCID_IS_VALID (helper->check_delete)));
  assert (object_ptr != NULL);

  /* Get helper from other_args. */
  helper = (BTREE_REC_FIND_OBJ_HELPER *) other_args;

  /* Compare OID's. */
  if (!OID_EQ (oid, &helper->oid))
    {
      /* This is not the object we're looking for. */
      return NO_ERROR;
    }
  /* If check_insert is valid, compare it to object's insert MVCCID. */
  if (MVCCID_IS_VALID (helper->check_insert)
      && (helper->check_insert != BTREE_MVCC_INFO_INSID (mvcc_info)
	  && MVCCID_ALL_VISIBLE != BTREE_MVCC_INFO_INSID (mvcc_info)))
    {
      /* This is not the object we're looking for. */
      return NO_ERROR;
    }
  /* If check_delete is valid, compare it to object's delete MVCCID. */
  if (MVCCID_IS_VALID (helper->check_delete)
      && helper->check_delete != BTREE_MVCC_INFO_DELID (mvcc_info))
    {
      /* This is not the object we're looking for. */
      return NO_ERROR;
    }

  /* Object was found. */
  helper->found_offset = CAST_BUFLEN (object_ptr - record->data);
  *stop = true;
  return NO_ERROR;
}

/*
 * btree_record_satisfies_snapshot () - BTREE_PROCESS_OBJECT_FUNCTION.
 *					Output visible objects according to
 *					snapshot. If snapshot is NULL, all
 *					objects are saved.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree info.
 * record (in)	   : B-tree leaf/overflow record.
 * object_ptr (in) : Pointer to object in record data.
 * oid (in)	   : Object OID.
 * class_oid (in)  : Object class OID.
 * mvcc_info (in)  : Object MVCC info.
 * stop (out)	   : Set to true if index is unique and visible object is
 *		     found and if this is not a debug build.
 * args (in/out)   : BTREE_REC_SATISFIES_SNAPSHOT_HELPER *.
 */
static int
btree_record_satisfies_snapshot (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
				 RECDES * record, char *object_ptr,
				 OID * oid, OID * class_oid,
				 BTREE_MVCC_INFO * mvcc_info, bool * stop,
				 void *args)
{
  /* Helper used to filter objects and OID's of visible ones. */
  BTREE_REC_SATISFIES_SNAPSHOT_HELPER *helper = NULL;
  MVCC_REC_HEADER mvcc_header_for_snapshot;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (record != NULL);
  assert (object_ptr != NULL);
  assert (oid != NULL);
  assert (class_oid != NULL);
  assert (mvcc_info != NULL);
  assert (stop != NULL);
  assert (args != NULL);

  helper = (BTREE_REC_SATISFIES_SNAPSHOT_HELPER *) args;

  btree_mvcc_info_to_heap_mvcc_header (mvcc_info, &mvcc_header_for_snapshot);
  if (helper->snapshot == NULL
      || helper->snapshot->snapshot_fnc (thread_p, &mvcc_header_for_snapshot,
					 helper->snapshot))
    {
      /* Snapshot satisfied or not required. */

      /* If unique index, we may need to match class OID. */
      if (BTREE_IS_UNIQUE (btid_int->unique_pk)
	  && !OID_ISNULL (&helper->match_class_oid)
	  && !OID_EQ (&helper->match_class_oid, class_oid))
	{
	  /* Class OID did not match. Ignore this object. */
	  return NO_ERROR;
	}

      /* Make sure that if this is unique index, only one visible object is
       * found.
       */
      assert (!BTREE_IS_UNIQUE (btid_int->unique_pk) || helper->oid_cnt == 0);

      /* Save OID in buffer. */
      memcpy (helper->oid_ptr, oid, sizeof (*oid));
      /* Increment buffer pointer and OID counter. */
      helper->oid_ptr += sizeof (*oid);
      helper->oid_cnt++;

#if defined (NDEBUG)
      if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	{
	  /* Stop after first visible object. Since this is a unique index,
	   * there shouldn't be more than one visible anyway.
	   */
	  *stop = true;
	  /* Debug doesn't stop. It will continue to check there are no other
	   * visible objects.
	   */
	}
#endif
    }

  /* Success */
  return NO_ERROR;
}

/*
 * xbtree_find_unique () - Find (and sometimes lock) object in key of unique
 *			   index.
 *
 * return		  : BTREE_SEARCH result.
 * thread_p (in)	  : Thread entry.
 * btid (in)		  : B-tree identifier.
 * scan_op_type (in)	  : Operation type (purpose) of finding unique key
 *			    object.
 * key (in)		  : Key value.
 * class_oid (in)	  : Class OID.
 * oid (out)		  : Found (and sometimes locked) object OID.
 * is_all_class_srch (in) : True if search is based on all classes contained
 *			    in the class hierarchy.
 */
BTREE_SEARCH
xbtree_find_unique (THREAD_ENTRY * thread_p, BTID * btid,
		    SCAN_OPERATION_TYPE scan_op_type, DB_VALUE * key,
		    OID * class_oid, OID * oid, bool is_all_class_srch)
{
  /* Helper used to describe find unique process and to output results. */
  BTREE_FIND_UNIQUE_HELPER find_unique_helper =
    BTREE_FIND_UNIQUE_HELPER_INITIALIZER;
  int error_code = NO_ERROR;
  BTREE_ADVANCE_WITH_KEY_FUNCTION *advance_function =
    btree_advance_and_find_key;
  BTREE_PROCESS_KEY_FUNCTION *key_function = NULL;
  MVCC_SNAPSHOT dirty_snapshot;
#if defined (SERVER_MODE)
  int lock_result;
  LOCK class_lock;
#endif

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (scan_op_type == S_SELECT || scan_op_type == S_SELECT_WITH_LOCK
	  || scan_op_type == S_DELETE || scan_op_type == S_UPDATE);
  assert (class_oid != NULL && !OID_ISNULL (class_oid));
  assert (oid != NULL);

  /* Initialize oid as NULL. */
  OID_SET_NULL (oid);

  if (key == NULL || db_value_is_null (key)
      || btree_multicol_key_is_null (key))
    {
      /* Early out: Consider key is not found. */
      return BTREE_KEY_NOTFOUND;
    }

  if (!is_all_class_srch)
    {
      /* Object class must match class_oid argument. */
      COPY_OID (&find_unique_helper.match_class_oid, class_oid);
    }

#if defined (SERVER_MODE)
  /* Make sure transaction has intention lock on table. */
  class_lock =
    (scan_op_type == S_SELECT
     || scan_op_type == S_SELECT_WITH_LOCK) ? IS_LOCK : IX_LOCK;
  lock_result =
    lock_object (thread_p, class_oid, oid_Root_class_oid, class_lock,
		 LK_UNCOND_LOCK);
  if (lock_result != LK_GRANTED)
    {
      return BTREE_ERROR_OCCURRED;
    }
#endif /* SERVER_MODE */

  if (scan_op_type == S_SELECT)
    {
      /* 
       * If MVCC disabled, do not use snapshot and lock. If MVCC enabled and
       * find unique in catalog classes, use dirty version without lock.
       * Otherwise, use dirty version with lock since need to check whether the
       * object exists.
       */
      if (heap_is_mvcc_disabled_for_class (class_oid))
	{
	  find_unique_helper.snapshot = NULL;
	  key_function = btree_key_find_unique_version_oid;
	  find_unique_helper.lock_mode = NULL_LOCK;
	}
      else
	{
	  dirty_snapshot.snapshot_fnc = mvcc_satisfies_dirty;
	  find_unique_helper.snapshot = &dirty_snapshot;

	  if (tf_is_catalog_class (class_oid))
	    {
	      /* find the key without lock */
	      key_function = btree_key_find_unique_version_oid;
	      find_unique_helper.lock_mode = NULL_LOCK;
	    }
	  else
	    {
	      /* find the key with lock */
	      key_function = btree_key_find_and_lock_unique;
	      find_unique_helper.lock_mode = S_LOCK;
	      scan_op_type = S_SELECT_WITH_LOCK;
	    }
	}
    }
  else
    {
      /* S_SELECT_LOCK_DIRTY, S_DELETE, S_UPDATE. */
      assert (scan_op_type == S_SELECT_WITH_LOCK
	      || scan_op_type == S_DELETE || scan_op_type == S_UPDATE);

      /* First key object must be locked and returned. */
      find_unique_helper.lock_mode =
	(scan_op_type == S_SELECT_WITH_LOCK) ? S_LOCK : X_LOCK;
      key_function = btree_key_find_and_lock_unique;
    }

  /* Find unique key and object. */
  error_code =
    btree_search_key_and_apply_functions (thread_p, btid, NULL, key,
					  NULL, NULL,
					  advance_function, NULL,
					  key_function, &find_unique_helper,
					  NULL, NULL);
  if (error_code != NO_ERROR)
    {
      /* Error! */
      /* Error must be set! */
      ASSERT_ERROR ();
#if defined (SERVER_MODE)
      /* Safe guard: don't keep lock if error has occurred. */
      assert (OID_ISNULL (&find_unique_helper.locked_oid));
#endif /* SERVER_MODE */
      return BTREE_ERROR_OCCURRED;
    }

#if defined (SERVER_MODE)
  /* Safe guard: if op is S_SELECT, nothing should be locked. */
  assert (scan_op_type != S_SELECT
	  || OID_ISNULL (&find_unique_helper.locked_oid));
#endif /* SERVER_MODE */

  if (find_unique_helper.found_object)
    {
      /* Key found. */
      /* Output found object OID. */
      assert (!OID_ISNULL (&find_unique_helper.oid));
      COPY_OID (oid, &find_unique_helper.oid);

#if defined (SERVER_MODE)
      /* Safe guard: object is supposed to be locked. */
      assert (scan_op_type == S_SELECT
	      || lock_has_lock_on_object (oid, class_oid,
					  thread_get_current_tran_index (),
					  find_unique_helper.lock_mode) > 0);
#endif /* SERVER_MODE */

      return BTREE_KEY_FOUND;
    }
  /* Key/object not found. */

#if defined (SERVER_MODE)
  /* Safe guard: no lock is kept if object was not found. */
  assert (OID_ISNULL (&find_unique_helper.locked_oid));
#endif /* SERVER_MODE */
  return BTREE_KEY_NOTFOUND;
}

/*
 * btree_count_oids () - BTREE_PROCESS_OBJECT_FUNCTION - Increment object
 *			 counter.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree info.
 * record (in)	   : B-tree leaf/overflow record.
 * object_ptr (in) : Pointer to object in record data.
 * oid (in)	   : Object OID.
 * class_oid (in)  : Object class OID.
 * mvcc_info (in)  : Object MVCC info.
 * stop (out)	   : Set to true if index is unique and visible object is
 *		     found and if this is not a debug build.
 * args (in/out)   : Integer object counter. Outputs incremented value.
 */
STATIC_INLINE int
btree_count_oids (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
		  RECDES * record, char *object_ptr, OID * oid,
		  OID * class_oid, MVCC_REC_HEADER * mvcc_header,
		  bool * stop, void *args)
{
  /* Assert expected arguments. */
  assert (args != NULL);

  /* Increment counter. */
  (*((int *) args))++;
  return NO_ERROR;
}

/*
 * btree_range_scan_count_oids_leaf_and_one_ovf () - Count key objects from
 *						     leaf record and from
 *						     the first overflow page.
 *
 * return	 : OID count or error code.
 * thread_p (in) : Thread entry.
 * bts (in)	 : B-tree scan.
 */
static int
btree_range_scan_count_oids_leaf_and_one_ovf (THREAD_ENTRY * thread_p,
					      BTREE_SCAN * bts)
{
  int leaf_oids_count = 0, ovf_oids_count = 0;
  int error_code = NO_ERROR;
  PAGE_PTR overflow_page = NULL;
  RECDES overflow_record;
  int oid_size = OR_OID_SIZE;

  if (BTREE_IS_UNIQUE (bts->btid_int.unique_pk))
    {
      oid_size += OR_OID_SIZE;
    }

  /* Count leaf objects. */
  leaf_oids_count =
    btree_record_get_num_oids (thread_p, &bts->btid_int, &bts->key_record,
			       bts->offset, BTREE_LEAF_NODE);
  if (leaf_oids_count < 0)
    {
      /* Error */
      ASSERT_ERROR ();
      return leaf_oids_count;
    }
  if (VPID_ISNULL (&bts->leaf_rec_info.ovfl))
    {
      /* No overflow. */
      return leaf_oids_count;
    }
  /* Get count from overflow. */
  overflow_page =
    pgbuf_fix (thread_p, &bts->leaf_rec_info.ovfl, OLD_PAGE, PGBUF_LATCH_READ,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (overflow_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  /* Get overflow record. */
  if (spage_get_record (overflow_page, 1, &overflow_record, PEEK)
      != S_SUCCESS)
    {
      assert_release (false);
      pgbuf_unfix_and_init (thread_p, overflow_page);
      return ER_FAILED;
    }
  ovf_oids_count =
    btree_record_get_num_oids (thread_p, &bts->btid_int, &overflow_record, 0,
			       BTREE_OVERFLOW_NODE);
  pgbuf_unfix_and_init (thread_p, overflow_page);
  if (ovf_oids_count < 0)
    {
      ASSERT_ERROR ();
      return ovf_oids_count;
    }
  /* Add objects count to total count. */
  return (leaf_oids_count + ovf_oids_count);
}

/*
 * btree_range_scan_start () - Start a range scan by finding the first
 *			       eligible key.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * bts (in)	 : B-tree scan structure.
 *
 * NOTE: Key is considered eligible if:
 *	 - Key is within desired range.
 *	 - Key is not a fence key.
 *	 - Key passes filter.
 */
static int
btree_range_scan_start (THREAD_ENTRY * thread_p, BTREE_SCAN * bts)
{
  int error_code = NO_ERROR;
  bool found = false;

  /* Assert expected arguments. */
  assert (bts != NULL);
  assert (VPID_ISNULL (&bts->C_vpid));
  assert (bts->C_page == NULL);

  /* Find starting key. */
  /* Starting key must be checked and pass filters first. */
  bts->key_status = BTS_KEY_IS_NOT_VERIFIED;
  if (bts->key_range.lower_key == NULL)
    {
      /* No lower limit. Just find lowest key in index. */
      error_code = btree_find_lower_bound_leaf (thread_p, bts, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (bts->end_scan)
	{
	  return NO_ERROR;
	}
    }
  else
    {
      /* Has lower limit. Try to locate the key. */
      bts->C_page =
	btree_locate_key (thread_p, &bts->btid_int, bts->key_range.lower_key,
			  &bts->C_vpid, &bts->slot_id, &found);
      if (bts->C_page == NULL)
	{
	  /* Error locating or fixing leaf. */
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      if (!found && bts->use_desc_index)
	{
	  /* Key was not found and the bts->slot_id was positioned to next key
	   * bigger than bts->key_range.lower_key. For descending scan, we
	   * should be positioned on the first smaller when key is not found.
	   * Update bts->slot_id.
	   */
	  bts->slot_id--;
	}
      if (found
	  && (bts->key_range.range == GT_LT || bts->key_range.range == GT_LE
	      || bts->key_range.range == GT_INF))
	{
	  /* Lower limit key was found, but the scan range must be bigger than
	   * the limit. Go to next key.
	   */
	  /* Mark the key as consumed and let
	   * btree_range_scan_advance_over_filtered_keys handle it.
	   */
	  bts->key_status = BTS_KEY_IS_CONSUMED;
	}
    }
  /* Found starting key. */
  error_code = btree_range_scan_advance_over_filtered_keys (thread_p, bts);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* Start scanning */
  bts->is_scan_started = true;
  return NO_ERROR;
}

/*
 * btree_range_scan_resume () - Function used to resume range scans after
 *				being interrupted. It will try to resume from
 *				saved leaf node (if possible). Otherwise,
 *				current key must looked up starting from
 *				b-tree root.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * bts (in)	 : B-tree scan helper.
 */
static int
btree_range_scan_resume (THREAD_ENTRY * thread_p, BTREE_SCAN * bts)
{
  int error_code = NO_ERROR;
  BTREE_SEARCH_KEY_HELPER search_key = BTREE_SEARCH_KEY_HELPER_INITIALIZER;
  BTREE_NODE_HEADER *node_header = NULL;
  bool found = false;

  assert (bts->force_restart_from_root || !VPID_ISNULL (&bts->C_vpid));
  assert (bts->C_page == NULL);
  assert (!DB_IS_NULL (&bts->cur_key));

  /* Resume range scan. It can be resumed from same leaf or by looking up the
   * key again from root.
   */
  if (!bts->force_restart_from_root)
    {
      /* Try to resume from saved leaf node. */
      bts->C_page =
	pgbuf_fix_without_validation (thread_p, &bts->C_vpid, OLD_PAGE,
				      PGBUF_LATCH_READ,
				      PGBUF_UNCONDITIONAL_LATCH);
      if (bts->C_page == NULL)
	{
	  /* Error */
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}

      if (LSA_EQ (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page)))
	{
	  /* Leaf page suffered no changes while range search was interrupted.
	   * Range search can be resumed using current position.
	   */
	  return btree_range_scan_advance_over_filtered_keys (thread_p, bts);
	}

      /* Page suffered some changes. */
      if (BTREE_IS_PAGE_VALID_LEAF (thread_p, bts->C_page))
	{
	  /* Page is still a valid leaf page. Check if key still exists. */

	  /* Is key still in this page? */
	  error_code =
	    btree_search_leaf_page (thread_p, &bts->btid_int, bts->C_page,
				    &bts->cur_key, &search_key);
	  if (error_code != NO_ERROR)
	    {
	      /* Error! */
	      pgbuf_unfix_and_init (thread_p, bts->C_page);
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  switch (search_key.result)
	    {
	    case BTREE_KEY_FOUND:
	      /* Key was found. Use this key. */
	      bts->slot_id = search_key.slotid;
	      return btree_range_scan_advance_over_filtered_keys (thread_p,
								  bts);

	    case BTREE_KEY_BETWEEN:
	      /* Key should have been in this page, but it was removed.
	       * Proceed to next key.
	       */
	      if (bts->use_desc_index)
		{
		  /* Use previous slot. */
		  bts->slot_id = search_key.slotid - 1;
		  assert (bts->slot_id >= 1
			  && bts->slot_id <=
			  btree_node_number_of_keys (bts->C_page));
		}
	      else
		{
		  /* Use next slotid. */
		  bts->slot_id = search_key.slotid;
		  assert (bts->slot_id >= 1
			  && bts->slot_id <=
			  btree_node_number_of_keys (bts->C_page));
		}
	      bts->key_status = BTS_KEY_IS_NOT_VERIFIED;
	      return btree_range_scan_advance_over_filtered_keys (thread_p,
								  bts);

	    case BTREE_KEY_SMALLER:
	    case BTREE_KEY_BIGGER:
	      /* Key is no longer in this leaf node. Locate key by advancing
	       * from root.
	       */
	      /* Fall through. */
	      break;

	    default:
	      /* Unexpected. */
	      assert (false);
	      return ER_FAILED;
	    }
	}
      else			/* !BTREE_IS_PAGE_VALID_LEAF (bts->C_page) */
	{
	  /* Page must have been deallocated/reused for other purposes. */
	  /* Fall through. */
	}
      pgbuf_unfix_and_init (thread_p, bts->C_page);
    }
  /* Couldn't resume from saved leaf node. */

  /* No page should be fixed. */
  assert (bts->C_page == NULL);

  /* Reset bts->force_restart_from_root flag. */
  bts->force_restart_from_root = false;

  /* Search key from top. */
  bts->C_page =
    btree_locate_key (thread_p, &bts->btid_int, &bts->cur_key, &bts->C_vpid,
		      &bts->slot_id, &found);
  if (bts->C_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  /* Safe guard. */
  assert (btree_get_node_level (bts->C_page) == 1);
  if (found)
    {
      /* Found key. Resume from here. */
      return btree_range_scan_advance_over_filtered_keys (thread_p, bts);
    }
  if (btree_node_number_of_keys (bts->C_page) < 1)
    {
      assert (btree_node_number_of_keys (bts->C_page) == 0);
      /* No more keys. */
      bts->end_scan = true;
      return NO_ERROR;
    }
  /* Not found. */
  if (bts->use_desc_index)
    {
      bts->slot_id = bts->slot_id - 1;
    }
  bts->key_status = BTS_KEY_IS_NOT_VERIFIED;
  return btree_range_scan_advance_over_filtered_keys (thread_p, bts);
}

/*
 * btree_range_scan_read_record () - Read b-tree record for b-tree range scan.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * bts (in/out)	 : B-tree scan.
 */
static int
btree_range_scan_read_record (THREAD_ENTRY * thread_p, BTREE_SCAN * bts)
{
  /* Clear current key value if needed. */
  btree_scan_clear_key (bts);
  /* Read record key (and other info). */
  return btree_read_record (thread_p, &bts->btid_int, bts->C_page,
			    &bts->key_record, &bts->cur_key,
			    &bts->leaf_rec_info, bts->node_type,
			    &bts->clear_cur_key, &bts->offset, COPY, bts);
}

/*
 * btree_range_scan_advance_over_filtered_keys () - Find a key to pass all
 *						    all filters.
 *
 * return	  : Error code.
 * thread_p (in)  : Thread entry.
 * bts (in/out)	  : B-tree scan helper.
 */
static int
btree_range_scan_advance_over_filtered_keys (THREAD_ENTRY * thread_p,
					     BTREE_SCAN * bts)
{
  int inc_slot;			/* Slot incremental value to advance to
				 * next key.
				 */
  VPID next_vpid;		/* VPID of next leaf. */
  BTREE_NODE_HEADER *node_header;	/* Leaf node header. */
  int key_count;		/* Node key count. */
  PAGE_PTR next_node_page = NULL;	/* Page pointer to next leaf node. */
  int error_code;		/* Error code. */
  bool is_range_satisfied;	/* True if range is satisfied. */
  bool is_filter_satisfied;	/* True if filter is satisfied. */

  /* Assert expected arguments. */
  assert (bts != NULL);
  assert (bts->C_page != NULL);

  /* Initialize. */
  node_header = btree_get_node_header (bts->C_page);
  assert (node_header != NULL);
  assert (node_header->node_level == 1);

  if (bts->key_status == BTS_KEY_IS_VERIFIED)
    {
      /* Current key is not yet consumed, but it already passed range/filter
       * checks. Resume scan on current key.
       */

      /* Safe guard: this is not a fence key. */
      assert (!btree_is_fence_key (bts->C_page, bts->slot_id));

      /* This must be a resumed scan. Page was probably unfixed since last
       * key read and may be changed. To be sure, obtain record again.
       */

      /* Read record. */
      /* Get current key. */
      if (spage_get_record (bts->C_page, bts->slot_id, &bts->key_record, PEEK)
	  != S_SUCCESS)
	{
	  /* Unexpected error. */
	  assert (false);
	  return ER_FAILED;
	}
      /* TODO: Don't clear/copy key again. */
      error_code = btree_range_scan_read_record (thread_p, bts);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      /* Continue with current key. */
      return NO_ERROR;
    }
  /* Current key is completely consumed or is a new key. */
  assert (bts->key_status == BTS_KEY_IS_CONSUMED
	  || bts->key_status == BTS_KEY_IS_NOT_VERIFIED);

  if (bts->key_range_max_value_equal)
    {
      /* Range was already consumed. End the scan. */
      bts->end_scan = true;
      return NO_ERROR;
    }

  /* If current key is not new and is completely consumed, go to next key. */
  inc_slot = bts->use_desc_index ? -1 : 1;
  if (bts->key_status == BTS_KEY_IS_CONSUMED)
    {
      /* Go to next key. */
      bts->slot_id += inc_slot;
    }
  else
    {
      /* Assert expected key status. */
      assert (bts->key_status == BTS_KEY_IS_NOT_VERIFIED);
    }

  /* Get VPID of next leaf and key count in current leaf. */
  next_vpid =
    bts->use_desc_index ? node_header->prev_vpid : node_header->next_vpid;
  key_count = btree_node_number_of_keys (bts->C_page);
  assert (key_count >= 0);

  while (true)
    {
      /* Safe guard: current leaf is fixed. */
      assert (bts->C_page != NULL);

      /* If key is not in the range 1 -> key count, a different leaf node
       * must be fixed.
       * This while should pass over all empty or consumed leaf nodes.
       */
      while (bts->slot_id <= 0 || bts->slot_id > key_count || key_count == 0)
	{
	  /* Current leaf node was consumed (or was empty).
	   * Try next leaf node.
	   */

	  /* If scan is descending, current slot_id is expected to be 0,
	   * if it is ascending, then slot_id must be key_count + 1.
	   */
	  assert ((bts->use_desc_index && bts->slot_id == 0)
		  || (!bts->use_desc_index && bts->slot_id == key_count + 1));
	  if (VPID_ISNULL (&next_vpid))
	    {
	      /* Index was consumed. */
	      bts->end_scan = true;
	      return NO_ERROR;
	    }
	  if (bts->use_desc_index)
	    {
	      /* Descending index will try conditional latch on previous leaf.
	       * If that fails, current leaf will be unfixed and pages fixed
	       * in normal order. However, until pages are successfully fixed,
	       * it is possible that they are not reusable and scan must
	       * be restarted from root.
	       * We assume and hope this is does not happen too often
	       * (must see).
	       */
	      error_code =
		btree_range_scan_descending_fix_prev_leaf (thread_p, bts,
							   &key_count,
							   &node_header,
							   &next_vpid);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return ER_FAILED;
		}
	      if (bts->force_restart_from_root)
		{
		  /* Failed to continue descending scan. Restart from root. */
		  return NO_ERROR;
		}
	    }
	  else
	    {
	      /* Fix next leaf page. */
	      next_node_page =
		pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
			   PGBUF_UNCONDITIONAL_LATCH);
	      if (next_node_page == NULL)
		{
		  ASSERT_ERROR_AND_SET (error_code);
		  return error_code;
		}
	      /* Advance to next node. */
	      pgbuf_unfix (thread_p, bts->C_page);
	      bts->C_page = next_node_page;
	      VPID_COPY (&bts->C_vpid, &next_vpid);
	      next_node_page = NULL;
	      /* Initialize stuff. */
	      key_count = btree_node_number_of_keys (bts->C_page);
	      node_header = btree_get_node_header (bts->C_page);
	      assert (node_header != NULL);
	      assert (node_header->node_level == 1);
	      /* Ascending scan: start from first key in page and then advance
	       * to next page.
	       */
	      bts->slot_id = 1;
	      next_vpid = node_header->next_vpid;
	    }
	}

      /* Get current key. */
      if (spage_get_record (bts->C_page, bts->slot_id, &bts->key_record, PEEK)
	  != S_SUCCESS)
	{
	  /* Unexpected error. */
	  assert (false);
	  return ER_FAILED;
	}

      if (!btree_leaf_is_flaged (&bts->key_record, BTREE_LEAF_RECORD_FENCE))
	{
	  /* Not a fence key. */
	  /* Handle. */
	  error_code = btree_range_scan_read_record (thread_p, bts);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  error_code =
	    btree_apply_key_range_and_filter (thread_p, bts,
					      BTS_IS_INDEX_ISS (bts),
					      &is_range_satisfied,
					      &is_filter_satisfied,
					      bts->need_to_check_null);
	  if (!is_range_satisfied)
	    {
	      /* Range is not satisfied, which means scan is ended. */
	      bts->end_scan = true;
	      return NO_ERROR;
	    }
	  /* Range satisfied. */

	  if (is_filter_satisfied)
	    {
	      /* Filter is satisfied, which means key can be used. */
	      bts->read_keys++;
	      bts->key_status = BTS_KEY_IS_VERIFIED;
	      return NO_ERROR;
	    }
	  /* Filter not satisfied. Try next key. */
	  /* Fall through. */
	}
      else
	{
	  /* Key is fence and must be filtered. */
	  /* Safe guard: Fence keys can be only first or last keys in page,
	   * but never first or last in entire index.
	   */
	  assert ((bts->slot_id == 1
		   && !VPID_ISNULL (&node_header->prev_vpid))
		  || (bts->slot_id == key_count
		      && !VPID_ISNULL (&node_header->next_vpid)));
	  /* Fall through. */

	  /* TODO: Get key info should be able to obtain fences too. */
	}

      /* Key is not usable. Advance. */
      bts->slot_id += inc_slot;
    }
  /* Impossible to reach. */
  assert (false);
  return ER_FAILED;
}

/*
 * btree_range_scan_descending_fix_prev_leaf () - Fix previous leaf node
 *						  without generating cross
 *						  latches with regular scans
 *						  and by trying to avoid a key
 *						  lookup from root.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * bts (in)		    : B-tree scan data.
 * key_count (in/out)	    : Current page key count.
 * node_header_ptr (in/out) : Current page node header.
 * next_vpid (in/out)	    : Next (actually previous) leaf VPID.
 */
static int
btree_range_scan_descending_fix_prev_leaf (THREAD_ENTRY * thread_p,
					   BTREE_SCAN * bts,
					   int *key_count,
					   BTREE_NODE_HEADER **
					   node_header_ptr, VPID * next_vpid)
{
  PAGE_PTR prev_leaf = NULL;	/* Page pointer to previous leaf
				 * node.
				 */
  VPID prev_leaf_vpid;		/* VPID of previous leaf node. */
  int error_code = NO_ERROR;	/* Error code. */
  /* Search key result. */
  BTREE_SEARCH_KEY_HELPER search_key = BTREE_SEARCH_KEY_HELPER_INITIALIZER;

  /* Assert expected arguments. */
  assert (bts != NULL && bts->use_desc_index == true);
  assert (key_count != NULL);
  assert (next_vpid != NULL);

  VPID_COPY (&prev_leaf_vpid, next_vpid);

  /* Conditional latch for previous page. */
  prev_leaf =
    pgbuf_fix (thread_p, &prev_leaf_vpid, OLD_PAGE, PGBUF_LATCH_READ,
	       PGBUF_CONDITIONAL_LATCH);
  if (prev_leaf != NULL)
    {
      /* Previous leaf was successfully latched. Advance. */
      pgbuf_unfix_and_init (thread_p, bts->C_page);
      bts->C_page = prev_leaf;
      VPID_COPY (&bts->C_vpid, &prev_leaf_vpid);
      *key_count = btree_node_number_of_keys (bts->C_page);
      bts->slot_id = *key_count;
      *node_header_ptr = btree_get_node_header (bts->C_page);
      VPID_COPY (next_vpid, &(*node_header_ptr)->prev_vpid);
      return NO_ERROR;
    }
  /* Conditional latch failed. */

  /* Unfix current page and retry. */
  pgbuf_unfix_and_init (thread_p, bts->C_page);
  prev_leaf =
    pgbuf_fix_without_validation (thread_p, &prev_leaf_vpid, OLD_PAGE,
				  PGBUF_LATCH_READ,
				  PGBUF_UNCONDITIONAL_LATCH);
  if (prev_leaf == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  if (!BTREE_IS_PAGE_VALID_LEAF (thread_p, prev_leaf))
    {
      /* Page deallocated/reused, but not currently a valid b-tree leaf. */
      /* Try again from top. */
      bts->force_restart_from_root = true;
      pgbuf_unfix_and_init (thread_p, prev_leaf);
      return NO_ERROR;
    }
  /* Valid leaf. */

  *node_header_ptr = btree_get_node_header (prev_leaf);
  if (!VPID_EQ (&(*node_header_ptr)->next_vpid, &bts->C_vpid))
    {
      /* No longer linked leaves. Restart search from top. */
      bts->force_restart_from_root = true;
      pgbuf_unfix_and_init (thread_p, prev_leaf);
      return NO_ERROR;
    }
  /* Pages are still linked. */

  /* Fix current page too. */
  bts->C_page =
    pgbuf_fix (thread_p, &bts->C_vpid, OLD_PAGE, PGBUF_LATCH_READ,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (bts->C_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      pgbuf_unfix_and_init (thread_p, prev_leaf);
      return error_code;
    }

  /* Before searching the key in leaf page, we must make sure to handle this
   * next peculiar case:
   * 1. First key search of descending scan. Lower key limit is located as
   *    first in leaf page.
   * 2. Range scan says strictly less than lower key limit.
   * 3. The algorithm tries to fix go to previous leaf. However, bts->cur_key
   *    does not yet store any key values.
   *
   * Set bts->cur_key to lower key limit of range.
   * NOTE: If there is no lower limit, this case cannot happen.
   */
  if (!bts->is_scan_started && bts->key_range.lower_key != NULL
      && DB_IS_NULL (&bts->cur_key))
    {
      pr_clone_value (bts->key_range.lower_key, &bts->cur_key);
      /* Next steps will go before bts->key_range.lower_key. */
    }
  /* We must have a non-null current key. */
  assert (!DB_IS_NULL (&bts->cur_key));

  /* Normally, key is found in current page. */
  error_code =
    btree_search_leaf_page (thread_p, &bts->btid_int, bts->C_page,
			    &bts->cur_key, &search_key);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, prev_leaf);
      return error_code;
    }
  switch (search_key.result)
    {
    case BTREE_KEY_BETWEEN:
      /* Still in current page. Current slotid cannot be 1. */
      assert (search_key.slotid > 1);
      /* Fall through to advance to previous key. Search function returned
       * the first bigger key.
       */
    case BTREE_KEY_FOUND:
      if (search_key.slotid > 1)
	{
	  /* Must check remaining keys in current page. */
	  bts->slot_id = search_key.slotid - 1;

	  *key_count = btree_node_number_of_keys (bts->C_page);
	  *node_header_ptr = btree_get_node_header (bts->C_page);
	  VPID_COPY (next_vpid, &(*node_header_ptr)->prev_vpid);

	  /* Unfix previous page. */
	  pgbuf_unfix_and_init (thread_p, prev_leaf);
	  return NO_ERROR;
	}

      /* Found key in the first slot. */
      /* Safe guard: could not fall through from BTREE_KEY_BETWEEN. */
      assert (search_key.result == BTREE_KEY_FOUND);
      /* Move to previous page. */
      pgbuf_unfix_and_init (thread_p, bts->C_page);
      bts->C_page = prev_leaf;
      VPID_COPY (&bts->C_vpid, &prev_leaf_vpid);
      *key_count = btree_node_number_of_keys (bts->C_page);
      VPID_COPY (next_vpid, &(*node_header_ptr)->prev_vpid);
      bts->slot_id = *key_count;
      return NO_ERROR;

    case BTREE_KEY_BIGGER:
      /* TODO: Go to next page until key is found? */
      /* For now just fall through to restart. */
    case BTREE_KEY_NOTFOUND:
      /* Unknown key fate. */
      bts->force_restart_from_root = true;
      pgbuf_unfix_and_init (thread_p, prev_leaf);
      return NO_ERROR;

    case BTREE_KEY_SMALLER:
      /* Fall through to try search key in previous page. */
      break;

    default:
      /* Unhandled. */
      assert_release (false);
      pgbuf_unfix_and_init (thread_p, prev_leaf);
      return ER_FAILED;
    }
  /* Search key in previous page. */
  assert (search_key.result == BTREE_KEY_SMALLER);
  /* Unfix current page. */
  pgbuf_unfix_and_init (thread_p, bts->C_page);
  error_code =
    btree_search_leaf_page (thread_p, &bts->btid_int, prev_leaf,
			    &bts->cur_key, &search_key);
  switch (search_key.result)
    {
    case BTREE_KEY_BIGGER:
    case BTREE_KEY_BETWEEN:
      /* First bigger key. */
      assert (search_key.slotid > 1);
      /* Fall through to advance to previous key. */
    case BTREE_KEY_FOUND:
      /* Update bts and advance to previous key. */
      bts->C_page = prev_leaf;
      VPID_COPY (&bts->C_vpid, &prev_leaf_vpid);
      *key_count = btree_node_number_of_keys (bts->C_page);
      VPID_COPY (next_vpid, &(*node_header_ptr)->prev_vpid);
      bts->slot_id = search_key.slotid - 1;

      /* If this was the first key in page, now slot_id will be 0. */
      /* This function should get called again, but looking for another
       * previous leaf node.
       */
      return NO_ERROR;

    default:
      /* BTREE_KEY_SMALLER */
      /* BTREE_KEY_NOTFOUND */
      /* Unknown key fate. */
      bts->force_restart_from_root = true;
      pgbuf_unfix_and_init (thread_p, prev_leaf);
      return NO_ERROR;
    }

  /* Impossible to reach. */
  assert_release (false);
  return ER_FAILED;
}

/*
 * btree_range_scan () - Generic function to do a range scan on b-tree. It
 *			 can scan key by key starting with first (or last
 *			 key for descending scans). For each key, it calls an
 *			 internal function to process the key.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * bts (in)	      : B-tree scan structure.
 * key_func (in)      : Internal function to call when an eligible key is
 *			found.
 */
int
btree_range_scan (THREAD_ENTRY * thread_p, BTREE_SCAN * bts,
		  BTREE_RANGE_SCAN_PROCESS_KEY_FUNC * key_func)
{
  int error_code = NO_ERROR;	/* Error code. */

  /* Assert expected arguments. */
  assert (bts != NULL);
  assert (key_func != NULL);

  /* Reset end_scan and end_one_iteration flags. */
  bts->end_scan = false;
  bts->end_one_iteration = false;

  /* Reset read counters for iteration. */
  bts->n_oids_read_last_iteration = 0;

  if (bts->index_scan_idp != NULL && bts->index_scan_idp->oid_list != NULL)
    {
      /* Reset oid_ptr. */
      bts->oid_ptr = bts->index_scan_idp->oid_list->oidp;
    }

  while (!bts->end_scan && !bts->end_one_iteration)
    {
      bts->is_interrupted = false;

      /* Start from top or resume. */
      if (!bts->is_scan_started)
	{
	  /* Must find a starting key. */
	  error_code = btree_range_scan_start (thread_p, bts);
	}
      else
	{
	  /* Resume from current key. */
	  error_code = btree_range_scan_resume (thread_p, bts);
	}
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      if (bts->end_scan)
	{
	  /* Scan is finished. */
	  break;
	}
      if (bts->force_restart_from_root)
	{
	  /* Couldn't advance. Restart from root. */
	  assert (bts->use_desc_index);
	  /* TODO: This is a notification to see how often this happens.
	   *       If it is spamming remove it.
	   *       Also maybe give up descending scan after a number of
	   *       restarts.
	   */
	  er_log_debug (ARG_FILE_LINE,
			"Notification: descending range scan had to be "
			"interrupted and restarted from root.\n");
	  if (bts->C_page != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, bts->C_page);
	    }
	  continue;
	}

      /* Scan is now positioned on an usable key; consumed/fence/filtered keys
       * have been skipped.
       */

      while (true)
	{
	  /* For each valid key */
	  assert (bts->C_page != NULL);
	  assert (!VPID_ISNULL (&bts->C_vpid));
	  assert (bts->O_page == NULL);
	  /* Do we need P_page here? */
	  assert (bts->P_page == NULL);
	  assert (bts->key_status == BTS_KEY_IS_VERIFIED);
	  assert (!bts->is_interrupted && !bts->end_one_iteration
		  && !bts->end_scan);

	  /* Call internal key function. */
	  error_code = key_func (thread_p, bts);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	  if (bts->is_interrupted || bts->end_one_iteration || bts->end_scan)
	    {
	      /* Interrupt key processing loop. */
	      break;
	    }
	  /* Current key must be consumed. Find a new valid key. */
	  assert (bts->key_status == BTS_KEY_IS_CONSUMED);
	  error_code =
	    btree_range_scan_advance_over_filtered_keys (thread_p, bts);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	  if (bts->end_scan)
	    {
	      /* Finished scan. */
	      break;
	    }
	  if (bts->force_restart_from_root)
	    {
	      /* Couldn't advance. Restart from root. */
	      assert (bts->use_desc_index);
	      /* TODO: This is a notification to see how often this happens.
	       *       If it is spamming remove it.
	       *       Also maybe give up descending scan after a number of
	       *       restarts.
	       */
	      er_log_debug (ARG_FILE_LINE,
			    "Notification: descending range scan had to be "
			    "interrupted and restarted from root.\n");
	      if (bts->C_page != NULL)
		{
		  pgbuf_unfix_and_init (thread_p, bts->C_page);
		}
	      break;
	    }
	}
    }

end:
  /* End scan or end one iteration or maybe an error case. */
  assert (bts->end_scan || bts->end_one_iteration || error_code != NO_ERROR);
  if (bts->end_scan)
    {
      /* Scan is ended. Reset current page VPID and is_scan_started flag */
      VPID_SET_NULL (&bts->C_vpid);
      bts->is_scan_started = false;

      /* Clear current key (if not already cleared). */
      btree_scan_clear_key (bts);

      assert (VPID_ISNULL (&bts->O_vpid));
    }

  if (bts->C_page != NULL)
    {
      /* Unfix current page and save its LSA. */
      assert (bts->end_scan
	      || VPID_EQ (pgbuf_get_vpid_ptr (bts->C_page), &bts->C_vpid));
      LSA_COPY (&bts->cur_leaf_lsa, pgbuf_get_lsa (bts->C_page));
      pgbuf_unfix_and_init (thread_p, bts->C_page);
    }
  else
    {
      /* No current page fixed. */
      LSA_SET_NULL (&bts->cur_leaf_lsa);
    }
  assert (bts->O_page == NULL);

  if (bts->P_page != NULL)
    {
      /* bts->P_page is still used by btree_find_boundary_leaf. Make sure it
       * is unfixed.
       */
      VPID_SET_NULL (&bts->P_vpid);
      pgbuf_unfix_and_init (thread_p, bts->P_page);
    }

  if (bts->key_limit_upper != NULL)
    {
      /* Update upper limit for next scans. */
      (*bts->key_limit_upper) -= bts->n_oids_read_last_iteration;
      assert ((*bts->key_limit_upper) >= 0);
    }

  return error_code;

exit_on_error:
  error_code = error_code != NO_ERROR ? error_code : ER_FAILED;
  goto end;
}

/*
 * btree_range_scan_select_visible_oids () - BTREE_RANGE_SCAN_PROCESS_KEY_FUNC
 *					     Used internally by
 *					     btree_range_scan to select
 *					     visible objects from key OID's.
 *					     Handling depends on the type of
 *					     scan:
 *					     1. Multiple ranges optimization.
 *					     2. Covering index.
 *					     3. Index skip scan.
 *					     4. Regular index scan.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * bts (in)	   : B-tree scan info.
 */
int
btree_range_scan_select_visible_oids (THREAD_ENTRY * thread_p,
				      BTREE_SCAN * bts)
{
  /* Helper used to process record and find visible object. */
  BTREE_REC_SATISFIES_SNAPSHOT_HELPER rec_process_helper =
    BTREE_REC_SATISFIES_SNAPSHOT_HELPER_INITIALIZER;
  int error_code = NO_ERROR;	/* Returned error code. */
  int oid_count;		/* Total number of objects of this
				 * key. Overflow OID's are also
				 * considered.
				 * For unique indexes, only one object is
				 * considered (we know for sure there can not
				 * be more than one visible).
				 */
  int total_oid_count;		/* Total count of key OID's
				 * considering OID's already read
				 * and all OID's of current key.
				 */
  int remaining_oid_count = 0;	/* The number of object still to be processed.
				 */
  bool stop = false;		/* Set to true when processing record should
				 * stop.
				 */
  VPID overflow_vpid;		/* Overflow VPID. */
  PAGE_PTR overflow_page = NULL;	/* Current overflow page. */
  PAGE_PTR prev_overflow_page = NULL;	/* Previous overflow page. */
  RECDES ovf_record;		/* Overflow page record. */
  int oid_size = OR_OID_SIZE;	/* Object size (OID/class OID). */
  int save_oid_count = 0;	/* While processing the overflow pages
				 * we are required to save the last
				 * page that had any visible objects.
				 * Use this to save object counter
				 * before processing overflow and
				 * compare with the count of objects
				 * after processing the overflow.
				 */
  VPID last_visible_overflow;	/* VPID of last overflow page that had
				 * at least one visible OID.
				 * If the scan is interrupted because
				 * too many objects were processed,
				 * it will be resumed after this
				 * overflow page.
				 */

  /* Assert b-tree scan is valid. */
  assert (bts != NULL);
  assert (!VPID_ISNULL (&bts->C_vpid));
  assert (bts->C_page != NULL);
  /* MRO and covering index optimization are not compatible with
   * bts->need_count_only.
   */
  assert (!BTS_NEED_COUNT_ONLY (bts)
	  || (!BTS_IS_INDEX_MRO (bts) && !BTS_IS_INDEX_COVERED (bts)));

  /* Index skip scan optimization has an early out when a new key is found: */
  if (BTS_IS_INDEX_ISS (bts)
      && (bts->index_scan_idp->iss.current_op == ISS_OP_GET_FIRST_KEY
	  || bts->index_scan_idp->iss.current_op
	  == ISS_OP_SEARCH_NEXT_DISTINCT_KEY))
    {
      /* A new key was found. End current scan here (another range scan will
       * be started after a range based on current key is defined.
       */
      /* Set key. */
      error_code = btree_iss_set_key (bts, &bts->index_scan_idp->iss);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
      /* Set oid_cnt to 1. */
      bts->n_oids_read_last_iteration = 1;

      /* End this scan here. */
      bts->end_scan = true;
      return NO_ERROR;
    }
  /* Not an early out of ISS optimization. */
  /* Safe guard: Not an ISS or ISS current op is ISS_OP_DO_RANGE_SEARCH. */
  assert (!BTS_IS_INDEX_ISS (bts)
	  || bts->index_scan_idp->iss.current_op == ISS_OP_DO_RANGE_SEARCH);

  /* ISS and regular scans can use a buffer to store read OID's. Covering
   * index optimization uses a list file, while MRO uses a Top N structure.
   * When OID buffer is used, it can be filled during scan. If this happens,
   * interrupt range scan, handle currently buffered objects and resume with
   * an empty buffer.
   * NOTE: There are two types of limits used here. A soft limit and hard
   *       limit. Hard limit is used when key has too many objects and don't
   *       fit soft limit. Hard limit is necessary to handle key processing
   *       interrupt/resume.
   *       Soft key limit is ignored if no objects have been processed in this
   *       iteration.
   */
  /* Don't do any checks for MRO or if objects only have to be counted. */
  if (!BTS_IS_INDEX_MRO (bts) && !BTS_NEED_COUNT_ONLY (bts))
    {
      if (!bts->is_key_partially_processed)
	{
	  /* If no objects have been processed this iteration, don't count.
	   * If already processed more than soft limit, don't count.
	   * If currently process + key's leaf and first overflow count exceed
	   * soft limit, stop iteration before processing key.
	   */
	  if (bts->n_oids_read_last_iteration > 0
	      && BTS_IS_SOFT_CAPACITY_ENOUGH (bts,
					      bts->
					      n_oids_read_last_iteration))
	    {
	      /* Count the objects. */
	      if (BTREE_IS_UNIQUE (bts->btid_int.unique_pk))
		{
		  /* Only one object can be visible for each key. */
		  oid_count = 1;
		}
	      else
		{
		  /* Get the number of OID in current key's leaf and first
		   * overflow page.
		   */
		  oid_count =
		    btree_range_scan_count_oids_leaf_and_one_ovf (thread_p,
								  bts);
		  if (oid_count < 0)
		    {
		      /* Unexpected error. */
		      assert (false);
		      return ER_FAILED;
		    }
		}
	      total_oid_count = bts->n_oids_read_last_iteration + oid_count;

	      if (!BTS_IS_SOFT_CAPACITY_ENOUGH (bts, total_oid_count))
		{
		  /* Stop this iteration and resume with an empty buffer. */
		  bts->end_one_iteration = true;
		  return NO_ERROR;
		}
	      else
		{
		  /* Safe guard: if soft capacity is enough, then hard limit
		   * must also be enough.
		   */
		  assert (BTS_IS_HARD_CAPACITY_ENOUGH (bts, total_oid_count));
		}
	      /* There is enough space to handle key objects at least in its
	       * leaf and first overflow.
	       */
	    }
	}
      else
	{
	  /* Key was not fully processed. Resume from its current overflow. */
	  /* The interrupt algorithm is based on next facts:
	   * 1. Objects can be vacuumed. Overflow pages can be deallocated
	   *    when all its objects are vacuumed. Entire key cannot be
	   *    vacuumed while interrupted (because at least one visible
	   *    object existed in previous iteration).
	   * 2. Objects can be swapped from overflow page to leaf record.
	   *    Only one object from first overflow page is swapped. If this
	   *    object is not visible, it can be vacuumed again and another
	   *    object is swapped. This can continue until this thread resumes
	   *    scan or until a visible object is swapped.
	   *    Note that first overflow page will be deallocated if all its
	   *    objects have been swapped. This is possible if page has at
	   *    most one visible object that cannot be vacuumed.
	   * 3. New objects can be inserted and new overflow pages can be
	   *    created.
	   *
	   * Interrupt/resume system tries to work with constants in this
	   * behavior:
	   * 1. A key without overflow pages is never interrupted (actually
	   *    a key without at least four overflow pages is never
	   *    interrupted since all its objects can be processed in default
	   *    buffer).
	   * 2. If interrupted, the buffer should have at least default
	   *    number of OID's - one overflow page of OID's. This means
	   *    roughly default_buffer_size / OR_OID_SIZE -
	   *    db_page_size / OID_WITH_MVCC_INFO_SIZE = 16k*4/8 - 16k/32 with
	   *    default parameters = ~7.5k objects.
	   * 3. When interrupted, bts saves last overflow page with at least
	   *    one visible object to make sure it is not deallocated.
	   *    How can an overflow page be deallocated? Well first of all if
	   *    all its objects are invisible and vacuumed. If it had any
	   *    visible objects, it should be swapped to leaf record. Since
	   *    only one visible object can be swapped to leaf, the page must
	   *    have at most one visible object.
	   *    However, swapping starts with first overflow page. But this
	   *    scan has processed at least 7.5k visible objects, worth at
	   *    least several full pages. The last page, even if it had only
	   *    one visible object, it wouldn't get swapped to leaf.
	   * 4. Can swapping objects to leaf be a problem? No. It can be
	   *    considered already processed and ignored. Since there have
	   *    been at least 7.5k visible objects processed in previous
	   *    iteration, the first leaf object, if visible, must be one of
	   *    these.
	   * 5. Inserting new OID's will not affect our scan in any way. If
	   *    they are found after resume, they will be ignored by
	   *    visibility test. If not found, they are again ignored (as they
	   *    should). Even creating new overflow pages, does not affect us.
	   *
	   * The above statements are true for default buffer.
	   * In order to make it true for small buffers, there are two limits
	   * used by scan: a soft limit and a hard limit.
	   * The hard limit is used when key has too many objects.
	   * See comment from BTS_IS_HARD_CAPACITY_ENOUGH.
	   */
	  /* Resume from next page of last overflow page. */
	  prev_overflow_page =
	    pgbuf_fix (thread_p, &bts->O_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		       PGBUF_UNCONDITIONAL_LATCH);
	  if (prev_overflow_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      return error_code;
	    }
	  error_code =
	    btree_get_next_overflow_vpid (prev_overflow_page, &overflow_vpid);
	  if (error_code != NO_ERROR)
	    {
	      assert_release (false);
	      pgbuf_unfix_and_init (thread_p, prev_overflow_page);
	      return error_code;
	    }
	}
    }

  /* Start processing key objects. */

  if (!bts->is_key_partially_processed)
    {
      /* Start processing key with leaf record objects. */
      error_code =
	btree_record_process_objects
	(thread_p, &bts->btid_int, BTREE_LEAF_NODE, &bts->key_record,
	 bts->offset, &stop, btree_select_visible_object_for_range_scan, bts);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (stop || bts->end_one_iteration || bts->end_scan
	  || bts->is_interrupted)
	{
	  /* Early out. */
	  return NO_ERROR;
	}
      /* Process overflow objects. */
      /* Start processing overflow with first one. */
      VPID_COPY (&overflow_vpid, &bts->leaf_rec_info.ovfl);
      /* Fall through. */
    }
  else
    {
      /* Start processing key with an overflow page. */
      /* Overflow VPID is already set. */
      assert (!VPID_ISNULL (&overflow_vpid));
      /* Assume key will be entirely processed. It will be changed if it
       * interrupted again.
       */
      bts->is_key_partially_processed = false;
    }

  /* Prepare required data. */
  if (BTREE_IS_UNIQUE (bts->btid_int.unique_pk))
    {
      /* Class OID included. */
      oid_size += OR_OID_SIZE;
    }
  /* Initialize last visible overflow. */
  VPID_SET_NULL (&last_visible_overflow);
  /* Process overflow pages. */
  while (!VPID_ISNULL (&overflow_vpid))
    {
      /* Fix next overflow page. */
      overflow_page =
	pgbuf_fix (thread_p, &overflow_vpid, OLD_PAGE, PGBUF_LATCH_READ,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (overflow_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  if (prev_overflow_page != NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, prev_overflow_page);
	    }
	  return error_code;
	}
      /* Previous overflow page (if any) can be unfixed, now that next
       * overflow page was fixed.
       */
      if (prev_overflow_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, prev_overflow_page);
	}

      /* Save current object count. */
      save_oid_count = bts->n_oids_read_last_iteration;

      /* Get record. */
      if (spage_get_record (overflow_page, 1, &ovf_record, PEEK) != S_SUCCESS)
	{
	  assert_release (false);
	  pgbuf_unfix_and_init (thread_p, overflow_page);
	  return ER_FAILED;
	}
      /* Can we save all objects? */
      oid_count =
	btree_record_get_num_oids (thread_p, &bts->btid_int, &ovf_record, 0,
				   BTREE_OVERFLOW_NODE);
      if (!BTS_IS_HARD_CAPACITY_ENOUGH (bts,
					bts->n_oids_read_last_iteration
					+ oid_count))
	{
	  /* We don't know how many visible objects there are in this page,
	   * and we don't want to process the page partially.
	   * Stop here and we will continue from last overflow page that
	   * had visible objects.
	   */
	  /* Index coverage uses a list file and can handle all objects for
	   * this key.
	   */

	  /* Safe guard: we should have at least one page with visible objects
	   *             here. We already processed at least ~7.5k.
	   */
	  assert (bts->n_oids_read_last_iteration > 0);
	  assert (!VPID_ISNULL (&last_visible_overflow));

	  /* Save page to resume. */
	  VPID_COPY (&bts->O_vpid, &last_visible_overflow);
	  /* Mark key as partially processed to know to resume from an
	   * overflow page.
	   */
	  bts->is_key_partially_processed = true;
	  /* End current iteration. */
	  bts->end_one_iteration = true;

	  /* Unfix current overflow page. */
	  pgbuf_unfix_and_init (thread_p, overflow_page);
	  return NO_ERROR;
	}
      /* We can handle all current objects. */

      /* Process this overflow OID's. */
      error_code =
	btree_record_process_objects
	(thread_p, &bts->btid_int, BTREE_OVERFLOW_NODE, &ovf_record, 0, &stop,
	 btree_select_visible_object_for_range_scan, bts);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init (thread_p, overflow_page);
	  return error_code;
	}
      /* Successful processing. */
      if (stop || bts->end_one_iteration || bts->end_scan
	  || bts->is_interrupted)
	{
	  /* Early out. */
	  pgbuf_unfix_and_init (thread_p, overflow_page);
	  return NO_ERROR;
	}
      if (save_oid_count < bts->n_oids_read_last_iteration)
	{
	  /* Page had at least one visible object. */
	  VPID_COPY (&last_visible_overflow, &overflow_vpid);
	}
      /* Process next overflow page. */
      error_code =
	btree_get_next_overflow_vpid (overflow_page, &overflow_vpid);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  pgbuf_unfix_and_init (thread_p, overflow_page);
	  return error_code;
	}
      prev_overflow_page = overflow_page;
      overflow_page = NULL;
    }
  if (prev_overflow_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, prev_overflow_page);
    }
  /* Entire key has been processed. */
  VPID_SET_NULL (&bts->O_vpid);

  /* Key has been consumed. */
  bts->key_status = BTS_KEY_IS_CONSUMED;
  /* Success. */
  return NO_ERROR;
}

/*
 * btree_select_visible_object_for_range_scan () - BTREE_PROCESS_OBJECT_FUNCTION
 *						   Function handles each found
 *						   object based on type of
 *						   index scan.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree info.
 * record (in)	   : Index record containing one key's objects.
 * object_ptr (in) : Pointer in record to current object (not used).
 * oid (in)	   : Current object OID.
 * class_oid (in)  : Current object class OID.
 * mvcc_info (in)  : Current object MVCC info.
 * stop (out)	   : Set to true if processing record should stop after this
 *		     function execution.
 * args (in/out)   : BTREE_SCAN *.
 */
static int
btree_select_visible_object_for_range_scan (THREAD_ENTRY * thread_p,
					    BTID_INT * btid_int,
					    RECDES * record,
					    char *object_ptr, OID * oid,
					    OID * class_oid,
					    BTREE_MVCC_INFO * mvcc_info,
					    bool * stop, void *args)
{
  BTREE_SCAN *bts = NULL;
  int error_code = NO_ERROR;
  MVCC_SNAPSHOT *snapshot = NULL;
  MVCC_REC_HEADER mvcc_header_for_snapshot;

  /* Assert expected arguments. */
  assert (args != NULL);
  assert (btid_int != NULL);
  assert (oid != NULL);
  assert (class_oid != NULL);
  assert (mvcc_info != NULL);
  assert (stop != NULL);

  /* Index loose scan must also be covered index. */
  assert (!BTS_IS_INDEX_ILS (bts) || BTS_IS_INDEX_COVERED (bts));

  /* args is the b-tree scan structure. */
  bts = (BTREE_SCAN *) args;

  /* This function first checks object eligibility. If eligible it will be
   * then saved.
   * Eligibility must pass several filters:
   * 1. Snapshot: object must be visible.
   * 2. Match class: for unique indexes of hierarchical classes, query may be
   *    executed on one class only. The class must be matched.
   * 3. Key limit filters: First lower key limit and all after upper key limit
   *    objects are ignored.
   *
   * Then object will be saved/processed differently depending on type of
   * scan:
   * 1. MRO - object is checked against the current Top N objects.
   * 2. Covering index: object and key are saved in a list file.
   * 3. ISS/regular scan: OID is saved in a buffer.
   *
   * NOTE: Unique indexes will stop after the first visible objects.
   */

  /* Verify snapshot. */
  btree_mvcc_info_to_heap_mvcc_header (mvcc_info, &mvcc_header_for_snapshot);
  snapshot =
    bts->index_scan_idp != NULL ?
    bts->index_scan_idp->scan_cache.mvcc_snapshot : NULL;

  if (bts->index_scan_idp->check_not_vacuumed)
    {
      /* Check if object should have been vacuumed. */
      DISK_ISVALID disk_result = DISK_VALID;

      disk_result =
	vacuum_check_not_vacuumed_rec_header (thread_p, oid, class_oid,
					      &mvcc_header_for_snapshot,
					      /* TODO: Actually node type is
					       *       not accurate.
					       */
					      bts->node_type);
      if (disk_result != DISK_VALID)
	{
	  /* Error or object should have been vacuumed. */
	  bts->index_scan_idp->not_vacuumed_res = disk_result;
	  if (disk_result == DISK_ERROR)
	    {
	      /* Error. */
	      ASSERT_ERROR_AND_SET (error_code);
	      return error_code;
	    }
	}
    }

  /* Check snapshot. */
  if (snapshot != NULL && snapshot->snapshot_fnc != NULL
      && !snapshot->snapshot_fnc (thread_p, &mvcc_header_for_snapshot,
				  snapshot))
    {
      /* Snapshot not satisfied. Ignore object. */
      return NO_ERROR;
    }
  /* No snapshot or snapshot was satisfied. */

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* Key can have only one visible object. */
      bts->key_status = BTS_KEY_IS_CONSUMED;
      *stop = true;
      /* Fall through to handle current object. Processing this key will stop
       * afterwards.
       */

      /* Verify class match. */
      if (!OID_ISNULL (&bts->match_class_oid)
	  && !OID_EQ (&bts->match_class_oid, class_oid))
	{
	  /* Class not matched. */
	  return NO_ERROR;
	}
      /* Class was matched. */
    }

  /* Select object. */

  /* Check key limit filters */
  /* Only objects between lower key limit and lower+upper key limit are
   * selected. First lower key limit objects are skipped, and after another
   * upper key limit objects scan is ended.
   */

  /* Lower key limit */
  if (bts->key_limit_lower != NULL && *bts->key_limit_lower > 0)
    {
      /* Do not copy object. Just decrement key_limit_lower until it is 0. */
      assert (!BTS_IS_INDEX_ILS (bts));
      (*bts->key_limit_lower)--;
      return NO_ERROR;
    }
  /* No lower key limit or lower key limit was already reached. */

  /* Upper key limit */
  if (bts->key_limit_upper != NULL
      && bts->n_oids_read_last_iteration >= *bts->key_limit_upper)
    {
      /* Upper limit reached. Stop scan. */
      assert (!BTS_IS_INDEX_ILS (bts));
      bts->end_scan = true;
      *stop = true;
      return NO_ERROR;
    }
  /* No upper key limit or upper key limit was not reached yet. */

  /* Must objects be read or just counted? */
  if (BTS_NEED_COUNT_ONLY (bts))
    {
      /* Just count. */
      assert (!BTS_IS_INDEX_ILS (bts));
      BTS_INCREMENT_READ_OIDS (bts);
      return NO_ERROR;
    }
  /* Read object. */

  /* Possible scans that can reach this code:
   * - Multi-range-optimization.
   * - Covering index.
   * - ISS, if current op is ISS_OP_DO_RANGE_SEARCH.
   * - Regular index range scan.
   */

  if (BTS_IS_INDEX_MRO (bts))
    {
      /* Multiple range optimization */
      /* Add current key to TOP N sorted keys */
      /* Pass current key and next pseudo OID's to handle lock release
       * when a candidate is thrown out of TOP N structure.
       */
      bool mro_continue = true;
      error_code =
	btree_range_opt_check_add_index_key (thread_p, bts,
					     &bts->index_scan_idp->
					     multi_range_opt,
					     oid, &mro_continue);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (!mro_continue)
	{
	  /* Current item didn't fit in the TOP N keys, and the
	   * following items in current btree_range_search iteration
	   * will not be better. Go to end of scan.
	   */
	  bts->end_scan = true;
	  *stop = true;
	}
      else
	{
	  /* Increment OID counter. */
	  BTS_INCREMENT_READ_OIDS (bts);
	}
      /* Finished handling object for MRO. */
      return NO_ERROR;
    }

  /* Possible scans that can reach this code:
   * - Covering index.
   * - ISS, if current op is ISS_OP_DO_RANGE_SEARCH.
   * - Regular index range scan.
   */

  if (BTS_IS_INDEX_COVERED (bts))
    {
      if (BTS_IS_INDEX_ILS (bts))
	{
	  /* Index loose scan. */
	  *stop = true;
	  bts->key_status = BTS_KEY_IS_CONSUMED;
	  /* Interrupt range scan. It must be restarted with a new range. */
	  bts->is_interrupted = true;

	  /* Since range scan must be moved on a totally different range,
	   * it must restart by looking for the first eligible key of the
	   * new range. Trick it to think this a new call of btree_range_scan.
	   */
	  BTS_RESET_SCAN (bts);

	  /* Adjust range of scan. */
	  error_code = btree_ils_adjust_range (thread_p, bts);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  /* Fall through to dump key. */
	}

      /* Covering index. */
      error_code =
	btree_dump_curr_key (thread_p, bts, bts->key_filter, oid,
			     bts->index_scan_idp);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      BTS_INCREMENT_READ_OIDS (bts);
      return NO_ERROR;
    }

  /* Possible scans that can reach this code:
   * - ISS, if current op is ISS_OP_DO_RANGE_SEARCH.
   * - Regular index range scan.
   * They are both treated in the same way (copied to OID buffer).
   */
  BTS_SAVE_OID_IN_BUFFER (bts, oid);
  assert (HEAP_ISVALID_OID (oid) != DISK_INVALID);
  assert (HEAP_ISVALID_OID (bts->index_scan_idp->oid_list->oidp)
	  != DISK_INVALID);
  return NO_ERROR;
}

/*
 * btree_range_scan_find_fk_any_object () - BTREE_RANGE_SCAN_PROCESS_KEY_FUNC
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * bts (in)	   : B-tree scan info.
 */
static int
btree_range_scan_find_fk_any_object (THREAD_ENTRY * thread_p,
				     BTREE_SCAN * bts)
{
  int error_code = NO_ERROR;	/* Error code. */

  /* Assert expected arguments. */
  assert (bts != NULL);
  assert (bts->bts_other != NULL);

  /* Search and lock one object in key. */
  error_code =
    btree_key_process_objects (thread_p, &bts->btid_int, &bts->key_record,
			       bts->offset, &bts->leaf_rec_info,
			       btree_fk_object_does_exist, bts);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (bts->end_scan)
    {
      return NO_ERROR;
    }
  if (!bts->is_interrupted)
    {
      /* Key was fully consumed. We are here because no object was found.
       * Since this key was the only one of interest, scan can be stopped.
       */
      assert (OID_ISNULL
	      (&((BTREE_FIND_FK_OBJECT *) bts->bts_other)->found_oid));
      bts->end_scan = true;
    }
  return NO_ERROR;
}

/*
 * btree_fk_object_does_exist () - Check whether current object exists (it
 *				   must not be deleted and successfully
 *				   locked).
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree info.
 * record (in)	   : Index record containing one key's objects.
 * object_ptr (in) : Pointer in record to current object (not used).
 * oid (in)	   : Current object OID.
 * class_oid (in)  : Current object class OID.
 * mvcc_info (in)  : Current object MVCC info.
 * stop (out)	   : Set to true if processing record should stop after this
 *		     function execution.
 * args (in/out)   : BTREE_SCAN *
 */
static int
btree_fk_object_does_exist (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			    RECDES * record, char *object_ptr, OID * oid,
			    OID * class_oid, BTREE_MVCC_INFO * mvcc_info,
			    bool * stop, void *args)
{
  BTREE_SCAN *bts = (BTREE_SCAN *) args;
  BTREE_FIND_FK_OBJECT *find_fk_obj = NULL;
  MVCC_SATISFIES_DELETE_RESULT satisfy_delete;
  MVCC_REC_HEADER mvcc_header_for_check_delete;
  int lock_result;
  int error_code = NO_ERROR;

  /* Assert expected arguments. */
  assert (bts != NULL);
  assert (bts->bts_other != NULL);
  assert (oid != NULL);
  assert (class_oid != NULL && !OID_ISNULL (class_oid));
  assert (mvcc_info != NULL);

  find_fk_obj = (BTREE_FIND_FK_OBJECT *) bts->bts_other;

  /* Is object not dirty and lockable? */

  btree_mvcc_info_to_heap_mvcc_header (mvcc_info,
				       &mvcc_header_for_check_delete);
  satisfy_delete =
    mvcc_satisfies_delete (thread_p, &mvcc_header_for_check_delete);
  switch (satisfy_delete)
    {
    case DELETE_RECORD_DELETED:
    case DELETE_RECORD_SELF_DELETED:
      /* Object is already deleted. It doesn't exist. */
      return NO_ERROR;

    case DELETE_RECORD_INVISIBLE:
#if defined (SERVER_MODE)
      /* Recently inserted. This can be ignored, since it is not inserted yet.
       * To successfully insert, the inserter should also obtain lock on
       * primary key object (which is already held by current transaction).
       * Current transaction can consider that this object doesn't exist yet.
       */
      return NO_ERROR;
#else	/* !SERVER_MODE */		   /* SA_MODE */
      /* Impossible: no other active transactions. */
      assert_release (false);
      return ER_FAILED;
#endif /* SA_MODE */

    case DELETE_RECORD_IN_PROGRESS:
#if defined (SERVER_MODE)
      /* Object is being deleted by an active transaction. We have to wait for
       * that transaction to commit. Fall through to suspend.
       */
      break;
#else	/* !SERVER_MODE */		   /* SA_MODE */
      /* Impossible: no other active transactions. */
      assert_release (false);
      return ER_FAILED;
#endif /* SA_MODE */

    case DELETE_RECORD_CAN_DELETE:
#if defined (SERVER_MODE)
      /* Try conditional lock */
      /* Make sure there is no other object already locked. */
      if (!OID_ISNULL (&find_fk_obj->locked_object))
	{
	  if (OID_EQ (&find_fk_obj->locked_object, oid))
	    {
	      /* Object already locked. */
	      assert (lock_has_lock_on_object
		      (oid, class_oid, thread_get_current_tran_index (),
		       find_fk_obj->lock_mode) > 0);
	      lock_result = LK_GRANTED;
	    }
	  else
	    {
	      /* Current object must be unlocked. */
	      lock_unlock_object_donot_move_to_non2pl (thread_p,
						       &find_fk_obj->
						       locked_object,
						       class_oid,
						       find_fk_obj->
						       lock_mode);
	      OID_SET_NULL (&find_fk_obj->locked_object);
	    }
	}
      if (OID_ISNULL (&find_fk_obj->locked_object))
	{
	  /* Get conditional lock. */
	  lock_result =
	    lock_object (thread_p, oid, class_oid, find_fk_obj->lock_mode,
			 LK_COND_LOCK);
	}
#else	/* !SERVER_MODE */		   /* SA_MODE */
      lock_result = LK_GRANTED;
#endif /* SA_MODE */
      if (lock_result == LK_GRANTED)
	{
	  /* Object was successfully locked. Stop now. */
	  COPY_OID (&find_fk_obj->found_oid, oid);
	  bts->end_scan = true;
	  *stop = true;
#if defined (SERVER_MODE)
	  COPY_OID (&find_fk_obj->locked_object, oid);
#endif /* SERVER_MODE */
	  return NO_ERROR;
	}
      /* Conditional lock failed. Fall through to suspend. */
      break;
    }

#if defined (SA_MODE)
  /* Impossible to reach. */
  assert_release (false);
  return ER_FAILED;
#else	/* !SA_MODE */	       /* SERVER_MODE */
  /* Object may exist but could not be locked conditionally. */
  /* Unconditional lock on object. */
  /* Must release fixed pages first. */
  bts->is_interrupted = true;
  pgbuf_unfix_and_init (thread_p, bts->C_page);

  /* Make sure another object was not already fixed. */
  if (!OID_ISNULL (&find_fk_obj->locked_object))
    {
      lock_unlock_object_donot_move_to_non2pl (thread_p,
					       &find_fk_obj->locked_object,
					       class_oid,
					       find_fk_obj->lock_mode);
      OID_SET_NULL (&find_fk_obj->locked_object);
    }
  lock_result =
    lock_object (thread_p, oid, class_oid, find_fk_obj->lock_mode,
		 LK_UNCOND_LOCK);
  if (lock_result != LK_GRANTED)
    {
      /* Lock failed. */
      error_code = er_errid ();
      if (error_code == NO_ERROR)
	{
	  /* Set an error code. */
	  error_code = ER_CANNOT_GET_LOCK;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	  return error_code;
	}
    }
  /* Lock successful. */
  /* Key will be checked again just to make sure object was not deleted or
   * moved while current thread was suspended.
   */
  *stop = true;
  return NO_ERROR;
#endif /* SERVER_MODE */
}

/*
 * btree_undo_delete_physical () - Undo of physical delete from b-tree. Must
 *				   insert back object and other required
 *				   information (class OID, MVCC info).
 *
 * return	  : Error code.
 * thread_p (in)  : Thread entry.
 * btid (in)	  : B-tree identifier.
 * key (in)	  : Key value.
 * class_oid (in) : Class OID.
 * oid (in)	  : Instance OID.
 * mvcc_info (in) : B-tree MVCC information.
 */
static int
btree_undo_delete_physical (THREAD_ENTRY * thread_p, BTID * btid,
			    DB_VALUE * key, OID * class_oid,
			    OID * oid, BTREE_MVCC_INFO * mvcc_info)
{
  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      if (class_oid == NULL)
	{
	  class_oid = (OID *) & oid_Null_oid;
	}
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Start undo physical delete %d|%d|%d, "
		     "class_oid %d|%d|%d, insert MVCCID=%llu delete "
		     "MVCCID=%llu into index (%d, %d|%d).\n",
		     oid->volid, oid->pageid, oid->slotid,
		     class_oid->volid, class_oid->pageid, class_oid->slotid,
		     mvcc_info->insert_mvccid, mvcc_info->delete_mvccid,
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid);
    }
  return btree_insert_internal (thread_p, btid, key, class_oid, oid,
				SINGLE_ROW_INSERT, NULL, NULL, mvcc_info,
				BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE);
}

/*
 * btree_insert () - Insert new object into b-tree.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * btid (in)		  : B-tree identifier.
 * key (in)		  : Key value.
 * cls_oid (in)		  : Class OID.
 * oid (in)		  : Instance OID.
 * op_type (in)		  : Single-multi row operations.
 * unique_stat_info (in)  : Statistics collector used multi row operations.
 * unique (out)		  : Outputs if b-tree is unique when not NULL.
 * p_mvcc_rec_header (in) : Heap MVCC record header.
 */
int
btree_insert (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * key,
	      OID * cls_oid, OID * oid, int op_type,
	      BTREE_UNIQUE_STATS * unique_stat_info, int *unique,
	      MVCC_REC_HEADER * p_mvcc_rec_header)
{
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;
  int error_code = NO_ERROR;

  /* Assert expected arguments. */
  assert (oid != NULL);

  if (p_mvcc_rec_header != NULL)
    {
#if !defined (SERVER_MODE)
      assert_release (false);
#endif /* SERVER_MODE */
      btree_mvcc_info_from_heap_mvcc_header (p_mvcc_rec_header, &mvcc_info);
    }

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      if (cls_oid == NULL)
	{
	  cls_oid = (OID *) & oid_Null_oid;
	}
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Start insert object %d|%d|%d, "
		     "class_oid %d|%d|%d, insert MVCCID=%llu into "
		     "index (%d, %d|%d), op_type=%d.\n",
		     oid->volid, oid->pageid, oid->slotid,
		     cls_oid->volid, cls_oid->pageid, cls_oid->slotid,
		     p_mvcc_rec_header != NULL ?
		     MVCC_GET_INSID (p_mvcc_rec_header) : MVCCID_ALL_VISIBLE,
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid,
		     op_type);
    }

  /* Safe guard. */
  assert (!BTREE_MVCC_INFO_IS_DELID_VALID (&mvcc_info));

  return btree_insert_internal (thread_p, btid, key, cls_oid, oid, op_type,
				unique_stat_info, unique, &mvcc_info,
				BTREE_OP_INSERT_NEW_OBJECT);
}

/*
 * btree_mvcc_delete () - MVCC logical delete. Adds delete MVCCID to an
 *			  existing object.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * btid (in)		  : B-tree identifier.
 * key (in)		  : Key value.
 * cls_oid (in)		  : Class OID.
 * oid (in)		  : Instance OID.
 * op_type (in)		  : Single-multi row operations.
 * unique_stat_info (in)  : Statistics collector used multi row operations.
 * unique (out)		  : Outputs if b-tree is unique when not NULL.
 * p_mvcc_rec_header (in) : Heap MVCC record header.
 */
int
btree_mvcc_delete (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * key,
		   OID * class_oid, OID * oid, int op_type,
		   BTREE_UNIQUE_STATS * unique_stat_info, int *unique,
		   MVCC_REC_HEADER * p_mvcc_rec_header)
{
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;

  /* Assert expected arguments. */
  assert (oid != NULL);
  assert (p_mvcc_rec_header != NULL);

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      if (class_oid == NULL)
	{
	  class_oid = (OID *) & oid_Null_oid;
	}
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Start MVCC delete object %d|%d|%d, "
		     "class_oid %d|%d|%d, delete MVCCID=%llu into "
		     "index (%d, %d|%d), op_type=%d.\n",
		     oid->volid, oid->pageid, oid->slotid,
		     class_oid->volid, class_oid->pageid, class_oid->slotid,
		     MVCC_GET_DELID (p_mvcc_rec_header),
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid,
		     op_type);
    }

  btree_mvcc_info_from_heap_mvcc_header (p_mvcc_rec_header, &mvcc_info);

  /* Safe guard. */
  assert (BTREE_MVCC_INFO_IS_DELID_VALID (&mvcc_info));

  return btree_insert_internal (thread_p, btid, key, class_oid, oid, op_type,
				unique_stat_info, unique, &mvcc_info,
				BTREE_OP_INSERT_MVCC_DELID);
}

/*
 * btree_insert_internal () - Generic index function that inserts new data in
 *			      a b-tree key.
 *
 * return		     : Error code.
 * thread_p (in)	     : Thread entry.
 * btid (in)		     : B-tree identifier.
 * key (in)		     : Key value.
 * class_oid (in)	     : Class OID.
 * oid (in)		     : Instance OID.
 * op_type (in)		     : Single/Multi row operation type.
 * unique_stat_info (in/out) : Unique stats info, used to track changes during
 *			       multi-update operation.
 * unique (out)		     : Outputs true if index was unique, false
 *			       otherwise.
 * mvcc_info (in)	     : B-tree MVCC information.
 * purpose (in)		     : B-tree insert purpose
 *			       BTREE_OP_INSERT_NEW_OBJECT
 *			       BTREE_OP_INSERT_MVCC_DELID
 *			       BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE.
 */
static int
btree_insert_internal (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * key,
		       OID * class_oid, OID * oid, int op_type,
		       BTREE_UNIQUE_STATS * unique_stat_info, int *unique,
		       BTREE_MVCC_INFO * mvcc_info, BTREE_OP_PURPOSE purpose)
{
  int error_code = NO_ERROR;	/* Error code. */
  BTID_INT btid_int;		/* B-tree info. */
  /* Search key helper which will point to where data should inserted. */
  BTREE_SEARCH_KEY_HELPER search_key = BTREE_SEARCH_KEY_HELPER_INITIALIZER;
  /* Insert helper. */
  BTREE_INSERT_HELPER insert_helper = BTREE_INSERT_HELPER_INITIALIZER;
  /* Processing key function: can insert an object or just a delete MVCCID. */
  BTREE_PROCESS_KEY_FUNCTION *key_insert_func = NULL;

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (oid != NULL);
  /* Assert class OID is valid or not required. */
  assert ((purpose != BTREE_OP_INSERT_NEW_OBJECT
	   && purpose != BTREE_OP_INSERT_MVCC_DELID)
	  || (class_oid != NULL && !OID_ISNULL (class_oid)));

  /* Save OID, class OID and MVCC info in insert helper. */
  COPY_OID (BTREE_INSERT_OID (&insert_helper), oid);
  if (class_oid != NULL)
    {
      COPY_OID (BTREE_INSERT_CLASS_OID (&insert_helper), class_oid);
    }
  else
    {
      OID_SET_NULL (BTREE_INSERT_CLASS_OID (&insert_helper));
    }
  *BTREE_INSERT_MVCC_INFO (&insert_helper) = *mvcc_info;

  /* Is key NULL? */
  insert_helper.is_null =
    key == NULL || DB_IS_NULL (key) || btree_multicol_key_is_null (key);

  /* Set key function. */
  switch (purpose)
    {
    case BTREE_OP_INSERT_NEW_OBJECT:
    case BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE:
      key_insert_func = btree_key_insert_new_object;
      break;
    case BTREE_OP_INSERT_MVCC_DELID:
      key_insert_func = btree_key_insert_delete_mvccid;
      break;
    default:
      assert (false);
      return ER_FAILED;
    }
  insert_helper.purpose = purpose;

  /* Set operation type. */
  insert_helper.op_type = op_type;
  /* Set unique stats info. */
  insert_helper.unique_stats_info = unique_stat_info;

  /* Do we log the operations? Use for debug only. */
  insert_helper.log_operations = prm_get_bool_value (PRM_ID_LOG_BTREE_OPS);

  /* Is this an unique index and is operation type MULTI_ROW_UPDATE?
   * Unique constraint violation will be treated slightly different.
   */
  insert_helper.is_unique_multi_update =
    unique_stat_info != NULL && op_type == MULTI_ROW_UPDATE;
  /* Is HA enabled? The above exception will no longer apply. */
  insert_helper.is_ha_enabled =
    prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF;

  /* Add more insert_helper initialization here. */

  /* Search for key leaf page and insert data. */
  error_code =
    btree_search_key_and_apply_functions (thread_p, btid, &btid_int, key,
					  btree_fix_root_for_insert,
					  &insert_helper,
					  btree_split_node_and_advance,
					  &insert_helper,
					  key_insert_func,
					  &insert_helper, &search_key, NULL);

  /* Free allocated resources. */
  if (insert_helper.printed_key != NULL)
    {
      free_and_init (insert_helper.printed_key);
    }

#if defined (SERVER_MODE)
  /* Saved locked OID keeps the object that used to be first in unique key
   * before new object was inserted. In either case, if error occurred or if
   * inserting new object was successful, keeping this lock is no longer
   * necessary.
   * If insert was successful, key is protected by the newly inserted object,
   * which is already locked.
   */
  if (!OID_ISNULL (&insert_helper.saved_locked_oid))
    {
      lock_unlock_object_donot_move_to_non2pl (thread_p,
					       &insert_helper.
					       saved_locked_oid,
					       &insert_helper.
					       saved_locked_class_oid,
					       X_LOCK);
    }
#endif /* SERVER_MODE */

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  mnt_bt_inserts (thread_p);

  if (unique != NULL)
    {
      /* Output unique. */
      *unique =
	(BTREE_IS_UNIQUE (btid_int.unique_pk)
	 && error_code == NO_ERROR) ? 1 : 0;
    }

  if (BTREE_IS_UNIQUE (btid_int.unique_pk)
      && insert_helper.is_unique_multi_update && !insert_helper.is_ha_enabled
      && !insert_helper.is_unique_key_added_or_deleted)
    {
      assert (op_type == MULTI_ROW_UPDATE);
      assert (unique_stat_info != NULL);

      /* Key was not inserted/deleted. Correct unique_stat_info (which assumed
       * that key will be inserted/deleted).
       */
      if (purpose == BTREE_OP_INSERT_NEW_OBJECT)
	{
	  unique_stat_info->num_keys--;
	}
      else if (purpose == BTREE_OP_INSERT_MVCC_DELID)
	{
	  unique_stat_info->num_keys++;
	}
      else
	{
	  /* Unexpected. */
	  assert_release (false);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * btree_fix_root_for_insert () - BTREE_ROOT_WITH_KEY_FUNCTION - fix root
 *				  before inserting data in b-tree.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid (in)	       : B-tree identifier.
 * btid_int (out)      : BTID_INT (B-tree data).
 * key (in)	       : Key value.
 * root_page (out)     : Output b-tree root page.
 * is_leaf (out)       : Output true if root is leaf page.
 * search_key (out)    : Output key search result (if root is also leaf).
 * stop (out)	       : Output true if advancing in b-tree should stop.
 * restart (out)       : Output true if advancing in b-tree should be
 *			 restarted.
 * other_args (in/out) : BTREE_INSERT_HELPER *.
 *
 * NOTE: Besides fixing root page, this function can also modify the root
 *	 header. This must be done only once.
 */
static int
btree_fix_root_for_insert (THREAD_ENTRY * thread_p, BTID * btid,
			   BTID_INT * btid_int, DB_VALUE * key,
			   PAGE_PTR * root_page, bool * is_leaf,
			   BTREE_SEARCH_KEY_HELPER * search_key, bool * stop,
			   bool * restart, void *other_args)
{
  BTREE_INSERT_HELPER *insert_helper = (BTREE_INSERT_HELPER *) other_args;
  BTREE_ROOT_HEADER *root_header = NULL;
  int error_code;
  int key_len;
  int increment;
  int increment_nulls;
  int increment_keys;
  int increment_oids;
  int lock_result;

  /* Assert expected arguments. */
  assert (insert_helper != NULL);
  assert (root_page != NULL && *root_page == NULL);
  assert (btid != NULL);
  assert (btid_int != NULL);
  assert (search_key != NULL);

  /* Possible insert data operations:
   * 1. Insert a new object along with other necessary informations (class OID
   *    and/or insert MVCCID.
   * 2. Undo of physical delete. If an object is physically removed from
   *    b-tree and operation must be undone, the object with all its
   *    additional information existing before delete must be inserted.
   * 3. Logical delete, which inserts a delete MVCCID.
   */
  assert (insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT
	  || insert_helper->purpose == BTREE_OP_INSERT_MVCC_DELID
	  || insert_helper->purpose == BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE);

  if (insert_helper->is_first_try)
    {
      /* Fix root and get header/b-tree info to do some additional operations
       * on b-tree.
       */
      *root_page =
	btree_fix_root_with_info (thread_p, btid,
				  insert_helper->nonleaf_latch_mode,
				  NULL, &root_header, btid_int);
      if (*root_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
    }
  else
    {
      /* Just fix root page. */
      *root_page =
	btree_fix_root_with_info (thread_p, btid,
				  insert_helper->nonleaf_latch_mode,
				  NULL, NULL, NULL);
      if (*root_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
      /* Root fixed. */
      return NO_ERROR;
    }
  assert (*root_page != NULL);
  assert (root_header != NULL);
  assert (BTREE_IS_SAME_DB_TYPE (DB_VALUE_DOMAIN_TYPE (key),
				 btid_int->key_type->type->id));

  insert_helper->is_root = true;

  /* Do additional operations. */
  /* Next time this function is called, it must be just a restart of b-tree
   * traversal and the additional operations must not be executed again.
   * Mark insert_helper to know to skip this part.
   */
  insert_helper->is_first_try = false;

  /* Set complete domain for MIDXKEYS. */
  if (key != NULL && DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      key->data.midxkey.domain = btid_int->key_type;
    }
  if (insert_helper->log_operations && insert_helper->printed_key == NULL)
    {
      /* This is postponed here to make sure midxkey domain was initialized.
       */
      insert_helper->printed_key = pr_valstring (key);
    }

  if (insert_helper->purpose == BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE)
    {
      /* Stop here. */
      /* Code after this:
       * 1. Update unique statistics. In this case, they are updated by undone
       *    log records.
       * 2. Create overflow key file. Undoing a physical delete cannot create
       *    overflow file (the file should already exist if that's the case).
       */
      /* Safe guard: there should be no physical delete of NULL keys. */
      assert (!insert_helper->is_null);

      if (BTREE_IS_UNIQUE (btid_int->unique_pk)
	  && OID_ISNULL (BTREE_INSERT_CLASS_OID (insert_helper)))
	{
	  /* Top class OID is not packed for recovery. Save it here. */
	  COPY_OID (BTREE_INSERT_CLASS_OID (insert_helper),
		    &btid_int->topclass_oid);
	}
      return NO_ERROR;
    }

  /* Update nulls, oids, keys statistics for unique indexes. */
  /* If transaction is not active (being rolled back), statistics don't need
   * manual updating. They will be reverted automatically by undo logs.
   *
   * NOTE that users to see the header statistics may have the transient
   * values.
   */
  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* Is increment positive/negative? (is object inserted/deleted?) */
      if (insert_helper->purpose == BTREE_OP_INSERT_MVCC_DELID)
	{
	  /* Object is being logically deleted. */
	  increment = -1;
	}
      else
	{
	  /* Object is being inserted. */
	  increment = 1;
	}
      /* Set increment of each counter: nulls, oids, keys. */
      if (insert_helper->is_null)
	{
	  increment_keys = 0;
	  increment_oids = increment;
	  increment_nulls = increment;
	}
      else
	{
	  increment_keys = increment;
	  increment_oids = increment;
	  increment_nulls = 0;
	}
      /* Update statistics. */
      /* Based on type of operation - single or multi, update the
       * unique_stats_info structure or update the transaction collected
       * statistics. They will be reflected into global statistics later.
       */
      if (BTREE_IS_MULTI_ROW_OP (insert_helper->op_type))
	{
	  /* Update unique_stats_info. */
	  if (insert_helper->unique_stats_info == NULL)
	    {
	      assert_release (false);
	      error_code = ER_FAILED;
	      goto error;
	    }
	  insert_helper->unique_stats_info->num_keys += increment_keys;
	  insert_helper->unique_stats_info->num_oids += increment_oids;
	  insert_helper->unique_stats_info->num_nulls += increment_nulls;
	}
      else
	{
	  /* Update transactions collected statistics. */
	  error_code =
	    logtb_tran_update_unique_stats (thread_p, btid, increment_keys,
					    increment_oids, increment_nulls,
					    true);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	}
    }

  if (insert_helper->is_null)
    {
      /* Stop here. Object is not inserted in b-tree. */
      *stop = true;
      pgbuf_unfix_and_init (thread_p, *root_page);

      if (insert_helper->log_operations)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_INSERT: A NULL object was %s "
			 "index (%d, %d|%d).\n",
			 insert_helper->purpose
			 == BTREE_OP_INSERT_MVCC_DELID ?
			 "deleted from" : "inserted into",
			 btid_int->sys_btid->root_pageid,
			 btid_int->sys_btid->vfid.volid,
			 btid_int->sys_btid->vfid.fileid);
	}
      return NO_ERROR;
    }

  if (insert_helper->purpose == BTREE_OP_INSERT_MVCC_DELID)
    {
      /* This is a deleted object. From here on, the code is for inserting
       * keys only.
       */
      return NO_ERROR;
    }

  /* Purpose is BTREE_OP_INSERT_NEW_OBJECT. */
  assert (insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT);

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* We need to lock currently being inserted object to protect it from
       * unique constraint violation. Most time, this lock can be obtained
       * conditionally, since the object is only visible to current
       * transaction. There is however one peculiar case when another
       * transaction has lock on this object. See next steps:
       * 1. Transaction T1 inserts OID1 and locks to protect it from unique
       *    constraint violation.
       * 2. Transaction T1 rollbacks and frees slot of OID1.
       * 3. Transaction T2 finds free slot OID1 and decides to insert new
       *    object.
       * 4. Transaction T2 locks OID1 to protect it from unique constraint
       *    violation. Note that T1 didn't release locks yet and still has
       *    lock on OID1.
       *
       * We do the locking here, in case it fails. Re-fixing root should cause
       * no problems (compared to re-fixing a non-root page which may need to
       * restart tree walk).
       */
      lock_result =
	lock_object (thread_p, BTREE_INSERT_OID (insert_helper),
		     BTREE_INSERT_CLASS_OID (insert_helper), X_LOCK,
		     LK_COND_LOCK);
      if (lock_result != LK_GRANTED)
	{
	  /* Need to unfix root. */
	  pgbuf_unfix_and_init (thread_p, *root_page);
	  lock_result =
	    lock_object (thread_p, BTREE_INSERT_OID (insert_helper),
			 BTREE_INSERT_CLASS_OID (insert_helper), X_LOCK,
			 LK_UNCOND_LOCK);
	  if (lock_result != LK_GRANTED)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	  *root_page =
	    btree_fix_root_with_info (thread_p, btid,
				      insert_helper->nonleaf_latch_mode,
				      NULL, NULL, btid_int);
	  if (*root_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	}
    }

  /* Check if key length is too big and if an overflow key file needs to be
   * created.
   */
  key_len = btree_get_disk_size_of_key (key);
  if (key_len >= BTREE_MAX_KEYLEN_INPAGE && VFID_ISNULL (&btid_int->ovfid))
    {
      /* Promote latch (if required). */
      error_code =
	pgbuf_promote_read_latch (thread_p, root_page,
				  PGBUF_PROMOTE_SHARED_READER);
      if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	{
	  /* Unfix and re-fix root page. */
	  pgbuf_unfix_and_init (thread_p, *root_page);
	  *root_page =
	    btree_fix_root_with_info (thread_p, btid, PGBUF_LATCH_WRITE, NULL,
				      &root_header, btid_int);
	  if (*root_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	  insert_helper->is_crt_node_write_latched = true;
	}
      else if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      else if (*root_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
      /* Check that overflow key file was not created by another. */
      if (VFID_ISNULL (&btid_int->ovfid))
	{
	  /* Create overflow key file. */

	  /* Start a system operation. */
	  if (log_start_system_op (thread_p) == NULL)
	    {
	      assert_release (false);
	      error_code = ER_FAILED;
	      goto error;
	    }

	  /* Create file. */
	  error_code = btree_create_overflow_key_file (thread_p, btid_int);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
	      goto error;
	    }

	  /* Notification. */
	  BTREE_SET_CREATED_OVERFLOW_KEY_NOTIFICATION (thread_p, key,
						       BTREE_INSERT_OID
						       (insert_helper),
						       BTREE_INSERT_CLASS_OID
						       (insert_helper),
						       btid, NULL);

	  /* Change the root header. */
	  log_append_undoredo_data2 (thread_p, RVBT_UPDATE_OVFID,
				     &btid_int->sys_btid->vfid, *root_page,
				     HEADER, sizeof (VFID), sizeof (VFID),
				     &root_header->ovfid, &btid_int->ovfid);
	  VFID_COPY (&root_header->ovfid, &btid_int->ovfid);
	  pgbuf_set_dirty (thread_p, *root_page, DONT_FREE);

	  /* Finish system operation. */
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	  file_new_declare_as_old (thread_p, &btid_int->ovfid);
	}
    }

  insert_helper->key_len_in_page = BTREE_GET_KEY_LEN_IN_PAGE (key_len);

  /* Success. */
  return NO_ERROR;

error:
  assert (error_code != NO_ERROR);
  ASSERT_ERROR ();
  if (*root_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, *root_page);
    }
  return error_code;
}

/*
 * btree_split_node_and_advance () - BTREE_ADVANCE_WITH_KEY_FUNCTION used by
 *				     btree_insert_internal while advancing
 *				     following key. It also has the role
 *				     to make sure b-tree has enough space to
 *				     insert new data.
 *
 * return		 : Error code. 
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree data.
 * key (in)		 : Search key value.
 * crt_page (in)	 : Page of current node.
 * advance_to_page (out) : Fixed page of child node found by following key.
 * is_leaf (out)	 : Output true if current page is leaf node.
 * key_slotid (out)	 : Output slotid of key if found, otherwise
 *			   NULL_SLOTID.
 * stop (out)		 : Output true if advancing in b-tree should be
 *			   stopped.
 * restart (out)	 : Output true if advancing in b-tree should be
 *			   restarted from top.
 * other_args (in/out)	 : BTREE_INSERT_HELPER *.
 */
static int
btree_split_node_and_advance (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			      DB_VALUE * key, PAGE_PTR * crt_page,
			      PAGE_PTR * advance_to_page, bool * is_leaf,
			      BTREE_SEARCH_KEY_HELPER * search_key,
			      bool * stop, bool * restart, void *other_args)
{
  /* Insert helper: used to store insert specific data that can be used during
   * the call off btree_search_key_and_apply_functions.
   */
  BTREE_INSERT_HELPER *insert_helper = (BTREE_INSERT_HELPER *) other_args;
  int max_new_data_size = 0;	/* The maximum possible size of data to be
				 * inserted in current node.
				 */
  int max_key_len = 0;		/* Maximum key length of a new entry in
				 * node.
				 */
  int error_code = NO_ERROR;	/* Error code. */
  int key_count = 0;		/* Node key count. */
  BTREE_NODE_TYPE node_type;	/* Node type. */
  BTREE_NODE_HEADER *node_header = NULL;	/* Node header. */
  /* VPID's of newly allocated pages for split. Both can be used if the root
   * is split, only first is used if non-root node is split.
   */
  VPID new_page_vpid1 = VPID_INITIALIZER, new_page_vpid2 = VPID_INITIALIZER;
  VPID child_vpid = VPID_INITIALIZER;	/* VPID of child (based on the
					 * direction set by key).
					 */
  VPID *crt_vpid = NULL;	/* VPID pointer for current node. */
  VPID advance_vpid = VPID_INITIALIZER;	/* VPID to advance to (hinted
					 * by split functions).
					 */
  PAGE_PTR child_page = NULL;	/* Page pointer of child node. */
  PAGE_PTR new_page1 = NULL, new_page2 = NULL;	/* Page pointers to newly
						 * allocated pages. Both can
						 * be used if root is split,
						 * only first is used if
						 * non-root node is split.
						 */
  PAGEID_STRUCT pageid_struct;	/* Recovery structure used by split. */
  PGSLOTID child_slotid;	/* Slot ID that points to child node. */
  bool is_new_key_possible = false;	/* Set to true if insert operation can
					 * add new keys to b-tree (and not
					 * just data in existing keys).
					 */
  bool need_split = false;	/* Set to true if split is required. */
  bool need_update_max_key_len = false;	/* Set to true if the node max key
					 * length must be updated to the
					 * length of new key.
					 */
  bool is_system_op_started = false;	/* Set to true when a system operations
					 * is started. Set to false when it is
					 * ended. Used to properly end system
					 * operation in case of errors.
					 */
  bool is_child_leaf = false;	/* Set to true when current node fathers
				 * a leaf node. It is treated
				 * differently and it must be determined
				 * before fixing the child node page.
				 */

#if !defined (NDEBUG)
  int parent_max_key_len = 0;	/* Used by debug to check the rule that
				 * parent max key length is always
				 * bigger or equal to child max key
				 * length.
				 */
  int parent_node_level = 0;	/* Used by debug to check that level of
				 * parent node is always the incremented
				 * value of level of child node.
				 */
#endif /* !NDEBUG */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (crt_page != NULL && *crt_page != NULL);
  assert (advance_to_page != NULL && *advance_to_page == NULL);
  assert (is_leaf != NULL);
  assert (search_key != NULL);
  assert (stop != NULL);
  assert (restart != NULL);
  assert (insert_helper != NULL);

  /* Get informations on current node. */
  /* Node header. */
  node_header = btree_get_node_header (*crt_page);
  if (node_header == NULL)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto error;
    }
  /* Leaf/Non-leaf node type. */
  node_type =
    (node_header->node_level == 1) ? BTREE_LEAF_NODE : BTREE_NON_LEAF_NODE;
  /* Key count. */
  key_count = btree_node_number_of_keys (*crt_page);
  /* Safe guard. */
  assert (key_count >= 0);
  assert (key_count > 0 || node_type == BTREE_LEAF_NODE);

  /* Is new key possible? True if inserting new object or if undoing the
   * removal of some key/object.
   */
  is_new_key_possible =
    insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT
    || insert_helper->purpose == BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE;

  /* Split algorithm:
   * There are two types of splits: root split and normal split.
   * 1. Root split:
   *    If there is not enough space for new data in root, split it into
   *    three nodes: two nodes containing all previous entries and a new root
   *    node to point to these nodes. This will increase the b-tree level.
   *    After split, function will continue with one of the two newly created
   *    nodes.
   * 2. Normal split:
   *    If child doesn't have enough space to handle new insert data, split
   *    it into two nodes and update current node entries. Current node will
   *    gain one additional entry, so the split algorithm should make sure
   *    it always has enough space before checking its children. One of the
   *    two children resulted from the split will be chosen to advance to.
   */
  /* Part of split algorithm is to keep the maximum key length for each node.
   * The value kept by a node is actually the maximum length of all keys found
   * in all leaf pages of the sub-tree fathered by current node. One resulting
   * rule is that the maximum key length value for one node is always bigger
   * than or equal to the maximum key length value for any of its children.
   * Maximum key length is used to estimate the size of future entries (in a
   * defensive way).
   */
  /* NOTE 1:
   * To optimize b-tree access, the algorithm assumes that no change is
   * required and READ latch on nodes is normally used. However if max key
   * length must be update or if split is needed, latches must then be
   * promoted to write/exclusive. Promotion may sometimes fail (e.g. when
   * there is already another promoter in waiting list). This case is
   * considered to be an exceptional and rare case, therefore it is allowed to
   * restart b-tree traversal. However, to avoid repeating traversals
   * indefinetly, the second traversal is done using exclusive latches (which
   * should guarantee success).
   * NOTE 2:
   * Leaf nodes are always latched using exclusive latches (because they are
   * changed almost every time and using promotion can actually lead to poor
   * performance).
   * NOTE 3:
   * Due to always using exclusive latches on leaf node, promotion for their
   * parents are more restrictive: promote succeeds only if there is no other
   * reader on that node (that could be blocked on same leaf node).
   * Consider this case:
   * 1. Thread 1 holds shared latch on parent and exclusive latch on leaf.
   * 2. Thread 2 also holds shared latch on parent but is blocked on exclusive
   *    latch on leaf.
   * 3. Thread 1 decides that split is required and wants to promote parent
   *    latch to exclusive. If promotion would be allowed (because there
   *    are no other promoters waiting), a dead-latch between threads 1 and 2
   *    would occur (thread 1 waiting on promotion, thread 2 waiting on leaf
   *    node).
   * Therefore, seeing that there is at least one more reader, thread 1 is
   * very defensive and decides to abort promotion (and restart traversal).
   */

  /* Root case. */
  if (insert_helper->is_root)
    {
      /* Check if root needs to split or update max key length. */

      /* Updating max key length is possible if new key can be added and if
       * the length of the new key exceeds the current max key length.
       */
      need_update_max_key_len =
	is_new_key_possible
	&& insert_helper->key_len_in_page > node_header->max_key_len;

      /* Get maximum key length for a possible new entry in node. */
      max_key_len =
	need_update_max_key_len ? insert_helper->
	key_len_in_page : node_header->max_key_len;

      /* Compute the maximum possible size of data being inserted in node. */
      max_new_data_size = 0;
      if (node_type == BTREE_NON_LEAF_NODE || is_new_key_possible)
	{
	  /* A new entry is possible for all non-leaf nodes because their
	   * children can be split and for leaf nodes if new key can be
	   * inserted.
	   */
	  max_new_data_size +=
	    BTREE_NEW_ENTRY_MAX_SIZE (max_key_len, node_type);
	}
      else
	{
	  /* Only adding DELID is required.
	   */
	  max_new_data_size += OR_MVCCID_SIZE;
	}

      /* Split is needed if there is a risk that inserted data doesn't fit the
       * root node.
       */
      need_split =
	(max_new_data_size
	 > spage_max_space_for_new_record (thread_p, *crt_page));

      /* If root node should suffer changes, its latch must be promoted to
       * exclusive.
       */
      if (insert_helper->nonleaf_latch_mode == PGBUF_LATCH_READ
	  /* root has read. */
	  && (need_split || need_update_max_key_len
	      || node_type == BTREE_LEAF_NODE)
	  /* and requires write. */ )
	{
	  /* Promote latch. */
	  error_code =
	    pgbuf_promote_read_latch (thread_p, crt_page,
				      PGBUF_PROMOTE_SHARED_READER);
	  if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	    {
	      /* Retry fix with write latch. */
	      pgbuf_unfix_and_init (thread_p, *crt_page);
	      insert_helper->nonleaf_latch_mode = PGBUF_LATCH_WRITE;
	      *restart = true;
	      return NO_ERROR;
	    }
	  else if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	  else if (*crt_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	}

      if (need_update_max_key_len)
	{
	  /* Update max key length. */
	  node_header->max_key_len = insert_helper->key_len_in_page;
	  error_code =
	    btree_change_root_header_delta (thread_p,
					    &btid_int->sys_btid->vfid,
					    *crt_page, 0, 0, 0);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	  pgbuf_set_dirty (thread_p, *crt_page, DONT_FREE);

	  if (insert_helper->log_operations)
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "BTREE_INSERT: Update root %d|%d max_key_length "
			     "to %d in index (%d, %d|%d).\n",
			     pgbuf_get_volume_id (*crt_page),
			     pgbuf_get_page_id (*crt_page),
			     node_header->max_key_len,
			     btid_int->sys_btid->root_pageid,
			     btid_int->sys_btid->vfid.volid,
			     btid_int->sys_btid->vfid.fileid);
	    }

	  /* If this node required to update its max key length, then also
	   * the children we meet will require to update their max key length.
	   * (rule being that parent->max_key_len >= child->max_key_len).
	   */
	  insert_helper->need_update_max_key_len = true;
	  /* Set insert_helper->is_crt_node_write_latched to avoid trying
	   * promotion on current node.
	   */
	  insert_helper->is_crt_node_write_latched = true;
	}

      if (need_split)
	{
	  /* Split root node. */
	  assert (key_count >= 3);

	  /* Start system operation. */
	  if (log_start_system_op (thread_p) == NULL)
	    {
	      assert_release (false);
	      error_code = ER_FAILED;
	      goto error;
	    }
	  is_system_op_started = true;

	  /* Create two new b-tree pages. */
	  /* First page. */
	  crt_vpid = pgbuf_get_vpid_ptr (*crt_page);
	  new_page1 =
	    btree_get_new_page (thread_p, btid_int, &new_page_vpid1,
				crt_vpid);
	  if (new_page1 == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	  /* Log the newly allocated pageid for deallocation for undo
	   * purposes.
	   */
	  pageid_struct.vpid = new_page_vpid1;
	  pageid_struct.vfid.fileid = btid_int->sys_btid->vfid.fileid;
	  pageid_struct.vfid.volid = btid_int->sys_btid->vfid.volid;
	  log_append_undo_data2 (thread_p, RVBT_NEW_PGALLOC,
				 &btid_int->sys_btid->vfid, NULL, -1,
				 sizeof (PAGEID_STRUCT), &pageid_struct);
	  /* Second page. */
	  new_page2 =
	    btree_get_new_page (thread_p, btid_int, &new_page_vpid2,
				crt_vpid);
	  if (new_page2 == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	  /* Log the newly allocated pageid for deallocation for undo
	   * purposes.
	   */
	  pageid_struct.vpid = new_page_vpid2;
	  assert (pageid_struct.vfid.fileid
		  == btid_int->sys_btid->vfid.fileid);
	  assert (pageid_struct.vfid.volid == btid_int->sys_btid->vfid.volid);
	  log_append_undo_data2 (thread_p, RVBT_NEW_PGALLOC,
				 &btid_int->sys_btid->vfid, NULL, -1,
				 sizeof (PAGEID_STRUCT), &pageid_struct);

	  /* Split the root. */
	  error_code =
	    btree_split_root (thread_p, btid_int, *crt_page, new_page1,
			      new_page2, crt_vpid, &new_page_vpid1,
			      &new_page_vpid2, node_type, key, &advance_vpid);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }

	  if (insert_helper->log_operations)
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "BTREE_INSERT: split root %d|%d and create "
			     "nodes %d|%d and %d|%d in index (%d, %d|%d).\n",
			     pgbuf_get_volume_id (*crt_page),
			     pgbuf_get_page_id (*crt_page),
			     new_page_vpid1.volid, new_page_vpid1.pageid,
			     new_page_vpid2.volid, new_page_vpid2.pageid,
			     btid_int->sys_btid->root_pageid,
			     btid_int->sys_btid->vfid.volid,
			     btid_int->sys_btid->vfid.fileid);
	    }

#if !defined(NDEBUG)
	  /* Safe guard checks */
	  (void) spage_check_num_slots (thread_p, *crt_page);
	  (void) spage_check_num_slots (thread_p, new_page1);
	  (void) spage_check_num_slots (thread_p, new_page2);
#endif

	  /* Unfix root and choose adequate child to advance to (based on
	   * given key).
	   * The child is hinted by btree_split_root through advance_vpid.
	   */
	  pgbuf_unfix_and_init (thread_p, *crt_page);
	  /* Which child? */
	  if (VPID_EQ (&advance_vpid, &new_page_vpid1))
	    {
	      /* Go to new page 1. */

	      /* Unfix the other child. */
	      pgbuf_unfix_and_init (thread_p, new_page2);

	      /* End opened system operation. */
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	      is_system_op_started = false;

	      /* Choose child 1 to advance. */
	      *crt_page = new_page1;
	      new_page1 = NULL;

	      /* Set insert_helper->is_crt_node_write_latched to avoid trying
	       * promotion on current node.
	       */
	      insert_helper->is_crt_node_write_latched = true;
	    }
	  else if (VPID_EQ (&advance_vpid, &new_page_vpid2))
	    {
	      /* Go to new page 1. */

	      /* Unfix the other child. */
	      pgbuf_unfix_and_init (thread_p, new_page1);

	      /* End opened system operation. */
	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	      is_system_op_started = false;

	      /* Choose child 2 to advance. */
	      *crt_page = new_page2;
	      new_page2 = NULL;

	      insert_helper->is_crt_node_write_latched = true;
	    }
	  else
	    {
	      /* Impossible! */
	      assert_release (false);

	      pgbuf_unfix_and_init (thread_p, new_page1);
	      pgbuf_unfix_and_init (thread_p, new_page2);

	      /* Error */
	      goto error;
	    }
	  assert (*crt_page != NULL);

	  /* Re-obtain node header. */
	  node_header = btree_get_node_header (*crt_page);
	  if (node_header == NULL)
	    {
	      /* Unexpected. */
	      assert (false);
	      goto error;
	    }
	  node_type =
	    (node_header->node_level ==
	     1) ? BTREE_LEAF_NODE : BTREE_NON_LEAF_NODE;

	  insert_helper->is_root = false;
	}
      assert (node_type != BTREE_LEAF_NODE
	      || pgbuf_get_latch_mode (*crt_page) == PGBUF_LATCH_WRITE);
    }

  /* Here, node represented by *crt_page has enough space to handle a child
   * split. Either because it was root and was split into two nodes in this
   * call or because it was non-root and was split in previous iteration
   * (if split was required of course).
   */

  if (node_type == BTREE_LEAF_NODE)
    {
      assert (pgbuf_get_latch_mode (*crt_page) == PGBUF_LATCH_WRITE);

      /* No other child. Notify called this is a leaf node and return the slot
       * of new key.
       */
      error_code =
	btree_search_leaf_page (thread_p, btid_int, *crt_page, key,
				search_key);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      *is_leaf = true;
      return NO_ERROR;
    }

  /* Node is non-leaf. */
  /* Check if the child we would normally advance to from this node requires
   * splitting. Also check if child should update its max key length.
   */

  /* If next child is leaf, it must be treated differently. See NOTE 2,3 from
   * the big comment.
   */
  is_child_leaf = node_header->node_level == 2;

  /* Find and fix the child to advance to. Use write latch if the child is
   * leaf or if it is already known that it will require an update of max key
   * length.
   */
  error_code =
    btree_search_nonleaf_page (thread_p, btid_int, *crt_page, key,
			       &child_slotid, &child_vpid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }
  child_page =
    pgbuf_fix (thread_p, &child_vpid, OLD_PAGE,
	       (is_child_leaf || insert_helper->need_update_max_key_len) ?
	       PGBUF_LATCH_WRITE : insert_helper->nonleaf_latch_mode,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (child_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto error;
    }

#if !defined (NDEBUG)
  /* Save parent max key length and node level. */
  parent_max_key_len = node_header->max_key_len;
  parent_node_level = node_header->node_level;
#endif

  insert_helper->is_root = false;

  /* Get child node header */
  node_header = btree_get_node_header (child_page);
  if (node_header == NULL)
    {
      assert (false);
      error_code = er_errid ();
      goto error;
    }
  node_type = is_child_leaf ? BTREE_LEAF_NODE : BTREE_NON_LEAF_NODE;
  key_count = btree_node_number_of_keys (child_page);
  assert (key_count >= 0);
  assert (key_count > 0 || is_child_leaf);

  assert (parent_max_key_len >= node_header->max_key_len);
  assert (parent_node_level == node_header->node_level + 1);

  /* Safe guard: if current node max key length was updated, it should be
   *             write latched. So either its max key length was not updated
   *             or used latch mode must be write mode.
   */
  assert (!insert_helper->need_update_max_key_len
	  || insert_helper->is_crt_node_write_latched);

  /* Is updating max key length necessary? True if:
   * 1. Parent node already needed an update
   *    (insert_helper->need_update_max_key_len is set to true).
   * 2. Current node*/
  need_update_max_key_len =
    insert_helper->need_update_max_key_len
    || (is_new_key_possible
	&& insert_helper->key_len_in_page > node_header->max_key_len);

  max_key_len =
    need_update_max_key_len ? insert_helper->key_len_in_page : node_header->
    max_key_len;
  max_new_data_size = 0;
  if (!is_child_leaf || is_new_key_possible)
    {
      /* A new entry is possible due to split of child or due to new key. */
      max_new_data_size += BTREE_NEW_ENTRY_MAX_SIZE (max_key_len, node_type);
    }
  else
    {
      /* Only adding DELID is required and maybe an overflow OID VPID. */
      max_new_data_size += OR_MVCCID_SIZE;
    }
  need_split =
    max_new_data_size > spage_max_space_for_new_record (thread_p, child_page);

  /* If split is needed, we first need to make sure current node is latched
   * exclusively. A new entry must be added.
   * Promoting latch from read to write may be required if the node is not
   * already latched exclusively.
   * Node is not already exclusive if:
   * 1. traversal is done in read mode (insert_helper->nonleaf_latch_mode).
   * 2. node was not part of a split and did not need an update of max key
   *    length.
   */
  if (need_split
      && insert_helper->nonleaf_latch_mode == PGBUF_LATCH_READ
      && !insert_helper->is_crt_node_write_latched)
    {
      /* Promote mode depends on whether child is leaf or not. See NOTE 3 from
       * the above big comment.
       */
      error_code =
	pgbuf_promote_read_latch (thread_p, crt_page,
				  is_child_leaf ? PGBUF_PROMOTE_SINGLE_READER
				  : PGBUF_PROMOTE_SHARED_READER);
      if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	{
	  /* Could not promote. Restart insert from root by using write latch
	   * directly.
	   */
	  insert_helper->nonleaf_latch_mode = PGBUF_LATCH_WRITE;
	  *restart = true;
	  pgbuf_unfix_and_init (thread_p, child_page);
	  pgbuf_unfix_and_init (thread_p, *crt_page);
	  return NO_ERROR;
	}
      else if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      else if (*crt_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
    }

  /* Do we need to promote child node latch mode? It must currently be read
   * and should be promoted to write.
   */
  if ((need_split || need_update_max_key_len)	/* need write latch */
      && (insert_helper->nonleaf_latch_mode == PGBUF_LATCH_READ
	  && !insert_helper->need_update_max_key_len
	  && !is_child_leaf) /* and had read latch */ )
    {
      error_code =
	pgbuf_promote_read_latch (thread_p, &child_page,
				  PGBUF_PROMOTE_SHARED_READER);
      if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	{
	  /* Could not promote. Restart insert from root by using write latch
	   * directly.
	   */
	  insert_helper->nonleaf_latch_mode = PGBUF_LATCH_WRITE;
	  *restart = true;
	  pgbuf_unfix_and_init (thread_p, child_page);
	  pgbuf_unfix_and_init (thread_p, *crt_page);
	  return NO_ERROR;
	}
      else if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      else if (child_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
    }

  if (need_update_max_key_len)
    {
      /* Update max key length. */
      node_header->max_key_len = insert_helper->key_len_in_page;
      error_code =
	btree_node_header_redo_log (thread_p, &btid_int->sys_btid->vfid,
				    child_page);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      pgbuf_set_dirty (thread_p, child_page, DONT_FREE);

      if (insert_helper->log_operations)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_INSERT: Update node %d|%d max_key_length to "
			 "%d in index (%d, %d|%d).\n",
			 child_vpid.volid, child_vpid.pageid,
			 node_header->max_key_len,
			 btid_int->sys_btid->root_pageid,
			 btid_int->sys_btid->vfid.volid,
			 btid_int->sys_btid->vfid.fileid);
	}

      /* If this node required to update its max key length, then also
       * the children we meet will require to update their max key length.
       * (rule being that parent->max_key_len >= child->max_key_len).
       */
      insert_helper->need_update_max_key_len = true;
      insert_helper->is_crt_node_write_latched = true;
    }

  if (need_split)
    {
      if (log_start_system_op (thread_p) == NULL)
	{
	  /* Failed starting system operation! */
	  assert_release (false);
	  goto error;
	}
      is_system_op_started = true;

      /* Get a new page */
      crt_vpid = pgbuf_get_vpid_ptr (*crt_page);
      new_page1 =
	btree_get_new_page (thread_p, btid_int, &new_page_vpid1, crt_vpid);
      if (new_page1 == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
      /* Log the newly allocated pageid for deallocation for undo purposes */
      pageid_struct.vpid = new_page_vpid1;
      pageid_struct.vfid.fileid = btid_int->sys_btid->vfid.fileid;
      pageid_struct.vfid.volid = btid_int->sys_btid->vfid.volid;
      log_append_undo_data2 (thread_p, RVBT_NEW_PGALLOC,
			     &btid_int->sys_btid->vfid, NULL, -1,
			     sizeof (PAGEID_STRUCT), &pageid_struct);

      /* Split the node. */
      error_code =
	btree_split_node (thread_p, btid_int, *crt_page, child_page,
			  new_page1, crt_vpid, &child_vpid, &new_page_vpid1,
			  child_slotid, node_type, key, &advance_vpid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}

      if (insert_helper->log_operations)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_INSERT: Split node %d|%d of "
			 "index (%d, %d|%d).\n",
			 child_vpid.volid, child_vpid.pageid,
			 node_header->max_key_len,
			 btid_int->sys_btid->root_pageid,
			 btid_int->sys_btid->vfid.volid,
			 btid_int->sys_btid->vfid.fileid);
	}

      /* Choose which of the split nodes we need to advance to. */
      if (VPID_EQ (&advance_vpid, &child_vpid))
	{
	  /* Go to current child. */

	  /* Unfix newly created page. */
	  pgbuf_unfix_and_init (thread_p, new_page1);

	  /* End opened system operation. */
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	  is_system_op_started = false;

	  *advance_to_page = child_page;
	  insert_helper->is_crt_node_write_latched = true;

	  return NO_ERROR;
	}
      else if (VPID_EQ (&advance_vpid, &new_page_vpid1))
	{
	  /* Go to newly allocated node. */

	  /* Unfix current child page. */
	  pgbuf_unfix_and_init (thread_p, child_page);

	  /* End opened system operation. */
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	  is_system_op_started = false;

	  *advance_to_page = new_page1;
	  insert_helper->is_crt_node_write_latched = true;

	  return NO_ERROR;
	}
      else
	{
	  /* Impossible. */
	  assert_release (false);

	  pgbuf_unfix_and_init (thread_p, child_page);
	  pgbuf_unfix_and_init (thread_p, new_page1);
	  pgbuf_unfix_and_init (thread_p, *crt_page);

	  error_code = ER_FAILED;
	  goto error;
	}
    }
  assert (!need_split);
  /* Split was not required. Just advance to current child. */

  if (!need_update_max_key_len
      && insert_helper->nonleaf_latch_mode == PGBUF_LATCH_READ)
    {
      /* Node was fixed with read latch. */
      insert_helper->is_crt_node_write_latched = false;
    }
  *advance_to_page = child_page;

  /* Finished successfully. */
  return NO_ERROR;

error:
  /* Error. */
  if (is_system_op_started)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }
  if (new_page1 != NULL)
    {
      pgbuf_unfix_and_init (thread_p, new_page1);
    }
  if (new_page2 != NULL)
    {
      pgbuf_unfix_and_init (thread_p, new_page2);
    }
  if (child_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, child_page);
    }
  assert (*advance_to_page == NULL);
  assert (error_code != NO_ERROR);
  ASSERT_ERROR ();
  return error_code;
}

/*
 * btree_key_insert_new_object () - BTREE_PROCESS_KEY_FUNCTION used for
 *				    inserting new object in b-tree.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree info.
 * record (in)	   : B-tree leaf/overflow record.
 * object_ptr (in) : Pointer to object in record data.
 * oid (in)	   : Object OID.
 * class_oid (in)  : Object class OID.
 * mvcc_info (in)  : Object MVCC info.
 * stop (out)	   : Set to true if index is unique and visible object is
 *		     found and if this is not a debug build.
 * args (in/out)   : BTREE_INSERT_HELPER *.
 */
static int
btree_key_insert_new_object (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			     DB_VALUE * key, PAGE_PTR * leaf_page,
			     BTREE_SEARCH_KEY_HELPER * search_key,
			     bool * restart, void *other_args)
{
  /* B-tree insert helper used as argument for different btree insert
   * functions.
   */
  BTREE_INSERT_HELPER *insert_helper = (BTREE_INSERT_HELPER *) other_args;
  int error_code = NO_ERROR;	/* Error code. */
  RECDES leaf_record;		/* Record descriptor for leaf key record. */
  char data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];	/* Data buffer used
							 * to copy record
							 * data.
							 */
  LEAF_REC leaf_info;		/* Leaf record info. */
  int offset_after_key;		/* Offset in record data where packed key is
				 * ended.
				 */
  bool dummy_clear_key;		/* Dummy field used as argument for
				 * btree_read_record.
				 */

  /* Recovery structures. */
  char rv_undo_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  char *rv_undo_data_bufalign =
    PTR_ALIGN (rv_undo_data_buffer, BTREE_MAX_ALIGN);
  int rv_undo_data_capacity = IO_MAX_PAGE_SIZE;

  char rv_redo_data_buffer[BTREE_RV_REDO_BUFFER_SIZE + BTREE_MAX_ALIGN];

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (leaf_page != NULL && *leaf_page != NULL
	  && pgbuf_get_latch_mode (*leaf_page) == PGBUF_LATCH_WRITE);
  assert (search_key != NULL);
  assert (search_key->slotid > 0
	  && search_key->slotid
	  <= btree_node_number_of_keys (*leaf_page) + 1);
  assert (restart != NULL);
  assert (insert_helper != NULL);
  assert (insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT
	  || insert_helper->purpose == BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE);

  /* Do not allow inserting a deleted object. It should never happen
   *
   * Insert new object should insert objects with no delete MVCCID.
   *
   * Rollback of object physical removal, cannot reach here with a
   * deleted object. There are three types of physical removal:
   * - Delete object (should not have a delete MVCCID).
   * - Rollback insert (should not have a delete MVCCID).
   * - Vacuum (deleted object). However, vacuum is not rollbacked.
   */
  assert (!BTREE_MVCC_INFO_IS_DELID_VALID
	  (BTREE_INSERT_MVCC_INFO (insert_helper)));

  /* Prepare log data */
  insert_helper->leaf_addr.offset = search_key->slotid;
  insert_helper->leaf_addr.pgptr = *leaf_page;
  insert_helper->leaf_addr.vfid = &btid_int->sys_btid->vfid;
  /* Based on recovery index it is know if this is MVCC-like operation or not.
   * Particularly important for vacuum.
   */
  /* Undo physical delete will add a compensate record and doesn't require
   * undo recovery data.
   */
  /* Prepare undo data. */
  if (insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT)
    {
      insert_helper->rcvindex =
	BTREE_MVCC_INFO_IS_INSID_NOT_ALL_VISIBLE
	(BTREE_INSERT_MVCC_INFO (insert_helper)) ?
	RVBT_MVCC_INSERT_OBJECT : RVBT_NON_MVCC_INSERT_OBJECT;
      insert_helper->rv_undo_data = rv_undo_data_bufalign;
      error_code =
	btree_rv_save_keyval_for_undo (btid_int, key,
				       BTREE_INSERT_CLASS_OID (insert_helper),
				       BTREE_INSERT_OID (insert_helper),
				       BTREE_INSERT_MVCC_INFO (insert_helper),
				       insert_helper->purpose,
				       rv_undo_data_bufalign,
				       &insert_helper->rv_undo_data,
				       &rv_undo_data_capacity,
				       &insert_helper->rv_undo_data_length);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }
  else				/* BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE */
    {
      insert_helper->rcvindex = RVBT_RECORD_MODIFY_COMPENSATE;
    }
  insert_helper->rv_redo_data =
    PTR_ALIGN (rv_redo_data_buffer, BTREE_MAX_ALIGN);
  insert_helper->rv_redo_data_ptr = insert_helper->rv_redo_data;

  /* Does key already exist? */
  if (search_key->result != BTREE_KEY_FOUND)
    {
      /* Key doesn't exist. Insert new key. */
      error_code =
	btree_key_insert_new_key (thread_p, btid_int, key, *leaf_page,
				  insert_helper, search_key);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      if (insert_helper->rv_undo_data != NULL
	  && insert_helper->rv_undo_data != rv_undo_data_bufalign)
	{
	  db_private_free_and_init (thread_p, insert_helper->rv_undo_data);
	}
      return NO_ERROR;
    }
  /* Key was found. Append new object to existing key. */

  /* Initialize leaf record */
  leaf_record.type = REC_HOME;
  leaf_record.area_size = DB_PAGESIZE;
  leaf_record.data = PTR_ALIGN (data_buffer, BTREE_MAX_ALIGN);

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* Call unique insert function. */
      error_code =
	btree_key_append_object_unique (thread_p, btid_int, key, leaf_page,
					restart, search_key, insert_helper,
					&leaf_record);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }
  else
    {
      /* Get record and call non-unique insert function. */
      if (spage_get_record (*leaf_page, search_key->slotid, &leaf_record,
			    COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, &leaf_record,
				       BTREE_LEAF_NODE, NULL);
#endif /* !NDEBUG */

      error_code =
	btree_read_record (thread_p, btid_int, *leaf_page, &leaf_record,
			   NULL, &leaf_info, BTREE_LEAF_NODE,
			   &dummy_clear_key, &offset_after_key,
			   PEEK_KEY_VALUE, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}

#if !defined (NDEBUG)
      if (oid_is_db_class (BTREE_INSERT_CLASS_OID (insert_helper)))
	{
	  /* Although the indexes on _db_class and _db_attribute are not
	   * unique, they cannot have duplicate OID's. Check here this
	   * consistency (no visible objects should exist).
	   */
	  btree_key_record_check_no_visible (thread_p, btid_int, *leaf_page,
					     search_key->slotid);
	}
#endif /* !NDEBUG */

      error_code =
	btree_key_append_object_non_unique (thread_p, btid_int, key,
					    *leaf_page, search_key,
					    &leaf_record, offset_after_key,
					    &leaf_info,
					    &insert_helper->obj_info,
					    insert_helper);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }

  if (insert_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Successfully executed insert "
		     "object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s in index (%d, %d|%d).\n",
		     insert_helper->obj_info.oid.volid,
		     insert_helper->obj_info.oid.pageid,
		     insert_helper->obj_info.oid.slotid,
		     insert_helper->obj_info.class_oid.volid,
		     insert_helper->obj_info.class_oid.pageid,
		     insert_helper->obj_info.class_oid.slotid,
		     insert_helper->obj_info.mvcc_info.insert_mvccid,
		     insert_helper->obj_info.mvcc_info.delete_mvccid,
		     insert_helper->printed_key != NULL ?
		     insert_helper->printed_key : "(unknown)",
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

#if !defined (NDEBUG)
  (void) btree_verify_node (thread_p, btid_int, *leaf_page);
#endif /* !NDEBUG */

  if (insert_helper->rv_undo_data != NULL
      && insert_helper->rv_undo_data != rv_undo_data_bufalign)
    {
      db_private_free_and_init (thread_p, insert_helper->rv_undo_data);
    }

  /* Return result. */
  return NO_ERROR;

error:
  if (insert_helper->rv_undo_data != NULL
      && insert_helper->rv_undo_data != rv_undo_data_bufalign)
    {
      db_private_free_and_init (thread_p, insert_helper->rv_undo_data);
    }

  assert_release (error_code != NO_ERROR);
  return error_code;
}

/*
 * btree_key_insert_new_key () - Insert new key in b-tree.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * btid_int (in)      : B-tree info.
 * key (in)	      : Key value.
 * leaf_page (in)     : Leaf node page.
 * insert_helper (in) : Insert helper.
 * search_key (in)    : Search key result.
 */
static int
btree_key_insert_new_key (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			  DB_VALUE * key, PAGE_PTR leaf_page,
			  BTREE_INSERT_HELPER * insert_helper,
			  BTREE_SEARCH_KEY_HELPER * search_key)
{
  int error_code = NO_ERROR;
  int key_len;
  int key_type = BTREE_NORMAL_KEY;
  int key_cnt;
  DB_VALUE local_key;
  RECDES record;
  char data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  BTREE_NODE_HEADER *node_header;
  int rv_redo_data_length;
  bool update_max_key_length = false;
  DB_VALUE *new_key = key;

  /* Redo recovery. */
  /* One page size buffer can handle one b-tree record entirely and any
   * additional recovery data (e.g. debug info).
   */
  char rv_redo_recovery_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  char *rv_redo_data = PTR_ALIGN (rv_redo_recovery_buffer, BTREE_MAX_ALIGN);
  char *rv_redo_data_ptr = rv_redo_data;
  /* Do not use insert_helper redo structures since they may not be able to
   * handle the entire record.
   */

  /* Insert new key into search_key->slotid. */
  insert_helper->is_unique_key_added_or_deleted = true;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (leaf_page != NULL && btree_get_node_level (leaf_page) == 1
	  && pgbuf_get_latch_mode (leaf_page) == PGBUF_LATCH_WRITE);
  assert (insert_helper != NULL);
  assert (search_key != NULL && search_key->result != BTREE_KEY_FOUND);
  assert (search_key->slotid > 0
	  && search_key->slotid <= btree_node_number_of_keys (leaf_page) + 1);
  assert (insert_helper->rv_redo_data != NULL
	  && insert_helper->rv_redo_data_ptr != NULL);
#if defined (SERVER_MODE)
  assert (!BTREE_IS_UNIQUE (btid_int->unique_pk)
	  || log_is_in_crash_recovery ()
	  || lock_has_lock_on_object (BTREE_INSERT_OID (insert_helper),
				      BTREE_INSERT_CLASS_OID (insert_helper),
				      thread_get_current_tran_index (),
				      X_LOCK) > 0);
#endif /* SERVER_MODE */

  /* Insert new key. */
  DB_MAKE_NULL (&local_key);

  key_len = btree_get_disk_size_of_key (key);
  if (key_len >= BTREE_MAX_KEYLEN_INPAGE)
    {
      key_type = BTREE_OVERFLOW_KEY;
    }
  else
    {
      int diff_column;

      diff_column = btree_node_common_prefix (thread_p, btid_int, leaf_page);
      if (diff_column > 0)
	{
	  /* Remove columns from key value. */
	  /* Use local_key as buffer. */
	  new_key = &local_key;
	  pr_clone_value (key, new_key);
	  pr_midxkey_remove_prefix (new_key, diff_column);
	}
      else if (diff_column < 0)
	{
	  ASSERT_ERROR ();
	  return diff_column;
	}
    }
  record.type = REC_HOME;
  record.data = PTR_ALIGN (data_buffer, MAX_ALIGNMENT);
  record.area_size = DB_PAGESIZE;
  error_code =
    btree_write_record (thread_p, btid_int, NULL, new_key, BTREE_LEAF_NODE,
			key_type, key_len, false,
			BTREE_INSERT_CLASS_OID (insert_helper),
			BTREE_INSERT_OID (insert_helper),
			BTREE_INSERT_MVCC_INFO (insert_helper), &record);
  if (new_key == &local_key)
    {
      pr_clear_value (&local_key);
    }

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, &record,
				   BTREE_LEAF_NODE, NULL);
#endif

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* Node header will be updated. */
  node_header = btree_get_node_header (leaf_page);
  if (node_header == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Nothing should fail after spage_insert_at! */
  if (spage_insert_at (thread_p, leaf_page, search_key->slotid, &record)
      != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  key_cnt = btree_node_number_of_keys (leaf_page);
  key_len = BTREE_GET_KEY_LEN_IN_PAGE (key_len);
  /* Do not write log for updating header. Redo recovery function of insert
   * key will know to update it.
   */
  if (key_len > node_header->max_key_len)
    {
      update_max_key_length = true;
      node_header->max_key_len = key_len;
    }

  assert (node_header->split_info.pivot >= 0 && key_cnt > 0);
  btree_split_next_pivot (&node_header->split_info,
			  (float) search_key->slotid / key_cnt, key_cnt);

  /* Log */
  assert (insert_helper->leaf_addr.offset == search_key->slotid);

  /* Redo logging. */
  LOG_RV_RECORD_SET_MODIFY_MODE (&insert_helper->leaf_addr,
				 LOG_RV_RECORD_INSERT);
#if !defined (NDEBUG)
  /* For debugging info. */
  BTREE_RV_REDO_SET_DEBUG_INFO (&insert_helper->leaf_addr,
				rv_redo_data_ptr, btid_int,
				BTREE_RV_DEBUG_ID_INS_KEY);
#endif /* !NDEBUG */
  if (update_max_key_length)
    {
      /* Put key length. */
      rv_redo_data_ptr = or_pack_int (rv_redo_data_ptr, key_len);
      BTREE_RV_REDO_SET_UPDATE_MAX_KEY_LEN (&insert_helper->leaf_addr);
    }
  /* Put record data. */
  memcpy (rv_redo_data_ptr, record.data, record.length);
  rv_redo_data_ptr += record.length;

  if (insert_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Inserted new key=%s in node %d|%d at "
		     "slot %d, lsa=%lld|%d, in index (%d, %d|%d).\n",
		     insert_helper->printed_key != NULL ?
		     insert_helper->printed_key : "(unknown)",
		     pgbuf_get_volume_id (leaf_page),
		     pgbuf_get_page_id (leaf_page), search_key->slotid,
		     (long long int) pgbuf_get_lsa (leaf_page)->pageid,
		     (int) pgbuf_get_lsa (leaf_page)->offset,
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  /* Add logging. */
  rv_redo_data_length = CAST_BUFLEN (rv_redo_data_ptr - rv_redo_data);
  assert (rv_redo_data_length < DB_PAGESIZE);
  if (insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT)
    {
      /* Undo/redo logging. */
      log_append_undoredo_data (thread_p, insert_helper->rcvindex,
				&insert_helper->leaf_addr,
				insert_helper->rv_undo_data_length,
				rv_redo_data_length,
				insert_helper->rv_undo_data, rv_redo_data);
    }
  else				/* BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE */
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (leaf_page),
			     insert_helper->leaf_addr.offset, leaf_page,
			     rv_redo_data_length, rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, leaf_page, DONT_FREE);

#if !defined(NDEBUG)
  if (prm_get_integer_value (PRM_ID_ER_BTREE_DEBUG) & BTREE_DEBUG_DUMP_SIMPLE)
    {
      VPID *vpid = pgbuf_get_vpid_ptr (leaf_page);
      fprintf (stdout, "btree insert at (%d:%d:%d) with key:",
	       vpid->volid, vpid->pageid, search_key->slotid);
      db_value_print (key);
      fprintf (stdout, "\n");
    }
#endif

#if !defined(NDEBUG)
  (void) btree_verify_node (thread_p, btid_int, leaf_page);
#endif

  return NO_ERROR;
}

#if defined (SERVER_MODE)
/*
 * btree_key_insert_does_leaf_need_split () - Check if there is not enough
 *					      space in leaf node to handle new
 *					      object.
 *
 * return	      : True if there is not enough space in page.
 * thread_p (in)      : Thread entry.
 * btid_int (in)      : B-tree info.
 * leaf_page (in)     : Leaf page.
 * insert_helper (in) : Insert helper.
 * search_key (in)    : Search key result.
 */
static bool
btree_key_insert_does_leaf_need_split (THREAD_ENTRY * thread_p,
				       BTID_INT * btid_int,
				       PAGE_PTR leaf_page,
				       BTREE_INSERT_HELPER * insert_helper,
				       BTREE_SEARCH_KEY_HELPER * search_key)
{
  int max_new_data_size = 0;
  BTREE_NODE_HEADER *node_header = NULL;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (leaf_page != NULL && btree_get_node_level (leaf_page) == 1);
  assert (insert_helper != NULL);
  assert (search_key != NULL);

  if (search_key->result == BTREE_KEY_FOUND)
    {
      /* Does a new object fit the page? */
      return (BTREE_OBJECT_MAX_SIZE
	      > spage_get_free_space_without_saving (thread_p, leaf_page,
						     NULL));
    }
  else
    {
      /* Does a new key fit the page? */
      max_new_data_size =
	BTREE_NEW_ENTRY_MAX_SIZE (insert_helper->key_len_in_page,
				  BTREE_LEAF_NODE);
      return (max_new_data_size
	      > spage_max_space_for_new_record (thread_p, leaf_page));
    }
}
#endif /* SERVER_MODE */

/*
 * btree_key_append_object_unique () - Append new object into an existing
 *				       unique index key. New objects are
 *				       always inserted at the beginning of the
 *				       key (as long as unique constraint is
 *				       not violated).
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid_int (in)       : B-tree info.
 * key (in)	       : Inserted key.
 * leaf (in/out)       : Pointer to leaf page (can be re-fixed).
 * restart (out)       : Outputs true when restarting from b-tree root is
 *			 required.
 * search_key (in/out) : Search key result.
 * insert_helper (in)  : Insert operation helper structure.
 * leaf_record (in)    : Preallocated record descriptor used to read b-tree
 *			 record.
 */
static int
btree_key_append_object_unique (THREAD_ENTRY * thread_p,
				BTID_INT * btid_int, DB_VALUE * key,
				PAGE_PTR * leaf, bool * restart,
				BTREE_SEARCH_KEY_HELPER * search_key,
				BTREE_INSERT_HELPER * insert_helper,
				RECDES * leaf_record)
{
  int error_code = NO_ERROR;	/* Error code. */
  BTREE_OBJECT_INFO first_object;	/* Current first object in
					 * record. It will be replaced.
					 */
  LEAF_REC leaf_info;		/* Leaf record info. */
  int offset_after_key;		/* Offset in record where packed
				 * key ends.
				 */
  bool dummy_clear_key;		/* Dummy */
  /* Used by btree_key_find_and_lock_unique. */
  BTREE_FIND_UNIQUE_HELPER find_unique_helper =
    BTREE_FIND_UNIQUE_HELPER_INITIALIZER;
  RECINS_STRUCT recins = RECINS_STRUCT_INITIALIZER;	/* Redo recovery
							 * structure.
							 */
  MVCC_SNAPSHOT mvcc_snapshot_dirty;	/* Dirty snapshot used to count
					 * visible objects for
					 * multi-update.
					 */
  bool is_key_record_read = false;	/* Set to true when key record
					 * is read (btree_read_record
					 * function was called).
					 */
  int max_visible_oids = 1;	/* Used to check visible OID's for
				 * REPEATABLE READ isolation.
				 */
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;	/* Used to check visibility for
					 * REPEATABLE READ isolation.
					 */
  int num_visible = 0;		/* Used to count number of visible
				 * objects.
				 */
  int rv_redo_data_length = 0;

#if defined (SERVER_MODE)
  LOG_LSA saved_leaf_lsa;	/* Save page LSA before locking the first
				 * object in record. If LSA is changed,
				 * it is no longer guaranteed that page has
				 * enough space for new key/object.
				 * We need to be conservative and check it.
				 */
#endif /* SERVER_MODE */

  /* Assert expected arguments. */
  assert (btid_int != NULL && BTREE_IS_UNIQUE (btid_int->unique_pk));
  assert (key != NULL);
  assert (leaf != NULL);
  assert (restart != NULL);
  assert (search_key != NULL && search_key->result == BTREE_KEY_FOUND);
  assert (insert_helper->rv_redo_data != NULL
	  && insert_helper->rv_redo_data_ptr != NULL);
#if defined (SERVER_MODE)
  assert (log_is_in_crash_recovery ()
	  || lock_has_lock_on_object (BTREE_INSERT_OID (insert_helper),
				      BTREE_INSERT_CLASS_OID (insert_helper),
				      thread_get_current_tran_index (),
				      X_LOCK) > 0);
#endif /* SERVER_MODE */

  /* TODO: Lock escalation. */
  /* TODO: Handle BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE
   *       No locks are required.
   *       No undo logging is required (is it?).
   */

  /* Insert object in the beginning of leaf record if unique constraint is not
   * violated.
   *
   * Step 1: Protect key by locking its first object. Every transaction that
   *         insert or delete objects will follow this rule. So, the inserter
   *         must first lock the first object, check its status and decide
   *         if insert is possible or if it would violation unique constraint.
   *         After locking the first object, if it is deleted and committed,
   *         then unique constraint would not be violated. Otherwise, if the
   *         object is not deleted (and nobody else is trying to delete it),
   *         inserting new object is no longer accepted.
   *         Use btree_key_find_and_lock_unique_of_unique to find and
   *         lock the first object.
   *
   * Step 2: Once the first object is locked and it is established that unique
   *         constraint is preserved, adding object to key record can be done.
   *         Because the first object is always considered last key version,
   *         inserting new object must be done in the beginning of leaf
   *         record. So, the current first object must be relocated (it
   *         simulates a regular insert and follows the regular insert rules).
   *         Then, the first object is replaced with new object.
   */

#if defined (SERVER_MODE)
  /* Transfer locked object from insert helper to find unique helper. */
  COPY_OID (&find_unique_helper.locked_oid, &insert_helper->saved_locked_oid);
  COPY_OID (&find_unique_helper.locked_class_oid,
	    &insert_helper->saved_locked_class_oid);

  LSA_COPY (&saved_leaf_lsa, pgbuf_get_lsa (*leaf));
#endif /* SERVER_MODE */

  find_unique_helper.lock_mode = X_LOCK;
  error_code =
    btree_key_find_and_lock_unique_of_unique (thread_p, btid_int,
					      key, leaf, search_key,
					      restart, &find_unique_helper);
#if defined (SERVER_MODE)
  /* Transfer locked object from find unique helper to insert helper. */
  COPY_OID (&insert_helper->saved_locked_oid, &find_unique_helper.locked_oid);
  COPY_OID (&insert_helper->saved_locked_class_oid,
	    &find_unique_helper.locked_class_oid);
#endif /* SERVER_MODE */

  if (error_code != NO_ERROR || *restart)
    {
      /* Error occurred or failed to lock key object and keep a relevant leaf
       * node. Return now.
       */
      return error_code;
    }
  /* No error, object was locked and key leaf node is held. */
  assert (*leaf != NULL);

  /* Slot ID may have changed. Update insert_helper->leaf_addr.offset. */
  insert_helper->leaf_addr.offset = search_key->slotid;

#if defined (SERVER_MODE)
  if (!LSA_EQ (&saved_leaf_lsa, pgbuf_get_lsa (*leaf)))
    {
      /* Does leaf need split? */
      if (btree_key_insert_does_leaf_need_split (thread_p, btid_int, *leaf,
						 insert_helper, search_key))
	{
	  /* We need to go back from top and split leaf. */
	  *restart = true;
	  return NO_ERROR;
	}
    }
#endif /* SERVER_MODE */

  if (search_key->result != BTREE_KEY_FOUND)
    {
      /* Key was deleted or vacuumed. */
      /* Insert key directly. */
      return btree_key_insert_new_key (thread_p, btid_int, key, *leaf,
				       insert_helper, search_key);
    }

  if (!find_unique_helper.found_object
      && logtb_find_current_isolation (thread_p) >= TRAN_REPEATABLE_READ)
    {
      /* This could be an object deleted but still visible to me. */
      /* The problem is reading the object next time. Old object, even if
       * it was deleted, on the next read will be visible to me. This object
       * is inserted by me, so it is also visible to me. This means I will
       * see two different objects that have this key, which is obviously
       * a violation of unique constraint.
       * READ COMMITTED doesn't have this problem, since on the next statement
       * it will refresh its snapshot and old object will no longer be visible
       * (even if it was with this snapshot).
       */
      /* TODO: Is this required in STAND ALONE? */

      /* Get current key record. */
      if (spage_get_record (*leaf, search_key->slotid, leaf_record, COPY)
	  != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, leaf_record,
				       BTREE_LEAF_NODE, key);
#endif

      /* Read record. */
      error_code =
	btree_read_record (thread_p, btid_int, *leaf, leaf_record, NULL,
			   &leaf_info, BTREE_LEAF_NODE, &dummy_clear_key,
			   &offset_after_key, PEEK_KEY_VALUE, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      /* Don't repeat key record read. */
      is_key_record_read = true;

      /* Count visible objects considering transaction snapshot. */
      mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
      num_visible =
	btree_get_num_visible_from_leaf_and_ovf (thread_p, btid_int,
						 leaf_record,
						 offset_after_key, &leaf_info,
						 &max_visible_oids,
						 mvcc_snapshot);
      if (num_visible < 0)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      else if (num_visible > 0)
	{
	  /* Unique constraint violation. */
	  if (prm_get_bool_value (PRM_ID_UNIQUE_ERROR_KEY_VALUE))
	    {
	      char *keyval = pr_valstring (key);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_UNIQUE_VIOLATION_WITHKEY, 1,
		      (keyval == NULL) ? "(null)" : keyval);
	      if (keyval != NULL)
		{
		  free_and_init (keyval);
		}
	      return ER_UNIQUE_VIOLATION_WITHKEY;
	    }
	  else
	    {
	      /* Object already exists. Unique constraint violation. */
	      BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, key,
						BTREE_INSERT_OID
						(insert_helper),
						BTREE_INSERT_CLASS_OID
						(insert_helper),
						btid_int->sys_btid, NULL);
	      return ER_BTREE_UNIQUE_FAILED;
	    }
	}
    }
  /* No visible objects or isolation not >= RR */

  /* Unique constraint may be violated if there is already a visible object
   * and if this is not recovery (BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE).
   * Unique constraint is violated in this case, except if op_type is
   * MULTI-ROW-UPDATE. In this case, a duplicate key can be allowed, as long
   * as it will be deleted until the end of query execution (this is checked
   * with unique_stats_info). Even in this case, never allow more than two
   * visible objects.
   * If HA is enabled, never allow more than one visible object.
   */
  if (find_unique_helper.found_object
      && insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT)
    {
      mvcc_snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;

      if (insert_helper->is_unique_multi_update)
	{
	  /* This should be MULTI-ROW-UPDATE. 
	   * Get key record and count visible objects.
	   */

	  /* Get current key record. */
	  if (spage_get_record (*leaf, search_key->slotid, leaf_record, COPY)
	      != S_SUCCESS)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }

#if !defined (NDEBUG)
	  (void) btree_check_valid_record (thread_p, btid_int, leaf_record,
					   BTREE_LEAF_NODE, key);
#endif

	  /* Read record. */
	  error_code =
	    btree_read_record (thread_p, btid_int, *leaf, leaf_record, NULL,
			       &leaf_info, BTREE_LEAF_NODE, &dummy_clear_key,
			       &offset_after_key, PEEK_KEY_VALUE, NULL);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  /* Don't repeat key record read. */
	  is_key_record_read = true;

	  /* Count visible (not dirty) objects. */
	  num_visible =
	    btree_get_num_visible_from_leaf_and_ovf (thread_p, btid_int,
						     leaf_record,
						     offset_after_key,
						     &leaf_info, NULL,
						     &mvcc_snapshot_dirty);
	}

      /* Should we consider this a unique constraint violation? */
      if (!insert_helper->is_unique_multi_update || num_visible > 1)
	{
	  /* Not multi-update operation or there would be more than two
	   * objects visible. Unique constraint violation.
	   */
	  if (prm_get_bool_value (PRM_ID_UNIQUE_ERROR_KEY_VALUE))
	    {
	      char *keyval = pr_valstring (key);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_UNIQUE_VIOLATION_WITHKEY, 1,
		      (keyval == NULL) ? "(null)" : keyval);
	      if (keyval != NULL)
		{
		  free_and_init (keyval);
		}
	      return ER_UNIQUE_VIOLATION_WITHKEY;
	    }
	  else
	    {
	      /* Object already exists. Unique constraint violation. */
	      BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, key,
						BTREE_INSERT_OID
						(insert_helper),
						BTREE_INSERT_CLASS_OID
						(insert_helper),
						btid_int->sys_btid, NULL);
	      return ER_BTREE_UNIQUE_FAILED;
	    }
	}
      else if (insert_helper->is_ha_enabled)
	{
	  /* When HA is enabled, unique constraint can never be violated. */
	  error_code = ER_REPL_MULTI_UPDATE_UNIQUE_VIOLATION;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	  return error_code;
	}

      /* Cannot consider this a new key. */
      insert_helper->is_unique_key_added_or_deleted = false;
    }
  else
    {
      /* All existing objects are deleted. Proceed */
      insert_helper->is_unique_key_added_or_deleted = true;
    }
  /* Unique constraint not violated (yet). */
  /* New object can be inserted. */

  /* Slot ID points to key in page. */
  assert (search_key->slotid > 0
	  && (search_key->slotid <= btree_node_number_of_keys (*leaf) + 1));

  /* Insert new object in leaf record:
   * 1. Get current first object.
   * 2. Relocate first object. Insert it using
   *    btree_key_append_object_non_unique.
   * 3. Replace first object with new object.
   */

  if (!is_key_record_read)
    {
      /* Get current key record. */
      if (spage_get_record (*leaf, search_key->slotid, leaf_record, COPY)
	  != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, leaf_record,
				       BTREE_LEAF_NODE, key);
#endif

      /* Read record. */
      error_code =
	btree_read_record (thread_p, btid_int, *leaf, leaf_record, NULL,
			   &leaf_info, BTREE_LEAF_NODE, &dummy_clear_key,
			   &offset_after_key, PEEK_KEY_VALUE, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  /* Get current first object and its info */
  error_code =
    btree_leaf_get_first_object (btid_int, leaf_record, &first_object.oid,
				 &first_object.class_oid,
				 &first_object.mvcc_info);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

#if !defined (NDEBUG)
  BTREE_RV_REDO_SET_DEBUG_INFO (&insert_helper->leaf_addr,
				insert_helper->rv_redo_data_ptr, btid_int,
				BTREE_RV_DEBUG_ID_UNIQUE);
#endif /* !NDEBUG */
  LOG_RV_RECORD_SET_MODIFY_MODE (&insert_helper->leaf_addr,
				 LOG_RV_RECORD_UPDATE_PARTIAL);

  /* Insert current first object elsewhere. */
  insert_helper->is_relocation_for_unique = true;
  error_code =
    btree_key_append_object_non_unique (thread_p, btid_int, key, *leaf,
					search_key, leaf_record,
					offset_after_key, &leaf_info,
					&first_object, insert_helper);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* TODO: Optimize logging data:
   * Do not log in btree_key_append_object_non_unique, just create redo log
   * data. If object is inserted in leaf page, unify changes into only one
   * log record (replacing first and moving old first to the end).
   * If first object is relocated to overflow, there should be one redo
   * record for overflow page and an undoredo for leaf record.
   */

  /* Replace current first object with new object. */
  btree_leaf_change_first_object (leaf_record, btid_int,
				  BTREE_INSERT_OID (insert_helper),
				  BTREE_INSERT_CLASS_OID (insert_helper),
				  BTREE_INSERT_MVCC_INFO (insert_helper),
				  NULL, &insert_helper->rv_redo_data_ptr);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Update record in page. */
  if (spage_update (thread_p, *leaf, search_key->slotid, leaf_record)
      != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  if (insert_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_INSERT: Replaced first object %d|%d|%d - "
		     "class_oid %d|%d|%d, mvcc_info=%llu|%llu with "
		     "object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu, key=%s - "
		     "in node %d|%d slot %d, in unique index (%d, %d|%d). "
		     "First object was relocated.\n",
		     first_object.oid.volid, first_object.oid.pageid,
		     first_object.oid.slotid, first_object.class_oid.volid,
		     first_object.class_oid.pageid,
		     first_object.class_oid.slotid,
		     first_object.mvcc_info.insert_mvccid,
		     first_object.mvcc_info.delete_mvccid,
		     insert_helper->obj_info.oid.volid,
		     insert_helper->obj_info.oid.pageid,
		     insert_helper->obj_info.oid.slotid,
		     insert_helper->obj_info.class_oid.volid,
		     insert_helper->obj_info.class_oid.pageid,
		     insert_helper->obj_info.class_oid.slotid,
		     insert_helper->obj_info.mvcc_info.insert_mvccid,
		     insert_helper->obj_info.mvcc_info.delete_mvccid,
		     insert_helper->printed_key != NULL ?
		     insert_helper->printed_key : "(unknown)",
		     pgbuf_get_volume_id (*leaf), pgbuf_get_page_id (*leaf),
		     search_key->slotid,
		     (long long int) pgbuf_get_lsa (*leaf)->pageid,
		     (int) pgbuf_get_lsa (*leaf)->offset,
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  /* Add logging. */
  /* Redo logging is collected from btree_key_append_object_non_unique too. */
  rv_redo_data_length =
    CAST_BUFLEN (insert_helper->rv_redo_data_ptr
		 - insert_helper->rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  if (insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT)
    {
      log_append_undoredo_data (thread_p, insert_helper->rcvindex,
				&insert_helper->leaf_addr,
				insert_helper->rv_undo_data_length,
				rv_redo_data_length,
				insert_helper->rv_undo_data,
				insert_helper->rv_redo_data);
    }
  else				/* BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE */
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (*leaf),
			     insert_helper->leaf_addr.offset, *leaf,
			     rv_redo_data_length, insert_helper->rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, *leaf, DONT_FREE);

  /* Success. */
  return error_code;
}

/*
 * btree_key_append_object_non_unique () - Append a new object in an existing
 *					   b-tree key.
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree info.
 * key (in)		 : Key value.
 * leaf (in)		 : Leaf node.
 * search_key (in)	 : Search key result.
 * leaf_record (in)	 : Key's leaf record.
 * offset_after_key (in) : Offset to where packed key is ended in leaf record
 *			   data.
 * leaf_info (in)	 : Leaf record info.
 * btree_obj (in)	 : B-tree object info.
 * insert_helper (in)	 : B-tree insert helper.
 */
static int
btree_key_append_object_non_unique (THREAD_ENTRY * thread_p,
				    BTID_INT * btid_int, DB_VALUE * key,
				    PAGE_PTR leaf,
				    BTREE_SEARCH_KEY_HELPER * search_key,
				    RECDES * leaf_record,
				    int offset_after_key,
				    LEAF_REC * leaf_info,
				    BTREE_OBJECT_INFO * btree_obj,
				    BTREE_INSERT_HELPER * insert_helper)
{
  int n_objects;		/* Current number of leaf objects. If
				 * maximum size is reached, next
				 * object is inserted in overflows.
				 */
  int oid_size = OR_OID_SIZE;	/* OID+class OID size */
  int error_code = NO_ERROR;	/* Error code. */
  PAGE_PTR overflow_page = NULL;	/* Fixed overflow page. */
  PAGE_PTR local_inserted_page = NULL;	/* Store page where OID is inserted
					 * if given argument is NULL.
					 */
  int n_objects_limit = 0;
  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL);
  assert (leaf != NULL);
  assert (search_key != NULL && search_key->slotid > 0
	  && search_key->slotid <= btree_node_number_of_keys (leaf));
  assert (leaf_record != NULL);
  assert (leaf_info != NULL);
  assert (btree_obj != NULL);
  assert (insert_helper->purpose == BTREE_OP_INSERT_NEW_OBJECT
	  || insert_helper->purpose == BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE);
  assert (insert_helper->rv_redo_data != NULL
	  && insert_helper->rv_redo_data_ptr != NULL);

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* Add class OID size too. */
      oid_size += OR_OID_SIZE;
      /* Append OID means this is not first and must be fixed size. */
      BTREE_MVCC_INFO_SET_FIXED_SIZE (&btree_obj->mvcc_info);
    }

  /* Append new object to list of OID's for this key.
   * First it should try to add the object to leaf record.
   * Due to split algorithm the page should always have enough space to add
   * it. However, there is a limit of objects that can be inserted in a leaf
   * record.
   * If that limit is reached, then the new object will be inserted in an
   * overflow OID's page.
   * If there is none or if all existing pages are full, a new page is created
   * and appended to current OID's pages.
   */

  /* The OID list size on leaf page is limited to threshold number. */
  n_objects =
    btree_record_get_num_oids (thread_p, btid_int, leaf_record,
			       offset_after_key, BTREE_LEAF_NODE);
  n_objects_limit =
    CEIL_PTVDIV (BTREE_MAX_OIDLEN_INPAGE, oid_size + 2 * OR_MVCCID_SIZE);
  /* Is inserting another object possible? */
  if (n_objects < n_objects_limit)
    {
      /* Insert in leaf record is possible. */

#if !defined (NDEBUG)
      if (!insert_helper->is_relocation_for_unique)
	{
	  BTREE_RV_REDO_SET_DEBUG_INFO (&insert_helper->leaf_addr,
					insert_helper->rv_redo_data_ptr,
					btid_int,
					BTREE_RV_DEBUG_ID_NON_UNIQUE);
	}
#endif /* !NDEBUG */
      LOG_RV_RECORD_SET_MODIFY_MODE (&insert_helper->leaf_addr,
				     LOG_RV_RECORD_UPDATE_PARTIAL);

      /* Append object at the end of leaf record. */
      btree_record_append_object (thread_p, btid_int, leaf_record,
				  BTREE_LEAF_NODE, btree_obj,
				  &insert_helper->rv_redo_data_ptr);

      /* Update should work. */
      if (spage_update (thread_p, leaf, search_key->slotid, leaf_record)
	  != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

      if (insert_helper->log_operations)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_INSERT: Successfully %s at the "
			 "end of record the object %d|%d|%d, "
			 "class_oid %d|%d|%d, mvcc_info=%llu|%llu, "
			 "in leaf %d|%d slotid=%d, lsa=%lld|%d, key=%s, "
			 "in index (%d, %d|%d).\n",
			 insert_helper->is_relocation_for_unique ?
			 "relocated" : "inserted",
			 btree_obj->oid.volid, btree_obj->oid.pageid,
			 btree_obj->oid.slotid, btree_obj->class_oid.volid,
			 btree_obj->class_oid.pageid,
			 btree_obj->class_oid.slotid,
			 (unsigned long long int)
			 btree_obj->mvcc_info.insert_mvccid,
			 (unsigned long long int)
			 btree_obj->mvcc_info.delete_mvccid,
			 pgbuf_get_volume_id (leaf),
			 pgbuf_get_page_id (leaf), search_key->slotid,
			 (long long int) pgbuf_get_lsa (leaf)->pageid,
			 (int) pgbuf_get_lsa (leaf)->offset,
			 insert_helper->printed_key != NULL ?
			 insert_helper->printed_key : "(unknown)",
			 btid_int->sys_btid->root_pageid,
			 btid_int->sys_btid->vfid.volid,
			 btid_int->sys_btid->vfid.fileid);
	}

      /* Log changes. */
      rv_redo_data_length =
	CAST_BUFLEN (insert_helper->rv_redo_data_ptr
		     - insert_helper->rv_redo_data);
      assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);

      if (insert_helper->is_relocation_for_unique)
	{
	  /* Caller will handle logging. */
	}
      else if (insert_helper->purpose == BTREE_OP_INSERT_UNDO_PHYSICAL_DELETE)
	{
	  log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
				 pgbuf_get_vpid_ptr (leaf),
				 insert_helper->leaf_addr.offset, leaf,
				 rv_redo_data_length,
				 insert_helper->rv_redo_data,
				 LOG_FIND_CURRENT_TDES (thread_p));
	}
      else			/* BTREE_OP_INSERT_NEW_OBJECT */
	{
	  /* Add logging. */
	  log_append_undoredo_data (thread_p, insert_helper->rcvindex,
				    &insert_helper->leaf_addr,
				    insert_helper->rv_undo_data_length,
				    rv_redo_data_length,
				    insert_helper->rv_undo_data,
				    insert_helper->rv_redo_data);
	}
      pgbuf_set_dirty (thread_p, leaf, DONT_FREE);

      return NO_ERROR;
    }

  /* Insert into overflow. */
  /* Overflow OID's have fixed size. */
  BTREE_MVCC_INFO_SET_FIXED_SIZE (&btree_obj->mvcc_info);

  /* Is there enough space in existing overflow pages? */
  error_code =
    btree_find_free_overflow_oids_page (thread_p, btid_int, &leaf_info->ovfl,
					&overflow_page);
  if (error_code != NO_ERROR)
    {
      assert (overflow_page == NULL);
      ASSERT_ERROR ();
      return error_code;
    }
  if (overflow_page == NULL)
    {
      /* Could not find free space for object. Create overflow page. */

      /* Notification */
      BTREE_SET_CREATED_OVERFLOW_PAGE_NOTIFICATION (thread_p, key,
						    &btree_obj->oid,
						    &btree_obj->class_oid,
						    btid_int->sys_btid);
      error_code =
	btree_key_append_object_as_new_overflow (thread_p, btid_int, leaf,
						 btree_obj, insert_helper,
						 search_key, leaf_record,
						 &leaf_info->ovfl);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }
  else
    {
      error_code =
	btree_key_append_object_to_overflow (thread_p, btid_int,
					     overflow_page, btree_obj,
					     insert_helper);
      pgbuf_unfix_and_init (thread_p, overflow_page);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }
  assert (overflow_page == NULL);

  /* Success. */
  return NO_ERROR;
}

/*
 * btree_key_insert_delete_mvccid () - BTREE_ADVANCE_WITH_KEY_FUNCTION used
 *				       for MVCC logical delete. An object
 *				       is found and an MVCCID is added to
 *				       its MVCC info.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree identifier.
 * key (in)	   : Key value.
 * leaf_page (in)  : Pointer to leaf node page.
 * search_key (in) : Search key result.
 * restart (in)	   : Not used.
 * other_args (in) : BTREE_INSERT_HELPER *
 */
static int
btree_key_insert_delete_mvccid (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
				DB_VALUE * key, PAGE_PTR * leaf_page,
				BTREE_SEARCH_KEY_HELPER * search_key,
				bool * restart, void *other_args)
{
#define BTREE_INSERT_DELID_REDO_CRUMBS_MAX 2
#define BTREE_INSERT_DELID_UNDO_CRUMBS_MAX 1

  BTREE_INSERT_HELPER *insert_helper = (BTREE_INSERT_HELPER *) other_args;
  int error_code = NO_ERROR;
  RECDES record;
  char data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  LEAF_REC leaf_info;
  int offset_after_key;
  int offset_to_found_object;
  BTREE_MVCC_INFO mvcc_info;
  bool dummy_clear_key;

  PAGE_PTR found_page = NULL;

  int num_visible = 0;
  MVCC_SNAPSHOT snapshot_dirty;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (leaf_page != NULL && *leaf_page != NULL);
  assert (search_key != NULL);
  assert (insert_helper != NULL);

  if (search_key->result != BTREE_KEY_FOUND)
    {
      /* Impossible. Object and key should exist in b-tree. */
      assert (false);
      btree_set_unknown_key_error (thread_p, btid_int->sys_btid, key,
				   "btree_key_insert_delete_mvccid");
      return ER_BTREE_UNKNOWN_KEY;
    }

  /* Prepare leaf record descriptor to read from b-tree. */
  record.type = REC_HOME;
  record.area_size = DB_PAGESIZE;
  record.data = PTR_ALIGN (data_buffer, BTREE_MAX_ALIGN);

  /* Get & read record. */
  if (spage_get_record (*leaf_page, search_key->slotid, &record, COPY)
      != S_SUCCESS)
    {
      /* Unexpected. */
      assert_release (false);
      return ER_FAILED;
    }
#if !defined (NDEBUG)
  /* Check valid record before changing it. */
  (void) btree_check_valid_record (thread_p, btid_int, &record,
				   BTREE_LEAF_NODE, key);
#endif
  error_code =
    btree_read_record (thread_p, btid_int, *leaf_page, &record, NULL,
		       &leaf_info, BTREE_LEAF_NODE, &dummy_clear_key,
		       &offset_after_key, PEEK_KEY_VALUE, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (insert_helper->is_unique_multi_update && !insert_helper->is_ha_enabled
      && BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;
      num_visible =
	btree_get_num_visible_from_leaf_and_ovf (thread_p, btid_int,
						 &record,
						 offset_after_key,
						 &leaf_info, NULL,
						 &snapshot_dirty);
      /* Even though multiple visible objects are allowed, they cannot exceed
       * two visible objects (insert does not allow it).
       * Also, there should be at least one object to delete.
       */
      assert (num_visible > 0);
      assert (num_visible <= 2);
      /* The key is considered deleted only if there is one visible object
       * (which will be deleted).
       */
      insert_helper->is_unique_key_added_or_deleted = (num_visible == 1);
    }

  error_code =
    btree_find_oid_and_its_page (thread_p, btid_int,
				 BTREE_INSERT_OID (insert_helper),
				 *leaf_page, insert_helper->purpose,
				 NULL, &record, &leaf_info,
				 offset_after_key, &found_page, NULL,
				 &offset_to_found_object, &mvcc_info);
  if (error_code != NO_ERROR)
    {
      /* Error. */
      ASSERT_ERROR ();
      assert (found_page == NULL);
      return error_code;
    }
  if (offset_to_found_object == NOT_FOUND)
    {
      assert (found_page == NULL);
      assert (false);
      btree_set_unknown_key_error (thread_p, btid_int->sys_btid, key,
				   "btree_key_insert_delete_mvccid");
      return ER_BTREE_UNKNOWN_OID;
    }
  /* Object was found. */
  if (found_page == *leaf_page)
    {
      /* Found in leaf page. */

      /* Unique index can only delete the first object.
       * Exception: If this is multi-row update, it is allowed to have more
       *            than one visible objects. Second visible object may be
       *            deleted later, thus preserving the unique constraint.
       */
      assert (!BTREE_IS_UNIQUE (btid_int->unique_pk)
	      || offset_to_found_object == 0
	      || insert_helper->op_type == MULTI_ROW_UPDATE);

      /* Object was found in leaf page and can be deleted. */
      assert (!BTREE_MVCC_INFO_IS_DELID_VALID (&mvcc_info));
      error_code =
	btree_insert_mvcc_delid_into_page (thread_p, btid_int, *leaf_page,
					   BTREE_LEAF_NODE, key,
					   insert_helper, search_key->slotid,
					   &record, offset_to_found_object);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

#if !defined (NDEBUG)
      if (oid_is_db_class (BTREE_INSERT_CLASS_OID (insert_helper)))
	{
	  /* Although the indexes on _db_class and _db_attribute are not
	   * unique, they cannot have duplicate OID's. Check here this
	   * consistency (no visible objects should exist).
	   */
	  btree_key_record_check_no_visible (thread_p, btid_int, *leaf_page,
					     search_key->slotid);
	}
#endif /* !NDEBUG */

      return error_code;
    }
  /* Found in overflow page. */

  /* Get overflow record. */
  if (spage_get_record (found_page, 1, &record, COPY) != S_SUCCESS)
    {
      assert_release (false);
      pgbuf_unfix_and_init (thread_p, found_page);
      return ER_FAILED;
    }
  /* Insert delete MVCCID. */
  error_code =
    btree_insert_mvcc_delid_into_page (thread_p, btid_int, found_page,
				       BTREE_OVERFLOW_NODE, key,
				       insert_helper, 1, &record,
				       offset_to_found_object);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, found_page);
      return error_code;
    }
  pgbuf_unfix_and_init (thread_p, found_page);

#if !defined (NDEBUG)
  if (oid_is_db_class (BTREE_INSERT_CLASS_OID (insert_helper)))
    {
      /* Although the indexes on _db_class and _db_attribute are not
       * unique, they cannot have duplicate OID's. Check here this
       * consistency (no visible objects should exist).
       */
      btree_key_record_check_no_visible (thread_p, btid_int, *leaf_page,
					 search_key->slotid);
    }
#endif /* !NDEBUG */

  return NO_ERROR;
}

#if !defined (NDEBUG)
/*
 * btree_key_record_check_no_visible () - Check b-tree record has no visible
 *					  objects. Debug only.
 *
 * thread_p (in)  : Thread entry.
 * btid_int (in)  : B-tree info.
 * leaf_page (in) : Leaf page.
 * slotid (in)	  : Record slot ID.
 */
static void
btree_key_record_check_no_visible (THREAD_ENTRY * thread_p,
				   BTID_INT * btid_int, PAGE_PTR leaf_page,
				   PGSLOTID slotid)
{
  RECDES record;
  LEAF_REC leaf_rec_info;
  int offset_after_key;
  bool dummy_clear_key;
  int num_visible;
  MVCC_SNAPSHOT dirty_snapshot;

  dirty_snapshot.snapshot_fnc = mvcc_satisfies_dirty;

  if (spage_get_record (leaf_page, slotid, &record, PEEK) != S_SUCCESS)
    {
      assert (false);
      return;
    }
  if (btree_read_record (thread_p, btid_int, leaf_page, &record, NULL,
			 &leaf_rec_info, BTREE_LEAF_NODE, &dummy_clear_key,
			 &offset_after_key, PEEK_KEY_VALUE, NULL) != NO_ERROR)
    {
      assert (false);
      return;
    }
  num_visible =
    btree_get_num_visible_from_leaf_and_ovf (thread_p, btid_int, &record,
					     offset_after_key,
					     &leaf_rec_info, NULL,
					     &dirty_snapshot);
  assert (num_visible == 0);
}
#endif /* !NDEBUG */

/*
 * btree_mvcc_info_from_heap_mvcc_header () - Convert an MVCC record header
 *					      (used for heap records) into
 *					      a b-tree MVCC info structure
 *					      (used to store an object in
 *					      b-tree).
 *
 * return	    : Void.
 * mvcc_header (in) : Heap record MVCC header.
 * mvcc_info (out)  : B-tree MVCC info.
 */
void
btree_mvcc_info_from_heap_mvcc_header (MVCC_REC_HEADER * mvcc_header,
				       BTREE_MVCC_INFO * mvcc_info)
{
  /* Assert expected arguments. */
  assert (mvcc_header != NULL);
  assert (mvcc_info != NULL);

  mvcc_info->flags = 0;
  if (MVCC_IS_FLAG_SET (mvcc_header, OR_MVCC_FLAG_VALID_INSID))
    {
      mvcc_info->flags |= BTREE_OID_HAS_MVCC_INSID;
      mvcc_info->insert_mvccid = MVCC_GET_INSID (mvcc_header);
    }
  else
    {
      mvcc_info->insert_mvccid = MVCCID_ALL_VISIBLE;
    }
  if (MVCC_IS_FLAG_SET (mvcc_header, OR_MVCC_FLAG_VALID_DELID))
    {
      mvcc_info->flags |= BTREE_OID_HAS_MVCC_DELID;
      mvcc_info->delete_mvccid = MVCC_GET_DELID (mvcc_header);
    }
  else
    {
      mvcc_info->delete_mvccid = MVCCID_NULL;
    }
}

/*
 * btree_mvcc_info_to_heap_mvcc_header () - Convert a b-tree MVCC info
 *					    structure into a heap record
 *					    MVCC header.
 *
 * return	     : Void.
 * mvcc_info (in)    : B-tree MVCC info.
 * mvcc_header (out) : Heap record MVCC header.
 */
void
btree_mvcc_info_to_heap_mvcc_header (BTREE_MVCC_INFO * mvcc_info,
				     MVCC_REC_HEADER * mvcc_header)
{
  /* Assert expected arguments. */
  assert (mvcc_header != NULL);
  assert (mvcc_info != NULL);

  mvcc_header->mvcc_flag = 0;
  if (BTREE_MVCC_INFO_IS_INSID_NOT_ALL_VISIBLE (mvcc_info))
    {
      mvcc_header->mvcc_flag |= OR_MVCC_FLAG_VALID_INSID;
      MVCC_SET_INSID (mvcc_header, mvcc_info->insert_mvccid);
    }
  else
    {
      MVCC_SET_INSID (mvcc_header, MVCCID_ALL_VISIBLE);
    }
  if (BTREE_MVCC_INFO_IS_DELID_VALID (mvcc_info))
    {
      mvcc_header->mvcc_flag |= OR_MVCC_FLAG_VALID_DELID;
      MVCC_SET_DELID (mvcc_header, mvcc_info->delete_mvccid);
    }
  else
    {
      MVCC_SET_DELID (mvcc_header, MVCCID_NULL);
    }
}

/*
 * btree_rv_redo_record_modify () - Redo recovery of b-tree key records.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 * rcv (in)	 : Recovery data.
 */
int
btree_rv_redo_record_modify (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  short flags;			/* Flags set into rcv->offset. */
  PGSLOTID slotid;		/* Slot ID stored in rcv->offset. */
  BTREE_NODE_HEADER *node_header = NULL;	/* Node header. */
  int key_cnt;			/* Node key count. */
  RECDES update_record;		/* Used to store modified record.
				 */
  /* Buffer to store modified record data. */
  char data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  char *rcv_data_ptr = NULL;	/* Current pointer in recovery data.
				 */
  BTID_INT btid_int_for_debug;	/* B-tree info structure used to store
				 * unique flag and top class OID.
				 */
  BTREE_NODE_TYPE node_type;	/* Node type. */
  int error_code = NO_ERROR;	/* Error code. */
  /* True when b-tree operations should be logged. For debugging. */
  bool log_btree_ops = prm_get_bool_value (PRM_ID_LOG_BTREE_OPS);
  bool has_debug_info = false;

  /* >>>>>>>>>>>> */
  BTREE_RV_DEBUG_ID rv_debug_id;	/* Debug ID to help developers find the
					 * source of bug in logging/recovery
					 * code.
					 */
  /* <<<<<<<<<<<< */

  /* Get flags and slot ID. */
  flags = rcv->offset & BTREE_RV_REDO_FLAGS_MASK;
  slotid = rcv->offset & (~BTREE_RV_REDO_FLAGS_MASK);

  /* There are four major cases here:
   * 1. LOG_RV_RECORD_DELETE: Key is removed completely.
   * 2. LOG_RV_RECORD_UPDATE_ALL: Entire record is updated (overflow header).
   * 2. LOG_RV_RECORD_INSERT: Key is inserted or overflow is created.
   * 4. LOG_RV_RECORD_UPDATE_PARTIAL: Record is updated by bits.
   */

  /* Case 1: Is key being removed completely? */
  /* Logged by: btree_delete_key_from_leaf */
  if (LOG_RV_RECORD_IS_DELETE (flags))
    {
      /* Record is completely removed from page. Redo of key removal from leaf
       * page, when all its objects have been deleted.
       */
      assert (!BTREE_RV_REDO_HAS_DEBUG_INFO (flags));

      /* Delete key record. */
      node_header = btree_get_node_header (rcv->pgptr);
      if (node_header == NULL)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      assert (slotid > 0);
      if (spage_delete (thread_p, rcv->pgptr, slotid) != slotid)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      /* Update the page header */
      key_cnt = btree_node_number_of_keys (rcv->pgptr);
      if (key_cnt == 0)
	{
	  node_header->max_key_len = 0;
	}
      pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

      if (log_btree_ops)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_REDO: remove slotid=%d from "
			 "leaf page %d|%d, lsa=%lld|%d, "
			 "in an unknown index.\n",
			 slotid, pgbuf_get_volume_id (rcv->pgptr),
			 pgbuf_get_page_id (rcv->pgptr),
			 (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
			 (int) pgbuf_get_lsa (rcv->pgptr)->offset);
	}
      return NO_ERROR;
    }
  /* Case 1 is ruled out. Cases 2, 3, 4.
   * These cases may also require unpacking debug info and running sanity
   * checks after modifying record.
   */

  /* First get debug info if any. */
  rcv_data_ptr = (char *) rcv->data;

  /* First check if there is debug info stored. */
  if (BTREE_RV_REDO_HAS_DEBUG_INFO (flags))
    {
      has_debug_info = true;

      /* Get BTREE_RV_DEBUG_ID */
      rv_debug_id = OR_GET_INT (rcv_data_ptr);
      rcv_data_ptr += OR_INT_SIZE;

      /* Read unique_pk flag. */
      btid_int_for_debug.unique_pk = OR_GET_INT (rcv_data_ptr);
      rcv_data_ptr += OR_INT_SIZE;

      /* Set top class OID. */
      if (BTREE_IS_UNIQUE (btid_int_for_debug.unique_pk))
	{
	  OR_GET_OID (rcv_data_ptr, &btid_int_for_debug.topclass_oid);
	  rcv_data_ptr += OR_OID_SIZE;
	}
      else
	{
	  OID_SET_NULL (&btid_int_for_debug.topclass_oid);
	}

      /* Read key type. */
      ASSERT_ALIGN (rcv_data_ptr, INT_ALIGNMENT);
      rcv_data_ptr =
	or_unpack_domain (rcv_data_ptr, &btid_int_for_debug.key_type, NULL);
      rcv_data_ptr = PTR_ALIGN (rcv_data_ptr, INT_ALIGNMENT);
    }

  /* Get node type. */
  if (flags & BTREE_RV_REDO_OVERFLOW_FLAG)
    {
      node_type = BTREE_OVERFLOW_NODE;
    }
  else
    {
      node_type = BTREE_LEAF_NODE;
    }

  /* Case 2: Is entire record being updated? */
  /* Logged by:
   * btree_modify_overflow_link.
   * btree_overflow_record_replace_object.
   */
  if (LOG_RV_RECORD_IS_UPDATE_ALL (flags))
    {
      /* Update entire record. */
      /* The remaining recovery data is updated record data. */
      update_record.data = (char *) rcv_data_ptr;
      update_record.length =
	rcv->length - CAST_BUFLEN (rcv_data_ptr - rcv->data);
      update_record.type = REC_HOME;

#if !defined (NDEBUG)
      if (has_debug_info)
	{
	  (void) btree_check_valid_record (thread_p, &btid_int_for_debug,
					   &update_record, node_type, NULL);
	}
#endif /* NDEBUG */

      /* Update record in page. */
      if (spage_update (thread_p, rcv->pgptr, slotid, &update_record)
	  != SP_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

      if (log_btree_ops)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_REDO: update slotid=%d from "
			 "page %d|%d, lsa=%lld|%d in an unknown index.\n",
			 slotid, pgbuf_get_volume_id (rcv->pgptr),
			 pgbuf_get_page_id (rcv->pgptr),
			 (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
			 (int) pgbuf_get_lsa (rcv->pgptr)->offset);
	}
      return NO_ERROR;
    }
  /* Case 2 is ruled out. Cases 3 and 4.
   */

  /* Case 3:
   * 1. New overflow page. Logged by: btree_start_overflow_page.
   * 2. New key in leaf record. Logged by: btree_key_insert_new_key.
   */
  if (LOG_RV_RECORD_IS_INSERT (flags))
    {
      if (node_type == BTREE_OVERFLOW_NODE)
	{
	  /* Actually two records are inserted: overflow header and record
	   * containing one object.
	   */
	  BTREE_OVERFLOW_HEADER overflow_header;

	  /* Insert overflow header. */
	  OR_GET_VPID (rcv_data_ptr, &overflow_header.next_vpid);
	  rcv_data_ptr += DISK_VPID_ALIGNED_SIZE;

	  update_record.type = REC_HOME;
	  update_record.data = (char *) &overflow_header;
	  update_record.length = sizeof (overflow_header);
	  if (spage_insert_at (thread_p, rcv->pgptr, HEADER, &update_record)
	      != SP_SUCCESS)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }

	  /* Insert object record. */
	  update_record.data = rcv_data_ptr;
	  update_record.length =
	    rcv->length - CAST_BUFLEN (rcv_data_ptr - rcv->data);

#if !defined (NDEBUG)
	  if (has_debug_info)
	    {
	      assert (update_record.length
		      == BTREE_OBJECT_FIXED_SIZE (&btid_int_for_debug));
	      (void) btree_check_valid_record (thread_p, &btid_int_for_debug,
					       &update_record,
					       BTREE_OVERFLOW_NODE, NULL);
	    }
#endif /* !NDEDBUG */

	  if (spage_insert_at (thread_p, rcv->pgptr, 1, &update_record)
	      != SP_SUCCESS)
	    {
	      assert_release (false);
	      (void) spage_delete (thread_p, rcv->pgptr, HEADER);
	      return ER_FAILED;
	    }
	  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

	  if (log_btree_ops)
	    {
	      if (has_debug_info)
		{
		  OID oid = OID_INITIALIZER;
		  OID class_oid = OID_INITIALIZER;
		  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;

		  btree_unpack_object (update_record.data,
				       &btid_int_for_debug, node_type,
				       &update_record, 0, &oid, &class_oid,
				       &mvcc_info);
		  _er_log_debug (ARG_FILE_LINE,
				 "BTREE_REDO: create new overflow "
				 "page %d|%d, lsa=%lld|%d, "
				 "in an unknown index. "
				 "Insert object=%d|%d|%d, "
				 "class_oid=%d|%d|%d, mvcc_info=%llu|%llu.\n",
				 pgbuf_get_volume_id (rcv->pgptr),
				 pgbuf_get_page_id (rcv->pgptr),
				 (long long int)
				 pgbuf_get_lsa (rcv->pgptr)->pageid,
				 (int) pgbuf_get_lsa (rcv->pgptr)->offset,
				 oid.volid, oid.pageid, oid.slotid,
				 class_oid.volid, class_oid.pageid,
				 class_oid.slotid,
				 (unsigned long long int)
				 mvcc_info.insert_mvccid,
				 (unsigned long long int)
				 mvcc_info.delete_mvccid);
		}
	      else
		{
		  _er_log_debug (ARG_FILE_LINE,
				 "BTREE_REDO: create new overflow "
				 "page %d|%d, lsa=%lld|%d, "
				 "in an unknown index.\n",
				 pgbuf_get_volume_id (rcv->pgptr),
				 pgbuf_get_page_id (rcv->pgptr),
				 (long long int)
				 pgbuf_get_lsa (rcv->pgptr)->pageid,
				 (int) pgbuf_get_lsa (rcv->pgptr)->offset);
		}
	    }
	}
      else
	{
	  /* Insert key into leaf page. */
	  int key_length;
	  int key_count;
	  BTREE_NODE_HEADER *node_header = NULL;

	  node_header = btree_get_node_header (rcv->pgptr);
	  if (node_header == NULL)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }

	  if (BTREE_RV_REDO_IS_UPDATE_MAX_KEY_LEN (flags))
	    {
	      rcv_data_ptr = or_unpack_int (rcv_data_ptr, &key_length);
	    }

	  update_record.type = REC_HOME;
	  update_record.data = rcv_data_ptr;
	  update_record.length =
	    rcv->length - CAST_BUFLEN (rcv_data_ptr - rcv->data);

#if !defined (NDEBUG)
	  if (has_debug_info)
	    {
	      (void) btree_check_valid_record (thread_p, &btid_int_for_debug,
					       &update_record, node_type,
					       NULL);
	    }
#endif /* !NDEBUG */

	  if (spage_insert_at (thread_p, rcv->pgptr, slotid, &update_record)
	      != SP_SUCCESS)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }

	  if (BTREE_RV_REDO_IS_UPDATE_MAX_KEY_LEN (flags))
	    {
	      assert (key_length > node_header->max_key_len);
	      node_header->max_key_len = key_length;
	    }

	  key_count = btree_node_number_of_keys (rcv->pgptr);

	  assert (node_header->split_info.pivot >= 0 && key_count > 0);
	  btree_split_next_pivot (&node_header->split_info,
				  (float) slotid / key_count, key_count);

	  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

	  if (log_btree_ops)
	    {
	      if (has_debug_info)
		{
		  OID oid = OID_INITIALIZER;
		  OID class_oid = OID_INITIALIZER;
		  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;
		  LEAF_REC leaf_rec_info;
		  int offset_after_key = 0;
		  DB_VALUE key;
		  bool clear_key;
		  char *printed_key = NULL;

		  DB_MAKE_NULL (&key);
		  (void) btree_read_record (thread_p, &btid_int_for_debug,
					    rcv->pgptr, &update_record,
					    &key, &leaf_rec_info, node_type,
					    &clear_key, &offset_after_key,
					    PEEK_KEY_VALUE, NULL);
		  printed_key = pr_valstring (&key);
		  btree_clear_key_value (&clear_key, &key);

		  (void) btree_unpack_object (update_record.data,
					      &btid_int_for_debug, node_type,
					      &update_record,
					      offset_after_key,
					      &oid, &class_oid, &mvcc_info);
		  _er_log_debug (ARG_FILE_LINE,
				 "BTREE_REDO: insert slotid=%d in "
				 "leaf page %d|%d, lsa=%lld|%d, "
				 "in an unknown index. "
				 "Object=%d|%d|%d, class_oid=%d|%d|%d, "
				 "mvcc_info=%lld|%lld, key=%s.\n",
				 slotid, pgbuf_get_volume_id (rcv->pgptr),
				 pgbuf_get_page_id (rcv->pgptr),
				 (long long int)
				 pgbuf_get_lsa (rcv->pgptr)->pageid,
				 (int) pgbuf_get_lsa (rcv->pgptr)->offset,
				 oid.volid, oid.pageid, oid.slotid,
				 class_oid.volid, class_oid.pageid,
				 class_oid.slotid,
				 (unsigned long long int)
				 mvcc_info.insert_mvccid,
				 (unsigned long long int)
				 mvcc_info.delete_mvccid,
				 printed_key != NULL ?
				 printed_key : "unknown");
		  if (printed_key != NULL)
		    {
		      free_and_init (printed_key);
		    }
		}
	      else
		{
		  _er_log_debug (ARG_FILE_LINE,
				 "BTREE_REDO: insert slotid=%d in "
				 "leaf page %d|%d, lsa=%lld|%d, "
				 "in an unknown index.\n",
				 slotid, pgbuf_get_volume_id (rcv->pgptr),
				 pgbuf_get_page_id (rcv->pgptr),
				 (long long int)
				 pgbuf_get_lsa (rcv->pgptr)->pageid,
				 (int) pgbuf_get_lsa (rcv->pgptr)->offset);
		}
	    }
	}
      return NO_ERROR;
    }
  /* Case 4: Update record by parts.
   * All other b-tree record changes.
   * Logged by:
   * btree_insert_mvcc_delid_into_page
   * btree_key_append_object_as_new_overflow
   * btree_key_append_object_to_overflow
   * btree_key_append_object_unique
   * btree_key_append_object_non_unique
   * btree_key_remove_insert_mvccid
   * btree_key_remove_delete_mvccid_unique
   * btree_key_remove_delete_mvccid_non_unique
   * btree_overflow_record_replace_object
   * btree_swap_first_oid_with_ovfl_rec
   * btree_modify_overflow_link
   * btree_leaf_record_replace_first_with_last
   * btree_record_remove_object
   */
  assert (LOG_RV_RECORD_IS_UPDATE_PARTIAL (flags));

  /* Check there is at least one change logged. */
  assert (CAST_BUFLEN (rcv_data_ptr - rcv->data) < rcv->length);

  /* Get existing record. */
  update_record.data = PTR_ALIGN (data_buffer, BTREE_MAX_ALIGN);
  update_record.area_size = DB_PAGESIZE;
  if (spage_get_record (rcv->pgptr, slotid, &update_record, COPY)
      != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

#if !defined (NDEBUG)
  if (has_debug_info)
    {
      /* Check existing record is valid. */
      (void) btree_check_valid_record (thread_p, &btid_int_for_debug,
				       &update_record, node_type, NULL);
    }
#endif /* !NDEBUG */

  /* Apply changes. */
  error_code =
    log_rv_redo_record_partial_changes
    (thread_p, rcv_data_ptr,
     rcv->length - CAST_BUFLEN (rcv_data_ptr - rcv->data), &update_record);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  /* Changes applied successfully. */

#if !defined (NDEBUG)
  if (has_debug_info)
    {
      /* Check record validity after changes. */
      (void) btree_check_valid_record (thread_p, &btid_int_for_debug,
				       &update_record, node_type, NULL);
    }
#endif /* !NDEBUG */

  /* Update in page. */
  if (spage_update (thread_p, rcv->pgptr, slotid, &update_record)
      != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  if (log_btree_ops)
    {
      if (has_debug_info)
	{
	  LEAF_REC leaf_rec_info;
	  int offset_after_key = 0;
	  DB_VALUE key;
	  bool clear_key;
	  char *printed_key = NULL;

	  /* Read key value if node is leaf and if key is not overflow
	   * (avoid fixing other pages here since it may crash).
	   */
	  if (node_type == BTREE_LEAF_NODE
	      && !btree_leaf_is_flaged (&update_record,
					BTREE_LEAF_RECORD_OVERFLOW_KEY))
	    {
	      DB_MAKE_NULL (&key);
	      (void) btree_read_record (thread_p, &btid_int_for_debug,
					rcv->pgptr, &update_record, &key,
					&leaf_rec_info, node_type, &clear_key,
					&offset_after_key, PEEK_KEY_VALUE,
					NULL);
	      printed_key = pr_valstring (&key);
	      btree_clear_key_value (&clear_key, &key);
	    }

	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_REDO: update slotid=%d from "
			 "%s page %d|%d, lsa=%lld|%d, in an unknown index."
			 "key=%s, rv_debug_id=%d.\n",
			 slotid,
			 node_type == BTREE_LEAF_NODE ? "leaf" : "overflow",
			 pgbuf_get_volume_id (rcv->pgptr),
			 pgbuf_get_page_id (rcv->pgptr),
			 (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
			 (int) pgbuf_get_lsa (rcv->pgptr)->offset,
			 printed_key != NULL ? printed_key : "unknown",
			 rv_debug_id);
	  if (printed_key != NULL)
	    {
	      free_and_init (printed_key);
	    }
	}
      else
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_REDO: update slotid=%d from "
			 "%s page %d|%d, lsa=%lld|%d, in an unknown index.\n",
			 slotid,
			 node_type == BTREE_LEAF_NODE ? "leaf" : "overflow",
			 pgbuf_get_volume_id (rcv->pgptr),
			 pgbuf_get_page_id (rcv->pgptr),
			 (long long int) pgbuf_get_lsa (rcv->pgptr)->pageid,
			 (int) pgbuf_get_lsa (rcv->pgptr)->offset);
	}
    }
  return NO_ERROR;
}

/*
 * btree_rv_undo_delete_index () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Restores unique statistics in global hash
 */
int
btree_rv_undo_delete_index (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  char *datap;
  BTID btid;
  int ret = NO_ERROR;
  int num_nulls, num_oids, num_keys, unique_pk;

  assert (recv->length >= OR_INT_SIZE + OR_BTID_ALIGNED_SIZE);

  /* unpack the index btid */
  datap = (char *) recv->data;

  OR_GET_BTID (datap, &btid);
  datap += OR_BTID_ALIGNED_SIZE;

  unique_pk = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  if (unique_pk)
    {
      num_nulls = OR_GET_INT (datap);
      datap += OR_INT_SIZE;

      num_oids = OR_GET_INT (datap);
      datap += OR_INT_SIZE;

      num_keys = OR_GET_INT (datap);
      datap += OR_INT_SIZE;

      ret =
	logtb_update_global_unique_stats_by_abs (thread_p, &btid, num_oids,
						 num_nulls, num_keys, false);
      if (ret != NO_ERROR)
	{
	  goto error;
	}
    }

  return NO_ERROR;

error:
  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_rv_redo_delete_index () -
 *   return: int
 *   recv(in): Recovery structure
 *
 * Note: Remove unique statistics from global hash
 */
int
btree_rv_redo_delete_index (THREAD_ENTRY * thread_p, LOG_RCV * recv)
{
  char *datap;
  BTID btid;
  int ret = NO_ERROR, unique_pk;

  assert (recv->length >= OR_INT_SIZE + OR_BTID_ALIGNED_SIZE);

  /* unpack the index btid */
  datap = (char *) recv->data;

  OR_GET_BTID (datap, &btid);
  datap += OR_BTID_ALIGNED_SIZE;

  unique_pk = OR_GET_INT (datap);
  datap += OR_INT_SIZE;

  if (unique_pk)
    {
      ret = logtb_delete_global_unique_stats (thread_p, &btid);
      if (ret != NO_ERROR)
	{
	  goto error;
	}
    }

  return NO_ERROR;

error:
  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * btree_physical_delete () - Physically delete an object (unlike MVCC delete,
 *			      object and all its info are removed).
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * btid (in)		 : B-tree ID.
 * key (in)		 : Key value.
 * oid (in)		 : Object OID.
 * class_oid (in)	 : Object class OID.
 * unique (in)		 : Output if index is unique.
 * op_type (in)		 : Operation type.
 * unique_stat_info (in) : Unique statistics information.
 */
int
btree_physical_delete (THREAD_ENTRY * thread_p, BTID * btid, DB_VALUE * key,
		       OID * oid, OID * class_oid, int *unique, int op_type,
		       BTREE_UNIQUE_STATS * unique_stat_info)
{
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Start physical delete object %d|%d|%d, "
		     "class_oid %d|%d|%d in index (%d, %d|%d).\n",
		     oid->volid, oid->pageid, oid->slotid,
		     class_oid->volid, class_oid->pageid, class_oid->slotid,
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid);
    }

  return btree_delete_internal (thread_p, btid, oid, class_oid, &mvcc_info,
				key, NULL, unique, op_type, unique_stat_info,
				MVCCID_NULL, BTREE_OP_DELETE_OBJECT_PHYSICAL);
}

/*
 * btree_vacuum_insert_mvccid () - Vacuum the insert MVCCID of object.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * btid (in)	      : B-tree ID.
 * buffered_key (in)  : Key value.
 * oid (in)	      : Object OID.
 * class_oid (in)     : Object class OID.
 * insert_mvccid (in) : Insert MVCCID of object.
 */
int
btree_vacuum_insert_mvccid (THREAD_ENTRY * thread_p, BTID * btid,
			    OR_BUF * buffered_key, OID * oid, OID * class_oid,
			    MVCCID insert_mvccid)
{
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Start vacuum insert MVCCID %lld from "
		     "object %d|%d|%d, class_oid %d|%d|%d in "
		     "index (%d, %d|%d).\n",
		     (long long int) insert_mvccid,
		     oid->volid, oid->pageid, oid->slotid,
		     class_oid->volid, class_oid->pageid, class_oid->slotid,
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid);
    }

  return btree_delete_internal (thread_p, btid, oid, class_oid, &mvcc_info,
				NULL, buffered_key, NULL, SINGLE_ROW_MODIFY,
				NULL, insert_mvccid,
				BTREE_OP_DELETE_VACUUM_INSID);
}

/*
 * btree_vacuum_object () - Vacuum (remove) deleted object and all its info
 *			    from b-tree key.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * btid (in)	      : B-tree ID.
 * buffered_key (in)  : Key value.
 * oid (in)	      : Object OID.
 * class_oid (in)     : Object class OID.
 * delete_mvccid (in) : Delete MVCCID of object.
 */
int
btree_vacuum_object (THREAD_ENTRY * thread_p, BTID * btid,
		     OR_BUF * buffered_key, OID * oid, OID * class_oid,
		     MVCCID delete_mvccid)
{
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Start vacuum object %d|%d|%d, "
		     "class_oid %d|%d|%d and delete MVCCID %lld in "
		     "index (%d, %d|%d).\n",
		     oid->volid, oid->pageid, oid->slotid,
		     class_oid->volid, class_oid->pageid, class_oid->slotid,
		     (long long int) delete_mvccid,
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid);
    }

  return btree_delete_internal (thread_p, btid, oid, class_oid, &mvcc_info,
				NULL, buffered_key, NULL, SINGLE_ROW_MODIFY,
				NULL, delete_mvccid,
				BTREE_OP_DELETE_VACUUM_OBJECT);
}

/*
 * btree_undo_mvcc_delete () - Undo MVCC delete (undo the insert of delete
 *			       MVCCID).
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * btid (in)	      : B-tree ID.
 * buffered_key (in)  : Key value.
 * oid (in)	      : Object OID.
 * class_oid (in)     : Object class OID.
 * delete_mvccid (in) : The delete MVCCID of object.
 */
static int
btree_undo_mvcc_delete (THREAD_ENTRY * thread_p, BTID * btid,
			OR_BUF * buffered_key, OID * oid, OID * class_oid,
			MVCCID delete_mvccid)
{
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Start undo MVCC delete on "
		     "object %d|%d|%d, class_oid %d|%d|%d and "
		     "delete MVCCID %lld in index (%d, %d|%d).\n",
		     oid->volid, oid->pageid, oid->slotid,
		     class_oid->volid, class_oid->pageid, class_oid->slotid,
		     (long long int) delete_mvccid,
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid);
    }

  return btree_delete_internal (thread_p, btid, oid, class_oid, &mvcc_info,
				NULL, buffered_key, NULL, SINGLE_ROW_MODIFY,
				NULL, delete_mvccid,
				BTREE_OP_DELETE_UNDO_INSERT_DELID);
}

/*
 * btree_undo_insert_object () - Delete object from index as part of an undo
 *				 of insert object operation.
 *
 * return	      : Error code.
 * thread_p (in)      : Thread entry.
 * btid (in)	      : B-tree ID.
 * buffered_key (in)  : Key value.
 * oid (in)	      : Object OID.
 * class_oid (in)     : Object class OID.
 * insert_mvccid (in) : Insert MVCCID.
 */
static int
btree_undo_insert_object (THREAD_ENTRY * thread_p, BTID * btid,
			  OR_BUF * buffered_key, OID * oid, OID * class_oid,
			  MVCCID insert_mvccid)
{
  BTREE_MVCC_INFO mvcc_info = BTREE_MVCC_INFO_INITIALIZER;

  if (prm_get_bool_value (PRM_ID_LOG_BTREE_OPS))
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Start undo insert object %d|%d|%d, "
		     "class_oid %d|%d|%d and insert MVCCID %lld "
		     "in index (%d, %d|%d).\n",
		     oid->volid, oid->pageid, oid->slotid,
		     class_oid->volid, class_oid->pageid, class_oid->slotid,
		     (long long int) insert_mvccid,
		     btid->root_pageid, btid->vfid.volid, btid->vfid.fileid);
    }

  return btree_delete_internal (thread_p, btid, oid, class_oid, &mvcc_info,
				NULL, buffered_key, NULL, SINGLE_ROW_MODIFY,
				NULL, insert_mvccid,
				BTREE_OP_DELETE_UNDO_INSERT);
}

/*
 * btree_delete_internal () - Index internal function to delete data from a
 *			      b-tree key.
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * btid (in)		 : B-tree ID.
 * oid (in)		 : Object OID.
 * class_oid (in)	 : Object class OID.
 * mvcc_info (in)	 : Object MVCC info.
 * key (in)		 : Object key value.
 * buffered_key (in)	 : Buffered value of key. Must be unpacked.
 * unique (out)		 : Output if index is unique.
 * op_type (in)		 : Operation type.
 * unique_stat_info (in) : Unique statistics collector.
 * match_mvccid (in)	 : MVCCID to be matched by either insert or delete
 *			   MVCCID.
 * purpose (in)		 : Purpose/context for function call.
 */
static int
btree_delete_internal (THREAD_ENTRY * thread_p, BTID * btid, OID * oid,
		       OID * class_oid, BTREE_MVCC_INFO * mvcc_info,
		       DB_VALUE * key, OR_BUF * buffered_key, int *unique,
		       int op_type, BTREE_UNIQUE_STATS * unique_stat_info,
		       MVCCID match_mvccid, BTREE_OP_PURPOSE purpose)
{
  /* Structure used by internal functions. */
  BTREE_DELETE_HELPER delete_helper = BTREE_DELETE_HELPER_INITIALIZER;
  BTID_INT btree_info;		/* B-tree info. */
  int error_code = NO_ERROR;	/* Error code. */
  bool old_check_interrupt = false;	/* Save check interrupt before
					 * setting it to false.
					 */
  BTREE_PROCESS_KEY_FUNCTION *key_func = NULL;	/* Internal function called to
						 * manipulate key.
						 */
  DB_VALUE local_key;		/* Local storage for DB_VALUE if key is
				 * buffered.
				 */

  /* Assert expected arguments. */
  assert (btid != NULL && !BTREE_INVALID_INDEX_ID (btid));
  assert ((key == NULL && buffered_key != NULL)
	  || (key != NULL && buffered_key == NULL));
  assert (oid != NULL);
  assert (file_is_new_file (thread_p, &btid->vfid) == FILE_OLD_FILE);
  assert (op_type == SINGLE_ROW_DELETE || op_type == MULTI_ROW_DELETE
	  || op_type == SINGLE_ROW_UPDATE || op_type == MULTI_ROW_UPDATE
	  || op_type == SINGLE_ROW_MODIFY);

  /* Choose internal function based on purpose. */
  switch (purpose)
    {
    case BTREE_OP_DELETE_OBJECT_PHYSICAL:
    case BTREE_OP_DELETE_UNDO_INSERT:
    case BTREE_OP_DELETE_VACUUM_OBJECT:
      key_func = btree_key_delete_remove_object;
      break;
    case BTREE_OP_DELETE_VACUUM_INSID:
      key_func = btree_key_remove_insert_mvccid;
      break;
    case BTREE_OP_DELETE_UNDO_INSERT_DELID:
      key_func = btree_key_remove_delete_mvccid;
      break;
    default:
      /* Unhandled or unexpected. */
      assert_release (false);
      return ER_FAILED;
    }

  /* Initialize delete helper. */
  COPY_OID (BTREE_DELETE_OID (&delete_helper), oid);
  COPY_OID (BTREE_DELETE_CLASS_OID (&delete_helper), class_oid);
  *BTREE_DELETE_MVCC_INFO (&delete_helper) = *mvcc_info;
  delete_helper.purpose = purpose;

  /* Set operation type. */
  delete_helper.op_type = op_type;

  /* Set unique stats information. */
  delete_helper.unique_stats_info = unique_stat_info;

  /* Set MVCCID to be matched. */
  delete_helper.match_mvccid = match_mvccid;

  /* Is key buffered? */
  if (buffered_key != NULL)
    {
      /* Key will be unpacked after fixing root and getting key type. */
      delete_helper.buffered_key = buffered_key;
      key = &local_key;
      DB_MAKE_NULL (key);
    }

  /* Log b-tree operations? For debugging. */
  delete_helper.log_operations = prm_get_bool_value (PRM_ID_LOG_BTREE_OPS);

  old_check_interrupt = thread_set_check_interrupt (thread_p, false);
  FI_SET (thread_p, FI_TEST_BTREE_MANAGER_PAGE_DEALLOC_FAIL, 1);

  error_code =
    btree_search_key_and_apply_functions (thread_p, btid, &btree_info,
					  key, btree_fix_root_for_delete,
					  &delete_helper,
					  btree_merge_node_and_advance,
					  &delete_helper,
					  key_func, &delete_helper, NULL,
					  NULL);

  (void) thread_set_check_interrupt (thread_p, old_check_interrupt);
  FI_RESET (thread_p, FI_TEST_BTREE_MANAGER_PAGE_DEALLOC_FAIL);

  if (delete_helper.printed_key != NULL)
    {
      free_and_init (delete_helper.printed_key);
    }

  if (buffered_key != NULL)
    {
      assert (key != NULL);
      pr_clear_value (key);
    }

  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();

      if (delete_helper.log_operations)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_DELETE: Failed delete data operation on "
			 "object %d|%d|%d, class_oid %d|%d|%d in "
			 "index (%d, %d|%d).\n",
			 oid->volid, oid->pageid, oid->slotid,
			 class_oid->volid, class_oid->pageid,
			 class_oid->slotid, btid->root_pageid,
			 btid->vfid.volid, btid->vfid.fileid);
	}
      return error_code;
    }

  if (unique != NULL)
    {
      *unique = BTREE_IS_UNIQUE (btree_info.unique_pk);
    }

  if (delete_helper.check_key_deleted && !delete_helper.is_key_deleted)
    {
      /* Correct unique stats info (key is not actually deleted). */
      assert (delete_helper.unique_stats_info != NULL);
      delete_helper.unique_stats_info->num_keys++;
    }

  return NO_ERROR;
}

/*
 * btree_fix_root_for_delete () - BTREE_ROOT_WITH_KEY_FUNCTION - fix root page
 *				  before deleting data from a key.
 *
 * return	       : Error code.
 * thread_p (in)       : Thread entry.
 * btid (in)	       : B-tree ID.
 * btid_int (in/out)   : Can output b-tree info.
 * key (in)	       : Not used.
 * root_page (out)     : Fixed root node page.
 * is_leaf (in)	       : Not used.
 * search_key (in)     : Not used.
 * stop (out)	       : Outputs to stop when deleting a NULL key.
 * restart (out)       : Not used.
 * other_args (in/out) : BTREE_DELETE_HELPER *
 */
static int
btree_fix_root_for_delete (THREAD_ENTRY * thread_p, BTID * btid,
			   BTID_INT * btid_int, DB_VALUE * key,
			   PAGE_PTR * root_page, bool * is_leaf,
			   BTREE_SEARCH_KEY_HELPER * search_key,
			   bool * stop, bool * restart, void *other_args)
{
  /* Structure used for internal functions used in btree_delete_internal. */
  BTREE_DELETE_HELPER *delete_helper = (BTREE_DELETE_HELPER *) other_args;
  int error_code = NO_ERROR;	/* Error code. */
  int increment_nulls;		/* Increment value of number of NULL's. */
  int increment_keys;		/* Increment value of number of keys. */
  int increment_oids;		/* Increment value of number of OID's. */
  bool is_null = false;		/* Is key null. */

  /* Assert expected arguments. */
  assert (btid != NULL);
  assert (btid_int != NULL);
  assert (root_page != NULL && *root_page == NULL);
  assert (delete_helper != NULL);
  assert (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT_DELID
	  || delete_helper->purpose == BTREE_OP_DELETE_VACUUM_INSID
	  || delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT);

  /* Root node is being fixed. */
  delete_helper->is_root = true;
  if (delete_helper->is_first_search)
    {
      /* First search: read b-tree info. */
      *root_page =
	btree_fix_root_with_info (thread_p, btid,
				  delete_helper->nonleaf_latch_mode, NULL,
				  NULL, btid_int);
      if (*root_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
    }
  else
    {
      /* Just fix root page. */
      *root_page =
	btree_fix_root_with_info (thread_p, btid,
				  delete_helper->nonleaf_latch_mode,
				  NULL, NULL, NULL);
      if (*root_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      return NO_ERROR;
    }
  /* Root page fixed. */
  /* This is first search. */
  /* Reset first search flag. We don't want to repeat next operations. */
  delete_helper->is_first_search = false;

  /* If buffered key is not NULL, key value must be unpacked. */
  if (delete_helper->buffered_key != NULL)
    {
      /* Unpack key from buffer. */
      PR_TYPE *pr_type;
      int key_size = -1;

      /* Assert key is initialized. */
      assert (DB_IS_NULL (key));

      pr_type = btid_int->key_type->type;

      /* Do not copy the string--just use the pointer.  The pr_ routines
       * for strings and sets have different semantics for length.
       */
      if (pr_type->id == DB_TYPE_MIDXKEY)
	{
	  key_size =
	    CAST_BUFLEN (delete_helper->buffered_key->endptr
			 - delete_helper->buffered_key->ptr);
	}

      /* Read key. */
      error_code =
	(*(pr_type->index_readval)) (delete_helper->buffered_key, key,
				     btid_int->key_type, key_size,
				     false /* not copy */ , NULL, 0);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  pgbuf_unfix_and_init (thread_p, *root_page);
	  return error_code;
	}
    }

  /* Safe guard: key type matches. */
  assert (BTREE_IS_SAME_DB_TYPE (DB_VALUE_DOMAIN_TYPE (key),
				 btid_int->key_type->type->id));

  if (key != NULL && DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      /* Set complete set domain. */
      key->data.midxkey.domain = btid_int->key_type;
    }

  /* Is key NULL? */
  is_null =
    key == NULL || DB_IS_NULL (key) || btree_multicol_key_is_null (key);

  if (delete_helper->log_operations)
    {
      /* Key must be printed. */
      delete_helper->printed_key = pr_valstring (key);
    }

  /* Safe guard: key cannot always be NULL. */
  assert (!is_null
	  || delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL);

  if (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_INSID
      || delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT)
    {
      /* Vacuum operations don't need to go further. */
      return NO_ERROR;
    }
  if (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT
      || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT_DELID)
    {
      if (BTREE_IS_UNIQUE (btid_int->unique_pk))
	{
	  /* Class OID is not packed for undo when it matches topclass_oid.
	   * If it is NULL here, then it must be replaced with topclass_oid.
	   */
	  if (OID_ISNULL (BTREE_DELETE_CLASS_OID (delete_helper)))
	    {
	      COPY_OID (BTREE_DELETE_CLASS_OID (delete_helper),
			&btid_int->topclass_oid);
	    }
	}
      /* Undo operations don't need to go further. */
      return NO_ERROR;
    }
  /* Not BTREE_OP_DELETE_UNDO_INSERT or BTREE_OP_DELETE_UNDO_INSERT_DELID. */

  assert (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL);

  /* Update unique statistics. */
  if (BTREE_IS_UNIQUE (btid_int->unique_pk)
      && delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL)
    {
      /* Do not update statistics when vacuuming or during undo recovery. */
      if (is_null)
	{
	  increment_keys = 0;
	  increment_nulls = -1;
	  increment_oids = -1;
	}
      else
	{
	  increment_keys = -1;
	  increment_nulls = 0;
	  increment_oids = -1;
	}
      if (BTREE_IS_MULTI_ROW_OP (delete_helper->op_type))
	{
	  /* Collect statistics */
	  assert (delete_helper->unique_stats_info != NULL);
	  delete_helper->unique_stats_info->num_keys += increment_keys;
	  delete_helper->unique_stats_info->num_oids += increment_oids;
	  delete_helper->unique_stats_info->num_nulls += increment_nulls;

	  if (delete_helper->op_type == MULTI_ROW_UPDATE)
	    {
	      /* It is possible that after deleting key, it isn't actually
	       * deleted. We must count visible objects and correct above
	       * statistics.
	       */
	      delete_helper->check_key_deleted = true;
	      delete_helper->is_key_deleted = true;
	    }
	}
      else
	{
	  /* Save and log statistics changes. */
	  error_code =
	    logtb_tran_update_unique_stats (thread_p, btid, increment_keys,
					    increment_oids, increment_nulls,
					    true);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	}
    }
  if (is_null)
    {
      /* Nothing to do anymore. */
      *stop = true;
    }
  return NO_ERROR;
}

/*
 * btree_merge_node_and_advance () - BTREE_ADVANCE_WITH_KEY_FUNCTION used by
 *				     btree_delete_internal to merge b-tree
 *				     nodes while advancing to delete data from
 *				     a key.
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree info.
 * key (in)		 : Key to follow while advancing.
 * crt_page (in)	 : Pointer to current node's page.
 * advance_to_page (out) : Outputs next node page to advance to.
 * is_leaf (out)	 : Outputs whether current node is leaf.
 * search_key (out)	 : Outputs search key result when current node is
 *			   leaf.
 * stop (out)		 : Outputs to end advancing (not used).
 * restart (out)	 : Outputs to restart from root.
 * other_args (in/out)	 : BTREE_DELETE_HELPER *
 */
static int
btree_merge_node_and_advance (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			      DB_VALUE * key, PAGE_PTR * crt_page,
			      PAGE_PTR * advance_to_page, bool * is_leaf,
			      BTREE_SEARCH_KEY_HELPER * search_key,
			      bool * stop, bool * restart, void *other_args)
{
  /* Delete helper used by internal functions of btree_delete_internal. */
  BTREE_DELETE_HELPER *delete_helper = (BTREE_DELETE_HELPER *) other_args;
  PAGE_PTR left_page = NULL;	/* Left page to merge. */
  PAGE_PTR right_page = NULL;	/* Right age to merge. */
  VPID left_vpid = VPID_INITIALIZER;	/* VPID of left page to merge. */
  VPID right_vpid = VPID_INITIALIZER;	/* VPID of right page to merge. */
  int left_used = 0;		/* Space used in left page. */
  int right_used = 0;		/* Space used in right page. */
  int error_code = NO_ERROR;	/* Error code. */
  int key_count = 0;		/* Node key count. */
  BTREE_NODE_HEADER *node_header = NULL;	/* B-tree node header. */
  RECDES left_recdes, right_recdes;	/* Record descriptors to read links
					 * to left/right pages.
					 */
  NON_LEAF_REC non_leaf_rec_info;	/* Non-leaf record info used to read
					 * links to left/right pages.
					 */
  PGBUF_LATCH_MODE child_latch = PGBUF_LATCH_READ;	/* Latch mode for
							 * children of current
							 * node.
							 */
  PGBUF_PROMOTE_CONDITION promote_cond;	/* Promote condition when write
					 * latch is required on nodes.
					 */
  VPID child_vpid;		/* VPID of next child by following
				 * key argument.
				 */
  VPID child_vpid_after_merge;	/* VPID of next by following key
				 * argument after merge is done.
				 */
  PAGE_PTR child_page;		/* Next page by following key
				 * argument.
				 */
  BTREE_MERGE_STATUS merge_status;	/* Status that tells when nodes can
					 * be merged.
					 */
  bool need_root_merge = false;	/* Set to true when root can be
				 * merged.
				 */
  bool force_root_merge = false;	/* Set to true when root must be
					 * merged.
					 */
  bool is_system_op_started = false;	/* Set to true when a system
					 * operation is started to be
					 * properly aborted in case of
					 * errors.
					 */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (crt_page != NULL && *crt_page != NULL);
  assert (advance_to_page != NULL && *advance_to_page == NULL);
  assert (is_leaf != NULL);
  assert (search_key != NULL);
  assert (restart != NULL);
  assert (delete_helper != NULL);

  /* Merge algorithm:
   * There are two types of merges: root merge and normal merge.
   * 1. Root merge:
   *    If root has only two keys and a level more than 2, it could be
   *    merged if all keys stored in leaf page pass the size check.
   *    All three nodes are merged into current root.
   *    After merging, function will continue to check the merged node
   *    similarly with any non-leaf nodes.
   *    NOTE: I don't really know why the root_level has to be greater than
   *          2. This means the b-tree will never be reduced back to one
   *          node. It may not be a real issue, since the index is too small
   *          to worry about performance.
   * 2. Normal merge:
   *    Two nodes are merged if they pass the size check or if any of them is
   *    empty. Both are merged to "left" node and their parent is updated.
   *    During delete, the child node found by following key argument is
   *    tested against its neighbors. A node is merged only once (if it was
   *    merged to the right node, it will not be merged to left node too).
   *
   * To optimize b-tree access, the algorithm assumes that no change is
   * required (nodes are not merged) and uses READ latch on non-leaf nodes
   * (leaf nodes still require WRITE latch since they are very likely to be
   * changed). Instead, if merge is required, latches are then promoted to
   * WRITE.
   * If promotions fail, the algorithm has two choices:
   * 1. Skip merging and just advance to child page.
   * 2. Restart b-tree traversal using exclusive access and force the merge.
   *    Second choice is decided when the two nodes use together less than one
   *    third of a page or when one of them is completely empty.
   *
   * Normally, promotions use shared reader condition (multiple readers are
   * allowed to promote, but no other promoters).
   * On level 2 of b-tree, single reader condition is used for promotion. The
   * restriction is explained in btree_split_node_and_advance.
   */

  /* Get current node header. */
  node_header = btree_get_node_header (*crt_page);
  if (node_header == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }
  if (node_header->node_level == 1)
    {
      /* This is a leaf node. Advancing can stop.
       * Search key, promote latch and return.
       */
      error_code =
	btree_search_leaf_page (thread_p, btid_int, *crt_page, key,
				search_key);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (delete_helper->is_root
	  && delete_helper->nonleaf_latch_mode != PGBUF_LATCH_WRITE)
	{
	  /* Promote latch. */
	  error_code =
	    pgbuf_promote_read_latch (thread_p, crt_page,
				      PGBUF_PROMOTE_SHARED_READER);
	  if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	    {
	      /* Promotion failed. Restart using write latch directly. */
	      *restart = true;
	      delete_helper->nonleaf_latch_mode = PGBUF_LATCH_WRITE;
	      return NO_ERROR;
	    }
	  else if (error_code != NO_ERROR)
	    {
	      /* Error promoting. */
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  else if (*crt_page == NULL)
	    {
	      /* Promoting has failed. Make sure an error has been set. */
	      ASSERT_ERROR_AND_SET (error_code);
	      return error_code;
	    }
	}
      else
	{
	  /* Leaf node should already have exclusive latch. */
	  assert (pgbuf_get_latch_mode (*crt_page) >= PGBUF_LATCH_WRITE);
	}
      /* Successful. */
      *is_leaf = true;
      return NO_ERROR;
    }
  /* Not a leaf page. */

  /* Get key count. */
  key_count = btree_node_number_of_keys (*crt_page);

  /* Check if current node is root and must be merged. */
  if (delete_helper->is_root	/* Current node is root. */
      && node_header->node_level > 2	/* Its level is more than two. */
      && btree_node_number_of_keys (*crt_page) == 2 /* Has only two keys */ )
    {
      /* Save root max key length. */
      int root_max_key_length = node_header->max_key_len;

      /* Since the root has at least level 2, its children are non-leaf. */

      /* Read the first record. */
      if (spage_get_record (*crt_page, 1, &left_recdes, PEEK) != S_SUCCESS)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
      btree_read_fixed_portion_of_non_leaf_record (&left_recdes,
						   &non_leaf_rec_info);
      /* Fix left child. */
      VPID_COPY (&left_vpid, &non_leaf_rec_info.pnt);
      assert (!VPID_ISNULL (&left_vpid));
      left_page =
	pgbuf_fix (thread_p, &left_vpid, OLD_PAGE,
		   delete_helper->nonleaf_latch_mode,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (left_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, left_page, PAGE_BTREE);
#endif /* !NDEBUG */
      left_used = DB_PAGESIZE - spage_get_free_space (thread_p, left_page);

      /* Read second record. */
      if (spage_get_record (*crt_page, 2, &right_recdes, PEEK) != S_SUCCESS)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
      btree_read_fixed_portion_of_non_leaf_record (&right_recdes,
						   &non_leaf_rec_info);
      /* Fix right child. */
      VPID_COPY (&right_vpid, &non_leaf_rec_info.pnt);
      assert (!VPID_ISNULL (&right_vpid));
      right_page =
	pgbuf_fix (thread_p, &right_vpid, OLD_PAGE,
		   delete_helper->nonleaf_latch_mode,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (right_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, right_page, PAGE_BTREE);
#endif /* !NDEBUG */
      right_used = DB_PAGESIZE - spage_get_free_space (thread_p, right_page);

      /* Is merge needed? Should be forced if promotion fails? */
      /* We need to consider the largest key size since merge will use a key
       * from root page as the middle key. It may be larger than any key in
       * its children. This may be overly defensive, but it doesn't seem so
       * bad for root merges.
       * TODO: The above comment may be part of a legacy code. Currently,
       *       Merging root algorithm doesn't show any extra key being
       *       required. Consider removing root_max_key_length.
       */
      need_root_merge =
	(left_used + right_used + CAN_MERGE_WHEN_EMPTY + root_max_key_length)
	< DB_PAGESIZE;
      /* If latch promotions fail, merge might be skipped. However, it is not
       * desirable to skip all merges in a highly accessed index. So we have
       * two levels of how much space can be wasted before merge is executed:
       * - one level when merge is attempted with latch promotion.
       * - one level, when more space was wasted and we need to force
       *   restarting b-tree traversal using exclusive latches and making sure
       *   that merge is executed.
       */
      force_root_merge =
	(left_used + right_used + FORCE_MERGE_WHEN_EMPTY
	 + root_max_key_length) < DB_PAGESIZE;

      /* If merge is required, promote latches. */
      if (need_root_merge)
	{
	  /* Need merge. */
	  /* All pages must be write latched. */

	  if (delete_helper->nonleaf_latch_mode == PGBUF_LATCH_READ)
	    {
	      /* Promote latches. */

	      /* First promote root. */
	      error_code =
		pgbuf_promote_read_latch (thread_p, crt_page,
					  PGBUF_PROMOTE_SHARED_READER);

	      if (error_code == NO_ERROR && *crt_page != NULL)
		{
		  /* Root successfully promoted. Promote left page. */
		  error_code =
		    pgbuf_promote_read_latch (thread_p, &left_page,
					      PGBUF_PROMOTE_SHARED_READER);
		  if (error_code == NO_ERROR && left_page != NULL)
		    {
		      /* Left page successfully promoted. Promote right page.
		       */
		      error_code =
			pgbuf_promote_read_latch
			(thread_p, &right_page, PGBUF_PROMOTE_SHARED_READER);
		    }
		}
	    }
	  else
	    {
	      /* Pages already latched exclusively. Fall through to continue
	       * merging.
	       */
	    }
	  if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	    {
	      /* Not all nodes could be promoted. */
	      if (force_root_merge)
		{
		  /* Restart b-tree traversal with exclusive access. */
		  delete_helper->nonleaf_latch_mode = PGBUF_LATCH_WRITE;
		  *restart = true;
		  if (left_page != NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, left_page);
		    }
		  if (right_page != NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, right_page);
		    }
		  return NO_ERROR;
		}
	      /* Skip merging. */
	      /* Fall through to advance to child. */
	    }
	  else if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	  else if (*crt_page == NULL || left_page == NULL
		   || right_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	  else
	    {
	      /* All pages are write latched. Merge the nodes. */
	      assert ((pgbuf_get_latch_mode (*crt_page) >= PGBUF_LATCH_WRITE)
		      && (pgbuf_get_latch_mode (left_page)
			  >= PGBUF_LATCH_WRITE)
		      && (pgbuf_get_latch_mode (right_page)
			  >= PGBUF_LATCH_WRITE));

	      /* Start system operation. */
	      if (log_start_system_op (thread_p) == NULL)
		{
		  assert_release (false);
		  error_code = ER_FAILED;
		  goto error;
		}
	      is_system_op_started = true;

	      /* Merge the three nodes into root node. */
	      error_code =
		btree_merge_root (thread_p, btid_int, *crt_page, left_page,
				  right_page);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto error;
		}
#if !defined (NDEBUG)
	      (void) spage_check_num_slots (thread_p, *crt_page);
#endif /* !NDEBUG */

	      /* Merge successfully finished. */
	      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	      is_system_op_started = false;

	      pgbuf_unfix_and_init (thread_p, left_page);
	      error_code =
		btree_dealloc_page (thread_p, btid_int, &left_vpid);
	      if (error_code != NO_ERROR)
		{
		  /* Is this acceptable? Pages are "leaked" if it happens. */
		  ASSERT_ERROR ();
		  goto error;
		}
	      pgbuf_unfix_and_init (thread_p, right_page);
	      error_code =
		btree_dealloc_page (thread_p, btid_int, &right_vpid);
	      if (error_code != NO_ERROR)
		{
		  /* Is this acceptable? Pages are "leaked" if it happens. */
		  ASSERT_ERROR ();
		  goto error;
		}

	      /* Refresh header & key count. */
	      node_header = btree_get_node_header (*crt_page);
	      if (node_header == NULL)
		{
		  assert_release (false);
		  error_code = ER_FAILED;
		  goto error;
		}
	      assert (node_header->node_level > 1);
	      key_count = btree_node_number_of_keys (*crt_page);
	    }
	}
      /* Unfix pages if merge was not executed. */
      if (left_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, left_page);
	}
      if (right_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, right_page);
	}
    }
  assert (left_page == NULL);
  assert (right_page == NULL);

  /* Choose the node to advance to. Then check whether it can be merged with
   * any of its neighbors.
   */
  error_code =
    btree_search_nonleaf_page (thread_p, btid_int, *crt_page, key,
			       &search_key->slotid, &child_vpid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }
  if (node_header->node_level == 2)
    {
      /* Next child is leaf and must be latched using write latch mode
       * directly.
       * Promote condition is single reader to avoid dead-latch (see the
       * comment explaining promote condition in
       * btree_split_node_and_advance).
       */
      child_latch = PGBUF_LATCH_WRITE;
      promote_cond = PGBUF_PROMOTE_SINGLE_READER;
    }
  else
    {
      /* Use non-leaf latch mode. */
      /* Promote when non-leaf latch mode is PGBUF_LATCH_READ can be shared
       * reader. If latch mode is PGBUF_LATCH_WRITE, no promotion is required.
       */
      child_latch = delete_helper->nonleaf_latch_mode;
      promote_cond = PGBUF_PROMOTE_SHARED_READER;
    }
  /* Fix child node. */
  child_page =
    pgbuf_fix (thread_p, &child_vpid, OLD_PAGE, child_latch,
	       PGBUF_UNCONDITIONAL_LATCH);
  if (child_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      goto error;
    }
#if !defined (NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, child_page, PAGE_BTREE);
#endif /* !NDEBUG */

  /* Get header of child. */
  node_header = btree_get_node_header (child_page);
  if (node_header == NULL)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto error;
    }
  /* Check merges. */
  if (search_key->slotid < key_count)
    {
      /* Check right merge. */

      /* Get link to right page. */
      if (spage_get_record (*crt_page, search_key->slotid + 1, &right_recdes,
			    PEEK) != S_SUCCESS)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
      btree_read_fixed_portion_of_non_leaf_record (&right_recdes,
						   &non_leaf_rec_info);
      /* Fix right page. */
      VPID_COPY (&right_vpid, &non_leaf_rec_info.pnt);
      right_page =
	pgbuf_fix (thread_p, &right_vpid, OLD_PAGE, child_latch,
		   PGBUF_UNCONDITIONAL_LATCH);
      if (right_page == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto error;
	}
#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, right_page, PAGE_BTREE);
#endif /* !NDEBUG */

      /* Can child page be merged with its right page? */
      merge_status =
	btree_node_mergeable (thread_p, btid_int, child_page, right_page);
      if (merge_status != BTREE_MERGE_NO)
	{
	  /* Try to merge. */

	  /* Exclusive latch is required on all nodes. */
	  if (delete_helper->nonleaf_latch_mode != PGBUF_LATCH_WRITE)
	    {
	      /* Promote latch on current node. */
	      error_code =
		pgbuf_promote_read_latch (thread_p, crt_page, promote_cond);
	      if (error_code == NO_ERROR && *crt_page != NULL
		  && child_latch != PGBUF_LATCH_WRITE)
		{
		  /* Promote latches on children. */

		  /* Promote latch on child. */
		  error_code =
		    pgbuf_promote_read_latch (thread_p, &child_page,
					      promote_cond);
		  if (error_code == NO_ERROR && child_page != NULL)
		    {
		      /* Promote latch on right page. */
		      error_code =
			pgbuf_promote_read_latch (thread_p, &right_page,
						  promote_cond);
		    }
		}
	    }
	  /* Are all pages successfully write latched? */
	  if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	    {
	      /* Failed to promote all latches to exclusive. */
	      if (merge_status != BTREE_MERGE_SIZE)
		{
		  /* Merge must be executed. Restart using exclusive access.
		   */
		  delete_helper->nonleaf_latch_mode = PGBUF_LATCH_WRITE;
		  *restart = true;
		  if (right_page != NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, right_page);
		    }
		  if (child_page != NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, child_page);
		    }
		  return NO_ERROR;
		}
	      /* Merge can be skipped. Fall through. */
	    }
	  else if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	  else if (*crt_page == NULL || child_page == NULL
		   || right_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	  else
	    {
	      /* All pages are write latched. */
	      assert ((pgbuf_get_latch_mode (*crt_page) >= PGBUF_LATCH_WRITE)
		      && (pgbuf_get_latch_mode (child_page)
			  >= PGBUF_LATCH_WRITE)
		      && (pgbuf_get_latch_mode (right_page)
			  >= PGBUF_LATCH_WRITE));

	      /* Start system operation. */
	      if (log_start_system_op (thread_p) == NULL)
		{
		  assert_release (false);
		  error_code = ER_FAILED;
		  goto error;
		}
	      is_system_op_started = true;

	      /* Merge children and update parent. */
	      error_code =
		btree_merge_node (thread_p, btid_int, *crt_page, child_page,
				  right_page, search_key->slotid + 1,
				  &child_vpid_after_merge, merge_status);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto error;
		}
	      /* Children are merged to the "left" node which is our case
	       * is the child page.
	       */
	      assert (!VPID_ISNULL (&child_vpid_after_merge));
	      assert (VPID_EQ (&child_vpid_after_merge, &child_vpid));
	      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	      is_system_op_started = false;

#if !defined(NDEBUG)
	      (void) spage_check_num_slots (thread_p, *crt_page);
	      (void) spage_check_num_slots (thread_p, child_page);
#endif

	      /* Deallocate right page. */
	      pgbuf_unfix_and_init (thread_p, right_page);
	      error_code =
		btree_dealloc_page (thread_p, btid_int, &right_vpid);
	      if (error_code != NO_ERROR)
		{
		  /* Is this acceptable? Pages are "leaked" if it happens. */
		  ASSERT_ERROR ();
		  goto error;
		}

	      /* Advance to child page. */
	      *advance_to_page = child_page;
	      pgbuf_unfix_and_init (thread_p, *crt_page);
	      delete_helper->is_root = false;
	      return NO_ERROR;
	    }
	}
      /* Merge not executed. Unfix right page. */
      pgbuf_unfix_and_init (thread_p, right_page);
    }
  if (search_key->slotid > 1)
    {
      /* Check merge with left node. */

      /* Get link to left page. */
      if (spage_get_record (*crt_page, search_key->slotid - 1,
			    &left_recdes, PEEK) != S_SUCCESS)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
      btree_read_fixed_portion_of_non_leaf_record (&left_recdes,
						   &non_leaf_rec_info);
      /* Get left page. */
      VPID_COPY (&left_vpid, &non_leaf_rec_info.pnt);
      assert (!VPID_ISNULL (&left_vpid));
      /* Try conditional latch on left page to avoid dead latches. */
      left_page =
	pgbuf_fix (thread_p, &left_vpid, OLD_PAGE, child_latch,
		   PGBUF_CONDITIONAL_LATCH);
      if (left_page == NULL)
	{
	  /* Failed conditional latch. */
	  /* Fix left, then child. */

	  /* Unfix child. */
	  pgbuf_unfix_and_init (thread_p, child_page);

	  /* Fix left. */
	  left_page =
	    pgbuf_fix (thread_p, &left_vpid, OLD_PAGE, child_latch,
		       PGBUF_UNCONDITIONAL_LATCH);
	  if (left_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }

	  /* Fix child. */
	  child_page =
	    pgbuf_fix (thread_p, &child_vpid, OLD_PAGE, child_latch,
		       PGBUF_UNCONDITIONAL_LATCH);
	  if (child_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	}
      /* Both left and child pages are fixed. */
      assert (left_page != NULL && child_page != NULL);

#if !defined (NDEBUG)
      (void) pgbuf_check_page_ptype (thread_p, left_page, PAGE_BTREE);
#endif /* !NDEBUG */

      merge_status =
	btree_node_mergeable (thread_p, btid_int, left_page, child_page);
      if (merge_status != BTREE_MERGE_NO)
	{
	  /* All pages must be write latched. */
	  if (delete_helper->nonleaf_latch_mode != PGBUF_LATCH_WRITE)
	    {
	      /* Promote latches. */
	      error_code =
		pgbuf_promote_read_latch (thread_p, crt_page, promote_cond);
	      if (error_code == NO_ERROR && *crt_page != NULL
		  && child_latch != PGBUF_LATCH_WRITE)
		{
		  /* Promote children latches. */
		  error_code =
		    pgbuf_promote_read_latch (thread_p, &left_page,
					      promote_cond);
		  if (error_code == NO_ERROR && left_page != NULL)
		    {
		      error_code =
			pgbuf_promote_read_latch (thread_p, &child_page,
						  promote_cond);
		    }
		}
	    }
	  /* Check write latches were successfully obtained. */
	  if (error_code == ER_PAGE_LATCH_PROMOTE_FAIL)
	    {
	      if (merge_status != BTREE_MERGE_SIZE)
		{
		  /* Merge has to be done. Restart using exclusive access. */
		  delete_helper->nonleaf_latch_mode = PGBUF_LATCH_WRITE;
		  *restart = true;
		  if (left_page != NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, left_page);
		    }
		  if (child_page != NULL)
		    {
		      pgbuf_unfix_and_init (thread_p, child_page);
		    }
		  return NO_ERROR;
		}
	      /* Merge can be skipped. Fall through. */
	    }
	  else if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	  else if (*crt_page == NULL || left_page == NULL
		   || child_page == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      goto error;
	    }
	  else
	    {
	      /* All pages are write latched. */
	      assert ((pgbuf_get_latch_mode (*crt_page) >= PGBUF_LATCH_WRITE)
		      && (pgbuf_get_latch_mode (child_page)
			  >= PGBUF_LATCH_WRITE)
		      && (pgbuf_get_latch_mode (left_page)
			  >= PGBUF_LATCH_WRITE));

	      /* Start system operation. */
	      if (log_start_system_op (thread_p) == NULL)
		{
		  assert_release (false);
		  error_code = ER_FAILED;
		  goto error;
		}
	      is_system_op_started = true;

	      /* Merge left page and child page and update parent. */
	      error_code =
		btree_merge_node (thread_p, btid_int, *crt_page, left_page,
				  child_page, search_key->slotid,
				  &child_vpid_after_merge, merge_status);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  goto error;
		}

#if !defined(NDEBUG)
	      (void) spage_check_num_slots (thread_p, *crt_page);
	      (void) spage_check_num_slots (thread_p, left_page);
#endif /* !NDEBUG */

	      /* Pages have been merged into left page. */
	      assert (!VPID_ISNULL (&child_vpid_after_merge));
	      assert (VPID_EQ (&child_vpid_after_merge, &left_vpid));

	      log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	      is_system_op_started = false;

	      /* Deallocate child page. */
	      pgbuf_unfix_and_init (thread_p, child_page);
	      error_code =
		btree_dealloc_page (thread_p, btid_int, &child_vpid);
	      if (error_code != NO_ERROR)
		{
		  /* Is this acceptable? Pages are "leaked" if it happens. */
		  ASSERT_ERROR ();
		  goto error;
		}

	      /* Choose to advance to left page. */
	      *advance_to_page = left_page;
	      pgbuf_unfix_and_init (thread_p, *crt_page);
	      delete_helper->is_root = false;
	      return NO_ERROR;
	    }
	}
      /* Not merged. Unfix left page. */
      pgbuf_unfix_and_init (thread_p, left_page);
    }
  /* No merge has been executed. */
  assert (left_page == NULL);
  assert (right_page == NULL);

  /* Advance to current child. */
  *advance_to_page = child_page;
  pgbuf_unfix_and_init (thread_p, *crt_page);
  delete_helper->is_root = false;

  return NO_ERROR;

error:
  assert_release (error_code != NO_ERROR);

  if (is_system_op_started)
    {
      /* Abort system operation. */
      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  /* Unfix used pages. */
  assert (*advance_to_page == NULL);
  if (left_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, left_page);
    }
  if (right_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, right_page);
    }
  if (child_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, child_page);
    }

  return error_code;
}

/*
 * btree_key_delete_remove_object () - Remove one object and all its info from
 *				       b-tree key.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree info.
 * key (in)	   : Key value.
 * leaf_page (in)  : Leaf page.
 * search_key (in) : Search key result.
 * restart (in)	   : Not used.
 * other_args (in) : BTREE_DELETE_HELPER *
 */
static int
btree_key_delete_remove_object (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
				DB_VALUE * key, PAGE_PTR * leaf_page,
				BTREE_SEARCH_KEY_HELPER * search_key,
				bool * restart, void *other_args)
{
  /* btree_delete_internal helper. */
  BTREE_DELETE_HELPER *delete_helper = (BTREE_DELETE_HELPER *) other_args;
  int error_code = NO_ERROR;	/* Error code. */
  RECDES leaf_record;		/* Copy leaf record. */
  /* Buffer used to copy leaf record. */
  char record_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  LEAF_REC leaf_rec_info;	/* Leaf record info. */
  int offset_after_key = 0;	/* Offset after key in leaf record. */
  bool dummy_clear_value = false;	/* Dummy. */
  PAGE_PTR found_page = NULL;	/* Page where object being removed is
				 * found.
				 */
  PAGE_PTR prev_found_page = NULL;	/* Previous page to the page where
					 * object being removed is found.
					 * Saved in case that object is last
					 * in an overflow page and page must
					 * be deallocated.
					 */
  int offset_to_object = NOT_FOUND;	/* Offset in record where object to
					 * be removed is found.
					 */

  /* Recovery structures. */
  /* Undo recovery structures. */
  char rv_undo_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  char *rv_undo_data_bufalign =
    PTR_ALIGN (rv_undo_data_buffer, BTREE_MAX_ALIGN);
  int rv_undo_data_capacity = IO_MAX_PAGE_SIZE;
  /* Redo recovery structures. */
  char rv_redo_data_buffer[BTREE_RV_REDO_BUFFER_SIZE + BTREE_MAX_ALIGN];

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (leaf_page != NULL && *leaf_page != NULL
	  && pgbuf_get_latch_mode (*leaf_page) >= PGBUF_LATCH_WRITE);
  assert (search_key != NULL);
  assert (delete_helper != NULL);
  assert (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL
	  || delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT);

  if (search_key->result == BTREE_KEY_FOUND)
    {
      /* Key was found. We need to find OID. */
      /* Read key record. */
      leaf_record.data = PTR_ALIGN (record_data_buffer, BTREE_MAX_ALIGN);
      leaf_record.area_size = DB_PAGESIZE;
      if (spage_get_record (*leaf_page, search_key->slotid, &leaf_record,
			    COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      error_code =
	btree_read_record (thread_p, btid_int, *leaf_page, &leaf_record, NULL,
			   &leaf_rec_info, BTREE_LEAF_NODE,
			   &dummy_clear_value, &offset_after_key,
			   PEEK_KEY_VALUE, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      /* Find OID and output its location/MVCC info. */
      error_code =
	btree_find_oid_and_its_page (thread_p, btid_int,
				     BTREE_DELETE_OID (delete_helper),
				     *leaf_page, delete_helper->purpose,
				     &delete_helper->match_mvccid,
				     &leaf_record, &leaf_rec_info,
				     offset_after_key, &found_page,
				     &prev_found_page, &offset_to_object,
				     BTREE_DELETE_MVCC_INFO (delete_helper));
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }
  else
    {
      /* Key was not found. Fall through to handle the case. */
    }
  if (offset_to_object == NOT_FOUND)
    {
      /* Key/object was not found. */
      assert (found_page == NULL && prev_found_page == NULL);

      if (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT)
	{
	  /* Most probably, object was already vacuumed.
	   * One uncommon, but yet possible case when this can happen:
	   * 1. OID1, reusable object in a b-tree overflow page.
	   * 2. OID1 is marked as deleted to be vacuumed later.
	   * 3. OID1 is vacuumed from heap and its slot can be reused.
	   * 4. OID1 is reused and is inserted in the same overflow page. The
	   *    algorithm recognizes that the old version is just not vacuumed
	   *    yet and replaces it. OID's cannot be repeated in overflow
	   *    pages.
	   * 5. Vacuum will not find the old OID1 to remove.
	   */
	  vacuum_er_log (VACUUM_ER_LOG_WARNING | VACUUM_ER_LOG_BTREE
			 | VACUUM_ER_LOG_WORKER,
			 "VACUUM WARNING: Could not find object %d|%d|%d "
			 "in key=%s to vacuum it.",
			 delete_helper->object_info.oid.volid,
			 delete_helper->object_info.oid.pageid,
			 delete_helper->object_info.oid.slotid,
			 delete_helper->printed_key != NULL ?
			 delete_helper->printed_key : "(unknown)");
	  if (delete_helper->log_operations)
	    {
	      _er_log_debug (ARG_FILE_LINE,
			     "BTREE_DELETE: Could not find object %d|%d|%d "
			     "in key=%s to vacuum it.",
			     delete_helper->object_info.oid.volid,
			     delete_helper->object_info.oid.pageid,
			     delete_helper->object_info.oid.slotid,
			     delete_helper->printed_key != NULL ?
			     delete_helper->printed_key : "(unknown)");
	    }
	  return NO_ERROR;
	}
      else
	{
	  /* Key/oid should be found. */
	  assert_release (false);
	  btree_set_unknown_key_error (thread_p, btid_int->sys_btid, key,
				       "btree_key_delete_remove_object: "
				       "key was not found.");
	  return ER_BTREE_UNKNOWN_KEY;
	}
    }
  /* Object was found. */

#if defined (SERVER_MODE)
  /* When do we need to have a lock on object to protect it?
   * 1. If this is not vacuum.
   * 2. If this is not crash recovery.
   * 3. If object is not a rollback of insert into non-unique index.
   * 4. If object is not inserted by current transaction into non-unique
   *    index.
   * All other cases must have lock on object.
   */
  assert (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT
	  || log_is_in_crash_recovery ()
	  || (!BTREE_IS_UNIQUE (btid_int->unique_pk)
	      && (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT
		  || BTREE_MVCC_INFO_INSID
		  (BTREE_DELETE_MVCC_INFO (delete_helper))
		  == logtb_find_current_mvccid (thread_p)))
	  /* Cannot check if class OID is NULL. Get it in debug mode. */
	  || OID_ISNULL (BTREE_DELETE_CLASS_OID (delete_helper))
	  || lock_has_lock_on_object (BTREE_DELETE_OID (delete_helper),
				      BTREE_DELETE_CLASS_OID (delete_helper),
				      thread_get_current_tran_index (),
				      X_LOCK) > 0);
#endif /* SERVER_MODE */

  /* Safe guard: if the index is unique and we want to physically delete the
   *             object and if operation type is not MULTI_ROW_UPDATE, then
   *             object must be first in record.
   */
  assert (!BTREE_IS_UNIQUE (btid_int->unique_pk)
	  || delete_helper->purpose != BTREE_OP_DELETE_OBJECT_PHYSICAL
	  || delete_helper->op_type == MULTI_ROW_UPDATE
	  || (found_page == *leaf_page && offset_to_object == 0));

  /* Prepare logging. */
  delete_helper->leaf_addr.pgptr = *leaf_page;
  delete_helper->leaf_addr.offset = search_key->slotid;
  delete_helper->leaf_addr.vfid = &btid_int->sys_btid->vfid;

  /* Undo logging. */
  /* Vacuum doesn't require undo logging - vacuum may sometime open a system
   * operation to handle complex actions, like deallocating overflow pages.
   * However, the object is removal is last and if successful, then the entire
   * operation is considered successful. Object removal does not require undo
   * logging.
   * BTREE_OP_DELETE_UNDO_INSERT will append a compensate log record and also
   * requires only redo recovery data.
   */
  if (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL)
    {
      delete_helper->rv_undo_data = rv_undo_data_bufalign;
      error_code =
	btree_rv_save_keyval_for_undo (btid_int, key,
				       BTREE_DELETE_CLASS_OID (delete_helper),
				       BTREE_DELETE_OID (delete_helper),
				       BTREE_DELETE_MVCC_INFO (delete_helper),
				       delete_helper->purpose,
				       rv_undo_data_bufalign,
				       &delete_helper->rv_undo_data,
				       &rv_undo_data_capacity,
				       &delete_helper->rv_undo_data_length);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }
  else
    {
      /* No need for undo data. */
      delete_helper->rv_undo_data = NULL;
      delete_helper->rv_undo_data_length = 0;
    }

  /* Redo logging. */
  delete_helper->rv_redo_data =
    PTR_ALIGN (rv_redo_data_buffer, BTREE_MAX_ALIGN);
  delete_helper->rv_redo_data_ptr = delete_helper->rv_redo_data;

  /* Where was object found? */
  if (*leaf_page == found_page)
    {
      /* Leaf record. */
      assert (prev_found_page == NULL);

      error_code =
	btree_leaf_remove_object (thread_p, btid_int, delete_helper,
				  *leaf_page, &leaf_record, &leaf_rec_info,
				  offset_after_key, search_key,
				  offset_to_object);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      /* Dereference found_page. */
      found_page = NULL;
    }
  else				/* *leaf_page != found_page */
    {
      /* Object belongs to overflow. */
      error_code =
	btree_overflow_remove_object (thread_p, btid_int, delete_helper,
				      &found_page, prev_found_page,
				      *leaf_page, &leaf_record, search_key,
				      offset_to_object);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      if (found_page != NULL)
	{
	  pgbuf_unfix_and_init (thread_p, found_page);
	}
      if (prev_found_page != *leaf_page)
	{
	  pgbuf_unfix_and_init (thread_p, prev_found_page);
	}
      else
	{
	  /* Dereference prev_found_page. */
	  prev_found_page = NULL;
	}
    }
  /* Safe guard: don't leak pages. */
  assert (found_page == NULL && prev_found_page == NULL);

  if (delete_helper->check_key_deleted)
    {
      /* Check key is really deleted (it may still have visible objects). */
      int num_visible_oids;
      int max_visible_oids = 1;
      MVCC_SNAPSHOT mvcc_snapshot_dirty;

      assert (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL);

      mvcc_snapshot_dirty.snapshot_fnc = mvcc_satisfies_dirty;

      /* Re-read leaf record. */
      if (spage_get_record (*leaf_page, search_key->slotid, &leaf_record,
			    PEEK) != S_SUCCESS)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
      error_code =
	btree_read_record (thread_p, btid_int, *leaf_page, &leaf_record, NULL,
			   &leaf_rec_info, BTREE_LEAF_NODE,
			   &dummy_clear_value, &offset_after_key,
			   PEEK_KEY_VALUE, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      num_visible_oids =
	btree_get_num_visible_from_leaf_and_ovf (thread_p, btid_int,
						 &leaf_record,
						 offset_after_key,
						 &leaf_rec_info,
						 &max_visible_oids,
						 &mvcc_snapshot_dirty);
      if (num_visible_oids < 0)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      else if (num_visible_oids > 0)
	{
	  /* Key still has visible objects and is not deleted. */
	  delete_helper->is_key_deleted = false;
	}
      else
	{
	  /* Key is deleted. */
	  assert (delete_helper->is_key_deleted);
	}
    }
  /* Success. */
  if (delete_helper->rv_undo_data != NULL
      && delete_helper->rv_undo_data != rv_undo_data_bufalign)
    {
      db_private_free_and_init (thread_p, delete_helper->rv_undo_data);
    }
  return NO_ERROR;

error:
  if (found_page != NULL && found_page != *leaf_page)
    {
      pgbuf_unfix_and_init (thread_p, found_page);
    }
  if (prev_found_page != NULL && prev_found_page != *leaf_page)
    {
      pgbuf_unfix_and_init (thread_p, prev_found_page);
    }
  if (delete_helper->rv_undo_data != NULL
      && delete_helper->rv_undo_data != rv_undo_data_bufalign)
    {
      db_private_free_and_init (thread_p, delete_helper->rv_undo_data);
    }
  assert_release (error_code != NO_ERROR);
  return error_code;
}

/*
 * btree_leaf_record_replace_first_with_last () - Remove first object by
 *						  replacing it with last.
 *
 * return		      : Error code.
 * thread_p (in)	      : Thread entry.
 * btid_int (in)	      : B-tree identifier.
 * key (in)		      : Key value.
 * delete_helper (in)	      : B-tree delete helper.
 * leaf_page (in)	      : Leaf page.
 * leaf_record (in)	      : Key leaf record.
 * search_key (in)	      : Search key result.
 * last_oid (in)	      : Last object OID.
 * last_class_oid (in)	      : Last object class OID.
 * last_mvcc_info (in)	      : Last object MVCC info.
 * offset_to_last_object (in) : Offset to last object.
 */
static int
btree_leaf_record_replace_first_with_last (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   BTREE_DELETE_HELPER *
					   delete_helper,
					   PAGE_PTR leaf_page,
					   RECDES * leaf_record,
					   BTREE_SEARCH_KEY_HELPER *
					   search_key,
					   OID * last_oid,
					   OID * last_class_oid,
					   BTREE_MVCC_INFO * last_mvcc_info,
					   int offset_to_last_object)
{
  int error_code = NO_ERROR;	/* Error code. */
  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (delete_helper != NULL);
  assert (leaf_page != NULL
	  && pgbuf_get_latch_mode (leaf_page) >= PGBUF_LATCH_WRITE);
  assert (leaf_record != NULL);
  assert (search_key != NULL);
  assert (last_oid != NULL);
  assert (last_class_oid != NULL);
  assert (last_mvcc_info != NULL);
  assert (offset_to_last_object > 0
	  && offset_to_last_object < leaf_record->length);
  assert (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT
	  || delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT);
  assert (delete_helper->rv_redo_data != NULL
	  && delete_helper->rv_redo_data_ptr != NULL);

#if !defined (NDEBUG)
  /* For debugging recovery. */
  BTREE_RV_REDO_SET_DEBUG_INFO (&delete_helper->leaf_addr,
				delete_helper->rv_redo_data_ptr, btid_int,
				BTREE_RV_DEBUG_ID_LAST_OID);
#endif /* !NDEBUG */
  LOG_RV_RECORD_SET_MODIFY_MODE (&delete_helper->leaf_addr,
				 LOG_RV_RECORD_UPDATE_PARTIAL);

  /* Replace first object with last object. */
  /* First remove last object (so its offset doesn't change. */
  btree_record_remove_last_object (thread_p, btid_int, leaf_record,
				   BTREE_LEAF_NODE, offset_to_last_object,
				   &delete_helper->rv_redo_data_ptr);
  /* Replace first. */
  btree_leaf_change_first_object (leaf_record, btid_int, last_oid,
				  last_class_oid, last_mvcc_info, NULL,
				  &delete_helper->rv_redo_data_ptr);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Update record. */
  if (spage_update (thread_p, leaf_page, search_key->slotid, leaf_record)
      != SP_SUCCESS)
    {
      /* Should not fail. */
      assert_release (false);
      return ER_FAILED;
    }

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully executed delete "
		     "object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s, slotid=%d, page=%d|%d, "
		     "lsa=%lld|%d, in index (%d, %d|%d). "
		     "First object was replaced with last.\n",
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.delete_mvccid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     search_key->slotid,
		     pgbuf_get_volume_id (leaf_page),
		     pgbuf_get_page_id (leaf_page),
		     (long long int) pgbuf_get_lsa (leaf_page)->pageid,
		     (int) pgbuf_get_lsa (leaf_page)->offset,
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  /* Log changes. */
  rv_redo_data_length =
    CAST_BUFLEN (delete_helper->rv_redo_data_ptr
		 - delete_helper->rv_redo_data);
  /* Safe guard. */
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  if (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL)
    {
      /* Add undoredo log. */
      /* TODO: Remove undo on rollback. */
      log_append_undoredo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
				&delete_helper->leaf_addr,
				delete_helper->rv_undo_data_length,
				rv_redo_data_length,
				delete_helper->rv_undo_data,
				delete_helper->rv_redo_data);
    }
  else if (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT)
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (leaf_page),
			     delete_helper->leaf_addr.offset,
			     leaf_page, rv_redo_data_length,
			     delete_helper->rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }
  else				/* BTREE_OP_DELETE_VACUUM_OBJECT */
    {
      log_append_redo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL,
			    &delete_helper->leaf_addr, rv_redo_data_length,
			    delete_helper->rv_redo_data);
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, leaf_page, DONT_FREE);

  /* Success */
  return NO_ERROR;
}

/*
 * btree_record_remove_object () - Remove object from b-tree leaf or overflow
 *				   record.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * btid_int (in)	    : B-tree info.
 * delete_helper (in)	    : B-tree delete helper.
 * page (in)		    : Leaf or overflow page.
 * record (in)		    : Leaf or overflow record.
 * search_key (in)	    : Search key result.
 * node_type (in)	    : Leaf or overflow node type.
 * offset_to_object (in)    : Offset to object being removed.
 * addr (in)		    : Leaf or overflow log address.
 */
static int
btree_record_remove_object (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			    BTREE_DELETE_HELPER * delete_helper,
			    PAGE_PTR page, RECDES * record,
			    BTREE_SEARCH_KEY_HELPER * search_key,
			    BTREE_NODE_TYPE node_type, int offset_to_object,
			    LOG_DATA_ADDR * addr)
{
  int oid_size = OR_OID_SIZE;	/* Size of object and all its info. */
  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (delete_helper != NULL);
  assert (page != NULL);
  assert (record != NULL);
  assert (search_key != NULL && search_key->result == BTREE_KEY_FOUND
	  && search_key->slotid > 0);
  assert (addr != NULL);
  assert (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT
	  || delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT);
  assert (delete_helper->rv_redo_data != NULL
	  && delete_helper->rv_redo_data_ptr != NULL);

  /* Safe guard: first object in leaf record cannot be handled here. */
  assert (offset_to_object > 0 || node_type == BTREE_OVERFLOW_NODE);

#if !defined (NDEBUG)
  /* For debugging recovery. */
  BTREE_RV_REDO_SET_DEBUG_INFO (addr, delete_helper->rv_redo_data_ptr,
				btid_int, BTREE_RV_DEBUG_ID_REM_OBJ);
#endif /* !NDEBUG */
  LOG_RV_RECORD_SET_MODIFY_MODE (addr, LOG_RV_RECORD_UPDATE_PARTIAL);

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      /* Class OID is also saved. */
      oid_size += OR_OID_SIZE;
    }
  /* Compute MVCC info size. */
  oid_size +=
    BTREE_GET_MVCC_INFO_SIZE_FROM_FLAGS (BTREE_DELETE_MVCC_INFO
					 (delete_helper)->flags);

  /* Update record. */
  RECORD_MOVE_DATA (record, offset_to_object, offset_to_object + oid_size);

  /* Redo logging. */
  delete_helper->rv_redo_data_ptr =
    log_rv_pack_redo_record_changes (delete_helper->rv_redo_data_ptr,
				     offset_to_object, oid_size, 0, NULL);
  if (node_type == BTREE_OVERFLOW_NODE)
    {
      BTREE_RV_REDO_SET_OVERFLOW_NODE (addr);
    }

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, record, node_type,
				   NULL);
#endif /* !NDEBUG */

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Update page. */
  /* NOTE: Overflow record slotid is always 1. */
  if (spage_update (thread_p, page,
		    node_type == BTREE_LEAF_NODE ? search_key->slotid : 1,
		    record) != SP_SUCCESS)
    {
      /* No error is expected. */
      assert_release (false);
      return ER_FAILED;
    }

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully executed delete "
		     "object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s, slotid=%d, "
		     "%s page=%d|%d, lsa=%lld|%d in index (%d, %d|%d). "
		     "Object was removed.\n",
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.delete_mvccid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     search_key->slotid,
		     node_type == BTREE_OVERFLOW_NODE ? "overflow" : "leaf",
		     pgbuf_get_volume_id (page), pgbuf_get_page_id (page),
		     (long long int) pgbuf_get_lsa (page)->pageid,
		     (int) pgbuf_get_lsa (page)->offset,
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  /* Add logging. */
  rv_redo_data_length =
    CAST_BUFLEN (delete_helper->rv_redo_data_ptr
		 - delete_helper->rv_redo_data);
  /* Safe guard. */
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);

  if (delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL)
    {
      /* Add undo/redo logging. */
      log_append_undoredo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL, addr,
				delete_helper->rv_undo_data_length,
				rv_redo_data_length,
				delete_helper->rv_undo_data,
				delete_helper->rv_redo_data);
    }
  else if (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT)
    {
      log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			     pgbuf_get_vpid_ptr (page), addr->offset,
			     page, rv_redo_data_length,
			     delete_helper->rv_redo_data,
			     LOG_FIND_CURRENT_TDES (thread_p));
    }
  else				/* BTREE_OP_DELETE_VACUUM_OBJECT */
    {
      log_append_redo_data (thread_p, RVBT_DELETE_OBJECT_PHYSICAL, addr,
			    rv_redo_data_length, delete_helper->rv_redo_data);
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Set page dirty. */
  pgbuf_set_dirty (thread_p, page, DONT_FREE);

  /* Success. */
  return NO_ERROR;
}

/*
 * btree_overflow_remove_object () - Remove an object from overflow page.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * btid_int (in)	    : B-tree info.
 * delete_helper (in)	    : B-tree delete helper.
 * overflow_page (in)	    : Overflow page (can be set to NULL).
 * prev_page (in)	    : Page previous to overflow page (can be leaf page
 *			      or another overflow page).
 * leaf_page (in)	    : Leaf page.
 * leaf_record (in)	    : Leaf record.
 * search_key (in)	    : Search key result.
 * offset_to_object (in)    : Offset to object being removed.
 */
static int
btree_overflow_remove_object (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			      BTREE_DELETE_HELPER * delete_helper,
			      PAGE_PTR * overflow_page, PAGE_PTR prev_page,
			      PAGE_PTR leaf_page, RECDES * leaf_record,
			      BTREE_SEARCH_KEY_HELPER * search_key,
			      int offset_to_object)
{
  int error_code = NO_ERROR;	/* Error code. */
  RECDES overflow_record;	/* Overflow record. */
  /* Buffer to copy overflow record data. */
  char overflow_record_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  bool is_system_op_started = false;	/* Set to true when a system
					 * operation is started.
					 */
  VPID overflow_vpid = VPID_INITIALIZER;	/* VPID of overflow page. */
  VPID next_overflow_vpid = VPID_INITIALIZER;	/* VPID of next overflow page.
						 */
  LOG_DATA_ADDR ovf_addr;	/* Address for logging. */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (delete_helper != NULL);
  assert (overflow_page != NULL && *overflow_page != NULL);
  assert (prev_page != NULL);
  assert (leaf_page != NULL);
  assert (leaf_record != NULL);
  assert (search_key != NULL && search_key->result == BTREE_KEY_FOUND
	  && search_key->slotid > 0);
  assert (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT
	  || delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT);

  /* Read overflow record. */
  overflow_record.area_size = DB_PAGESIZE;
  overflow_record.data =
    PTR_ALIGN (overflow_record_data_buffer, BTREE_MAX_ALIGN);
  if (spage_get_record (*overflow_page, 1, &overflow_record, COPY)
      != S_SUCCESS)
    {
      /* Unexpected. */
      assert_release (false);
      return ER_FAILED;
    }

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, &overflow_record,
				   BTREE_OVERFLOW_NODE, NULL);
#endif /* !NDEBUG */

  if (overflow_record.length == BTREE_OBJECT_FIXED_SIZE (btid_int))
    {
      /* Only one object. */
      /* Remove page completely. */

      /* Safe guard: only first object can be deleted. */
      assert (offset_to_object == 0);

      /* Get VPID of next overflow page. */
      error_code =
	btree_get_next_overflow_vpid (*overflow_page, &next_overflow_vpid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      /* Unfix the page before deallocating. */
      pgbuf_get_vpid (*overflow_page, &overflow_vpid);
      pgbuf_unfix_and_init (thread_p, *overflow_page);

      /* Vacuum needs system operation to deallocate pages. */
      if (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT)
	{
	  if (log_start_system_op (thread_p) == NULL)
	    {
	      assert_release (false);
	      return ER_FAILED;
	    }
	  is_system_op_started = true;
	}

      /* Deallocate page. */
      error_code =
	file_dealloc_page (thread_p, &btid_int->sys_btid->vfid,
			   &overflow_vpid);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
      /* Notification. */
      BTREE_SET_DELETED_OVERFLOW_PAGE_NOTIFICATION (thread_p, NULL,
						    BTREE_DELETE_OID
						    (delete_helper),
						    BTREE_DELETE_CLASS_OID
						    (delete_helper),
						    btid_int->sys_btid);

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

      /* Update previous page link. */
      if (prev_page == leaf_page)
	{
	  /* Update leaf record link. */
	  error_code =
	    btree_modify_leaf_ovfl_vpid (thread_p, btid_int, delete_helper,
					 leaf_page, leaf_record, search_key,
					 &next_overflow_vpid);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	}
      else			/* prev_page != leaf_page. */
	{
	  /* Update link in an overflow page. */
	  error_code =
	    btree_modify_overflow_link (thread_p, btid_int, delete_helper,
					prev_page, &next_overflow_vpid);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto error;
	    }
	}

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

      /* End system operation. */
      if (is_system_op_started)
	{
	  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);
	  is_system_op_started = false;
	}
    }
  else
    {
      /* More than one object. */

      /* Just remove object from record. */
      ovf_addr.offset = 1;
      ovf_addr.pgptr = *overflow_page;
      ovf_addr.vfid = &btid_int->sys_btid->vfid;

      error_code =
	btree_record_remove_object (thread_p, btid_int, delete_helper,
				    *overflow_page, &overflow_record,
				    search_key, BTREE_OVERFLOW_NODE,
				    offset_to_object, &ovf_addr);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }

  /* Safe guard: no system op was left started. */
  assert (!is_system_op_started);

  /* Success. */
  return NO_ERROR;

error:
  if (is_system_op_started)
    {
      log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }
  assert_release (error_code != NO_ERROR);
  return error_code;
}

/*
 * btree_leaf_remove_object () - Remove an object from leaf record.
 *
 * return		    : Error code.
 * thread_p (in)	    : Thread entry.
 * btid_int (in)	    : B-tree info.
 * delete_helper (in)	    : B-tree delete helper.
 * leaf_page (in)	    : Leaf page.
 * leaf_record (in)	    : Leaf record.
 * leaf_rec_info (in)	    : Leaf record info.
 * offset_after_key (in)    : Offset in leaf record where packed key ends.
 * search_key (in)	    : Search key result.
 * offset_to_object (in)    : Offset to object being removed.
 * leaf_addr (in)	    : Log address for leaf record.
 */
static int
btree_leaf_remove_object (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
			  BTREE_DELETE_HELPER * delete_helper,
			  PAGE_PTR leaf_page, RECDES * leaf_record,
			  LEAF_REC * leaf_rec_info, int offset_after_key,
			  BTREE_SEARCH_KEY_HELPER * search_key,
			  int offset_to_object)
{
  int error_code = NO_ERROR;	/* Error code. */
  OID last_oid;			/* Last object OID. */
  OID last_class_oid;		/* Last object class OID. */
  BTREE_MVCC_INFO last_mvcc_info;	/* Last object MVCC info. */
  int offset_to_last_object = 0;	/* Offset to last object. */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (delete_helper != NULL);
  assert (leaf_page != NULL);
  assert (leaf_record != NULL);
  assert (search_key != NULL && search_key->result == BTREE_KEY_FOUND
	  && search_key->slotid > 0);
  assert (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_OBJECT
	  || delete_helper->purpose == BTREE_OP_DELETE_OBJECT_PHYSICAL
	  || delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT);

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, leaf_record,
				   BTREE_LEAF_NODE, NULL);
#endif /* NDEBUG */

  /* Remove object from leaf record. */
  if (offset_to_object == 0)
    {
      /* Object to remove is first. */

      /* Get last object in leaf. */
      error_code =
	btree_record_get_last_object (thread_p, btid_int, leaf_record,
				      BTREE_LEAF_NODE, offset_after_key,
				      &last_oid, &last_class_oid,
				      &last_mvcc_info,
				      &offset_to_last_object);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return ER_FAILED;
	}

      if (offset_to_last_object == 0)
	{
	  /* This is the only object in leaf record. */
	  /* Safe guard: first and last are the same object. */
	  assert (OID_EQ (BTREE_DELETE_OID (delete_helper), &last_oid));
	  if (VPID_ISNULL (&leaf_rec_info->ovfl))
	    {
	      /* This is the last key object! */
	      /* Remove key. */
	      error_code =
		btree_delete_key_from_leaf (thread_p, btid_int, leaf_page,
					    leaf_rec_info, delete_helper,
					    search_key);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return ER_FAILED;
		}
	      /* Key was successfully removed. */

	      /* MULTI_ROW_UPDATE will try to check key was deleted in
	       * btree_key_delete_remove_object. Don't allow it since the
	       * key no longer exists.
	       */
	      assert (!delete_helper->check_key_deleted
		      || delete_helper->is_key_deleted);
	      delete_helper->check_key_deleted = false;

	      /* Fall through. */
	    }
	  else			/* !VPID_ISNULL (&leaf_rec_info.ovfl) */
	    {
	      /* Key has overflow objects. Swap one object from first
	       * overflow page.
	       */
	      error_code =
		btree_swap_first_oid_with_ovfl_rec (thread_p, btid_int,
						    delete_helper, leaf_page,
						    search_key, leaf_record,
						    &leaf_rec_info->ovfl);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return ER_FAILED;
		}
	      /* First object was successfully replaced. Fall through. */
	    }
	}
      else			/* offset_to_last_object != 0. */
	{
	  /* Replace first object with last object. */
	  error_code =
	    btree_leaf_record_replace_first_with_last (thread_p, btid_int,
						       delete_helper,
						       leaf_page,
						       leaf_record,
						       search_key, &last_oid,
						       &last_class_oid,
						       &last_mvcc_info,
						       offset_to_last_object);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return ER_FAILED;
	    }
	}
    }
  else				/* offset_to_object != 0 */
    {
      /* Not the first object. Just remove it. */
      error_code =
	btree_record_remove_object (thread_p, btid_int, delete_helper,
				    leaf_page, leaf_record, search_key,
				    BTREE_LEAF_NODE, offset_to_object,
				    &delete_helper->leaf_addr);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return ER_FAILED;
	}
    }
  /* Success. */
  return NO_ERROR;
}

/*
 * btree_key_remove_insert_mvccid () - Remove insert MVCCID from object info.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree info.
 * key (in)	   : Key of object.
 * leaf_page (in)  : Leaf page.
 * search_key (in) : Search key result.
 * restart (in)	   : Not used.
 * other_args (in) : BTREE_DELETE_HELPER *
 */
static int
btree_key_remove_insert_mvccid (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
				DB_VALUE * key, PAGE_PTR * leaf_page,
				BTREE_SEARCH_KEY_HELPER * search_key,
				bool * restart, void *other_args)
{
  /* btree_delete_internal helper. */
  BTREE_DELETE_HELPER *delete_helper = (BTREE_DELETE_HELPER *) other_args;
  int offset_to_object = NOT_FOUND;	/* Offset to found object. */
  int error_code = NO_ERROR;	/* Error code. */
  PAGE_PTR found_page = NULL;	/* Page of found object. */
  RECDES record;		/* B-tree record. */
  /* Buffer to copy the b-tree record. */
  char record_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  LEAF_REC leaf_rec_info;	/* Leaf record info. */
  int offset_after_key = 0;	/* Offset after key in leaf record.
				 */
  bool dummy_clear_key = false;	/* Dummy. */
  int offset_to_insert_mvccid = 0;	/* Offset to the insert MVCCID being
					 * removed.
					 */
  PGSLOTID slotid;		/* Slot ID of record being updated.
				 * It is either search_key->slotid
				 * if record is from leaf or 1 if
				 * record is from overflow.
				 */
  /* MVCCID_ALL_VISIBLE */
  MVCCID all_visible_mvccid = MVCCID_ALL_VISIBLE;
  BTREE_NODE_TYPE node_type;	/* Page of found object node type. */
  char *oid_ptr = NULL;		/* Pointer to object data in record. */
  char *mvccid_ptr = NULL;	/* Pointer to insert MVCCID. */
  bool has_fixed_size = false;	/* True if object size is fixed. */
  LOG_DATA_ADDR addr;		/* Address for recovery. */

  /* Redo recovery structures. */
  char rv_redo_data_buffer[BTREE_RV_REDO_BUFFER_SIZE + BTREE_MAX_ALIGN];
  char *rv_redo_data = PTR_ALIGN (rv_redo_data_buffer, BTREE_MAX_ALIGN);
  char *rv_redo_data_ptr = rv_redo_data;
  int rv_redo_data_length = 0;
  /* NOTE: No undo logging is required to vacuum insert MVCCID. */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (leaf_page != NULL && *leaf_page != NULL
	  && pgbuf_get_latch_mode (*leaf_page) >= PGBUF_LATCH_WRITE);
  assert (search_key != NULL);
  assert (delete_helper != NULL);
  assert (delete_helper->purpose == BTREE_OP_DELETE_VACUUM_INSID);
  assert (VACUUM_IS_THREAD_VACUUM_WORKER (thread_p));

  if (search_key->result == BTREE_KEY_FOUND)
    {
      /* Key was found. Find the object. */

      /* Get leaf record. */
      record.area_size = DB_PAGESIZE;
      record.data = PTR_ALIGN (record_data_buffer, BTREE_MAX_ALIGN);
      if (spage_get_record (*leaf_page, search_key->slotid, &record,
			    COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, &record,
				       BTREE_LEAF_NODE, NULL);
#endif /* !NDEBUG */

      error_code =
	btree_read_record (thread_p, btid_int, *leaf_page, &record, NULL,
			   &leaf_rec_info, BTREE_LEAF_NODE, &dummy_clear_key,
			   &offset_after_key, PEEK_KEY_VALUE, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      /* Search object with insert MVCCID. */
      error_code =
	btree_find_oid_and_its_page (thread_p, btid_int,
				     BTREE_DELETE_OID (delete_helper),
				     *leaf_page, delete_helper->purpose,
				     &delete_helper->match_mvccid,
				     &record, &leaf_rec_info,
				     offset_after_key, &found_page, NULL,
				     &offset_to_object,
				     BTREE_DELETE_MVCC_INFO (delete_helper));
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }
  if (offset_to_object == NOT_FOUND)
    {
      /* Key or object not found. */
      /* Object must have been vacuumed/removed already. */
      vacuum_er_log (VACUUM_ER_LOG_WARNING | VACUUM_ER_LOG_BTREE
		     | VACUUM_ER_LOG_WORKER,
		     "VACUUM WARNING: Could not find object %d|%d|%d "
		     "in key=%s to vacuum it.",
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)");
      if (delete_helper->log_operations)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_DELETE: Could not find object %d|%d|%d "
			 "in key=%s to vacuum it.",
			 delete_helper->object_info.oid.volid,
			 delete_helper->object_info.oid.pageid,
			 delete_helper->object_info.oid.slotid,
			 delete_helper->printed_key != NULL ?
			 delete_helper->printed_key : "(unknown)");
	}
      return NO_ERROR;
    }
  /* Object was found. */
  node_type =
    (found_page == *leaf_page) ? BTREE_LEAF_NODE : BTREE_OVERFLOW_NODE;

  if (node_type == BTREE_OVERFLOW_NODE)
    {
      /* Get overflow record. */
      slotid = 1;
      if (spage_get_record (found_page, slotid, &record, COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, &record,
				       BTREE_OVERFLOW_NODE, NULL);
#endif /* !NDEBUG */
    }
  else
    {
      /* Leaf record was already obtained. */
      slotid = search_key->slotid;
    }
  oid_ptr = record.data + offset_to_object;

  /* It should have delete MVCCID. */
  assert (BTREE_DELETE_MVCC_INFO (delete_helper)->flags
	  == btree_record_object_get_mvcc_flags (oid_ptr));
  assert (BTREE_MVCC_INFO_IS_INSID_NOT_ALL_VISIBLE
	  (BTREE_DELETE_MVCC_INFO (delete_helper)));

  /* Where is insert MVCCID. */
  /* Skip object OID. */
  offset_to_insert_mvccid = offset_to_object + OR_OID_SIZE;

  if (BTREE_IS_UNIQUE (btid_int->unique_pk)
      && (node_type == BTREE_OVERFLOW_NODE || offset_to_object > 0
	  || btree_leaf_is_flaged (&record, BTREE_LEAF_RECORD_CLASS_OID)))
    {
      /* Also class OID is stored. */
      offset_to_insert_mvccid += OR_OID_SIZE;
    }

  /* Set insert MVCCID pointer. */
  mvccid_ptr = record.data + offset_to_insert_mvccid;

  /* Is object fixed size? */
  has_fixed_size =
    (node_type == BTREE_OVERFLOW_NODE)
    || (offset_to_object > 0 && BTREE_IS_UNIQUE (btid_int->unique_pk))
    || (offset_to_object == 0
	&& btree_leaf_is_flaged (&record, BTREE_LEAF_RECORD_OVERFLOW_OIDS));

  /* Prepare logging. */
  addr.offset = slotid;
  addr.pgptr = found_page;
  addr.vfid = &btid_int->sys_btid->vfid;

#if !defined (NDEBUG)
  /* For debugging recovery. */
  BTREE_RV_REDO_SET_DEBUG_INFO (&addr, rv_redo_data_ptr, btid_int,
				BTREE_RV_DEBUG_ID_REM_INSID);
#endif
  if (node_type == BTREE_OVERFLOW_NODE)
    {
      BTREE_RV_REDO_SET_OVERFLOW_NODE (&addr);
    }
  LOG_RV_RECORD_SET_MODIFY_MODE (&addr, LOG_RV_RECORD_UPDATE_PARTIAL);

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully executed remove "
		     "insert MVCCID for object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s, slotid=%d, "
		     "%s page=%d|%d, lsa=%lld|%d, in index (%d, %d|%d). "
		     "Insert MVCCID was %s.\n",
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     delete_helper->object_info.mvcc_info.insert_mvccid,
		     delete_helper->object_info.mvcc_info.delete_mvccid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     slotid,
		     node_type == BTREE_OVERFLOW_NODE ? "overflow" : "leaf",
		     pgbuf_get_volume_id (found_page),
		     pgbuf_get_page_id (found_page),
		     (long long int) pgbuf_get_lsa (found_page)->pageid,
		     (int) pgbuf_get_lsa (found_page)->offset,
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid,
		     has_fixed_size ? "replaced" : "removed");
    }

  /* Remove or replace delete MVCCID. */
  if (has_fixed_size)
    {
      /* Replace. */
      OR_PUT_MVCCID (mvccid_ptr, &all_visible_mvccid);

      /* Redo log replace. */
      rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (rv_redo_data_ptr,
					 offset_to_insert_mvccid,
					 OR_MVCCID_SIZE, OR_MVCCID_SIZE,
					 mvccid_ptr);
    }
  else
    {
      /* Remove. */
      RECORD_MOVE_DATA (&record, offset_to_insert_mvccid,
			offset_to_insert_mvccid + OR_MVCCID_SIZE);
      btree_record_object_clear_mvcc_flags (oid_ptr,
					    BTREE_OID_HAS_MVCC_INSID);

      /* Redo log remove MVCCID. */
      rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (rv_redo_data_ptr,
					 offset_to_insert_mvccid,
					 OR_MVCCID_SIZE, 0, NULL);
      /* Redo log clear flag. */
      rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (rv_redo_data_ptr,
					 offset_to_object + OR_OID_VOLID,
					 OR_SHORT_SIZE, OR_SHORT_SIZE,
					 oid_ptr + OR_OID_VOLID);
    }

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, &record, node_type,
				   NULL);
#endif /* !NDEBUG */

  /* Update in page. */
  if (spage_update (thread_p, found_page, slotid, &record) != SP_SUCCESS)
    {
      /* Unexpected. */
      assert_release (false);
      error_code = ER_FAILED;
      goto error;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Logging. */
  rv_redo_data_length = CAST_BUFLEN (rv_redo_data_ptr - rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  log_append_redo_data (thread_p, RVBT_RECORD_MODIFY_NO_UNDO, &addr,
			rv_redo_data_length, rv_redo_data);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, found_page, DONT_FREE);

  if (found_page != NULL && found_page != *leaf_page)
    {
      pgbuf_unfix_and_init (thread_p, found_page);
    }
  return NO_ERROR;

error:
  if (found_page != NULL && found_page != *leaf_page)
    {
      pgbuf_unfix_and_init (thread_p, found_page);
    }
  assert_release (error_code != NO_ERROR);
  return error_code;
}

/*
 * btree_key_remove_delete_mvccid () - Remove delete MVCCID from object info.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * btid_int (in)   : B-tree info.
 * key (in)	   : Key of object.
 * leaf_page (in)  : Leaf page.
 * search_key (in) : Search key result.
 * restart (in)	   : Not used.
 * other_args (in) : BTREE_DELETE_HELPER *
 */
static int
btree_key_remove_delete_mvccid (THREAD_ENTRY * thread_p, BTID_INT * btid_int,
				DB_VALUE * key, PAGE_PTR * leaf_page,
				BTREE_SEARCH_KEY_HELPER * search_key,
				bool * restart, void *other_args)
{
  /* btree_delete_internal helper. */
  BTREE_DELETE_HELPER *delete_helper = (BTREE_DELETE_HELPER *) other_args;
  int offset_to_object = NOT_FOUND;	/* Offset to found object. */
  int error_code = NO_ERROR;	/* Error code. */
  PAGE_PTR found_page = NULL;	/* Page of found object. */
  RECDES leaf_record;		/* B-tree leaf record. */
  RECDES overflow_record;	/* B-tree overflow record. */
  /* Buffers to copy b-tree records. */
  char leaf_record_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  char ovf_record_data_buffer[IO_MAX_PAGE_SIZE + BTREE_MAX_ALIGN];
  LEAF_REC leaf_rec_info;	/* Leaf leaf_record info. */
  int offset_after_key = 0;	/* Offset after key in leaf leaf_record.
				 */
  bool dummy_clear_key = false;	/* Dummy. */
  PGSLOTID slotid;		/* Slot ID of leaf_record being updated.
				 * It is either search_key->slotid
				 * if leaf_record is from leaf or 1 if
				 * leaf_record is from overflow.
				 */
  BTREE_NODE_TYPE node_type;	/* Page of found object node type. */

  /* Redo recovery structures. */
  char rv_redo_data_buffer[BTREE_RV_REDO_BUFFER_SIZE + BTREE_MAX_ALIGN];

  /* No undo logging is required during rollback. */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (key != NULL && !DB_IS_NULL (key)
	  && !btree_multicol_key_is_null (key));
  assert (leaf_page != NULL && *leaf_page != NULL
	  && pgbuf_get_latch_mode (*leaf_page) >= PGBUF_LATCH_WRITE);
  assert (search_key != NULL);
  assert (delete_helper != NULL);
  assert (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT_DELID);

  if (search_key->result == BTREE_KEY_FOUND)
    {
      /* Key was found. Try to find object. */

      /* Get leaf leaf record. */
      leaf_record.area_size = DB_PAGESIZE;
      leaf_record.data = PTR_ALIGN (leaf_record_data_buffer, BTREE_MAX_ALIGN);
      if (spage_get_record (*leaf_page, search_key->slotid, &leaf_record,
			    COPY) != S_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, &leaf_record,
				       BTREE_LEAF_NODE, NULL);
#endif /* !NDEBUG */

      error_code =
	btree_read_record (thread_p, btid_int, *leaf_page, &leaf_record, NULL,
			   &leaf_rec_info, BTREE_LEAF_NODE, &dummy_clear_key,
			   &offset_after_key, PEEK_KEY_VALUE, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      /* Find object. */
      error_code =
	btree_find_oid_and_its_page (thread_p, btid_int,
				     BTREE_DELETE_OID (delete_helper),
				     *leaf_page, delete_helper->purpose,
				     &delete_helper->match_mvccid,
				     &leaf_record, &leaf_rec_info,
				     offset_after_key, &found_page, NULL,
				     &offset_to_object,
				     BTREE_DELETE_MVCC_INFO (delete_helper));
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }
  if (offset_to_object == NOT_FOUND)
    {
      /* Key or object not found, but it should have been found. */
      assert_release (false);
      btree_set_unknown_key_error (thread_p, btid_int->sys_btid, key,
				   "btree_key_remove_delete_mvccid: "
				   "key was not found.");
      error_code = ER_BTREE_UNKNOWN_KEY;
      goto error;
    }
  /* Object was found. */

  /* Prepare logging. */
  delete_helper->rv_redo_data =
    PTR_ALIGN (rv_redo_data_buffer, BTREE_MAX_ALIGN);
  delete_helper->rv_redo_data_ptr = delete_helper->rv_redo_data;

  /* Where was object found? */
  node_type =
    (found_page == *leaf_page) ? BTREE_LEAF_NODE : BTREE_OVERFLOW_NODE;

  if (node_type == BTREE_OVERFLOW_NODE)
    {
      /* Get overflow record. */
      slotid = 1;
      overflow_record.data =
	PTR_ALIGN (ovf_record_data_buffer, BTREE_MAX_ALIGN);
      overflow_record.area_size = DB_PAGESIZE;
      if (spage_get_record (found_page, slotid, &overflow_record, COPY)
	  != S_SUCCESS)
	{
	  assert_release (false);
	  error_code = ER_FAILED;
	  goto error;
	}
#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, &overflow_record,
				       BTREE_OVERFLOW_NODE, NULL);
#endif /* !NDEBUG */
    }
  else
    {
      /* Leaf leaf_record was already obtained. */
      slotid = search_key->slotid;
    }

  if (BTREE_IS_UNIQUE (btid_int->unique_pk))
    {
      PAGE_PTR overflow_page =
	node_type == BTREE_OVERFLOW_NODE ? found_page : NULL;
      RECDES *ovf_record_p =
	node_type == BTREE_OVERFLOW_NODE ? &overflow_record : NULL;

      error_code =
	btree_key_remove_delete_mvccid_unique (thread_p, btid_int,
					       delete_helper, search_key,
					       *leaf_page, &leaf_record,
					       overflow_page, ovf_record_p,
					       node_type, offset_to_object);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }
  else
    {
      RECDES *recdes_p =
	node_type == BTREE_OVERFLOW_NODE ? &overflow_record : &leaf_record;

      error_code =
	btree_key_remove_delete_mvccid_non_unique (thread_p, btid_int,
						   delete_helper, found_page,
						   recdes_p, slotid,
						   node_type,
						   offset_to_object);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }

  if (found_page != NULL && found_page != *leaf_page)
    {
      pgbuf_unfix_and_init (thread_p, found_page);
    }
  return NO_ERROR;

error:
  if (found_page != NULL && found_page != *leaf_page)
    {
      pgbuf_unfix_and_init (thread_p, found_page);
    }
  assert_release (error_code != NO_ERROR);
  return error_code;
}

/*
 * btree_key_remove_delete_mvccid_unique () - Remove delete MVCCID from an
 *					      object in unique index as
 *					      part of undoing a MVCC delete
 *					      operation.
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree info.
 * delete_helper (in)	 : B-tree delete helper.
 * search_key (in)	 : Search key result.
 * leaf_page (in)	 : Leaf node page.
 * leaf_record (in)	 : Key's leaf record.
 * overflow_page (in)	 : Overflow node page (if object was found in overflow
 *			   page).
 * overflow_record (in)  : Overflow record (if object was found in overflow
 *			   page).
 * node_type (in)	 : Node type of page where object was found.
 * offset_to_object (in) : Offset to object in its record.
 */
static int
btree_key_remove_delete_mvccid_unique (THREAD_ENTRY * thread_p,
				       BTID_INT * btid_int,
				       BTREE_DELETE_HELPER *
				       delete_helper,
				       BTREE_SEARCH_KEY_HELPER * search_key,
				       PAGE_PTR leaf_page,
				       RECDES * leaf_record,
				       PAGE_PTR overflow_page,
				       RECDES * overflow_record,
				       BTREE_NODE_TYPE node_type,
				       int offset_to_object)
{
  int error_code = NO_ERROR;	/* Error code. */
  BTREE_OBJECT_INFO first_object;	/* Current first object in leaf
					 * record.
					 */
  LOG_DATA_ADDR leaf_addr;	/* Leaf record address for logging. */
  char *oid_ptr = NULL;		/* Pointer to found object in its
				 * record.
				 */
  int object_fixed_size;	/* Object & info size if fixed. */
  int rv_redo_data_length = 0;	/* Length of redo recovery data. */

  /* Assert expected arguments. */
  assert (btid_int != NULL && BTREE_IS_UNIQUE (btid_int->unique_pk));
  assert (delete_helper != NULL);
  assert (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT_DELID);
  assert (leaf_page != NULL);
  assert (leaf_record != NULL && leaf_record->data != NULL);
  assert (search_key != NULL && search_key->slotid > 0);
  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_OVERFLOW_NODE);
  assert (node_type == BTREE_LEAF_NODE
	  || (overflow_page != NULL && overflow_record != NULL));
  assert (offset_to_object >= 0
	  && offset_to_object < (node_type ==
				 BTREE_LEAF_NODE ? leaf_record->
				 length : overflow_record->length));
  assert (BTREE_MVCC_INFO_HAS_DELID (BTREE_DELETE_MVCC_INFO (delete_helper)));

  /* Undoing MVCC delete should consider one rule for unique index: the
   * non-dirty visible object should always be first.
   * When this object was deleted, it may have been relocated from the first
   * position in leaf record. If that's the case, we now have to move it
   * back.
   */

  if (node_type == BTREE_LEAF_NODE && offset_to_object == 0)
    {
      /* Object is still first in its record. Just remove delete MVCCID in the
       * same way a non-unique index would do.
       */
      return
	btree_key_remove_delete_mvccid_non_unique (thread_p, btid_int,
						   delete_helper, leaf_page,
						   leaf_record,
						   search_key->slotid,
						   node_type,
						   offset_to_object);
    }
  /* Not the first object in leaf record. */

  /* We need to swap the object with first object in leaf record. */
  /* Get the first object in leaf record. */
  error_code =
    btree_leaf_get_first_object (btid_int, leaf_record, &first_object.oid,
				 &first_object.class_oid,
				 &first_object.mvcc_info);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  /* Since first object is going to be relocated, it will have fixed size. */
  BTREE_MVCC_INFO_SET_FIXED_SIZE (&first_object.mvcc_info);

  /* Since our object becomes first, if there are no overflow OID's, having
   * fixed size is no longer required.
   */
  if (!btree_leaf_is_flaged (leaf_record, BTREE_LEAF_RECORD_OVERFLOW_OIDS))
    {
      /* Clear unnecessary MVCC info. */
      BTREE_MVCC_INFO_CLEAR_FIXED_SIZE
	(BTREE_DELETE_MVCC_INFO (delete_helper));
    }
  /* Remove delete MVCCID from object. */
  BTREE_MVCC_INFO_CLEAR_DELID (BTREE_DELETE_MVCC_INFO (delete_helper));

  /* Object are ready to be swapped. */

  /* Prepare logging for leaf record. */
  leaf_addr.offset = search_key->slotid;
  leaf_addr.pgptr = leaf_page;
  leaf_addr.vfid = &btid_int->sys_btid->vfid;

#if !defined (NDEBUG)
  BTREE_RV_REDO_SET_DEBUG_INFO (&leaf_addr, delete_helper->rv_redo_data_ptr,
				btid_int, BTREE_RV_DEBUG_ID_REM_DELID_UNIQUE);
#endif /* !NDEBUG */
  LOG_RV_RECORD_SET_MODIFY_MODE (&leaf_addr, LOG_RV_RECORD_UPDATE_PARTIAL);

  /* Where was object found? */
  if (node_type == BTREE_LEAF_NODE)
    {
      /* Leaf record. */
      /* Replace our object with first object. */
      oid_ptr = leaf_record->data + offset_to_object;
      object_fixed_size = BTREE_OBJECT_FIXED_SIZE (btid_int);

      /* Objects are fixed size, therefore replacing data is the same size.
       * Just pack first object info where our object used to be.
       */
      (void) btree_pack_object (oid_ptr, btid_int, BTREE_LEAF_NODE,
				leaf_record, &first_object.oid,
				&first_object.class_oid,
				&first_object.mvcc_info);
#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, leaf_record,
				       node_type, NULL);
#endif /* !NDEBUG */

      /* Add redo recovery data for leaf record. */
      delete_helper->rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (delete_helper->rv_redo_data_ptr,
					 offset_to_object,
					 object_fixed_size,
					 object_fixed_size, oid_ptr);

      if (delete_helper->log_operations)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_DELETE: Successfully swapped "
			 "object %d|%d|%d, class_oid %d|%d|%d, "
			 "mvcc_info=%llu|%llu into leaf page %d|%d, "
			 "lsa=%lld|%d, in key=%s, in index (%d, %d|%d).\n",
			 first_object.oid.volid, first_object.oid.pageid,
			 first_object.oid.slotid,
			 first_object.class_oid.volid,
			 first_object.class_oid.pageid,
			 first_object.class_oid.slotid,
			 (unsigned long long int)
			 first_object.mvcc_info.insert_mvccid,
			 (unsigned long long int)
			 first_object.mvcc_info.delete_mvccid,
			 pgbuf_get_volume_id (leaf_page),
			 pgbuf_get_page_id (leaf_page),
			 (long long int)
			 pgbuf_get_lsa (leaf_page)->pageid,
			 (int) pgbuf_get_lsa (leaf_page)->offset,
			 delete_helper->printed_key != NULL ?
			 delete_helper->printed_key : "(unknown)",
			 btid_int->sys_btid->root_pageid,
			 btid_int->sys_btid->vfid.volid,
			 btid_int->sys_btid->vfid.fileid);
	}
    }
  else
    {
      /* Swap first object to overflow page and replace it with our object. */
      error_code =
	btree_overflow_record_replace_object (thread_p, btid_int,
					      delete_helper, overflow_page,
					      overflow_record,
					      offset_to_object,
					      &first_object);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Replace first object. */
  btree_leaf_change_first_object (leaf_record, btid_int,
				  BTREE_DELETE_OID (delete_helper),
				  BTREE_DELETE_CLASS_OID (delete_helper),
				  BTREE_DELETE_MVCC_INFO (delete_helper),
				  NULL, &delete_helper->rv_redo_data_ptr);
  /* Update in page. */
  if (spage_update (thread_p, leaf_page, search_key->slotid, leaf_record)
      != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully removed delete MVCCID %llu "
		     "and moved object %d|%d|%d, class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu first in leaf record, "
		     "leaf_page=%d|%d, lsa=%lld|%d, slotid=%d, key=%s, "
		     "in index (%d, %d|%d).\n",
		     (unsigned long long int)
		     delete_helper->match_mvccid,
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     (unsigned long long int)
		     BTREE_MVCC_INFO_INSID
		     (&delete_helper->object_info.mvcc_info),
		     (unsigned long long int)
		     BTREE_MVCC_INFO_DELID
		     (&delete_helper->object_info.mvcc_info),
		     pgbuf_get_volume_id (leaf_page),
		     pgbuf_get_page_id (leaf_page),
		     (long long int) pgbuf_get_lsa (leaf_page)->pageid,
		     (int) pgbuf_get_lsa (leaf_page)->offset,
		     search_key->slotid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid);
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Add compensate log. */
  rv_redo_data_length =
    CAST_BUFLEN (delete_helper->rv_redo_data_ptr
		 - delete_helper->rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			 pgbuf_get_vpid_ptr (leaf_page), leaf_addr.offset,
			 leaf_page, rv_redo_data_length,
			 delete_helper->rv_redo_data,
			 LOG_FIND_CURRENT_TDES (thread_p));
  pgbuf_set_dirty (thread_p, leaf_page, DONT_FREE);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Success. */
  return NO_ERROR;
}

/*
 * btree_key_remove_delete_mvccid_non_unique () - Remove delete MVCCID from an
 *						  index object as part of
 *						  undoing a MVCC delete
 *						  operation.
 *
 * return		 : Error code.
 * thread_p (in)	 : Thread entry.
 * btid_int (in)	 : B-tree info.
 * delete_helper (in)	 : B-tree delete helper.
 * page (in)		 : Leaf or overflow page.
 * record (in)		 : Leaf or overflow record.
 * slotid (in)		 : Slot ID of record.
 * node_type (in)	 : BTREE_LEAF_NODE or BTREE_OVERFLOW_NODE.
 * offset_to_object (in) : Offset to object in its record.
 *
 * NOTE: Even though this function is targeted for non-unique indexes, it can
 *	 be used in one case for unique indexes: when the object being undone
 *	 is already first in leaf record and does not require relocation.
 */
static int
btree_key_remove_delete_mvccid_non_unique (THREAD_ENTRY * thread_p,
					   BTID_INT * btid_int,
					   BTREE_DELETE_HELPER *
					   delete_helper,
					   PAGE_PTR page,
					   RECDES * record,
					   PGSLOTID slotid,
					   BTREE_NODE_TYPE node_type,
					   int offset_to_object)
{
  char *mvccid_ptr = NULL;	/* Pointer to delete MVCCID being
				 * removed or replaced.
				 */
  char *oid_ptr = NULL;		/* Pointer to object being changed. */
  MVCCID null_mvccid = MVCCID_NULL;	/* MVCCID_NULL used to replace old
					 * MVCCID when object size is fixed.
					 */
  bool has_fixed_size = false;	/* Set to true if object must have
				 * fixed size.
				 */
  int offset_to_delete_mvccid;	/* Offset to MVCCID being removed or
				 * replaced.
				 */
  LOG_DATA_ADDR addr;		/* Log address for record. */
  int rv_redo_data_length = 0;	/* Redo recovery data length. */

  /* Assert expected arguments. */
  assert (btid_int != NULL);
  assert (delete_helper != NULL);
  assert (delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT_DELID);
  assert (page != NULL);
  assert (record != NULL && record->data != NULL);
  assert (slotid > 0);
  assert (node_type == BTREE_LEAF_NODE || node_type == BTREE_OVERFLOW_NODE);
  assert (offset_to_object >= 0 && offset_to_object < record->length);
  assert (!BTREE_IS_UNIQUE (btid_int->unique_pk)
	  || (node_type == BTREE_LEAF_NODE && offset_to_object == 0));

  /* Set object OID pointer (to change MVCC flags). */
  oid_ptr = record->data + offset_to_object;

  /* Object has fixed size if:
   * 1. It belongs to overflow node.
   * 2. It is first in leaf record and key has overflow objects.
   * Third case of non-first in unique key leaf record cannot happen here.
   */
  if (node_type == BTREE_OVERFLOW_NODE
      || (offset_to_object == 0
	  && btree_leaf_is_flaged (record, BTREE_LEAF_RECORD_OVERFLOW_OIDS)))
    {
      has_fixed_size = true;
    }

  /* Compute offset to delete MVCCID. */
  /* Start with offset_to_object. */
  offset_to_delete_mvccid = offset_to_object;
  /* OID is always saved. */
  offset_to_delete_mvccid += OR_OID_SIZE;

  if (BTREE_IS_UNIQUE (btid_int->unique_pk)
      && btree_leaf_is_flaged (record, BTREE_LEAF_RECORD_CLASS_OID))
    {
      /* Class OID is also saved. */
      /* Safe guard. */
      assert (node_type == BTREE_LEAF_NODE && offset_to_object == 0);
      offset_to_delete_mvccid += OR_OID_SIZE;
    }
  if (has_fixed_size
      || BTREE_MVCC_INFO_HAS_INSID (BTREE_DELETE_MVCC_INFO (delete_helper)))
    {
      /* Insert MVCCID is also saved. */
      offset_to_delete_mvccid += OR_MVCCID_SIZE;
    }
  /* Set MVCCID pointer. */
  mvccid_ptr = record->data + offset_to_delete_mvccid;

  /* Prepare logging before starting changes. */
  addr.offset = slotid;
  addr.pgptr = page;
  addr.vfid = &btid_int->sys_btid->vfid;

#if !defined (NDEBUG)
  /* For debugging recovery. */
  BTREE_RV_REDO_SET_DEBUG_INFO (&addr, delete_helper->rv_redo_data_ptr,
				btid_int,
				BTREE_RV_DEBUG_ID_REM_DELID_NON_UNIQUE);
#endif /* !NDEBUG */
  if (node_type == BTREE_OVERFLOW_NODE)
    {
      BTREE_RV_REDO_SET_OVERFLOW_NODE (&addr);
    }
  LOG_RV_RECORD_SET_MODIFY_MODE (&addr, LOG_RV_RECORD_UPDATE_PARTIAL);

  /* Remove or replace delete MVCCID. */
  if (has_fixed_size)
    {
      /* Replace. */
      OR_PUT_MVCCID (mvccid_ptr, &null_mvccid);

      /* Redo logging: replace MVCCID. */
      delete_helper->rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (delete_helper->rv_redo_data_ptr,
					 offset_to_delete_mvccid,
					 OR_MVCCID_SIZE, OR_MVCCID_SIZE,
					 mvccid_ptr);
    }
  else
    {
      /* Remove. */
      RECORD_MOVE_DATA (record, offset_to_delete_mvccid,
			offset_to_delete_mvccid + OR_MVCCID_SIZE);
      btree_record_object_clear_mvcc_flags (oid_ptr,
					    BTREE_OID_HAS_MVCC_DELID);

      /* Redo logging: remove MVCCID. */
      delete_helper->rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (delete_helper->rv_redo_data_ptr,
					 offset_to_delete_mvccid,
					 OR_MVCCID_SIZE, 0, NULL);
      /* Redo logging: clear flag. */
      delete_helper->rv_redo_data_ptr =
	log_rv_pack_redo_record_changes (delete_helper->rv_redo_data_ptr,
					 offset_to_object + OR_OID_VOLID,
					 OR_SHORT_SIZE, OR_SHORT_SIZE,
					 oid_ptr + OR_OID_VOLID);
    }

#if !defined (NDEBUG)
  (void) btree_check_valid_record (thread_p, btid_int, record, node_type,
				   NULL);
#endif /* !NDEBUG */

  /* Update in page. */
  if (spage_update (thread_p, page, slotid, record) != SP_SUCCESS)
    {
      /* Unexpected. */
      assert_release (false);
      return ER_FAILED;
    }

  if (delete_helper->log_operations)
    {
      _er_log_debug (ARG_FILE_LINE,
		     "BTREE_DELETE: Successfully executed remove "
		     "delete MVCCID %llu for object %d|%d|%d, "
		     "class_oid %d|%d|%d, "
		     "mvcc_info=%llu|%llu and key=%s, slotid=%d, "
		     "%s page=%d|%d, lsa=%lld|%d, in index (%d, %d|%d). "
		     "Delete MVCCID was %s.\n",
		     (unsigned long long int)
		     delete_helper->match_mvccid,
		     delete_helper->object_info.oid.volid,
		     delete_helper->object_info.oid.pageid,
		     delete_helper->object_info.oid.slotid,
		     delete_helper->object_info.class_oid.volid,
		     delete_helper->object_info.class_oid.pageid,
		     delete_helper->object_info.class_oid.slotid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.insert_mvccid,
		     (unsigned long long int)
		     delete_helper->object_info.mvcc_info.delete_mvccid,
		     delete_helper->printed_key != NULL ?
		     delete_helper->printed_key : "(unknown)",
		     slotid,
		     node_type == BTREE_OVERFLOW_NODE ? "overflow" : "leaf",
		     pgbuf_get_volume_id (page), pgbuf_get_page_id (page),
		     (long long int) pgbuf_get_lsa (page)->pageid,
		     (int) pgbuf_get_lsa (page)->offset,
		     btid_int->sys_btid->root_pageid,
		     btid_int->sys_btid->vfid.volid,
		     btid_int->sys_btid->vfid.fileid,
		     has_fixed_size ? "replaced" : "removed");
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Add logging. */
  rv_redo_data_length =
    CAST_BUFLEN (delete_helper->rv_redo_data_ptr
		 - delete_helper->rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  log_append_compensate (thread_p, RVBT_RECORD_MODIFY_COMPENSATE,
			 pgbuf_get_vpid_ptr (page), addr.offset,
			 page, rv_redo_data_length,
			 delete_helper->rv_redo_data,
			 LOG_FIND_CURRENT_TDES (thread_p));

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  pgbuf_set_dirty (thread_p, page, DONT_FREE);
  return NO_ERROR;
}

/*
 * btree_overflow_record_replace_object () - Replace an object from an
 *					     overflow record with another
 *					     object. Part of remove MVCCID
 *					     algorithm for unique indexes.
 *
 * return			  : Error code.
 * thread_p (in)		  : Thread entry.
 * btid_int (in)		  : B-tree info.
 * delete_helper (in)		  : B-tree delete helper.
 * overflow_page (in)		  : Overflow page.
 * overflow_record (in)		  : Overflow record.
 * offset_to_replaced_object (in) : Offset to object being replaced.
 * replacing_object (in)	  : Object info for replacement.
 */
static int
btree_overflow_record_replace_object (THREAD_ENTRY * thread_p,
				      BTID_INT * btid_int,
				      BTREE_DELETE_HELPER * delete_helper,
				      PAGE_PTR overflow_page,
				      RECDES * overflow_record,
				      int offset_to_replaced_object,
				      BTREE_OBJECT_INFO * replacing_object)
{
  /* Size of overflow object (it is fixed). */
  int object_fixed_size = BTREE_OBJECT_FIXED_SIZE (btid_int);

  /* Redo recovery data. */
  LOG_DATA_ADDR overflow_addr;
  char rv_redo_data_buffer[BTREE_RV_REDO_BUFFER_SIZE + BTREE_MAX_ALIGN];
  char *rv_redo_data = PTR_ALIGN (rv_redo_data_buffer, BTREE_MAX_ALIGN);
  char *rv_redo_data_ptr = rv_redo_data;
  int rv_redo_data_length = 0;

  /* Assert expected arguments. */
  assert (btid_int != NULL && BTREE_IS_UNIQUE (btid_int->unique_pk));
  assert (delete_helper != NULL
	  && delete_helper->purpose == BTREE_OP_DELETE_UNDO_INSERT_DELID);
  assert (overflow_page != NULL);
  assert (overflow_record != NULL);
  assert (offset_to_replaced_object >= 0
	  && offset_to_replaced_object < overflow_record->length);
  assert (replacing_object != NULL
	  && BTREE_MVCC_INFO_HAS_INSID (&replacing_object->mvcc_info)
	  && BTREE_MVCC_INFO_HAS_DELID (&replacing_object->mvcc_info));

  /* Prepare logging. */
  overflow_addr.offset = 1;
  overflow_addr.pgptr = overflow_page;
  overflow_addr.vfid = &btid_int->sys_btid->vfid;

#if !defined (NDEBUG)
  BTREE_RV_REDO_SET_DEBUG_INFO (&overflow_addr, rv_redo_data_ptr, btid_int,
				BTREE_RV_DEBUG_ID_OVF_REPLACE);
#endif /* !NDEBUG */
  BTREE_RV_REDO_SET_OVERFLOW_NODE (&overflow_addr);

  if (overflow_record->length == object_fixed_size)
    {
      /* Only one object. Just replace it. */
      assert (offset_to_replaced_object == 0);
      LOG_RV_RECORD_SET_MODIFY_MODE (&overflow_addr,
				     LOG_RV_RECORD_UPDATE_ALL);

      (void) btree_pack_object (overflow_record->data, btid_int,
				BTREE_OVERFLOW_NODE, overflow_record,
				&replacing_object->oid,
				&replacing_object->class_oid,
				&replacing_object->mvcc_info);
#if !defined (NDEBUG)
      (void) btree_check_valid_record (thread_p, btid_int, overflow_record,
				       BTREE_OVERFLOW_NODE, NULL);
#endif /* !NDEBUG */

      /* Add the change to redo logging. */
      memcpy (rv_redo_data_ptr, overflow_record->data,
	      overflow_record->length);
      rv_redo_data_ptr += overflow_record->length;

      if (spage_update (thread_p, overflow_page, 1, overflow_record)
	  != SP_SUCCESS)
	{
	  assert_release (false);
	  return ER_FAILED;
	}

      if (delete_helper->log_operations)
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "BTREE_DELETE: Successfully swapped "
			 "object %d|%d|%d, class_oid %d|%d|%d, "
			 "mvcc_info=%llu|%llu into overflow page %d|%d, "
			 "lsa=%lld|%d, in key=%s, in index (%d, %d|%d).\n",
			 replacing_object->oid.volid,
			 replacing_object->oid.pageid,
			 replacing_object->oid.slotid,
			 replacing_object->class_oid.volid,
			 replacing_object->class_oid.pageid,
			 replacing_object->class_oid.slotid,
			 (unsigned long long int)
			 replacing_object->mvcc_info.insert_mvccid,
			 (unsigned long long int)
			 replacing_object->mvcc_info.delete_mvccid,
			 pgbuf_get_volume_id (overflow_page),
			 pgbuf_get_page_id (overflow_page),
			 (long long int)
			 pgbuf_get_lsa (overflow_page)->pageid,
			 (int) pgbuf_get_lsa (overflow_page)->offset,
			 delete_helper->printed_key != NULL ?
			 delete_helper->printed_key : "(unknown)",
			 btid_int->sys_btid->root_pageid,
			 btid_int->sys_btid->vfid.volid,
			 btid_int->sys_btid->vfid.fileid);
	}

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

      /* Log changes (only redo logging). */
      rv_redo_data_length = CAST_BUFLEN (rv_redo_data_ptr - rv_redo_data);
      assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
      log_append_redo_data (thread_p, RVBT_RECORD_MODIFY_NO_UNDO,
			    &overflow_addr, rv_redo_data_length,
			    rv_redo_data);
      pgbuf_set_dirty (thread_p, overflow_page, DONT_FREE);

      FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

      return NO_ERROR;
    }
  /* More than one object. */

  /* Remove existing and then insert replacement object. */
  LOG_RV_RECORD_SET_MODIFY_MODE (&overflow_addr,
				 LOG_RV_RECORD_UPDATE_PARTIAL);

  /* Remove existing. */
  RECORD_MOVE_DATA (overflow_record, offset_to_replaced_object,
		    offset_to_replaced_object + object_fixed_size);
  /* Redo logging. */
  rv_redo_data_ptr =
    log_rv_pack_redo_record_changes (rv_redo_data_ptr,
				     offset_to_replaced_object,
				     object_fixed_size, 0, NULL);

  /* Insert new object. */
  btree_insert_object_ordered_by_oid (overflow_record, btid_int,
				      replacing_object, &rv_redo_data_ptr);

  /* Update page. */
  if (spage_update (thread_p, overflow_page, 1, overflow_record)
      != SP_SUCCESS)
    {
      assert_release (false);
      return ER_FAILED;
    }

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  /* Add redo logging. */
  rv_redo_data_length = CAST_BUFLEN (rv_redo_data_ptr - rv_redo_data);
  assert (rv_redo_data_length <= BTREE_RV_REDO_BUFFER_SIZE);
  log_append_redo_data (thread_p, RVBT_RECORD_MODIFY_NO_UNDO, &overflow_addr,
			rv_redo_data_length, rv_redo_data);
  pgbuf_set_dirty (thread_p, overflow_page, DONT_FREE);

  FI_TEST (thread_p, FI_TEST_BTREE_MANAGER_RANDOM_EXIT, 0);

  return NO_ERROR;
}
